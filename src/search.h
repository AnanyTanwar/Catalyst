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

// LMRTable[isQuiet][depth][moveCount] — precomputed in init_lmr().
// Formula: base + scale * log(depth) * log(moveCount).
// Quiet moves use a steeper scale since they're safer to reduce than captures.
inline constexpr double LMR_QUIET_BASE      = 0.77;
inline constexpr double LMR_QUIET_SCALE     = 0.40;
inline constexpr double LMR_NOISY_BASE      = 0.10;
inline constexpr double LMR_NOISY_SCALE     = 0.30;
inline constexpr int    LMR_FRAC            = 1024;  // fixed-point denominator for fractional reductions
inline constexpr int    LMR_ROUNDING_CUTOFF = LMR_FRAC / 2;
inline constexpr int    LMR_HIST_QUIET_DIV  = 8192;
inline constexpr int    LMR_HIST_NOISY_DIV  = 12288;
inline constexpr int    LMR_TTPV_REDUCTION  = 2;

// Aspiration windows: start narrow around prev score, widen geometrically on fail.
inline constexpr double ASP_BETA_LERP  = 0.25;
inline constexpr int    ASP_INIT_DELTA = 16;
inline constexpr int    ASP_MAX_DELTA  = 500;  // fall back to full-window search beyond this

// RFP: if static eval - margin >= beta at low depth, skip the search.
inline constexpr int RFP_MARGIN_MULT = 77;
inline constexpr int RFP_HIST_DIV    = 512;
inline constexpr int RFP_MAX_DEPTH   = 16;

// NMP: give opponent a free move — if they still can't beat beta, return early.
// Verification search at high depths guards against zugzwang false positives.
inline constexpr int NMP_BASE_R      = 3;
inline constexpr int NMP_BETA_BASE   = 150;
inline constexpr int NMP_BETA_MULT   = 15;
inline constexpr int NMP_EVAL_DIV    = 200;
inline constexpr int NMP_VERIF_DEPTH = 16;

// ProbCut: if a shallow capture search already beats a high beta, skip the full search.
inline constexpr int PROBCUT_MARGIN = 190;
inline constexpr int PROBCUT_DEPTH  = 5;
inline constexpr int PROBCUT_MAX    = 5;

// SE: extend TT move by 1 if a reduced search with singBeta finds nothing better.
// Double/triple extension when the margin is overwhelming — capped to prevent explosions.
inline constexpr int SE_DEPTH         = 6;
inline constexpr int SE_DOUBLE_MARGIN = 13;
inline constexpr int SE_TRIPLE_MARGIN = 86;

// Futility: skip quiet moves when static eval + margin still fails below alpha.
inline constexpr int FUTILITY_BASE  = 42;
inline constexpr int FUTILITY_MULT  = 120;
inline constexpr int FUTILITY_MAX_D = 13;

// SEE thresholds for pruning — scaled by depth inside the move loop.
inline constexpr int SEE_QUIET_THRESH = -64;
inline constexpr int SEE_NOISY_THRESH = -20;

// LMP: after trying enough quiets at low depth, the rest are almost certainly bad.
inline constexpr int LMP_BASE      = 3;
inline constexpr int LMP_MAX_DEPTH = 8;

// History pruning: skip quiets with deeply negative history at very low depths.
inline constexpr int HIST_PRUNE_MULT  = 4096;
inline constexpr int HIST_PRUNE_DEPTH = 4;

// QS delta pruning: if stand-pat + best possible capture still can't reach alpha, bail.
inline constexpr int    DELTA_MARGIN     = 200;
inline constexpr double QS_CUTOFF_LERP   = 0.50;  // soften stand-pat cutoff to avoid eval cliffs
inline constexpr double QS_FAILHIGH_LERP = 0.50;

// Scale eval toward 0 as 50-move counter climbs — avoids chasing wins we can't prove.
inline constexpr int FIFTY_SCALE_NUM = 220;

// After a ZWS beats alpha, decide if the re-search should go deeper or shallower.
inline constexpr int ZWS_DEEPER_MARGIN    = 50;
inline constexpr int ZWS_SHALLOWER_MARGIN = 9;

inline constexpr int LMR_ALPHA_RAISE_SCALE = LMR_FRAC / 2;
inline constexpr int ALPHA_RAISE_DEPTH_MIN = 5;
inline constexpr int ALPHA_RAISE_DEPTH_MAX = 9;

// IIR: no TT move at sufficient depth = reduce by 1, the search is flying blind.
inline constexpr int IIR_MIN_DEPTH = 5;

// History bonus/malus scale linearly with depth and are capped to keep scores bounded.
// gravity() applies decay so values stay in [-HISTORY_MAX, +HISTORY_MAX] naturally.
inline constexpr int STAT_BONUS_MAX  = 1409;
inline constexpr int STAT_BONUS_MULT = 175;
inline constexpr int STAT_BONUS_BASE = 15;
inline constexpr int STAT_MALUS_MAX  = 1047;
inline constexpr int STAT_MALUS_MULT = 196;
inline constexpr int STAT_MALUS_BASE = 25;

