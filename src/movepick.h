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

#include "board.h"
#include "types.h"

namespace Catalyst {

// SEE threshold for qsearch captures — skip captures losing more than this
inline constexpr int SEE_QS_THRESHOLD = -100;
// SEE threshold for normal search captures — more lenient than qsearch
inline constexpr int SEE_CAPTURE_THRESHOLD = -20;

// History values are gravity-clamped to this limit to prevent overflow
inline constexpr int HISTORY_MAX       = 16384;
inline constexpr int CAPT_HIST_MAX     = 16384;
inline constexpr int PAWN_HISTORY_SIZE = 16384;

// Quiet pruning sentinel (disabled)
inline constexpr int QUIET_PRUNE_DISABLED = -32000000;

// Pawn history index helper
[[nodiscard]] FORCE_INLINE int pawn_history_index(Key pawnKey)
{
    return int(pawnKey & (PAWN_HISTORY_SIZE - 1));
}

// Threat index helper
[[nodiscard]] FORCE_INLINE int threat_index(Square from, Square to, Bitboard threats)
{
    return 2 * bool(threats & square_bb(from)) + bool(threats & square_bb(to));
}

// [color][from][to][threat_index] — main quiet move history indexed by threat context
using ButterflyHistory = int[COLOR_NB][SQUARE_NB][SQUARE_NB][4];

// [color][piece_type][to][threat_index] — used for continuation history tables
using PieceToHistory = int[COLOR_NB][PIECE_TYPE_NB][SQUARE_NB][4];

// [color][attacker][to][victim][threat_index] — history for capture moves
using CaptureHistory = int[COLOR_NB][PIECE_TYPE_NB][SQUARE_NB][PIECE_TYPE_NB][4];

// [piece_type][to] — single-ply continuation history entry
using ContinuationHistory = int[PIECE_TYPE_NB][SQUARE_NB];

// [pawn_key % size][piece_type][to] — history conditioned on pawn structure
using PawnHistory = int[PAWN_HISTORY_SIZE][PIECE_TYPE_NB][SQUARE_NB];

// Staged move ordering: moves are generated and returned in priority
// order. Good captures first, then killers/counter, then quiets, then bad
// captures. MovePicker advances through these stages internally as
// next_move() is called repeatedly - search never sees or manages the
// stage transitions directly, just keeps calling next_move() until it
// returns MOVE_NONE.
enum PickStage {
    STAGE_TT,
    STAGE_INIT_CAPTURES,
    STAGE_GOOD_CAPTURES,
    STAGE_KILLERS,
    STAGE_KILLER2,
    STAGE_COUNTERS,
    STAGE_INIT_QUIETS,
    STAGE_QUIETS,
    STAGE_BAD_CAPTURES,
    STAGE_DONE
};

// Caller-owned backing storage for one MovePicker instance's move list and
// per-move ordering scores - passed in by reference rather than owned by
// MovePicker itself so search can allocate it once per ply on the stack
// and reuse it, avoiding a heap allocation per node.
struct MoveBuffer {
    Move moves[MAX_MOVES];
    int  scores[MAX_MOVES];
};

// Generates and returns moves in priority order for a single search node,
// one call to next_move() at a time. Two constructors cover the two
// contexts search needs move ordering in: full staged ordering for normal
// alpha-beta nodes (TT/killers/counter/history all in play), and a
// simpler capture-focused mode for quiescence search and probcut, where
// only captures (optionally filtered by a SEE threshold) are relevant.
class MovePicker {
public:
    // Normal search constructor - full staged ordering. `hist`/`captHist`/
    // `pawnHist`/`contHist1-4` are all owned by the calling thread's search
    // state and passed by const reference; `killer1`/`killer2`/`counter` are
    // move-ordering hints specific to this node (killer moves are per-ply,
    // counter is indexed by the opponent's last move - see thread.h/search.cpp).
    MovePicker(const Board        &b,
        Move                       ttMove,
        int                        ply,
        Move                       killer1,
        Move                       killer2,
        Move                       counter,
        const ButterflyHistory    &hist,
        const CaptureHistory      &captHist,
        const PawnHistory         &pawnHist,
        const ContinuationHistory *contHist1,
        const ContinuationHistory *contHist2,
        const ContinuationHistory *contHist3,
        const ContinuationHistory *contHist4,
        Bitboard                   threats,
        MoveBuffer                &buf);

    // Qsearch / probcut constructor - captures only (optionally filtered to
    // captures passing a SEE threshold), no quiet move generation at all.
    // `qsearchOnly` further restricts to a stricter subset when set.
    MovePicker(const Board   &b,
        Move                  ttMove,
        int                   seeThreshold,
        bool                  qsearchOnly,
        const CaptureHistory &captHist,
        MoveBuffer           &buf);

    // Set quiet pruning threshold (called by search before move loop)
    void set_quiet_threshold(int threshold) { quietThreshold_ = threshold; }

    // Returns the next move in priority order, or MOVE_NONE once every move
    // for this node has been returned. Internally generates moves lazily
    // stage-by-stage rather than all up front (see generate_and_score_captures/
    // generate_and_score_quiets below) so a beta cutoff early in the good-
    // captures stage never pays the cost of generating quiet moves at all.
    Move      next_move();
    PickStage current_stage() const { return stage; }

public:
    const Board &board;
    PickStage    stage;
    Move         ttMove;
    int          ply;
    Color        us;
    Move         killer1, killer2, counter;

    const ButterflyHistory    *history;
    const CaptureHistory      *captureHistory;
    const PawnHistory         *pawnHistory;
    const ContinuationHistory *contHist1;
    const ContinuationHistory *contHist2;
    const ContinuationHistory *contHist3;
    const ContinuationHistory *contHist4;

    Move *moves;
    int  *scores;

    int      cur;
    int      goodCaptEnd;
    int      captEnd;
    int      quietEnd;
    int      badCaptCur;
    int      seeThreshold;
    bool     qsearchMode;
    Bitboard threats         = 0;
    int      quietThreshold_ = QUIET_PRUNE_DISABLED;

    void generate_and_score_captures();
    void generate_and_score_quiets();
    int  score_capture(Move m) const;
    void select_best(int begin, int end);
    bool see_ge(Move m, int threshold) const;
};

// Gravity/clamp history update to prevent overflow
inline void update_history(int &entry, int bonus)
{
    entry += bonus - entry * std::abs(bonus) / HISTORY_MAX;
}

}  // namespace Catalyst