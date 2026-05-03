/**
 * @file globals.hpp
 * @brief Global signal flags shared between main and transfer threads
 * @author xmikusm00
 */

#pragma once
#include <csignal>

extern volatile sig_atomic_t g_stop;
extern volatile sig_atomic_t g_interrupted;
