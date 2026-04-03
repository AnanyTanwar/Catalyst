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

// Prefetch intrinsics
#if defined(__INTEL_COMPILER) || defined(_MSC_VER)
#include <xmmintrin.h>
#endif

// Huge page support (Linux only)
#if defined(__linux__) && !defined(__ANDROID__)
#include <sys/mman.h>
#define USE_MADVISE
#endif

namespace Catalyst {

TT tt;

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

TT::TT() {
    resize(64);
}

TT::~TT() {
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

// ---------------------------------------------------------------------------
// resize — allocate aligned memory, request huge pages on Linux
// ---------------------------------------------------------------------------

void TT::resize(size_t mb) {
    mb = std::max(mb, size_t(1));

    // Free old table
    if (table)
    {
#if defined(_WIN32)
        _aligned_free(table);
#else
        free(table);
#endif
        table = nullptr;
    }

    // Compute cluster count — no power-of-2 rounding needed with __uint128_t index
    const size_t bytes = mb * 1024 * 1024;
    numClusters        = bytes / sizeof(TTCluster);

    if (numClusters == 0)
        numClusters = 1;

    const size_t allocSize = numClusters * sizeof(TTCluster);

    // Aligned allocation — 64 bytes for cache line, 2MB on Linux for huge pages
#if defined(USE_MADVISE)
    constexpr size_t alignment = 2 * 1024 * 1024;  // 2 MB huge page boundary
#elif defined(_WIN32)
    constexpr size_t alignment = 64;
#else
    constexpr size_t alignment = 64;
#endif

#if defined(_WIN32)
    table = reinterpret_cast<TTCluster *>(_aligned_malloc(allocSize, alignment));
#else
    // Round allocSize up to alignment multiple (required by aligned_alloc / posix_memalign)
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

    // On Linux, advise the kernel to use 2MB huge pages for the TT.
    // This reduces TLB pressure significantly at large TT sizes.
#if defined(USE_MADVISE)
    madvise(table, allocSize, MADV_HUGEPAGE);
#endif

    clear();
}

// ---------------------------------------------------------------------------
// clear — parallel memset using up to 8 threads (same as before)
// ---------------------------------------------------------------------------

void TT::clear() {
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

// ---------------------------------------------------------------------------
// new_search — bump generation counter each search
// ---------------------------------------------------------------------------

void TT::new_search() {
    currentGen += TT_AGE_INC;
}

// ---------------------------------------------------------------------------
// prefetch — non-blocking cache hint
// ---------------------------------------------------------------------------

void TT::prefetch(Key key) const {
    if (!table)
        return;
    const void *addr = &table[index(key)];
#if defined(__INTEL_COMPILER) || defined(_MSC_VER)
    _mm_prefetch(reinterpret_cast<const char *>(addr), _MM_HINT_T0);
#else
    __builtin_prefetch(addr);
#endif
}

// ---------------------------------------------------------------------------
// probe — look up position in TT
//
// On a hit  : refreshes the entry's age and returns a pointer + found=true
// On a miss : returns the best replacement candidate + found=false
// ---------------------------------------------------------------------------

TTEntry *TT::probe(Key key, bool &found) {
    TTCluster     *cluster = &table[index(key)];
    const uint32_t key32   = uint32_t(key);

    // Pass 1: check for a hit
    for (int i = 0; i < 4; ++i)
    {
        TTEntry &e = cluster->entries[i];
        if (e.hashKey == key32 && !e.is_empty())
        {
            // Refresh age, preserving pv + flag bits
            e.agePvBound = (e.agePvBound & ~TT_AGE_MASK) | currentGen;
            found        = true;
            return &e;
        }
    }

    // Pass 2: find the worst entry to evict
    // Prefer empty slots immediately; otherwise pick lowest replacement_score
    TTEntry *replace    = &cluster->entries[0];
    int      worstScore = replacement_score(*replace);

    for (int i = 1; i < 4; ++i)
    {
        TTEntry &e = cluster->entries[i];

        // Empty slot — take it immediately, no need to keep looking
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

// ---------------------------------------------------------------------------
// store — write a new result into the TT
//
// Replacement policy (inspired by Stormphrax / Alexandria / Stockfish):
//   Always overwrite if:
//     • it's an exact bound  (most valuable result)
//     • different position   (stale data, always replace)
//     • entry is from a previous search generation
//     • new depth + bonus is deeper than existing depth
//   Otherwise preserve the existing entry (it's better than what we have).
//
//   Move is preserved from the existing entry when the new search hasn't
//   found one yet (move == MOVE_NONE), as long as the position is the same.
// ---------------------------------------------------------------------------

void TT::store(
    Key key, int score, int depth, TTFlag flag, Move move, int eval, int rule50, bool isPv) {
    TTCluster     *cluster = &table[index(key)];
    const uint32_t key32   = uint32_t(key);

    TTEntry *replace = nullptr;

    // Pass 1: try to find an existing entry for this exact position
    for (int i = 0; i < 4; ++i)
    {
        TTEntry &e = cluster->entries[i];
        if (e.hashKey == key32 && !e.is_empty())
        {
            replace = &e;
            break;
        }
    }

    // Pass 2: if no existing entry, find the worst slot to evict
    if (!replace)
    {
        replace        = &cluster->entries[0];
        int worstScore = replacement_score(*replace);

        for (int i = 1; i < 4; ++i)
        {
            TTEntry &e = cluster->entries[i];

            // Empty slot — best possible replacement
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

    // Replacement condition — only skip write if none of these are true:
    //   1. exact bound (always the most valuable)
    //   2. different position (stale key — always overwrite)
    //   3. entry is from a previous generation (aged out)
    //   4. new depth (with PV bonus) beats existing depth
    //
    // The +4 offset and ×2 PV bonus match Stormphrax / Stockfish scheme.
    if (!(flag == TT_EXACT || replace->hashKey != key32
            || (replace->agePvBound & TT_AGE_MASK) != currentGen
            || depth + 4 + int(isPv) * 2 > replace->get_depth()))
    {
        // Preserve existing move even if we're not rewriting the full entry
        if (move != MOVE_NONE)
            replace->move = uint16_t(move);
        return;
    }

    // Preserve existing move for same position when we don't have a better one
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

// ---------------------------------------------------------------------------
// hashfull — approximate fill rate in permille (0-1000)
// Samples first 2000 clusters (same as Alexandria), counts only entries
// from the current generation.
// ---------------------------------------------------------------------------

int TT::hashfull() const {
    if (!table || numClusters == 0)
        return 0;

    constexpr size_t SAMPLE = 2000;
    const size_t     limit  = std::min(numClusters, SAMPLE);

    size_t filled = 0;

    for (size_t i = 0; i < limit; ++i)
    {
        for (int j = 0; j < 4; ++j)
        {
            const TTEntry &e = table[i].entries[j];
            // Count only non-empty entries from the current generation
            if (!e.is_empty() && (e.agePvBound & TT_AGE_MASK) == currentGen)
            {
                ++filled;
            }
        }
    }

    // filled / (limit * 4 entries) expressed as permille
    return int(filled * 1000 / (limit * 4));
}

}  // namespace Catalyst