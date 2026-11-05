/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * The 8-bit IDCT on x86: idct_neon.h's on SSE4.1 (four 32-bit lanes, a
 * row two vectors) and AVX2 (eight: a row one), each compiled for its set
 * and run where the CPU has it. The same bounds hold the same 32 bits
 * exactly: a pass's inputs within 35081 and 35079, past them the 64-bit
 * path.
 */

#ifndef src_io_jpg_idct_x86_h
#define src_io_jpg_idct_x86_h

#include <immintrin.h>

#if defined(__GNUC__) || defined(__clang__)
#  define JX_SSE41 __attribute__((target("sse4.1")))
#  define JX_AVX2  __attribute__((target("avx2")))
#else
#  define JX_SSE41
#  define JX_AVX2
#endif

#include "idct_pass.h"

/* what a block holds: only its DC (true; its samples written), or where
   its right half (columns 4–7) and bottom half (rows 4–7) aren't zero */
JX_SSE41 static inline bool
jx_sparse(const int16_t *in, const uint16_t *q, uint8_t *out, size_t stride, bool *right, bool *bottom) {
  __m128i  c[8], all, low;
  uint32_t i;

  for (i = 0; i < 8u; i++) c[i] = _mm_loadu_si128((const __m128i *)(const void *)(in + 8u * i));
  low     = _mm_or_si128(_mm_or_si128(c[1], c[2]), c[3]);
  all     = _mm_or_si128(_mm_or_si128(c[4], c[5]), _mm_or_si128(c[6], c[7]));
  *bottom = !_mm_testz_si128(all, all);
  all     = _mm_or_si128(all, low);
  low     = _mm_or_si128(all, _mm_insert_epi16(c[0], 0, 0));
  if (_mm_testz_si128(low, low)) {
    __m128i v = _mm_set1_epi8((char)jpg_dc8(in[0], q[0]));

    for (i = 0; i < 8u; i++) _mm_storel_epi64((__m128i *)(void *)(out + (size_t)i * stride), v);
    return true;
  }
  all    = _mm_or_si128(all, c[0]);
  *right = !_mm_testz_si128(all, _mm_set_epi64x(-1, 0));
  return false;
}

/* -- SSE4.1 ------------------------------------------------------------------ */

#define JX4_ADD(a, b) _mm_add_epi32(a, b)
#define JX4_SUB(a, b) _mm_sub_epi32(a, b)
#define JX4_MUL(a, k) _mm_mullo_epi32(a, _mm_set1_epi32(k))
#define JX4_SHL(a, n) _mm_slli_epi32(a, n)

JX_SSE41 static inline uint32_t
jx4_maxabs(const __m128i *v, uint32_t n) {
  __m128i  m = _mm_setzero_si128();
  uint32_t i, out[4];

  for (i = 0; i < n; i++) m = _mm_max_epu32(m, _mm_abs_epi32(v[i]));
  _mm_storeu_si128((__m128i *)out, m);
  return out[0] > out[1] ? (out[0] > out[2] ? (out[0] > out[3] ? out[0] : out[3]) : (out[2] > out[3] ? out[2] : out[3]))
                         : (out[1] > out[2] ? (out[1] > out[3] ? out[1] : out[3]) : (out[2] > out[3] ? out[2] : out[3]));
}

JX_SSE41 static inline void
jx4_tr(__m128i *a, __m128i *b, __m128i *c, __m128i *d) {
  __m128i t0 = _mm_unpacklo_epi32(*a, *b), t1 = _mm_unpacklo_epi32(*c, *d);
  __m128i t2 = _mm_unpackhi_epi32(*a, *b), t3 = _mm_unpackhi_epi32(*c, *d);

  *a = _mm_unpacklo_epi64(t0, t1);
  *b = _mm_unpackhi_epi64(t0, t1);
  *c = _mm_unpacklo_epi64(t2, t3);
  *d = _mm_unpackhi_epi64(t2, t3);
}

/* DESCALE(x, n): (x + 2^(n-1)) >> n, arithmetic */
#define JX4_DESCALE(x, n) _mm_srai_epi32(_mm_add_epi32(x, _mm_set1_epi32(1 << ((n) - 1))), n)

