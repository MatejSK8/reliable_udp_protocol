/**
 * @file protocol.hpp
 * @brief PDU header layout, protocol constants, checksum and validation helpers
 * @author xmikusm00
 */

#pragma once
#include <chrono>
#include <cstdint>

#define MAX_PDU_SIZE 1200
#define HEADER_SIZE 16
#define MAX_PAYLOAD_SIZE (MAX_PDU_SIZE - HEADER_SIZE)
#define WINDOW_SIZE 64

// Flags
#define FLAG_SYN 0x01
#define FLAG_ACK 0x02
#define FLAG_FIN 0x04
#define FLAG_DATA 0x08

#pragma pack(push, 1)
struct PduHeader
{
    uint32_t conn_id{};
    uint32_t seq{};
    uint32_t ack{};
    uint16_t length = MAX_PAYLOAD_SIZE;
    uint8_t flags{};
    uint8_t checksum{};
};
#pragma pack(pop)

struct WindowSlot
{
    char data[MAX_PAYLOAD_SIZE];
    size_t len;
    uint32_t seq;
    bool in_use;
    bool acked;
    bool retransmitted;
    std::chrono::steady_clock::time_point send_time;
};

inline uint8_t compute_checksum(const void *data, size_t len)
{
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(data);
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++)
        sum ^= bytes[i];
    return sum;
}

inline bool validate_pdu(const char *buf, ssize_t n)
{
    if (n < (ssize_t)sizeof(PduHeader))
        return false;
    PduHeader *hdr = reinterpret_cast<PduHeader *>(const_cast<char *>(buf));
    if (hdr->length > MAX_PAYLOAD_SIZE || (ssize_t)(sizeof(PduHeader) + hdr->length) > n)
        return false;
    uint8_t rc = hdr->checksum;
    hdr->checksum = 0;
    bool ok = (compute_checksum(buf, sizeof(PduHeader) + hdr->length) == rc);
    hdr->checksum = rc;
    return ok;
}
