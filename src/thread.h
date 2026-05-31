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
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace Catalyst {

// Manages a pool of search threads for SMP (symmetric multiprocessing) search.
// Thread 0 is the "main" thread — it outputs info and provides the best move.
class ThreadPool {
public:
    std::atomic<bool> stop { false };  // signals all threads to stop searching

    explicit ThreadPool(int numThreads = 1);
    ~ThreadPool();

    // Resize the pool, cleanly stopping and restarting all workers.
    void set_threads(int n);

    // Start a parallel search and block until thread 0 finishes, then return the best move.
    Move search(Board &board, TimeManager &tm);

    void stop_search();    // request an early stop (sets stop = true)
    void wait_for_idle();  // block until all workers finish their current search

    void clear_all();  // reset search history tables across all threads

    uint64_t total_nodes() const;  // sum of nodes searched across all threads

    Move ponder_move() const;

    int     thread_count() const { return static_cast<int>(workers_.size()); }
    Search &main_search() { return *workers_[0]->searcher; }

private:
    // Per-thread state: each worker owns its own searcher, board copy, and OS thread.
    struct Worker {
        std::unique_ptr<Search> searcher;
        std::unique_ptr<Board>  board;  // local copy of the root position

        std::mutex              mutex;
        std::condition_variable cv;

        bool searching = false;  // true while the worker is actively searching
        bool exiting   = false;  // true when the pool is being torn down

        std::unique_ptr<std::thread> thread;
    };

    std::vector<std::unique_ptr<Worker>> workers_;

    std::atomic<uint64_t> sharedNodes_ { 0 };  // node counter shared across all threads

    Board       *rootBoard_ = nullptr;
    TimeManager *rootTm_    = nullptr;

    // Used to wake the UCI thread once thread 0 finishes.
    std::mutex              mainMutex_;
    std::condition_variable mainCv_;

    void spawn_worker(int idx);  // create and start a single worker thread
    void idle_loop(int idx);     // worker thread entry point — waits then searches

    // Pick the thread that searched deepest as the source of the best move.
    const Search *best_thread() const;
};

}
