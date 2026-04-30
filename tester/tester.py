#!/usr/bin/env python3
import subprocess
import time
import os
import sys

BINARY = os.path.join(os.path.dirname(__file__), "..", "ipk-rdt")
INPUT = os.path.join(os.path.dirname(__file__), "..", "small_input_file")
OUTPUT = os.path.join(os.path.dirname(__file__), "..", "output_file")
PORT = 9001


def run_tc(cmd):
    result = subprocess.run(["sudo", "tc"] + cmd, capture_output=True)
    return result.returncode == 0


def set_netem(rules):
    run_tc(["qdisc", "del", "dev", "lo", "root"])
    if rules:
        run_tc(["qdisc", "add", "dev", "lo", "root", "netem"] + rules)


def clear_netem():
    run_tc(["qdisc", "del", "dev", "lo", "root"])


def run_test(label):
    print(f"\n--- {label} ---")

    if os.path.exists(OUTPUT):
        os.remove(OUTPUT)

    server = subprocess.Popen(
        [BINARY, "-s", "-p", str(PORT), "-o", OUTPUT, "-w", "5"],
        stderr=subprocess.DEVNULL,
    )
    time.sleep(0.2)

    client = subprocess.Popen(
        [BINARY, "-c", "-a", "127.0.0.1", "-p", str(PORT), "-i", INPUT, "-w", "5"],
        stderr=subprocess.DEVNULL,
    )

    try:
        client.wait(timeout=30)
        server.wait(timeout=10)
    except subprocess.TimeoutExpired:
        print("FAIL — timeout")
        client.kill()
        server.kill()
        return

    result = subprocess.run(["cmp", INPUT, OUTPUT], capture_output=True)
    if result.returncode == 0:
        print("PASS — files match")
    else:
        print("FAIL — files differ")


if __name__ == "__main__":
    if not os.path.exists(BINARY):
        print("Build the project first: make")
        sys.exit(1)
    if not os.path.exists(INPUT):
        print("Create input_file first: dd if=/dev/urandom of=input_file bs=1M count=10")
        sys.exit(1)

    try:
        # Test 1 — clean network
        clear_netem()
        run_test("Test 1: clean network")

        # Test 2 — 10% packet loss
        set_netem(["loss", "10%"])
        run_test("Test 2: 10% packet loss")

        # Test 3 — loss + reordering + delay
        set_netem(["loss", "5%", "delay", "30ms", "reorder", "20%"])
        run_test("Test 3: 5% loss + 30ms delay + 20% reorder")
    finally:
        clear_netem()
        print("\nnetem cleared.")

    print("Done.")
