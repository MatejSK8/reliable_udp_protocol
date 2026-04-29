#include <iostream>
#include <csignal>

#include "args.hpp"
#include "RFTClient.hpp"
#include "RFTServer.hpp"

volatile sig_atomic_t g_stop = 0;
volatile sig_atomic_t g_interrupted = 0;

static void sig_handler(int) {
    g_stop = 1;
    g_interrupted = 1;
}

// Sleep for up to ms milliseconds, waking every 10ms to check for interrupt.
static void interruptible_sleep(const int ms) {
    for (int elapsed = 0; elapsed < ms && !g_interrupted; elapsed += 10)
        usleep(10000);
}

int main(const int argc, char *argv[]) {
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    Args args = parse_args(argc, argv);

    std::cerr << "mode: " << (args.mode == Mode::CLIENT ? "client" : "server") << "\n"
            << "port: " << args.port << "\n"
            << "address: " << args.address_host << "\n"
            << "input: " << args.input << "\n"
            << "output: " << args.output << "\n"
            << "timeout: " << args.timeout << "\n";
    switch (args.mode) {
        case Mode::CLIENT: {
            RFTClient client(args);
            client.run(args);
            break;
        }
        case Mode::SERVER: {
            RFTServer server(args);
            server.run();
            break;
        }

        default:
            std::cerr << "invalid arguments\n";
            return 1;
    }
}
