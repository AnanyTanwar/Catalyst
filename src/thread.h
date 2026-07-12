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
#include "search.h"
#include "timeman.h"
#include "types.h"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace Catalyst {

class ThreadPool {
public:
    explicit ThreadPool(int numThreads = 1);
    ~ThreadPool() = default;

    void     set_threads(int n);
    Move     search(Board &board, TimeManager &tm);
    void     stop();
    void     clear_all();
    uint64_t total_nodes() const;

    // Ponder move is read from whichever thread's line was actually
    // returned as bestmove by the most recent search() call — NOT always
    // main_, since best_thread() may have picked a helper.
    Move ponder_move() const { return lastWinner_ ? lastWinner_->ponder_move() : MOVE_NONE; }
    int  thread_count() const { return int(helpers_.size()) + 1; }

private:
    std::unique_ptr<Search> main_;

    struct Helper {
        std::unique_ptr<Search> searcher;
        std::unique_ptr<Board>  board;
    };
    std::vector<Helper> helpers_;

    // Tracks whichever Search instance won the most recent search() call,
    // so ponder_move() can pull from the correct thread's PV.
    Search *lastWinner_ = nullptr;

    // Picks the strongest result across main_ + all helpers after a search
    // completes. Returns a pointer to the winning Search instance.
    Search *best_thread();
};

}  // namespace Catalyst