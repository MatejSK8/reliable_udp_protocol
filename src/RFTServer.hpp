#pragma once
#include <cstdint>
#include <netinet/in.h>
#include <sys/socket.h>
#include "args.hpp"
#include "protocol.hpp"

class RFTServer
{
public:
    RFTServer(const Args &args);
    ~RFTServer();
    void run();

private:
    int sock = -1;
    sockaddr_storage client_addr{};
    uint32_t conn_id = 0;
    enum class State
    {
        WAIT_SYN,
        SEND_SYNACK,
        WAIT_ACK,
        DATA_TRANSFER,
        SEND_FIN,
        DONE
    };

    State current_state = State::WAIT_SYN;
};
