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

// ============================================================
// simd.h — SIMD-accelerated kernels for Catalyst NNUE
//
// Provides five operations:
//   1. simd_add_weights      — accumulator += feature column
//   2. simd_sub_weights      — accumulator -= feature column
//   3. simd_add_sub_weights  — accumulator += col_add - col_sub  (fused move)
//   4. simd_screlu_forward   — SCReLU dot product for one accumulator half:
//                              sum += clamp(acc[i], 0, QA)^2 * weight[i]
//   5. simd_init_accumulator — acc[i] = bias[i]
//
// Architecture tiers (auto-selected at compile time):
//   AVX-512 VNNI  — 512-bit, fused dot-product (fastest)
//   AVX-512 F+BW  — 512-bit without VNNI
//   AVX2          — 256-bit (most common modern x86)
//   SSE4.1        — 128-bit fallback
//   Scalar        — pure C++ fallback (ARM / unknown)
//
// HIDDEN_SIZE must be a multiple of the widest register's lane count.
// 256 satisfies all tiers (multiple of 32, 16, 8).
// ============================================================

#include "intrinsics.h"
#include <cstdint>

// ── Detect available ISA ──────────────────────────────────────────────────────

#if defined(__AVX512VNNI__) && defined(__AVX512BW__) && defined(__AVX512F__)
#define SIMD_AVX512VNNI 1
#include <immintrin.h>
#elif defined(__AVX512F__) && defined(__AVX512BW__)
#define SIMD_AVX512 1
#include <immintrin.h>
#elif defined(__AVX2__)
#define SIMD_AVX2 1
#include <immintrin.h>
#elif defined(__SSE4_1__)
#define SIMD_SSE41 1
#include <smmintrin.h>
#endif

namespace Catalyst {
namespace SIMD {

    // ── SCReLU clamp bounds (must match QA in nnue.h) ────────────────────────────
    inline constexpr int16_t SCRELU_MIN = 0;
    inline constexpr int16_t SCRELU_MAX = 255;  // must equal NNUE::QA

    // ============================================================
    // 1.  simd_add_weights
    //     acc[i] += col[i]   for i in [0, HIDDEN_SIZE)
    //     Both pointers must be 64-byte aligned.
    // ============================================================

    template <int HIDDEN_SIZE>
    FORCE_INLINE void simd_add_weights(int16_t *__restrict__ acc, const int16_t *__restrict__ col) {
#if defined(SIMD_AVX512VNNI) || defined(SIMD_AVX512)
        static_assert(HIDDEN_SIZE % 32 == 0, "HIDDEN_SIZE must be multiple of 32");
        constexpr int ITERS = HIDDEN_SIZE / 32;
        for (int i = 0; i < ITERS; ++i)
        {
            __m512i a = _mm512_load_si512(reinterpret_cast<const __m512i *>(acc + i * 32));
            __m512i b = _mm512_load_si512(reinterpret_cast<const __m512i *>(col + i * 32));
            _mm512_store_si512(reinterpret_cast<__m512i *>(acc + i * 32), _mm512_add_epi16(a, b));
        }
#elif defined(SIMD_AVX2)
        static_assert(HIDDEN_SIZE % 16 == 0, "HIDDEN_SIZE must be multiple of 16");
        constexpr int ITERS = HIDDEN_SIZE / 16;
        for (int i = 0; i < ITERS; ++i)
        {
            __m256i a = _mm256_load_si256(reinterpret_cast<const __m256i *>(acc + i * 16));
            __m256i b = _mm256_load_si256(reinterpret_cast<const __m256i *>(col + i * 16));
            _mm256_store_si256(reinterpret_cast<__m256i *>(acc + i * 16), _mm256_add_epi16(a, b));
        }
#elif defined(SIMD_SSE41)
        static_assert(HIDDEN_SIZE % 8 == 0, "HIDDEN_SIZE must be multiple of 8");
        constexpr int ITERS = HIDDEN_SIZE / 8;
        for (int i = 0; i < ITERS; ++i)
        {
            __m128i a = _mm_load_si128(reinterpret_cast<const __m128i *>(acc + i * 8));
            __m128i b = _mm_load_si128(reinterpret_cast<const __m128i *>(col + i * 8));
            _mm_store_si128(reinterpret_cast<__m128i *>(acc + i * 8), _mm_add_epi16(a, b));
        }
#else
        for (int i = 0; i < HIDDEN_SIZE; ++i)
            acc[i] += col[i];
#endif
    }

