/**
 * @file RDTServer.hpp
 * @brief RDTServer — sliding-window receiver with 3-way handshake and FIN teardown
 * @author xmikusm00
 */

#pragma once
#include <chrono>
#include <cstdint>
#include <map>
#include <vector>
#include <netinet/in.h>
#include <sys/socket.h>
#include "args.hpp"
#include "protocol.hpp"
#include "RDTBase.hpp"

class RDTServer : public RDTBase
{
public:
    RDTServer(const Args &args);
    ~RDTServer();
    void run();

private:
    uint32_t fin_seq = 0;
    std::map<uint32_t, std::vector<char>> window_buffer;
    int timeout_sec = 1;
    FILE *output_file = nullptr;
    sockaddr_storage client_addr{};
    uint32_t conn_id = 0;
    uint32_t expected_seq = 0;

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
};
