#pragma once
#include <chrono>
#include <cstdint>
#include <map>
#include <vector>
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
    uint32_t fin_seq = 0;
    std::map<uint32_t, std::vector<char>> window_buffer;
    int sock = -1;
    int timeout_sec = 1;
    FILE *output_file = nullptr;
    sockaddr_storage client_addr{};
    uint32_t conn_id = 0;
    uint32_t expected_seq = 0;

    double srtt = -1;
    double rttvar = 0;
    double rto = 1.0;
    std::chrono::steady_clock::time_point synack_send_time;

    enum class State
    {
        WAIT_SYN,
        SEND_SYNACK,
        WAIT_ACK,
        DATA_TRANSFER,
        WAIT_CLOSE,
        LAST_ACK,
        DONE
    };

    State current_state = State::WAIT_SYN;
    std::chrono::steady_clock::time_point close_deadline;

    void set_recv_timeout(double seconds);
    void update_rtt(double sample);
};
