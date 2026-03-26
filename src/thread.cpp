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

#include "thread.h"
#include <algorithm>
#include <thread>
#include <vector>

namespace Catalyst {

ThreadPool::ThreadPool(int numThreads) : main_(std::make_unique<Search>()) {
  set_threads(numThreads);
}

void ThreadPool::set_threads(int n) {
  helpers_.clear();
  int numHelpers = std::max(0, n - 1);
  helpers_.reserve(numHelpers);
  for (int i = 0; i < numHelpers; ++i) {
    auto& h = helpers_.emplace_back();
    h.searcher = std::make_unique<Search>();
    h.board = std::make_unique<Board>();
    h.searcher->isSilent = true;
  }
}

void ThreadPool::stop() {
  main_->stop();
}

void ThreadPool::clear_all() {
  main_->clear_tables();
  for (auto& h : helpers_)
    h.searcher->clear_tables();
}

uint64_t ThreadPool::total_nodes() const {
  uint64_t total = main_->nodes();
  for (const auto& h : helpers_)
    total += h.searcher->nodes();
  return total;
}

Move ThreadPool::search(Board& board, TimeManager& tm) {
  // Reset stop flags
  main_->stopped.store(false, std::memory_order_relaxed);
  for (auto& h : helpers_)
    h.searcher->stopped.store(false, std::memory_order_relaxed);

  for (auto& h : helpers_)
    h.board->copy_from(board);

  std::vector<std::thread> threads;
  for (auto& h : helpers_) {
    Search* s = h.searcher.get();
    Board* b = h.board.get();
    threads.emplace_back([s, b, &tm]() { s->best_move(*b, tm); });
  }

  Move best = main_->best_move(board, tm);

  // Stop ALL helpers via their own stopped flag
  for (auto& h : helpers_)
    h.searcher->stopped.store(true, std::memory_order_relaxed);
  // Also stop TM so tm_->time_up() returns true as backup
  tm.stop();

  for (auto& t : threads)
    t.join();

  return best;
}

} // namespace Catalyst
