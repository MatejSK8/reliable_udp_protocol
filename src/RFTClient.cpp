#include "RFTClient.hpp"

#include <chrono>
#include <cstdio>
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
    input_file = args.input.empty() || args.input == "-" ? stdin : fopen(args.input.c_str(), "rb");

    base = next_seq = static_cast<uint32_t>(rand());

    memset(window, 0, sizeof(window));

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
            syn_pdu = PduHeader{};
            syn_pdu.conn_id = static_cast<uint32_t>(rand());
            syn_pdu.flags = FLAG_SYN;
            syn_pdu.seq = next_seq;
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
                if (g_stop)
                    break;
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
                last_progress = clock::now();
                current_state = State::DATA_TRANSFER;
            }
            break;
        }
        case State::DATA_TRANSFER:
        {
            while (!eof_reached)
            {
                int free_slot = -1;
                for (int i = 0; i < WINDOW_SIZE; i++)
                {
                    if (!window[i].in_use) { free_slot = i; break; }
                }
                if (free_slot == -1) break;

                char tmp[MAX_PAYLOAD_SIZE];
                size_t n = fread(tmp, 1, sizeof(tmp), input_file);
                if (n == 0) { eof_reached = true; break; }

                window[free_slot].seq = next_seq;
                window[free_slot].len = n;
                window[free_slot].in_use = true;
                std::memcpy(window[free_slot].data, tmp, n);

                char pdu[MAX_PDU_SIZE];
                PduHeader hdr{};
                hdr.conn_id = syn_pdu.conn_id;
                hdr.flags = FLAG_DATA;
                hdr.seq = next_seq;
                hdr.length = static_cast<uint16_t>(n);
                std::memcpy(pdu, &hdr, sizeof(hdr));
                std::memcpy(pdu + sizeof(hdr), tmp, n);
                hdr.checksum = compute_checksum(pdu, sizeof(hdr) + n);
                std::memcpy(pdu, &hdr, sizeof(hdr));
                sendto(sock, pdu, sizeof(hdr) + n, 0,
                       reinterpret_cast<sockaddr *>(&dest_addr), dest_len);
                next_seq += n;
                std::cerr << "[client] sent seq=" << window[free_slot].seq << " len=" << n << "\n";
            }
            current_state = State::WAIT_ACK;
            break;
        }
        case State::WAIT_ACK:
        {
            sender_len = sizeof(sender);
            ssize_t n = recvfrom(sock, buf, sizeof(buf), 0,
                                 reinterpret_cast<sockaddr *>(&sender), &sender_len);
            if (n < 0)
            {
                if (g_stop) break;
                std::cerr << "[client] timeout, resending window\n";
                current_state = State::RESEND_DATA;
                break;
            }
            PduHeader *hdr = reinterpret_cast<PduHeader *>(buf);
            if (hdr->conn_id == syn_pdu.conn_id && hdr->flags == FLAG_ACK)
            {
                std::cerr << "[client] ACK received, ack=" << hdr->ack << "\n";
                uint32_t acked_seq = hdr->ack;
                if (acked_seq != base)
                    last_progress = clock::now();
                base = acked_seq;
                for (int i = 0; i < WINDOW_SIZE; i++)
                {
                    if (window[i].in_use && window[i].seq + window[i].len <= acked_seq)
                        window[i].in_use = false;
                }
                bool any_in_use = false;
                for (int i = 0; i < WINDOW_SIZE; i++)
                    if (window[i].in_use) { any_in_use = true; break; }

                if (!any_in_use && eof_reached)
                    current_state = State::SEND_FIN;
                else if (!any_in_use)
                    current_state = State::DATA_TRANSFER;
            }
            break;
        }
        case State::RESEND_DATA:
            for (int i = 0; i < WINDOW_SIZE; i++)
            {
                if (window[i].in_use)
                {
                    char pdu[MAX_PDU_SIZE];
                    PduHeader hdr{};
                    hdr.conn_id = syn_pdu.conn_id;
                    hdr.flags = FLAG_DATA;
                    hdr.seq = window[i].seq;
                    hdr.length = static_cast<uint16_t>(window[i].len);
                    std::memcpy(pdu, &hdr, sizeof(hdr));
                    std::memcpy(pdu + sizeof(hdr), window[i].data, window[i].len);
                    hdr.checksum = compute_checksum(pdu, sizeof(hdr) + window[i].len);
                    std::memcpy(pdu, &hdr, sizeof(hdr));
                    sendto(sock, pdu, sizeof(hdr) + window[i].len, 0,
                           reinterpret_cast<sockaddr *>(&dest_addr), dest_len);
                }
            }
            current_state = State::WAIT_ACK;
            break;

        case State::SEND_FIN:
        {
            PduHeader fin{};
            fin.conn_id = syn_pdu.conn_id;
            fin.flags = FLAG_FIN;
            fin.checksum = compute_checksum(&fin, sizeof(fin));
            sendto(sock, &fin, sizeof(fin), 0,
                   reinterpret_cast<sockaddr *>(&dest_addr), dest_len);
            std::cerr << "[client] FIN sent\n";
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
            PduHeader *hdr = reinterpret_cast<PduHeader *>(buf);
            if (hdr->conn_id == syn_pdu.conn_id && hdr->flags == (FLAG_FIN | FLAG_ACK))
            {
                std::cerr << "[client] FIN-ACK received\n";
                last_progress = clock::now();
                current_state = State::DONE;
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
