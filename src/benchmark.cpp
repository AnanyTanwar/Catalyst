// Catalyst is a UCI compliant chess engine
// Copyright (C) 2026 Anany Tanwar

// Catalyst is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// Catalyst is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "benchmark.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>

#include "board.h"
#include "thread.h"
#include "timeman.h"
#include "tt.h"
#include "types.h"

namespace Catalyst {

const std::vector<std::string> &Benchmark::default_positions() {
  static const std::vector<std::string> positions = {
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
      "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
      "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
      "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
      "2rr3k/pp3pp1/1nnqbN1p/3pN3/2pP4/2P3Q1/PPB4P/R4RK1 w - - 0 1",
      "r1bq1rk1/pp2bppp/2n1pn2/3p4/3P4/2N1PN2/PP2BPPP/R1BQ1RK1 w - - 0 1",
      "r1bqr1k1/pp3ppp/2pb1n2/3p4/3P4/2NBPN2/PP3PPP/R1BQR1K1 w - - 0 1",
      "r2qk2r/ppp1bppp/2np1n2/1B2p3/4P3/3P1N2/PPP2PPP/RNBQR1K1 b kq - 0 1",
      "3r1rk1/p4ppp/qp2p3/2ppPb2/5B2/1P1P2P1/P1P2P1P/R2QR1K1 w - - 0 1",
      "r1bq1rk1/1pp2ppp/p1np1n2/4p3/2B1P3/2NP1N2/PPP2PPP/R1BQR1K1 w - - 0 1",
      "r3r1k1/1ppq1ppp/p2p1n2/4pb2/4P1b1/1NNP2P1/PPP2PBP/R1BQR1K1 w - - 0 1",
      "r2q1rk1/ppp2ppp/2np1n2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQ1RK1 w - - 0 1",
      "r4rk1/pp3ppp/3p1n2/q1pPp3/4P3/2P2N2/PP2QPPP/R4RK1 w - - 0 1",
      "r1b2rk1/pp2qppp/2np1n2/2p1p3/2B1P3/2NP1N2/PPP2PPP/R1BQ1RK1 w - - 0 1",
      "r1bqr1k1/ppp2ppp/2np1n2/4p3/2B1P3/2NP1N2/PPP2PPP/R1BQR1K1 w - - 0 1"};
  return positions;
}

uint64_t Benchmark::run_position(const std::string &fen, int depth, int threads,
                                 std::string &bestMove) {
  Board board;
  board.set_fen(fen);

  ThreadPool pool(threads);

  SearchLimits limits;
  limits.depth = depth;
  limits.infinite = false;

  TimeManager tm;
  tm.init(limits, board.side_to_move(), 0);
  tm.start_clock();

  tt.clear();
  pool.clear_all();

  Move move = pool.search(board, tm);
  bestMove = move_to_uci(move);

  return pool.total_nodes();
}

BenchmarkResult Benchmark::run(int depth, int threads) {
  return run_custom(default_positions(), depth, threads);
}

BenchmarkResult Benchmark::run_custom(const std::vector<std::string> &fens,
                                      int depth, int threads) {
  BenchmarkResult result;
  result.depth = depth;

  if (threads <= 0) {
    threads = std::max(1, int(std::thread::hardware_concurrency()));
  }
  result.threads = threads;

  result.bestMoves.reserve(fens.size());
  result.positionNodes.reserve(fens.size());
  result.positionTimes.reserve(fens.size());

  auto totalStart = std::chrono::steady_clock::now();
  result.totalNodes = 0;

  std::cout << "\nBenchmarking " << fens.size() << " positions at depth "
            << depth << " (" << threads << " thread" << (threads > 1 ? "s" : "")
            << ")...\n\n";

  for (size_t i = 0; i < fens.size(); ++i) {
    auto posStart = std::chrono::steady_clock::now();

    std::string bestMove;
    uint64_t nodes = run_position(fens[i], depth, threads, bestMove);

    auto posEnd = std::chrono::steady_clock::now();
    int posTime =
        std::chrono::duration_cast<std::chrono::milliseconds>(posEnd - posStart)
            .count();

    result.bestMoves.push_back(bestMove);
    result.positionNodes.push_back(nodes);
    result.positionTimes.push_back(posTime);
    result.totalNodes += nodes;

    std::cout << "pos " << (i + 1) << ": bestmove " << bestMove
              << " | nodes: " << nodes << " | time: " << posTime << "ms\n";
  }

  auto totalEnd = std::chrono::steady_clock::now();
  result.timeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                      totalEnd - totalStart)
                      .count();
  result.nps =
      result.timeMs > 0 ? int(result.totalNodes * 1000 / result.timeMs) : 0;

  return result;
}

void Benchmark::print_results(const BenchmarkResult &result, bool verbose) {
  std::cout << "\n=== BENCHMARK RESULTS ===\n";
  std::cout << "Depth:    " << result.depth << "\n";
  std::cout << "Threads:  " << result.threads << "\n";
  std::cout << "Positions: " << result.bestMoves.size() << "\n";
  std::cout << "Total nodes: " << result.totalNodes << "\n";
  std::cout << "Total time:  " << result.timeMs << "ms\n";
  std::cout << "NPS:      " << result.nps << "\n";

  if (verbose && !result.positionTimes.empty()) {
    std::cout << "\nPer-position breakdown:\n";
    std::cout << std::left << std::setw(8) << "Pos" << std::setw(12)
              << "Best Move" << std::setw(15) << "Nodes" << std::setw(10)
              << "Time(ms)" << "\n";
    std::cout << std::string(45, '-') << "\n";

    for (size_t i = 0; i < result.bestMoves.size(); ++i) {
      std::cout << std::left << std::setw(8) << (i + 1) << std::setw(12)
                << result.bestMoves[i] << std::setw(15)
                << result.positionNodes[i] << std::setw(10)
                << result.positionTimes[i] << "\n";
    }
  }

  std::cout << "\n";
}

} // namespace Catalyst