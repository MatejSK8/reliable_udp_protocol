# Project 2 - Reliable File Transfer over UDP

General build, submission, and documentation rules are defined in the shared IPK project guidelines repository. This document defines the assignment-specific requirements for Project 2.

## Deadlines

See deadlines in IS VUT.

## Global assignment rules

This project is governed by the common IPK project assignment and repository rules in addition to the project-specific requirements below.

Students MUST also follow the shared IPK project guidelines repository, especially for:

* repository structure,
* build and run conventions,
* submission rules,
* required project documentation,
* academic integrity and authorship rules.

## Assignment

1. Implement a command-line client and server that transfer a single continuous byte stream over UDP.
2. Design and implement your own application protocol that provides reliable and ordered delivery over an unreliable channel.
3. Create relevant automated tests for the project.

The assignment target is a simplified transport protocol inspired by TCP, implemented in user space above UDP.

The goal is not only to transfer the data correctly, but also to transfer it as fast as reasonably possible under adverse network conditions.

The required executable name is `ipk-rdt`. The project `Makefile` MUST build this executable in the repository root.

## Specification

The application transfers one file or byte stream from client to server. The implementation must use UDP sockets for network communication. The reliable transport protocol itself is part of the assignment and must be implemented by the student.

The program may be terminated at any moment with `SIGTERM` or `SIGINT`. Upon receiving either signal, the application MUST terminate promptly without crashing. It MUST NOT leave temporary files behind.

### Required functionality

The submitted solution MUST satisfy all of the following:

* All protocol communication between client and server MUST run over UDP only. TCP or another reliable transport protocol MUST NOT be used for any part of the transfer.
* The solution MUST contain both a client and a server mode in a single executable named `ipk-rdt`.
* The client mode MUST be selected by the `-c` option and the server mode MUST be selected by the `-s` option.
* The transferred content is an arbitrary byte stream. The implementation MUST correctly transfer both text and binary files.
* The implementation MUST support both IPv4 and IPv6.
* The implementation MUST support reading input from a file or from standard input.
* The implementation MUST support writing output to a file or to standard output.
* The receiver output MUST be byte-for-byte identical to the sender input.
* The protocol MUST tolerate packet loss, packet reordering, packet duplication, and network jitter.
* The protocol MUST NOT assume that packets arrive in order.
* The protocol MUST detect and discard duplicate data packets.
* The protocol MUST include acknowledgements and retransmissions.
* The protocol MUST include protection against accidental confusion of packets from different transfers, for example by using a connection identifier or equivalent mechanism.
* The protocol MUST include integrity protection of its own protocol units, for example a checksum over header and payload.
* Corrupted, truncated, malformed, or otherwise invalid protocol units MUST be detected and ignored.
* The sender MUST support pipelined transmission with more than one unacknowledged data segment in flight when the input is large enough for multiple segments. A pure stop-and-wait design is considered insufficient for this assignment.
* The implementation MUST terminate the transfer in a well-defined way so that both sides can determine whether the transfer succeeded or failed.
* The solution MUST handle empty input correctly.
* The solution SHOULD process the input and output as a stream. Automated tests MAY use inputs of at least tens of megabytes.
* One protocol data unit carried inside one UDP datagram MUST NOT exceed 1200 bytes of UDP payload, including the student's protocol header.

### What is not required

The following are explicitly out of scope unless a student chooses to implement them voluntarily:

* compatibility with TCP, QUIC, SCTP, or another existing transport protocol,
* congestion control behavior comparable to full TCP,
* encryption or authentication,
* transfer resume after process crash,
* serving multiple clients in parallel,
* bidirectional data transfer inside one session.

### Command-line interface

The assignment requires support for the following interface. Additional optional parameters are allowed, but the required interface must work exactly as specified.

#### Server

```sh
./ipk-rdt -s -p PORT [-a ADDRESS] [-o OUTPUT] [-w TIMEOUT] [-h | --help]
```

#### Client

```sh
./ipk-rdt -c -a HOST -p PORT [-i INPUT] [-w TIMEOUT] [-h | --help]
```

where:

* `-h` or `--help` writes usage instructions to `stdout` and terminates with exit code `0`.
* `-s` starts the receiving side of the application.
* `-c` starts the sending side of the application.
* Exactly one of `-c` or `-s` MUST be specified.
* `-p PORT` specifies the UDP port number.
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

### Output and exit codes

* On success, the client exits with code `0` after the whole input has been acknowledged and the protocol session has been cleanly finished.
* On success, the server exits with code `0` after the whole transferred byte stream has been written to the selected output and the protocol session has been cleanly finished.
* On failure, the application exits with a non-zero exit code.
* Informational, debug, and error messages MUST be written to `stderr`.
* If the server writes transferred data to `stdout`, then `stdout` MUST contain only the transferred data and nothing else.
* The client MUST NOT write transferred payload data to `stdout`.

