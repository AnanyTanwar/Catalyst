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

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

#include "board.h"
#include "simd.h"
#include "types.h"

namespace Catalyst {
namespace NNUE {

    // Architecture: (768 -> HIDDEN_SIZE)x2 -> 1
    inline constexpr int HIDDEN_SIZE    = 64;
    inline constexpr int INPUT_SIZE     = 768;
    inline constexpr int QA             = 255;
    inline constexpr int QB             = 64;
    inline constexpr int SCALE          = 400;
    inline constexpr int ACC_STACK_SIZE = MAX_PLY + 8;

    struct alignas(64) Accumulator {
        std::array<int16_t, HIDDEN_SIZE> vals;

        void init(const int16_t *bias) {
            std::memcpy(vals.data(), bias, HIDDEN_SIZE * sizeof(int16_t));
        }
        void add(int idx, const int16_t *weights) {
            const int16_t *col = weights + idx * HIDDEN_SIZE;
            for (int i = 0; i < HIDDEN_SIZE; ++i)
                vals[i] += col[i];
        }
        void remove(int idx, const int16_t *weights) {
            const int16_t *col = weights + idx * HIDDEN_SIZE;
            for (int i = 0; i < HIDDEN_SIZE; ++i)
                vals[i] -= col[i];
        }
        void copy_from(const Accumulator &o) {
            std::memcpy(vals.data(), o.vals.data(), HIDDEN_SIZE * sizeof(int16_t));
        }
    };

    struct AccumulatorPair {
        Accumulator white_acc;
        Accumulator black_acc;
    };

    struct AccumulatorStack {
        std::array<AccumulatorPair, ACC_STACK_SIZE> stack;
        int                                         top = 0;

        AccumulatorPair       &current() { return stack[top]; }
        const AccumulatorPair &current() const { return stack[top]; }

        void push() {
            stack[top + 1] = stack[top];
            ++top;
        }
        void pop() { --top; }
    };

    struct alignas(64) Network {
        std::array<int16_t, INPUT_SIZE * HIDDEN_SIZE> feature_weights;
        std::array<int16_t, HIDDEN_SIZE>              feature_bias;
        std::array<int16_t, 2 * HIDDEN_SIZE>          output_weights;
        int16_t                                       output_bias;
    };

    extern Network g_network;

    bool load(const std::string &path);

    [[nodiscard]] inline int white_idx(Color piece_color, PieceType pt, Square sq) {
        return ((piece_color == BLACK) ? 384 : 0) + (int(pt) - 1) * 64 + int(sq);
    }

    [[nodiscard]] inline int black_idx(Color piece_color, PieceType pt, Square sq) {
        return ((piece_color == WHITE) ? 384 : 0) + (int(pt) - 1) * 64 + (int(sq) ^ 56);
    }

    void refresh(const Board &board, AccumulatorPair &pair);

    void acc_add_piece(AccumulatorPair &pair, Color piece_color, PieceType pt, Square sq);
    void acc_remove_piece(AccumulatorPair &pair, Color piece_color, PieceType pt, Square sq);
    void acc_move_piece(
        AccumulatorPair &pair, Color piece_color, PieceType pt, Square from, Square to);

    void push_move(AccumulatorStack &stack,
        const Board                 &board,
        Move                         m,
        Color                        prev_stm,
        Piece                        moved_piece,
        Piece                        captured_piece);

    [[nodiscard]] int evaluate(const AccumulatorPair &pair);
    [[nodiscard]] int evaluate(const AccumulatorStack &stack, Color stm);
    [[nodiscard]] int evaluate(const Board &board);

}  // namespace NNUE
}  // namespace Catalyst