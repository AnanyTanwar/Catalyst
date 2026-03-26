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
// SEE thresholds
inline constexpr int SEE_QS_THRESHOLD = -100;
inline constexpr int SEE_CAPTURE_THRESHOLD = -20;

// History table sizes / limits
inline constexpr int HISTORY_MAX = 16384;
inline constexpr int CAPT_HIST_MAX = 16384;
inline constexpr int PAWN_HISTORY_SIZE = 16384;

// Pawn history index helper
[[nodiscard]] FORCE_INLINE int pawn_history_index(Key pawnKey) {
return int(pawnKey & (PAWN_HISTORY_SIZE - 1));
}

// Butterfly history: [color][from][to]
using ButterflyHistory = int[COLOR_NB][SQUARE_NB][SQUARE_NB];

// Capture history: [color][attacker_type][to_sq][victim_type]
using CaptureHistory = int[COLOR_NB][PIECE_TYPE_NB][SQUARE_NB][PIECE_TYPE_NB];

// Continuation history: [piece_type][to_sq] (1-ply, 2-ply, 4-ply follow-up)
using ContinuationHistory = int[PIECE_TYPE_NB][SQUARE_NB];

// Pawn history: [pawn_key_bucket][piece_type][to_sq]
using PawnHistory = int[PAWN_HISTORY_SIZE][PIECE_TYPE_NB][SQUARE_NB];

// Move ordering stages
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

struct MoveBuffer {
Move moves[MAX_MOVES];
int scores[MAX_MOVES];
};

// MovePicker for move ordering in search
class MovePicker {
public:
// Normal search constructor
MovePicker(const Board &b, Move ttMove, int ply, Move killer1, Move killer2,
            Move counter, const ButterflyHistory &hist,
            const CaptureHistory &captHist, const PawnHistory &pawnHist,
            const ContinuationHistory *contHist1,
            const ContinuationHistory *contHist2,
            const ContinuationHistory *contHist4, MoveBuffer &buf);

// Qsearch constructor
MovePicker(const Board &b, Move ttMove, int seeThreshold, bool qsearchOnly,
            const CaptureHistory &captHist, MoveBuffer &buf);

Move next_move();
PickStage current_stage() const { return stage; }

public:
const Board &board;
PickStage stage;
Move ttMove;
int ply;
Color us;
Move killer1, killer2, counter;

const ButterflyHistory *history;
const CaptureHistory *captureHistory;
const PawnHistory *pawnHistory;
const ContinuationHistory *contHist1;
const ContinuationHistory *contHist2;
const ContinuationHistory *contHist4;

Move *moves;
int *scores;

int cur;
int goodCaptEnd;
int captEnd;
int quietEnd;
int badCaptCur;
int seeThreshold;
bool qsearchMode;

void generate_and_score_captures();
void generate_and_score_quiets();
int score_capture(Move m) const;
void select_best(int begin, int end);
bool see_ge(Move m, int threshold) const;
};

// Gravity/clamp history update to prevent overflow
inline void update_history(int &entry, int bonus) {
entry += bonus - entry * std::abs(bonus) / HISTORY_MAX;
}
} // namespace Catalyst