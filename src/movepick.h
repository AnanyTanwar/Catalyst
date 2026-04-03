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

// ---------------------------------------------------------------------------
// SEE thresholds
// ---------------------------------------------------------------------------
inline constexpr int SEE_QS_THRESHOLD      = -100;
inline constexpr int SEE_CAPTURE_THRESHOLD = -20;

// ---------------------------------------------------------------------------
// History table sizes / limits
// ---------------------------------------------------------------------------
inline constexpr int HISTORY_MAX       = 16384;
inline constexpr int CAPT_HIST_MAX     = 16384;
inline constexpr int PAWN_HISTORY_SIZE = 16384;

// ---------------------------------------------------------------------------
// Tunable move ordering constants
// Keep these as named constants so they are easy to find for SPSA tuning.
// ---------------------------------------------------------------------------

// Good-capture SEE divisor (Stockfish pattern: -score / N).
// Higher N = more captures land in the good bucket (lenient bar).
// Lower N = stricter filtering. Empirical range: 14-22. SF uses 18.
inline constexpr int GOOD_CAP_SEE_DIVISOR = 18;

// Capture history weight divisor inside score_capture.
// Higher divisor = captHist is a smaller tiebreaker within victim tiers.
// Berserk uses 16, Stormphrax uses 8. Range to try: 8-20.
inline constexpr int CAPT_HIST_DIVISOR = 16;

// Threatened-square adjustment in quiet scoring.
// Escape bonus: piece is on a square attacked by a lesser enemy piece.
// Step-in penalty: piece is moving to such a square.
// Stockfish uses ~19000/20000, Berserk uses 16384. Range: 10000-20000.
inline constexpr int THREAT_ESCAPE_BONUS   = 14000;
inline constexpr int THREAT_STEPIN_PENALTY = 14000;

// Continuation history weights for quiet scoring.
// ch1 (1-ply back): multiplier 2 — strongest signal
// ch2 (2-ply back): multiplier 2 — equally important
// ch4 (4-ply back): multiplier 1 — useful but noisier
// Note: old values were ch2=1, ch4=1/2 — both were under-weighted.
inline constexpr int CONT_HIST1_WEIGHT = 2;
inline constexpr int CONT_HIST2_WEIGHT = 2;
inline constexpr int CONT_HIST4_WEIGHT = 1;

// Quiet history pruning threshold applied inside STAGE_QUIETS.
// Moves with score < quietThreshold_ are skipped without being searched.
// This threshold is set by search per node: typically -(HIST_PRUNE_MULT * depth).
// A sentinel of INT_MIN means "no pruning" (default for qsearch/non-search use).
inline constexpr int QUIET_PRUNE_DISABLED = -32000000;

// ---------------------------------------------------------------------------
// Pawn history index helper
// ---------------------------------------------------------------------------
[[nodiscard]] FORCE_INLINE int pawn_history_index(Key pawnKey) {
    return int(pawnKey & (PAWN_HISTORY_SIZE - 1));
}

// ---------------------------------------------------------------------------
// History table type aliases
// ---------------------------------------------------------------------------

// Butterfly history: [color][from][to]
using ButterflyHistory = int[COLOR_NB][SQUARE_NB][SQUARE_NB];

// Capture history: [color][attacker_type][to_sq][victim_type]
using CaptureHistory = int[COLOR_NB][PIECE_TYPE_NB][SQUARE_NB][PIECE_TYPE_NB];

// Continuation history: [piece_type][to_sq]
using ContinuationHistory = int[PIECE_TYPE_NB][SQUARE_NB];

// Pawn history: [pawn_key_bucket][piece_type][to_sq]
using PawnHistory = int[PAWN_HISTORY_SIZE][PIECE_TYPE_NB][SQUARE_NB];

// ---------------------------------------------------------------------------
// Move ordering stages
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// MoveBuffer — per-ply storage owned by search, passed into MovePicker
// ---------------------------------------------------------------------------
struct MoveBuffer {
    Move moves[MAX_MOVES];
    int  scores[MAX_MOVES];
};

// ---------------------------------------------------------------------------
// MovePicker
// ---------------------------------------------------------------------------
class MovePicker {
public:
    // Normal search constructor
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
        const ContinuationHistory *contHist4,
        MoveBuffer                &buf);

    // Qsearch / probcut constructor
    MovePicker(const Board   &b,
        Move                  ttMove,
        int                   seeThreshold,
        bool                  qsearchOnly,
        const CaptureHistory &captHist,
        MoveBuffer           &buf);

    // Called by search to enable in-picker quiet pruning.
    // Set to -(HIST_PRUNE_MULT * depth) or similar before iterating.
    // This avoids the wasted select_best() call on moves that will be pruned.
    void set_quiet_threshold(int threshold) { quietThreshold_ = threshold; }

    Move      next_move();
    PickStage current_stage() const { return stage; }

public:
    // ── Board + context ───────────────────────────────────────────────────
    const Board &board;
    PickStage    stage;
    Move         ttMove;
    int          ply;
    Color        us;
    Move         killer1, killer2, counter;

    // ── History table pointers ────────────────────────────────────────────
    const ButterflyHistory    *history;
    const CaptureHistory      *captureHistory;
    const PawnHistory         *pawnHistory;
    const ContinuationHistory *contHist1;
    const ContinuationHistory *contHist2;
    const ContinuationHistory *contHist4;

    // ── Move + score arrays (external storage from MoveBuffer) ────────────
    Move *moves;
    int  *scores;

    // ── Iteration state ───────────────────────────────────────────────────
    int  cur;
    int  goodCaptEnd;   // [0,          goodCaptEnd) = SEE-passing captures
    int  captEnd;       // [goodCaptEnd, captEnd)    = SEE-failing captures
    int  quietEnd;      // [captEnd,     quietEnd)   = quiet moves
    int  badCaptCur;    // cursor into bad-cap bucket during STAGE_BAD_CAPTURES
    int  seeThreshold;  // used by qsearch picker
    bool qsearchMode;

private:
    // ── Internal threshold for in-picker quiet pruning ────────────────────
    int quietThreshold_ = QUIET_PRUNE_DISABLED;

    // ── Internal helpers ──────────────────────────────────────────────────
    void generate_and_score_captures();
    void generate_and_score_quiets();
    int  score_capture(Move m) const;
    void select_best(int begin, int end);
    bool see_ge(Move m, int threshold) const;

    friend class Search;  // allow search to call see_ge via the public wrapper
};

// ---------------------------------------------------------------------------
// Gravity/clamped history update — prevents overflow
// ---------------------------------------------------------------------------
inline void update_history(int &entry, int bonus) {
    entry += bonus - entry * std::abs(bonus) / HISTORY_MAX;
}

}  // namespace Catalyst