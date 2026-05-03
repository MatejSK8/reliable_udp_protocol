#include "RFTClient.hpp"

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

void RFTClient::set_recv_timeout(double seconds)
{
    if (seconds < 0.001)
        seconds = 0.001;
    struct timeval tv{};
    tv.tv_sec = static_cast<time_t>(seconds);
    tv.tv_usec = static_cast<suseconds_t>((seconds - tv.tv_sec) * 1e6);
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

void RFTClient::update_rtt(double sample)
{
    if (srtt < 0)
    {
        srtt = sample;
        rttvar = sample / 2.0;
    }
    else
    {
        rttvar = 0.75 * rttvar + 0.25 * std::fabs(srtt - sample);
        srtt = 0.875 * srtt + 0.125 * sample;
    }
    rto = srtt + 4.0 * rttvar;
    if (rto < 0.01)
        rto = 0.01;
    if (rto > 60.0)
        rto = 60.0;
}

RFTClient::RFTClient(const Args &args)
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

void RFTClient::run(const Args &args)
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
                    break;

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

        case State::SEND_FIN:
        {
            send_pdu(sock, reinterpret_cast<sockaddr *>(&dest_addr), dest_len, syn_pdu.conn_id, FLAG_FIN, 0, 0, nullptr, 0);
            std::cerr << "[client] FIN sent\n";
            set_recv_timeout(rto);
            current_state = State::WAIT_FIN_ACK;
            break;
        }
        case State::WAIT_FIN_ACK:
        {
            sender_len = sizeof(sender);
            ssize_t n = recvfrom(sock, buf, sizeof(buf), 0,
                                 reinterpret_cast<sockaddr *>(&sender), &sender_len);
            if (n < 0)
            {
                if (g_stop)
                    break;
                std::cerr << "[client] timeout, resending FIN\n";
                current_state = State::SEND_FIN;
                break;
            }
            {
                if (!validate_pdu(buf, n))
                    break;
                PduHeader *hdr = reinterpret_cast<PduHeader *>(buf);
                if (hdr->conn_id == syn_pdu.conn_id && hdr->flags == (FLAG_FIN | FLAG_ACK))
                {
                    std::cerr << "[client] FIN-ACK received\n";
                    last_progress = clock::now();
                    current_state = State::DONE;
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

RFTClient::~RFTClient()
{
    if (sock >= 0)
        close(sock);
    if (input_file && input_file != stdin)
        fclose(input_file);
}
