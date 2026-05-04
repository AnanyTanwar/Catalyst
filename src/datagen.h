// Catalyst is a UCI compliant chess engine
// Copyright (C) 2026 Anany Tanwar
//
// Catalyst is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Catalyst is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.

#pragma once

#include "board.h"
#include "types.h"

#include <cstdint>
#include <string>

namespace Catalyst {
namespace Datagen {

    enum class Outcome : uint8_t {
        BlackWin = 0,
        Draw     = 1,
        WhiteWin = 2,
        Invalid  = 3,
    };

    struct PackedBoard {
        uint64_t occ;
        uint8_t  pieces[16];
        uint8_t  stm_ep;
        uint8_t  halfmoves;
        uint16_t fullmoves;
        int16_t  eval;
        Outcome  outcome;
        uint8_t  pad;

        static PackedBoard pack(const Board &board, int16_t score, Outcome outcome);
    };
    static_assert(sizeof(PackedBoard) == 32, "PackedBoard must be exactly 32 bytes");

    struct ViriMove {
        uint16_t move;
        int16_t  score;

        static ViriMove from_move(Move m, int score);
        static ViriMove sentinel() { return { 0, 0 }; }
    };
    static_assert(sizeof(ViriMove) == 4, "ViriMove must be exactly 4 bytes");

    struct DatagenConfig {
        std::string output_path = "catalyst_data.vf";
        std::string book_path   = "";

        int threads = 4;
        int games   = 0;

        uint64_t soft_nodes = 5000;
        uint64_t hard_nodes = 20000;

        int random_plies_min = 10;
        int random_plies_max = 20;

        int verify_depth = 8;
        int verify_limit = 1000;

        int win_adj_score    = 1000;
        int win_adj_plies    = 5;
        int draw_adj_score   = 10;
        int draw_adj_min_ply = 80;
        int draw_adj_plies   = 12;

        int max_game_ply = 400;
        int score_clamp  = 3000;

        bool dfrc = false;
    };

    DatagenConfig parse_config(const std::string &args);
    void          run(const DatagenConfig &cfg);

}  // namespace Datagen
}  // namespace Catalyst