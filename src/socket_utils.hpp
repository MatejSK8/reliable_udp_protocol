#pragma once
#include <cstdint>
#include <sys/socket.h>

#include "protocol.hpp"

void send_pdu(int sock, sockaddr *dest, socklen_t dest_len,
               uint32_t conn_id, uint8_t flags,
               uint32_t seq, uint32_t ack,
               const void *payload, size_t payload_len);