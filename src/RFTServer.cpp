#include "RFTServer.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <unistd.h>
#include <sys/socket.h>

#include "globals.hpp"
#include "protocol.hpp"

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

    struct timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 200000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    freeaddrinfo(res);
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
        if (current_state != State::DONE && current_state != State::WAIT_SYN &&
            std::chrono::duration_cast<std::chrono::seconds>(clock::now() - last_progress).count() >= timeout_sec)
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
            PduHeader *hdr = reinterpret_cast<PduHeader *>(buf);
            if (hdr->flags == FLAG_SYN)
            {
                conn_id = hdr->conn_id;
                initial_seq = expected_seq = hdr->seq;
                std::cerr << "[server] SYN received, ISN=" << expected_seq << "\n";
                current_state = State::SEND_SYNACK;
                last_progress = clock::now();
            }
            break;
        }
        case State::SEND_SYNACK:
        {
            PduHeader synack{};
            synack.conn_id = conn_id;
            synack.flags = FLAG_SYN | FLAG_ACK;
            synack.checksum = compute_checksum(&synack, sizeof(synack));
            sendto(sock, &synack, sizeof(synack), 0,
                   reinterpret_cast<sockaddr *>(&client_addr), sender_len);
            std::cerr << "[server] SYN-ACK sent\n";
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
            PduHeader *hdr = reinterpret_cast<PduHeader *>(buf);
            if (hdr->conn_id == conn_id && (hdr->flags == FLAG_ACK || hdr->flags == FLAG_DATA))
            {
                std::cerr << "[server] handshake complete\n";
                last_progress = clock::now();
                current_state = State::DATA_TRANSFER;
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
            PduHeader *pdu = reinterpret_cast<PduHeader *>(buf);
            if (pdu->conn_id != conn_id)
                break;
            if (pdu->flags == FLAG_DATA)
            {
                uint32_t slot = ((pdu->seq - initial_seq) / MAX_PAYLOAD_SIZE) % WINDOW_SIZE;
                if (pdu->seq >= expected_seq && pdu->seq < expected_seq + WINDOW_SIZE * MAX_PAYLOAD_SIZE)
                {
                    if (!window[slot].in_use)
                    {
                        window[slot].seq = pdu->seq;
                        window[slot].len = pdu->length;
                        window[slot].in_use = true;
                        std::memcpy(window[slot].data, buf + sizeof(PduHeader), pdu->length);
                        std::cerr << "[server] buffered seq=" << pdu->seq << " slot=" << slot << "\n";
                    }
                    while (true)
                    {
                        uint32_t front = ((expected_seq - initial_seq) / MAX_PAYLOAD_SIZE) % WINDOW_SIZE;
                        if (!window[front].in_use || window[front].seq != expected_seq)
                            break;
                        fwrite(window[front].data, 1, window[front].len, output_file);
                        std::cerr << "[server] wrote seq=" << expected_seq << " len=" << window[front].len << "\n";
                        expected_seq += window[front].len;
                        window[front].in_use = false;
                        last_progress = clock::now();
                    }
                }
                else
                {
                    std::cerr << "[server] out-of-window, expected=" << expected_seq << " got=" << pdu->seq << "\n";
                }
                PduHeader ack{};
                ack.conn_id = conn_id;
                ack.flags = FLAG_ACK;
                ack.ack = expected_seq;
                ack.checksum = compute_checksum(&ack, sizeof(ack));
                sendto(sock, &ack, sizeof(ack), 0,
                       reinterpret_cast<sockaddr *>(&client_addr), sender_len);
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
            PduHeader synack{};
            synack.conn_id = conn_id;
            synack.flags = FLAG_FIN | FLAG_ACK;
            synack.checksum = compute_checksum(&synack, sizeof(synack));
            sendto(sock, &synack, sizeof(synack), 0,
                   reinterpret_cast<sockaddr *>(&client_addr), sender_len);
            std::cerr << "[server] FIN-ACK sent\n";
            current_state = State::DONE;
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
    if (output_file && output_file != stdout) {
        fclose(output_file);
    }
}
