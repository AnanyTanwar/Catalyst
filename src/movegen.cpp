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

#include "movegen.h"
#include "bitboard.h"

namespace Catalyst {

template <GenType GT>
static FORCE_INLINE Move* make_promotions(Move* list, Square from, Square to) {
  *list++ = make_move(from, to, MT_PROMOTION, QUEEN);
  if constexpr (GT == QUIETS || GT == ALL_MOVES) {
    *list++ = make_move(from, to, MT_PROMOTION, ROOK);
    *list++ = make_move(from, to, MT_PROMOTION, BISHOP);
    *list++ = make_move(from, to, MT_PROMOTION, KNIGHT);
  }
  return list;
}

// Pawn moves
template <Color Us, GenType GT>
static FORCE_INLINE Move* generate_pawn_moves(const Board& board, Move* list, Bitboard target) {
  constexpr Color Them = ~Us;
  constexpr Direction Up = Us == WHITE ? NORTH : SOUTH;
  constexpr Direction UpLeft = Us == WHITE ? NORTH_WEST : SOUTH_EAST;
  constexpr Direction UpRight = Us == WHITE ? NORTH_EAST : SOUTH_WEST;
  constexpr Bitboard Rank7 = Us == WHITE ? Rank7BB : Rank2BB;
  constexpr Bitboard Rank3 = Us == WHITE ? Rank3BB : Rank6BB;

  Bitboard pawns = board.pieces(PAWN, Us);
  Bitboard enemies = board.pieces(Them);
  Bitboard empty = ~board.pieces();
  Bitboard promoPawns = pawns & Rank7;
  Bitboard nonPromoPawns = pawns & ~Rank7;

  if constexpr (GT == CAPTURES || GT == ALL_MOVES) {
    // Non-promo captures.
    Bitboard leftCap = shift<UpLeft>(nonPromoPawns) & enemies & target;
    Bitboard rightCap = shift<UpRight>(nonPromoPawns) & enemies & target;

    while (leftCap) {
      Square to = pop_lsb(leftCap);
      *list++ = make_move(Square(to - UpLeft), to);
    }
    while (rightCap) {
      Square to = pop_lsb(rightCap);
      *list++ = make_move(Square(to - UpRight), to);
    }

    // Capture-promotions.
    Bitboard promoLeft = shift<UpLeft>(promoPawns) & enemies;
    Bitboard promoRight = shift<UpRight>(promoPawns) & enemies;
    while (promoLeft) {
      Square to = pop_lsb(promoLeft);
      list = make_promotions<GT>(list, Square(to - UpLeft), to);
    }
    while (promoRight) {
      Square to = pop_lsb(promoRight);
      list = make_promotions<GT>(list, Square(to - UpRight), to);
    }

    // En passant — only generate if the ep square or the captured pawn is
    // inside the evasion target mask.
    Square ep = board.ep_square();
    if (ep != SQ_NONE) {
      Square capsq = Us == WHITE ? Square(ep - 8) : Square(ep + 8);
      if ((square_bb(ep) | square_bb(capsq)) & target) {
        Bitboard epPawns = pawn_attacks(Them, ep) & nonPromoPawns;
        while (epPawns) {
          Square from = pop_lsb(epPawns);
          *list++ = make_move(from, ep, MT_EN_PASSANT);
        }
      }
    }
  }

  if constexpr (GT == QUIETS || GT == ALL_MOVES) {
    Bitboard singlePush = shift<Up>(nonPromoPawns) & empty;
    Bitboard doublePush = shift<Up>(singlePush & Rank3) & empty & target;
    singlePush &= target;

    while (singlePush) {
      Square to = pop_lsb(singlePush);
      *list++ = make_move(Square(to - Up), to);
    }
    while (doublePush) {
      Square to = pop_lsb(doublePush);
      *list++ = make_move(Square(to - Up - Up), to);
    }

    // Quiet promotions.
    Bitboard quietPromo = shift<Up>(promoPawns) & empty & target;
    while (quietPromo) {
      Square to = pop_lsb(quietPromo);
      list = make_promotions<GT>(list, Square(to - Up), to);
    }
  }

  return list;
}

template <PieceType Pt, GenType GT>
static FORCE_INLINE Move* generate_piece_moves(const Board& board, Move* list, Color us,
                                               Bitboard target, Bitboard occ) {

  Bitboard pieces = board.pieces(Pt, us);
  Bitboard pinned = board.blockers_for_king(us);
  Square ksq = board.king_square(us);

  while (pieces) {
    Square from = pop_lsb(pieces);

    Bitboard atk = attacks_bb(Pt, from, occ) & target;

    if (pinned & square_bb(from))
      atk &= line_bb(from, ksq);

    while (atk) {
      Square to = pop_lsb(atk);
      *list++ = make_move(from, to);
    }
  }

  return list;
}

template <GenType GT>
static FORCE_INLINE Move* generate_king_moves(const Board& board, Move* list, Color us,
                                              Bitboard target, Bitboard occ) {
  Square from = board.king_square(us);
  Bitboard newOcc = occ ^ square_bb(from); // remove king for X-ray
  Bitboard atk = king_attacks(from) & target & ~board.pieces(KING);

  while (atk) {
    Square to = pop_lsb(atk);
    if (!(board.attackers_to(to, newOcc) & board.pieces(~us)))
      *list++ = make_move(from, to);
  }
  return list;
}

template <Color Us, GenType GT>
static FORCE_INLINE Move* generate_castling(const Board& board, Move* list, Bitboard occ) {
  if constexpr (GT == CAPTURES)
    return list;

  if (board.in_check())
    return list;

  constexpr CastlingRights OO = Us == WHITE ? WHITE_OO : BLACK_OO;
  constexpr CastlingRights OOO = Us == WHITE ? WHITE_OOO : BLACK_OOO;

  for (CastlingRights cr : {OO, OOO}) {
    if (!board.can_castle(cr))
      continue;

    Square kfrom = board.king_square(Us);
    if (kfrom != CASTLING_DATA[cr].kingSrc)
      continue;

    Square kto = CASTLING_DATA[cr].kingDest;
    Square rfrom = CASTLING_DATA[cr].rookSrc;
    Direction d = (kto > kfrom) ? EAST : WEST;

    if (board.piece_on(rfrom) != makePiece(Us, ROOK))
      continue;
    if (between_bb(kfrom, rfrom) & occ)
      continue;

    // Walk the king's path and verify no square is attacked.
    Bitboard noKingOcc = occ ^ square_bb(kfrom);
    bool safe = true;
    for (Square s = kfrom;; s = Square(s + d)) {
      if ((pawn_attacks(Us, s) & board.pieces(PAWN, ~Us))
          | (knight_attacks(s) & board.pieces(KNIGHT, ~Us))
          | (bishop_attacks(s, noKingOcc) & board.pieces(BISHOP, QUEEN, ~Us))
          | (rook_attacks(s, noKingOcc) & board.pieces(ROOK, QUEEN, ~Us))
          | (king_attacks(s) & board.pieces(KING, ~Us))) {
        safe = false;
        break;
      }
      if (s == kto)
        break;
    }

    if (safe)
      *list++ = make_move(kfrom, kto, MT_CASTLING);
  }

  return list;
}

// Evasion generation (in-check positions)
//
// 1. Always generate king escapes.
// 2. If double check, only king moves are legal — return early.
// 3. If single check, generate all moves that block or capture the checker.

template <Color Us>
static FORCE_INLINE Move* generate_evasions(const Board& board, Move* list, Bitboard occ) {
  Square ksq = board.king_square(Us);
  Bitboard checkers = board.checkers();

  // King escapes.
  Bitboard kingTargets = king_attacks(ksq) & ~board.pieces(Us);
  Bitboard temp = kingTargets;
  while (temp) {
    Square to = pop_lsb(temp);
    Bitboard afterKingOcc = (occ ^ square_bb(ksq)) | square_bb(to);
    if (!(board.attackers_to(to, afterKingOcc) & board.pieces(~Us)))
      *list++ = make_move(ksq, to);
  }

  if (more_than_one(checkers))
    return list;

  // Single checker: block or capture.
  Square checker = lsb_sq(checkers);
  Bitboard blockTarget = between_bb(ksq, checker) | checkers;

  list = generate_pawn_moves<Us, ALL_MOVES>(board, list, blockTarget);
  list = generate_piece_moves<KNIGHT, ALL_MOVES>(board, list, Us, blockTarget, occ);
  list = generate_piece_moves<BISHOP, ALL_MOVES>(board, list, Us, blockTarget, occ);
  list = generate_piece_moves<ROOK, ALL_MOVES>(board, list, Us, blockTarget, occ);
  list = generate_piece_moves<QUEEN, ALL_MOVES>(board, list, Us, blockTarget, occ);

  return list;
}

template <Color Us, GenType GT>
static FORCE_INLINE Move* generate_all_for_color(const Board& board, Move* list) {
  Bitboard usBB = board.pieces(Us);
  Bitboard occ = board.pieces();

  if (board.checkers())
    return generate_evasions<Us>(board, list, occ);

  Bitboard target;
  if constexpr (GT == CAPTURES)
    target = board.pieces(~Us) & ~board.pieces(KING);
  else if constexpr (GT == QUIETS)
    target = ~occ;
  else
    target = ~usBB & ~board.pieces(KING);

  // Pawn captures target only enemy pieces; pushes use the computed target.
  Bitboard pawnTarget = (GT == CAPTURES) ? board.pieces(~Us) : target;

  list = generate_pawn_moves<Us, GT>(board, list, pawnTarget);
  list = generate_piece_moves<KNIGHT, GT>(board, list, Us, target, occ);
  list = generate_piece_moves<BISHOP, GT>(board, list, Us, target, occ);
  list = generate_piece_moves<ROOK, GT>(board, list, Us, target, occ);
  list = generate_piece_moves<QUEEN, GT>(board, list, Us, target, occ);
  list = generate_king_moves<GT>(board, list, Us, ~usBB, occ);

  if constexpr (GT == QUIETS || GT == ALL_MOVES)
    list = generate_castling<Us, GT>(board, list, occ);

  return list;
}

template <GenType GT> Move* generate(const Board& board, Move* list) {
  return board.side_to_move() == WHITE ? generate_all_for_color<WHITE, GT>(board, list)
                                       : generate_all_for_color<BLACK, GT>(board, list);
}

template Move* generate<ALL_MOVES>(const Board&, Move*);
template Move* generate<CAPTURES>(const Board&, Move*);
template Move* generate<QUIETS>(const Board&, Move*);

MoveList generate_legal(Board& board) {
  MoveList legal;

  Move pseudoBuf[MAX_MOVES];
  Move* pseudoEnd = generate<ALL_MOVES>(board, pseudoBuf);

  for (Move* it = pseudoBuf; it != pseudoEnd; ++it) {
    if (board.is_legal(*it))
      legal.push(*it);
  }

  return legal;
}

int count_legal(Board& board) {
  return generate_legal(board).size();
}

} // namespace Catalyst