extern int LMRTable[2][64][64];
void       init_lmr();
static_assert(LMR_FRAC == 1024, "LMR_FRAC should be 1024");

struct SearchStack {
    // Squares attacked by the opponent — used for threat-aware history indexing.
    // A move that steps off or onto a threatened square gets a different history bucket.
    Bitboard             threats          = 0;
    Move                *pv               = nullptr;
    Move                 move             = MOVE_NONE;
    PieceType            movedPt          = NO_PIECE_TYPE;
    int                  staticEval       = SCORE_NONE;
    int                  rawEval          = SCORE_NONE;
    int                  complexity       = 0;   // |staticEval - rawEval|; large means correction history is doing heavy lifting
    int                  seenMoves        = 0;
    bool                 playedCap        = false;
    int                  cutoffCnt        = 0;   // beta cutoffs seen by children; used to bump LMR on siblings
    int                  reduction        = 0;   // fractional LMR applied this ply, in LMR_FRAC units
    bool                 ttPv             = false;
    int                  histScore        = 0;   // history score of the move played here, referenced by parent
    int                  doubleExtensions = 0;   // inherited double/triple extension count — capped to stop depth spirals
    ContinuationHistory *contHistEntry    = nullptr;
};

struct SearchInfo {
    uint64_t nodes         = 0;
    uint64_t bestMoveNodes = 0;  // nodes spent on the current best move's subtree (for time scaling)
    int      depth         = 0;
    int      selDepth      = 0;
    Move     bestMove      = MOVE_NONE;
    int      lastScore     = 0;

    void reset()
    {
        nodes = bestMoveNodes = 0;
        depth = selDepth = lastScore = 0;
        bestMove                     = MOVE_NONE;
    }
};

class Search {
public:
    // threadIdx: 0 = main thread, 1..N = SMP helpers.
    // poolStop: shared atomic owned by ThreadPool — all threads poll this to know when to abort.
    explicit Search(int threadIdx = 0, std::atomic<bool> *poolStop = nullptr);

    std::atomic<uint64_t> *sharedNodes_ = nullptr;
    bool                   isSilent     = false;  // suppress UCI output (helper threads)

    Move best_move(Board &board, TimeManager &tm);

    Move best_move_result() const { return lastBestMove_; }
    int  completed_depth() const { return completedDepth_; }  // used by best_thread() to pick winner
    uint64_t nodes() const { return info_.nodes; }
    int      last_score() const { return info_.lastScore; }
    void     clear_tables();

    Move ponder_move() const
    {
        return (pvTable_[0].length >= 2) ? pvTable_[0].moves[1] : MOVE_NONE;
    }

    [[nodiscard]] bool see_ge(const Board &board, Move m, int threshold) const
    {
        MoveBuffer tmpBuf;
        MovePicker tmp(board, MOVE_NONE, 0, true, captureHistory_, tmpBuf);
        return tmp.see_ge(m, threshold);
    }

    [[nodiscard]] bool is_stopped() const
    {
        return (poolStop_ && poolStop_->load(std::memory_order_relaxed))
               || (tm_ && tm_->is_stopped());
    }

private:
    int                threadIdx_      = 0;
    std::atomic<bool> *poolStop_       = nullptr;
    int                completedDepth_ = 0;
    Move               lastBestMove_   = MOVE_NONE;

    SearchInfo   info_;
    TimeManager *tm_ = nullptr;

    // history_[color][from][to][threat_index]
    ButterflyHistory    history_;
    // pieceToHistory_[color][piece_type][to][threat_index]
    PieceToHistory      pieceToHistory_;
    // captureHistory_[color][piece_type][to][captured_type][threat_index]
    CaptureHistory      captureHistory_;
    // pawnHistory_[pawn_key_index][piece_type][to] — same move can score differently per pawn structure
    PawnHistory         pawnHistory_;
    // counterMoves_[color][from][to] — quiet move that last refuted (from->to)
    Move                counterMoves_[COLOR_NB][SQUARE_NB][SQUARE_NB];
    Move                killers_[MAX_PLY][2];
    // contHistTable_[color][piece_type][to] — history conditioned on the previous move
    ContinuationHistory contHistTable_[COLOR_NB][PIECE_TYPE_NB][SQUARE_NB];

    static constexpr int CORR_SIZE         = 16384;
    static constexpr int CORR_SCALE        = 256;
    static constexpr int PAWN_CORR_SIZE    = 16384;
    static constexpr int NONPAWN_CORR_SIZE = 16384;
    static constexpr int CONT_CORR_SIZE    = 512;

    static_assert(CORR_SIZE == 16384, "CORR_SIZE must be 16384");
    static_assert(PAWN_CORR_SIZE == 16384, "PAWN_CORR_SIZE must be 16384");