### Protocol requirements

The protocol format is part of the solution design, but the resulting behavior MUST satisfy the following properties:

* ordered delivery to the application,
* reliable delivery despite loss and duplication,
* no duplicated bytes in the resulting output,
* correct handling of out-of-order arrivals,
* retransmission after timeout or equivalent loss detection,
* finite failure handling when the peer is unreachable or no protocol progress is made,
* explicit session establishment before data transfer,
* explicit session teardown after data transfer,
* robust behavior when duplicate or delayed packets from an earlier phase of the same transfer arrive late,
* reasonable retransmission timing that does not depend on busy waiting.

Students MAY choose cumulative acknowledgements, selective acknowledgements, Go-Back-N, Selective Repeat, or another defensible design. The choice MUST be described in `README.md`.

### Execution examples

Transfer file to file:

```sh
./ipk-rdt -s -p 9000 -o received.bin
./ipk-rdt -c -a 127.0.0.1 -p 9000 -i sample.bin
```

Transfer stdin to stdout:

```sh
./ipk-rdt -s -p 9000
printf 'IPK\n' | ./ipk-rdt -c -a 127.0.0.1 -p 9000
```

Transfer stdin to file:

```sh
./ipk-rdt -s -p 9000 -o output.data
cat input.data | ./ipk-rdt -c -a localhost -p 9000
```

## Automated testing expectations

Automated tests will evaluate both correctness and efficiency.

The tests MAY include combinations of the following conditions:

* random binary input data,
* empty input,
* small files and larger files,
* file-to-file transfer,
* `stdin` to file transfer,
* file to `stdout` transfer,
* `stdin` to `stdout` transfer,
* IPv4 and IPv6 addressing,
* packet loss,
* packet duplication,
* packet reordering,
* corrupted, truncated, malformed, or unexpected protocol packets,
* variable delay and jitter,
* repeated retransmission scenarios,
* hidden time limits for transfer completion.

Network impairment during evaluation may be introduced using Linux traffic shaping tools such as `tc netem`, a dedicated testing proxy, or another equivalent mechanism.

Implementations that busy-wait, retransmit the whole transfer unnecessarily, or rely on pure stop-and-wait behavior should be expected to fail timing-oriented tests.

The 1200-byte UDP payload limit is chosen to reduce the risk of IP fragmentation and to keep the protocol practical across both IPv4 and IPv6 environments.

## Submission requirements

In addition to the common IPK requirements, the submitted repository MUST contain at least:

* source code of the solution,
* a `Makefile` that builds `ipk-rdt` in the repository root,
* a student `README.md` describing the solution design,
* relevant automated tests created by the student,
* any additional files needed to build and run the submitted solution in the defined environment.

## Documentation requirements

In addition to the common IPK requirements, the student `README.md` MUST describe at least:

* protocol packet/header format,
* session establishment and termination,
* sequencing and acknowledgement strategy,
* retransmission strategy and timeout handling,
* duplicate and out-of-order packet handling,
* connection identification strategy,
* chosen segment size and window behavior,
* measured behavior in the student's test environment,
* appropriate UML diagrams,
* known limitations of the implementation.

## Evaluation

The project will be evaluated by automated tests.

An oral defence will also be held. During the defence, the student is obligated to justify and explain the chosen algorithm, test coverage, source code quality, and documentation quality.

Students will be notified via IS VUT about registration for the oral defence. Registration will be opened after automated evaluation. The oral defence is expected to be held within a week after submission.

The evaluation focuses mainly on:

* protocol correctness,
* resilience to unreliable network conditions,
* implementation quality,
* project structure and documentation,
* quality and relevance of student-written automated tests,
* transfer completion time under selected test scenarios.

Submissions are considered unacceptable especially if:

* the transfer is not reliable,
* the receiver output differs from the sender input,
* the solution uses TCP for the actual transfer (or other inherently reliable transport protocol, such as QUIC),
* the program crashes, hangs, or leaks resources in common scenarios,
* the required CLI is not supported,
* the implementation is so slow that it does not satisfy evaluation time limits.

## Recommended sources

* RFC 768: User Datagram Protocol, 1980. Online. Request for Comments. Internet Engineering Task Force.
* RFC 793 / RFC 9293: Transmission Control Protocol. Online. Request for Comments. Internet Engineering Task Force.
* RFC 6298: Computing TCP's Retransmission Timer. Online. Request for Comments. Internet Engineering Task Force.
* KUROSE, James F. and ROSS, Keith W. Computer Networking: A Top-Down Approach. Pearson.
* Linux `tc-netem` manual page and related documentation.

