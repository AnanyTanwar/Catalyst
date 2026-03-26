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

#include "board.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <sstream>

#include "bitboard.h"

namespace Catalyst {

namespace Zobrist {
Key psq[PIECE_NB][SQUARE_NB];
Key enpassant[FILE_NB];
Key castling[CASTLING_RIGHTS_NB];
Key side;

static uint64_t randomSeed = 0xF12563D471FCE219ULL;

static uint64_t random64() {
  uint64_t z = (randomSeed += 0x9e3779b97f4a7c15ULL);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

void init() {
  randomSeed = 0xF12563D471FCE219ULL;

  for (Piece pc = NO_PIECE; pc < PIECE_NB; ++pc)
    for (Square sq = SQ_A1; sq < SQUARE_NB; ++sq)
      psq[pc][sq] = random64();

  for (File f = FILE_A; f < FILE_NB; ++f)
    enpassant[f] = random64();

  for (int cr = 0; cr < CASTLING_RIGHTS_NB; ++cr)
    castling[cr] = random64();

  side = random64();
}
} // namespace Zobrist

Board::Board() : historyLen(0), sideToMove(WHITE), st(&startState), gamePly(0) {
  clear();
}

void Board::clear() {
  std::memset(byTypeBB, 0, sizeof(byTypeBB));
  std::memset(byColorBB, 0, sizeof(byColorBB));
  std::memset(board, 0, sizeof(board));
  std::memset(&startState, 0, sizeof(startState));
  std::memset(castlingPath, 0, sizeof(castlingPath));
  std::memset(castlingRookSquare, 0, sizeof(castlingRookSquare));
  std::memset(positionHistory, 0, sizeof(positionHistory));

  sideToMove = WHITE;
  st = &startState;
  gamePly = 0;
  historyLen = 0;
}

void Board::set_fen(const std::string& fen) {
  clear();

  std::istringstream iss(fen);
  std::string piecePlacement, side, castling, enpassant;
  int halfmove = 0, fullmove = 1;

  iss >> piecePlacement >> side >> castling >> enpassant;
  if (!(iss >> halfmove))
    halfmove = 0;
  if (!(iss >> fullmove))
    fullmove = 1;

  // clang-format off
  static constexpr PieceType charToPt[26] = {
    NO_PIECE_TYPE, BISHOP, NO_PIECE_TYPE, NO_PIECE_TYPE, NO_PIECE_TYPE, NO_PIECE_TYPE,
    NO_PIECE_TYPE, NO_PIECE_TYPE, NO_PIECE_TYPE, NO_PIECE_TYPE, KING, NO_PIECE_TYPE,
    NO_PIECE_TYPE, KNIGHT, NO_PIECE_TYPE, PAWN, QUEEN, ROOK,
    NO_PIECE_TYPE, NO_PIECE_TYPE, NO_PIECE_TYPE, NO_PIECE_TYPE, NO_PIECE_TYPE,
    NO_PIECE_TYPE, NO_PIECE_TYPE, NO_PIECE_TYPE
  };
  // clang-format on

  Square sq = SQ_A8;
  for (char c : piecePlacement) {
    if (c == '/')
      sq = Square(sq - 16);
    else if (isdigit(c))
      sq = Square(sq + (c - '0'));
    else {
      Color col = isupper(c) ? WHITE : BLACK;
      PieceType pt = charToPt[tolower(c) - 'a'];
      if (pt != NO_PIECE_TYPE) {
        put_piece(makePiece(col, pt), sq);
        ++sq;
      }
    }
  }

  sideToMove = (side == "w") ? WHITE : BLACK;
  if (sideToMove == BLACK)
    st->key ^= Zobrist::side;

  st->castlingRights = NO_CASTLING;
  for (char c : castling) {
    if (c == 'K')
      st->castlingRights |= WHITE_OO;
    else if (c == 'Q')
      st->castlingRights |= WHITE_OOO;
    else if (c == 'k')
      st->castlingRights |= BLACK_OO;
    else if (c == 'q')
      st->castlingRights |= BLACK_OOO;
  }

  st->epSquare = (enpassant == "-") ? SQ_NONE : string_to_square(enpassant);
  st->rule50 = halfmove;
  gamePly = (fullmove - 1) * 2 + (sideToMove == BLACK ? 1 : 0);

  set_state(st);

  for (int cr = 0; cr < CASTLING_RIGHTS_NB; ++cr) {
    if (CASTLING_DATA[cr].kingSrc != SQ_NONE) {
      castlingPath[cr] = CASTLING_PATH[cr];
      castlingRookSquare[cr] = CASTLING_DATA[cr].rookSrc;
    }
  }
}

std::string Board::get_fen() const {
  std::ostringstream oss;

  for (Rank r = RANK_8; r >= RANK_1; --r) {
    int emptyCount = 0;
    for (File f = FILE_A; f < FILE_NB; ++f) {
      Square sq = makeSquare(f, r);
      Piece pc = piece_on(sq);
      if (pc == NO_PIECE) {
        ++emptyCount;
      } else {
        if (emptyCount > 0) {
          oss << emptyCount;
          emptyCount = 0;
        }
        oss << " PNBRQK  pnbrqk"[pc];
      }
    }
    if (emptyCount > 0)
      oss << emptyCount;
    if (r > RANK_1)
      oss << '/';
  }

  oss << ' ' << (sideToMove == WHITE ? 'w' : 'b') << ' ';

  if (st->castlingRights == NO_CASTLING)
    oss << '-';
  else {
    if (can_castle(WHITE_OO))
      oss << 'K';
    if (can_castle(WHITE_OOO))
      oss << 'Q';
    if (can_castle(BLACK_OO))
      oss << 'k';
    if (can_castle(BLACK_OOO))
      oss << 'q';
  }

  oss << ' ' << (st->epSquare == SQ_NONE ? "-" : square_to_string(st->epSquare));
  oss << ' ' << st->rule50 << ' ' << (gamePly / 2 + 1);
  return oss.str();
}

void Board::set_startpos() {
  set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

void Board::copy_from(const Board& other) {
  std::memcpy(byTypeBB, other.byTypeBB, sizeof(byTypeBB));
  std::memcpy(byColorBB, other.byColorBB, sizeof(byColorBB));
  std::memcpy(board, other.board, sizeof(board));

  sideToMove = other.sideToMove;
  gamePly = other.gamePly;

  startState = other.startState;
  st = &startState;
  st->previous = nullptr;

  std::memcpy(castlingPath, other.castlingPath, sizeof(castlingPath));
  std::memcpy(castlingRookSquare, other.castlingRookSquare, sizeof(castlingRookSquare));

  historyLen = other.historyLen;
  std::memcpy(positionHistory, other.positionHistory, size_t(other.historyLen) * sizeof(Key));
}

void Board::put_piece(Piece pc, Square sq) {
  assert(pc != NO_PIECE);
  assert(sq < SQUARE_NB);
  board[sq] = pc;
  byTypeBB[piece_type(pc)] |= square_bb(sq);
  byTypeBB[ALL_PIECES] |= square_bb(sq);
  byColorBB[piece_color(pc)] |= square_bb(sq);
}

void Board::remove_piece(Square sq) {
  Piece pc = board[sq];
  assert(pc != NO_PIECE);
  byTypeBB[piece_type(pc)] &= ~square_bb(sq);
  byTypeBB[ALL_PIECES] &= ~square_bb(sq);
  byColorBB[piece_color(pc)] &= ~square_bb(sq);
  board[sq] = NO_PIECE;
}

void Board::move_piece(Square from, Square to) {
  Piece pc = board[from];
  if (pc == NO_PIECE || board[to] != NO_PIECE || from == to) {
    std::cerr << "FATAL: move_piece! from=" << square_to_string(from)
              << " to=" << square_to_string(to) << " pc=" << int(pc)
              << " displaced=" << int(board[to]) << "\n";
    std::abort();
  }
  Bitboard fromToBB = square_bb(from) | square_bb(to);
  byTypeBB[piece_type(pc)] ^= fromToBB;
  byTypeBB[ALL_PIECES] ^= fromToBB;
  byColorBB[piece_color(pc)] ^= fromToBB;
  board[to] = pc;
  board[from] = NO_PIECE;
}

void Board::set_state(StateInfo* si) {
  si->key = compute_key<false>();
  si->pawnKey = compute_pawn_key();
  si->checkersBB = attackers_to(king_square(sideToMove)) & pieces(~sideToMove);
  update_blockers(si);
}

void Board::update_state(StateInfo* si) {
  si->key ^= Zobrist::side;
  if (si->epSquare != SQ_NONE)
    si->key ^= Zobrist::enpassant[fileOf(si->epSquare)];
  si->checkersBB = attackers_to(king_square(sideToMove)) & pieces(~sideToMove);
  update_blockers(si);
}

void Board::update_blockers(StateInfo* si) {
  if (!pieces(KING, WHITE) || !pieces(KING, BLACK)) {
    std::cerr << "FATAL: king missing in update_blockers!\n";
    std::abort();
  }
  for (Color c : {WHITE, BLACK}) {
    Square ksq = king_square(c);
    Bitboard pinners = 0;
    Bitboard blockers = 0;

    Bitboard snipers = ((PseudoAttacks[ROOK][ksq] & pieces(QUEEN, ROOK))
                        | (PseudoAttacks[BISHOP][ksq] & pieces(QUEEN, BISHOP)))
                       & pieces(~c);

    while (snipers) {
      Square sniperSq = pop_lsb(snipers);
      Bitboard b = between_bb(ksq, sniperSq) & pieces();

      if (b && !more_than_one(b)) {
        blockers |= b;
        if (b & pieces(c))
          pinners |= square_bb(sniperSq);
      }
    }

    si->blockersForKing[c] = blockers;
    si->pinners[c] = pinners;
  }
}

Key Board::compute_pawn_key() const {
  Key k = 0;
  Bitboard pawns = pieces(PAWN);
  while (pawns) {
    Square sq = pop_lsb(pawns);
    k ^= Zobrist::psq[piece_on(sq)][sq];
  }
  return k;
}

template <bool AfterMove> Key Board::compute_key() const {
  Key k = 0;
  Bitboard occ = pieces();
  while (occ) {
    Square sq = pop_lsb(occ);
    k ^= Zobrist::psq[piece_on(sq)][sq];
  }
  k ^= Zobrist::castling[st->castlingRights];
  if (st->epSquare != SQ_NONE)
    k ^= Zobrist::enpassant[fileOf(st->epSquare)];
  if (sideToMove == BLACK)
    k ^= Zobrist::side;
  return k;
}

template Key Board::compute_key<false>() const;
template Key Board::compute_key<true>() const;

void Board::make_move(Move m, StateInfo& newSt) {

  if (!pieces(KING, WHITE) || !pieces(KING, BLACK)) {
    std::cerr << "FATAL: make_move called with missing king! move=" << move_to_uci(m)
              << " fen=" << get_fen() << "\n";
    std::abort();
  }

  Square from = from_sq(m);
  Square to = to_sq(m);
  if (from == to) {
    std::cerr << "FATAL: make_move from==to! move=" << move_to_uci(m) << " fen=" << get_fen()
              << "\n";
    std::abort();
  }
  newSt = *st;
  newSt.previous = st;
  st = &newSt;

  ++gamePly;
  st->pliesFromNull++;

  Color us = sideToMove;
  Color them = ~us;
  Piece pc = piece_on(from);
  PieceType pt = piece_type(pc);

  assert(pc != NO_PIECE);
  assert(piece_color(pc) == us);

  // XOR out old castling rights and EP square before modifying state.
  st->key ^= Zobrist::castling[st->castlingRights];
  if (st->epSquare != SQ_NONE)
    st->key ^= Zobrist::enpassant[fileOf(st->epSquare)];
  st->epSquare = SQ_NONE;

  if (is_castling(m)) {
    CastlingRights cr = (us == WHITE) ? (to > from ? WHITE_OO : WHITE_OOO)
                                      : (to > from ? BLACK_OO : BLACK_OOO);
    Square rfrom = CASTLING_DATA[cr].rookSrc;
    Square rto = CASTLING_DATA[cr].rookDest;
    Piece rook = makePiece(us, ROOK);
    Square kto = CASTLING_DATA[cr].kingDest;

    move_piece(from, kto);
    st->key ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][kto];
    move_piece(rfrom, rto);
    st->key ^= Zobrist::psq[rook][rfrom] ^ Zobrist::psq[rook][rto];

    st->capturedPiece = NO_PIECE;
    st->rule50++;

  } else if (is_en_passant(m)) {
    Square capsq = (us == WHITE) ? Square(to - 8) : Square(to + 8);
    st->capturedPiece = piece_on(capsq);
    assert(st->capturedPiece != NO_PIECE);
    assert(piece_type(st->capturedPiece) != KING);

    remove_piece(capsq);

    st->key ^= Zobrist::psq[st->capturedPiece][capsq];
    move_piece(from, to);
    st->key ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to];

    st->rule50 = 0;
    st->pawnKey = compute_pawn_key();

  } else if (is_promotion(m)) {
    if (piece_on(to) != NO_PIECE) {
      st->capturedPiece = piece_on(to);
      if (piece_type(st->capturedPiece) == KING) {
        std::cerr << "FATAL: make_move capturing king! move=" << move_to_uci(m)
                  << " fen=" << get_fen() << "\n";
        std::abort();
      }
      remove_piece(to);
      st->key ^= Zobrist::psq[st->capturedPiece][to];
    } else {
      st->capturedPiece = NO_PIECE;
    }

    remove_piece(from);
    st->key ^= Zobrist::psq[pc][from];

    Piece promoPiece = makePiece(us, promo_piece(m));
    put_piece(promoPiece, to);
    st->key ^= Zobrist::psq[promoPiece][to];

    st->rule50 = 0;
    st->pawnKey = compute_pawn_key();

  } else {
    if (piece_on(to) != NO_PIECE) {
      st->capturedPiece = piece_on(to);
      if (piece_type(st->capturedPiece) == KING) {
        std::cerr << "FATAL: make_move capturing king! move=" << move_to_uci(m)
                  << " fen=" << get_fen() << "\n";
        std::abort();
      }
      remove_piece(to);
      st->key ^= Zobrist::psq[st->capturedPiece][to];
    } else {
      st->capturedPiece = NO_PIECE;
    }

    move_piece(from, to);
    st->key ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to];

