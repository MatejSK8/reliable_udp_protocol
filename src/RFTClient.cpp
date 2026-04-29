#include "RFTClient.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <unistd.h>
#include <sys/socket.h>

#include "globals.hpp"
#include "protocol.hpp"

RFTClient::RFTClient(const Args &args)
{
    struct addrinfo hints{}, *res;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    std::string port_str = std::to_string(args.port);
    getaddrinfo(args.address_host.c_str(), port_str.c_str(), &hints, &res);

    for (struct addrinfo *p = res; p != nullptr; p = p->ai_next)
    {
        sock = socket(p->ai_family, p->ai_socktype, 0);
        if (sock >= 0)
        {
            std::memcpy(&dest_addr, p->ai_addr, p->ai_addrlen);
            dest_len = p->ai_addrlen;
            struct timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = 200000;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
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

    while (!g_stop && current_state != State::DONE)
    {
        switch (current_state)
        {
        case State::SEND_SYN:
        {
            syn_pdu = PduHeader{};
            syn_pdu.conn_id = static_cast<uint32_t>(rand());
            syn_pdu.flags = FLAG_SYN;
            syn_pdu.checksum = compute_checksum(&syn_pdu, sizeof(syn_pdu));
            sendto(sock, &syn_pdu, sizeof(syn_pdu), 0,
                   reinterpret_cast<sockaddr *>(&dest_addr), dest_len);
            std::cerr << "[client] SYN sent\n";
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
                if (g_stop) break;
                std::cerr << "[client] timeout, resending SYN\n";
                sendto(sock, &syn_pdu, sizeof(syn_pdu), 0,
                       reinterpret_cast<sockaddr *>(&dest_addr), dest_len);
                break;
            }
            PduHeader *hdr = reinterpret_cast<PduHeader *>(buf);
            if (hdr->conn_id == syn_pdu.conn_id && hdr->flags == (FLAG_SYN | FLAG_ACK))
            {
                std::cerr << "[client] SYN-ACK received, sending ACK\n";
                PduHeader ack{};
                ack.conn_id = syn_pdu.conn_id;
                ack.flags = FLAG_ACK;
                ack.checksum = compute_checksum(&ack, sizeof(ack));
                sendto(sock, &ack, sizeof(ack), 0,
                       reinterpret_cast<sockaddr *>(&dest_addr), dest_len);
                current_state = State::DATA_TRANSFER;
            }
            break;
        }
        case State::DATA_TRANSFER:
            std::cerr << "[client] data transfer not implemented yet\n";
            current_state = State::DONE;
            break;
        case State::WAIT_FIN_ACK:
            std::cerr << "[client] FIN/ACK not implemented yet\n";
            current_state = State::DONE;
            break;
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
}
