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

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "types.h"

namespace Catalyst {

constexpr int     TT_DEPTH_OFFSET = 2;
constexpr uint8_t TT_AGE_INC      = 8;
constexpr uint8_t TT_AGE_MASK     = 0xF8;

enum TTFlag : uint8_t { TT_NONE = 0, TT_EXACT = 1, TT_LOWER = 2, TT_UPPER = 3 };

struct TTEntry {
    uint32_t hashKey;
    uint8_t  depth;
    uint8_t  agePvBound;
    uint32_t evalAndMove;
    int16_t  score;
    uint16_t padding;

    [[nodiscard]] FORCE_INLINE Move   get_move() const { return Move(evalAndMove & 0xFFFFF); }
    [[nodiscard]] FORCE_INLINE int    get_score() const { return int(score); }
    [[nodiscard]] FORCE_INLINE int    get_depth() const { return int(depth) - TT_DEPTH_OFFSET; }
    [[nodiscard]] FORCE_INLINE TTFlag get_flag() const { return TTFlag(agePvBound & 0x3); }
    [[nodiscard]] FORCE_INLINE int    get_eval() const {
        return int((evalAndMove >> 20) & 0xFFF) - 2048;
    }
    [[nodiscard]] FORCE_INLINE bool is_pv() const { return (agePvBound & 0x4) != 0; }
};

struct alignas(64) TTCluster {
    TTEntry entries[4];
};

static_assert(sizeof(TTEntry) == 16, "");
static_assert(sizeof(TTCluster) == 64, "");

class TT {
public:
    TT();
    ~TT();

    void resize(size_t mb);
    void clear();
    void new_search();
    void store(Key key, int score, int depth, TTFlag flag, Move move, int eval, bool isPv = false);
    void prefetch(Key key) const;

    [[nodiscard]] TTEntry *probe(Key key, bool &found);
    [[nodiscard]] int      hashfull() const;

    static constexpr int     DEPTH_OFFSET = TT_DEPTH_OFFSET;
    static constexpr uint8_t AGE_INC      = TT_AGE_INC;
    static constexpr uint8_t AGE_MASK     = TT_AGE_MASK;

private:
    TTCluster *table       = nullptr;
    size_t     numClusters = 0;
    size_t     clusterMask = 0;
    uint8_t    currentGen  = 0;

    [[nodiscard]] FORCE_INLINE size_t index(Key key) const { return (size_t)key & clusterMask; }
};

extern TT tt;

[[nodiscard]] FORCE_INLINE int score_to_tt(int score, int ply) {
    if (score >= SCORE_MATE_IN_MAX_PLY)
        return score + ply;
    if (score <= -SCORE_MATE_IN_MAX_PLY)
        return score - ply;
    return score;
}

[[nodiscard]] FORCE_INLINE int score_from_tt(int score, int ply) {
    if (score >= SCORE_MATE_IN_MAX_PLY)
        return score - ply;
    if (score <= -SCORE_MATE_IN_MAX_PLY)
        return score + ply;
    return score;
}

}