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

#include "nnue.h"
#include <iostream>
#ifndef NNUE_EMBEDDED
#include <fstream>
#endif
#include "bitboard.h"
#include "board.h"
#include "simd.h"

#ifdef NNUE_EMBEDDED
extern "C" {
extern const uint8_t _binary_catalyst_v2_nnue_start[];
extern const uint8_t _binary_catalyst_v2_nnue_end[];
}
#endif

namespace Catalyst {
namespace NNUE {

    alignas(64) Network g_network;

    bool load([[maybe_unused]] const std::string &path) {
        constexpr size_t expected = INPUT_SIZE * HIDDEN_SIZE * sizeof(int16_t)
            + HIDDEN_SIZE * sizeof(int16_t) + OUTPUT_BUCKETS * 2 * HIDDEN_SIZE * sizeof(int16_t)
            + OUTPUT_BUCKETS * sizeof(int16_t);

#ifdef NNUE_EMBEDDED
        const uint8_t *data = _binary_catalyst_v2_nnue_start;
        const size_t   size
            = static_cast<size_t>(_binary_catalyst_v2_nnue_end - _binary_catalyst_v2_nnue_start);

        if (size < expected)
        {
            std::cerr << "NNUE: embedded net truncated (" << size << " bytes, need " << expected
                      << ")\n";
            return false;
        }

        size_t offset = 0;
        auto   read   = [&](void *dst, size_t n) {
            std::memcpy(dst, data + offset, n);
            offset += n;
        };

        read(g_network.feature_weights.data(), INPUT_SIZE * HIDDEN_SIZE * sizeof(int16_t));
        read(g_network.feature_bias.data(), HIDDEN_SIZE * sizeof(int16_t));
        read(g_network.output_weights.data(), OUTPUT_BUCKETS * 2 * HIDDEN_SIZE * sizeof(int16_t));
        read(g_network.output_bias.data(), OUTPUT_BUCKETS * sizeof(int16_t));

        std::cout << "NNUE: loaded embedded net v2 (" << size << " bytes)\n";
        return true;
#else
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            std::cerr << "NNUE: failed to open " << path << "\n";
            return false;
        }

        file.read(reinterpret_cast<char *>(g_network.feature_weights.data()),
            INPUT_SIZE * HIDDEN_SIZE * sizeof(int16_t));
        file.read(
            reinterpret_cast<char *>(g_network.feature_bias.data()), HIDDEN_SIZE * sizeof(int16_t));
        file.read(reinterpret_cast<char *>(g_network.output_weights.data()),
            OUTPUT_BUCKETS * 2 * HIDDEN_SIZE * sizeof(int16_t));
        file.read(reinterpret_cast<char *>(g_network.output_bias.data()),
            OUTPUT_BUCKETS * sizeof(int16_t));

        if (!file)
        {
            std::cerr << "NNUE: read error / truncated: " << path << "\n";
            return false;
        }

        std::cout << "NNUE: loaded " << path << " (v2 arch, HS=" << HIDDEN_SIZE
                  << " buckets=" << OUTPUT_BUCKETS << ")\n";
        return true;
#endif
    }

    void refresh(const Board &board, AccumulatorPair &pair) {
        SIMD::simd_init_accumulator<HIDDEN_SIZE>(
            pair.white_acc.vals.data(), g_network.feature_bias.data());
        SIMD::simd_init_accumulator<HIDDEN_SIZE>(
            pair.black_acc.vals.data(), g_network.feature_bias.data());

        const int16_t *fw = g_network.feature_weights.data();

        for (PieceType pt = PAWN; pt <= KING; ++pt)
        {
            for (Color c = WHITE; c <= BLACK; ++c)
            {
                Bitboard bb = board.pieces(pt, c);
                while (bb)
                {
                    Square sq = Square(pop_lsb(bb));
                    SIMD::simd_add_weights<HIDDEN_SIZE>(
                        pair.white_acc.vals.data(), fw + white_idx(c, pt, sq) * HIDDEN_SIZE);
                    SIMD::simd_add_weights<HIDDEN_SIZE>(
                        pair.black_acc.vals.data(), fw + black_idx(c, pt, sq) * HIDDEN_SIZE);
                }
            }
        }
    }

    void acc_add_piece(AccumulatorPair &pair, Color piece_color, PieceType pt, Square sq) {
        const int16_t *fw = g_network.feature_weights.data();
        SIMD::simd_add_weights<HIDDEN_SIZE>(
            pair.white_acc.vals.data(), fw + white_idx(piece_color, pt, sq) * HIDDEN_SIZE);
        SIMD::simd_add_weights<HIDDEN_SIZE>(
            pair.black_acc.vals.data(), fw + black_idx(piece_color, pt, sq) * HIDDEN_SIZE);
    }

