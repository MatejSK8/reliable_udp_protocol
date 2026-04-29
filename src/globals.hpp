#pragma once
#include <csignal>

extern volatile sig_atomic_t g_stop;
extern volatile sig_atomic_t g_interrupted;
