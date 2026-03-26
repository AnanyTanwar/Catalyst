#!/usr/bin/env python3
# Catalyst perft test runner

import subprocess
import sys
import os
import time

ENGINE_PATH = os.path.join(os.path.dirname(__file__), "..", "Catalyst")
EPD_PATH    = os.path.join(os.path.dirname(__file__), "perft.epd")
MAX_DEPTH   = 5  # set to 6 for full suite (slow)

def parse_epd(line):
    parts = line.strip().split(";")
    fen = parts[0].strip()
    expected = {}
    for part in parts[1:]:
        part = part.strip()
        if part.startswith("D") and len(part) > 1:
            try:
                depth = int(part[1])
                nodes = int(part[2:].strip())
                expected[depth] = nodes
            except ValueError:
                pass
    return fen, expected

def run_perft(fen, depth):
    commands = f"uci\nisready\nposition fen {fen}\ngo perft {depth}\nquit\n"
    try:
        proc = subprocess.Popen(
            [ENGINE_PATH],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        stdout, _ = proc.communicate(input=commands, timeout=120)
    except subprocess.TimeoutExpired:
        proc.kill()
        return None, "timeout"
    except FileNotFoundError:
        return None, "engine not found"

    for line in reversed(stdout.splitlines()):
        line = line.strip()
        if line.startswith("Nodes:"):
            try:
                return int(line.split(":")[1].strip()), None
            except (IndexError, ValueError):
                pass

    return None, "could not parse node count"

def main():
    print("Catalyst Perft Test Suite")
    print("=" * 50)

    if not os.path.exists(ENGINE_PATH):
        print(f"ERROR: Engine binary not found at: {ENGINE_PATH}")
        print("       Run 'make' in the project root first.")
        sys.exit(1)

    if not os.path.exists(EPD_PATH):
        print(f"ERROR: perft.epd not found at: {EPD_PATH}")
        sys.exit(1)

    with open(EPD_PATH) as f:
        positions = [
            parse_epd(l) for l in f
            if l.strip() and not l.startswith("#")
        ]

    passed = 0
    failed = 0
    errors = 0

    for idx, (fen, expected) in enumerate(positions, 1):
        print(f"\nPosition {idx}: {fen}")

        for depth in sorted(expected.keys()):
            if depth > MAX_DEPTH:
                continue

            expected_nodes = expected[depth]
            start = time.time()
            got, err = run_perft(fen, depth)
            elapsed = time.time() - start

            if err:
                print(f"  D{depth}  ERROR    {err}")
                errors += 1
            elif got == expected_nodes:
                print(f"  D{depth}  PASS     {got:>12,} nodes  ({elapsed:.2f}s)")
                passed += 1
            else:
                diff = got - expected_nodes
                print(f"  D{depth}  FAIL     got {got:>12,}  expected {expected_nodes:>12,}  diff {diff:+,}")
                failed += 1

    print(f"\n{'=' * 50}")
    print(f"Results: {passed} passed, {failed} failed, {errors} errors")
    print("=" * 50)
    sys.exit(0 if (failed == 0 and errors == 0) else 1)

if __name__ == "__main__":
    main()