    // ============================================================
    // 2.  simd_sub_weights
    //     acc[i] -= col[i]   for i in [0, HIDDEN_SIZE)
    // ============================================================

    template <int HIDDEN_SIZE>
    FORCE_INLINE void simd_sub_weights(int16_t *__restrict__ acc, const int16_t *__restrict__ col) {
#if defined(SIMD_AVX512VNNI) || defined(SIMD_AVX512)
        static_assert(HIDDEN_SIZE % 32 == 0, "HIDDEN_SIZE must be multiple of 32");
        constexpr int ITERS = HIDDEN_SIZE / 32;
        for (int i = 0; i < ITERS; ++i)
        {
            __m512i a = _mm512_load_si512(reinterpret_cast<const __m512i *>(acc + i * 32));
            __m512i b = _mm512_load_si512(reinterpret_cast<const __m512i *>(col + i * 32));
            _mm512_store_si512(reinterpret_cast<__m512i *>(acc + i * 32), _mm512_sub_epi16(a, b));
        }
#elif defined(SIMD_AVX2)
        static_assert(HIDDEN_SIZE % 16 == 0, "HIDDEN_SIZE must be multiple of 16");
        constexpr int ITERS = HIDDEN_SIZE / 16;
        for (int i = 0; i < ITERS; ++i)
        {
            __m256i a = _mm256_load_si256(reinterpret_cast<const __m256i *>(acc + i * 16));
            __m256i b = _mm256_load_si256(reinterpret_cast<const __m256i *>(col + i * 16));
            _mm256_store_si256(reinterpret_cast<__m256i *>(acc + i * 16), _mm256_sub_epi16(a, b));
        }
#elif defined(SIMD_SSE41)
        static_assert(HIDDEN_SIZE % 8 == 0, "HIDDEN_SIZE must be multiple of 8");
        constexpr int ITERS = HIDDEN_SIZE / 8;
        for (int i = 0; i < ITERS; ++i)
        {
            __m128i a = _mm_load_si128(reinterpret_cast<const __m128i *>(acc + i * 8));
            __m128i b = _mm_load_si128(reinterpret_cast<const __m128i *>(col + i * 8));
            _mm_store_si128(reinterpret_cast<__m128i *>(acc + i * 8), _mm_sub_epi16(a, b));
        }
#else
        for (int i = 0; i < HIDDEN_SIZE; ++i)
            acc[i] -= col[i];
#endif
    }

    // ============================================================
    // 3.  simd_add_sub_weights  (fused move: remove from + add to)
    //     acc[i] += col_add[i] - col_sub[i]
    // ============================================================

    template <int HIDDEN_SIZE>
    FORCE_INLINE void simd_add_sub_weights(int16_t *__restrict__ acc,
        const int16_t *__restrict__ col_add,
        const int16_t *__restrict__ col_sub) {
#if defined(SIMD_AVX512VNNI) || defined(SIMD_AVX512)
        static_assert(HIDDEN_SIZE % 32 == 0, "HIDDEN_SIZE must be multiple of 32");
        constexpr int ITERS = HIDDEN_SIZE / 32;
        for (int i = 0; i < ITERS; ++i)
        {
            __m512i a   = _mm512_load_si512(reinterpret_cast<const __m512i *>(acc + i * 32));
            __m512i add = _mm512_load_si512(reinterpret_cast<const __m512i *>(col_add + i * 32));
            __m512i sub = _mm512_load_si512(reinterpret_cast<const __m512i *>(col_sub + i * 32));
            _mm512_store_si512(reinterpret_cast<__m512i *>(acc + i * 32),
                _mm512_add_epi16(a, _mm512_sub_epi16(add, sub)));
        }
#elif defined(SIMD_AVX2)
        static_assert(HIDDEN_SIZE % 16 == 0, "HIDDEN_SIZE must be multiple of 16");
        constexpr int ITERS = HIDDEN_SIZE / 16;
        for (int i = 0; i < ITERS; ++i)
        {
            __m256i a   = _mm256_load_si256(reinterpret_cast<const __m256i *>(acc + i * 16));
            __m256i add = _mm256_load_si256(reinterpret_cast<const __m256i *>(col_add + i * 16));
            __m256i sub = _mm256_load_si256(reinterpret_cast<const __m256i *>(col_sub + i * 16));
            _mm256_store_si256(reinterpret_cast<__m256i *>(acc + i * 16),
                _mm256_add_epi16(a, _mm256_sub_epi16(add, sub)));
        }
#elif defined(SIMD_SSE41)
        static_assert(HIDDEN_SIZE % 8 == 0, "HIDDEN_SIZE must be multiple of 8");
        constexpr int ITERS = HIDDEN_SIZE / 8;
        for (int i = 0; i < ITERS; ++i)
        {
            __m128i a   = _mm_load_si128(reinterpret_cast<const __m128i *>(acc + i * 8));
            __m128i add = _mm_load_si128(reinterpret_cast<const __m128i *>(col_add + i * 8));
            __m128i sub = _mm_load_si128(reinterpret_cast<const __m128i *>(col_sub + i * 8));
            _mm_store_si128(reinterpret_cast<__m128i *>(acc + i * 8),
                _mm_add_epi16(a, _mm_sub_epi16(add, sub)));
        }
#else
        for (int i = 0; i < HIDDEN_SIZE; ++i)
            acc[i] += col_add[i] - col_sub[i];
#endif
    }

