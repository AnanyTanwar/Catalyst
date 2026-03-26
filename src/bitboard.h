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

#include <algorithm>
#include <cassert>
#include <cstdint>

#include "types.h"

namespace Catalyst {

struct alignas(64) MagicPext {
  Bitboard mask;
  Bitboard* attacks;
  uint8_t shift;

  [[nodiscard]] FORCE_INLINE unsigned index(Bitboard occupied) const {
    return unsigned(pext(occupied, mask));
  }
};

struct alignas(64) MagicMultiply {
  Bitboard mask;
  Bitboard magic;
  Bitboard* attacks;
  uint8_t shift;

  [[nodiscard]] FORCE_INLINE unsigned index(Bitboard occupied) const {
    return unsigned(((occupied & mask) * magic) >> shift);
  }
};

alignas(64) extern Bitboard PawnAttacks[COLOR_NB][SQUARE_NB];
alignas(64) extern Bitboard PawnPushes[COLOR_NB][SQUARE_NB];
alignas(64) extern Bitboard PawnDoublePushes[COLOR_NB][SQUARE_NB];
alignas(64) extern Bitboard KnightAttacks[SQUARE_NB];
alignas(64) extern Bitboard KingAttacks[SQUARE_NB];
alignas(64) extern Bitboard BetweenBB[SQUARE_NB][SQUARE_NB];
alignas(64) extern Bitboard LineBB[SQUARE_NB][SQUARE_NB];
alignas(64) extern Bitboard PseudoAttacks[PIECE_TYPE_NB][SQUARE_NB];

alignas(64) extern MagicPext RookMagicsPext[SQUARE_NB];
alignas(64) extern MagicPext BishopMagicsPext[SQUARE_NB];
alignas(64) extern MagicMultiply RookMagicsMul[SQUARE_NB];
alignas(64) extern MagicMultiply BishopMagicsMul[SQUARE_NB];

alignas(64) extern Bitboard RookTable[0x19000];
alignas(64) extern Bitboard BishopTable[0x1480];

extern Bitboard (*g_rook_attacks)(Square sq, Bitboard occ);
extern Bitboard (*g_bishop_attacks)(Square sq, Bitboard occ);

[[nodiscard]] FORCE_INLINE constexpr int distance(Square a, Square b) {
  return std::max(std::abs(fileOf(a) - fileOf(b)), std::abs(rankOf(a) - rankOf(b)));
}

[[nodiscard]] FORCE_INLINE constexpr int distance_file(Square a, Square b) {
  return std::abs(fileOf(a) - fileOf(b));
}

[[nodiscard]] FORCE_INLINE constexpr int distance_rank(Square a, Square b) {
  return std::abs(rankOf(a) - rankOf(b));
}

[[nodiscard]] FORCE_INLINE constexpr int diagonal_of(Square sq) {
  return 7 + rankOf(sq) - fileOf(sq);
}

[[nodiscard]] FORCE_INLINE constexpr int anti_diagonal_of(Square sq) {
  return rankOf(sq) + fileOf(sq);
}

[[nodiscard]] FORCE_INLINE constexpr Bitboard pawn_attacks_bb(Color c, Square s) {
  Bitboard b = square_bb(s);
  if (c == WHITE)
    return ((b & ~FileHBB) << 9) | ((b & ~FileABB) << 7);
  else
    return ((b & ~FileHBB) >> 7) | ((b & ~FileABB) >> 9);
}

[[nodiscard]] FORCE_INLINE constexpr Bitboard knight_attacks_bb(Square s) {
  Bitboard b = square_bb(s);
  return ((b & ~FileHBB) << 17) | ((b & ~FileABB) << 15) | ((b & ~(FileGBB | FileHBB)) << 10)
         | ((b & ~(FileABB | FileBBB)) << 6) | ((b & ~FileHBB) >> 15) | ((b & ~FileABB) >> 17)
         | ((b & ~(FileGBB | FileHBB)) >> 6) | ((b & ~(FileABB | FileBBB)) >> 10);
}

[[nodiscard]] FORCE_INLINE constexpr Bitboard king_attacks_bb(Square s) {
  Bitboard b = square_bb(s);
  return (b << 8) | (b >> 8) | ((b & ~FileHBB) << 1) | ((b & ~FileABB) >> 1) | ((b & ~FileHBB) << 9)
         | ((b & ~FileABB) << 7) | ((b & ~FileHBB) >> 7) | ((b & ~FileABB) >> 9);
}

[[nodiscard]] FORCE_INLINE Bitboard pawn_attacks(Color c, Square sq) {
  return PawnAttacks[c][sq];
}

[[nodiscard]] FORCE_INLINE Bitboard pawn_pushes(Color c, Square sq) {
  return PawnPushes[c][sq];
}

[[nodiscard]] FORCE_INLINE Bitboard pawn_double_pushes(Color c, Square sq) {
  return PawnDoublePushes[c][sq];
}

[[nodiscard]] FORCE_INLINE Bitboard knight_attacks(Square sq) {
  return KnightAttacks[sq];
}

[[nodiscard]] FORCE_INLINE Bitboard king_attacks(Square sq) {
  return KingAttacks[sq];
}

[[nodiscard]] FORCE_INLINE Bitboard bishop_attacks(Square sq, Bitboard occ) {
  assert(g_bishop_attacks != nullptr);
  return g_bishop_attacks(sq, occ);
}

[[nodiscard]] FORCE_INLINE Bitboard rook_attacks(Square sq, Bitboard occ) {
  assert(g_rook_attacks != nullptr);
  return g_rook_attacks(sq, occ);
}

[[nodiscard]] FORCE_INLINE Bitboard queen_attacks(Square sq, Bitboard occ) {
  return bishop_attacks(sq, occ) | rook_attacks(sq, occ);
}

[[nodiscard]] FORCE_INLINE Bitboard attacks_bb(PieceType pt, Square sq, Bitboard occupied) {
  if (pt == KNIGHT)
    return knight_attacks(sq);
  if (pt == KING)
    return king_attacks(sq);
  if (pt == BISHOP)
    return bishop_attacks(sq, occupied);
  if (pt == ROOK)
    return rook_attacks(sq, occupied);
  if (pt == QUEEN)
    return queen_attacks(sq, occupied);
  return 0;
}

[[nodiscard]] FORCE_INLINE Bitboard between_bb(Square s1, Square s2) {
  return BetweenBB[s1][s2];
}

[[nodiscard]] FORCE_INLINE Bitboard line_bb(Square s1, Square s2) {
  return LineBB[s1][s2];
}

[[nodiscard]] FORCE_INLINE bool aligned(Square s1, Square s2, Square s3) {
  return s3 != s1 && s3 != s2 && (LineBB[s1][s2] & square_bb(s3));
}

[[nodiscard]] FORCE_INLINE Square lsb_sq(Bitboard b) {
  assert(b);
  return Square(lsb(b));
}

[[nodiscard]] FORCE_INLINE Square msb_sq(Bitboard b) {
  assert(b);
  return Square(msb(b));
}

[[nodiscard]] FORCE_INLINE Square pop_lsb(Bitboard& b) {
  Square s = Square(lsb(b));
  b &= b - 1;
  return s;
}

[[nodiscard]] FORCE_INLINE constexpr Bitboard shift_north(Bitboard b) {
  return b << 8;
}
[[nodiscard]] FORCE_INLINE constexpr Bitboard shift_south(Bitboard b) {
  return b >> 8;
}
[[nodiscard]] FORCE_INLINE constexpr Bitboard shift_east(Bitboard b) {
  return (b & ~FileHBB) << 1;
}
[[nodiscard]] FORCE_INLINE constexpr Bitboard shift_west(Bitboard b) {
  return (b & ~FileABB) >> 1;
}
[[nodiscard]] FORCE_INLINE constexpr Bitboard shift_northeast(Bitboard b) {
  return (b & ~FileHBB) << 9;
}
[[nodiscard]] FORCE_INLINE constexpr Bitboard shift_northwest(Bitboard b) {
  return (b & ~FileABB) << 7;
}
[[nodiscard]] FORCE_INLINE constexpr Bitboard shift_southeast(Bitboard b) {
  return (b & ~FileHBB) >> 7;
}
[[nodiscard]] FORCE_INLINE constexpr Bitboard shift_southwest(Bitboard b) {
  return (b & ~FileABB) >> 9;
}

template <Direction D> [[nodiscard]] FORCE_INLINE constexpr Bitboard shift(Bitboard b) {
  if constexpr (D == NORTH)
    return b << 8;
  if constexpr (D == SOUTH)
    return b >> 8;
  if constexpr (D == EAST)
    return (b & ~FileHBB) << 1;
  if constexpr (D == WEST)
    return (b & ~FileABB) >> 1;
  if constexpr (D == NORTH_EAST)
    return (b & ~FileHBB) << 9;
  if constexpr (D == NORTH_WEST)
    return (b & ~FileABB) << 7;
  if constexpr (D == SOUTH_EAST)
    return (b & ~FileHBB) >> 7;
  if constexpr (D == SOUTH_WEST)
    return (b & ~FileABB) >> 9;
  return 0;
}

[[nodiscard]] FORCE_INLINE Bitboard shift_direction(Bitboard b, Direction d) {
  switch (d) {
  case NORTH:
    return shift_north(b);
  case SOUTH:
    return shift_south(b);
  case EAST:
    return shift_east(b);
  case WEST:
    return shift_west(b);
  case NORTH_EAST:
    return shift_northeast(b);
  case NORTH_WEST:
    return shift_northwest(b);
  case SOUTH_EAST:
    return shift_southeast(b);
  case SOUTH_WEST:
    return shift_southwest(b);
  default:
    return 0;
  }
}

[[nodiscard]] FORCE_INLINE Bitboard fill_north(Bitboard b) {
  b |= b << 8;
  b |= b << 16;
  b |= b << 32;
  return b;
}

[[nodiscard]] FORCE_INLINE Bitboard fill_south(Bitboard b) {
  b |= b >> 8;
  b |= b >> 16;
  b |= b >> 32;
  return b;
}

void init_bitboards();

} // namespace Catalyst
