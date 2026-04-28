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
#include <iostream>
#include <thread>
#include <vector>

#if defined(__INTEL_COMPILER) || defined(_MSC_VER)
#include <xmmintrin.h>
#endif

#if defined(__linux__) && !defined(__ANDROID__)
#include <sys/mman.h>
#define USE_MADVISE
#endif

namespace Catalyst {

TT tt;

TT::TT()
{
    resize(64);
}

TT::~TT()
{
    if (table)
    {
#if defined(_WIN32)
        _aligned_free(table);
#else
        free(table);
#endif
        table = nullptr;
    }
}

void TT::resize(size_t mb)
{
    mb = std::max(mb, size_t(1));

    if (table)
    {
#if defined(_WIN32)
        _aligned_free(table);
#else
        free(table);
#endif
        table = nullptr;
    }

    const size_t bytes = mb * 1024 * 1024;
    numClusters        = bytes / sizeof(TTCluster);

    if (numClusters == 0)
        numClusters = 1;

    const size_t allocSize = numClusters * sizeof(TTCluster);

#if defined(USE_MADVISE)
    constexpr size_t alignment = 2 * 1024 * 1024;
#elif defined(_WIN32)
    constexpr size_t alignment = 64;
#else
    constexpr size_t alignment = 64;
#endif

#if defined(_WIN32)
    table = reinterpret_cast<TTCluster *>(_aligned_malloc(allocSize, alignment));
#else
    const size_t paddedSize = (allocSize + alignment - 1) / alignment * alignment;
    if (posix_memalign(reinterpret_cast<void **>(&table), alignment, paddedSize) != 0)
        table = nullptr;
#endif

    if (!table)
    {
        std::cerr << "TT allocation failed, retrying with half size\n";
        resize(mb / 2);
        return;
    }

#if defined(USE_MADVISE)
    madvise(table, allocSize, MADV_HUGEPAGE);
#endif

    clear();
}

void TT::clear()
{
    if (!table)
        return;

    size_t numThreads = std::min(size_t(std::thread::hardware_concurrency()), size_t(8));
    if (numThreads == 0)
        numThreads = 1;

    const size_t perThread = numClusters / numThreads;

    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    for (size_t t = 0; t < numThreads; ++t)
    {
        const size_t start = t * perThread;
        const size_t end   = (t == numThreads - 1) ? numClusters : start + perThread;
        threads.emplace_back([this, start, end]() {
            std::memset(&table[start], 0, (end - start) * sizeof(TTCluster));
        });
    }

    for (auto &t : threads)
        t.join();

    currentGen = 0;
}

void TT::new_search()
{
    currentGen += TT_AGE_INC;
}

void TT::prefetch(Key key) const
{
    if (!table)
        return;
    const void *addr = &table[index(key)];
#if defined(__INTEL_COMPILER) || defined(_MSC_VER)
    _mm_prefetch(reinterpret_cast<const char *>(addr), _MM_HINT_T0);
#else
    __builtin_prefetch(addr);
#endif
}

TTEntry *TT::probe(Key key, bool &found)
{
    TTCluster     *cluster = &table[index(key)];
    const uint32_t key32   = uint32_t(key);

    for (int i = 0; i < 4; ++i)
    {
        TTEntry &e = cluster->entries[i];
        if (e.hashKey == key32 && !e.is_empty())
        {
            e.agePvBound = (e.agePvBound & ~TT_AGE_MASK) | currentGen;
            found        = true;
            return &e;
        }
    }

    TTEntry *replace    = &cluster->entries[0];
    int      worstScore = replacement_score(*replace);

    for (int i = 1; i < 4; ++i)
    {
        TTEntry &e = cluster->entries[i];

        if (e.is_empty())
        {
            found = false;
            return &e;
        }

        const int s = replacement_score(e);
        if (s < worstScore)
        {
            worstScore = s;
            replace    = &e;
        }
    }

    found = false;
    return replace;
}

void TT::store(Key key,
    int            score,
    int            depth,
    TTFlag         flag,
    Move           move,
    int            eval,
    int            rule50,
    bool           isPv)
{
    TTCluster     *cluster = &table[index(key)];
    const uint32_t key32   = uint32_t(key);

    TTEntry *replace = nullptr;

    for (int i = 0; i < 4; ++i)
    {
        TTEntry &e = cluster->entries[i];
        if (e.hashKey == key32 && !e.is_empty())
        {
            replace = &e;
            break;
        }
    }

    if (!replace)
    {
        replace        = &cluster->entries[0];
        int worstScore = replacement_score(*replace);

        for (int i = 1; i < 4; ++i)
        {
            TTEntry &e = cluster->entries[i];

            if (e.is_empty())
            {
                replace = &e;
                goto write;
            }

            const int s = replacement_score(e);
            if (s < worstScore)
            {
                worstScore = s;
                replace    = &e;
            }
        }
    }

    if (!(flag == TT_EXACT || replace->hashKey != key32
            || (replace->agePvBound & TT_AGE_MASK) != currentGen
            || depth + 4 + int(isPv) * 2 > replace->get_depth()))
    {
        if (move != MOVE_NONE)
            replace->move = uint16_t(move);
        return;
    }

    if (move == MOVE_NONE && replace->hashKey == key32)
        move = Move(replace->move);

write:
    replace->hashKey    = key32;
    replace->move       = uint16_t(move);
    replace->score      = int16_t(score);
    replace->eval       = int16_t(std::clamp(eval, -32000, 32000));
    replace->rule50     = int16_t(rule50);
    replace->depth      = uint8_t(depth + TT_DEPTH_OFFSET);
    replace->agePvBound = currentGen | (isPv ? 0x4 : 0x0) | uint8_t(flag);
}

int TT::hashfull() const
{
    if (!table || numClusters == 0)
        return 0;

    constexpr size_t SAMPLE = 1000;
    const size_t     stride = std::max(numClusters / SAMPLE, size_t(1));

    size_t filled = 0;
    size_t count  = 0;

    for (size_t i = 0; i < numClusters; i += stride)
    {
        for (int j = 0; j < 4; ++j)
        {
            const TTEntry &e = table[i].entries[j];
            if (!e.is_empty() && (e.agePvBound & TT_AGE_MASK) == currentGen)
                ++filled;
        }
        ++count;
    }

    return count > 0 ? int(filled * 1000 / (count * 4)) : 0;
}
}