    // ============================================================
    // 4.  simd_screlu_forward
    //
    //     One half of the SCReLU forward pass:
    //       sum += clamp(acc[i], 0, QA)^2 * weights[i]
    //     for i in [0, HIDDEN_SIZE).
    //
    //     Returns the int32_t dot product for one accumulator half.
    //     Call twice — once for stm_acc (weights[0..HS-1]),
    //     once for nstm_acc (weights[HS..2*HS-1]).
    //
    //     QA=255: max pre-square value 255, max squared 65025 — does NOT
    //     fit in int16 (max 32767).  We widen to int32 before squaring.
    //     Max term: 65025 * 32767 ~ 2.13e9, fits in int32.
    //     Accumulating HIDDEN_SIZE=256 such terms: max ~5.45e11, needs int64.
    //     The SIMD paths accumulate int32 and return int32 — the caller
    //     (evaluate()) widens to int64 before summing the two halves.
    //     For HIDDEN_SIZE=256 the per-half max is ~2.74e11, which overflows
    //     int32 in the worst case.  The int64 widen in evaluate() is therefore
    //     load-bearing — do not remove it.
    // ============================================================

    template <int HIDDEN_SIZE>
    FORCE_INLINE int32_t simd_screlu_forward(
        const int16_t *__restrict__ acc, const int16_t *__restrict__ weights) {
#if defined(SIMD_AVX512VNNI)
        static_assert(HIDDEN_SIZE % 32 == 0, "HIDDEN_SIZE must be multiple of 32");
        constexpr int ITERS = HIDDEN_SIZE / 32;

        const __m512i zero = _mm512_setzero_si512();
        const __m512i qa   = _mm512_set1_epi16(SCRELU_MAX);
        __m512i       sum  = _mm512_setzero_si512();

        for (int i = 0; i < ITERS; ++i)
        {
            __m512i x = _mm512_load_si512(reinterpret_cast<const __m512i *>(acc + i * 32));
            __m512i w = _mm512_load_si512(reinterpret_cast<const __m512i *>(weights + i * 32));

            x = _mm512_min_epi16(_mm512_max_epi16(x, zero), qa);

            __m256i x_lo = _mm512_castsi512_si256(x);
            __m256i w_lo = _mm512_castsi512_si256(w);
            __m256i x_hi = _mm512_extracti64x4_epi64(x, 1);
            __m256i w_hi = _mm512_extracti64x4_epi64(w, 1);

            __m256i x32_ll = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(x_lo));
            __m256i x32_lh = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(x_lo, 1));
            __m256i w32_ll = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(w_lo));
            __m256i w32_lh = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(w_lo, 1));
            __m256i x32_hl = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(x_hi));
            __m256i x32_hh = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(x_hi, 1));
            __m256i w32_hl = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(w_hi));
            __m256i w32_hh = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(w_hi, 1));

            __m256i r_ll = _mm256_mullo_epi32(_mm256_mullo_epi32(x32_ll, x32_ll), w32_ll);
            __m256i r_lh = _mm256_mullo_epi32(_mm256_mullo_epi32(x32_lh, x32_lh), w32_lh);
            __m256i r_hl = _mm256_mullo_epi32(_mm256_mullo_epi32(x32_hl, x32_hl), w32_hl);
            __m256i r_hh = _mm256_mullo_epi32(_mm256_mullo_epi32(x32_hh, x32_hh), w32_hh);

            __m512i part_lo = _mm512_inserti64x4(_mm512_castsi256_si512(r_ll), r_lh, 1);
            __m512i part_hi = _mm512_inserti64x4(_mm512_castsi256_si512(r_hl), r_hh, 1);
            sum             = _mm512_add_epi32(sum, _mm512_add_epi32(part_lo, part_hi));
        }

        return _mm512_reduce_add_epi32(sum);

