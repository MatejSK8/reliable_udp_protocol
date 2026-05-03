/**
 * @file RDTServer.cpp
 * @brief RDTServer implementation — handshake, data receive loop, reordering, teardown
 * @author xmikusm00
 */

#include "RDTServer.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <netdb.h>
#include <unistd.h>
#include <sys/socket.h>
#include <vector>

#include "globals.hpp"
#include "protocol.hpp"
#include "socket_utils.hpp"

RDTServer::RDTServer(const Args &args)
{
    output_file = args.output.empty() || args.output == "-" ? stdout : fopen(args.output.c_str(), "wb");
    timeout_sec = args.timeout;
    struct addrinfo hints{}, *res;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    std::string port_str = std::to_string(args.port);
    const char *addr = args.address_host.empty() ? nullptr : args.address_host.c_str();

    int rc = getaddrinfo(addr, port_str.c_str(), &hints, &res);
    if (rc != 0)
    {
        std::cerr << "getaddrinfo: " << gai_strerror(rc) << "\n";
        return;
    }
    if (res == nullptr)
    {
        std::cerr << "getaddrinfo: no results\n";
        return;
    }

    for (struct addrinfo *p = res; p != nullptr; p = p->ai_next)
    {
        sock = socket(p->ai_family, p->ai_socktype, 0);
        if (sock < 0)
        {
            perror("socket");
            continue;
        }
        if (bind(sock, p->ai_addr, p->ai_addrlen) == 0)
        {
            std::cerr << "Bound to port " << args.port << " (family=" << p->ai_family << ")\n";
            break;
        }
        perror("bind");
        close(sock);
        sock = -1;
    }
    if (sock < 0)
    {
        std::cerr << "Failed to bind\n";
        exit(1);
    }

    set_recv_timeout(rto);

    freeaddrinfo(res);
}

