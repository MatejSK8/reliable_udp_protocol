#pragma once
#include <netdb.h>
#include <sys/socket.h>
#include "args.hpp"
#include "protocol.hpp"

class RFTClient
{
public:
    RFTClient(const Args &args);
    ~RFTClient();
    void run(const Args &args);

private:
    PduHeader syn_pdu{};
    uint32_t base = 0;
    uint32_t next_seq = 0;
    bool eof_reached = false;

    WindowSlot window[WINDOW_SIZE];
    FILE *input_file;
    int sock = -1;
    sockaddr_storage dest_addr{};
    socklen_t dest_len = 0;
    enum class State
    {
        SEND_SYN,
        WAIT_SYNACK,
        DATA_TRANSFER,
        WAIT_FIN_ACK,
        SEND_FIN,
        WAIT_ACK,
        RESEND_DATA,
        DONE
    };
    State current_state = State::SEND_SYN;
};