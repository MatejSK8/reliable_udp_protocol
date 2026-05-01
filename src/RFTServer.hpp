#pragma once
#include <chrono>
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
    int timeout_sec = 1;
    FILE *output_file = nullptr;
    sockaddr_storage client_addr{};
    uint32_t conn_id = 0;
    uint32_t expected_seq = 0;
    uint32_t initial_seq = 0;

    WindowSlot window[WINDOW_SIZE]{};
    enum class State
    {
        WAIT_SYN,
        SEND_SYNACK,
        WAIT_ACK,
        DATA_TRANSFER,
        SEND_FIN_ACK,
        WAIT_CLOSE,
        DONE
    };

    State current_state = State::WAIT_SYN;
    std::chrono::steady_clock::time_point close_deadline;
};
