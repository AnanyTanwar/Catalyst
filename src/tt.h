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
#include <tuple>

namespace Catalyst {

// Depth stored with offset so QS depths (negative) are representable.
constexpr int TT_DEPTH_OFFSET = 7;

// agePvBound layout: [age: 5 bits | isPv: 1 bit | flag: 2 bits]
constexpr uint8_t TT_AGE_INC    = 8;
constexpr uint8_t TT_AGE_MASK   = 0xF8;
constexpr int     TT_AGE_CYCLE  = 256;
constexpr int     TT_AGE_WEIGHT = TT_AGE_CYCLE / TT_AGE_INC;  // 32

enum TTFlag : uint8_t {
    TT_NONE  = 0,
    TT_EXACT = 1,
    TT_LOWER = 2,
    TT_UPPER = 3
};

// 16 bytes × 4 entries = one 64-byte cache line per cluster.
// Occupancy signalled by hashKey != 0.
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
    [[nodiscard]] FORCE_INLINE bool   is_occupied() const { return hashKey != 0; }
};

static_assert(sizeof(TTEntry) == 16, "TTEntry must be 16 bytes");

struct alignas(64) TTCluster {
    TTEntry entries[4];
};

static_assert(sizeof(TTCluster) == 64, "TTCluster must be 64 bytes");

// Snapshot returned by probe() — safe to read after store().
struct TTData {
    Move   move;
    int    score;
    int    eval;
    int    depth;
    int    rule50;
    TTFlag flag;
    bool   is_pv;
};

// Write handle returned alongside TTData by probe().
struct TTWriter {
    void save(Key key,
        int       score,
        int       depth,
        TTFlag    flag,
        Move      move,
        int       eval,
        int       rule50,
        bool      isPv,
        uint8_t   currentGen);

private:
    friend class TT;
    TTEntry *entry;
    explicit TTWriter(TTEntry *e)
        : entry(e)
    {
    }
};

class TT {
public:
    TT();
    ~TT();

    void resize(size_t mb);
    void clear();
    void new_search();
    void prefetch(Key key) const;

    [[nodiscard]] std::tuple<bool, TTData, TTWriter> probe(Key key) const;
    [[nodiscard]] int                                hashfull() const;

    uint8_t generation() const { return currentGen; }

private:
    TTCluster *table       = nullptr;
    size_t     numClusters = 0;
    size_t     tableBytes  = 0;  // actual allocated size (may be rounded up for huge pages)
    bool       tableMmap   = false;
    uint8_t    currentGen  = 0;

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

    [[nodiscard]] FORCE_INLINE int replacement_score(const TTEntry &e) const
    {
        const int age = (TT_AGE_CYCLE + currentGen - (e.agePvBound & TT_AGE_MASK)) & TT_AGE_MASK;
        return int(e.depth) - age * TT_AGE_WEIGHT / TT_AGE_INC;
    }

    void free_table();
};

extern TT tt;

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