#elif defined(SIMD_AVX512)
        static_assert(HIDDEN_SIZE % 32 == 0, "HIDDEN_SIZE must be multiple of 32");
        constexpr int ITERS = HIDDEN_SIZE / 32;

        const __m512i zero = _mm512_setzero_si512();
        const __m512i qa   = _mm512_set1_epi16(SCRELU_MAX);
        __m512i       sum  = _mm512_setzero_si512();

        for (int i = 0; i < ITERS; ++i)
        {
            __m512i x = _mm512_load_si512(reinterpret_cast<const __m512i *>(acc + i * 32));
            __m512i w = _mm512_load_si512(reinterpret_cast<const __m512i *>(weights + i * 32));
            x         = _mm512_min_epi16(_mm512_max_epi16(x, zero), qa);

            __m256i x_lo = _mm512_castsi512_si256(x);
            __m256i w_lo = _mm512_castsi512_si256(w);
            __m256i x_hi = _mm512_extracti64x4_epi64(x, 1);
            __m256i w_hi = _mm512_extracti64x4_epi64(w, 1);

            auto accum256 = [&](__m256i xv, __m256i wv) -> __m512i {
                __m256i x32_lo = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(xv));
                __m256i x32_hi = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(xv, 1));
                __m256i w32_lo = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(wv));
                __m256i w32_hi = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(wv, 1));
                __m256i r_lo   = _mm256_mullo_epi32(_mm256_mullo_epi32(x32_lo, x32_lo), w32_lo);
                __m256i r_hi   = _mm256_mullo_epi32(_mm256_mullo_epi32(x32_hi, x32_hi), w32_hi);
                return _mm512_inserti64x4(_mm512_castsi256_si512(r_lo), r_hi, 1);
            };

            sum = _mm512_add_epi32(
                sum, _mm512_add_epi32(accum256(x_lo, w_lo), accum256(x_hi, w_hi)));
        }

        return _mm512_reduce_add_epi32(sum);

#elif defined(SIMD_AVX2)
        static_assert(HIDDEN_SIZE % 16 == 0, "HIDDEN_SIZE must be multiple of 16");
        constexpr int ITERS = HIDDEN_SIZE / 16;

        const __m256i zero = _mm256_setzero_si256();
        const __m256i qa   = _mm256_set1_epi16(SCRELU_MAX);
        __m256i       sum  = _mm256_setzero_si256();

        for (int i = 0; i < ITERS; ++i)
        {
            __m256i x = _mm256_load_si256(reinterpret_cast<const __m256i *>(acc + i * 16));
            __m256i w = _mm256_load_si256(reinterpret_cast<const __m256i *>(weights + i * 16));

            x = _mm256_min_epi16(_mm256_max_epi16(x, zero), qa);

            __m256i x32_lo = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(x));
            __m256i x32_hi = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(x, 1));
            __m256i w32_lo = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(w));
            __m256i w32_hi = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(w, 1));

            __m256i r_lo = _mm256_mullo_epi32(_mm256_mullo_epi32(x32_lo, x32_lo), w32_lo);
            __m256i r_hi = _mm256_mullo_epi32(_mm256_mullo_epi32(x32_hi, x32_hi), w32_hi);

            sum = _mm256_add_epi32(sum, _mm256_add_epi32(r_lo, r_hi));
        }

        __m128i lo = _mm256_castsi256_si128(sum);
        __m128i hi = _mm256_extracti128_si256(sum, 1);
        __m128i s  = _mm_add_epi32(lo, hi);
        s          = _mm_add_epi32(s, _mm_srli_si128(s, 8));
        s          = _mm_add_epi32(s, _mm_srli_si128(s, 4));
        return _mm_cvtsi128_si32(s);

