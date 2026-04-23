#!/usr/bin/env python3

import subprocess
import sys
import os
import time
import argparse

DEFAULT_BINARY = os.path.join(os.path.dirname(__file__), "..", "bin", "catalyst-linux-x86-64")
MOVETIME_MS = 3000

TACTICS = [
    ("8/8/8/8/5kp1/P7/8/1K1N4 w - - 0 1", ["b1c2"]),
    ("8/8/8/5N2/8/p7/N7/3K3k b - - 1 1", ["h1h2", "h1g1"]),
    ("8/8/1P6/5pr1/8/4R3/7k/2K5 w - - 0 1", ["e3e5"]),
    ("8/2p4P/8/kr6/6R1/8/8/1K6 w - - 0 1", ["b1c2"]),
    ("6k1/3b3r/1p1p4/p1n2p2/1PPNpP1q/P3Q1p1/1R1RB1P1/5K2 b - - 0 1", ["h4f4"]),
    ("r2r1n2/pp2bk2/2p1p2p/3q4/3PN1QP/2P3R1/P4PP1/5RK1 w - - 0 1", ["g4g7"]),
    ("8/1p3pp1/7p/5P1P/2k3P1/8/2K2P2/8 w - - 0 1", ["f5f6"]),
    ("r3qbrk/6p1/2b2pPp/p3pP1Q/PpPpP2P/3P1B2/2PB3K/R5R1 w - - 16 42", ["a1a2"]),
    ("r3kbbr/pp1n1p1P/3ppnp1/q5N1/1P1pP3/P1N1B3/2P1QP2/R3KB1R b KQkq b3 0 17", ["a5b6"]),
    ("r4qk1/6r1/1p4p1/2ppBbN1/1p5Q/P7/2P3PP/5RK1 w - - 2 25", ["g2g4"]),
]


def get_best_move(binary, fen, movetime):
    commands = f"position fen {fen}\ngo movetime {movetime}\n"
    try:
        proc = subprocess.Popen(
            [binary],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )

        proc.stdin.write(commands)
        proc.stdin.flush()

        best = None
        deadline = time.time() + (movetime / 1000.0) + 5.0

        while time.time() < deadline:
            line = proc.stdout.readline()
            if not line:
                break
            if line.strip().startswith("bestmove"):
                parts = line.strip().split()
                if len(parts) >= 2 and parts[1] != "(none)":
                    best = parts[1]
                break

        proc.stdin.write("quit\n")
        proc.stdin.flush()

        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()

        return best, None

    except FileNotFoundError:
        return None, f"binary not found: {binary}"
    except Exception as e:
        return None, str(e)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", default=DEFAULT_BINARY)
    parser.add_argument("--movetime", type=int, default=MOVETIME_MS)
    args = parser.parse_args()

    binary = os.path.abspath(args.binary)

    print("Tactics Test")
    print("=" * 55)
    print(f"Binary  : {binary}")
    print(f"Movetime: {args.movetime}ms per puzzle")
    print(f"Puzzles : {len(TACTICS)}")
    print("=" * 55)

    if not os.path.exists(binary):
        print(f"ERROR: binary not found at {binary}")
        print("       Run 'make' in the project root first.")
        sys.exit(1)

    passed = 0
    failed = 0
    errors = 0

    for idx, (fen, accepted) in enumerate(TACTICS, 1):
        start = time.time()
        got, err = get_best_move(binary, fen, args.movetime)
        elapsed = time.time() - start

        accepted_str = " or ".join(accepted)

        if err:
            print(f"  ERROR  [{idx:02d}] {err}")
            errors += 1
        elif got in accepted:
            print(f"  PASS   [{idx:02d}] {got}  ({elapsed:.1f}s)")
            passed += 1
        else:
            print(f"  FAIL   [{idx:02d}] got {got or 'None'}  expected {accepted_str}")
            failed += 1

    total = passed + failed + errors

    print(f"\n{'=' * 55}")
    print(f"Results: {passed} passed, {failed} failed, {errors} errors  ({total} total)")

    if failed == 0 and errors == 0:
        print("All puzzles solved!")
    else:
        solved_pct = int(passed / total * 100) if total > 0 else 0
        print(f"Solved {passed}/{total} ({solved_pct}%)")

    print("=" * 55)

    sys.exit(0 if (failed == 0 and errors == 0) else 1)


if __name__ == "__main__":
    main()