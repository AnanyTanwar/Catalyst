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

#include <atomic>
#include <chrono>

#include "types.h"

namespace Catalyst {

struct SearchLimits {
  int depth = 64;     // max search depth
  int movetime = 0;   // fixed time per move (ms), 0 = not set
  int wtime = 0;      // white clock (ms)
  int btime = 0;      // black clock (ms)
  int winc = 0;       // white increment (ms)
  int binc = 0;       // black increment (ms)
  int movestogo = 0;  // moves until next time control, 0 = sudden death
  uint64_t nodes = 0; // node limit, 0 = no limit
  int mate = 0;       // search for mate in N moves, 0 = disabled
  bool infinite = false;
  bool ponder = false; // go ponder — time suspended until ponderhit or stop
};

// ---------------------------------------------------------------------------
// TimeManager
//
// Dynamic time management with four scaling axes:
//
//  1. Base allocation  — function of clock, increment, and movestogo
//  2. Score instability scale — more time when eval is volatile across iters
//  3. Best-move stability scale — less time when best move has been unchanged
//  4. Node fraction scale — less time when root best-move consumed most nodes
//
// The search calls update_scale() after each iteration and reads
// soft_limit_reached() to decide whether to start the next depth.
// The hard limit (maxMs) is checked every 1024 nodes inside search.
// ---------------------------------------------------------------------------
class TimeManager {
public:
  TimeManager() = default;

  // Called at the start of every "go". Computes base optimalMs / maxMs.
  void init(const SearchLimits& limits, Color stm, int moveOverhead);

  // Called just before iterative deepening starts.
  void start_clock();

  // Called after each completed iteration (from best_move loop).
  // Parameters:
  //   bestMoveChanged  — did the best move change this iteration?
  //   scoreDelta       — abs(thisScore - prevScore); 0 on depth 1
  //   bestMoveNodes    — nodes spent on the best move at root
  //   totalNodes       — total nodes searched this iteration
  void update_scale(bool bestMoveChanged, int scoreDelta, uint64_t bestMoveNodes,
                    uint64_t totalNodes, int currentDepth, int currentScore);

  void stop() { stopped.store(true, std::memory_order_relaxed); }

  // Called when GUI sends "ponderhit" — transition from ponder mode to
  // normal timed search. Restarts the clock from now and computes limits
  // from the stored SearchLimits (which still has the real clock times).
  void ponderhit(Color stm, int moveOverhead);

  // Called when GUI sends "ponderhit" to check if we're still in ponder mode.
  [[nodiscard]] bool is_pondering() const { return pondering_.load(std::memory_order_relaxed); }

  [[nodiscard]] int elapsed_ms() const;
  [[nodiscard]] bool time_up(uint64_t nodes) const;
  [[nodiscard]] bool soft_limit_reached() const;
  [[nodiscard]] bool is_stopped() const { return stopped.load(std::memory_order_relaxed); }
  [[nodiscard]] const SearchLimits& limits() const { return lims; }

  // Exposed so search.cpp can log them if desired
  int optimalMs = 0;
  int maxMs = 0;

private:
  SearchLimits lims;
  std::chrono::steady_clock::time_point startTime;
  std::atomic<bool> stopped{false};
  std::atomic<bool> pondering_{false}; // true while in ponder mode

  // Running scale factor applied to optimalMs. Starts at 1.0.
  // Clamped to [MIN_SCALE, MAX_SCALE] after each update.
  double scale = 1.0;

  // Tracks how many consecutive iterations the best move has been stable
  int stableIters = 0;

  // Base optimal time (before scaling). Stored so we can re-apply scale.
  int baseOptimalMs = 0;

  // Score at depth 1 (anchor for complexity calculation)
  int scoreAtDepth1 = 0;
  bool hasDepth1Score = false;
  int remainingMs_ = 0;

  // Hard cap multiplier on optimalMs → maxMs
  static constexpr double MAX_HARD_MULT = 5.0;

  // Minimum and maximum scale
  static constexpr double MIN_SCALE = 0.5;
  static constexpr double MAX_SCALE = 2.5;

  // Best-move stability: scale down by this factor per stable iteration
  static constexpr double STABILITY_SCALE[6] = {
      2.50, // 0 stable iters — brand new best move, spend more time
      1.20, // 1
      1.00, // 2
      0.90, // 3
      0.80, // 4
      0.70, // 5+ stable iters — very confident, cut time
  };

  // Score instability: extra scale when eval jumped by more than threshold
  // scoreDelta >= 30  → ×1.25
  // scoreDelta >= 15  → ×1.12
  // scoreDelta < 15   → ×1.00
  static constexpr int SCORE_INSTAB_THRESH_HIGH = 30;
  static constexpr int SCORE_INSTAB_THRESH_LOW = 15;
  static constexpr double SCORE_INSTAB_SCALE_HIGH = 1.25;
  static constexpr double SCORE_INSTAB_SCALE_LOW = 1.12;

  // Node fraction: if best-move used < NODE_FRAC_THRESHOLD of total nodes,
  // we're not confident — spend more time. If it used a lot, we're confident.
  static constexpr double NODE_FRAC_THRESHOLD = 0.50; // < 50% → scale up
  static constexpr double NODE_FRAC_SCALE_UP = 1.20;
  static constexpr double NODE_FRAC_SCALE_DN = 0.85; // > 80% → scale down
  static constexpr double NODE_FRAC_DN_THRESH = 0.80;

  // Complexity: how much does current score differ from depth-1 score?
  // Larger divergence → engine is uncertain → spend more time
  static constexpr double COMPLEXITY_BASE = 0.77;
  static constexpr double COMPLEXITY_DIVISOR = 386.0;
  static constexpr double COMPLEXITY_MAX = 200.0;
  static constexpr double COMPLEXITY_LOG_FACTOR = 0.6;
};

} // namespace Catalyst
