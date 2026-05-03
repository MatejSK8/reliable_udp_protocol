#!/usr/bin/env bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
BINARY="$DIR/../ipk-rdt"
INPUT="$DIR/../small_input_file"
OUTPUT="$DIR/../output_file"
PORT=9001

clear_netem() {
    sudo tc qdisc del dev lo root 2>/dev/null || true
}

set_netem() {
    clear_netem
    if [[ $# -gt 0 ]]; then
        sudo tc qdisc add dev lo root netem "$@"
    fi
}

trap 'clear_netem; echo -e "\nnetem cleared." >&2; exit' INT TERM EXIT

run_test() {
    local label="$1"
    echo -e "\n--- $label ---" >&2
    
    rm -f "$OUTPUT"

    # Start server in background. 2>/dev/null hides stderr, but stdout prints to your terminal.
    "$BINARY" -s -p "$PORT" -o "$OUTPUT" -w 5 2>/dev/null &
    local server_pid=$!

    sleep 0.2

    # Start client in foreground with 30s timeout.
    timeout 30s "$BINARY" -c -a 127.0.0.1 -p "$PORT" -i "$INPUT" -w 5 2>/dev/null
    local client_exit=$?

    # 124 is the exit code 'timeout' returns if it kills the process
    if [[ $client_exit -eq 124 ]]; then
        echo "FAIL   timeout"
        kill "$server_pid" 2>/dev/null
        wait "$server_pid" 2>/dev/null
        return
    fi

    # Wait for server up to 10 seconds (Matches Python's server.wait(timeout=10))
    local count=0
    local server_timed_out=1
    while [[ $count -lt 100 ]]; do
        if ! kill -0 "$server_pid" 2>/dev/null; then
            server_timed_out=0
            break
        fi
        sleep 0.1
        ((count++))
    done

    if [[ $server_timed_out -eq 1 ]]; then
        echo "FAIL   timeout"
        kill "$server_pid" 2>/dev/null
        wait "$server_pid" 2>/dev/null
        return
    fi

    # Clean up the finished server process
    wait "$server_pid" 2>/dev/null

    if cmp -s "$INPUT" "$OUTPUT"; then
        echo "PASS   files match"
    else
        echo "FAIL   files differ"
    fi
}

if [[ ! -x "$BINARY" ]]; then
    echo "Build the project first: make" >&2
    exit 1
fi

if [[ ! -f "$INPUT" ]]; then
    echo "Create input_file first: dd if=/dev/urandom of=$INPUT bs=1M count=10" >&2
    exit 1
fi

clear_netem
run_test "Test 1: clean network"

set_netem loss 10%
run_test "Test 2: 10% packet loss"

set_netem loss 5% delay 30ms reorder 20%
run_test "Test 3: 5% loss + 30ms delay + 20% reorder"