#include "RFTServer.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <unistd.h>
#include <sys/socket.h>

#include "globals.hpp"
#include "protocol.hpp"

RFTServer::RFTServer(const Args &args)
{
    struct addrinfo hints{}, *res;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    std::string port_str = std::to_string(args.port);
    const char *addr = args.address_host.empty() ? nullptr : args.address_host.c_str();

    int rc = getaddrinfo(addr, port_str.c_str(), &hints, &res);
    if (rc != 0) { std::cerr << "getaddrinfo: " << gai_strerror(rc) << "\n"; return; }
    if (res == nullptr) { std::cerr << "getaddrinfo: no results\n"; return; }

    for (struct addrinfo *p = res; p != nullptr; p = p->ai_next)
    {
        sock = socket(p->ai_family, p->ai_socktype, 0);
        if (sock < 0) { perror("socket"); continue; }
        if (bind(sock, p->ai_addr, p->ai_addrlen) == 0)
        {
            std::cerr << "Bound to port " << args.port << " (family=" << p->ai_family << ")\n";
            break;
        }
        perror("bind");
        close(sock);
        sock = -1;
    }
    if (sock < 0) { std::cerr << "Failed to bind\n"; exit(1); }

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

    while (!g_stop)
    {
        if (g_interrupted) { std::cerr << "Interrupted\n"; break; }

        switch (current_state)
        {
        case State::WAIT_SYN:
        {
            sender_len = sizeof(client_addr);
            ssize_t n = recvfrom(sock, buf, sizeof(buf), 0,
                                 reinterpret_cast<sockaddr *>(&client_addr), &sender_len);
            if (n < 0) break;
            PduHeader *hdr = reinterpret_cast<PduHeader *>(buf);
            if (hdr->flags == FLAG_SYN)
            {
                conn_id = hdr->conn_id;
                std::cerr << "[server] SYN received\n";
                current_state = State::SEND_SYNACK;
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
            if (hdr->conn_id == conn_id && hdr->flags == FLAG_ACK)
            {
                std::cerr << "[server] ACK received, handshake complete\n";
                current_state = State::DATA_TRANSFER;
            }
            break;
        }
        case State::DATA_TRANSFER:
            std::cerr << "[server] data transfer not implemented yet\n";
            current_state = State::DONE;
            break;
        case State::SEND_FIN:
            break;
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
}
