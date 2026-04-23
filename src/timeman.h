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
#include <cstdint>
#include <limits>

#include "types.h"

namespace Catalyst {

struct SearchLimits {
    int depth     = 64;
    int movetime  = 0;
    int wtime     = 0;
    int btime     = 0;
    int winc      = 0;
    int binc      = 0;
    int movestogo = 0;
    int mate      = 0;

    uint64_t nodes     = 0;
    uint64_t softNodes = 0;
    uint64_t hardNodes = 0;

    bool infinite = false;
    bool ponder   = false;
};

class TimeManager {
public:
    TimeManager() = default;

    void init(const SearchLimits &limits, Color stm, int moveOverhead);
    void start_clock();
    void update_scale(bool bestMoveChanged,
        int                scoreDelta,
        uint64_t           bestMoveNodes,
        uint64_t           totalNodes,
        int                currentDepth,
        int                currentScore);
    void stop() { stopped.store(true, std::memory_order_relaxed); }
    void ponderhit(Color stm, int moveOverhead);

    [[nodiscard]] bool is_pondering() const { return pondering_.load(std::memory_order_relaxed); }
    [[nodiscard]] int  elapsed_ms() const;
    [[nodiscard]] bool time_up(uint64_t nodes) const;
    [[nodiscard]] bool soft_limit_reached(uint64_t nodes = 0) const;
    [[nodiscard]] bool is_stopped() const { return stopped.load(std::memory_order_relaxed); }
    [[nodiscard]] const SearchLimits &limits() const { return lims; }

    int optimalMs = 0;
    int maxMs     = 0;

private:
    SearchLimits                          lims;
    std::chrono::steady_clock::time_point startTime;
    std::atomic<bool>                     stopped { false };
    std::atomic<bool>                     pondering_ { false };

    double scale          = 1.0;
    int    stableIters    = 0;
    int    baseOptimalMs  = 0;
    int    scoreAtDepth1  = 0;
    bool   hasDepth1Score = false;
    int    remainingMs_   = 0;

    uint64_t effectiveSoftNodes_ = 0;
    uint64_t effectiveHardNodes_ = 0;

    static constexpr double MAX_HARD_MULT = 5.0;
    static constexpr double MIN_SCALE     = 0.5;
    static constexpr double MAX_SCALE     = 2.5;

    static constexpr double STABILITY_SCALE[6] = {
        2.50,
        1.20,
        1.00,
        0.90,
        0.80,
        0.70,
    };

    static constexpr int    SCORE_INSTAB_THRESH_HIGH = 30;
    static constexpr int    SCORE_INSTAB_THRESH_LOW  = 15;
    static constexpr double SCORE_INSTAB_SCALE_HIGH  = 1.25;
    static constexpr double SCORE_INSTAB_SCALE_LOW   = 1.12;

    static constexpr double NODE_FRAC_THRESHOLD = 0.50;
    static constexpr double NODE_FRAC_SCALE_UP  = 1.20;
    static constexpr double NODE_FRAC_SCALE_DN  = 0.85;
    static constexpr double NODE_FRAC_DN_THRESH = 0.80;

    static constexpr double COMPLEXITY_BASE       = 0.77;
    static constexpr double COMPLEXITY_DIVISOR    = 386.0;
    static constexpr double COMPLEXITY_MAX        = 200.0;
    static constexpr double COMPLEXITY_LOG_FACTOR = 0.6;
};

}  // namespace Catalyst