JX_SSE41 static bool
jpg_idct8_sse41(const int16_t *in, const uint16_t *q, uint8_t *out, size_t stride) {
  __m128i  lo[8], hi[8], olo[8], ohi[8];
  uint32_t i;
  bool     right = true, bottom = true;

  if (jx_sparse(in, q, out, stride, &right, &bottom)) return true;
  for (i = 0; i < 8u; i++) {
    __m128i c  = _mm_loadu_si128((const __m128i *)(in + 8u * i));
    __m128i qq = _mm_loadu_si128((const __m128i *)(q + 8u * i));
    __m128i pl = _mm_mullo_epi16(c, qq), ph = _mm_mulhi_epi16(c, qq);

    lo[i] = _mm_unpacklo_epi16(pl, ph); /* the exact 32-bit products */
    hi[i] = _mm_unpackhi_epi16(pl, ph);
  }
  if (jx4_maxabs(lo, 8u) > 35081u || (right && jx4_maxabs(hi, 8u) > 35081u)) return false;

  /* a zero half's pass skipped, or shortened */
  if (bottom) {
    JX_PASS(__m128i, JX4_ADD, JX4_SUB, JX4_MUL, JX4_SHL, lo, olo);
    if (right) JX_PASS(__m128i, JX4_ADD, JX4_SUB, JX4_MUL, JX4_SHL, hi, ohi);
  } else {
    JX_HALF(__m128i, JX4_ADD, JX4_SUB, JX4_MUL, JX4_SHL, lo, olo);
    if (right) JX_HALF(__m128i, JX4_ADD, JX4_SUB, JX4_MUL, JX4_SHL, hi, ohi);
  }
  for (i = 0; i < 8u; i++) olo[i] = JX4_DESCALE(olo[i], 11);
  if (jx4_maxabs(olo, 8u) > 35079u) return false;
  if (right) {
    for (i = 0; i < 8u; i++) ohi[i] = JX4_DESCALE(ohi[i], 11);
    if (jx4_maxabs(ohi, 8u) > 35079u) return false;
  }

  jx4_tr(&olo[0], &olo[1], &olo[2], &olo[3]);
  jx4_tr(&olo[4], &olo[5], &olo[6], &olo[7]);
  for (i = 0; i < 4u; i++) {
    lo[i] = olo[i];
    hi[i] = olo[i + 4u];
  }
  if (right) {
    jx4_tr(&ohi[0], &ohi[1], &ohi[2], &ohi[3]);
    jx4_tr(&ohi[4], &ohi[5], &ohi[6], &ohi[7]);
    for (i = 0; i < 4u; i++) {
      lo[i + 4u] = ohi[i];
      hi[i + 4u] = ohi[i + 4u];
    }
    JX_PASS(__m128i, JX4_ADD, JX4_SUB, JX4_MUL, JX4_SHL, lo, olo);
    JX_PASS(__m128i, JX4_ADD, JX4_SUB, JX4_MUL, JX4_SHL, hi, ohi);
  } else {
    JX_HALF(__m128i, JX4_ADD, JX4_SUB, JX4_MUL, JX4_SHL, lo, olo);
    JX_HALF(__m128i, JX4_ADD, JX4_SUB, JX4_MUL, JX4_SHL, hi, ohi);
  }

  /* o[x]: column x's rows; back to rows, wrapped and clamped as libjpeg */
  for (i = 0; i < 8u; i++) {
    olo[i] = JX4_DESCALE(olo[i], 18);
    ohi[i] = JX4_DESCALE(ohi[i], 18);
  }
  jx4_tr(&olo[0], &olo[1], &olo[2], &olo[3]);
  jx4_tr(&olo[4], &olo[5], &olo[6], &olo[7]);
  jx4_tr(&ohi[0], &ohi[1], &ohi[2], &ohi[3]);
  jx4_tr(&ohi[4], &ohi[5], &ohi[6], &ohi[7]);
  {
    const __m128i wrap = _mm_set1_epi32(1023), off = _mm_set1_epi32(512), back = _mm_set1_epi32(384);

    for (i = 0; i < 8u; i++) {
      /* row i: columns 0–3 in olo (rows 0–3) or ohi (rows 4–7), 4–7 beside */
      __m128i a = i < 4u ? olo[i] : ohi[i - 4u], b = i < 4u ? olo[i + 4u] : ohi[i];
      __m128i p;

      a = _mm_sub_epi32(_mm_and_si128(_mm_add_epi32(a, off), wrap), back);
      b = _mm_sub_epi32(_mm_and_si128(_mm_add_epi32(b, off), wrap), back);
      p = _mm_packus_epi16(_mm_packs_epi32(a, b), _mm_setzero_si128());
      _mm_storel_epi64((__m128i *)(out + (size_t)i * stride), p);
    }
  }
  return true;
}

/* -- AVX2 -------------------------------------------------------------------- */

#define JX8_ADD(a, b) _mm256_add_epi32(a, b)
#define JX8_SUB(a, b) _mm256_sub_epi32(a, b)
#define JX8_MUL(a, k) _mm256_mullo_epi32(a, _mm256_set1_epi32(k))
#define JX8_SHL(a, n) _mm256_slli_epi32(a, n)
#define JX8_DESCALE(x, n) _mm256_srai_epi32(_mm256_add_epi32(x, _mm256_set1_epi32(1 << ((n) - 1))), n)

