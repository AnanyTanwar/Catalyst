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
#include "movepick.h"
#include "nnue.h"
#include "timeman.h"
#include "tt.h"
#include "types.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace Catalyst {

// LMR parameters
inline constexpr double LMR_QUIET_BASE = 0.40;
inline constexpr double LMR_QUIET_SCALE = 0.38;
inline constexpr double LMR_NOISY_BASE = 0.10;
inline constexpr double LMR_NOISY_SCALE = 0.27;
inline constexpr int LMR_FRAC = 1024;
inline constexpr int LMR_ROUNDING_CUTOFF = LMR_FRAC / 2;
inline constexpr int LMR_HIST_QUIET_DIV = 8192;
inline constexpr int LMR_HIST_NOISY_DIV = 12288;

// Aspiration windows
inline constexpr double ASP_BETA_LERP = 0.25;
inline constexpr int ASP_INIT_DELTA = 16;
inline constexpr int ASP_MAX_DELTA = 500;

// Reverse futility pruning
inline constexpr int RFP_MARGIN_MULT = 77;
inline constexpr int RFP_HIST_DIV = 512;
inline constexpr int RFP_MAX_DEPTH = 16;

// Null move pruning
inline constexpr int NMP_BASE_R = 4;
inline constexpr int NMP_BETA_BASE = 150;
inline constexpr int NMP_BETA_MULT = 15;
inline constexpr int NMP_EVAL_DIV = 200;
inline constexpr int NMP_VERIF_DEPTH = 16;

// ProbCut
inline constexpr int PROBCUT_MARGIN = 229;
inline constexpr int PROBCUT_DEPTH = 5;
inline constexpr int PROBCUT_MAX = 5;

// Singular extensions
inline constexpr int SE_DEPTH = 7;
inline constexpr int SE_DOUBLE_MARGIN = 13;
inline constexpr int SE_TRIPLE_MARGIN = 86;

// Futility pruning
inline constexpr int FUTILITY_BASE = 42;
inline constexpr int FUTILITY_MULT = 150;
inline constexpr int FUTILITY_MAX_D = 13;

// SEE pruning
inline constexpr int SEE_QUIET_THRESH = -64;
inline constexpr int SEE_NOISY_THRESH = -20;

// Late move pruning
inline constexpr int LMP_BASE = 3;
inline constexpr int LMP_MAX_DEPTH = 8;

// History pruning
inline constexpr int HIST_PRUNE_MULT = 4096;
inline constexpr int HIST_PRUNE_DEPTH = 4;

// Quiescence search
inline constexpr int DELTA_MARGIN = 200;
inline constexpr double QS_CUTOFF_LERP = 0.50;
inline constexpr double QS_FAILHIGH_LERP = 0.50;

// Fifty-move eval scaling
inline constexpr int FIFTY_SCALE_NUM = 220;

// ZWS re-search thresholds
inline constexpr int ZWS_DEEPER_MARGIN = 50;
inline constexpr int ZWS_SHALLOWER_MARGIN = 9;

// Alpha raise depth decrement
inline constexpr int ALPHA_RAISE_DEPTH_MIN = 5;
inline constexpr int ALPHA_RAISE_DEPTH_MAX = 9;

// IIR
inline constexpr int IIR_MIN_DEPTH = 5;

// History stat bonus/malus
inline constexpr int STAT_BONUS_MAX = 1409;
inline constexpr int STAT_BONUS_MULT = 175;
inline constexpr int STAT_BONUS_BASE = 15;
inline constexpr int STAT_MALUS_MAX = 1047;
inline constexpr int STAT_MALUS_MULT = 196;
inline constexpr int STAT_MALUS_BASE = 25;

// LMR table
extern int LMRTable[2][64][64];
void init_lmr();
static_assert(LMR_FRAC == 1024, "LMR_FRAC should be 1024");

struct SearchStack {
  Move *pv = nullptr;
  Move move = MOVE_NONE;
  PieceType movedPt = NO_PIECE_TYPE;
  int staticEval = SCORE_NONE;
  int rawEval = SCORE_NONE;
  int complexity = 0;
  int seenMoves = 0;
  bool playedCap = false;
  int cutoffCnt = 0;
  int reduction = 0;
  bool ttPv = false;
  int histScore = 0;
  int doubleExtensions = 0;
  ContinuationHistory *contHistEntry = nullptr;
};

struct SearchInfo {
  uint64_t nodes = 0;
  uint64_t bestMoveNodes = 0;
  int depth = 0;
  int selDepth = 0;
  Move bestMove = MOVE_NONE;
  int lastScore = 0;

  void reset() {
    nodes = bestMoveNodes = 0;
    depth = selDepth = lastScore = 0;
    bestMove = MOVE_NONE;
  }
};

class Search {
public:
  Search();

  std::atomic<uint64_t> *sharedNodes_ = nullptr;
  bool isSilent = false;
  std::atomic<bool> stopped{false};

  Move best_move(Board &board, TimeManager &tm);
  void stop() {
    if (tm_)
      tm_->stop();
  }
  uint64_t nodes() const { return info_.nodes; }
  int last_score() const { return info_.lastScore; }
  void clear_tables();

