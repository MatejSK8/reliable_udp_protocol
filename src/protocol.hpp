#pragma once
#include <cstdint>

#define MAX_PDU_SIZE 1200
#define HEADER_SIZE 16
#define MAX_PAYLOAD_SIZE (MAX_PDU_SIZE - HEADER_SIZE)
#define WINDOW_SIZE 8

// Flags
#define FLAG_SYN 0x01
#define FLAG_ACK 0x02
#define FLAG_FIN 0x04
#define FLAG_DATA 0x08

#pragma pack(push, 1)
struct PduHeader
{
    uint32_t conn_id;                   // connection identifier
    uint32_t seq;                       // sequence number (byte offset or packet number)
    uint32_t ack;                       // cumulative ACK
    uint16_t length = MAX_PAYLOAD_SIZE; // payload length in bytes
    uint8_t flags;                      // SYN/ACK/FIN/DATA
    uint8_t checksum;                   // simple XOR or sum over header+payload
};
#pragma pack(pop)

struct WindowSlot
{
    char data[MAX_PAYLOAD_SIZE];
    size_t len;
    uint32_t seq;
    bool in_use;
};

inline uint8_t compute_checksum(const void *data, size_t len)
{
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(data);
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++)
        sum ^= bytes[i];
    return sum;
}