    if (pt == PAWN) {
      st->rule50 = 0;
      if (distance(from, to) == 2) {
        Square ep = Square((int(from) + int(to)) / 2);
        if (pawn_attacks(us, ep) & pieces(PAWN, them)) {
          st->epSquare = ep;
          st->key ^= Zobrist::enpassant[fileOf(ep)];
        }
      }
      st->pawnKey = compute_pawn_key();
    } else if (st->capturedPiece != NO_PIECE) {
      st->rule50 = 0;
      if (piece_type(st->capturedPiece) == PAWN)
        st->pawnKey = compute_pawn_key();
    } else {
      st->rule50++;
    }
  }

  st->castlingRights &= CASTLING_RIGHTS_MASK[from];
  st->castlingRights &= CASTLING_RIGHTS_MASK[to];
  st->key ^= Zobrist::castling[st->castlingRights];

  sideToMove = them;
  st->key ^= Zobrist::side;
  st->checkersBB = attackers_to(king_square(them)) & pieces(us);
  update_blockers(st);

  add_to_history(st->key);
}

void Board::unmake_move(Move m) {
  remove_from_history();

  sideToMove = ~sideToMove;
  Color us = sideToMove;
  Square from = from_sq(m);
  Square to = to_sq(m);

  if (is_castling(m)) {
    CastlingRights cr = (us == WHITE) ? (to > from ? WHITE_OO : WHITE_OOO)
                                      : (to > from ? BLACK_OO : BLACK_OOO);
    Square kto = CASTLING_DATA[cr].kingDest;
    Square rfrom = CASTLING_DATA[cr].rookSrc;
    Square rto = CASTLING_DATA[cr].rookDest;
    remove_piece(kto);
    put_piece(makePiece(us, KING), from);
    remove_piece(rto);
    put_piece(makePiece(us, ROOK), rfrom);

  } else if (is_en_passant(m)) {
    Square capsq = (us == WHITE) ? Square(to - 8) : Square(to + 8);
    move_piece(to, from);
    put_piece(st->capturedPiece, capsq);

  } else if (is_promotion(m)) {
    remove_piece(to);
    put_piece(makePiece(us, PAWN), from);
    if (st->capturedPiece != NO_PIECE)
      put_piece(st->capturedPiece, to);

  } else {
    move_piece(to, from);
    if (st->capturedPiece != NO_PIECE)
      put_piece(st->capturedPiece, to);
  }

  st = st->previous;
  --gamePly;
}

