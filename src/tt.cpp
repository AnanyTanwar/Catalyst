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

constexpr size_t HUGE_PAGE = 2 * 1024 * 1024; // 2MB

static size_t round_to_huge(size_t size) {
    return (size + HUGE_PAGE - 1) / HUGE_PAGE * HUGE_PAGE;
}

void TTEntry::save(Key     newKey,
                   int     newScore,
                   int     newDepth,
                   TTFlag  newFlag,
                   Move    newMove,
                   int     newEval,
                   bool    isPv,
                   uint8_t generation8)
{
    const uint16_t newKey16 = static_cast<uint16_t>(newKey);

    // Preserve existing move if caller doesn't have one, but only for same position
    if (newMove != MOVE_NONE || newKey16 != this->key16)
        this->move = newMove;

    // Replacement conditions: exact bound wins, different position overwrites,
    // stale entry, or new search deep enough
    if (   newFlag == TT_EXACT
        || newKey16 != this->key16
        || relative_age(generation8) != 0
        || newDepth + 4 + int(isPv) * 2 > get_depth())
    {
        this->key16     = newKey16;
        this->depth8    = static_cast<uint8_t>(newDepth + TT_DEPTH_OFFSET);
        this->genBound8 = generation8 | (static_cast<uint8_t>(isPv) << 2) | static_cast<uint8_t>(newFlag);
        this->score     = static_cast<int16_t>(newScore);
        this->eval      = static_cast<int16_t>(newEval);
    }
}

void TTWriter::save(Key     key,
                    int     score,
                    int     depth,
                    TTFlag  flag,
                    Move    move,
                    int     eval,
                    bool    isPv,
                    uint8_t generation8)
{
    entry->save(key, score, depth, flag, move, eval, isPv, generation8);
}

void TT::free_table()
{
    if (!table)
        return;

#if defined(USE_MADVISE)
    if (tableMmap)
        munmap(table, tableBytes);
    else
        free(table);
#elif defined(_WIN32)
    _aligned_free(table);
#else
    free(table);
#endif

    table     = nullptr;
    tableMmap = false;
    tableBytes = 0;
}

TT::TT()  { resize(64); }
TT::~TT() { free_table(); }

void TT::resize(size_t mb)
{
    mb = std::max(mb, size_t(1));
    free_table();

    const size_t requested = mb * 1024 * 1024;

#if defined(USE_MADVISE)
    const size_t padded = round_to_huge(requested);

    // Try explicit huge pages first — guaranteed 2MB pages, best TLB behaviour
    void* mem = mmap(nullptr, padded,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
                     -1, 0);

    if (mem != MAP_FAILED)
    {
        table      = reinterpret_cast<TTCluster*>(mem);
        tableBytes = padded;
        tableMmap  = true;
    }
    else
    {
        // Fall back to THP — kernel promotes pages opportunistically
        mem = nullptr;
        if (posix_memalign(&mem, HUGE_PAGE, padded) == 0)
        {
            madvise(mem, padded, MADV_HUGEPAGE);
            table      = reinterpret_cast<TTCluster*>(mem);
            tableBytes = padded;
            tableMmap  = false;
        }
    }

    numClusters = tableBytes / sizeof(TTCluster);

#elif defined(_WIN32)
    table       = reinterpret_cast<TTCluster*>(_aligned_malloc(requested, 64));
    tableBytes  = requested;
    tableMmap   = false;
    numClusters = requested / sizeof(TTCluster);
#else
    const size_t padded = round_to_huge(requested);
    void* mem = nullptr;
    if (posix_memalign(&mem, 64, padded) == 0)
        table = reinterpret_cast<TTCluster*>(mem);
    tableBytes  = padded;
    tableMmap   = false;
    numClusters = padded / sizeof(TTCluster);
#endif

    if (!table)
    {
        std::cerr << "TT allocation failed for " << mb << "MB, retrying with half\n";
        resize(mb / 2);
        return;
    }

    if (numClusters == 0)
        numClusters = 1;

    clear();
}

void TT::clear()
{
    if (!table)
        return;

    const size_t numThreads = std::clamp(
        size_t(std::thread::hardware_concurrency()), size_t(1), size_t(8));

    const size_t perThread = numClusters / numThreads;

    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    for (size_t t = 0; t < numThreads; ++t)
    {
        const size_t start = t * perThread;
        const size_t end   = (t + 1 == numThreads) ? numClusters : start + perThread;
        threads.emplace_back([this, start, end]() {
            std::memset(&table[start], 0, (end - start) * sizeof(TTCluster));
        });
    }

    for (auto& t : threads)
        t.join();

    currentGen = 0;
}

void TT::new_search()
{
    // Only advance the upper 5 bits; lower 3 bits are pv+flag and must stay zero
    currentGen = (currentGen + TT_AGE_INC) & TT_AGE_MASK;
}

void TT::prefetch(Key key) const
{
    if (!table)
        return;
    const void* addr = &table[index(key)];
#if defined(__INTEL_COMPILER) || defined(_MSC_VER)
    _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_T0);
#else
    __builtin_prefetch(addr);
#endif
}

std::tuple<bool, TTData, TTWriter> TT::probe(Key key) const
{
    TTCluster* const cluster = &table[index(key)];
    const uint16_t   key16   = static_cast<uint16_t>(key);

    for (int i = 0; i < TTCluster::ENTRIES; ++i)
    {
        TTEntry& e = cluster->entries[i];

        if (e.key16 == key16 && e.is_occupied())
        {
            TTData data {
                e.get_move(),
                e.get_score(),
                e.get_eval(),
                e.get_depth(),
                e.get_flag(),
                e.is_pv()
            };
            return { true, data, TTWriter(&e) };
        }
    }

    // Position not found — pick the least valuable slot to overwrite
    TTEntry* replace = &cluster->entries[0];

    // Check for empty slot first (score would be misleadingly negative)
    if (!replace->is_occupied())
        return { false, TTData{}, TTWriter(replace) };

    int worstScore = replacement_score(*replace);

    for (int i = 1; i < TTCluster::ENTRIES; ++i)
    {
        TTEntry& e = cluster->entries[i];

        if (!e.is_occupied())
            return { false, TTData{}, TTWriter(&e) };

        const int s = replacement_score(e);
        if (s < worstScore)
        {
            worstScore = s;
            replace    = &e;
        }
    }

    return { false, TTData{}, TTWriter(replace) };
}

int TT::hashfull() const
{
    if (!table || numClusters == 0)
        return 0;

    // Sample the first 1000 clusters (matches UCI convention used by SF)
    constexpr size_t SAMPLE = 1000;
    const size_t     count  = std::min(numClusters, SAMPLE);

    int filled = 0;
    for (size_t i = 0; i < count; ++i)
        for (int j = 0; j < TTCluster::ENTRIES; ++j)
        {
            const TTEntry& e = table[i].entries[j];
            if (e.is_occupied() && e.age() == currentGen)
                ++filled;
        }

    return filled * 1000 / int(count * TTCluster::ENTRIES);
}

} // namespace Catalyst
