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

#include "bitboard.h"

#include <type_traits>
#include <cstring>

namespace Catalyst {

alignas(64) Bitboard PawnAttacks[COLOR_NB][SQUARE_NB];
alignas(64) Bitboard PawnPushes[COLOR_NB][SQUARE_NB];
alignas(64) Bitboard PawnDoublePushes[COLOR_NB][SQUARE_NB];
alignas(64) Bitboard KnightAttacks[SQUARE_NB];
alignas(64) Bitboard KingAttacks[SQUARE_NB];
alignas(64) Bitboard BetweenBB[SQUARE_NB][SQUARE_NB];
alignas(64) Bitboard LineBB[SQUARE_NB][SQUARE_NB];
alignas(64) Bitboard PseudoAttacks[PIECE_TYPE_NB][SQUARE_NB];

alignas(64) MagicPext RookMagicsPext[SQUARE_NB];
alignas(64) MagicPext BishopMagicsPext[SQUARE_NB];
alignas(64) MagicMultiply RookMagicsMul[SQUARE_NB];
alignas(64) MagicMultiply BishopMagicsMul[SQUARE_NB];

static_assert(sizeof(RookTable) == 0x19000 * sizeof(Bitboard), "RookTable size mismatch");
static_assert(sizeof(BishopTable) == 0x1480 * sizeof(Bitboard), "BishopTable size mismatch");

alignas(64) Bitboard RookTable[0x19000];
alignas(64) Bitboard BishopTable[0x1480];

Bitboard (*g_rook_attacks)(Square sq, Bitboard occ) = nullptr;
Bitboard (*g_bishop_attacks)(Square sq, Bitboard occ) = nullptr;

static Bitboard rook_attacks_pext(Square sq, Bitboard occ) {
  return RookMagicsPext[sq].attacks[RookMagicsPext[sq].index(occ)];
}

static Bitboard rook_attacks_mul(Square sq, Bitboard occ) {
  return RookMagicsMul[sq].attacks[RookMagicsMul[sq].index(occ)];
}

static Bitboard bishop_attacks_pext(Square sq, Bitboard occ) {
  return BishopMagicsPext[sq].attacks[BishopMagicsPext[sq].index(occ)];
}

static Bitboard bishop_attacks_mul(Square sq, Bitboard occ) {
  return BishopMagicsMul[sq].attacks[BishopMagicsMul[sq].index(occ)];
}

template <PieceType Pt> static FORCE_INLINE Bitboard sliding_attack(Square sq, Bitboard occupied) {
  static_assert(Pt == ROOK || Pt == BISHOP, "Only ROOK or BISHOP");

  Bitboard attacks = 0;
  const auto walk = [&](Direction d, auto boundary) {
    Square s = sq;
    while (boundary(s)) {
      s += d;
      attacks |= square_bb(s);
      if (occupied & square_bb(s))
        break;
    }
  };

  if constexpr (Pt == ROOK) {
    walk(NORTH, [](Square s) { return rankOf(s) < RANK_8; });
    walk(SOUTH, [](Square s) { return rankOf(s) > RANK_1; });
    walk(EAST, [](Square s) { return fileOf(s) < FILE_H; });
    walk(WEST, [](Square s) { return fileOf(s) > FILE_A; });
  } else {
    walk(NORTH_EAST, [](Square s) { return rankOf(s) < RANK_8 && fileOf(s) < FILE_H; });
    walk(NORTH_WEST, [](Square s) { return rankOf(s) < RANK_8 && fileOf(s) > FILE_A; });
    walk(SOUTH_EAST, [](Square s) { return rankOf(s) > RANK_1 && fileOf(s) < FILE_H; });
    walk(SOUTH_WEST, [](Square s) { return rankOf(s) > RANK_1 && fileOf(s) > FILE_A; });
  }

  return attacks;
}

template <PieceType Pt> static FORCE_INLINE Bitboard sliding_mask(Square sq) {
  static_assert(Pt == ROOK || Pt == BISHOP, "Only ROOK or BISHOP");

  Bitboard mask = 0;
  const auto walk = [&](Direction d, auto boundary) {
    Square s = sq;
    while (boundary(s)) {
      s += d;
      mask |= square_bb(s);
    }
  };

  if constexpr (Pt == ROOK) {
    walk(NORTH, [](Square s) { return rankOf(s) < RANK_7; });
    walk(SOUTH, [](Square s) { return rankOf(s) > RANK_2; });
    walk(EAST, [](Square s) { return fileOf(s) < FILE_G; });
    walk(WEST, [](Square s) { return fileOf(s) > FILE_B; });
  } else {
    walk(NORTH_EAST, [](Square s) { return rankOf(s) < RANK_7 && fileOf(s) < FILE_G; });
    walk(NORTH_WEST, [](Square s) { return rankOf(s) < RANK_7 && fileOf(s) > FILE_B; });
    walk(SOUTH_EAST, [](Square s) { return rankOf(s) > RANK_2 && fileOf(s) < FILE_G; });
    walk(SOUTH_WEST, [](Square s) { return rankOf(s) > RANK_2 && fileOf(s) > FILE_B; });
  }

  return mask;
}

static constexpr Bitboard RookMagicNumbers[SQUARE_NB] = {
  0x8a80104000800020ULL, 0x140002000100040ULL,  0x2801880a0017001ULL,  0x100081001000420ULL,
  0x200020010080420ULL,  0x3001c0002010008ULL,  0x8480008002000100ULL, 0x2080088004402900ULL,
  0x800098204000ULL,     0x2024401000200040ULL, 0x100802000801000ULL,  0x120800800801000ULL,
  0x208808088000400ULL,  0x2802200800400ULL,    0x2200800100020080ULL, 0x801000060821100ULL,
  0x80044006422000ULL,   0x100808020004000ULL,  0x12108a0010204200ULL, 0x140848010000802ULL,
  0x481828014002800ULL,  0x8094004002004100ULL, 0x4010040010010802ULL, 0x20008806104ULL,
  0x100400080208000ULL,  0x2040002120081000ULL, 0x21200680100081ULL,   0x20100080080080ULL,
  0x2000a00200410ULL,    0x20080800400ULL,      0x80088400100102ULL,   0x80004600042881ULL,
  0x4040008040800020ULL, 0x440003000200801ULL,  0x4200011004500ULL,    0x188020010100100ULL,
  0x14800401802800ULL,   0x2080040080800200ULL, 0x124080204001001ULL,  0x200046502000484ULL,
  0x480400080088020ULL,  0x1000422010034000ULL, 0x30200100110040ULL,   0x100021010009ULL,
  0x2002080100110004ULL, 0x202008004008002ULL,  0x20020004010100ULL,   0x2048440040820001ULL,
  0x101002200408200ULL,  0x40802000401080ULL,   0x4008142004410100ULL, 0x2060820c0120200ULL,
  0x1001004080100ULL,    0x20c020080040080ULL,  0x2935610830022400ULL, 0x44440041009200ULL,
  0x280001040802101ULL,  0x2100190040002085ULL, 0x80c0084100102001ULL, 0x4024081001000421ULL,
  0x20030a0244872ULL,    0x12001008414402ULL,   0x2006104900a0804ULL,  0x1004081002402ULL,
};

static constexpr Bitboard BishopMagicNumbers[SQUARE_NB] = {
  0x40040844404084ULL,   0x2004208a004208ULL,   0x10190041080202ULL,   0x108060845042010ULL,
  0x581104180800210ULL,  0x2112080446200010ULL, 0x1080820820060210ULL, 0x3c0808410220200ULL,
  0x4050404440404ULL,    0x21001420088ULL,      0x24d0080801082102ULL, 0x1020a0a020400ULL,
  0x40308200402ULL,      0x4011002100800ULL,    0x401484104104005ULL,  0x801010402020200ULL,
  0x400210c3880100ULL,   0x404022024108200ULL,  0x810018200204102ULL,  0x4002801a02003ULL,
  0x85040820080400ULL,   0x810102c808880400ULL, 0xe900410884800ULL,    0x8002020480840102ULL,
  0x220200865090201ULL,  0x2010100a02021202ULL, 0x152048408022401ULL,  0x20080002081110ULL,
  0x4001001021004000ULL, 0x800040400a011002ULL, 0xe4004081011002ULL,   0x1c004001012080ULL,
  0x8004200962a00220ULL, 0x8422100208500202ULL, 0x2000402200300c08ULL, 0x8646020080080080ULL,
  0x80020a0200100808ULL, 0x2010004880111000ULL, 0x623000a080011400ULL, 0x42008c0340209202ULL,
  0x209188240001000ULL,  0x400408a884001800ULL, 0x110400a6080400ULL,   0x1840060a44020800ULL,
  0x90080104000041ULL,   0x201011000808101ULL,  0x1a2208080504f080ULL, 0x8012020600211212ULL,
  0x500861011240000ULL,  0x180806108200800ULL,  0x4000020e01040044ULL, 0x300000261044000aULL,
  0x802241102020002ULL,  0x20906061210001ULL,   0x5a84841004010310ULL, 0x4010801011c04ULL,
  0xa010109502200ULL,    0x4a02012000ULL,       0x500201010098b028ULL, 0x8040002811040900ULL,
  0x28000010020204ULL,   0x6000020202d0240ULL,  0x8918844842082200ULL, 0x4010011029020020ULL,
};

template <PieceType Pt, typename MagicT>
static void init_magic_table_impl(MagicT* magics, Bitboard* table, const Bitboard* magicNumbers) {
  Bitboard* ptr = table;

  for (Square sq = SQ_A1; sq < SQUARE_NB; ++sq) {
    MagicT& m = magics[sq];
    m.mask = sliding_mask<Pt>(sq);
    m.attacks = ptr;

    int bits = popcount(m.mask);
    int n = 1u << bits;

    if constexpr (std::is_same_v<MagicT, MagicPext>) {
      m.shift = uint8_t(bits);
    } else {
      m.magic = magicNumbers[sq];
      m.shift = uint8_t(64 - bits);
    }

    for (int i = 0; i < n; ++i) {
      Bitboard occ = 0;
      Bitboard temp = m.mask;
      for (int j = 0; j < bits; ++j) {
        Square sq2 = pop_lsb(temp);
        if (i & (1u << j))
          occ |= square_bb(sq2);
      }
      m.attacks[m.index(occ)] = sliding_attack<Pt>(sq, occ);
    }
    ptr += n;
  }
}

static void init_magics() {
  bool bmi2 = cpu_has_bmi2();

  if (bmi2) {
    init_magic_table_impl<ROOK>(RookMagicsPext, RookTable, nullptr);
    init_magic_table_impl<BISHOP>(BishopMagicsPext, BishopTable, nullptr);
    g_rook_attacks = rook_attacks_pext;
    g_bishop_attacks = bishop_attacks_pext;
  } else {
    init_magic_table_impl<ROOK>(RookMagicsMul, RookTable, RookMagicNumbers);
    init_magic_table_impl<BISHOP>(BishopMagicsMul, BishopTable, BishopMagicNumbers);
    g_rook_attacks = rook_attacks_mul;
    g_bishop_attacks = bishop_attacks_mul;
  }

  assert(g_rook_attacks != nullptr);
  assert(g_bishop_attacks != nullptr);
}

static void init_attacks() {
  std::memset(PawnAttacks, 0, sizeof(PawnAttacks));
  std::memset(PawnPushes, 0, sizeof(PawnPushes));
  std::memset(PawnDoublePushes, 0, sizeof(PawnDoublePushes));
  std::memset(KnightAttacks, 0, sizeof(KnightAttacks));
  std::memset(KingAttacks, 0, sizeof(KingAttacks));
  std::memset(BetweenBB, 0, sizeof(BetweenBB));
  std::memset(LineBB, 0, sizeof(LineBB));
  std::memset(PseudoAttacks, 0, sizeof(PseudoAttacks));

  for (Square sq = SQ_A1; sq < SQUARE_NB; ++sq) {
    PawnAttacks[WHITE][sq] = pawn_attacks_bb(WHITE, sq);
    PawnAttacks[BLACK][sq] = pawn_attacks_bb(BLACK, sq);

    Bitboard b = square_bb(sq);

    if (rankOf(sq) < RANK_8) {
      PawnPushes[WHITE][sq] = shift_north(b);
      if (rankOf(sq) == RANK_2)
        PawnDoublePushes[WHITE][sq] = shift_north(shift_north(b));
    }

    if (rankOf(sq) > RANK_1) {
      PawnPushes[BLACK][sq] = shift_south(b);
      if (rankOf(sq) == RANK_7)
        PawnDoublePushes[BLACK][sq] = shift_south(shift_south(b));
    }

    KnightAttacks[sq] = knight_attacks_bb(sq);
    KingAttacks[sq] = king_attacks_bb(sq);

    PseudoAttacks[KNIGHT][sq] = KnightAttacks[sq];
    PseudoAttacks[KING][sq] = KingAttacks[sq];
    PseudoAttacks[BISHOP][sq] = sliding_attack<BISHOP>(sq, 0);
    PseudoAttacks[ROOK][sq] = sliding_attack<ROOK>(sq, 0);
    PseudoAttacks[QUEEN][sq] = PseudoAttacks[BISHOP][sq] | PseudoAttacks[ROOK][sq];
  }

  for (Square s1 = SQ_A1; s1 < SQUARE_NB; ++s1) {
    for (Square s2 = SQ_A1; s2 < SQUARE_NB; ++s2) {
      if (s1 == s2)
        continue;

      Bitboard sqs = square_bb(s1) | square_bb(s2);

      if (fileOf(s1) == fileOf(s2) || rankOf(s1) == rankOf(s2)) {
        LineBB[s1][s2] = (PseudoAttacks[ROOK][s1] & PseudoAttacks[ROOK][s2]) | sqs;
        BetweenBB[s1][s2] = sliding_attack<ROOK>(s1, sqs) & sliding_attack<ROOK>(s2, sqs);
      } else if (diagonal_of(s1) == diagonal_of(s2)
                 || anti_diagonal_of(s1) == anti_diagonal_of(s2)) {
        LineBB[s1][s2] = (PseudoAttacks[BISHOP][s1] & PseudoAttacks[BISHOP][s2]) | sqs;
        BetweenBB[s1][s2] = sliding_attack<BISHOP>(s1, sqs) & sliding_attack<BISHOP>(s2, sqs);
      }
    }
  }
}

void init_bitboards() {
  init_magics();
  init_attacks();
}

} // namespace Catalyst
