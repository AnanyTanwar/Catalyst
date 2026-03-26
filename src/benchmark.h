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

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Catalyst {

struct BenchmarkResult {
  int depth;
  int threads;
  uint64_t totalNodes;
  int timeMs;
  int nps;
  std::vector<std::string> bestMoves;
  std::vector<uint64_t> positionNodes;
  std::vector<int> positionTimes;
};

class Benchmark {
public:
  static BenchmarkResult run(int depth, int threads = 0);
  static BenchmarkResult run_custom(const std::vector<std::string> &fens,
                                    int depth, int threads = 0);
  static void print_results(const BenchmarkResult &result, bool verbose = true);
  static const std::vector<std::string> &default_positions();

private:
  static uint64_t run_position(const std::string &fen, int depth, int threads,
                               std::string &bestMove);
};

} // namespace Catalyst