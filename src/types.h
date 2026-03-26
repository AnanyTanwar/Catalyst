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
// along with this program.  If not, see <http://www.gnu.org/licenses/ >.

#pragma once

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "intrinsics.h"

namespace Catalyst {

// Engine Info
inline constexpr std::string_view ENGINE_NAME = "Catalyst";
inline constexpr std::string_view ENGINE_VERSION = "v1.0";
inline constexpr std::string_view ENGINE_AUTHOR = "Anany Tanwar";

// Type Aliases
using Bitboard = uint64_t;
using Key = uint64_t;
using TbResult = uint32_t;
using Score = int;
using Move = uint16_t;
using Depth = int8_t;

// Limits
inline constexpr int MAX_PLY = 128;
inline constexpr int MAX_MOVES = 512;
inline constexpr int MAX_DEPTH = 64;

// Score Constants
inline constexpr Score SCORE_DRAW = 0;
inline constexpr Score SCORE_MATE = 32000;
inline constexpr Score SCORE_INFINITE = 32001;
inline constexpr Score SCORE_NONE = 32002;
inline constexpr Score SCORE_MATE_IN_MAX_PLY = SCORE_MATE - MAX_PLY;
inline constexpr Score SCORE_TB_WIN = SCORE_MATE_IN_MAX_PLY - 1;
inline constexpr Score SCORE_TB_WIN_IN_MAX_PLY = SCORE_TB_WIN - MAX_PLY;
inline constexpr Score SCORE_TB_LOSS_IN_MAX_PLY = -SCORE_TB_WIN_IN_MAX_PLY;

inline constexpr Move MOVE_NONE = 0;
static_assert(SCORE_MATE_IN_MAX_PLY > SCORE_TB_WIN, "Score ordering broken");

struct MoveList {
  Move moves[MAX_MOVES];
  int count = 0;

  void push(Move m) { moves[count++] = m; }
  Move* begin() { return moves; }
  Move* end() { return moves + count; }
  int size() const { return count; }
  bool empty() const { return count == 0; }
  void clear() { count = 0; }
};

struct PvList {
  Move moves[MAX_PLY];
  int length = 0;