void Board::make_null_move(StateInfo& newSt) {
  newSt = *st;
  newSt.previous = st;
  newSt.capturedPiece = NO_PIECE;
  st = &newSt;

  ++gamePly;
  st->pliesFromNull = 0;

  if (st->epSquare != SQ_NONE) {
    st->key ^= Zobrist::enpassant[fileOf(st->epSquare)];
    st->epSquare = SQ_NONE;
  }

  sideToMove = ~sideToMove;
  st->key ^= Zobrist::side;
  st->rule50++;

  st->checkersBB = 0;
  update_blockers(st);

  add_to_history(st->key);
}

void Board::unmake_null_move() {
  remove_from_history();
  st = st->previous;
  sideToMove = ~sideToMove;
  --gamePly;
}

Bitboard Board::attackers_to(Square sq) const {
  return attackers_to(sq, pieces());
}

Bitboard Board::attackers_to(Square sq, Bitboard occupied) const {
  return (pawn_attacks(WHITE, sq) & pieces(PAWN, BLACK))
         | (pawn_attacks(BLACK, sq) & pieces(PAWN, WHITE)) | (knight_attacks(sq) & pieces(KNIGHT))
         | (bishop_attacks(sq, occupied) & pieces(BISHOP, QUEEN))
         | (rook_attacks(sq, occupied) & pieces(ROOK, QUEEN)) | (king_attacks(sq) & pieces(KING));
}

