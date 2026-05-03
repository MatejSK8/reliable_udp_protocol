#!/usr/bin/env bash

BINARY="./ipk-rdt"
INPUT="./test_input.bin"
OUTPUT="./test_output.bin"
PORT=9005

if [[ ! -x "$BINARY" ]]; then
    echo "Error: Cannot find compiled $BINARY. Run this via 'make test'." >&2
    exit 1
fi

clear_netem() {
    sudo tc qdisc del dev lo root 2>/dev/null || true
}

set_netem() {
    clear_netem
    if [[ $# -gt 0 ]]; then
        sudo tc qdisc add dev lo root netem "$@"
    fi
}

cleanup() {
    clear_netem
    rm -f "$INPUT" "$OUTPUT"
    echo -e "\nCleanup complete. Network restored and temp files removed." >&2
}
trap 'cleanup; exit' INT TERM EXIT

run_test() {
    local test_name="$1"
    shift 
    
    printf "%-55s" "Running $test_name..."
    
    set_netem "$@"
    rm -f "$OUTPUT"

    timeout 15s "$BINARY" -s -p "$PORT" -o "$OUTPUT" >/dev/null 2>&1 &
    local SERVER_PID=$!

    sleep 0.2

    timeout 10s "$BINARY" -c -a 127.0.0.1 -p "$PORT" -i "$INPUT" >/dev/null 2>&1
    local CLIENT_EXIT=$?

    wait "$SERVER_PID" 2>/dev/null

    if [[ $CLIENT_EXIT -eq 124 ]]; then
        echo "FAIL (Client timed out)"
    elif [[ ! -f "$OUTPUT" ]] && [[ -s "$INPUT" ]]; then
        echo "FAIL (Output missing)"
    elif cmp -s "$INPUT" "$OUTPUT"; then
        echo "PASS"
    else
        echo "FAIL (Files differ)"
    fi
}

echo "=========================================================="
echo " Starting IPK-RDT Edge Case & Impairment Test Suite"
echo "=========================================================="

clear_netem


truncate -s 0 "$INPUT"
run_test "Test 1: Empty File (0 bytes)"

dd if=/dev/urandom of="$INPUT" bs=1 count=1 status=none
run_test "Test 2: Single Byte File"

dd if=/dev/urandom of="$INPUT" bs=1 count=1184 status=none
run_test "Test 3: Exact Max Payload (1184 bytes)"

dd if=/dev/urandom of="$INPUT" bs=1 count=75776 status=none
run_test "Test 4: Exact Full Window (75,776 bytes)"

echo ""
echo "--- Applying Network Impairments ---"
dd if=/dev/urandom of="$INPUT" bs=1M count=1 status=none

run_test "Test 5: 5% Packet Loss" loss 5%
run_test "Test 6: 20ms Delay" delay 20ms
run_test "Test 7: 15% Reordering + 10ms Delay" delay 10ms reorder 15%
run_test "Test 8: The Troll (10% Loss, 30ms Delay, 20% Reordering)" loss 10% delay 30ms reorder 20%

echo ""
echo "--- Stress Test ---"
clear_netem
dd if=/dev/urandom of="$INPUT" bs=1M count=10 status=none
run_test "Test 9: 10MB Pipelining Stress Test"

echo "=========================================================="
echo ""
echo "--- Protocol Logic & Integrity ---"

dd if=/dev/urandom of="$INPUT" bs=1M count=1 status=none
run_test "Test 10: Fast Retransmit (simulated drop)" loss 10%


run_test "Test 11: High Latency (100ms Delay)" delay 100ms

dd if=/dev/urandom of="$INPUT" bs=1M count=20 status=none
run_test "Test 12: 20MB Large Transfer"
run_cli_test() {
    local test_name="$1"
    local expected_exit="$2"
    shift 2
    local args=("$@")

    printf "%-55s" "Running CLI Test: $test_name..."

    $BINARY "${args[@]}" >/dev/null 2>&1
    local actual_exit=$?

    if [[ $actual_exit -eq $expected_exit ]]; then
        echo "PASS (Exit code $actual_exit)"
    else
        echo "FAIL (Expected $expected_exit, got $actual_exit)"
    fi
}


run_cli_test "Help flag (-h)" 0 -h

run_cli_test "Missing mode flag" 1 -p "$PORT"

run_cli_test "Missing port flag" 1 -s -a 127.0.0.1

run_cli_test "Invalid port (99999)" 1 -s -p 99999

run_cli_test "Non-existent input file" 1 -c -a 127.0.0.1 -p "$PORT" -i "./non_existent_file"

run_cli_test "Invalid timeout value (-w -5)" 1 -s -p "$PORT" -w -5

