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

namespace Catalyst
{

  enum TTFlag : uint8_t
  {
    TT_NONE = 0,
    TT_EXACT = 1,
    TT_LOWER = 2,
    TT_UPPER = 3
  };

  // 16 bytes per entry, 4 entries per cluster = 64-byte cache line.
  // Key verification uses 48 bits (key16 + key16b) to minimize collisions.
  struct TTEntry
  {
    uint16_t key16;     // bits 48-63 of Zobrist key
    uint16_t key16b;    // bits 32-47 of Zobrist key
    Move move;          // best move
    int16_t score;      // search score (mate-adjusted)
    int16_t eval;       // raw static eval
    int8_t depth;       // search depth
    TTFlag flag;        // TT_EXACT / TT_LOWER / TT_UPPER
    uint8_t generation; // search generation (0 = empty)
    uint8_t _pad[3];

    [[nodiscard]] FORCE_INLINE Move get_move() const { return move; }
    [[nodiscard]] FORCE_INLINE int get_score() const { return int(score); }
    [[nodiscard]] FORCE_INLINE int get_depth() const { return int(depth); }
    [[nodiscard]] FORCE_INLINE TTFlag get_flag() const { return flag; }
    [[nodiscard]] FORCE_INLINE int get_eval() const { return int(eval); }
  };

  struct alignas(64) TTCluster
  {
    TTEntry entries[4];
  };

  static_assert(sizeof(TTEntry) == 16, "TTEntry must be 16 bytes");
  static_assert(sizeof(TTCluster) == 64, "TTCluster must be 64 bytes");

  class TT
  {
  public:
    TT();
    ~TT();

    void resize(size_t mb);
    void clear();
    void new_search();
    void store(Key key, int score, int depth, TTFlag flag, Move move, int eval = 0);
    void prefetch(Key key) const;

    [[nodiscard]] TTEntry *probe(Key key, bool &found);
    [[nodiscard]] int hashfull() const;

  private:
    TTCluster *table = nullptr;
    size_t numClusters = 0;
    size_t clusterMask = 0;
    uint8_t currentGen = 1;

    [[nodiscard]] FORCE_INLINE size_t index(Key key) const { return size_t(key) & clusterMask; }
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

} // namespace Catalyst