Square Board::king_square(Color c) const {
  Bitboard kings = pieces(KING, c);
  assert(kings);
  return lsb_sq(kings);
}

Bitboard Board::blockers_for_king(Color c) const {
  return st->blockersForKing[c];
}
Bitboard Board::check_blockers(Color c, Color kingColor) const {
  return st->blockersForKing[kingColor] & pieces(c);
}

bool Board::gives_check(Move m) const {
  Square from = from_sq(m);
  Square to = to_sq(m);
  Color us = side_to_move();
  Square ksq = king_square(~us);

  PieceType pt = piece_type(piece_on(from));
  Bitboard occupied = (pieces() ^ square_bb(from)) | square_bb(to);

  if (is_castling(m)) {
    CastlingRights cr = (us == WHITE) ? (to > from ? WHITE_OO : WHITE_OOO)
                                      : (to > from ? BLACK_OO : BLACK_OOO);
    return (rook_attacks(CASTLING_DATA[cr].rookDest, occupied) & square_bb(ksq)) != 0;
  }

  if (is_en_passant(m)) {
    Square capsq = (us == WHITE) ? Square(to - 8) : Square(to + 8);
    Bitboard occ2 = occupied ^ square_bb(capsq);
    if (pawn_attacks(us, to) & square_bb(ksq))
      return true;
    return (bishop_attacks(ksq, occ2) & pieces(BISHOP, QUEEN) & pieces(us))
           || (rook_attacks(ksq, occ2) & pieces(ROOK, QUEEN) & pieces(us));
  }

  if (is_promotion(m))
    return (attacks_bb(promo_piece(m), to, occupied) & square_bb(ksq)) != 0;

  if (pt == PAWN ? (pawn_attacks(us, to) & square_bb(ksq))
                 : (attacks_bb(pt, to, occupied) & square_bb(ksq)))
    return true;

  // Discovered check
  if ((st->blockersForKing[~us] & square_bb(from)) && !(line_bb(ksq, from) & square_bb(to)))
    return true;

  return false;
}

