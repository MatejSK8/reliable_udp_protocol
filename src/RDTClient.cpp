/**
 * @file RDTClient.cpp
 * @brief RDTClient implementation — handshake, data send loop, retransmit, teardown
 * @author xmikusm00
 */

#include "RDTClient.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <unistd.h>
#include <sys/socket.h>

#include "globals.hpp"
#include "protocol.hpp"
#include "socket_utils.hpp"

RDTClient::RDTClient(const Args &args)
{
    input_file = args.input.empty() || args.input == "-" ? stdin : fopen(args.input.c_str(), "rb");

    next_seq = static_cast<uint32_t>(rand());

    for (int i = 0; i < WINDOW_SIZE; i++)
    {
        window[i].in_use = false;
        window[i].acked = false;
    }

    struct addrinfo hints{}, *res;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICSERV;

    std::string port_str = std::to_string(args.port);
    getaddrinfo(args.address_host.c_str(), port_str.c_str(), &hints, &res);

    for (struct addrinfo *p = res; p != nullptr; p = p->ai_next)
    {
        sock = socket(p->ai_family, p->ai_socktype, 0);
        if (sock >= 0)
        {
            std::memcpy(&dest_addr, p->ai_addr, p->ai_addrlen);
            dest_len = p->ai_addrlen;
            set_recv_timeout(rto);
            break;
        }
    }

    freeaddrinfo(res);
}

