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

#include "movepick.h"

#include <algorithm>
#include <climits>

#include "bitboard.h"
#include "movegen.h"

namespace Catalyst {

// ---------------------------------------------------------------------------
// MVV-LVA table — indexed [victim][attacker].
// Using victim * 10000 - attacker so cheaper attackers score higher within
// the same victim tier. Much better ordering than MVV alone.
// ---------------------------------------------------------------------------
// clang-format off
static constexpr int MVV_LVA[PIECE_TYPE_NB][PIECE_TYPE_NB] = {
  // victim:      NoPt   P      N      B      R      Q      K
  /* NoPt */  {     0,    0,    0,    0,    0,    0,    0 },
  /* P    */  {     0, 9900, 9800, 9800, 9700, 9600,    0 },
  /* N    */  {     0,19900,19800,19800,19700,19600,    0 },
  /* B    */  {     0,19900,19800,19800,19700,19600,    0 },
  /* R    */  {     0,29900,29800,29800,29700,29600,    0 },
  /* Q    */  {     0,39900,39800,39800,39700,39600,    0 },
  /* K    */  {     0,    0,    0,    0,    0,    0,    0 },
};
// clang-format on

// SEE piece values — coarse, used only in exchange evaluation
static constexpr int SEE_VALUE[PIECE_TYPE_NB] = {
  0, 100, 300, 300, 500, 900, 0,
};

MovePicker::MovePicker(const Board& b, Move ttM, int p, Move k1, Move k2, Move cnt,
                       const ButterflyHistory& hist, const CaptureHistory& captHist,
                       const PawnHistory& pawnHist, const ContinuationHistory* ch1,
                       const ContinuationHistory* ch2, const ContinuationHistory* ch4,
                       MoveBuffer& buf)
  : board(b), stage(STAGE_TT), ttMove(ttM), ply(p), us(b.side_to_move()), killer1(k1), killer2(k2),
    counter(cnt), history(&hist), captureHistory(&captHist), pawnHistory(&pawnHist), contHist1(ch1),
    contHist2(ch2), contHist4(ch4), moves(buf.moves), scores(buf.scores), cur(0), goodCaptEnd(0),
    captEnd(0), quietEnd(0), badCaptCur(0), seeThreshold(SEE_CAPTURE_THRESHOLD),
    qsearchMode(false) {
  if (ttMove != MOVE_NONE
      && (board.piece_on(from_sq(ttMove)) == NO_PIECE
          || piece_color(board.piece_on(from_sq(ttMove))) != board.side_to_move()
          || !board.is_pseudo_legal(ttMove) || !board.is_legal(ttMove)))
    ttMove = MOVE_NONE;
}

MovePicker::MovePicker(const Board& b, Move ttM, int threshold, bool qsOnly,
                       const CaptureHistory& captHist, MoveBuffer& buf)
  : board(b), stage(STAGE_TT), ttMove(ttM), ply(0), us(b.side_to_move()), killer1(MOVE_NONE),
    killer2(MOVE_NONE), counter(MOVE_NONE), history(nullptr), captureHistory(&captHist),
    pawnHistory(nullptr), contHist1(nullptr), contHist2(nullptr), contHist4(nullptr),
    moves(buf.moves), scores(buf.scores), cur(0), goodCaptEnd(0), captEnd(0), quietEnd(0),
    badCaptCur(0), seeThreshold(threshold), qsearchMode(true) {
  if (ttMove != MOVE_NONE
      && (board.piece_on(from_sq(ttMove)) == NO_PIECE
          || piece_color(board.piece_on(from_sq(ttMove))) != board.side_to_move()
          || !board.is_pseudo_legal(ttMove) || !board.is_legal(ttMove)))
    ttMove = MOVE_NONE;
  if (qsOnly && ttMove != MOVE_NONE && !board.is_capture_or_promotion(ttMove))
    ttMove = MOVE_NONE;
}

Move MovePicker::next_move() {
  while (true) {
    switch (stage) {

    case STAGE_TT:
      stage = STAGE_INIT_CAPTURES;
      if (ttMove != MOVE_NONE)
        return ttMove;
      break;

    case STAGE_INIT_CAPTURES:
      generate_and_score_captures();
      stage = STAGE_GOOD_CAPTURES;
      cur = 0;
      break;

    case STAGE_GOOD_CAPTURES:
      while (cur < goodCaptEnd) {
        select_best(cur, goodCaptEnd);
        Move m = moves[cur++];
        if (m == ttMove)
          continue;
        if (qsearchMode && !see_ge(m, seeThreshold))
          continue;
        return m;
      }
      stage = qsearchMode ? STAGE_DONE : STAGE_KILLERS;
      break;

    case STAGE_KILLERS:
      stage = STAGE_KILLER2;
      if (killer1 != MOVE_NONE && killer1 != ttMove && !board.is_capture(killer1)
          && board.is_pseudo_legal(killer1))
        return killer1;
      break;

    case STAGE_KILLER2:
      stage = STAGE_COUNTERS;
      if (killer2 != MOVE_NONE && killer2 != ttMove && killer2 != killer1
          && !board.is_capture(killer2) && board.is_pseudo_legal(killer2))
        return killer2;
      break;

    case STAGE_COUNTERS:
      stage = STAGE_INIT_QUIETS;
      if (counter != MOVE_NONE && counter != ttMove && counter != killer1 && counter != killer2
          && !board.is_capture(counter) && board.is_pseudo_legal(counter))
        return counter;
      break;

    case STAGE_INIT_QUIETS:
      generate_and_score_quiets();
      stage = STAGE_QUIETS;
      cur = captEnd;
      break;

    case STAGE_QUIETS:
      while (cur < quietEnd) {
        select_best(cur, quietEnd);
        Move m = moves[cur++];
        if (m == ttMove || m == killer1 || m == killer2 || m == counter)
          continue;
        if (board.is_capture(m))
          continue;
        return m;
      }
      stage = STAGE_BAD_CAPTURES;
      badCaptCur = goodCaptEnd;
      break;

    case STAGE_BAD_CAPTURES:
      while (badCaptCur < captEnd) {
        Move m = moves[badCaptCur++];
        if (m == ttMove)
          continue;
        if (see_ge(m, seeThreshold))
          continue;
        return m;
      }
      stage = STAGE_DONE;
      break;

    case STAGE_DONE:
      return MOVE_NONE;
    }
  }
}

void MovePicker::generate_and_score_captures() {
  Move* endPtr = generate<CAPTURES>(board, moves);
  captEnd = int(endPtr - moves);

  for (int i = 0; i < captEnd; ++i)
    scores[i] = score_capture(moves[i]);

  // Partition: promotions and SEE>=0 → good bucket
  int goodCount = 0;
  for (int i = 0; i < captEnd; ++i) {
    if (is_promotion(moves[i]) || see_ge(moves[i], 0)) {
      if (i != goodCount) {
        std::swap(moves[i], moves[goodCount]);
        std::swap(scores[i], scores[goodCount]);
      }
      ++goodCount;
    }
  }
  goodCaptEnd = goodCount;
}

void MovePicker::generate_and_score_quiets() {
  Move* quietStart = moves + captEnd;
  Move* endPtr = generate<QUIETS>(board, quietStart);
  quietEnd = int(endPtr - moves);

  int phIdx = pawn_history_index(board.pawn_key());

  for (int i = captEnd; i < quietEnd; ++i) {
    Move m = moves[i];
    Square to = to_sq(m);
    PieceType pt = piece_type(board.piece_on(from_sq(m)));
    int sc = 0;

    if (history)
      sc += (*history)[us][from_sq(m)][to];

    if (pawnHistory)
      sc += (*pawnHistory)[phIdx][pt][to];

    // Continuation history: 1-ply has the highest signal
    if (pt >= PAWN && pt <= KING) {
      if (contHist1)
        sc += 2 * (*contHist1)[pt][to];
      if (contHist2)
        sc += (*contHist2)[pt][to];
      if (contHist4)
        sc += (*contHist4)[pt][to] / 2;
    }

    // Small bonus for moves that give check
    if (board.gives_check(m))
      sc += 8000;

    scores[i] = sc;
  }
}

int MovePicker::score_capture(Move m) const {
  if (is_en_passant(m))
    return MVV_LVA[PAWN][PAWN] + 5000;

  Square from = from_sq(m);
  Square to = to_sq(m);
  PieceType attacker = piece_type(board.piece_on(from));
  PieceType victim = piece_type(board.piece_on(to));

  if (is_promotion(m)) {
    // Queen promotions score highest; under-promotions much lower
    int base = (promo_piece(m) == QUEEN) ? 50000 : 10000;
    if (victim != NO_PIECE_TYPE)
      base += MVV_LVA[victim][attacker];
    return base;
  }

  if (victim == NO_PIECE_TYPE)
    return 0;

  int score = MVV_LVA[victim][attacker];

  if (captureHistory)
    score += (*captureHistory)[us][attacker][to][victim] / 2;

  return score;
}

void MovePicker::select_best(int begin, int end) {
  int bestIdx = begin;
  int bestScore = scores[begin];
  for (int i = begin + 1; i < end; ++i) {
    if (scores[i] > bestScore) {
      bestScore = scores[i];
      bestIdx = i;
    }
  }
  if (bestIdx != begin) {
    std::swap(moves[begin], moves[bestIdx]);
    std::swap(scores[begin], scores[bestIdx]);
  }
}

bool MovePicker::see_ge(Move m, int threshold) const {
  if (is_en_passant(m))
    return threshold <= 0;

  Square from = from_sq(m);
  Square to = to_sq(m);

  int gain = SEE_VALUE[piece_type(board.piece_on(to))];

  if (is_promotion(m))
    gain += SEE_VALUE[promo_piece(m)] - SEE_VALUE[PAWN];

  if (gain < threshold)
    return false;

  int nextVal = is_promotion(m) ? SEE_VALUE[promo_piece(m)]
                                : SEE_VALUE[piece_type(board.piece_on(from))];

  if (gain - nextVal >= threshold)
    return true;

  Bitboard occ = board.pieces() ^ square_bb(from) ^ square_bb(to);
  Color side = ~board.side_to_move();
  Bitboard attackers = (pawn_attacks(WHITE, to) & board.pieces(PAWN, BLACK))
                       | (pawn_attacks(BLACK, to) & board.pieces(PAWN, WHITE))
                       | (knight_attacks(to) & board.pieces(KNIGHT))
                       | (bishop_attacks(to, occ) & board.pieces(BISHOP, QUEEN))
                       | (rook_attacks(to, occ) & board.pieces(ROOK, QUEEN))
                       | (king_attacks(to) & board.pieces(KING));
  attackers &= occ;

  int balance = gain - nextVal - threshold;

  while (true) {
    Bitboard myAtt = attackers & board.pieces(side);
    if (!myAtt)
      break;

    // Find cheapest attacker
    PieceType pt = NO_PIECE_TYPE;
    int minV = INT_MAX;
    for (PieceType p = PAWN; p <= KING; ++p) {
      if (myAtt & board.pieces(p)) {
        minV = SEE_VALUE[p];
        pt = p;
        break;
      }
    }

    Square attSq = lsb_sq(myAtt & board.pieces(pt));
    occ ^= square_bb(attSq);

    // Reveal x-ray attackers
    attackers |= (bishop_attacks(to, occ) & board.pieces(BISHOP, QUEEN));
    attackers |= (rook_attacks(to, occ) & board.pieces(ROOK, QUEEN));
    attackers &= occ;

    balance = -balance - 1 - minV;
    nextVal = minV;
    side = ~side;

    if (balance >= 0) {
      if (pt == KING && (attackers & board.pieces(side)))
        side = ~side;
      break;
    }
  }

  return side != board.side_to_move();
}

} // namespace Catalyst