  Move ponder_move() const {
    return (pvTable_[0].length >= 2) ? pvTable_[0].moves[1] : MOVE_NONE;
  }

  [[nodiscard]] bool see_ge(const Board &board, Move m, int threshold) const {
    MoveBuffer tmpBuf;
    MovePicker tmp(board, MOVE_NONE, 0, true, captureHistory_, tmpBuf);
    return tmp.see_ge(m, threshold);
  }

private:
  SearchInfo info_;
  TimeManager *tm_ = nullptr;

  // History tables
  ButterflyHistory history_;
  CaptureHistory captureHistory_;
  PawnHistory pawnHistory_;
  Move counterMoves_[COLOR_NB][SQUARE_NB][SQUARE_NB];
  Move killers_[MAX_PLY][2];
  ContinuationHistory contHistTable_[COLOR_NB][PIECE_TYPE_NB][SQUARE_NB];

  // Correction history
  static constexpr int CORR_SIZE = 16384;
  static constexpr int CORR_SCALE = 256;
  static constexpr int PAWN_CORR_SIZE = 16384;
  static constexpr int NONPAWN_CORR_SIZE = 16384;
  static constexpr int CONT_CORR_SIZE = 512;

  static_assert(CORR_SIZE == 16384, "CORR_SIZE must be 16384");
  static_assert(PAWN_CORR_SIZE == 16384, "PAWN_CORR_SIZE must be 16384");

  int corrMain_[COLOR_NB][CORR_SIZE];
  int corrPawn_[COLOR_NB][PAWN_CORR_SIZE];
  int corrNonPawnWhite_[COLOR_NB][NONPAWN_CORR_SIZE];
  int corrNonPawnBlack_[COLOR_NB][NONPAWN_CORR_SIZE];
  int contCorr_[COLOR_NB][PIECE_TYPE_NB][SQUARE_NB][CONT_CORR_SIZE];

  // Per-ply data (+4 guard entries at front)
  SearchStack stack_[MAX_PLY + 8];
  SearchStack *ss(int ply) { return &stack_[ply + 4]; }
  const SearchStack *ss(int ply) const { return &stack_[ply + 4]; }
  NNUE::AccumulatorStack accStack_;

  StateInfo statePool_[32768];
  int stateSP_ = 0;

  PvList pvTable_[MAX_PLY];

  MoveBuffer moveBufs_[MAX_PLY];

  int nmpMinPly_ = 0;

  int negamax(Board &board, int depth, int alpha, int beta, int ply, bool isPV,
              bool cutNode, Move excludedMove = MOVE_NONE);
  int quiescence(Board &board, int alpha, int beta, int ply);

  // Eval with correction
  int adjusted_eval(const Board &board, int ply);
  void update_correction(const Board &board, int ply, int staticEval,
                         int searchScore, int depth, bool bestIsCap);

  // History helpers
  static int stat_bonus(int depth) {
    return std::min(STAT_BONUS_MULT * depth + STAT_BONUS_BASE, STAT_BONUS_MAX);
  }
  static int stat_malus(int depth) {
    return std::min(STAT_MALUS_MULT * depth - STAT_MALUS_BASE, STAT_MALUS_MAX);
  }
  static void gravity(int &e, int bonus, int max) {
    e += bonus - e * std::abs(bonus) / max;
  }

  [[nodiscard]] int quiet_hist_score(const Board &board, Color us, Move m,
                                     PieceType movedPt, int ply) const;
  [[nodiscard]] int capture_hist_score(Color us, Move m, PieceType movedPt,
                                       PieceType capturedPt) const;
  void update_killers(Move m, int ply);
  void update_counter(Color us, Move prevMove, Move reply);
  void update_quiet_histories(const Board &board, Color us, Move bestMove,
                              PieceType bestPt, int histDepth, int ply,
                              Move *tried, int triedCount);
  void update_capture_histories(const Board &board, Color us, Move bestMove,
                                PieceType bestPt, PieceType bestCaptPt,
                                int histDepth, Move *tried, PieceType *triedPts,
                                PieceType *triedCaptPts, int triedCount);

  ContinuationHistory *cont_hist(Color us, PieceType pt, Square to) {
    return &contHistTable_[us][pt][to];
  }

  // Misc helpers
  bool is_valid_tt_move(const Board &board, Move m) const;
  bool opponent_has_winning_capture(const Board &board) const;
  bool is_shuffling(Move m, int ply) const;

  Key non_pawn_key(const Board &board) const {
    return board.key() * 0x9e3779b97f4a7c15ULL;
  }

  int draw_score() const { return 4 - int(info_.nodes & 3); }

  static int ilerp(int a, int b, double t) {
    return int(a + t * double(b - a));
  }

  static bool is_mate_score(int s) {
    return std::abs(s) >= SCORE_MATE_IN_MAX_PLY;
  }

  void print_info(const Board &board, int depth, int score, int elapsed,
                  uint64_t reportNodes) const;
};

} // namespace Catalyst