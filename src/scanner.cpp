/**
 * @file scanner.cpp
 * @brief IPK Project 1 - L4 Scanner
 * @author xmikusm00
 */

#include "scanner.hpp"
#include <atomic>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <thread>
#include <pcap/pcap.h>
#include <netinet/udp.h>

volatile sig_atomic_t g_interrupted = 0;

// Sleep for up to ms milliseconds, waking every 10ms to check for interrupt.
static void interruptible_sleep(int ms)
{
    for (int elapsed = 0; elapsed < ms && !g_interrupted; elapsed += 10)
        usleep(10000);
}

static uint16_t checksum(const void* data, size_t len)
{
    const auto* ptr = static_cast<const uint16_t*>(data);
    uint32_t sum = 0;

    while (len > 1)
    {
        sum += *ptr++;
        len -= 2;
    }
    if (len == 1)
        sum += *reinterpret_cast<const uint8_t*>(ptr);

    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return static_cast<uint16_t>(~sum);
}

static void send_udp_packet(const std::string& dst_ip, uint16_t dst_port)
{
    const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
    {
        perror("UDP socket failed");
        return;
    }

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(dst_port);
    inet_pton(AF_INET, dst_ip.c_str(), &dest.sin_addr);

    constexpr char payload = 0;
    sendto(sock, &payload, 1, 0,
           reinterpret_cast<struct sockaddr*>(&dest), sizeof(dest));
    close(sock);
}

static void send_udp6_packet(const std::string& dst_ip, uint16_t dst_port)
{
    const int sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
    {
        perror("UDP6 socket failed");
        return;
    }

    sockaddr_in6 dest{};
    dest.sin6_family = AF_INET6;
    dest.sin6_port = htons(dst_port);
    inet_pton(AF_INET6, dst_ip.c_str(), &dest.sin6_addr);

    constexpr char payload = 0;
    sendto(sock, &payload, 1, 0,
           reinterpret_cast<struct sockaddr*>(&dest), sizeof(dest));
    close(sock);
}