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

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "board.h"
#include "search.h"
#include "timeman.h"
#include "types.h"

namespace Catalyst {

class ThreadPool {
public:
  explicit ThreadPool(int numThreads = 1);
  ~ThreadPool() = default;

  void set_threads(int n);
  Move search(Board& board, TimeManager& tm);
  void stop();
  void clear_all();
  uint64_t total_nodes() const;
  Move ponder_move() const { return main_->ponder_move(); }
  int thread_count() const { return int(helpers_.size()) + 1; }

private:
  std::unique_ptr<Search> main_;

  struct Helper {
    std::unique_ptr<Search> searcher;
    std::unique_ptr<Board> board;
  };
  std::vector<Helper> helpers_;
};

} // namespace Catalyst
