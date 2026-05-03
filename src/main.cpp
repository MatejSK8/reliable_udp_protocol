#include <iostream>
#include <csignal>

#include "args.hpp"
#include "RDTClient.hpp"
#include "RDTServer.hpp"

volatile sig_atomic_t g_stop = 0;
volatile sig_atomic_t g_interrupted = 0;

static void sig_handler(int) {
    g_stop = 1;
    g_interrupted = 1;
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
            RDTClient client(args);
            client.run(args);
            break;
        }
        case Mode::SERVER: {
            RDTServer server(args);
            server.run();
            break;
        }

        default:
            std::cerr << "invalid arguments\n";
            return 1;
    }
}
