/**
 * @file args.cpp
 * @brief IPK Project 1 - L4 Scanner
 * @author xmikusm00
 */

#include <iostream>
#include <cstdlib>
#include <getopt.h>

#include "args.hpp"

void print_usage()
{
    std::cout << R"(

#### Server

```sh
./ipk-rdt -s -p port [-a ADDRESS] [-o OUTPUT] [-w TIMEOUT] [-h | --help]
```

#### Client

```sh
./ipk-rdt -c -a HOST -p port [-i INPUT] [-w TIMEOUT] [-h | --help]
```

* `-h` or `--help` writes usage instructions to `stdout` and terminates with exit code `0`.
* `-s` starts the receiving side of the application.
* `-c` starts the sending side of the application.
* Exactly one of `-c` or `-s` MUST be specified.
* `-p port` specifies the UDP port number.
* `-a ADDRESS` in server mode specifies the local bind address. If omitted, the server listens on all suitable local addresses.
* `-a HOST` in client mode specifies the destination hostname or IPv4/IPv6 address. If a hostname resolves to multiple addresses, the implementation MUST attempt at least one of them.
* `-i INPUT` specifies the input file to send. If omitted or if `INPUT` is `-`, the client reads from `stdin`.
* `-o OUTPUT` specifies the output file to create or overwrite. If omitted or if `OUTPUT` is `-`, the server writes the received data to `stdout`.
* `-w TIMEOUT` specifies a positive timeout in whole seconds. If omitted, the value `1` is used.
* All arguments can be given in any order.

Timeout semantics:

* The same `-w TIMEOUT` value is used during session establishment, data transfer, and session termination.
* `TIMEOUT` is the maximum allowed interval without protocol progress.
* Protocol progress means receiving a valid protocol unit that advances the session state beyond what was already known — for example a successful handshake step, an acknowledgement covering previously unacknowledged data, arrival of new (not duplicate) data, or a successful termination step. Receiving duplicates or retransmissions of already processed information does not count as progress.
* If no protocol progress is observed for `TIMEOUT` seconds, the application MUST terminate with a non-zero exit code. It MUST NOT wait forever.
* The implementation is expected to use finer internal timers for retransmissions and related recovery actions, so that packet loss can be handled before the terminating interval expires.
* The implementation MAY also use adaptive retransmission logic, but the externally visible termination behavior MUST still respect the user-selected `-w` value.
* Automated tests MAY start the application with different `-w` values and verify that timeout-based termination follows that setting. The `-w` parameter therefore MUST have a real effect on runtime behavior.

Invalid argument combinations or missing mandatory arguments MUST cause termination with a non-zero exit code and an understandable error message written to `stderr`.

The server MUST handle exactly one transfer per process run. The first successfully established protocol session is considered the handled transfer. Invalid, duplicate, malformed, or unrelated packets received before session establishment MUST be ignored. After one successful or failed handled transfer, the server MUST terminate.
)" << std::endl;
}

Args parse_args(const int argc, char *argv[])
{
    Args args;
    int opt;

    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--help")
        {
            print_usage();
            exit(0);
        }
    }

    while ((opt = getopt(argc, argv, "scp:a:i:o:w:h")) != -1)
    {
        switch (opt)
        {
        case 's':
            if (args.mode == Mode::CLIENT)
            {
                std::cerr << "Cannot specify both -s and -c" << std::endl;
                print_usage();
                exit(1);
            }
            args.mode = Mode::SERVER;
            break;
        case 'c':
            if (args.mode == Mode::SERVER)
            {
                std::cerr << "Cannot specify both -s and -c" << std::endl;
                print_usage();
                exit(1);
            }
            args.mode = Mode::CLIENT;
            break;
        case 'h':
            print_usage();
            exit(0);
        case 'p':
            args.port = static_cast<uint16_t>(std::stoi(optarg));
            if (args.port <= 0 || args.port > 65535)
            {
                std::cerr << "Invalid port number: " << args.port << std::endl;
                print_usage();
                exit(1);
            }
            break;
        case 'a':
            // SERVER/HOST
            args.address_host = optarg;
            break;
        case 'i':
            // INPUT
            args.input = optarg;
            break;
        case 'o':
            // OUTPUT
            args.output = optarg;
            break;
        case 'w':
            args.timeout = std::stoi(optarg);
            if (args.timeout <= 0)
            {
                std::cerr << "Invalid timeout value: " << args.timeout << std::endl;
                print_usage();
                exit(1);
            }
            break;

        default:
            std::cerr << "Unknown option: " << static_cast<char>(opt) << std::endl;
            print_usage();
            exit(1);
        }
    }

    return args;
}