JX_AVX2 static inline bool
jx8_within(const __m256i *v, int32_t bound) {
  __m256i m = _mm256_setzero_si256();
  uint32_t i;

  for (i = 0; i < 8u; i++) m = _mm256_max_epu32(m, _mm256_abs_epi32(v[i]));
  return _mm256_testz_si256(_mm256_cmpgt_epi32(m, _mm256_set1_epi32(bound)), _mm256_set1_epi32(-1));
}

/* 8 × 8 32-bit: rows to columns */
JX_AVX2 static inline void
jx8_tr(__m256i *r) {
  __m256i a0 = _mm256_unpacklo_epi32(r[0], r[1]), a1 = _mm256_unpackhi_epi32(r[0], r[1]);
  __m256i a2 = _mm256_unpacklo_epi32(r[2], r[3]), a3 = _mm256_unpackhi_epi32(r[2], r[3]);
  __m256i a4 = _mm256_unpacklo_epi32(r[4], r[5]), a5 = _mm256_unpackhi_epi32(r[4], r[5]);
  __m256i a6 = _mm256_unpacklo_epi32(r[6], r[7]), a7 = _mm256_unpackhi_epi32(r[6], r[7]);
  __m256i b0 = _mm256_unpacklo_epi64(a0, a2), b1 = _mm256_unpackhi_epi64(a0, a2);
  __m256i b2 = _mm256_unpacklo_epi64(a1, a3), b3 = _mm256_unpackhi_epi64(a1, a3);
  __m256i b4 = _mm256_unpacklo_epi64(a4, a6), b5 = _mm256_unpackhi_epi64(a4, a6);
  __m256i b6 = _mm256_unpacklo_epi64(a5, a7), b7 = _mm256_unpackhi_epi64(a5, a7);

  r[0] = _mm256_permute2x128_si256(b0, b4, 0x20);
  r[1] = _mm256_permute2x128_si256(b1, b5, 0x20);
  r[2] = _mm256_permute2x128_si256(b2, b6, 0x20);
  r[3] = _mm256_permute2x128_si256(b3, b7, 0x20);
  r[4] = _mm256_permute2x128_si256(b0, b4, 0x31);
  r[5] = _mm256_permute2x128_si256(b1, b5, 0x31);
  r[6] = _mm256_permute2x128_si256(b2, b6, 0x31);
  r[7] = _mm256_permute2x128_si256(b3, b7, 0x31);
}

JX_AVX2 static bool
jpg_idct8_avx2(const int16_t *in, const uint16_t *q, uint8_t *out, size_t stride) {
  __m256i  x[8], o[8];
  uint32_t i;
  bool     right = true, bottom = true;

  if (jx_sparse(in, q, out, stride, &right, &bottom)) return true;
  for (i = 0; i < 8u; i++) {
    x[i] = _mm256_mullo_epi32(_mm256_cvtepi16_epi32(_mm_loadu_si128((const __m128i *)(in + 8u * i))),
                              _mm256_cvtepi16_epi32(_mm_loadu_si128((const __m128i *)(q + 8u * i))));
  }
  if (!jx8_within(x, 35081)) return false;
  /* x[k] row k (lanes its columns), then o[k] column k (lanes its rows):
     a zero bottom or right half, a shorter pass */
  if (bottom) JX_PASS(__m256i, JX8_ADD, JX8_SUB, JX8_MUL, JX8_SHL, x, o);
  else JX_HALF(__m256i, JX8_ADD, JX8_SUB, JX8_MUL, JX8_SHL, x, o);
  for (i = 0; i < 8u; i++) o[i] = JX8_DESCALE(o[i], 11);
  if (!jx8_within(o, 35079)) return false;
  jx8_tr(o);
  if (right) JX_PASS(__m256i, JX8_ADD, JX8_SUB, JX8_MUL, JX8_SHL, o, x);
  else JX_HALF(__m256i, JX8_ADD, JX8_SUB, JX8_MUL, JX8_SHL, o, x);
  for (i = 0; i < 8u; i++) x[i] = JX8_DESCALE(x[i], 18);
  jx8_tr(x);
  {
    const __m256i wrap = _mm256_set1_epi32(1023), off = _mm256_set1_epi32(512), back = _mm256_set1_epi32(384);

    for (i = 0; i < 8u; i++) {
      __m256i v = _mm256_sub_epi32(_mm256_and_si256(_mm256_add_epi32(x[i], off), wrap), back);
      __m128i p = _mm_packs_epi32(_mm256_castsi256_si128(v), _mm256_extracti128_si256(v, 1));

      _mm_storel_epi64((__m128i *)(out + (size_t)i * stride), _mm_packus_epi16(p, p));
    }
  }
  return true;
}

#endif /* src_io_jpg_idct_x86_h */
