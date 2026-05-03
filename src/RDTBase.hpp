#pragma once

class RDTBase
{
protected:
    double srtt = -1;
    double rttvar = 0;
    double rto = 0.01;
    int sock = -1;

public:
    void set_recv_timeout(double seconds);
    void update_rtt(double sample);
};