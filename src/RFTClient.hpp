#pragma once
#include <chrono>
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
    double srtt = -1;
    double rttvar = 0;
    double rto = 0.2;
    std::chrono::steady_clock::time_point syn_send_time;
    PduHeader syn_pdu{};
    uint32_t next_seq = 0;
    bool eof_reached = false;
    int window_start = 0;

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
        SEND_FIN,
        WAIT_FIN_ACK,
        DONE
    };
    State current_state = State::SEND_SYN;

    void set_recv_timeout(double seconds);
    void update_rtt(double rtt_sample);
};