void RDTClient::run(const Args &args)
{
    char buf[MAX_PDU_SIZE]{};
    sockaddr_storage sender{};
    socklen_t sender_len = sizeof(sender);
    using clock = std::chrono::steady_clock;
    auto last_progress = clock::now();

    while (!g_stop && current_state != State::DONE)
    {
        if (std::chrono::duration_cast<std::chrono::seconds>(clock::now() - last_progress).count() >= args.timeout)
        {
            std::cerr << "[client] no progress for " << args.timeout << "s, terminating\n";
            exit(1);
        }
        switch (current_state)
        {
        // --- Session establishment — implemented per RFC 9293 §3.4 (3-way handshake) ---
        case State::SEND_SYN:
        {
            syn_pdu.conn_id = static_cast<uint32_t>(rand());
            syn_pdu.seq = next_seq;
            syn_send_time = clock::now();
            send_pdu(sock, reinterpret_cast<sockaddr *>(&dest_addr), dest_len, syn_pdu.conn_id, FLAG_SYN, syn_pdu.seq, 0, nullptr, 0);
            current_state = State::WAIT_SYNACK;
            break;
        }
        case State::WAIT_SYNACK:
        {
            sender_len = sizeof(sender);
            ssize_t n = recvfrom(sock, buf, sizeof(buf), 0,
                                 reinterpret_cast<sockaddr *>(&sender), &sender_len);
            if (n < 0)
            {
                if (g_stop)
                    break;
                std::cerr << "[client] timeout, resending SYN\n";
                syn_send_time = clock::now();
                send_pdu(sock, reinterpret_cast<sockaddr *>(&dest_addr), dest_len, syn_pdu.conn_id, FLAG_SYN, syn_pdu.seq, 0, nullptr, 0);
                break;
            }
            {
                if (!validate_pdu(buf, n))
                    break;
                PduHeader *hdr = reinterpret_cast<PduHeader *>(buf);
                if (hdr->conn_id == syn_pdu.conn_id && hdr->flags == (FLAG_SYN | FLAG_ACK))
                {
                    double rtt = std::chrono::duration<double>(clock::now() - syn_send_time).count();
                    update_rtt(rtt);
                    set_recv_timeout(rto);
                    std::cerr << "[client] SYN-ACK received, RTT=" << rtt << "s RTO=" << rto << "s\n";
                    send_pdu(sock, reinterpret_cast<sockaddr *>(&dest_addr), dest_len, syn_pdu.conn_id, FLAG_ACK, 0, hdr->seq + 1, nullptr, 0);
                    last_progress = clock::now();
                    current_state = State::DATA_TRANSFER;
                }
            }
            break;
        }
        // --- Data transfer — sliding window (RFC 9293 §3.7), Go-Back-N retransmit (Kurose & Ross ch.3),
        //     fast retransmit on 3 duplicate ACKs (RFC 5681 §3.2), cumulative ACKs (RFC 9293 §3.3) ---
        case State::DATA_TRANSFER:
        {
            auto now = clock::now();
            auto rto_dur = std::chrono::duration<double>(rto);

            if (window[window_start].in_use && (now - window[window_start].send_time) >= rto_dur)
            {
                for (int i = 0; i < WINDOW_SIZE; i++)
                {
                    int idx = (window_start + i) % WINDOW_SIZE;
                    if (!window[idx].in_use)
                        break;
                    send_pdu(sock, reinterpret_cast<sockaddr *>(&dest_addr), dest_len,
                             syn_pdu.conn_id, FLAG_DATA, window[idx].seq, 0,
                             window[idx].data, window[idx].len);
                    window[idx].send_time = now;
                    window[idx].retransmitted = true;
                    std::cerr << "[client] retransmit seq=" << window[idx].seq << "\n";
                }
            }

            while (!eof_reached)
            {
                int count = 0;
                for (int i = 0; i < WINDOW_SIZE; i++)
                    if (window[i].in_use)
                        count++;
                if (count >= WINDOW_SIZE)
                    break;
                int free_slot = (window_start + count) % WINDOW_SIZE;

                size_t bytes = fread(window[free_slot].data, 1, MAX_PAYLOAD_SIZE, input_file);
                if (bytes == 0)
                {
                    eof_reached = true;
                    break;
                }

                window[free_slot].seq = next_seq;
                window[free_slot].len = bytes;
                window[free_slot].in_use = true;
                window[free_slot].acked = false;
                window[free_slot].retransmitted = false;
                window[free_slot].send_time = clock::now();

                send_pdu(sock, reinterpret_cast<sockaddr *>(&dest_addr), dest_len,
                         syn_pdu.conn_id, FLAG_DATA, window[free_slot].seq, 0,
                         window[free_slot].data, bytes);
                next_seq += bytes;
                std::cerr << "[client] sent seq=" << window[free_slot].seq << " len=" << bytes << "\n";
            }

            bool any_in_use = false;
            for (int i = 0; i < WINDOW_SIZE; i++)
                if (window[i].in_use)
                {
                    any_in_use = true;
                    break;
                }

            if (eof_reached && !any_in_use)
            {
                current_state = State::SEND_FIN;
                break;
            }

            if (any_in_use)
            {
                double elapsed = std::chrono::duration<double>(clock::now() - window[window_start].send_time).count();
                double remaining = rto - elapsed;
                set_recv_timeout(remaining > 0.001 ? remaining : 0.001);
            }
            while (true)
            {
                sender_len = sizeof(sender);
                ssize_t n = recvfrom(sock, buf, sizeof(buf), MSG_DONTWAIT,
                                     reinterpret_cast<sockaddr *>(&sender), &sender_len);

                if (n < 0)
                    break;
                if (!validate_pdu(buf, n))
                    continue;
                PduHeader *hdr = reinterpret_cast<PduHeader *>(buf);
                if (hdr->conn_id != syn_pdu.conn_id || hdr->flags != FLAG_ACK)
                    continue;

                uint32_t cum_ack = hdr->ack;
                std::cerr << "[client] ACK cum=" << cum_ack << "\n";
                if (cum_ack == highest_cumulative_ack)
                {
                    dup_ack_count++;
                    if (dup_ack_count == 3)
                    {
                        std::cerr << "[client] 3 duplicate ACKs, fast retransmit seq=" << cum_ack << "\n";

                        send_pdu(sock, reinterpret_cast<sockaddr *>(&dest_addr), dest_len,
                                 syn_pdu.conn_id, FLAG_DATA, window[window_start].seq, 0,
                                 window[window_start].data, window[window_start].len);
                        window[window_start].send_time = clock::now();
                        window[window_start].retransmitted = true;
                        dup_ack_count = 0;
                    }
                }
                else if (cum_ack > highest_cumulative_ack || highest_cumulative_ack == 0)
                {
                    highest_cumulative_ack = cum_ack;
                    dup_ack_count = 0;
                    while (window[window_start].in_use &&
                           (window[window_start].seq + static_cast<uint32_t>(window[window_start].len)) <= cum_ack)
                    {
                        if (!window[window_start].retransmitted)
                        {
                            double rtt = std::chrono::duration<double>(clock::now() - window[window_start].send_time).count();
                            update_rtt(rtt);
                        }
                        window[window_start].in_use = false;
                        window_start = (window_start + 1) % WINDOW_SIZE;
                        last_progress = clock::now();

                        if (rto > 2.0)
                            rto = 2.0;
                    }
                }
            }
            break;
        }

        // --- Session teardown — implemented per RFC 9293 §3.6 (4-way FIN exchange, active closer) ---
        case State::SEND_FIN:
        {
            last_progress = clock::now();
            send_pdu(sock, reinterpret_cast<sockaddr *>(&dest_addr), dest_len, syn_pdu.conn_id, FLAG_FIN, next_seq, 0, nullptr, 0);
            std::cerr << "[client][SEND_FIN] FIN sent\n";
            set_recv_timeout(rto);
            current_state = State::FIN_WAIT_1;
            break;
        }
        case State::FIN_WAIT_1:
        {
            sender_len = sizeof(sender);
            ssize_t n = recvfrom(sock, buf, sizeof(buf), 0,
                                 reinterpret_cast<sockaddr *>(&sender), &sender_len);
            if (n < 0)
            {
                if (g_stop)
                    break;
                std::cerr << "[client][FIN_WAIT1] timeout, resending FIN\n";
                current_state = State::SEND_FIN;
                break;
            }
            {
                if (!validate_pdu(buf, n))
                    break;
                PduHeader *hdr = reinterpret_cast<PduHeader *>(buf);
                if (hdr->conn_id == syn_pdu.conn_id && hdr->flags == FLAG_ACK)
                {
                    if (hdr->ack == next_seq + 1)
                    {
                        std::cerr << "[client][FIN_WAIT1] FIN-ACK received\n";
                        last_progress = clock::now();
                        current_state = State::FIN_WAIT_2;
                    }
                }
                else if (hdr->conn_id == syn_pdu.conn_id && hdr->flags == FLAG_FIN)
                {
                    std::cerr << "[client][FIN_WAIT1] FIN received (simultaneous close), sending ACK\n";
                    send_pdu(sock, reinterpret_cast<sockaddr *>(&dest_addr), dest_len, syn_pdu.conn_id, FLAG_ACK, 0, hdr->seq + 1, nullptr, 0);
                    last_progress = clock::now();
                    current_state = State::TIME_WAIT;
                }
            }
            break;
        }
        case State::FIN_WAIT_2:
        {
            sender_len = sizeof(sender);
            ssize_t n = recvfrom(sock, buf, sizeof(buf), 0,
                                 reinterpret_cast<sockaddr *>(&sender), &sender_len);
            if (n < 0)
            {
                if (g_stop)
                    break;

                break;
            }
            {
                if (!validate_pdu(buf, n))
                    break;
                PduHeader *hdr = reinterpret_cast<PduHeader *>(buf);
                if (hdr->conn_id == syn_pdu.conn_id && hdr->flags == (FLAG_FIN))
                {
                    std::cerr << "[client][FIN_WAIT2] FIN received\n";
                    last_progress = clock::now();
                    send_pdu(sock, reinterpret_cast<sockaddr *>(&dest_addr), dest_len, syn_pdu.conn_id, FLAG_ACK, 0, 0, nullptr, 0);
                    current_state = State::TIME_WAIT;
                }
            }
            break;
        }
        // TIME_WAIT — implemented per RFC 9293 §3.6.1 (linger 2×RTO to absorb delayed FIN retransmissions)
        case State::TIME_WAIT:
        {
            if (std::chrono::duration_cast<std::chrono::duration<double>>(clock::now() - last_progress).count() >= 2 * rto)
            {
                std::cerr << "[client][TIME_WAIT] TIME_WAIT expired, closing connection\n";
                current_state = State::DONE;
                break;
            }
            sender_len = sizeof(sender);
            ssize_t n = recvfrom(sock, buf, sizeof(buf), 0,
                                 reinterpret_cast<sockaddr *>(&sender), &sender_len);
            if (n < 0)
                break;
            if (!validate_pdu(buf, n))
                break;
            {
                PduHeader *hdr = reinterpret_cast<PduHeader *>(buf);
                if (hdr->conn_id == syn_pdu.conn_id && hdr->flags == FLAG_FIN)
                {
                    std::cerr << "[client][TIME_WAIT] FIN retransmission in TIME_WAIT, resending ACK\n";
                    send_pdu(sock, reinterpret_cast<sockaddr *>(&dest_addr), dest_len,
                             syn_pdu.conn_id, FLAG_ACK, 0, hdr->seq + 1, nullptr, 0);
                    last_progress = clock::now();
                }
            }
            break;
        }
        case State::DONE:
            break;
        default:
            break;
        }
    }
}

RDTClient::~RDTClient()
{
    if (sock >= 0)
        close(sock);
    if (input_file && input_file != stdin)
        fclose(input_file);
}
