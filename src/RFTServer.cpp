#include "RFTServer.hpp"

#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <unistd.h>
#include <sys/socket.h>
#include <map>
#include <vector>

#include "globals.hpp"
#include "protocol.hpp"
#include "socket_utils.hpp"

RFTServer::RFTServer(const Args &args)
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

void RFTServer::set_recv_timeout(double seconds)
{
    if (seconds < 0.001)
        seconds = 0.001;
    struct timeval tv{};
    tv.tv_sec = static_cast<time_t>(seconds);
    tv.tv_usec = static_cast<suseconds_t>((seconds - tv.tv_sec) * 1e6);
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

void RFTServer::update_rtt(double sample)
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

void RFTServer::run()
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
                last_progress = clock::now();
                current_state = State::DATA_TRANSFER;
            }
            else if (hdr->conn_id == conn_id && hdr->flags == FLAG_SYN)
            {
                current_state = State::SEND_SYNACK;
            }
            else if (hdr->conn_id == conn_id && hdr->flags == FLAG_FIN)
            {
                current_state = State::SEND_FIN_ACK;
            }
            }
            break;
        }
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
                    while (window_buffer.count(expected_seq) > 0)
                    {
                        std::vector<char> &data = window_buffer[expected_seq];
                        uint32_t size = data.size();
                        fwrite(data.data(), 1, size, output_file);
                        last_progress = clock::now();
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
                    }
                }
                send_pdu(sock, reinterpret_cast<sockaddr *>(&client_addr), sender_len, conn_id, FLAG_ACK, 0, expected_seq, nullptr, 0);
            }
            else if (pdu->flags == FLAG_FIN)
            {
                std::cerr << "[server] FIN received\n";
                current_state = State::SEND_FIN_ACK;
            }
            break;
        }
        case State::SEND_FIN_ACK:
        {
            send_pdu(sock, reinterpret_cast<sockaddr *>(&client_addr), sender_len, conn_id, FLAG_FIN | FLAG_ACK, 0, expected_seq, nullptr, 0);
            std::cerr << "[server] FIN-ACK sent, RTO=" << rto << "s\n";
            set_recv_timeout(rto);
            close_deadline = clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(rto) * 4);
            if (std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(rto) * 4) >= std::chrono::seconds(timeout_sec))
            {
                close_deadline = clock::now() + std::chrono::seconds(timeout_sec);
            }
            current_state = State::WAIT_CLOSE;
            break;
        }
        case State::WAIT_CLOSE:
        {
            if (clock::now() >= close_deadline)
            {
                current_state = State::DONE;
                std::cerr << "[server] close timeout, moving to DONE\n";
                break;
            }
            sender_len = sizeof(client_addr);
            ssize_t n = recvfrom(sock, buf, sizeof(buf), 0,
                                 reinterpret_cast<sockaddr *>(&client_addr), &sender_len);
            if (n < 0)
                break;
            PduHeader *pdu = reinterpret_cast<PduHeader *>(buf);
            if (pdu->conn_id == conn_id && pdu->flags == FLAG_FIN)
            {
                current_state = State::SEND_FIN_ACK;
                std::cerr << "[server] FIN retransmission received, resending FIN-ACK\n";
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

RFTServer::~RFTServer()
{
    if (sock >= 0)
        close(sock);
    if (output_file && output_file != stdout)
    {
        fclose(output_file);
    }
}
