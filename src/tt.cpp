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

#include "tt.h"

#include <algorithm>
#include <climits>
#include <iostream>
#include <thread>
#include <vector>

#if defined(__AVX2__) || defined(__SSE4_2__)
#include <immintrin.h>
#endif

namespace Catalyst {

TT tt;

TT::TT() {
    resize(64);
}

TT::~TT() {
#if defined(_WIN32)
    _aligned_free(table);
#else
    free(table);
#endif
}

void TT::resize(size_t mb) {
    mb = std::max(mb, size_t(1));

    size_t bytes = mb * 1024 * 1024;
    size_t count = bytes / sizeof(TTCluster);

    size_t power2 = 1;
    while (power2 * 2 <= count)
        power2 *= 2;

    numClusters = power2;
    clusterMask = power2 - 1;

    if (table)
    {
#if defined(_WIN32)
        _aligned_free(table);
#else
        free(table);
#endif
        table = nullptr;
    }

#if defined(_WIN32)
    table = reinterpret_cast<TTCluster *>(_aligned_malloc(numClusters * sizeof(TTCluster), 64));
#else
    if (posix_memalign(reinterpret_cast<void **>(&table), 64, numClusters * sizeof(TTCluster)) != 0)
        table = nullptr;
#endif

    if (!table)
    {
        std::cerr << "TT allocation failed, retrying with half size\n";
        resize(mb / 2);
        return;
    }

    clear();
}

void TT::clear() {
    if (!table)
        return;

    size_t numThreads = std::min(size_t(std::thread::hardware_concurrency()), size_t(8));
    if (numThreads == 0)
        numThreads = 1;

    size_t                   perThread = numClusters / numThreads;
    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    for (size_t t = 0; t < numThreads; ++t)
    {
        size_t start = t * perThread;
        size_t end   = (t == numThreads - 1) ? numClusters : start + perThread;
        threads.emplace_back([this, start, end]() {
            std::memset(&table[start], 0, (end - start) * sizeof(TTCluster));
        });
    }
    for (auto &t : threads)
        t.join();

    currentGen = 0;
}

void TT::new_search() {
    currentGen += TT_AGE_INC;
}

void TT::prefetch(Key key) const {
    if (table)
    {
        const void *addr = &table[index(key)];
#if defined(__AVX2__) || defined(__SSE4_2__)
        _mm_prefetch((const char *)addr, _MM_HINT_T0);
#endif
    }
}

static inline int replacement_score(uint8_t currentGen, const TTEntry &e) {
    uint8_t age = (currentGen - (e.agePvBound & TT_AGE_MASK)) & TT_AGE_MASK;
    return (int(e.depth) << 2) - (int(age) >> 1);
}

TTEntry *TT::probe(Key key, bool &found) {
    TTCluster *cluster = &table[index(key)];
    uint32_t   key32   = uint32_t(key);

    TTEntry *replace   = nullptr;
    int      bestScore = INT_MIN;

    for (int i = 0; i < 4; ++i)
    {
        TTEntry &e = cluster->entries[i];

        if (e.depth == 0)
        {
            found = false;
            return &e;
        }

        if (e.hashKey == key32)
        {
            found        = true;
            e.agePvBound = (e.agePvBound & 0x7) | currentGen;
            return &e;
        }

        int score = replacement_score(currentGen, e);
        if (score > bestScore)
        {
            bestScore = score;
            replace   = &e;
        }
    }

    found = false;
    return replace;
}

void TT::store(Key key, int score, int depth, TTFlag flag, Move move, int eval, bool isPv) {
    TTCluster *cluster = &table[index(key)];
    uint32_t   key32   = uint32_t(key);

    TTEntry *replace   = nullptr;
    int      bestScore = INT_MIN;

    for (int i = 0; i < 4; ++i)
    {
        TTEntry &e = cluster->entries[i];

        if (e.hashKey == key32 && e.depth != 0)
        {
            if (!isPv && e.get_depth() > depth + 2 && e.get_flag() == TT_EXACT && flag != TT_EXACT)
            {
                if (move != MOVE_NONE)
                    e.evalAndMove = (e.evalAndMove & 0xFFF00000) | (move & 0xFFFFF);
                return;
            }
            replace = &e;
            break;
        }

        if (!e.depth)
        {
            replace = &e;
            break;
        }

        int replaceScore = replacement_score(currentGen, e);
        if (replaceScore > bestScore)
        {
            bestScore = replaceScore;
            replace   = &e;
        }
    }

    if (!replace)
        return;

    if (move == MOVE_NONE && replace->hashKey == key32 && replace->depth != 0)
        move = replace->get_move();

    int clampedEval = std::clamp(eval, -2048, 2047);

    uint32_t evalMove = (uint32_t(move) & 0xFFFFF) | ((uint32_t(clampedEval + 2048) & 0xFFF) << 20);

    replace->hashKey     = key32;
    replace->depth       = uint8_t(depth + TT_DEPTH_OFFSET);
    replace->agePvBound  = currentGen | (isPv ? 0x4 : 0x0) | uint8_t(flag);
    replace->evalAndMove = evalMove;
    replace->score       = int16_t(score);
}

int TT::hashfull() const {
    if (!table || numClusters == 0)
        return 0;

    size_t step   = std::max(numClusters / 1000, size_t(1));
    size_t filled = 0;
    size_t sample = 0;

    for (size_t i = 0; i < numClusters && sample < 1000; i += step)
    {
        for (int j = 0; j < 4; ++j)
        {
            const TTEntry &e = table[i].entries[j];
            if (e.depth != 0)
            {
                uint8_t age = (currentGen - (e.agePvBound & TT_AGE_MASK)) & TT_AGE_MASK;
                if (age <= TT_AGE_INC)
                    ++filled;
            }
        }
        ++sample;
    }

    size_t totalEntries = sample * 4;
    return totalEntries > 0 ? int(filled * 1000 / totalEntries) : 0;
}

}