bool Board::is_legal(Move m) const {
  Color us = sideToMove;
  Square from = from_sq(m);
  Square to = to_sq(m);
  PieceType pt = piece_type(piece_on(from));
  Bitboard occ = pieces();

  if (!is_promotion(m) && (m >> 14) != 0)
    return false;

  if (piece_on(from) == NO_PIECE || piece_color(piece_on(from)) != us)
    return false;
  if (!is_castling(m) && piece_on(to) != NO_PIECE && piece_color(piece_on(to)) == us)
    return false;
  if (!is_castling(m) && piece_type(piece_on(to)) == KING)
    return false;
  if (is_promotion(m) && pt != PAWN)
    return false;
  if (is_en_passant(m) && pt != PAWN)
    return false;
  if (is_castling(m) && pt != KING)
    return false;

  switch (pt) {
  case PAWN: {
    if (is_en_passant(m)) {
      if (to != st->epSquare)
        return false;
      Square capsq = (us == WHITE) ? Square(to - 8) : Square(to + 8);
      Bitboard newOcc = (pieces() ^ square_bb(from) ^ square_bb(capsq)) | square_bb(to);
      Bitboard enemyAfterEP = pieces(~us) & ~square_bb(capsq);
      return !(attackers_to(king_square(us), newOcc) & enemyAfterEP);
    }

    Bitboard valid = 0;
    Bitboard pawnBB = square_bb(from);
    Bitboard epBB = (st->epSquare != SQ_NONE) ? square_bb(st->epSquare) : 0;

    if (us == WHITE) {
      if (!(occ & (pawnBB << 8))) {
        valid |= pawnBB << 8;
        if (rankOf(from) == RANK_2 && !(occ & (pawnBB << 16)))
          valid |= pawnBB << 16;
      }
      valid |= pawn_attacks(WHITE, from) & (pieces(BLACK) | epBB);
    } else {
      if (!(occ & (pawnBB >> 8))) {
        valid |= pawnBB >> 8;
        if (rankOf(from) == RANK_7 && !(occ & (pawnBB >> 16)))
          valid |= pawnBB >> 16;
      }
      valid |= pawn_attacks(BLACK, from) & (pieces(WHITE) | epBB);
    }
    if (!(valid & square_bb(to)))
      return false;
    break;
  }
  case KNIGHT:
    if (!(knight_attacks(from) & square_bb(to) & ~pieces(us)))
      return false;
    break;
  case BISHOP:
    if (!(bishop_attacks(from, occ) & square_bb(to) & ~pieces(us)))
      return false;
    break;
  case ROOK:
    if (!(rook_attacks(from, occ) & square_bb(to) & ~pieces(us)))
      return false;
    break;
  case QUEEN:
    if (!(queen_attacks(from, occ) & square_bb(to) & ~pieces(us)))
      return false;
    break;
  case KING: {
    if (is_castling(m)) {
      CastlingRights cr = (to > from) ? (us == WHITE ? WHITE_OO : BLACK_OO)
                                      : (us == WHITE ? WHITE_OOO : BLACK_OOO);
      if (!can_castle(cr))
        return false;
      if (castlingPath[cr] & pieces())
        return false;
      if (st->checkersBB)
        return false;

      Square kingDest = CASTLING_DATA[cr].kingDest;
      Square rfrom = CASTLING_DATA[cr].rookSrc;
      Square rookDest = CASTLING_DATA[cr].rookDest;
      Bitboard newOcc = occ ^ square_bb(from) ^ square_bb(rfrom);

      // Destinations must be vacant after removing king+rook
      if (newOcc & (square_bb(kingDest) | square_bb(rookDest)))
        return false;

      Square step = (kingDest > from) ? Square(from + 1) : Square(from - 1);
      if (attackers_to(step, newOcc) & pieces(~us))
        return false;
      if (attackers_to(kingDest, newOcc) & pieces(~us))
        return false;
    } else {
      if (!(king_attacks(from) & square_bb(to) & ~pieces(us)))
        return false;
    }
    break;
  }
  default:
    return false;
  }

  // Full legality check: make sure king is not in check after move
  if (pt != KING && !is_castling(m) && !is_en_passant(m)) {

    Bitboard newOcc = (occ ^ square_bb(from)) | square_bb(to);
    Bitboard enemyAfter = pieces(~us) & ~square_bb(to);

    if (attackers_to(king_square(us), newOcc) & enemyAfter)
      return false;
  }

  if (pt == KING && !is_castling(m)) {
    Bitboard newOcc = (occ ^ square_bb(from)) | square_bb(to);
    if (attackers_to(to, newOcc) & pieces(~us))
      return false;
  }

  if (is_en_passant(m)) {
    Square capsq = (us == WHITE) ? Square(to - 8) : Square(to + 8);
    Bitboard newOcc = (occ ^ square_bb(from) ^ square_bb(capsq)) | square_bb(to);
    Bitboard enemyAfterEP = pieces(~us) & ~square_bb(capsq);
    if (attackers_to(king_square(us), newOcc) & enemyAfterEP)
      return false;
  }

  return true;
}

