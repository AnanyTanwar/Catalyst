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

#include "board.h"
#include "types.h"

#include <span>

namespace Catalyst {

// Generates pseudo-legal moves into `list`, returns one-past-end.
template <GenType GT> std::span<Move> generate(const Board &board, Move *list);

extern template std::span<Move> generate<ALL_MOVES>(const Board &, Move *);
extern template std::span<Move> generate<CAPTURES>(const Board &, Move *);
extern template std::span<Move> generate<QUIETS>(const Board &, Move *);

// Filters generate<ALL_MOVES>() through Board::is_legal() - convenience wrapper.
MoveList generate_legal(Board &board);

// Counts legal moves without allocating a MoveList - cheap stalemate/checkmate checks.
int count_legal(Board &board);

}  // namespace Catalyst