  void update(Move m, const PvList& child) {
    moves[0] = m;
    for (int i = 0; i < child.length; ++i)
      moves[i + 1] = child.moves[i];
    length = child.length + 1;
  }
};

inline constexpr size_t CACHE_LINE_SIZE = 64;

[[nodiscard]] FORCE_INLINE int BitCount(uint64_t x) {
  return popcount(x);
}

[[nodiscard]] inline int64_t timeMillis() {
  auto sinceEpoch = std::chrono::steady_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(sinceEpoch).count();
}

// clang-format off
enum MoveType      : uint8_t { MT_NORMAL = 0, MT_CASTLING = 1, MT_EN_PASSANT = 2, MT_PROMOTION = 3 };
enum GenType       : uint8_t { CAPTURES, QUIETS, ALL_MOVES };
enum Color         : int     { WHITE, BLACK, COLOR_NB = 2 };
enum Rank          : int     { RANK_1, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8, RANK_NB = 8 };
enum File          : int     { FILE_A, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_H, FILE_NB = 8 };
enum PieceType     : int     { NO_PIECE_TYPE, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING, ALL_PIECES, PIECE_TYPE_NB = 8 };

enum Square : int {
  SQ_A1, SQ_B1, SQ_C1, SQ_D1, SQ_E1, SQ_F1, SQ_G1, SQ_H1,
  SQ_A2, SQ_B2, SQ_C2, SQ_D2, SQ_E2, SQ_F2, SQ_G2, SQ_H2,
  SQ_A3, SQ_B3, SQ_C3, SQ_D3, SQ_E3, SQ_F3, SQ_G3, SQ_H3,
  SQ_A4, SQ_B4, SQ_C4, SQ_D4, SQ_E4, SQ_F4, SQ_G4, SQ_H4,
  SQ_A5, SQ_B5, SQ_C5, SQ_D5, SQ_E5, SQ_F5, SQ_G5, SQ_H5,
  SQ_A6, SQ_B6, SQ_C6, SQ_D6, SQ_E6, SQ_F6, SQ_G6, SQ_H6,
  SQ_A7, SQ_B7, SQ_C7, SQ_D7, SQ_E7, SQ_F7, SQ_G7, SQ_H7,
  SQ_A8, SQ_B8, SQ_C8, SQ_D8, SQ_E8, SQ_F8, SQ_G8, SQ_H8,
  SQ_NONE, SQUARE_NB = 64, SQUARE_NB_PLUS_NULL = 65
};

enum CastlingRights : int {
  NO_CASTLING       = 0,
  WHITE_OO          = 1,
  WHITE_OOO         = 2,
  BLACK_OO          = 4,
  BLACK_OOO         = 8,
  WHITE_CASTLING    = WHITE_OO | WHITE_OOO,
  BLACK_CASTLING    = BLACK_OO | BLACK_OOO,
  ALL_CASTLING      = WHITE_CASTLING | BLACK_CASTLING,
  CASTLING_RIGHTS_NB = 16
};

enum Direction : int {
  NORTH      = 8,
  EAST       = 1,
  SOUTH      = -8,
  WEST       = -1,
  NORTH_EAST = NORTH + EAST,
  NORTH_WEST = NORTH + WEST,
  SOUTH_EAST = SOUTH + EAST,
  SOUTH_WEST = SOUTH + WEST
};

enum Piece : uint8_t {
  NO_PIECE     = 0,
  WHITE_PAWN   = (WHITE << 3) | PAWN,
  WHITE_KNIGHT = (WHITE << 3) | KNIGHT,
  WHITE_BISHOP = (WHITE << 3) | BISHOP,
  WHITE_ROOK   = (WHITE << 3) | ROOK,
  WHITE_QUEEN  = (WHITE << 3) | QUEEN,
  WHITE_KING   = (WHITE << 3) | KING,
  BLACK_PAWN   = (BLACK << 3) | PAWN,
  BLACK_KNIGHT = (BLACK << 3) | KNIGHT,
  BLACK_BISHOP = (BLACK << 3) | BISHOP,
  BLACK_ROOK   = (BLACK << 3) | ROOK,
  BLACK_QUEEN  = (BLACK << 3) | QUEEN,
  BLACK_KING   = (BLACK << 3) | KING,
  PIECE_NB     = 16
};
// clang-format on

[[nodiscard]] constexpr Piece makePiece(Color c, PieceType pt) {
  return Piece((c << 3) | pt);
}
[[nodiscard]] constexpr PieceType piece_type(Piece pc) {
  return PieceType(pc & 7);
}
[[nodiscard]] constexpr Color piece_color(Piece pc) {
  return Color(pc >> 3);
}

inline constexpr int PIECE_VALUE[PIECE_TYPE_NB] = {0, 100, 320, 330, 500, 900, 0, 0};

struct CastlingData {
  Square kingSrc, kingDest, rookSrc, rookDest;
};

// clang-format off
inline constexpr CastlingData CASTLING_DATA[CASTLING_RIGHTS_NB] = {
    {SQ_NONE, SQ_NONE, SQ_NONE, SQ_NONE}, // 0: NO_CASTLING
    {SQ_E1, SQ_G1, SQ_H1, SQ_F1},         // 1: WHITE_OO
    {SQ_E1, SQ_C1, SQ_A1, SQ_D1},         // 2: WHITE_OOO
    {SQ_NONE, SQ_NONE, SQ_NONE, SQ_NONE}, // 3
    {SQ_E8, SQ_G8, SQ_H8, SQ_F8},         // 4: BLACK_OO
    {SQ_NONE, SQ_NONE, SQ_NONE, SQ_NONE}, // 5
    {SQ_NONE, SQ_NONE, SQ_NONE, SQ_NONE}, // 6
    {SQ_NONE, SQ_NONE, SQ_NONE, SQ_NONE}, // 7
    {SQ_E8, SQ_C8, SQ_A8, SQ_D8},         // 8: BLACK_OOO
    {SQ_NONE, SQ_NONE, SQ_NONE, SQ_NONE}, // 9
    {SQ_NONE, SQ_NONE, SQ_NONE, SQ_NONE}, // 10
    {SQ_NONE, SQ_NONE, SQ_NONE, SQ_NONE}, // 11
    {SQ_NONE, SQ_NONE, SQ_NONE, SQ_NONE}, // 12
    {SQ_NONE, SQ_NONE, SQ_NONE, SQ_NONE}, // 13
    {SQ_NONE, SQ_NONE, SQ_NONE, SQ_NONE}, // 14
    {SQ_NONE, SQ_NONE, SQ_NONE, SQ_NONE}  // 15
};

inline constexpr Bitboard CASTLING_PATH[CASTLING_RIGHTS_NB] = {
  0,                                                      // 0
  (1ULL << SQ_F1) | (1ULL << SQ_G1),                     // 1: WHITE_OO
  (1ULL << SQ_B1) | (1ULL << SQ_C1) | (1ULL << SQ_D1),   // 2: WHITE_OOO
  0,                                                      // 3
  (1ULL << SQ_F8) | (1ULL << SQ_G8),                     // 4: BLACK_OO
  0, 0, 0,                                                // 5-7
  (1ULL << SQ_B8) | (1ULL << SQ_C8) | (1ULL << SQ_D8),   // 8: BLACK_OOO
  0, 0, 0, 0, 0, 0, 0                                     // 9-15
};
// clang-format on

[[nodiscard]] constexpr Square makeSquare(File f, Rank r) {
  return Square((r << 3) | f);
}
[[nodiscard]] constexpr File fileOf(Square s) {
  return File(s & 7);
}
[[nodiscard]] constexpr Rank rankOf(Square s) {
  return Rank(s >> 3);
}

[[nodiscard]] constexpr Square relative_square(Color c, Square s) {
  return (c == WHITE) ? s : Square(s ^ 56);
}

[[nodiscard]] constexpr Rank relative_rank(Color c, Rank r) {
  return Rank(r ^ (c * 7));
}
[[nodiscard]] constexpr bool is_ok(Square s) {
  return s >= SQ_A1 && s < SQ_NONE;
}

// clang-format off
inline constexpr Bitboard FileABB = 0x0101010101010101ULL;
inline constexpr Bitboard FileBBB = FileABB << 1;
inline constexpr Bitboard FileCBB = FileABB << 2;
inline constexpr Bitboard FileDBB = FileABB << 3;
inline constexpr Bitboard FileEBB = FileABB << 4;
inline constexpr Bitboard FileFBB = FileABB << 5;
inline constexpr Bitboard FileGBB = FileABB << 6;
inline constexpr Bitboard FileHBB = FileABB << 7;

inline constexpr Bitboard Rank1BB = 0xFF;
inline constexpr Bitboard Rank2BB = Rank1BB << 8;
inline constexpr Bitboard Rank3BB = Rank1BB << 16;
inline constexpr Bitboard Rank4BB = Rank1BB << 24;
inline constexpr Bitboard Rank5BB = Rank1BB << 32;
inline constexpr Bitboard Rank6BB = Rank1BB << 40;
inline constexpr Bitboard Rank7BB = Rank1BB << 48;
inline constexpr Bitboard Rank8BB = Rank1BB << 56;

inline constexpr Bitboard LightSquares = 0x55AA55AA55AA55AAULL;
inline constexpr Bitboard DarkSquares  = 0xAA55AA55AA55AA55ULL;
// clang-format on

[[nodiscard]] FORCE_INLINE constexpr Bitboard square_bb(Square s) {
  return 1ULL << s;
}
[[nodiscard]] FORCE_INLINE constexpr Bitboard file_bb(File f) {
  return FileABB << f;
}
[[nodiscard]] FORCE_INLINE constexpr Bitboard file_bb(Square s) {
  return file_bb(fileOf(s));
}
[[nodiscard]] FORCE_INLINE constexpr Bitboard rank_bb(Rank r) {
  return Rank1BB << (8 * r);
}
[[nodiscard]] FORCE_INLINE constexpr Bitboard rank_bb(Square s) {
  return rank_bb(rankOf(s));
}
[[nodiscard]] FORCE_INLINE constexpr bool more_than_one(Bitboard b) {
  return b & (b - 1);
}

[[nodiscard]] FORCE_INLINE constexpr bool opposite_colors(Square s1, Square s2) {
  return ((int(fileOf(s1)) ^ int(rankOf(s1)) ^ int(fileOf(s2)) ^ int(rankOf(s2))) & 1);
}

[[nodiscard]] constexpr bool is_mate_score(Score s) {
  return std::abs(s) >= SCORE_MATE_IN_MAX_PLY;
}
[[nodiscard]] constexpr Score mate_in(int ply) {
  return SCORE_MATE - ply;
}
[[nodiscard]] constexpr Score mated_in(int ply) {
  return -SCORE_MATE + ply;
}

// clang-format off
#define ENABLE_BASE_OPERATORS_ON(T)                                        \
  [[nodiscard]] constexpr T operator+(T d1, int d2) { return T(int(d1) + d2); } \
  [[nodiscard]] constexpr T operator-(T d1, int d2) { return T(int(d1) - d2); } \
  [[nodiscard]] constexpr T operator-(T d)          { return T(-int(d)); }       \
  inline T& operator+=(T& d1, int d2) { return d1 = d1 + d2; }                  \
  inline T& operator-=(T& d1, int d2) { return d1 = d1 - d2; }

#define ENABLE_INCR_OPERATORS_ON(T)                                        \
  inline T& operator++(T& d) { return d = T(int(d) + 1); }                \
  inline T& operator--(T& d) { return d = T(int(d) - 1); }

#define ENABLE_LOGIC_OPERATORS_ON(T)                                       \
  [[nodiscard]] constexpr T operator~(T d)          { return T(~int(d)); }       \
  [[nodiscard]] constexpr T operator&(T d1, T d2)   { return T(int(d1) & int(d2)); } \
  [[nodiscard]] constexpr T operator|(T d1, T d2)   { return T(int(d1) | int(d2)); } \
  [[nodiscard]] constexpr T operator^(T d1, T d2)   { return T(int(d1) ^ int(d2)); } \
  inline T& operator&=(T& d1, T d2) { return d1 = d1 & d2; }                    \
  inline T& operator|=(T& d1, T d2) { return d1 = d1 | d2; }                    \
  inline T& operator^=(T& d1, T d2) { return d1 = d1 ^ d2; }

ENABLE_BASE_OPERATORS_ON(File)
ENABLE_BASE_OPERATORS_ON(Rank)
ENABLE_INCR_OPERATORS_ON(PieceType)
ENABLE_INCR_OPERATORS_ON(Color)
ENABLE_INCR_OPERATORS_ON(File)
ENABLE_INCR_OPERATORS_ON(Rank)
ENABLE_INCR_OPERATORS_ON(Square)
ENABLE_INCR_OPERATORS_ON(Piece)
ENABLE_LOGIC_OPERATORS_ON(CastlingRights)
// clang-format on

#undef ENABLE_BASE_OPERATORS_ON
#undef ENABLE_LOGIC_OPERATORS_ON
#undef ENABLE_INCR_OPERATORS_ON

[[nodiscard]] constexpr Color operator~(Color c) {
  return Color(c ^ 1);
}

[[nodiscard]] FORCE_INLINE constexpr Square operator+(Square s, Direction d) {
  return Square(int(s) + int(d));
}

[[nodiscard]] FORCE_INLINE constexpr Square operator-(Square s, Direction d) {
  return Square(int(s) - int(d));
}
inline Square& operator+=(Square& s, Direction d) {
  return s = s + d;
}
inline Square& operator-=(Square& s, Direction d) {
  return s = s - d;
}

[[nodiscard]] inline std::string square_to_string(Square s) {
  if (s >= SQ_NONE)
    return "-";
  char str[3];
  str[0] = char('a' + fileOf(s));
  str[1] = char('1' + rankOf(s));
  str[2] = '\0';
  return std::string(str, 2);
}

[[nodiscard]] inline Square string_to_square(const std::string& s) {
  if (s.size() < 2)
    return SQ_NONE;
  int f = s[0] - 'a';
  int r = s[1] - '1';
  if (f < 0 || f >= FILE_NB || r < 0 || r >= RANK_NB)
    return SQ_NONE;
  return makeSquare(File(f), Rank(r));
}

inline std::ostream& operator<<(std::ostream& os, Square s) {
  os << square_to_string(s);
  return os;
}

inline std::ostream& operator<<(std::ostream& os, Color c) {
  os << (c == WHITE ? std::string_view("white") : std::string_view("black"));
  return os;
}

// Move encoding: [from: 6][to: 6][type: 2][promo: 2]
inline constexpr PieceType PROMO_PIECES[4] = {KNIGHT, BISHOP, ROOK, QUEEN};

[[nodiscard]] FORCE_INLINE constexpr Square from_sq(Move m) {
  return Square(m & 0x3F);
}
[[nodiscard]] FORCE_INLINE constexpr Square to_sq(Move m) {
  return Square((m >> 6) & 0x3F);
}
[[nodiscard]] FORCE_INLINE constexpr MoveType move_type(Move m) {
  return MoveType((m >> 12) & 0x3);
}
[[nodiscard]] FORCE_INLINE constexpr PieceType promo_piece(Move m) {
  return PROMO_PIECES[(m >> 14) & 0x3];
}

[[nodiscard]] FORCE_INLINE constexpr Move make_move(Square from, Square to) {
  return Move(from | (to << 6));
}

[[nodiscard]] FORCE_INLINE constexpr Move make_move(Square from, Square to, MoveType mt,
                                                    PieceType promo = KNIGHT) {
  int promoIdx = (promo == KNIGHT) ? 0 : (promo == BISHOP) ? 1 : (promo == ROOK) ? 2 : 3;
  return Move(from | (to << 6) | (mt << 12) | (promoIdx << 14));
}

[[nodiscard]] FORCE_INLINE constexpr bool is_promotion(Move m) {
  return move_type(m) == MT_PROMOTION;
}
[[nodiscard]] FORCE_INLINE constexpr bool is_castling(Move m) {
  return move_type(m) == MT_CASTLING;
}
[[nodiscard]] FORCE_INLINE constexpr bool is_en_passant(Move m) {
  return move_type(m) == MT_EN_PASSANT;
}

[[nodiscard]] inline std::string move_to_uci(Move m) {
  if (m == MOVE_NONE)
    return "0000";

  std::string uci = square_to_string(from_sq(m)) + square_to_string(to_sq(m));

  if (is_promotion(m)) {
    PieceType promo = promo_piece(m);
    char promoChar = (promo == KNIGHT)   ? 'n'
                     : (promo == BISHOP) ? 'b'
                     : (promo == ROOK)   ? 'r'
                                         : 'q';
    uci += promoChar;
  }

  return uci;
}

#if defined(__GNUC__) || defined(__clang__)
#define PREFETCH(addr) __builtin_prefetch(addr, 0, 3)
#elif defined(_MSC_VER)
#include <intrin.h>
#define PREFETCH(addr) _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_T0)
#else
#define PREFETCH(addr) ((void)0)
#endif

} // namespace Catalyst