bool Board::is_pseudo_legal(Move m) const {
  Square from = from_sq(m);
  Square to = to_sq(m);

  if (!is_promotion(m) && (m >> 14) != 0)
    return false;

  if (!is_ok(from) || !is_ok(to))
    return false;

  Piece pc = piece_on(from);
  if (pc == NO_PIECE || piece_color(pc) != sideToMove)
    return false;

  Piece target = piece_on(to);
  if (!is_castling(m) && target != NO_PIECE) {
    if (piece_color(target) == sideToMove)
      return false;
    if (piece_type(target) == KING)
      return false;
  }

  PieceType pt = piece_type(pc);
  Bitboard occ = pieces();

  if (is_promotion(m) && pt != PAWN)
    return false;
  if (is_en_passant(m) && pt != PAWN)
    return false;
  if (is_castling(m) && pt != KING)
    return false;

  switch (pt) {
  case PAWN: {
    Bitboard fromBB = square_bb(from);
    Bitboard valid = 0;
    Bitboard epBB = (st->epSquare != SQ_NONE) ? square_bb(st->epSquare) : 0;

    if (sideToMove == WHITE) {
      if (!(occ & (fromBB << 8))) {
        valid |= fromBB << 8;
        if (rankOf(from) == RANK_2 && !(occ & (fromBB << 16)))
          valid |= fromBB << 16;
      }
      valid |= pawn_attacks(WHITE, from) & (pieces(BLACK) | epBB);
    } else {
      if (!(occ & (fromBB >> 8))) {
        valid |= fromBB >> 8;
        if (rankOf(from) == RANK_7 && !(occ & (fromBB >> 16)))
          valid |= fromBB >> 16;
      }
      valid |= pawn_attacks(BLACK, from) & (pieces(WHITE) | epBB);
    }

    if (!(valid & square_bb(to)))
      return false;

    // Promotion flag must match whether the pawn reaches the back rank.
    Rank backRank = (sideToMove == WHITE) ? RANK_8 : RANK_1;
    if (is_promotion(m) != (rankOf(to) == backRank))
      return false;
    break;
  }
  case KNIGHT:
    if (!(knight_attacks(from) & square_bb(to)))
      return false;
    break;
  case BISHOP:
    if (!(bishop_attacks(from, occ) & square_bb(to)))
      return false;
    break;
  case ROOK:
    if (!(rook_attacks(from, occ) & square_bb(to)))
      return false;
    break;
  case QUEEN:
    if (!(queen_attacks(from, occ) & square_bb(to)))
      return false;
    break;
  case KING: {
    if (is_castling(m)) {
      CastlingRights cr = (to > from) ? (sideToMove == WHITE ? WHITE_OO : BLACK_OO)
                                      : (sideToMove == WHITE ? WHITE_OOO : BLACK_OOO);
      if (!can_castle(cr))
        return false;
      if (piece_on(from) != makePiece(sideToMove, KING))
        return false;
      if (from != CASTLING_DATA[cr].kingSrc)
        return false;
      if (piece_on(CASTLING_DATA[cr].rookSrc) != makePiece(sideToMove, ROOK))
        return false;
      if (castlingPath[cr] & pieces())
        return false;

      // King and rook destinations must be vacant (excluding themselves)
      Bitboard withoutCastlers = pieces() ^ square_bb(from) ^ square_bb(CASTLING_DATA[cr].rookSrc);
      if (withoutCastlers
          & (square_bb(CASTLING_DATA[cr].kingDest) | square_bb(CASTLING_DATA[cr].rookDest)))
        return false;
    } else {
      if (!(king_attacks(from) & square_bb(to)))
        return false;
    }
    break;
  }
  default:
    return false;
  }

  return true;
}

