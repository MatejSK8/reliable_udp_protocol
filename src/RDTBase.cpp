/**
 * @file RDTBase.cpp
 * @brief Shared RTT estimation and socket timeout helpers for client and server
 * @author xmikusm00
 */

#include "RDTBase.hpp"
#include <cmath>
#include <sys/socket.h>

void RDTBase::set_recv_timeout(double seconds)
{
    if (seconds < 0.001)
        seconds = 0.001;
    struct timeval tv{};
    tv.tv_sec = static_cast<time_t>(seconds);
    tv.tv_usec = static_cast<suseconds_t>((seconds - tv.tv_sec) * 1e6);
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

// Retransmission timer — implemented per RFC 6298 (SRTT/RTTVAR EWMA, RTO = SRTT + 4*RTTVAR)
void RDTBase::update_rtt(double sample)
{
    if (srtt < 0)
    {
        srtt = sample;
        rttvar = sample / 2.0;
    }
    else
    {
        rttvar = 0.75 * rttvar + 0.25 * std::fabs(srtt - sample);
        srtt = 0.875 * srtt + 0.125 * sample;
    }
    rto = srtt + 4.0 * rttvar;
    if (rto < 0.01)
        rto = 0.01;
    if (rto > 60.0)
        rto = 60.0;
}
