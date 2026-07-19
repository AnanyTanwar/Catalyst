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

#include <cstdint>
#include <type_traits>

#if defined(__GNUC__) || defined(__clang__)
#include <cpuid.h>
#endif

#if defined(__BMI2__)
#include <immintrin.h>
#endif

// Compilers won't always force-inline these tiny hot-path functions on
// their own (especially in debug builds), so FORCE_INLINE spells it out
// explicitly per-compiler. Falls back to a plain `inline` on anything
// unrecognized.
#if defined(__GNUC__) || defined(__clang__)
#define FORCE_INLINE __attribute__((always_inline)) inline
#elif defined(_MSC_VER)
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE inline
#endif

namespace Catalyst {

using Bitboard = uint64_t;

// Returns the number of set bits in x (i.e. the number of pieces/squares
// present in the bitboard). Maps to a single hardware POPCNT instruction
// on GCC/Clang/MSVC; falls back to Kernighan's bit-clearing loop
// (x &= x - 1 strips the lowest set bit each iteration) elsewhere.
[[nodiscard]] FORCE_INLINE int popcount(uint64_t x)
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_popcountll(x);
#elif defined(_MSC_VER)
    return (int)__popcnt64(x);
#else
    int count = 0;
    while (x)
    {
        count++;
        x &= x - 1;
    }
    return count;
#endif
}

// Returns the index (0-63) of the least significant set bit of x - i.e.
// the lowest-numbered occupied square in a bitboard. Used by pop_lsb()-style
// "iterate all set bits" loops throughout move generation and search.
//
// CONTRACT: x must be non-zero. __builtin_ctzll and _BitScanForward64 are
// both undefined behavior on a zero input, and this function does not
// guard against it - callers are expected to have already checked
// (e.g. via a `while (bb)` loop condition) before calling.
[[nodiscard]] FORCE_INLINE int lsb(uint64_t x)
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(x);
#elif defined(_MSC_VER)
    unsigned long idx;
    _BitScanForward64(&idx, x);
    return (int)idx;
#else
    if (!x)
        return 64;
    int n = 0;
    while (!(x & 1))
    {
        x >>= 1;
        ++n;
    }
    return n;
#endif
}

// Returns the index (0-63) of the most significant set bit of x - i.e.
// the highest-numbered occupied square in a bitboard.
//
// CONTRACT: same as lsb() above - x must be non-zero. __builtin_clzll and
// _BitScanReverse64 are undefined behavior for x == 0, and this function
// does not guard against it.
[[nodiscard]] FORCE_INLINE int msb(uint64_t x)
{
#if defined(__GNUC__) || defined(__clang__)
    return 63 - __builtin_clzll(x);
#elif defined(_MSC_VER)
    unsigned long idx;
    _BitScanReverse64(&idx, x);
    return (int)idx;
#else
    if (!x)
        return 64;
    int n = 0;
    while (x >>= 1)
        ++n;
    return n;
#endif
}

// Parallel bit extract: gathers the bits of src that fall under the set
// bits of mask, and packs them contiguously into the low bits of the
// result (in mask order, LSB first). This is the core primitive behind
// PEXT-based magic bitboard attack lookups (see bitboard.cpp) - the
// occupancy bits relevant to a sliding piece's attack rays are extracted
// into a small dense index used to look up a precomputed attack table.
//
// On BMI2-capable hardware this compiles to the single PEXT instruction.
// The #else branch is a bit-by-bit software emulation used both for non-
// BMI2 builds and to compute lookup tables at startup/build time when
// hardware PEXT isn't assumed available.
[[nodiscard]] FORCE_INLINE uint64_t pext(uint64_t src, uint64_t mask)
{
#if defined(__BMI2__)
    return _pext_u64(src, mask);
#else
    uint64_t res = 0;
    int      i   = 0;
    while (mask)
    {
        uint64_t lsb = mask & -mask;
        if (src & lsb)
            res |= (1ULL << i);
        mask ^= lsb;
        ++i;
    }
    return res;
#endif
}

// Parallel bit deposit: the inverse of pext(). Takes the low bits of src
// (in order) and scatters them into the positions marked by mask, leaving
// all other bits 0. Used to reconstruct an occupancy bitboard from a dense
// index - e.g. when enumerating every possible blocker configuration for a
// magic bitboard table during initialization.
//
// Same hardware-vs-fallback split as pext(): a single PDEP instruction
// under BMI2, otherwise a bit-by-bit software emulation.
[[nodiscard]] FORCE_INLINE uint64_t pdep(uint64_t src, uint64_t mask)
{
#if defined(__BMI2__)
    return _pdep_u64(src, mask);
#else
    uint64_t res = 0;
    int      i   = 0;
    while (mask)
    {
        uint64_t lsb = mask & -mask;
        if (src & (1ULL << i))
            res |= lsb;
        mask ^= lsb;
        ++i;
    }
    return res;
#endif
}

// Runtime check for BMI2 support via CPUID, independent of whether the
// binary was *compiled* with -mbmi2. This lets a single release binary
// (compiled for a baseline architecture) decide at startup whether it's
// safe to dispatch to a BMI2-optimized code path, versus a build that was
// compiled with __BMI2__ defined and can use the intrinsics unconditionally.
// Always false on non-x86 targets, since BMI2 is an x86 extension.
[[nodiscard]] inline bool cpu_has_bmi2()
{
#if (defined(__x86_64__) || defined(_M_X64))
#if defined(__GNUC__) || defined(__clang__)
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx))
        return (ebx & (1 << 8)) != 0;
    return false;
#elif defined(_MSC_VER)
    int cpuInfo[4];
    __cpuidex(cpuInfo, 7, 0);
    return (cpuInfo[1] & (1 << 8)) != 0;
#else
    return false;
#endif
#else
    return false;
#endif
}

}  // namespace Catalyst