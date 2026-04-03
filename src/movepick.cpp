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

#include "bitboard.h"
#include "movegen.h"

namespace Catalyst {

// ---------------------------------------------------------------------------
// MVV table — indexed [victim][attacker].
// captHist acts as the tiebreaker within victim tiers (see CAPT_HIST_DIVISOR).
// ---------------------------------------------------------------------------
// clang-format off
static constexpr int MVV[PIECE_TYPE_NB][PIECE_TYPE_NB] = {
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
    0,
    100,
    300,
    300,
    500,
    900,
    0,
};

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

MovePicker::MovePicker(const Board &b,
    Move                            ttM,
    int                             p,
    Move                            k1,
    Move                            k2,
    Move                            cnt,
    const ButterflyHistory         &hist,
    const CaptureHistory           &captHist,
    const PawnHistory              &pawnHist,
    const ContinuationHistory      *ch1,
    const ContinuationHistory      *ch2,
    const ContinuationHistory      *ch4,
    MoveBuffer                     &buf)
    : board(b)
    , stage(STAGE_TT)
    , ttMove(ttM)
    , ply(p)
    , us(b.side_to_move())
    , killer1(k1)
    , killer2(k2)
    , counter(cnt)
    , history(&hist)
    , captureHistory(&captHist)
    , pawnHistory(&pawnHist)
    , contHist1(ch1)
    , contHist2(ch2)
    , contHist4(ch4)
    , moves(buf.moves)
    , scores(buf.scores)
    , cur(0)
    , goodCaptEnd(0)
    , captEnd(0)
    , quietEnd(0)
    , badCaptCur(0)
    , seeThreshold(SEE_CAPTURE_THRESHOLD)
    , qsearchMode(false) {
    if (ttMove != MOVE_NONE
        && (board.piece_on(from_sq(ttMove)) == NO_PIECE
            || piece_color(board.piece_on(from_sq(ttMove))) != board.side_to_move()
            || !board.is_pseudo_legal(ttMove) || !board.is_legal(ttMove)))
        ttMove = MOVE_NONE;
}

MovePicker::MovePicker(const Board &b,
    Move                            ttM,
    int                             threshold,
    bool                            qsOnly,
    const CaptureHistory           &captHist,
    MoveBuffer                     &buf)
    : board(b)
    , stage(STAGE_TT)
    , ttMove(ttM)
    , ply(0)
    , us(b.side_to_move())
    , killer1(MOVE_NONE)
    , killer2(MOVE_NONE)
    , counter(MOVE_NONE)
    , history(nullptr)
    , captureHistory(&captHist)
    , pawnHistory(nullptr)
    , contHist1(nullptr)
    , contHist2(nullptr)
    , contHist4(nullptr)
    , moves(buf.moves)
    , scores(buf.scores)
    , cur(0)
    , goodCaptEnd(0)
    , captEnd(0)
    , quietEnd(0)
    , badCaptCur(0)
    , seeThreshold(threshold)
    , qsearchMode(true) {
    if (ttMove != MOVE_NONE
        && (board.piece_on(from_sq(ttMove)) == NO_PIECE
            || piece_color(board.piece_on(from_sq(ttMove))) != board.side_to_move()
            || !board.is_pseudo_legal(ttMove) || !board.is_legal(ttMove)))
        ttMove = MOVE_NONE;
    if (qsOnly && ttMove != MOVE_NONE && !board.is_capture_or_promotion(ttMove))
        ttMove = MOVE_NONE;
}

// ---------------------------------------------------------------------------
// next_move — main dispatch loop
// ---------------------------------------------------------------------------

Move MovePicker::next_move() {
    while (true)
    {
        switch (stage)
        {

        // ── TT move ──────────────────────────────────────────────────────
        case STAGE_TT:
            stage = STAGE_INIT_CAPTURES;
            if (ttMove != MOVE_NONE)
                return ttMove;
            break;

        // ── Generate + score all captures ────────────────────────────────
        case STAGE_INIT_CAPTURES:
            generate_and_score_captures();
            stage = STAGE_GOOD_CAPTURES;
            cur   = 0;
            break;

        // ── Good captures ─────────────────────────────────────────────────
        // Bucket [0, goodCaptEnd) was partitioned by see_ge(m, -score/18).
        // In qsearch we additionally gate on seeThreshold here.
        case STAGE_GOOD_CAPTURES:
            while (cur < goodCaptEnd)
            {
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

        // ── Killer 1 ─────────────────────────────────────────────────────
        case STAGE_KILLERS:
            stage = STAGE_KILLER2;
            if (killer1 != MOVE_NONE && killer1 != ttMove && !board.is_capture(killer1)
                && board.is_pseudo_legal(killer1))
                return killer1;
            break;

        // ── Killer 2 ─────────────────────────────────────────────────────
        case STAGE_KILLER2:
            stage = STAGE_COUNTERS;
            if (killer2 != MOVE_NONE && killer2 != ttMove && killer2 != killer1
                && !board.is_capture(killer2) && board.is_pseudo_legal(killer2))
                return killer2;
            break;

        // ── Counter move ─────────────────────────────────────────────────
        case STAGE_COUNTERS:
            stage = STAGE_INIT_QUIETS;
            if (counter != MOVE_NONE && counter != ttMove && counter != killer1
                && counter != killer2 && !board.is_capture(counter)
                && board.is_pseudo_legal(counter))
                return counter;
            break;

        // ── Generate + score all quiets ───────────────────────────────────
        case STAGE_INIT_QUIETS:
            generate_and_score_quiets();
            stage = STAGE_QUIETS;
            cur   = captEnd;
            break;

        // ── Quiets ────────────────────────────────────────────────────────
        // History-based quiet pruning: if search has set quietThreshold_,
        // moves scoring below that value are skipped here — before the
        // select_best call — so we never pay the sorting cost for them.
        // This tightens LMR by ensuring only history-trusted moves survive.
        //
        // Implementation: select_best finds the best remaining move. If it
        // already scores below the threshold the entire remaining list does
        // too (scores are descending after each select_best), so we can break.
        case STAGE_QUIETS:
            while (cur < quietEnd)
            {
                select_best(cur, quietEnd);
                // After select_best, moves[cur] is the highest-scoring quiet.
                // If it is below threshold, everything after it is worse — stop.
                if (quietThreshold_ != QUIET_PRUNE_DISABLED && scores[cur] < quietThreshold_)
                    break;

                Move m = moves[cur++];
                if (m == ttMove || m == killer1 || m == killer2 || m == counter)
                    continue;
                if (board.is_capture(m))
                    continue;
                return m;
            }
            stage      = STAGE_BAD_CAPTURES;
            badCaptCur = goodCaptEnd;
            break;

        // ── Bad captures ──────────────────────────────────────────────────
        // Bucket [goodCaptEnd, captEnd) contains SEE-failing captures.
        // They are scored by their SEE loss (least losing first) so that
        // select_best returns them in order of decreasing material exchange.
        // No second SEE pass here — the partition already sorted them out.
        case STAGE_BAD_CAPTURES:
            while (badCaptCur < captEnd)
            {
                select_best(badCaptCur, captEnd);
                Move m = moves[badCaptCur++];
                if (m == ttMove)
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

// ---------------------------------------------------------------------------
// score_capture
//
// Score = MVV[victim][attacker] + captHist / CAPT_HIST_DIVISOR
//
// captHist acts as a tiebreaker within the same victim tier.
// CAPT_HIST_DIVISOR = 16 (tunable). Old value was 2, which let captHist
// dominate MVV by ~8x, causing bad captures to beat good ones in ordering.
// ---------------------------------------------------------------------------

int MovePicker::score_capture(Move m) const {
    if (is_en_passant(m))
        return MVV[PAWN][PAWN] + 5000;

    Square    from     = from_sq(m);
    Square    to       = to_sq(m);
    PieceType attacker = piece_type(board.piece_on(from));
    PieceType victim   = piece_type(board.piece_on(to));

    if (is_promotion(m))
    {
        int base = (promo_piece(m) == QUEEN) ? 50000 : 10000;
        if (victim != NO_PIECE_TYPE)
            base += MVV[victim][attacker];
        return base;
    }

    if (victim == NO_PIECE_TYPE)
        return 0;

    int score = MVV[victim][attacker];

    if (captureHistory)
        score += (*captureHistory)[us][attacker][to][victim] / CAPT_HIST_DIVISOR;

    return score;
}

// ---------------------------------------------------------------------------
// generate_and_score_captures
//
// After scoring, moves are partitioned into two buckets:
//   Good [0, goodCaptEnd)    : see_ge(m, -score / GOOD_CAP_SEE_DIVISOR)
//   Bad  [goodCaptEnd, captEnd) : everything else
//
// Bad captures are scored separately by their SEE loss value (negated) so
// select_best during STAGE_BAD_CAPTURES returns least-losing captures first.
// This replaces the old "return in partition order" approach which was
// effectively random ordering within bad captures.
// ---------------------------------------------------------------------------

void MovePicker::generate_and_score_captures() {
    Move *endPtr = generate<CAPTURES>(board, moves);
    captEnd      = int(endPtr - moves);

    for (int i = 0; i < captEnd; ++i)
        scores[i] = score_capture(moves[i]);

    // Partition into good / bad buckets using dynamic SEE threshold.
    // -score / GOOD_CAP_SEE_DIVISOR: high-MVV captures get a lenient bar,
    // low-MVV captures need to pass a stricter SEE. (Stockfish pattern.)
    int goodCount = 0;
    for (int i = 0; i < captEnd; ++i)
    {
        if (is_promotion(moves[i]) || see_ge(moves[i], -scores[i] / GOOD_CAP_SEE_DIVISOR))
        {
            if (i != goodCount)
            {
                std::swap(moves[i], moves[goodCount]);
                std::swap(scores[i], scores[goodCount]);
            }
            ++goodCount;
        }
    }
    goodCaptEnd = goodCount;

    // Re-score bad captures by SEE loss so STAGE_BAD_CAPTURES can order them
    // least-losing first via select_best. We encode loss as a negative number:
    // a capture that loses 300cp gets score = -300, losing 100cp gets -100.
    // select_best picks the highest score, so -100 comes before -300 — correct.
    // We use a coarse SEE value estimate: capturedPiece - movingPiece.
    for (int i = goodCaptEnd; i < captEnd; ++i)
    {
        Move      m        = moves[i];
        PieceType attacker = piece_type(board.piece_on(from_sq(m)));
        PieceType victim   = piece_type(board.piece_on(to_sq(m)));

        // Coarse SEE loss: how much material we expect to lose.
        // Least-losing = highest score (e.g. rook-for-pawn = -500+100 = -400
        // is better than queen-for-pawn = -900+100 = -800).
        int seeLoss = SEE_VALUE[victim] - SEE_VALUE[attacker];
        scores[i]   = seeLoss;  // negative for losing captures, 0 for equal
    }
}

// ---------------------------------------------------------------------------
// generate_and_score_quiets
//
// Key changes vs old version:
//   1. contHist weights corrected: ch2 ×2 (was ×1), ch4 ×1 (was /2).
//   2. Removed flat gives_check +8000 (context-free, harmful on average).
//   3. Threatened-square adjustment (Stockfish + Berserk pattern):
//        +THREAT_ESCAPE_BONUS   for moving FROM a lesser-threatened square
//        -THREAT_STEPIN_PENALTY for moving TO a lesser-threatened square
//      Threat maps are built once via bitboard iteration over enemy pieces.
// ---------------------------------------------------------------------------

void MovePicker::generate_and_score_quiets() {
    Move *quietStart = moves + captEnd;
    Move *endPtr     = generate<QUIETS>(board, quietStart);
    quietEnd         = int(endPtr - moves);

    int phIdx = pawn_history_index(board.pawn_key());

    // ── Build threat maps (once, before the scoring loop) ─────────────────
    // Aggregate attack bitboards of each enemy piece type.
    // We need three tiers matching Stockfish's threatByLesser[pt]:
    //   pawnThreat  = squares attacked by enemy pawns
    //   minorThreat = pawnThreat ∪ squares attacked by enemy knights/bishops
    //   rookThreat  = minorThreat ∪ squares attacked by enemy rooks
    //
    // A piece of type pt is "escape-worthy" if it sits on threat[pt]:
    //   KNIGHT/BISHOP → threatened by pawnThreat
    //   ROOK          → threatened by minorThreat
    //   QUEEN         → threatened by rookThreat
    // Pawns and kings excluded: pawn structure is handled by pawnHistory,
    // king safety is handled in search.

    const Bitboard occ  = board.pieces();
    const Color    them = ~us;

    // Pawn threats: all squares enemy pawns attack
    Bitboard pawnThreat = 0;
    {
        Bitboard pawns = board.pieces(PAWN, them);
        while (pawns)
        {
            Square s = pop_lsb(pawns);
            pawnThreat |= pawn_attacks(them, s);
        }
    }

    // Minor threats: pawnThreat + knight + bishop attacks
    Bitboard minorThreat = pawnThreat;
    {
        Bitboard knights = board.pieces(KNIGHT, them);
        while (knights)
        {
            Square s = pop_lsb(knights);
            minorThreat |= knight_attacks(s);
        }
        Bitboard bishops = board.pieces(BISHOP, them);
        while (bishops)
        {
            Square s = pop_lsb(bishops);
            minorThreat |= bishop_attacks(s, occ);
        }
    }

    // Rook threats: minorThreat + rook attacks
    Bitboard rookThreat = minorThreat;
    {
        Bitboard rooks = board.pieces(ROOK, them);
        while (rooks)
        {
            Square s = pop_lsb(rooks);
            rookThreat |= rook_attacks(s, occ);
        }
    }

    // ── Score each quiet move ─────────────────────────────────────────────
    for (int i = captEnd; i < quietEnd; ++i)
    {
        Move      m    = moves[i];
        Square    from = from_sq(m);
        Square    to   = to_sq(m);
        PieceType pt   = piece_type(board.piece_on(from));
        int       sc   = 0;

        // Butterfly history
        if (history)
            sc += (*history)[us][from][to];

        // Pawn structure history
        if (pawnHistory)
            sc += (*pawnHistory)[phIdx][pt][to];

        // Continuation histories — corrected weights:
        //   ch1 (1-ply back): ×CONT_HIST1_WEIGHT = 2 (unchanged, highest signal)
        //   ch2 (2-ply back): ×CONT_HIST2_WEIGHT = 2 (was 1 — under-weighted)
        //   ch4 (4-ply back): ×CONT_HIST4_WEIGHT = 1 (was /2 — under-weighted)
        if (pt >= PAWN && pt <= KING)
        {
            if (contHist1)
                sc += CONT_HIST1_WEIGHT * (*contHist1)[pt][to];
            if (contHist2)
                sc += CONT_HIST2_WEIGHT * (*contHist2)[pt][to];
            if (contHist4)
                sc += CONT_HIST4_WEIGHT * (*contHist4)[pt][to];
        }

        // ── Threatened-square adjustment ─────────────────────────────────
        // For non-pawn, non-king pieces only.
        // The threat tier for a piece = smallest enemy piece that can attack it:
        //   KNIGHT/BISHOP → pawn threats
        //   ROOK          → minor threats (pawn + knight + bishop)
        //   QUEEN         → rook threats (minor + rook)
        //
        // +THREAT_ESCAPE_BONUS   if moving FROM a threatened square (hang!),
        // -THREAT_STEPIN_PENALTY if moving TO   a threatened square (bad!).
        if (pt != PAWN && pt != KING)
        {
            const Bitboard threat = (pt == QUEEN) ? rookThreat
                : (pt == ROOK)                    ? minorThreat
                                                  : pawnThreat;

            if (square_bb(from) & threat)
                sc += THREAT_ESCAPE_BONUS;
            if (square_bb(to) & threat)
                sc -= THREAT_STEPIN_PENALTY;
        }

        scores[i] = sc;
    }
}

// ---------------------------------------------------------------------------
// select_best — partial selection sort, one step
// Swaps the highest-scoring element in [begin, end) to position begin.
// ---------------------------------------------------------------------------

void MovePicker::select_best(int begin, int end) {
    int bestIdx   = begin;
    int bestScore = scores[begin];
    for (int i = begin + 1; i < end; ++i)
    {
        if (scores[i] > bestScore)
        {
            bestScore = scores[i];
            bestIdx   = i;
        }
    }
    if (bestIdx != begin)
    {
        std::swap(moves[begin], moves[bestIdx]);
        std::swap(scores[begin], scores[bestIdx]);
    }
}

// ---------------------------------------------------------------------------
// see_ge — Static Exchange Evaluation
//
// Returns true if the SEE value of move m >= threshold.
//
// Cheapest-attacker lookup uses an if-else chain on typed piece bitboards
// instead of the old O(N) PieceType loop — no INT_MAX scan, one lsb_sq call.
// ---------------------------------------------------------------------------

bool MovePicker::see_ge(Move m, int threshold) const {
    if (is_en_passant(m))
        return threshold <= 0;

    Square from = from_sq(m);
    Square to   = to_sq(m);

    int gain = SEE_VALUE[piece_type(board.piece_on(to))];

    if (is_promotion(m))
        gain += SEE_VALUE[promo_piece(m)] - SEE_VALUE[PAWN];

    if (gain < threshold)
        return false;

    int nextVal
        = is_promotion(m) ? SEE_VALUE[promo_piece(m)] : SEE_VALUE[piece_type(board.piece_on(from))];

    if (gain - nextVal >= threshold)
        return true;

    Bitboard occ       = board.pieces() ^ square_bb(from) ^ square_bb(to);
    Color    side      = ~board.side_to_move();
    Bitboard attackers = (pawn_attacks(WHITE, to) & board.pieces(PAWN, BLACK))
        | (pawn_attacks(BLACK, to) & board.pieces(PAWN, WHITE))
        | (knight_attacks(to) & board.pieces(KNIGHT))
        | (bishop_attacks(to, occ) & board.pieces(BISHOP, QUEEN))
        | (rook_attacks(to, occ) & board.pieces(ROOK, QUEEN))
        | (king_attacks(to) & board.pieces(KING));
    attackers &= occ;

    int balance = gain - nextVal - threshold;

    while (true)
    {
        Bitboard myAtt = attackers & board.pieces(side);
        if (!myAtt)
            break;

        // Find cheapest attacker — if-else on typed bitboards.
        // No loop, no sentinel value. One lsb_sq on the winning branch.
        PieceType pt;
        Square    attSq;

        if (myAtt & board.pieces(PAWN))
        {
            pt    = PAWN;
            attSq = lsb_sq(myAtt & board.pieces(PAWN));
        }
        else if (myAtt & board.pieces(KNIGHT))
        {
            pt    = KNIGHT;
            attSq = lsb_sq(myAtt & board.pieces(KNIGHT));
        }
        else if (myAtt & board.pieces(BISHOP))
        {
            pt    = BISHOP;
            attSq = lsb_sq(myAtt & board.pieces(BISHOP));
        }
        else if (myAtt & board.pieces(ROOK))
        {
            pt    = ROOK;
            attSq = lsb_sq(myAtt & board.pieces(ROOK));
        }
        else if (myAtt & board.pieces(QUEEN))
        {
            pt    = QUEEN;
            attSq = lsb_sq(myAtt & board.pieces(QUEEN));
        }
        else
        {
            pt    = KING;
            attSq = lsb_sq(myAtt & board.pieces(KING));
        }

        int minV = SEE_VALUE[pt];

        occ ^= square_bb(attSq);

        // Reveal x-ray attackers after removing the piece
        attackers |= (bishop_attacks(to, occ) & board.pieces(BISHOP, QUEEN));
        attackers |= (rook_attacks(to, occ) & board.pieces(ROOK, QUEEN));
        attackers &= occ;

        balance = -balance - 1 - minV;
        side    = ~side;

        if (balance >= 0)
        {
            if (pt == KING && (attackers & board.pieces(side)))
                side = ~side;
            break;
        }
    }

    return side != board.side_to_move();
}

}  // namespace Catalyst
