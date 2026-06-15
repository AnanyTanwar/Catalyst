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

// Depth is stored with an offset so QS depths (negative) fit in a uint8.
// depth8 == 0 doubles as the "entry is empty" sentinel — any real search
// result stored here will have depth >= -TT_DEPTH_OFFSET, making depth8 >= 1.
constexpr int TT_DEPTH_OFFSET = 7;

// genBound8 packs three fields into one byte: [age: 5 bits | isPv: 1 bit | flag: 2 bits]
// Age increments by TT_AGE_INC each new_search() call, cycling every TT_AGE_CYCLE steps.
constexpr uint8_t TT_AGE_BITS  = 3;                         // lower 3 bits are pv + flag
constexpr uint8_t TT_AGE_INC   = 1 << TT_AGE_BITS;          // = 8
constexpr uint8_t TT_AGE_MASK  = 0xFF & ~(TT_AGE_INC - 1);  // = 0xF8
constexpr int     TT_AGE_CYCLE = 255 + TT_AGE_INC;

enum TTFlag : uint8_t {
    TT_NONE  = 0,
    TT_UPPER = 1,  // fail-low, score is an upper bound
    TT_LOWER = 2,  // fail-high, score is a lower bound
    TT_EXACT = 3,
};

// 10 bytes per entry — identical footprint to Stockfish / Stormphrax / Berserk.
// Field order matches probe() access order for sequential memory reads.
//
//   key16     : low 16 bits of the Zobrist key — used to verify a cluster hit
//   depth8    : depth + TT_DEPTH_OFFSET; value 0 means the slot is empty
//   genBound8 : [age 5b | isPv 1b | flag 2b]
//   move      : best move found at this node (MOVE_NONE if unknown)
//   score     : search score, mate-distance adjusted (see score_to_tt / score_from_tt)
//   eval      : raw static eval before search (SCORE_NONE if not available)
struct TTEntry {
    uint16_t key16;
    uint8_t  depth8;
    uint8_t  genBound8;
    Move     move;
    int16_t  score;
    int16_t  eval;

    // Returns true if this slot contains real data (depth8 == 0 means empty).
    [[nodiscard]] FORCE_INLINE bool is_occupied() const { return depth8 != 0; }

    [[nodiscard]] FORCE_INLINE int     get_depth() const { return int(depth8) - TT_DEPTH_OFFSET; }
    [[nodiscard]] FORCE_INLINE TTFlag  get_flag() const { return TTFlag(genBound8 & 0x3); }
    [[nodiscard]] FORCE_INLINE bool    is_pv() const { return (genBound8 & 0x4) != 0; }
    [[nodiscard]] FORCE_INLINE uint8_t age() const { return genBound8 & TT_AGE_MASK; }
    [[nodiscard]] FORCE_INLINE Move    get_move() const { return move; }
    [[nodiscard]] FORCE_INLINE int     get_score() const { return int(score); }
    [[nodiscard]] FORCE_INLINE int     get_eval() const { return int(eval); }

    // How many generations old is this entry relative to the current search?
    // Wraps correctly even when generation8 has cycled past 255.
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

// 3 entries + 2 bytes padding = 32 bytes per cluster.
// Two clusters fit in one 64-byte cache line → 6 entries per cache line,
// vs the old layout (4 entries per 64-byte cluster) — 50% more TT capacity.
struct alignas(32) TTCluster {
    static constexpr int ENTRIES = 3;
    TTEntry              entries[ENTRIES];
    uint8_t              _pad[2];
};

static_assert(sizeof(TTCluster) == 32, "TTCluster must be 32 bytes");

// Local snapshot returned by probe() — safe to read after a subsequent save().
struct TTData {
    Move   move;
    int    score;
    int    eval;
    int    depth;
    TTFlag flag;
    bool   is_pv;
};

// Thin write handle returned alongside TTData by probe().
// Separates the local copy (TTData) from the global table entry (TTWriter).
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

    // Main lookup. Returns:
    //   bool    — true if the position was found in the table
    //   TTData  — local copy of the stored data (valid even on a miss)
    //   TTWriter— handle to write back to this entry
    [[nodiscard]] std::tuple<bool, TTData, TTWriter> probe(Key key) const;

    // Permill of entries written during the current search (UCI "hashfull").
    [[nodiscard]] int hashfull() const;

    uint8_t generation() const { return currentGen; }

private:
    TTCluster *table       = nullptr;
    size_t     numClusters = 0;
    size_t     tableBytes  = 0;
    bool       tableMmap   = false;
    uint8_t    currentGen  = 0;

    // Lemire's fast range reduction — avoids modulo without requiring power-of-2 sizes.
    [[nodiscard]] FORCE_INLINE size_t index(Key key) const
    {
#ifdef __SIZEOF_INT128__
        return static_cast<size_t>(
            (static_cast<__uint128_t>(key) * static_cast<__uint128_t>(numClusters)) >> 64);
#else
        // Manual 64×64→128 multiply for platforms without __int128.
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

    // Score used to pick the worst entry in a cluster when all slots are occupied.
    // Older entries (larger relative_age) are penalised — matched to SF's formula.
    [[nodiscard]] FORCE_INLINE int replacement_score(const TTEntry &e) const
    {
        return int(e.depth8) - int(e.relative_age(currentGen));
    }

    void free_table();
};

extern TT tt;

// Adjust a mate/TB score before storing so it reflects distance from root, not from current node.
[[nodiscard]] FORCE_INLINE int score_to_tt(int score, int ply)
{
    if (score >= SCORE_MATE_IN_MAX_PLY)
        return score + ply;
    if (score <= -SCORE_MATE_IN_MAX_PLY)
        return score - ply;
    return score;
}

// Undo the adjustment above when reading back from the TT.
[[nodiscard]] FORCE_INLINE int score_from_tt(int score, int ply)
{
    if (score >= SCORE_MATE_IN_MAX_PLY)
        return score - ply;
    if (score <= -SCORE_MATE_IN_MAX_PLY)
        return score + ply;
    return score;
}
}  // namespace Catalyst
