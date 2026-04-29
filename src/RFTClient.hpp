#pragma once
#include <netdb.h>
#include <sys/socket.h>
#include "args.hpp"
#include "protocol.hpp"

class RFTClient {
public:
    RFTClient(const Args &args);
    ~RFTClient();
    void run(const Args &args);

private:
    int sock = -1;
    sockaddr_storage dest_addr{};
    socklen_t dest_len = 0;
    PduHeader syn_pdu{};
    enum class State { SEND_SYN, WAIT_SYNACK, DATA_TRANSFER, WAIT_FIN_ACK, DONE };
    State current_state = State::SEND_SYN;
};