void RDTServer::run()
{
    std::cerr << "RFTServer running\n";

    char buf[MAX_PDU_SIZE]{};
    socklen_t sender_len = sizeof(client_addr);
    using clock = std::chrono::steady_clock;
    auto last_progress = clock::now();

    while (!g_stop)
    {
        if (std::chrono::duration_cast<std::chrono::seconds>(clock::now() - last_progress).count() >= timeout_sec)
        {
            if (current_state == State::LAST_ACK)
            {
                std::cerr << "[server] LAST_ACK timeout, closing cleanly\n";
                current_state = State::DONE;
                break;
            }
            std::cerr << "[server] no progress for " << timeout_sec << "s, terminating\n";
            exit(1);
        }
        if (g_interrupted)
        {
            std::cerr << "Interrupted\n";
            break;
        }

        switch (current_state)
        {
        // --- Session establishment — implemented per RFC 9293 §3.4 (3-way handshake, passive open) ---
        case State::WAIT_SYN:
        {
            sender_len = sizeof(client_addr);
            ssize_t n = recvfrom(sock, buf, sizeof(buf), 0,
                                 reinterpret_cast<sockaddr *>(&client_addr), &sender_len);
            if (n < 0)
                break;
            if (!validate_pdu(buf, n))
                break;
            {
                PduHeader *hdr = reinterpret_cast<PduHeader *>(buf);
                if (hdr->flags == FLAG_SYN)
                {
                    conn_id = hdr->conn_id;
                    expected_seq = hdr->seq;
                    std::cerr << "[server][WAIT_SYN] SYN received conn_id=" << conn_id << " seq=" << expected_seq << "\n";
                    current_state = State::SEND_SYNACK;
                    last_progress = clock::now();
                }
            }
            break;
        }
        case State::SEND_SYNACK:
        {
            synack_send_time = clock::now();
            send_pdu(sock, reinterpret_cast<sockaddr *>(&client_addr), sender_len, conn_id, FLAG_SYN | FLAG_ACK, 0, expected_seq + 1, nullptr, 0);
            std::cerr << "[server][SEND_SYNACK] SYN-ACK sent ack=" << expected_seq + 1 << "\n";
            set_recv_timeout(rto);
            current_state = State::WAIT_ACK;
            break;
        }
        case State::WAIT_ACK:
        {
            sender_len = sizeof(client_addr);
            ssize_t n = recvfrom(sock, buf, sizeof(buf), 0,
                                 reinterpret_cast<sockaddr *>(&client_addr), &sender_len);
            if (n < 0)
            {
                std::cerr << "[server][WAIT_ACK] timeout, resending SYN-ACK\n";
                current_state = State::SEND_SYNACK;
                break;
            }
            if (!validate_pdu(buf, n))
                break;
            {
                PduHeader *hdr = reinterpret_cast<PduHeader *>(buf);
                if (hdr->conn_id == conn_id && (hdr->flags == FLAG_ACK || hdr->flags == FLAG_DATA))
                {
                    double rtt = std::chrono::duration<double>(clock::now() - synack_send_time).count();
                    update_rtt(rtt);
                    set_recv_timeout(rto);
                    std::cerr << "[server][WAIT_ACK] handshake complete RTT=" << rtt << "s RTO=" << rto << "s\n";
                    last_progress = clock::now();
                    current_state = State::DATA_TRANSFER;
                }
                else if (hdr->conn_id == conn_id && hdr->flags == FLAG_SYN)
                {
                    std::cerr << "[server][WAIT_ACK] SYN retransmit received, resending SYN-ACK\n";
                    current_state = State::SEND_SYNACK;
                }
                else if (hdr->conn_id == conn_id && hdr->flags == FLAG_FIN)
                {
                    std::cerr << "[server][WAIT_ACK] FIN received, sending ACK+FIN\n";
                    fin_seq = hdr->seq + 1;
                    send_pdu(sock, reinterpret_cast<sockaddr *>(&client_addr), sender_len, conn_id, FLAG_ACK, 0, fin_seq, nullptr, 0);
                    send_pdu(sock, reinterpret_cast<sockaddr *>(&client_addr), sender_len, conn_id, FLAG_FIN, 0, fin_seq, nullptr, 0);
                    last_progress = clock::now();
                    current_state = State::LAST_ACK;
                }
            }
            break;
        }
        // --- Data transfer — cumulative ACKs (RFC 9293 §3.3), out-of-order receive buffer (RFC 9293 §3.4) ---
        case State::DATA_TRANSFER:
        {
            sender_len = sizeof(client_addr);
            ssize_t n = recvfrom(sock, buf, sizeof(buf), 0,
                                 reinterpret_cast<sockaddr *>(&client_addr), &sender_len);
            if (n < 0)
                break;

            if (!validate_pdu(buf, n))
                break;
            PduHeader *pdu = reinterpret_cast<PduHeader *>(buf);
            if (pdu->conn_id != conn_id)
                break;

            if (pdu->flags == FLAG_DATA)
            {
                if (pdu->seq == expected_seq)
                {
                    fwrite(buf + sizeof(PduHeader), 1, pdu->length, output_file);
                    expected_seq += pdu->length;
                    last_progress = clock::now();
                    std::cerr << "[server][DATA] in-order seq=" << pdu->seq << " len=" << pdu->length << " next_expected=" << expected_seq << "\n";
                    while (window_buffer.count(expected_seq) > 0)
                    {
                        std::vector<char> &data = window_buffer[expected_seq];
                        uint32_t size = data.size();
                        fwrite(data.data(), 1, size, output_file);
                        last_progress = clock::now();
                        std::cerr << "[server][DATA] flushed buffered seq=" << expected_seq << " len=" << size << "\n";
                        window_buffer.erase(expected_seq);
                        expected_seq += size;
                    }
                }
                else
                {
                    if (pdu->seq > expected_seq &&
                        window_buffer.count(pdu->seq) == 0 &&
                        pdu->seq < expected_seq + WINDOW_SIZE * MAX_PAYLOAD_SIZE)
                    {
                        window_buffer[pdu->seq] = std::vector<char>(buf + sizeof(PduHeader), buf + sizeof(PduHeader) + pdu->length);
                        std::cerr << "[server][DATA] out-of-order seq=" << pdu->seq << " buffered (expected=" << expected_seq << ")\n";
                    }
                    else
                    {
                        std::cerr << "[server][DATA] duplicate/stale seq=" << pdu->seq << " discarded (expected=" << expected_seq << ")\n";
                    }
                }
                send_pdu(sock, reinterpret_cast<sockaddr *>(&client_addr), sender_len, conn_id, FLAG_ACK, 0, expected_seq, nullptr, 0);
                std::cerr << "[server][DATA] ACK sent ack=" << expected_seq << "\n";
            }
            else if (pdu->flags == FLAG_FIN)
            {
                std::cerr << "[server][DATA_TRANSFER] FIN received, sending ACK+FIN\n";
                fin_seq = pdu->seq + 1;
                send_pdu(sock, reinterpret_cast<sockaddr *>(&client_addr), sender_len, conn_id, FLAG_ACK, 0, fin_seq, nullptr, 0);
                send_pdu(sock, reinterpret_cast<sockaddr *>(&client_addr), sender_len, conn_id, FLAG_FIN, 0, fin_seq, nullptr, 0);
                last_progress = clock::now();
                current_state = State::LAST_ACK;
            }
            break;
        }

        // --- Session teardown — implemented per RFC 9293 §3.6 (passive closer, LAST_ACK) ---
        case State::LAST_ACK:
        {
            sender_len = sizeof(client_addr);
            ssize_t n = recvfrom(sock, buf, sizeof(buf), 0,
                                 reinterpret_cast<sockaddr *>(&client_addr), &sender_len);
            if (n < 0)
            {
                std::cerr << "[server][LAST_ACK] timeout waiting for ACK, resending FIN\n";
                send_pdu(sock, reinterpret_cast<sockaddr *>(&client_addr), sender_len, conn_id, FLAG_FIN, 0, fin_seq, nullptr, 0);
                break;
            }
            if (!validate_pdu(buf, n))
                break;
            PduHeader *pdu = reinterpret_cast<PduHeader *>(buf);
            if (pdu->conn_id == conn_id && pdu->flags == FLAG_ACK)
            {
                current_state = State::DONE;
                std::cerr << "[server][LAST_ACK] ACK received, moving to DONE\n";
            }
            else if (pdu->conn_id == conn_id && pdu->flags == FLAG_FIN)
            {
                std::cerr << "[server][LAST_ACK] FIN retransmission received, resending ACK+FIN\n";
                send_pdu(sock, reinterpret_cast<sockaddr *>(&client_addr), sender_len, conn_id, FLAG_ACK, 0, fin_seq, nullptr, 0);
                send_pdu(sock, reinterpret_cast<sockaddr *>(&client_addr), sender_len, conn_id, FLAG_FIN, 0, fin_seq, nullptr, 0);
            }
            break;
        }
        case State::DONE:
            std::cerr << "[server] done\n";
            g_stop = 1;
            break;
        default:
            break;
        }
    }
}

RDTServer::~RDTServer()
{
    if (sock >= 0)
        close(sock);
    if (output_file && output_file != stdout)
    {
        fclose(output_file);
    }
}
