/**
 * @file args.hpp
 * @brief IPK Project 2 - Reliable Data Transfer
 * @author xmikusm00
 */

#pragma once
#include <string>
#include <cstdint>

enum class Mode
{
    NONE,
    SERVER,
    CLIENT
};

struct Args
{
    Mode mode = Mode::NONE;
    uint16_t port = 0;        // -p PORT
    std::string address_host; // -a ADDRESS (server: bind addr) / HOST (client: destination)
    std::string input;        // -i INPUT (client only, empty = stdin)
    std::string output;       // -o OUTPUT (server only, empty = stdout)
    int timeout = 1;          // -w TIMEOUT in seconds
};

// Parses argc/argv into Args. Prints usage and exits on error.
Args parse_args(int argc, char *argv[]);

// Prints usage to stdout.
void print_usage(const char *prog);
