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

#include "search.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>

#include "bitboard.h"
#include "board.h"
#include "movegen.h"
#include "movepick.h"
#include "timeman.h"
#include "tt.h"
#include "types.h"

namespace Catalyst {

int LMRTable[2][64][64];

void init_lmr() {
  for (int d = 1; d < 64; ++d)
    for (int m = 1; m < 64; ++m) {
      double lp = std::log(double(d)) * std::log(double(m));
      LMRTable[1][d][m] = int(LMR_QUIET_BASE + lp * LMR_QUIET_SCALE);
      LMRTable[0][d][m] = int(LMR_NOISY_BASE + lp * LMR_NOISY_SCALE);
    }
}

Search::Search() {
  init_lmr();
  clear_tables();
}

void Search::clear_tables() {
  std::memset(history_, 0, sizeof(history_));
  std::memset(captureHistory_, 0, sizeof(captureHistory_));
  std::memset(pawnHistory_, 0, sizeof(pawnHistory_));
  std::memset(counterMoves_, 0, sizeof(counterMoves_));
  std::memset(killers_, 0, sizeof(killers_));
  std::memset(contHistTable_, 0, sizeof(contHistTable_));
  std::memset(corrMain_, 0, sizeof(corrMain_));
  std::memset(corrPawn_, 0, sizeof(corrPawn_));
  std::memset(corrNonPawnWhite_, 0, sizeof(corrNonPawnWhite_));
  std::memset(corrNonPawnBlack_, 0, sizeof(corrNonPawnBlack_));
  std::memset(contCorr_, 0, sizeof(contCorr_));
  for (auto &pv : pvTable_) {
    pv.length = 0;
  }
  for (auto &s : stack_)
    s = SearchStack{};
  nmpMinPly_ = 0;
  stateSP_ = 0;
}

// ---------------------------------------------------------------------------
// TT move validation — guards against hash collisions producing garbage moves
// ---------------------------------------------------------------------------
bool Search::is_valid_tt_move(const Board &board, Move m) const {
  if (m == MOVE_NONE)
    return false;
  Square from = from_sq(m);
  Square to = to_sq(m);
  if (!is_ok(from) || !is_ok(to) || from == to)
    return false;
  Piece pc = board.piece_on(from);
  if (pc == NO_PIECE)
    return false;
  if (piece_color(pc) != board.side_to_move())
    return false;
  if (!board.is_pseudo_legal(m))
    return false;
  if (!board.is_legal(m))
    return false;
  return true;
}

// ---------------------------------------------------------------------------
// History helpers
// ---------------------------------------------------------------------------
int Search::quiet_hist_score(const Board &board, Color us, Move m,
                             PieceType movedPt, int ply) const {
  int s = history_[us][from_sq(m)][to_sq(m)];
  s += pawnHistory_[pawn_history_index(board.pawn_key())][movedPt][to_sq(m)];
  const SearchStack *cur = ss(ply);
  if ((cur - 1)->contHistEntry)
    s += (*(cur - 1)->contHistEntry)[movedPt][to_sq(m)];
  if ((cur - 2)->contHistEntry)
    s += (*(cur - 2)->contHistEntry)[movedPt][to_sq(m)];
  if ((cur - 4)->contHistEntry)
    s += (*(cur - 4)->contHistEntry)[movedPt][to_sq(m)] / 2;
  return s;
}

int Search::capture_hist_score(Color us, Move m, PieceType movedPt,
                               PieceType capturedPt) const {
  return captureHistory_[us][movedPt][to_sq(m)][capturedPt];
}

void Search::update_killers(Move m, int ply) {
  if (killers_[ply][0] == m)
    return;
  killers_[ply][1] = killers_[ply][0];
  killers_[ply][0] = m;
}

void Search::update_counter(Color us, Move prevMove, Move reply) {
  if (prevMove != MOVE_NONE && reply != MOVE_NONE)
    counterMoves_[us][from_sq(prevMove)][to_sq(prevMove)] = reply;
}

void Search::update_quiet_histories(const Board &board, Color us, Move bestMove,
                                    PieceType bestPt, int histDepth, int ply,
                                    Move *tried, int triedCount) {
  int bonus = stat_bonus(histDepth);
  int malus = -stat_malus(histDepth);
  int phIdx = pawn_history_index(board.pawn_key());
  SearchStack *cur = ss(ply);
  ContinuationHistory *ch1 = (cur - 1)->contHistEntry;
  ContinuationHistory *ch2 = (cur - 2)->contHistEntry;
  ContinuationHistory *ch4 = (cur - 4)->contHistEntry;

  gravity(history_[us][from_sq(bestMove)][to_sq(bestMove)], bonus, HISTORY_MAX);
  gravity(pawnHistory_[phIdx][bestPt][to_sq(bestMove)], bonus, HISTORY_MAX);
  if (ch1)
    gravity((*ch1)[bestPt][to_sq(bestMove)], bonus, HISTORY_MAX);
  if (ch2)
    gravity((*ch2)[bestPt][to_sq(bestMove)], bonus, HISTORY_MAX);
  if (ch4)
    gravity((*ch4)[bestPt][to_sq(bestMove)], bonus / 2, HISTORY_MAX);

  for (int i = 0; i < triedCount; ++i) {
    if (tried[i] == bestMove)
      continue;
    PieceType qpt = piece_type(board.piece_on(from_sq(tried[i])));
    gravity(history_[us][from_sq(tried[i])][to_sq(tried[i])], malus,
            HISTORY_MAX);
    gravity(pawnHistory_[phIdx][qpt][to_sq(tried[i])], malus, HISTORY_MAX);
    if (ch1)
      gravity((*ch1)[qpt][to_sq(tried[i])], malus, HISTORY_MAX);
    if (ch2)
      gravity((*ch2)[qpt][to_sq(tried[i])], malus, HISTORY_MAX);
    if (ch4)
      gravity((*ch4)[qpt][to_sq(tried[i])], malus / 2, HISTORY_MAX);
  }
}

void Search::update_capture_histories(const Board & /*board*/, Color us,
                                      Move bestMove, PieceType bestPt,
                                      PieceType bestCaptPt, int histDepth,
                                      Move *tried, PieceType *triedPts,
                                      PieceType *triedCaptPts, int triedCount) {
  int bonus = stat_bonus(histDepth);
  int malus = -stat_malus(histDepth);
  gravity(captureHistory_[us][bestPt][to_sq(bestMove)][bestCaptPt], bonus,
          HISTORY_MAX);
  for (int i = 0; i < triedCount; ++i) {
    if (tried[i] == bestMove)
      continue;
    gravity(captureHistory_[us][triedPts[i]][to_sq(tried[i])][triedCaptPts[i]],
            malus, HISTORY_MAX);
  }
}

// ---------------------------------------------------------------------------
// Misc helpers
// ---------------------------------------------------------------------------
bool Search::opponent_has_winning_capture(const Board &board) const {
  Color them = ~board.side_to_move();
  Bitboard occ = board.pieces();
  for (PieceType pt = PAWN; pt <= QUEEN; ++pt) {
    Bitboard pieces = board.pieces(pt, them);
    while (pieces) {
      Square from = pop_lsb(pieces);
      Bitboard targets = board.pieces(board.side_to_move()) &
                         ~board.pieces(KING, board.side_to_move());
      Bitboard atks = 0;
      switch (pt) {
      case PAWN:
        atks = pawn_attacks(them, from);
        break;
      case KNIGHT:
        atks = knight_attacks(from);
        break;
      case BISHOP:
        atks = bishop_attacks(from, occ);
        break;
      case ROOK:
        atks = rook_attacks(from, occ);
        break;
      case QUEEN:
        atks = bishop_attacks(from, occ) | rook_attacks(from, occ);
        break;
      default:
        break;
      }
      Bitboard caps = atks & targets;
      while (caps) {
        Square to = pop_lsb(caps);
        if (PIECE_VALUE[pt] <= PIECE_VALUE[piece_type(board.piece_on(to))])
          return true;
      }
    }
  }
  return false;
}

bool Search::is_shuffling(Move m, int ply) const {
  if (ply < 4)
    return false;
  const SearchStack *cur = ss(ply);
  if ((cur - 2)->move == MOVE_NONE)
    return false;
  bool backForth = (from_sq(m) == to_sq((cur - 2)->move)) &&
                   (to_sq(m) == from_sq((cur - 2)->move));
  if (!backForth)
    return false;
  if (ply >= 4 && (cur - 4)->move != MOVE_NONE)
    return (from_sq((cur - 2)->move) == to_sq((cur - 4)->move));
  return true;
}

// ---------------------------------------------------------------------------
// Evaluation with correction history and fifty-move scaling
// ---------------------------------------------------------------------------
int Search::adjusted_eval(const Board &board, int ply) {
  int raw = NNUE::evaluate(accStack_, board, board.side_to_move());
  Color us = board.side_to_move();

  int corr = 0;
  corr += corrMain_[us][(board.non_pawn_key(WHITE) ^ board.non_pawn_key(BLACK) * 2) % CORR_SIZE];
  corr += corrPawn_[us][board.pawn_key() % PAWN_CORR_SIZE];
  
  corr += corrNonPawnWhite_[us][board.non_pawn_key(WHITE) % NONPAWN_CORR_SIZE];
  corr += corrNonPawnBlack_[us][board.non_pawn_key(BLACK) % NONPAWN_CORR_SIZE];

  const SearchStack *cur = ss(ply);
  if (ply >= 1 && (cur - 1)->move != MOVE_NONE &&
      (cur - 1)->movedPt != NO_PIECE_TYPE &&
      to_sq((cur - 1)->move) < SQUARE_NB) {
    int ph = pawn_history_index(board.pawn_key());
    corr += contCorr_[us][(cur - 1)->movedPt][to_sq((cur - 1)->move)]
                     [ph & (CONT_CORR_SIZE - 1)];
  }

  int adjusted = raw + corr / CORR_SCALE;
  adjusted = std::clamp(adjusted, -SCORE_MATE_IN_MAX_PLY + 1,
                        SCORE_MATE_IN_MAX_PLY - 1);

  int rule50 = board.rule50_count();
  if (rule50 >= FIFTY_SCALE_NUM)
    return 0;
  return adjusted * (FIFTY_SCALE_NUM - rule50) / FIFTY_SCALE_NUM;
}

void Search::update_correction(const Board &board, int ply, int staticEval,
                               int searchScore, int depth, bool bestIsCap) {
  if (bestIsCap || depth < 2 || staticEval == SCORE_NONE)
    return;
  if (is_mate_score(searchScore) || is_mate_score(staticEval))
    return;

  int bonus = std::clamp((searchScore - staticEval) * depth / 8, -8192, 8192);
  Color us = board.side_to_move();
  auto upd = [&](int &e) {
    e += bonus - e * std::abs(bonus) / (32 * CORR_SCALE);
  };

  upd(corrMain_[us][(board.non_pawn_key(WHITE) ^ board.non_pawn_key(BLACK) * 2) % CORR_SIZE]);
  upd(corrPawn_[us][board.pawn_key() % PAWN_CORR_SIZE]);
  
  upd(corrNonPawnWhite_[us][board.non_pawn_key(WHITE) % NONPAWN_CORR_SIZE]);
  upd(corrNonPawnBlack_[us][board.non_pawn_key(BLACK) % NONPAWN_CORR_SIZE]);

  SearchStack *cur = ss(ply);
  if ((cur - 1)->move != MOVE_NONE) {
    int ph = pawn_history_index(board.pawn_key());
    upd(contCorr_[us][(cur - 1)->movedPt][to_sq((cur - 1)->move)]
                 [ph & (CONT_CORR_SIZE - 1)]);
  }
}

void Search::print_info([[maybe_unused]] const Board &board, int depth,
                        int score, int elapsed, uint64_t reportNodes) const {
  std::cout << "info depth " << depth << " seldepth " << info_.selDepth;
  if (is_mate_score(score)) {
    int mateMoves = (SCORE_MATE - std::abs(score) + 1) / 2;
    std::cout << " score mate " << (score > 0 ? mateMoves : -mateMoves);
  } else {
    std::cout << " score cp " << score;
  }
  int nps = elapsed > 0 ? int(reportNodes * 1000ULL / uint64_t(elapsed)) : 0;
  std::cout << " nodes " << reportNodes << " nps " << nps << " time " << elapsed
            << " hashfull " << tt.hashfull() << " pv ";
  for (int i = 0; i < pvTable_[0].length; ++i) {
    Move pvm = pvTable_[0].moves[i];
    if (pvm == MOVE_NONE)
      break;
    std::cout << move_to_uci(pvm) << " ";
  }
  std::cout << "\n";
  std::cout.flush();
}

// ---------------------------------------------------------------------------
// Quiescence search
// ---------------------------------------------------------------------------
int Search::quiescence(Board &board, int alpha, int beta, int ply) {
  ++info_.nodes;

  if ((info_.nodes & ((1 << 15) - 1)) == 0) {
    if (!isSilent && tm_ && !tm_->is_pondering()) {
      int elapsed = tm_->elapsed_ms();
      std::cout << "info nodes " << info_.nodes << " time " << elapsed << "\n";
      std::cout.flush();
    }
  }

  if ((stopped.load(std::memory_order_relaxed) || tm_->time_up(info_.nodes)))
    return 0;
  if (ply >= MAX_PLY - 1)
    return adjusted_eval(board, ply);
  if (ply > info_.selDepth)
    info_.selDepth = ply;
  if (board.is_draw(ply))
    return draw_score();

  // TT probe
  bool ttHit = false;
  TTEntry *ttEntry = tt.probe(board.key(), ttHit);
  Move ttMove = MOVE_NONE;
  int ttScore = SCORE_NONE;
  TTFlag ttFlag = TT_NONE;

  if (ttHit) {
    Move rawTT = ttEntry->get_move();
    ttScore = score_from_tt(ttEntry->get_score(), ply);
    ttFlag = ttEntry->get_flag();

    if (is_valid_tt_move(board, rawTT))
      ttMove = rawTT;

    if (ttEntry->get_depth() >= 0) {
      if (ttFlag == TT_EXACT)
        return ttScore;
      if (ttFlag == TT_LOWER && ttScore >= beta)
        return ttScore;
      if (ttFlag == TT_UPPER && ttScore <= alpha)
        return ttScore;
    }
  }

  const bool inCheck = board.in_check();
  int standPat = adjusted_eval(board, ply);
  int bestScore = inCheck ? -SCORE_INFINITE : standPat;

  if (!inCheck) {
    if (standPat >= beta) {
      if (!is_mate_score(standPat) && !is_mate_score(beta))
        return ilerp(standPat, beta, QS_CUTOFF_LERP);
      return standPat;
    }
    if (standPat > alpha)
      alpha = standPat;
    if (standPat + DELTA_MARGIN + PIECE_VALUE[QUEEN] <= alpha)
      return standPat;
  }

  ss(ply)->staticEval = inCheck ? SCORE_NONE : standPat;

  // Use a local buffer — never alias with negamax buffers
  MoveBuffer qsBuf;
  MovePicker mp(board, ttMove, standPat - alpha - DELTA_MARGIN, true,
                captureHistory_, qsBuf);

  int moveCount = 0;

  while (Move m = mp.next_move()) {
    if (!is_valid_tt_move(board, m) && !board.is_pseudo_legal(m))
      continue;

    const bool isCapture = board.is_capture(m);
    const bool isPromo = is_promotion(m);
    const bool isQuiet = !isCapture && !isPromo;
    const bool givesCheck = board.gives_check(m);

    if (isQuiet && !givesCheck)
      continue;
    if (isQuiet && givesCheck) {
      PieceType movedPt = piece_type(board.piece_on(from_sq(m)));
      if (PIECE_VALUE[movedPt] > PIECE_VALUE[PAWN] + 50)
        continue;
    }

    if (!inCheck && !givesCheck && isCapture && !isPromo) {
      PieceType captPt = piece_type(board.piece_on(to_sq(m)));
      int futVal = standPat + PIECE_VALUE[captPt] + 200;
      if (futVal <= alpha) {
        bestScore = std::max(bestScore, futVal);
        continue;
      }
    }

    if (!board.is_legal(m))
      continue;
    if (stateSP_ >= 32767) {

      continue;
    }

    const Piece qs_moved = board.piece_on(from_sq(m));
    const Piece qs_captured = is_en_passant(m)
                                  ? makePiece(~board.side_to_move(), PAWN)
                                  : board.piece_on(to_sq(m));
    const Color qs_stm = board.side_to_move();

    board.make_move(m, statePool_[stateSP_++]);
    NNUE::push_move(accStack_, board, m, qs_stm, qs_moved, qs_captured);

    ++moveCount;
    int score = -quiescence(board, -beta, -alpha, ply + 1);
    board.unmake_move(m);
    accStack_.pop();
    --stateSP_;

    if ((stopped.load(std::memory_order_relaxed) || tm_->time_up(info_.nodes)))
      return 0;

    if (score > bestScore)
      bestScore = score;
    if (score > alpha) {
      alpha = score;
      if (alpha >= beta)
        break;
    }
  }

  if (inCheck && moveCount == 0)
    return -SCORE_MATE + ply;

  if (bestScore >= beta && !is_mate_score(bestScore) && !is_mate_score(beta))
    bestScore = ilerp(bestScore, beta, QS_FAILHIGH_LERP);

  if (moveCount > 0 &&
      !(stopped.load(std::memory_order_relaxed) || tm_->time_up(info_.nodes)) &&
      std::abs(bestScore) < SCORE_INFINITE) {
    TTFlag flag = (bestScore >= beta) ? TT_LOWER : TT_UPPER;
    int storeEval =
        (standPat != SCORE_NONE && std::abs(standPat) < SCORE_INFINITE)
            ? standPat
            : 0;
    tt.store(board.key(), score_to_tt(bestScore, ply), -1, flag, MOVE_NONE, storeEval, false);
  }

  return bestScore;
}

// ---------------------------------------------------------------------------
// Negamax
// ---------------------------------------------------------------------------
int Search::negamax(Board &board, int depth, int alpha, int beta, int ply,
                    bool isPV, bool cutNode, Move excludedMove) {
  ++info_.nodes;

  if ((info_.nodes & ((1 << 17) - 1)) == 0) {
    if (!isSilent && tm_ && !tm_->is_pondering()) {
      int elapsed = tm_->elapsed_ms();
      std::cout << "info depth " << info_.depth << " seldepth "
                << info_.selDepth << " nodes " << info_.nodes << " nps "
                << (elapsed > 0 ? int(info_.nodes * 1000ULL / uint64_t(elapsed))
                                : 0)
                << " time " << elapsed << " hashfull " << tt.hashfull() << "\n";
      std::cout.flush();
    }
  }

  if ((stopped.load(std::memory_order_relaxed) || tm_->time_up(info_.nodes)))
    return 0;
  if (ply >= MAX_PLY - 1)
    return adjusted_eval(board, ply);

  const bool rootNode = (ply == 0);
  const bool pvNode = isPV;

  if (!rootNode && board.is_draw(ply))
    return draw_score();

  // Mate distance pruning
  if (!rootNode) {
    alpha = std::max(alpha, -SCORE_MATE + ply);
    beta = std::min(beta, SCORE_MATE - ply);
    if (alpha >= beta)
      return alpha;
  }

  const bool inCheck = board.in_check();

  if (inCheck && depth <= 0)
    depth = 1;
  if (depth <= 0 && !inCheck)
    return quiescence(board, alpha, beta, ply + 1);

  if (ply > info_.selDepth)
    info_.selDepth = ply;

  SearchStack *cur = ss(ply);
  pvTable_[ply].length = 0;
  cur->seenMoves = 0;
  if (ply + 1 < MAX_PLY)
    ss(ply + 1)->cutoffCnt = 0;

  const int priorReduction = (ply > 0) ? ss(ply - 1)->reduction : 0;
  cur->reduction = 0;

  // ── TT probe ──────────────────────────────────────────────────────────────
  bool ttHit = false;
  TTEntry *ttEntry = tt.probe(board.key(), ttHit);
  Move ttMove = MOVE_NONE;
  int ttScore = SCORE_NONE;
  int ttDepth = 0;
  int ttEval = SCORE_NONE;
  TTFlag ttFlag = TT_NONE;
  bool ttPV = pvNode;

  if (ttHit && excludedMove == MOVE_NONE) {
    Move rawTT = ttEntry->get_move();
    ttScore = score_from_tt(ttEntry->get_score(), ply);
    ttDepth = ttEntry->get_depth();
    ttFlag = ttEntry->get_flag();
    ttEval = ttEntry->get_eval();
    ttPV |= (ttFlag == TT_EXACT);

    if (is_valid_tt_move(board, rawTT))
      ttMove = rawTT;

    // TT cutoff
    if (!pvNode && ttDepth >= depth) {
      if (ttFlag == TT_EXACT)
        return ttScore;
      if (ttFlag == TT_LOWER && ttScore >= beta) {
        // Reward quiet TT cut move
        if (ttMove != MOVE_NONE && !board.is_capture_or_promotion(ttMove)) {
          int bonus = std::min(STAT_BONUS_MULT * depth + STAT_BONUS_BASE,
                               STAT_BONUS_MAX);
          Color us = board.side_to_move();
          PieceType ttPt = piece_type(board.piece_on(from_sq(ttMove)));
          gravity(history_[us][from_sq(ttMove)][to_sq(ttMove)], bonus,
                  HISTORY_MAX);
          int phIdx = pawn_history_index(board.pawn_key());
          gravity(pawnHistory_[phIdx][ttPt][to_sq(ttMove)], bonus, HISTORY_MAX);
          if ((cur - 1)->contHistEntry)
            gravity((*(cur - 1)->contHistEntry)[ttPt][to_sq(ttMove)], bonus,
                    HISTORY_MAX);
          if ((cur - 2)->contHistEntry)
            gravity((*(cur - 2)->contHistEntry)[ttPt][to_sq(ttMove)], bonus,
                    HISTORY_MAX);
        }
        return ttScore;
      }
      if (ttFlag == TT_UPPER && ttScore <= alpha)
        return ttScore;
    }
  }

  cur->ttPv = ttPV;

  // ── Static evaluation ──────────────────────────────────────────────────────
  int staticEval = SCORE_NONE;
  int rawEval = SCORE_NONE;

  if (!inCheck) {
    rawEval = (ttHit && ttEval != 0)
                  ? ttEval
                  : NNUE::evaluate(accStack_, board, board.side_to_move());
    staticEval = adjusted_eval(board, ply);
    cur->complexity = std::abs(staticEval - rawEval);

    if (!ttHit && excludedMove == MOVE_NONE && depth >= 4)
        tt.store(board.key(), 0, 0, TT_NONE, MOVE_NONE, rawEval, false);

    if (ttHit && ttFlag != TT_NONE) {
      if (ttFlag == TT_LOWER && ttScore > staticEval)
        staticEval = ttScore;
      if (ttFlag == TT_UPPER && ttScore < staticEval)
        staticEval = ttScore;
    }

    cur->staticEval = staticEval;
    cur->rawEval = rawEval;

    // Eval history update
    if (ply >= 1 && (cur - 1)->move != MOVE_NONE && !(cur - 1)->playedCap &&
        (cur - 1)->staticEval != SCORE_NONE) {
      int delta = staticEval + (cur - 1)->staticEval;
      int ehBonus = std::clamp(delta, -512, 512);
      Color them = ~board.side_to_move();
      gravity(history_[them][from_sq((cur - 1)->move)][to_sq((cur - 1)->move)],
              ehBonus, HISTORY_MAX);
    }
  } else {
    cur->staticEval = SCORE_NONE;
    cur->rawEval = SCORE_NONE;
    cur->complexity = 0;
  }

  // ── Improving / opponentWorsening ─────────────────────────────────────────
  bool improving = false, opponentWorsening = false;
  if (!inCheck) {
    if (ply >= 2 && (cur - 2)->staticEval != SCORE_NONE)
      improving = staticEval > (cur - 2)->staticEval;
    else if (ply >= 4 && (cur - 4)->staticEval != SCORE_NONE)
      improving = staticEval > (cur - 4)->staticEval;

    if (ply >= 1 && (cur - 1)->staticEval != SCORE_NONE)
      opponentWorsening = staticEval + (cur - 1)->staticEval > 1;
    // NEW: improving if static eval already beats beta
    improving |= staticEval >= beta;
  }

  // ── Hindsight depth adjustment ────────────────────────────────────────────
  if (!inCheck && excludedMove == MOVE_NONE) {
    if (priorReduction >= 3 * LMR_FRAC && !opponentWorsening)
      ++depth;
    // NEW: reduce depth if prior reduction was large and both evals sum high
    if (priorReduction >= 2 * LMR_FRAC && depth >= 2 &&
        staticEval != SCORE_NONE && (cur - 1)->staticEval != SCORE_NONE &&
        staticEval + (cur - 1)->staticEval > 188)
      --depth;
  }

  // ── Non-PV non-check pruning ──────────────────────────────────────────────
  if (!pvNode && !inCheck && excludedMove == MOVE_NONE &&
      staticEval != SCORE_NONE) {
    int prevHistScore = (ply >= 1) ? (cur - 1)->histScore : 0;

    // Razoring
    if (depth <= 3 && staticEval + 350 * depth < alpha) {
      int q = quiescence(board, alpha, alpha + 1, ply + 1);
      if (q <= alpha)
        return q;
    }

    // Reverse futility pruning
    if (depth < RFP_MAX_DEPTH && !is_mate_score(staticEval)) {
      int margin = RFP_MARGIN_MULT * depth - (improving ? 80 : 0) -
                   (opponentWorsening ? 20 : 0) - prevHistScore / RFP_HIST_DIV;
      if (staticEval - std::max(margin, 0) >= beta)
        return ilerp(staticEval, beta, 0.333);
    }

    // Null move pruning
    if (depth >= 3 && staticEval >= beta &&
        staticEval >= beta + NMP_BETA_BASE - NMP_BETA_MULT * depth &&
        (ply == 0 || (cur - 1)->move != MOVE_NONE) && ply >= nmpMinPly_ &&
        (board.pieces(KNIGHT, board.side_to_move()) |
         board.pieces(BISHOP, board.side_to_move()) |
         board.pieces(ROOK, board.side_to_move()) |
         board.pieces(QUEEN, board.side_to_move()))) {

      int R = std::min(NMP_BASE_R + depth / 3 +
                           std::min(3, (staticEval - beta) / NMP_EVAL_DIV),
                       depth);

      cur->move = MOVE_NONE;
      cur->movedPt = NO_PIECE_TYPE;
      cur->playedCap = false;
      cur->contHistEntry = nullptr;

      if (stateSP_ < 32760) {
        board.make_null_move(statePool_[stateSP_++]);
        accStack_.push(); // null move — no piece changes
        int nullScore = -negamax(board, depth - 1 - R, -beta, -beta + 1,
                                 ply + 1, false, !cutNode);
        board.unmake_null_move();
        accStack_.pop();
        --stateSP_;

        if ((stopped.load(std::memory_order_relaxed) ||
             tm_->time_up(info_.nodes)))
          return 0;

        if (nullScore >= beta) {
          if (nullScore >= SCORE_MATE_IN_MAX_PLY)
            nullScore = beta;

          if (depth >= NMP_VERIF_DEPTH && nmpMinPly_ == 0) {
            nmpMinPly_ = ply + 3 * (depth - R) / 4;
            int verified = negamax(board, depth - R, beta - 1, beta, ply + 1,
                                   false, false);
            nmpMinPly_ = 0;
            if (verified >= beta)
              return nullScore;
          } else {
            return nullScore;
          }
        }
      }
    }
  }

  // ── ProbCut ───────────────────────────────────────────────────────────────
  if (!pvNode && !inCheck && excludedMove == MOVE_NONE &&
      depth >= PROBCUT_DEPTH && std::abs(beta) < SCORE_MATE_IN_MAX_PLY) {

    int pcBeta = beta + PROBCUT_MARGIN - 63 * improving;
    int pcCount = 0;

    // Dedicated buffer — never touches the main move loop buffer
    MoveBuffer pcBuf;
    MovePicker pcMp(board, ttMove, pcBeta - staticEval, true, captureHistory_,
                    pcBuf);

    while (Move m = pcMp.next_move()) {
      if (pcCount++ >= PROBCUT_MAX)
        break;
      if (!board.is_pseudo_legal(m))
        continue;
      if (m == excludedMove)
        continue;
      if (!board.is_legal(m))
        continue;
      if (stateSP_ >= 32767)
        continue;

      const Color pc_stm = board.side_to_move();
      const Piece pc_moved = board.piece_on(from_sq(m));
      const Piece pc_captured = is_en_passant(m) ? makePiece(~pc_stm, PAWN)
                                                 : board.piece_on(to_sq(m));
      board.make_move(m, statePool_[stateSP_++]);
      NNUE::push_move(accStack_, board, m, pc_stm, pc_moved, pc_captured);
      int pcScore = -quiescence(board, -pcBeta, -pcBeta + 1, ply + 1);
      if (pcScore >= pcBeta && depth >= 4)
        pcScore = -negamax(board, depth - 4, -pcBeta, -pcBeta + 1, ply + 1,
                           false, !cutNode);
      board.unmake_move(m);
      accStack_.pop();
      --stateSP_;

      if ((stopped.load(std::memory_order_relaxed) ||
           tm_->time_up(info_.nodes)))
        return 0;

      if (pcScore >= pcBeta) {
          tt.store(board.key(),
              score_to_tt(pcScore, ply),
              depth - 3,
              TT_LOWER,
              m,
              rawEval != SCORE_NONE ? rawEval : 0,
              false);
          return pcScore;
      }
    }
  }

  // ── IIR ───────────────────────────────────────────────────────────────────
  if (depth >= IIR_MIN_DEPTH && ttMove == MOVE_NONE &&
      excludedMove == MOVE_NONE && !inCheck)
    --depth;

  // NEW: small probcut — if TT says this node is likely to fail high by a large
  // margin, return early without searching moves
  if (!pvNode && excludedMove == MOVE_NONE && ttFlag == TT_LOWER &&
      ttDepth >= depth - 4 && ttScore >= beta + 416 && !is_mate_score(beta) &&
      !is_mate_score(ttScore))
    return beta + 416;

  // ── Move loop ─────────────────────────────────────────────────────────────
  const Color us = board.side_to_move();
  const Move prevMove = (ply > 0) ? (cur - 1)->move : MOVE_NONE;
  const Move counter =
      (prevMove != MOVE_NONE)
          ? counterMoves_[us][from_sq(prevMove)][to_sq(prevMove)]
          : MOVE_NONE;

  ContinuationHistory *ch1 = (cur - 1)->contHistEntry;
  ContinuationHistory *ch2 = (cur - 2)->contHistEntry;
  ContinuationHistory *ch4 = (cur - 4)->contHistEntry;

  // Each ply gets its own dedicated buffer slot
  MovePicker mp(board, ttMove, ply, killers_[ply][0], killers_[ply][1], counter,
                history_, captureHistory_, pawnHistory_, ch1, ch2, ch4,
                moveBufs_[std::min(ply, MAX_PLY - 1)]);

  Move bestMove = MOVE_NONE;
  int bestScore = -SCORE_INFINITE;
  int origAlpha = alpha;
  int moveCount = 0;
  bool skipQuiets = false;

  Move quietsTried[64];
  int quietCount = 0;
  Move capsTried[32];
  int capsCount = 0;
  PieceType capsPts[32], capsCaptPts[32];

  while (Move m = mp.next_move()) {
    if (m == excludedMove)
      continue;
    if (skipQuiets && !board.is_capture(m) && !is_promotion(m))
      continue;
    if (!board.is_pseudo_legal(m))
      continue;

    const bool isCapture = board.is_capture(m);
    const bool isPromo = is_promotion(m);
    const bool isQuiet = !isCapture && !isPromo;
    const PieceType movedPt = piece_type(board.piece_on(from_sq(m)));
    const PieceType captPt =
        isCapture
            ? (is_en_passant(m) ? PAWN : piece_type(board.piece_on(to_sq(m))))
            : NO_PIECE_TYPE;

    int histScore = isQuiet ? quiet_hist_score(board, us, m, movedPt, ply)
                            : capture_hist_score(us, m, movedPt, captPt);

    if (isQuiet && quietCount < 64)
      quietsTried[quietCount++] = m;
    if (isCapture && capsCount < 32) {
      capsTried[capsCount] = m;
      capsPts[capsCount] = movedPt;
      capsCaptPts[capsCount] = captPt;
      ++capsCount;
    }

    ++moveCount;
    cur->seenMoves = moveCount;

    // ── Pruning ──────────────────────────────────────────────────────────
    if (!rootNode && bestScore > -SCORE_MATE_IN_MAX_PLY) {
      int lmrD = std::max(0, depth - 1 -
                                 LMRTable[isQuiet ? 1 : 0][std::min(63, depth)]
                                         [std::min(63, moveCount)]);

      if (isQuiet && depth <= LMP_MAX_DEPTH && moveCount > 1) {
        int lmpThresh = (LMP_BASE + depth * depth) / (improving ? 1 : 2);
        if (moveCount >= lmpThresh) {
          skipQuiets = true;
          continue;
        }
      }

      if (isQuiet && !inCheck && staticEval != SCORE_NONE &&
          lmrD < FUTILITY_MAX_D && moveCount > 1) {
        int futScore = staticEval + FUTILITY_BASE + FUTILITY_MULT * lmrD;
        if (futScore <= alpha) {
          bestScore = std::max(bestScore, futScore);
          continue;
        }
      }

      // Capture futility - prune bad captures
      if (isCapture && !inCheck && !isPromo && !board.gives_check(m) &&
          moveCount > 1) {
        int captValue = PIECE_VALUE[captPt];
        int futScore = staticEval + captValue + 200;
        if (futScore <= alpha && !see_ge(board, m, 0))
          continue;
      }

      if (moveCount > 1 && depth <= 10) {
        int seeThresh = isQuiet ? SEE_QUIET_THRESH * depth * depth
                                : SEE_NOISY_THRESH * depth;
        if (!see_ge(board, m, seeThresh))
          continue;
      }

      if (isQuiet && !inCheck && depth <= HIST_PRUNE_DEPTH &&
          histScore < -HIST_PRUNE_MULT * depth)
        continue;
    }

    // ── Singular extension ────────────────────────────────────────────────
    int ext = 0;
    if (!rootNode && m == ttMove && excludedMove == MOVE_NONE &&
        depth >= SE_DEPTH && ttDepth >= depth - 3 && ttFlag == TT_LOWER &&
        std::abs(ttScore) < SCORE_MATE_IN_MAX_PLY && !is_shuffling(m, ply)) {

      int singBeta = ttScore - (1 + (ttPV && !pvNode)) * depth;
      // Use ply+1 so the recursive call gets its own moveBuf slot
      int singScore = negamax(board, depth / 2, singBeta - 1, singBeta, ply + 1,
                              false, cutNode, m);

      if (singScore < singBeta) {
        bool doDouble = !pvNode && singScore < singBeta - SE_DOUBLE_MARGIN &&
                        (cur - 1)->doubleExtensions < 3;
        bool doTriple = !pvNode && singScore < singBeta - SE_TRIPLE_MARGIN &&
                        (cur - 1)->doubleExtensions < 2;
        ext = 1 + doDouble + doTriple;
        cur->doubleExtensions = (cur - 1)->doubleExtensions + (ext > 1 ? 1 : 0);
        if (!pvNode && depth < 12)
          ++depth;
      } else if (singScore >= beta) {
        int mcDepth = std::max(depth - 4, depth / 2);
        int mcScore =
            negamax(board, mcDepth, beta - 1, beta, ply, false, cutNode);
        if (mcScore >= beta)
          return mcScore;
      } else if (singBeta >= beta) {
        int mcScore = std::min(singBeta, SCORE_MATE_IN_MAX_PLY - 1);
        tt.store(board.key(),
            score_to_tt(mcScore, ply),
            depth / 2,
            TT_LOWER,
            m,
            rawEval != SCORE_NONE ? rawEval : 0,
            false);
        return mcScore;
      } else if (ttScore >= beta) {
        ext = -1;
      } else if (cutNode) {
        ext = -3;
      }
    }

    if (inCheck)
      ext = std::max(ext, 1);

    if (!board.is_legal(m)) {

      continue;
    }
    if (stateSP_ >= 32767) {

      continue;
    }

    // Save info needed for incremental NNUE update BEFORE make_move
    const Piece moved_piece = board.piece_on(from_sq(m));
    const Piece captured_piece =
        is_en_passant(m) ? makePiece(~us, PAWN) : board.piece_on(to_sq(m));

    board.make_move(m, statePool_[stateSP_++]);

    // Incremental accumulator update
    NNUE::push_move(accStack_, board, m, us, moved_piece, captured_piece);

    const bool givesCheck = board.in_check();

    cur->move = m;
    cur->movedPt = movedPt;
    cur->playedCap = isCapture;
    cur->histScore = histScore;
    cur->contHistEntry = cont_hist(us, movedPt, to_sq(m));

    int newDepth = std::min(depth - 1 + ext, MAX_PLY - ply - 1);
    int score = 0;

    // ── LMR ──────────────────────────────────────────────────────────────
    bool fullSearch = true;
    if (moveCount > 1 + rootNode && newDepth >= 2 && !inCheck) {
      int R_frac = LMRTable[isQuiet ? 1 : 0][std::min(63, newDepth)]
                           [std::min(63, moveCount)] *
                   LMR_FRAC;

      if (cutNode)
        R_frac += 2 * LMR_FRAC;
      else if (!pvNode)
        R_frac += LMR_FRAC;
      if (!improving)
        R_frac += LMR_FRAC;
      if (givesCheck)
        R_frac -= LMR_FRAC;
      if (ttPV)
        R_frac -= 2 * LMR_FRAC;
      if (m == killers_[ply][0] || m == killers_[ply][1])
        R_frac -= 2 * LMR_FRAC;
      if (m == counter)
        R_frac -= LMR_FRAC;
      if (cur->complexity > 50)
        R_frac -= LMR_FRAC;

      if (isQuiet)
        R_frac -= histScore * LMR_FRAC / LMR_HIST_QUIET_DIV;
      else
        R_frac -= histScore * LMR_FRAC / LMR_HIST_NOISY_DIV;
      // Reduce more for very bad history
      if (isQuiet && histScore < -8192)
        R_frac += LMR_FRAC;

      int nextCutoffs = (ply + 1 < MAX_PLY) ? ss(ply + 1)->cutoffCnt : 0;
      if (nextCutoffs > 2)
        R_frac += 2 * LMR_FRAC;

      cur->reduction = R_frac;
      int R = std::clamp((R_frac + LMR_ROUNDING_CUTOFF) / LMR_FRAC, 0,
                         newDepth - 1);

      score = -negamax(board, newDepth - R, -alpha - 1, -alpha, ply + 1, false,
                       true);
      cur->reduction = 0;

      if (score > alpha && R > 0) {
        bool deeper = score > bestScore + ZWS_DEEPER_MARGIN + 2 * newDepth;
        bool shallower = score < bestScore + ZWS_SHALLOWER_MARGIN;
        newDepth = std::min(newDepth + deeper - shallower, MAX_PLY - ply - 1);
        fullSearch = true;
      } else {
        fullSearch = false;
      }
    }

    if (fullSearch) {
      if (pvNode && moveCount == 1) {
        uint64_t nodesBefore = info_.nodes;
        score = -negamax(board, newDepth, -beta, -alpha, ply + 1, true, false);
        if (rootNode)
          info_.bestMoveNodes = info_.nodes - nodesBefore;
      } else {
        score = -negamax(board, newDepth, -alpha - 1, -alpha, ply + 1, false,
                         !cutNode);
        if (pvNode && score > alpha && score < beta) {
          score =
              -negamax(board, newDepth, -beta, -alpha, ply + 1, true, false);
        }
      }
    }

    board.unmake_move(m);
    --stateSP_;
    accStack_.pop();

    if ((stopped.load(std::memory_order_relaxed) || tm_->time_up(info_.nodes)))
      return 0;

    if (score > bestScore) {
      bestScore = score;
      if (rootNode)
        info_.bestMove = m;
    }

    if (score > alpha) {
      bestMove = m;
      alpha = score;
      if (pvNode && (pvTable_[ply].length == 0 ||
                     pvTable_[ply + 1].length + 1 >= pvTable_[ply].length)) {
        pvTable_[ply].update(m, pvTable_[ply + 1]);
      }
      if (!isCapture)
        update_counter(us, prevMove, m);
      if (depth >= ALPHA_RAISE_DEPTH_MIN && depth <= ALPHA_RAISE_DEPTH_MAX &&
          !is_mate_score(beta) && !is_mate_score(alpha))
        --depth;
    }
    if (alpha >= beta) {

      int histDepth =
          depth +
          (staticEval != SCORE_NONE && staticEval <= origAlpha ? 1 : 0) +
          (bestScore > beta + 209 ? 1 : 0);

      if (isQuiet) {
        update_killers(m, ply);
        update_quiet_histories(board, us, m, movedPt, histDepth, ply,
                               quietsTried, quietCount);
      } else {
        update_capture_histories(board, us, m, movedPt, captPt, histDepth,
                                 capsTried, capsPts, capsCaptPts, capsCount);
      }

      if (ext < 2)
        cur->cutoffCnt++;
      break;
    }
  }

  // ── Terminal ──────────────────────────────────────────────────────────────
  if (moveCount == 0) {
    if (excludedMove != MOVE_NONE)
      return alpha;
    return inCheck ? -SCORE_MATE + ply : draw_score();
  }

  // Reward the previous move when we fail low
  if (bestMove == MOVE_NONE && ply >= 1 && (cur - 1)->move != MOVE_NONE &&
      !(cur - 1)->playedCap) {
    Move prev = (cur - 1)->move;
    Color them = ~us;
    int bonus = stat_bonus(depth);
    gravity(history_[them][from_sq(prev)][to_sq(prev)], bonus, HISTORY_MAX);
    int phIdx = pawn_history_index(board.pawn_key());
    gravity(pawnHistory_[phIdx][(cur - 1)->movedPt][to_sq(prev)], bonus / 2,
            HISTORY_MAX);
  }

  // NEW: ttPv propagation — if we fail low, inherit parent's ttPv flag
  // This helps move ordering on re-searches by remembering PV history
  if (bestScore <= origAlpha)
    cur->ttPv = cur->ttPv || (ply > 0 && ss(ply - 1)->ttPv);

  // Correction history update
  bool bestIsCap = (bestMove != MOVE_NONE) && board.is_capture(bestMove);
  if (excludedMove == MOVE_NONE && staticEval != SCORE_NONE &&
      !(stopped.load(std::memory_order_relaxed) || tm_->time_up(info_.nodes)))
    update_correction(board, ply, cur->staticEval, bestScore, depth, bestIsCap);

  // TT store
  if (!(stopped.load(std::memory_order_relaxed) || tm_->time_up(info_.nodes)) &&
      excludedMove == MOVE_NONE && std::abs(bestScore) < SCORE_INFINITE) {

    TTFlag flag;
    if (alpha >= beta)
      flag = TT_LOWER;
    else if (alpha > origAlpha)
      flag = TT_EXACT;
    else
      flag = TT_UPPER;

    int storeScore = bestScore;
    if (flag == TT_LOWER && !is_mate_score(bestScore) && depth > 0)
      storeScore = (bestScore * depth + beta) / (depth + 1);

    int storeEval =
        (rawEval != SCORE_NONE && std::abs(rawEval) < SCORE_INFINITE) ? rawEval
                                                                      : 0;
    tt.store(board.key(), score_to_tt(storeScore, ply), depth, flag, bestMove, storeEval, isPV);
  }

  return bestScore;
}

// ---------------------------------------------------------------------------
// Root search — iterative deepening with aspiration windows
// ---------------------------------------------------------------------------
Move Search::best_move(Board &board, TimeManager &tm) {
  tm_ = &tm;
  stateSP_ = 0;
  if (!isSilent)
    tt.new_search();
  info_.reset();
  // Light reset — preserve history tables across moves within a game.
  // Full clear_tables() is only called between games (via ucinewgame).
  for (auto &pv : pvTable_)
    pv.length = 0;
  for (auto &s : stack_)
    s = SearchStack{};
  nmpMinPly_ = 0;
  stateSP_ = 0;
  accStack_.top = 0;
  NNUE::refresh(board, accStack_.current());

  const SearchLimits &limits = tm_->limits();

  Move bestMove = MOVE_NONE;
  int bestScore = 0;
  int prevScore = 0;
  PvList savedPV{};

  for (int depth = 1; depth <= limits.depth; ++depth) {
    if (!isSilent && !limits.infinite && tm_->soft_limit_reached() && depth > 1)
      break;

    info_.selDepth = 0;
    info_.depth = depth;
    savedPV = PvList{};

    int score = 0;

    bool nearMate = is_mate_score(bestScore) ||
                    std::abs(bestScore) >= SCORE_MATE_IN_MAX_PLY - 50;

    if (depth >= 6 && !nearMate) {
      int delta = ASP_INIT_DELTA + bestScore * bestScore / 13000;
      int wAlpha = bestScore - delta;
      int wBeta = bestScore + delta;

      while (true) {
        score = negamax(board, depth, wAlpha, wBeta, 0, true, false);

        if (score == 0 && (stopped.load(std::memory_order_relaxed) ||
                           tm_->time_up(info_.nodes)))
          break;
        if (tm_->is_stopped())
          break;

        if (score <= wAlpha) {
          wBeta = (wAlpha + wBeta) / 2;
          wAlpha = std::max(wAlpha - delta, -SCORE_INFINITE);
          delta += delta / 2;
        } else if (score >= wBeta) {
          wBeta = std::min(wBeta + delta, SCORE_INFINITE);
          delta += delta / 2;
          if (info_.bestMove != MOVE_NONE)
            bestMove = info_.bestMove;
          savedPV = pvTable_[0];

        } else {
          savedPV = pvTable_[0];
          break;
        }
        if (delta >= ASP_MAX_DELTA) {
          score = negamax(board, depth, -SCORE_INFINITE, SCORE_INFINITE, 0,
                          true, false);
          savedPV = pvTable_[0];

          break;
        }
      }
    } else {
      score = negamax(board, depth, -SCORE_INFINITE, SCORE_INFINITE, 0, true,
                      false);
      savedPV = pvTable_[0];
    }

    if ((stopped.load(std::memory_order_relaxed) ||
         tm_->time_up(info_.nodes)) &&
        depth > 1)
      break;
    if (tm_->is_stopped() && depth > 1)
      break;

    if (info_.bestMove != MOVE_NONE && board.is_legal(info_.bestMove)) {
      bool changed = (info_.bestMove != bestMove);
      int delta = (depth > 1) ? std::abs(score - prevScore) : 0;
      uint64_t totalNodes = sharedNodes_
                                ? sharedNodes_->load(std::memory_order_relaxed)
                                : info_.nodes;
      tm_->update_scale(changed, delta, info_.bestMoveNodes, totalNodes, depth,
                        score);

      bestMove = info_.bestMove;
      prevScore = bestScore;
      bestScore = score;
      info_.lastScore = score;
    }

    if (pvTable_[0].length == 0 && savedPV.length > 0)
      pvTable_[0] = savedPV;
    if (!isSilent) {
      uint64_t reportNodes = sharedNodes_
                                 ? sharedNodes_->load(std::memory_order_relaxed)
                                 : info_.nodes;
      print_info(board, depth, bestScore, tm_->elapsed_ms(), reportNodes);
    }
    // Only stop early for very short forced mates (mate in 1 or 2)
    if (is_mate_score(bestScore) && std::abs(bestScore) >= SCORE_MATE - 4)
      break;
    if (!isSilent && !limits.infinite && tm_->soft_limit_reached())
      break;
  }

  // Fallback
  if (bestMove == MOVE_NONE || !board.is_legal(bestMove)) {
    MoveList moves = generate_legal(board);
    if (!moves.empty())
      bestMove = *moves.begin();
  }

  tm_ = nullptr;
  return bestMove;
}

} // namespace Catalyst
