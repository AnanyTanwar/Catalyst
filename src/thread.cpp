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
#include <cassert>

namespace Catalyst {

ThreadPool::ThreadPool(int numThreads)
{
    set_threads(numThreads);
}

ThreadPool::~ThreadPool()
{
    // Signal all workers to exit, then wake and join them.
    for (auto &w : workers_)
    {
        std::lock_guard<std::mutex> lock(w->mutex);
        w->exiting   = true;
        w->searching = true;  // unblocks the cv.wait in idle_loop
    }
    for (auto &w : workers_)
        w->cv.notify_all();
    for (auto &w : workers_)
        if (w->thread && w->thread->joinable())
            w->thread->join();
}

void ThreadPool::set_threads(int n)
{
    n = std::max(1, n);

    if (!workers_.empty())
    {
        wait_for_idle();

        // Tear down existing workers before rebuilding.
        for (auto &w : workers_)
        {
            {
                std::lock_guard<std::mutex> lock(w->mutex);
                w->exiting   = true;
                w->searching = true;
            }
            w->cv.notify_all();
        }
        for (auto &w : workers_)
            if (w->thread && w->thread->joinable())
                w->thread->join();

        workers_.clear();
    }

    workers_.reserve(n);
    for (int i = 0; i < n; ++i)
        spawn_worker(i);
}

void ThreadPool::spawn_worker(int idx)
{
    auto &w     = workers_.emplace_back(std::make_unique<Worker>());
    w->searcher = std::make_unique<Search>(idx, &stop);
    w->board    = std::make_unique<Board>();

    // Only thread 0 prints UCI output; helpers run silently.
    w->searcher->isSilent = (idx != 0);

    w->thread = std::make_unique<std::thread>([this, idx]() { idle_loop(idx); });
}

// Worker thread entry point. Sleeps until a search is requested, runs it, then sleeps again.
void ThreadPool::idle_loop(int idx)
{
    Worker &w = *workers_[idx];

    while (true)
    {
        std::unique_lock<std::mutex> lock(w.mutex);
        w.cv.wait(lock, [&w] { return w.searching; });

        if (w.exiting)
            return;

        lock.unlock();

        w.searcher->best_move(*w.board, *rootTm_);

        {
            std::lock_guard<std::mutex> lg(w.mutex);
            w.searching = false;
        }
        w.cv.notify_all();

        // Thread 0 also wakes the UCI caller waiting in search().
        if (idx == 0)
        {
            std::lock_guard<std::mutex> lg(mainMutex_);
            mainCv_.notify_all();
        }
    }
}

void ThreadPool::wait_for_idle()
{
    for (auto &w : workers_)
    {
        std::unique_lock<std::mutex> lock(w->mutex);
        w->cv.wait(lock, [&w] { return !w->searching; });
    }
}

Move ThreadPool::search(Board &board, TimeManager &tm)
{
    wait_for_idle();

    stop.store(false, std::memory_order_seq_cst);
    sharedNodes_.store(0, std::memory_order_relaxed);

    // Give every worker a fresh copy of the root position.
    for (auto &w : workers_)
        w->board->copy_from(board);

    rootTm_ = &tm;
    for (auto &w : workers_)
        w->searcher->sharedNodes_ = &sharedNodes_;

    // Wake all workers simultaneously.
    for (auto &w : workers_)
    {
        std::lock_guard<std::mutex> lock(w->mutex);
        w->searching = true;
    }
    for (auto &w : workers_)
        w->cv.notify_all();

    // Block the UCI thread until thread 0 finishes (it will signal mainCv_).
    {
        std::unique_lock<std::mutex> lock(mainMutex_);
        mainCv_.wait(lock, [this] { return !workers_[0]->searching; });
    }

    // Tell remaining threads to stop and wait for them to settle.
    stop.store(true, std::memory_order_seq_cst);
    wait_for_idle();

    for (auto &w : workers_)
        w->searcher->sharedNodes_ = nullptr;

    return best_thread()->best_move_result();
}

/// Return the searcher whose result is most trustworthy.
// Prefer mate finders first, then deepest search that isn't significantly worse in score.
const Search *ThreadPool::best_thread() const
{
    const Search *best = workers_[0]->searcher.get();
    for (size_t i = 1; i < workers_.size(); ++i)
    {
        const Search *s      = workers_[i]->searcher.get();
        int           sBest  = s->last_score();
        int           bBest  = best->last_score();
        int           sDepth = s->completed_depth();
        int           bDepth = best->completed_depth();

        bool sMate = std::abs(sBest) >= SCORE_MATE_IN_MAX_PLY;
        bool bMate = std::abs(bBest) >= SCORE_MATE_IN_MAX_PLY;

        if (sMate != bMate)
        {
            if (sMate)
                best = s;
            continue;
        }
        if (sDepth > bDepth && sBest >= bBest - 50)
            best = s;
    }
    return best;
}

void ThreadPool::stop_search()
{
    stop.store(true, std::memory_order_seq_cst);
}

void ThreadPool::clear_all()
{
    wait_for_idle();
    for (auto &w : workers_)
        w->searcher->clear_tables();
}

uint64_t ThreadPool::total_nodes() const
{
    return sharedNodes_.load(std::memory_order_relaxed);
}

Move ThreadPool::ponder_move() const
{
    return best_thread()->ponder_move();
}

}