    // Correction history: adjusts NNUE eval based on how wrong it's been in similar positions.
    // Each table slices the position differently; all contributions are summed / CORR_SCALE.
    int corrMain_[COLOR_NB][CORR_SIZE];           // keyed on XOR of both sides' non-pawn material keys
    int corrPawn_[COLOR_NB][PAWN_CORR_SIZE];      // keyed on pawn structure hash
    int corrNonPawnWhite_[COLOR_NB][NONPAWN_CORR_SIZE];
    int corrNonPawnBlack_[COLOR_NB][NONPAWN_CORR_SIZE];
    int contCorr_[COLOR_NB][PIECE_TYPE_NB][SQUARE_NB][CONT_CORR_SIZE];  // conditioned on prev move + pawn structure

    // stack_[ply + 4] — the +4 offset means ss(-4) is always a valid dereference.
    SearchStack            stack_[MAX_PLY + 8];
    SearchStack           *ss(int ply) { return &stack_[ply + 4]; }
    const SearchStack     *ss(int ply) const { return &stack_[ply + 4]; }
    NNUE::AccumulatorStack accStack_;

    // Pre-allocated state pool — avoids heap allocation in the hot path.
    // stateSP_ is a stack pointer, incremented on make_move and decremented on unmake.
    StateInfo statePool_[32768];
    int       stateSP_ = 0;

    PvList pvTable_[MAX_PLY];

    // Each ply gets its own buffer so nested move pickers don't clobber each other.
    MoveBuffer moveBufs_[MAX_PLY];

    // Temporarily raised during NMP verification to prevent recursive null moves.
    int nmpMinPly_ = 0;

    int negamax(Board &board,
        int            depth,
        int            alpha,
        int            beta,
        int            ply,
        bool           isPV,
        bool           cutNode,
        Move           excludedMove = MOVE_NONE);
    int quiescence(Board &board, int alpha, int beta, int ply);

    int  adjusted_eval(const Board &board, int ply);
    void update_correction(const Board &board,
        int                             ply,
        int                             staticEval,
        int                             searchScore,
        int                             depth,
        bool                            bestIsCap);

    static int  stat_bonus(int depth) { return std::min(STAT_BONUS_MULT * depth + STAT_BONUS_BASE, STAT_BONUS_MAX); }
    static int  stat_malus(int depth) { return std::min(STAT_MALUS_MULT * depth - STAT_MALUS_BASE, STAT_MALUS_MAX); }
    // e += bonus - e * |bonus| / max — keeps scores bounded without hard clamping.
    static void gravity(int &e, int bonus, int max) { e += bonus - e * std::abs(bonus) / max; }

    [[nodiscard]] int quiet_hist_score(const Board &board,
        Color                                       us,
        Move                                        m,
        PieceType                                   movedPt,
        int                                         ply) const;
    [[nodiscard]] int capture_hist_score(Color us,
        Move                                   m,
        PieceType                              movedPt,
        PieceType                              capturedPt,
        Bitboard                               threats) const;
    void update_killers(Move m, int ply);
    void update_counter(Color us, Move prevMove, Move reply);
    void update_quiet_histories(const Board &board,
        Color                                us,
        Move                                 bestMove,
        PieceType                            bestPt,
        int                                  histDepth,
        int                                  ply,
        Move                                *tried,
        int                                  triedCount,
        Bitboard                             threats,
        bool                                 improving);
    void update_capture_histories(const Board &board,
        Color                                  us,
        Move                                   bestMove,
        PieceType                              bestPt,
        PieceType                              bestCaptPt,
        int                                    histDepth,
        Move                                  *tried,
        PieceType                             *triedPts,
        PieceType                             *triedCaptPts,
        int                                    triedCount,
        Bitboard                               threats);

    ContinuationHistory *cont_hist(Color us, PieceType pt, Square to)
    {
        return &contHistTable_[us][pt][to];
    }

    bool is_valid_tt_move(const Board &board, Move m) const;
    bool opponent_has_winning_capture(const Board &board) const;
    // Detects A->B->A->B patterns — used to suppress SE on shuffling TT moves.
    bool is_shuffling(Move m, int ply) const;

    // Jittered near-zero draw score — tiny variation by node count prevents the engine
    // from being indifferent between all draws, which can cause repetition-hunting bugs.
    int draw_score() const { return 4 - int(info_.nodes & 3); }

    // Integer lerp — used to soften pruning returns so scores don't jump discontinuously.
    static int  ilerp(int a, int b, double t) { return int(a + t * double(b - a)); }
    static bool is_mate_score(int s) { return std::abs(s) >= SCORE_MATE_IN_MAX_PLY; }

    void print_info(const Board &board,
        int                      depth,
        int                      score,
        int                      elapsed,
        uint64_t                 reportNodes) const;
};

}  // namespace Catalyst
