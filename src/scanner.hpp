/**
 * @file scanner.hpp
 * @brief IPK Project 1 - L4 Scanner
 * @author xmikusm00
 */

#pragma once

#include <cstdint>
#include <csignal>
#include <string>
#include <vector>
#include <unordered_map>

using namespace std;

// Set to 1 from the signal handler to interrupt in-progress scans
extern volatile sig_atomic_t g_interrupted;
// Fills buffer with an IPv4 TCP SYN packet (IP + TCP headers). Returns packet length.

static uint16_t checksum(const void* data, size_t len);
static void send_udp_packet(const std::string& dst_ip, uint16_t dst_port);
static void send_udp6_packet(const std::string& dst_ip, uint16_t dst_port);
