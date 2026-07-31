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

// Depth is stored with an offset so QS depths (negative) fit in a uint8
constexpr int TT_DEPTH_OFFSET = 7;

// genBound8 packs: [age: 5 bits | isPv: 1 bit | flag: 2 bits]
constexpr uint8_t TT_AGE_BITS  = 3;
constexpr uint8_t TT_AGE_INC   = 1 << TT_AGE_BITS;
constexpr uint8_t TT_AGE_MASK  = 0xFF & ~(TT_AGE_INC - 1);
constexpr int     TT_AGE_CYCLE = 255 + TT_AGE_INC;

enum TTFlag : uint8_t {
    TT_NONE  = 0,
    TT_UPPER = 1,
    TT_LOWER = 2,
    TT_EXACT = 3,
};

// 10 bytes per entry
struct TTEntry {
    uint16_t key16;
    uint8_t  depth8;
    uint8_t  genBound8;
    Move     move;
    int16_t  score;
    int16_t  eval;

    [[nodiscard]] FORCE_INLINE bool is_occupied() const { return depth8 != 0; }

    [[nodiscard]] FORCE_INLINE int     get_depth() const { return int(depth8) - TT_DEPTH_OFFSET; }
    [[nodiscard]] FORCE_INLINE TTFlag  get_flag() const { return TTFlag(genBound8 & 0x3); }
    [[nodiscard]] FORCE_INLINE bool    is_pv() const { return (genBound8 & 0x4) != 0; }
    [[nodiscard]] FORCE_INLINE uint8_t age() const { return genBound8 & TT_AGE_MASK; }
    [[nodiscard]] FORCE_INLINE Move    get_move() const { return move; }
    [[nodiscard]] FORCE_INLINE int     get_score() const { return int(score); }
    [[nodiscard]] FORCE_INLINE int     get_eval() const { return int(eval); }

    [[nodiscard]] FORCE_INLINE uint8_t relative_age(uint8_t generation8) const
    {
        return (TT_AGE_CYCLE + generation8 - genBound8) & TT_AGE_MASK;
    }

    void save(Key newKey,
        int       newScore,
        int       newDepth,
        TTFlag    newFlag,
        Move      newMove,
        int       newEval,
        bool      isPv,
        uint8_t   generation8);
};

static_assert(sizeof(TTEntry) == 10, "TTEntry must be 10 bytes");

// 3 entries + 2 bytes padding = 32 bytes per cluster
struct alignas(32) TTCluster {
    static constexpr int ENTRIES = 3;
    TTEntry              entries[ENTRIES];
    uint8_t              _pad[2];
};

static_assert(sizeof(TTCluster) == 32, "TTCluster must be 32 bytes");

struct TTData {
    Move   move;
    int    score;
    int    eval;
    int    depth;
    TTFlag flag;
    bool   is_pv;
};

struct TTWriter {
    void save(Key key,
        int       score,
        int       depth,
        TTFlag    flag,
        Move      move,
        int       eval,
        bool      isPv,
        uint8_t   generation8);

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

    [[nodiscard]] int hashfull() const;

    uint8_t generation() const { return currentGen; }

private:
    TTCluster *table       = nullptr;
    size_t     numClusters = 0;
    size_t     tableBytes  = 0;
    bool       tableMmap   = false;
    uint8_t    currentGen  = 0;

    // Lemire's fast range reduction - avoids modulo
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
        return int(e.depth8) - int(e.relative_age(currentGen));
    }

    void free_table();
};

extern TT tt;

// Adjust scores for storage so they reflect distance from root
[[nodiscard]] FORCE_INLINE int score_to_tt(int score, int ply)
{
    if (score >= SCORE_MATE_IN_MAX_PLY)
        return score + ply;
    if (score <= -SCORE_MATE_IN_MAX_PLY)
        return score - ply;
    return score;
}

// Undo adjustment when reading back from TT
[[nodiscard]] FORCE_INLINE int score_from_tt(int score, int ply)
{
    if (score >= SCORE_MATE_IN_MAX_PLY)
        return score - ply;
    if (score <= -SCORE_MATE_IN_MAX_PLY)
        return score + ply;
    return score;
}
}  // namespace Catalyst
