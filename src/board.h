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

#include <string>

#include "types.h"

namespace Catalyst {

namespace Zobrist {
extern Key psq[PIECE_NB][SQUARE_NB];
extern Key enpassant[FILE_NB];
extern Key castling[CASTLING_RIGHTS_NB];
extern Key side;

void init();
} // namespace Zobrist

struct alignas(64) StateInfo {
  Key key;
  Key pawnKey;
  Key nonPawnKey[COLOR_NB];
  int castlingRights;
  Square epSquare;
  int rule50;
  int pliesFromNull;

  Bitboard checkersBB;
  Bitboard blockersForKing[COLOR_NB];
  Bitboard pinners[COLOR_NB];
  Piece capturedPiece;
  uint8_t _pad[7];

  StateInfo* previous;
};
static_assert(sizeof(StateInfo) == 128, "StateInfo size mismatch — check padding");

// clang-format off
inline constexpr int CASTLING_RIGHTS_MASK[SQUARE_NB] = {
  13, 15, 15, 15, 12, 15, 15, 14, // Rank 1: a1=~WHITE_OOO, e1=~WHITE_CASTLING, h1=~WHITE_OO
  15, 15, 15, 15, 15, 15, 15, 15, // Rank 2
  15, 15, 15, 15, 15, 15, 15, 15, // Rank 3
  15, 15, 15, 15, 15, 15, 15, 15, // Rank 4
  15, 15, 15, 15, 15, 15, 15, 15, // Rank 5
  15, 15, 15, 15, 15, 15, 15, 15, // Rank 6
  15, 15, 15, 15, 15, 15, 15, 15, // Rank 7
  7, 15, 15, 15,  3, 15, 15, 11,  // Rank 8: a8=~BLACK_OOO, e8=~BLACK_CASTLING, h8=~BLACK_OO
};
// clang-format on

class alignas(64) Board {
public:
  Board();
  ~Board() = default;

  Board(const Board&) = delete;
  Board& operator=(const Board&) = delete;
  Board(Board&&) = default;
  Board& operator=(Board&&) = default;

  void set_fen(const std::string& fen);
  std::string get_fen() const;
  void set_startpos();

  void make_move(Move m, StateInfo& newSt);
  void unmake_move(Move m);
  void make_null_move(StateInfo& newSt);
  void unmake_null_move();
  void copy_from(const Board& other);

  // clang-format off
  [[nodiscard]] FORCE_INLINE Piece    piece_on(Square sq) const               { return board[sq]; }
  [[nodiscard]] FORCE_INLINE bool     empty(Square sq) const                  { return board[sq] == NO_PIECE; }
  [[nodiscard]] FORCE_INLINE Bitboard pieces() const                          { return byTypeBB[ALL_PIECES]; }
  [[nodiscard]] FORCE_INLINE Bitboard pieces(Color c) const                   { return byColorBB[c]; }
  [[nodiscard]] FORCE_INLINE Bitboard pieces(PieceType pt) const              { return byTypeBB[pt]; }
  [[nodiscard]] FORCE_INLINE Bitboard pieces(PieceType pt, Color c) const     { return byTypeBB[pt] & byColorBB[c]; }
  [[nodiscard]] FORCE_INLINE Bitboard pieces(PieceType pt1, PieceType pt2) const           { return byTypeBB[pt1] | byTypeBB[pt2]; }
  [[nodiscard]] FORCE_INLINE Bitboard pieces(PieceType pt1, PieceType pt2, Color c) const  { return (byTypeBB[pt1] | byTypeBB[pt2]) & byColorBB[c]; }

  [[nodiscard]] FORCE_INLINE Color    side_to_move() const    { return sideToMove; }
  [[nodiscard]] FORCE_INLINE Square   ep_square() const       { return st->epSquare; }
  [[nodiscard]] FORCE_INLINE int      castling_rights() const { return st->castlingRights; }
  [[nodiscard]] FORCE_INLINE int      rule50_count() const    { return st->rule50; }
  [[nodiscard]] FORCE_INLINE int      game_ply() const        { return gamePly; }
  [[nodiscard]] FORCE_INLINE Key      key() const             { return st->key; }
  [[nodiscard]] FORCE_INLINE Key      pawn_key() const        { return st->pawnKey; }
  [[nodiscard]] FORCE_INLINE Key non_pawn_key(Color c) const { return st->nonPawnKey[c]; }
  [[nodiscard]] FORCE_INLINE bool     can_castle(CastlingRights cr) const { return (st->castlingRights & cr) != 0; }

  [[nodiscard]] FORCE_INLINE Bitboard checkers() const  { return st->checkersBB; }
  [[nodiscard]] FORCE_INLINE bool     in_check() const  { return st->checkersBB != 0; }
  [[nodiscard]] FORCE_INLINE bool move_leaves_king_in_check() const {
    return (attackers_to(king_square(sideToMove)) & pieces(~sideToMove)) != 0;
  }

  [[nodiscard]] FORCE_INLINE bool is_capture(Move m) const {
    return move_type(m) == MT_EN_PASSANT ||
           (move_type(m) != MT_CASTLING && piece_on(to_sq(m)) != NO_PIECE);
  }
  [[nodiscard]] FORCE_INLINE bool is_capture_or_promotion(Move m) const { return is_capture(m) || is_promotion(m); }
  // clang-format on

  [[nodiscard]] Square castling_rook_square(CastlingRights cr) const;
  [[nodiscard]] Bitboard blockers_for_king(Color c) const;
  [[nodiscard]] Bitboard check_blockers(Color c, Color kingColor) const;
  [[nodiscard]] Bitboard attackers_to(Square sq) const;
  [[nodiscard]] Bitboard attackers_to(Square sq, Bitboard occupied) const;
  [[nodiscard]] Square king_square(Color c) const;
  [[nodiscard]] bool gives_check(Move m) const;
  [[nodiscard]] bool is_legal(Move m) const;
  [[nodiscard]] bool is_pseudo_legal(Move m) const;
  [[nodiscard]] bool is_draw(int ply) const;
  [[nodiscard]] bool has_game_cycle(int ply) const;

  void display() const;

  static constexpr int MAX_GAME_PLY = 512;
  Key positionHistory[MAX_GAME_PLY];
  int historyLen = 0;

  FORCE_INLINE void add_to_history(Key k) {
    if (historyLen < MAX_GAME_PLY)
      positionHistory[historyLen++] = k;
  }
  FORCE_INLINE void remove_from_history() {
    if (historyLen > 0)
      --historyLen;
  }

  [[nodiscard]] bool is_repetition(int ply) const;

private:
  alignas(64) Bitboard byTypeBB[PIECE_TYPE_NB];
  alignas(64) Bitboard byColorBB[COLOR_NB];
  alignas(64) Piece board[SQUARE_NB];

  StateInfo startState;
  Color sideToMove;
  StateInfo* st;
  int gamePly;

  Bitboard castlingPath[CASTLING_RIGHTS_NB];
  Square castlingRookSquare[CASTLING_RIGHTS_NB];

  void clear();
  void put_piece(Piece pc, Square sq);
  void remove_piece(Square sq);
  void move_piece(Square from, Square to);
  void set_state(StateInfo* si);
  void update_state(StateInfo* si);
  void update_blockers(StateInfo* si);
  Key compute_pawn_key() const;
  Key compute_non_pawn_key(Color c) const;

  template <bool AfterMove> Key compute_key() const;
};

} // namespace Catalyst
