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

ThreadPool::ThreadPool(int numThreads)
    : main_(std::make_unique<Search>())
{
    set_threads(numThreads);
}

void ThreadPool::set_threads(int n)
{
    helpers_.clear();
    int numHelpers = std::max(0, n - 1);
    helpers_.reserve(numHelpers);
    for (int i = 0; i < numHelpers; ++i)
    {
        auto &h              = helpers_.emplace_back();
        h.searcher           = std::make_unique<Search>();
        h.board              = std::make_unique<Board>();
        h.searcher->isSilent = true;
    }
}

void ThreadPool::stop()
{
    main_->stop();
}

void ThreadPool::clear_all()
{
    main_->clear_tables();
    for (auto &h : helpers_)
        h.searcher->clear_tables();
}

uint64_t ThreadPool::total_nodes() const
{
    uint64_t total = main_->nodes();
    for (const auto &h : helpers_)
        total += h.searcher->nodes();
    return total;
}

Search *ThreadPool::best_thread()
{
    Search *best = main_.get();

    for (auto &h : helpers_)
    {
        Search *cand = h.searcher.get();

        // A thread with no move found is never preferred.
        if (cand->best_move_found() == MOVE_NONE)
            continue;
        if (best->best_move_found() == MOVE_NONE)
        {
            best = cand;
            continue;
        }

        const int  candScore = cand->last_score();
        const int  bestScore = best->last_score();
        const bool candMate  = std::abs(candScore) >= SCORE_MATE_IN_MAX_PLY;
        const bool bestMate  = std::abs(bestScore) >= SCORE_MATE_IN_MAX_PLY;

        if (candMate || bestMate)
        {
            // Winning mate beats everything; between two winning mates,
            // shorter wins; a winning mate beats a losing/non-mate score.
            if (candMate != bestMate)
            {
                // Prefer whichever one is mate, but only if it's a WINNING
                // mate for us (positive score) — a found losing mate isn't
                // automatically better than a merely bad non-mate score.
                if (candMate && candScore > 0 && (!bestMate || bestScore <= 0))
                {
                    best = cand;
                    continue;
                }
                if (bestMate && bestScore > 0)
                    continue;  // keep best
                if (candMate && candScore > 0)
                {
                    best = cand;
                    continue;
                }
            }
            else if (candMate && bestMate && candScore > 0 && bestScore > 0)
            {
                // Both found winning mates — shorter (higher score, since
                // mate scores shrink with distance) wins.
                if (candScore > bestScore)
                    best = cand;
                continue;
            }
        }

        // Neither side is a clearly-winning mate case handled above —
        // fall through to depth/score/node comparison.
        const int candDepth = cand->depth();
        const int bestDepth = best->depth();

        if (candDepth != bestDepth)
        {
            // Deeper search wins outright unless the shallower one's score
            // is meaningfully higher (by more than a small margin), in
            // which case we trust the score over raw depth.
            constexpr int SCORE_OVERRIDE_MARGIN = 30;
            if (candDepth > bestDepth)
            {
                if (bestScore > candScore + SCORE_OVERRIDE_MARGIN)
                    continue;  // shallower 'best' had a much better score — keep it
                best = cand;
                continue;
            }
            else
            {
                if (candScore > bestScore + SCORE_OVERRIDE_MARGIN)
                    best = cand;
                continue;
            }
        }

        // Same depth — prefer the better score, tie-break on nodes searched.
        if (candScore > bestScore)
        {
            best = cand;
        }
        else if (candScore == bestScore && cand->nodes() > best->nodes())
        {
            best = cand;
        }
    }

    return best;
}

Move ThreadPool::search(Board &board, TimeManager &tm)
{
    main_->stopped.store(false, std::memory_order_relaxed);
    for (auto &h : helpers_)
        h.searcher->stopped.store(false, std::memory_order_relaxed);

    for (auto &h : helpers_)
        h.board->copy_from(board);

    // shared atomic node counter
    std::atomic<uint64_t> sharedNodes { 0 };
    main_->sharedNodes_ = &sharedNodes;
    for (auto &h : helpers_)
        h.searcher->sharedNodes_ = &sharedNodes;

    std::vector<std::thread> threads;
    for (auto &h : helpers_)
    {
        Search *s = h.searcher.get();
        Board  *b = h.board.get();
        threads.emplace_back([s, b, &tm]() { s->best_move(*b, tm); });
    }

    // Main thread still drives time management and prints "info" lines as
    // it goes — that behaviour is unchanged. We just stop trusting its
    // result blindly once everyone has finished.
    main_->best_move(board, tm);

    for (auto &h : helpers_)
        h.searcher->stopped.store(true, std::memory_order_relaxed);
    tm.stop();

    for (auto &t : threads)
        t.join();

    // clear pointers — sharedNodes goes out of scope after this
    main_->sharedNodes_ = nullptr;
    for (auto &h : helpers_)
        h.searcher->sharedNodes_ = nullptr;

    // Now that every thread has fully stopped, pick the strongest result.
    // All threads have joined at this point, so it's safe to read their
    // final info_ state without synchronization.
    Search *winner = best_thread();
    lastWinner_    = winner;
    return winner->best_move_found();
}

}  // namespace Catalyst