    void acc_remove_piece(AccumulatorPair &pair, Color piece_color, PieceType pt, Square sq) {
        const int16_t *fw = g_network.feature_weights.data();
        SIMD::simd_sub_weights<HIDDEN_SIZE>(
            pair.white_acc.vals.data(), fw + white_idx(piece_color, pt, sq) * HIDDEN_SIZE);
        SIMD::simd_sub_weights<HIDDEN_SIZE>(
            pair.black_acc.vals.data(), fw + black_idx(piece_color, pt, sq) * HIDDEN_SIZE);
    }

    void acc_move_piece(
        AccumulatorPair &pair, Color piece_color, PieceType pt, Square from, Square to) {
        const int16_t *fw = g_network.feature_weights.data();
        SIMD::simd_add_sub_weights<HIDDEN_SIZE>(pair.white_acc.vals.data(),
            fw + white_idx(piece_color, pt, to) * HIDDEN_SIZE,
            fw + white_idx(piece_color, pt, from) * HIDDEN_SIZE);
        SIMD::simd_add_sub_weights<HIDDEN_SIZE>(pair.black_acc.vals.data(),
            fw + black_idx(piece_color, pt, to) * HIDDEN_SIZE,
            fw + black_idx(piece_color, pt, from) * HIDDEN_SIZE);
    }

    void push_move(AccumulatorStack &stack,
        const Board                 &board,
        Move                         m,
        Color                        prev_stm,
        Piece                        moved_piece,
        Piece                        captured_piece) {
        stack.push();
        AccumulatorPair &pair = stack.current();

        Square    from = from_sq(m);
        Square    to   = to_sq(m);
        PieceType pt   = piece_type(moved_piece);
        Color     us   = prev_stm;

        if (is_castling(m))
        {
            CastlingRights cr = (us == WHITE) ? (to > from ? WHITE_OO : WHITE_OOO)
                                              : (to > from ? BLACK_OO : BLACK_OOO);
            acc_move_piece(pair, us, KING, from, CASTLING_DATA[cr].kingDest);
            acc_move_piece(pair, us, ROOK, CASTLING_DATA[cr].rookSrc, CASTLING_DATA[cr].rookDest);
        }
        else if (is_en_passant(m))
        {
            Square capsq = (us == WHITE) ? Square(int(to) - 8) : Square(int(to) + 8);
            acc_move_piece(pair, us, PAWN, from, to);
            acc_remove_piece(pair, ~us, PAWN, capsq);
        }
        else if (is_promotion(m))
        {
            acc_remove_piece(pair, us, PAWN, from);
            if (captured_piece != NO_PIECE)
                acc_remove_piece(pair, ~us, piece_type(captured_piece), to);
            acc_add_piece(pair, us, promo_piece(m), to);
        }
        else
        {
            if (captured_piece != NO_PIECE)
                acc_remove_piece(pair, ~us, piece_type(captured_piece), to);
            acc_move_piece(pair, us, pt, from, to);
        }
    }

    int evaluate(const AccumulatorPair &pair, const Board &board) {
        const Color stm    = board.side_to_move();
        const int   bucket = output_bucket(board);

        const int16_t *ow_base = g_network.output_weights.data() + bucket * 2 * HIDDEN_SIZE;
        const int16_t *acc_stm
            = (stm == WHITE) ? pair.white_acc.vals.data() : pair.black_acc.vals.data();
        const int16_t *acc_nstm
            = (stm == WHITE) ? pair.black_acc.vals.data() : pair.white_acc.vals.data();

        int64_t output = 0;
        output += static_cast<int64_t>(SIMD::simd_screlu_forward<HIDDEN_SIZE>(acc_stm, ow_base));
        output += static_cast<int64_t>(
            SIMD::simd_screlu_forward<HIDDEN_SIZE>(acc_nstm, ow_base + HIDDEN_SIZE));

        output /= static_cast<int64_t>(QA);
        output += static_cast<int64_t>(g_network.output_bias[bucket]);
        output *= static_cast<int64_t>(SCALE);
        output /= static_cast<int64_t>(QA) * static_cast<int64_t>(QB);

        return static_cast<int>(output);
    }

    int evaluate(const AccumulatorStack &stack, const Board &board, Color stm) {
        return evaluate(stack.current(), board);
    }

    int evaluate(const Board &board) {
        AccumulatorPair pair;
        refresh(board, pair);
        return evaluate(pair, board);
    }

}  // namespace NNUE
}  // namespace Catalyst