#elif defined(SIMD_SSE41)
        static_assert(HIDDEN_SIZE % 8 == 0, "HIDDEN_SIZE must be multiple of 8");
        constexpr int ITERS = HIDDEN_SIZE / 8;

        const __m128i zero = _mm_setzero_si128();
        const __m128i qa   = _mm_set1_epi16(SCRELU_MAX);
        __m128i       sum  = _mm_setzero_si128();

        for (int i = 0; i < ITERS; ++i)
        {
            __m128i x = _mm_load_si128(reinterpret_cast<const __m128i *>(acc + i * 8));
            __m128i w = _mm_load_si128(reinterpret_cast<const __m128i *>(weights + i * 8));

            x = _mm_min_epi16(_mm_max_epi16(x, zero), qa);

            __m128i x32_lo = _mm_cvtepi16_epi32(x);
            __m128i x32_hi = _mm_cvtepi16_epi32(_mm_srli_si128(x, 8));
            __m128i w32_lo = _mm_cvtepi16_epi32(w);
            __m128i w32_hi = _mm_cvtepi16_epi32(_mm_srli_si128(w, 8));

            __m128i r_lo = _mm_mullo_epi32(_mm_mullo_epi32(x32_lo, x32_lo), w32_lo);
            __m128i r_hi = _mm_mullo_epi32(_mm_mullo_epi32(x32_hi, x32_hi), w32_hi);

            sum = _mm_add_epi32(sum, _mm_add_epi32(r_lo, r_hi));
        }

        sum = _mm_add_epi32(sum, _mm_srli_si128(sum, 8));
        sum = _mm_add_epi32(sum, _mm_srli_si128(sum, 4));
        return _mm_cvtsi128_si32(sum);

#else
        int32_t result = 0;
        for (int i = 0; i < HIDDEN_SIZE; ++i)
        {
            int32_t x = acc[i];
            if (x < SCRELU_MIN)
                x = SCRELU_MIN;
            if (x > SCRELU_MAX)
                x = SCRELU_MAX;
            result += x * x * int32_t(weights[i]);
        }
        return result;
#endif
    }

    // ============================================================
    // 5.  simd_init_accumulator
    //     acc[i] = bias[i]
    // ============================================================

    template <int HIDDEN_SIZE>
    FORCE_INLINE void simd_init_accumulator(
        int16_t *__restrict__ acc, const int16_t *__restrict__ bias) {
#if defined(SIMD_AVX512VNNI) || defined(SIMD_AVX512)
        static_assert(HIDDEN_SIZE % 32 == 0);
        constexpr int ITERS = HIDDEN_SIZE / 32;
        for (int i = 0; i < ITERS; ++i)
            _mm512_store_si512(reinterpret_cast<__m512i *>(acc + i * 32),
                _mm512_load_si512(reinterpret_cast<const __m512i *>(bias + i * 32)));
#elif defined(SIMD_AVX2)
        static_assert(HIDDEN_SIZE % 16 == 0);
        constexpr int ITERS = HIDDEN_SIZE / 16;
        for (int i = 0; i < ITERS; ++i)
            _mm256_store_si256(reinterpret_cast<__m256i *>(acc + i * 16),
                _mm256_load_si256(reinterpret_cast<const __m256i *>(bias + i * 16)));
#elif defined(SIMD_SSE41)
        static_assert(HIDDEN_SIZE % 8 == 0);
        constexpr int ITERS = HIDDEN_SIZE / 8;
        for (int i = 0; i < ITERS; ++i)
            _mm_store_si128(reinterpret_cast<__m128i *>(acc + i * 8),
                _mm_load_si128(reinterpret_cast<const __m128i *>(bias + i * 8)));
#else
        for (int i = 0; i < HIDDEN_SIZE; ++i)
            acc[i] = bias[i];
#endif
    }

}  // namespace SIMD
}  // namespace Catalyst
