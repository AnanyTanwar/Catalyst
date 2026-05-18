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

#include "types.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Catalyst {

// Depth is stored as uint8_t with an offset to allow negative depths to be stored
constexpr int TT_DEPTH_OFFSET = 7;

// Age increments in steps of 8 so the lower 3 bits are free for PV and flag bits
// agePvBound layout: [age: 5 bits][isPv: 1 bit][flag: 2 bits]
constexpr uint8_t TT_AGE_INC  = 8;
constexpr uint8_t TT_AGE_MASK = 0xF8;

enum TTFlag : uint8_t {
    TT_NONE  = 0,
    TT_EXACT = 1,
    TT_LOWER = 2,
    TT_UPPER = 3
};

// 16 bytes per entry, 4 entries per cluster = one 64-byte cache line per cluster
// hashKey is only 32 bits (upper 32 bits of the Zobrist key) to save space
struct TTEntry {
    uint32_t hashKey;
    uint16_t move;
    int16_t  score;
    int16_t  eval;
    int16_t  rule50;
    uint8_t  depth;
    uint8_t  agePvBound;
    uint8_t  _pad[2];

    [[nodiscard]] FORCE_INLINE Move   get_move() const { return Move(move); }
    [[nodiscard]] FORCE_INLINE int    get_score() const { return int(score); }
    [[nodiscard]] FORCE_INLINE int    get_depth() const { return int(depth) - TT_DEPTH_OFFSET; }
    [[nodiscard]] FORCE_INLINE TTFlag get_flag() const { return TTFlag(agePvBound & 0x3); }
    [[nodiscard]] FORCE_INLINE int    get_eval() const { return int(eval); }
    [[nodiscard]] FORCE_INLINE bool   is_pv() const { return (agePvBound & 0x4) != 0; }
    [[nodiscard]] FORCE_INLINE int    get_rule50() const { return int(rule50); }
    [[nodiscard]] FORCE_INLINE bool   is_empty() const { return depth == 0; }
};

static_assert(sizeof(TTEntry) == 16, "TTEntry must be 16 bytes");

struct alignas(64) TTCluster {
    TTEntry entries[4];
};

static_assert(sizeof(TTCluster) == 64, "TTCluster must be 64 bytes");

class TT {
public:
    TT();
    ~TT();

    void resize(size_t mb);
    void clear();
    void new_search();
    void prefetch(Key key) const;

    void store(Key key,
        int        score,
        int        depth,
        TTFlag     flag,
        Move       move,
        int        eval,
        int        rule50,
        bool       isPv = false);

    [[nodiscard]] TTEntry *probe(Key key, bool &found);
    [[nodiscard]] int      hashfull() const;

private:
    TTCluster *table       = nullptr;
    size_t     numClusters = 0;
    uint8_t    currentGen  = 0;

    // Fast modulo-free cluster index using 128-bit multiply trick (avoids division)
    [[nodiscard]] FORCE_INLINE size_t index(Key key) const
    {
#ifdef __SIZEOF_INT128__
        return static_cast<size_t>(
            (static_cast<__uint128_t>(key) * static_cast<__uint128_t>(numClusters)) >> 64);
#else
        uint64_t xlo = static_cast<uint32_t>(key);
        uint64_t xhi = key >> 32;
        uint64_t nlo = static_cast<uint32_t>(numClusters);
        uint64_t nhi = numClusters >> 32;
        uint64_t c1  = (xlo * nlo) >> 32;
        uint64_t c2  = (xhi * nlo) + c1;
        uint64_t c3  = (xlo * nhi) + static_cast<uint32_t>(c2);
        return static_cast<size_t>(xhi * nhi + (c2 >> 32) + (c3 >> 32));
#endif
    }

    // Replacement policy: prefer replacing old entries (high age) and shallow entries (low depth)
    // Age difference is weighted x2 relative to depth to strongly prefer fresh entries
    [[nodiscard]] FORCE_INLINE int replacement_score(const TTEntry &e) const
    {
        uint8_t age = (currentGen - (e.agePvBound & TT_AGE_MASK)) & TT_AGE_MASK;
        return int(e.depth) - int(age) * 2;
    }
};

extern TT tt;

// Mate scores are stored relative to root but must be adjusted to/from ply-relative form
// so the same mate distance is preserved regardless of which ply the entry was stored at
[[nodiscard]] FORCE_INLINE int score_to_tt(int score, int ply)
{
    if (score >= SCORE_MATE_IN_MAX_PLY)
        return score + ply;
    if (score <= -SCORE_MATE_IN_MAX_PLY)
        return score - ply;
    return score;
}

[[nodiscard]] FORCE_INLINE int score_from_tt(int score, int ply)
{
    if (score >= SCORE_MATE_IN_MAX_PLY)
        return score - ply;
    if (score <= -SCORE_MATE_IN_MAX_PLY)
        return score + ply;
    return score;
}

}