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

#include "timeman.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace Catalyst {

static int compute_moves_to_go(int remaining, int movestogo) {
  if (movestogo > 0)
    return std::clamp(movestogo, 2, 60);
  if (remaining > 60000)
    return 40;
  if (remaining > 30000)
    return 35;
  if (remaining > 10000)
    return 30;
  return 20;
}

static void compute_limits(int remaining, int inc, int movestogo,
                           double maxHardMult, int &outOptimal, int &outMax,
                           int &outBase) {
  if (remaining < 100) {
    outBase = std::max(1, remaining / 8);
    outOptimal = outBase;
    outMax = std::max(1, remaining / 4);
    return;
  }
  if (remaining < 500) {
    outBase = std::max(1, remaining / 5);
    outOptimal = outBase;
    outMax = std::max(1, remaining / 3);
    return;
  }

  int mtg = compute_moves_to_go(remaining, movestogo);
  double base = double(remaining) / double(mtg) + double(inc) * 0.85;
  base = std::min(base, remaining * 0.25);
  base = std::max(base, 50.0);

  int safe = std::max(0, remaining - 50);
  int hard = int(std::min(base * maxHardMult, remaining * 0.50));
  hard = std::max(hard, int(base));

  outBase = int(base);
  outOptimal = std::clamp(int(base), 10, safe);
  outMax = std::clamp(hard, outOptimal, safe);
}

void TimeManager::init(const SearchLimits &limits, Color stm,
                       int moveOverhead) {
  lims = limits;
  optimalMs = 0;
  maxMs = 0;
  baseOptimalMs = 0;
  remainingMs_ = 0;
  scale = 1.0;
  stableIters = 0;
  hasDepth1Score = false;
  scoreAtDepth1 = 0;
  pondering_.store(limits.ponder, std::memory_order_relaxed);

  if (limits.ponder || limits.infinite)
    return;

  if (limits.movetime <= 0 && limits.wtime <= 0 && limits.btime <= 0)
    return;

  if (limits.movetime > 0) {
    optimalMs = maxMs = baseOptimalMs =
        std::max(1, limits.movetime - moveOverhead);
    return;
  }

  int remaining =
      std::max(1, (stm == WHITE ? limits.wtime : limits.btime) - moveOverhead);
  int inc = (stm == WHITE) ? limits.winc : limits.binc;
  remainingMs_ = remaining;

  compute_limits(remaining, inc, limits.movestogo, MAX_HARD_MULT, optimalMs,
                 maxMs, baseOptimalMs);
}

void TimeManager::start_clock() {
  startTime = std::chrono::steady_clock::now();
  stopped.store(false, std::memory_order_relaxed);
}

void TimeManager::ponderhit(Color stm, int moveOverhead) {
  pondering_.store(false, std::memory_order_relaxed);
  startTime = std::chrono::steady_clock::now();
  lims.ponder = false;

  if (lims.infinite ||
      (lims.movetime <= 0 && lims.wtime <= 0 && lims.btime <= 0))
    return;

  if (lims.movetime > 0) {
    optimalMs = maxMs = baseOptimalMs =
        std::max(1, lims.movetime - moveOverhead);
    return;
  }

  int remaining =
      std::max(1, (stm == WHITE ? lims.wtime : lims.btime) - moveOverhead);
  int inc = (stm == WHITE) ? lims.winc : lims.binc;
  remainingMs_ = remaining;

  compute_limits(remaining, inc, lims.movestogo, MAX_HARD_MULT, optimalMs,
                 maxMs, baseOptimalMs);
}

void TimeManager::update_scale(bool bestMoveChanged, int scoreDelta,
                               uint64_t bestMoveNodes, uint64_t totalNodes,
                               int currentDepth, int currentScore) {
  if (lims.movetime > 0 || lims.infinite || baseOptimalMs <= 0)
    return;

  // 1. Best-move stability
  stableIters = bestMoveChanged ? 0 : std::min(stableIters + 1, 5);
  int idx = std::min(stableIters, 5);
  double stabilityFactor = STABILITY_SCALE[idx];

  // 2. Score instability
  double scoreFactor = 1.0;
  if (scoreDelta >= SCORE_INSTAB_THRESH_HIGH)
    scoreFactor = SCORE_INSTAB_SCALE_HIGH;
  else if (scoreDelta >= SCORE_INSTAB_THRESH_LOW)
    scoreFactor = SCORE_INSTAB_SCALE_LOW;

  // 3. Node fraction
  double nodeFactor = 1.0;
  if (totalNodes > 0) {
    double frac = double(bestMoveNodes) / double(totalNodes);
    if (frac < NODE_FRAC_THRESHOLD)
      nodeFactor = NODE_FRAC_SCALE_UP;
    else if (frac > NODE_FRAC_DN_THRESH)
      nodeFactor = NODE_FRAC_SCALE_DN;
  }

  // 4. Complexity (how much has score drifted from depth-1 anchor)
  if (!hasDepth1Score && currentDepth == 1) {
    scoreAtDepth1 = currentScore;
    hasDepth1Score = true;
  }
  double complexityFactor = 1.0;
  if (hasDepth1Score && currentDepth > 1) {
    double complexity = COMPLEXITY_LOG_FACTOR *
                        std::abs(double(scoreAtDepth1 - currentScore)) *
                        std::log(double(currentDepth));
    complexityFactor =
        std::max(COMPLEXITY_BASE +
                     std::min(complexity, COMPLEXITY_MAX) / COMPLEXITY_DIVISOR,
                 1.0);
  }

  double target =
      std::clamp(stabilityFactor * scoreFactor * nodeFactor * complexityFactor,
                 MIN_SCALE, MAX_SCALE);

  scale = std::clamp(0.70 * target + 0.30 * scale, MIN_SCALE, MAX_SCALE);

  int safeRemaining =
      remainingMs_ > 0 ? std::max(0, remainingMs_ - elapsed_ms() - 50) : maxMs;
  optimalMs = std::clamp(int(double(baseOptimalMs) * scale), 10,
                         std::min(maxMs, safeRemaining));
}

int TimeManager::elapsed_ms() const {
  return int(std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::steady_clock::now() - startTime)
                 .count());
}

bool TimeManager::time_up(uint64_t nodes) const {
  if (stopped.load(std::memory_order_relaxed))
    return true;
  if (pondering_.load(std::memory_order_relaxed))
    return false;
  if (lims.nodes > 0 && nodes >= lims.nodes)
    return true;
  if (maxMs <= 0)
    return false;
  if ((nodes & 1023) != 0)
    return false;
  return elapsed_ms() >= maxMs;
}

bool TimeManager::soft_limit_reached() const {
  if (stopped.load(std::memory_order_relaxed))
    return true;
  if (pondering_.load(std::memory_order_relaxed))
    return false;
  if (optimalMs <= 0)
    return false;
  return elapsed_ms() >= optimalMs;
}

} // namespace Catalyst
