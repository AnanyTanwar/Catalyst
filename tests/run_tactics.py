#!/usr/bin/env python3
# Catalyst tactics test runner

import subprocess
import sys
import os
import time

ENGINE_PATH = os.path.join(os.path.dirname(__file__), "..", "Catalyst")
MOVETIME_MS = 2000

# (fen, accepted_moves, description)
# Multiple accepted moves for positions where several moves win equally
TACTICS = [
    # Mates
    (
        "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4",
        ["h5f7"],
        "Scholar's mate (Qxf7#)"
    ),
    (
        "6k1/5ppp/8/8/8/8/5PPP/4R1K1 w - - 0 1",
        ["e1e8"],
        "Back rank mate (Re8#)"
    ),
    (
        "r1b2k1r/ppp1bppp/2n5/3q4/3pN3/8/PPP2PPP/RNBQKB1R b KQ - 0 8",
        ["d5g2"],
        "Queen sac into back rank"
    ),
    # Tactics
    (
        "r1b1kb1r/pppp1ppp/2n5/4p3/2BnP3/5N2/PPPP1PPP/RNBQ1RK1 w kq - 0 5",
        ["f3d4"],
        "Knight recapture fork (Nxd4)"
    ),
    (
        "4r1k1/pp3ppp/3b4/3p4/3P4/2N5/PP3PPP/4R1K1 w - - 0 1",
        ["c3d5"],
        "Knight fork on d5"
    ),
    (
        "r1bqr1k1/ppp2ppp/2n2n2/3pp3/1bBPP3/2N1BN2/PPP2PPP/R2QK2R w KQ - 0 8",
        ["c4b5"],
        "Pin winning material (Bxb5)"
    ),
    (
        "2r3k1/5ppp/p7/1p6/1P6/P7/5PPP/2R3K1 w - - 0 1",
        ["c1c8"],
        "Rook trade into won ending"
    ),
    (
        "r2q1rk1/ppp2ppp/2n1bn2/3pp3/1bBPP3/2NBPN2/PPP2PPP/R2QK2R w KQ - 0 8",
        ["c4b5"],
        "Bishop pin on c6 knight"
    ),
    # Endgame
    (
        "8/8/8/8/8/4k3/4p3/4K3 b - - 0 1",
        ["e3d2", "e3f2"],
        "King and pawn opposition"
    ),
    (
        "8/1k6/8/8/8/8/1K1R4/8 w - - 0 1",
        ["d2d7"],
        "Rook cuts off king (Rd7+)"
    ),
]

def get_best_move(fen, movetime):
    commands = f"uci\nisready\nposition fen {fen}\ngo movetime {movetime}\n"
    try:
        proc = subprocess.Popen(
            [ENGINE_PATH],
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
            line = line.strip()
            if line.startswith("bestmove"):
                parts = line.split()
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
        return None, "engine binary not found"
    except Exception as e:
        return None, str(e)

def main():
    print("Catalyst Tactics Test Suite")
    print("=" * 55)
    print(f"Engine  : {ENGINE_PATH}")
    print(f"Movetime: {MOVETIME_MS}ms per puzzle")
    print(f"Puzzles : {len(TACTICS)}")
    print("=" * 55)

    if not os.path.exists(ENGINE_PATH):
        print(f"ERROR: Engine not found at: {ENGINE_PATH}")
        print("       Run 'make' in the project root first.")
        sys.exit(1)

    passed = 0
    failed = 0
    errors = 0

    for idx, (fen, accepted, desc) in enumerate(TACTICS, 1):
        start = time.time()
        got, err = get_best_move(fen, MOVETIME_MS)
        elapsed = time.time() - start

        if err:
            print(f"  ERROR  [{idx:02d}] {desc}")
            print(f"         {err}")
            errors += 1
        elif got in accepted:
            print(f"  PASS   [{idx:02d}] {desc:<45}  {got}  ({elapsed:.1f}s)")
            passed += 1
        else:
            accepted_str = " or ".join(accepted)
            print(f"  FAIL   [{idx:02d}] {desc:<45}  got {got or 'None'}  expected {accepted_str}")
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