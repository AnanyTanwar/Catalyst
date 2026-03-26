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

#if defined(__GNUC__) || defined(__clang__)
#define FORCE_INLINE __attribute__((always_inline)) inline
#elif defined(_MSC_VER)
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE inline
#endif

namespace Catalyst {

using Bitboard = uint64_t;

[[nodiscard]] FORCE_INLINE int popcount(uint64_t x) {
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_popcountll(x);
#elif defined(_MSC_VER)
  return (int)__popcnt64(x);
#else
  int count = 0;
  while (x) {
    count++;
    x &= x - 1;
  }
  return count;
#endif
}

[[nodiscard]] FORCE_INLINE int lsb(uint64_t x) {
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
  while (!(x & 1)) {
    x >>= 1;
    ++n;
  }
  return n;
#endif
}

[[nodiscard]] FORCE_INLINE int msb(uint64_t x) {
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

[[nodiscard]] FORCE_INLINE uint64_t pext(uint64_t src, uint64_t mask) {
#if defined(__BMI2__)
  return _pext_u64(src, mask);
#else
  uint64_t res = 0;
  int i = 0;
  while (mask) {
    uint64_t lsb = mask & -mask;
    if (src & lsb)
      res |= (1ULL << i);
    mask ^= lsb;
    ++i;
  }
  return res;
#endif
}

[[nodiscard]] FORCE_INLINE uint64_t pdep(uint64_t src, uint64_t mask) {
#if defined(__BMI2__)
  return _pdep_u64(src, mask);
#else
  uint64_t res = 0;
  int i = 0;
  while (mask) {
    uint64_t lsb = mask & -mask;
    if (src & (1ULL << i))
      res |= lsb;
    mask ^= lsb;
    ++i;
  }
  return res;
#endif
}

[[nodiscard]] inline bool cpu_has_bmi2() {
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

} // namespace Catalyst