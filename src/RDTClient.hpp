/**
 * @file RDTClient.hpp
 * @brief RDTClient — sliding-window sender with 3-way handshake and FIN teardown
 * @author xmikusm00
 */

#pragma once
#include <chrono>
#include <netdb.h>
#include <sys/socket.h>
#include "args.hpp"
#include "protocol.hpp"
#include "RDTBase.hpp"

class RDTClient : public RDTBase
{
public:
    RDTClient(const Args &args);
    ~RDTClient();
    void run(const Args &args);

    uint32_t highest_cumulative_ack = 0;
    int dup_ack_count = 0;
    std::chrono::steady_clock::time_point syn_send_time;
    PduHeader syn_pdu{};
    uint32_t next_seq = 0;
    bool eof_reached = false;
    int window_start = 0;

    WindowSlot window[WINDOW_SIZE];
    FILE *input_file;
    sockaddr_storage dest_addr{};
    socklen_t dest_len = 0;
    enum class State
    {
        SEND_SYN,
        WAIT_SYNACK,
        DATA_TRANSFER,
        SEND_FIN,
        FIN_WAIT_1,
        FIN_WAIT_2,
        TIME_WAIT,
        DONE
    };
    State current_state = State::SEND_SYN;
};