bool Board::is_repetition(int /*ply*/) const {
  int end = std::max(0, historyLen - 1 - st->rule50);
  int start = historyLen - 3;

  for (int i = start; i >= end; i -= 2) {
    if (i < 0)
      break;
    if (positionHistory[i] == st->key)
      return true;
  }
  return false;
}

bool Board::has_game_cycle(int ply) const {
  return is_repetition(ply);
}

bool Board::is_draw(int ply) const {
  if (st->rule50 >= 100)
    return true;
  if (is_repetition(ply))
    return true;

  if (!pieces(PAWN) && !pieces(ROOK) && !pieces(QUEEN)) {
    if (!pieces(KNIGHT) && !pieces(BISHOP))
      return true;
    int minors = popcount(pieces(KNIGHT)) + popcount(pieces(BISHOP));
    if (minors <= 1)
      return true;
    if (popcount(pieces(KNIGHT)) == 2 && !pieces(BISHOP))
      return true;
  }

  return false;
}

void Board::display() const {
  const char* pieceChars = " PNBRQK  pnbrqk";
  std::cout << "\n  +---+---+---+---+---+---+---+---+\n";
  for (Rank r = RANK_8; r >= RANK_1; --r) {
    std::cout << (r + 1) << " |";
    for (File f = FILE_A; f < FILE_NB; ++f)
      std::cout << ' ' << pieceChars[piece_on(makeSquare(f, r))] << " |";
    std::cout << "\n  +---+---+---+---+---+---+---+---+\n";
  }
  std::cout << "    a   b   c   d   e   f   g   h\n";
  std::cout << "\nFEN: " << get_fen() << '\n';
  std::cout << "Key: 0x" << std::hex << st->key << std::dec << '\n';
  std::cout << "Checkers: " << popcount(st->checkersBB) << '\n';
  std::cout << (sideToMove == WHITE ? "White" : "Black") << " to move\n\n";
}

Square Board::castling_rook_square(CastlingRights cr) const {
  return castlingRookSquare[cr];
}

} // namespace Catalyst
