#include "socket_utils.hpp"

#include <cstring>
#include <sys/socket.h>

#include "protocol.hpp"

void send_pdu(int sock, sockaddr *dest, socklen_t dest_len,
              uint32_t conn_id, uint8_t flags,
              uint32_t seq, uint32_t ack,
              const void *payload, size_t payload_len)
{
       char pdu[MAX_PDU_SIZE];
       PduHeader hdr{};
       hdr.conn_id = conn_id;
       hdr.flags = flags;
       hdr.seq = seq;
       hdr.ack = ack;
       hdr.length = static_cast<uint16_t>(payload_len);
       std::memcpy(pdu, &hdr, sizeof(hdr));
       std::memcpy(pdu + sizeof(hdr), payload, payload_len);
       hdr.checksum = compute_checksum(pdu, sizeof(hdr) + payload_len);
       std::memcpy(pdu, &hdr, sizeof(hdr));
       sendto(sock, pdu, sizeof(hdr) + payload_len, 0,
              reinterpret_cast<sockaddr *>(dest), dest_len);
}
