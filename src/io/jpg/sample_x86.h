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
 * sample.c's 8-bit loops on x86 (SSSE3 and SSE4.1, compiled for them and
 * run where the CPU has them), each the plain loop's bytes: sample_neon.h's
 * formulas. libjpeg's (x c + 32768) >> 16 in 16-bit lanes is the product's
 * high half plus its low half's top bit; green's two products one madd.
 */

#ifndef src_io_jpg_sample_x86_h
#define src_io_jpg_sample_x86_h

#include <immintrin.h>

#if defined(__GNUC__) || defined(__clang__)
#  define JX_SSSE3 __attribute__((target("ssse3,sse4.1")))
#else
#  define JX_SSSE3
#endif

JX_SSSE3 static inline void
jx_h2v1(const uint8_t *in, uint32_t n, uint8_t *out) {
  const __m128i z = _mm_setzero_si128(), one = _mm_set1_epi16(1), two = _mm_set1_epi16(2), three = _mm_set1_epi16(3);
  uint32_t      i = 0;

  for (; i + 16u <= n; i += 16u) {
    __m128i a = _mm_loadu_si128((const __m128i *)(in + i));
    __m128i l = _mm_loadu_si128((const __m128i *)((in + i) - 1));
    __m128i r = _mm_loadu_si128((const __m128i *)(in + i + 1));
    __m128i al = _mm_mullo_epi16(_mm_unpacklo_epi8(a, z), three), ah = _mm_mullo_epi16(_mm_unpackhi_epi8(a, z), three);
    __m128i el = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(al, _mm_unpacklo_epi8(l, z)), one), 2);
    __m128i eh = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(ah, _mm_unpackhi_epi8(l, z)), one), 2);
    __m128i ol = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(al, _mm_unpacklo_epi8(r, z)), two), 2);
    __m128i oh = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(ah, _mm_unpackhi_epi8(r, z)), two), 2);
    __m128i e = _mm_packus_epi16(el, eh), o = _mm_packus_epi16(ol, oh);

    _mm_storeu_si128((__m128i *)(out + 2u * i), _mm_unpacklo_epi8(e, o));
    _mm_storeu_si128((__m128i *)(out + 2u * i + 16u), _mm_unpackhi_epi8(e, o));
  }
  for (; i < n; i++) {
    uint32_t v = in[i] * 3u;

    out[2u * i]      = (uint8_t)((v + (in + i)[-1] + 1u) >> 2);
    out[2u * i + 1u] = (uint8_t)((v + (in + i)[1] + 2u) >> 2);
  }
}

JX_SSSE3 static inline void
jx_colsum(const uint8_t *rnear, const uint8_t *rfar, uint32_t n, uint16_t *s) {
  const __m128i z = _mm_setzero_si128(), three = _mm_set1_epi16(3);
  uint32_t      i = 0;

  for (; i + 16u <= n; i += 16u) {
    __m128i a = _mm_loadu_si128((const __m128i *)(rnear + i)), b = _mm_loadu_si128((const __m128i *)(rfar + i));

    _mm_storeu_si128((__m128i *)(s + i), _mm_add_epi16(_mm_mullo_epi16(_mm_unpacklo_epi8(a, z), three), _mm_unpacklo_epi8(b, z)));
    _mm_storeu_si128((__m128i *)(s + i + 8u), _mm_add_epi16(_mm_mullo_epi16(_mm_unpackhi_epi8(a, z), three), _mm_unpackhi_epi8(b, z)));
  }
  for (; i < n; i++) s[i] = (uint16_t)(rnear[i] * 3u + rfar[i]);
  s[-1] = s[0];
  s[n]  = s[n - 1u];
}

JX_SSSE3 static inline void
jx_h2v2(const uint16_t *s, uint32_t n, uint8_t *out) {
  const __m128i three = _mm_set1_epi16(3), eight = _mm_set1_epi16(8), seven = _mm_set1_epi16(7);
  uint32_t      i = 0;

  for (; i + 8u <= n; i += 8u) {
    __m128i c = _mm_loadu_si128((const __m128i *)(s + i));
    __m128i l = _mm_loadu_si128((const __m128i *)((s + i) - 1)), r = _mm_loadu_si128((const __m128i *)(s + i + 1));
    __m128i c3 = _mm_mullo_epi16(c, three);
    __m128i e = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(c3, l), eight), 4);
    __m128i o = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(c3, r), seven), 4);
    __m128i p = _mm_packus_epi16(e, o); /* e in the low 8, o the high */

    _mm_storeu_si128((__m128i *)(out + 2u * i), _mm_unpacklo_epi8(p, _mm_srli_si128(p, 8)));
  }
  for (; i < n; i++) {
    uint32_t v = s[i] * 3u;

    out[2u * i]      = (uint8_t)((v + (s + i)[-1] + 8u) >> 4);
    out[2u * i + 1u] = (uint8_t)((v + (s + i)[1] + 7u) >> 4);
  }
}

JX_SSSE3 static inline void
jx_h1v2(const uint16_t *s, uint32_t n, uint32_t bias, uint8_t *out) {
  const __m128i b = _mm_set1_epi16((short)bias);
  uint32_t      i = 0;

  for (; i + 8u <= n; i += 8u) {
    __m128i v = _mm_srli_epi16(_mm_add_epi16(_mm_loadu_si128((const __m128i *)(s + i)), b), 2);

    _mm_storel_epi64((__m128i *)(out + i), _mm_packus_epi16(v, v));
  }
  for (; i < n; i++) out[i] = (uint8_t)((s[i] + bias) >> 2);
}

/* (x c + 32768) >> 16, exactly, a 16-bit lane at a time */
JX_SSSE3 static inline __m128i
jx_round16(__m128i x, short c) {
  __m128i k = _mm_set1_epi16(c);

  return _mm_add_epi16(_mm_mulhi_epi16(x, k), _mm_srli_epi16(_mm_mullo_epi16(x, k), 15));
}

/* 16 R, G, B (and K) bytes to 48 (64) interleaved */
JX_SSSE3 static inline void
jx_store3(uint8_t *o, __m128i r, __m128i g, __m128i b) {
  const __m128i r0 = _mm_setr_epi8(0, -1, -1, 1, -1, -1, 2, -1, -1, 3, -1, -1, 4, -1, -1, 5);
  const __m128i g0 = _mm_setr_epi8(-1, 0, -1, -1, 1, -1, -1, 2, -1, -1, 3, -1, -1, 4, -1, -1);
  const __m128i b0 = _mm_setr_epi8(-1, -1, 0, -1, -1, 1, -1, -1, 2, -1, -1, 3, -1, -1, 4, -1);
  const __m128i r1 = _mm_setr_epi8(-1, -1, 6, -1, -1, 7, -1, -1, 8, -1, -1, 9, -1, -1, 10, -1);
  const __m128i g1 = _mm_setr_epi8(5, -1, -1, 6, -1, -1, 7, -1, -1, 8, -1, -1, 9, -1, -1, 10);
  const __m128i b1 = _mm_setr_epi8(-1, 5, -1, -1, 6, -1, -1, 7, -1, -1, 8, -1, -1, 9, -1, -1);
  const __m128i r2 = _mm_setr_epi8(-1, 11, -1, -1, 12, -1, -1, 13, -1, -1, 14, -1, -1, 15, -1, -1);
  const __m128i g2 = _mm_setr_epi8(-1, -1, 11, -1, -1, 12, -1, -1, 13, -1, -1, 14, -1, -1, 15, -1);
  const __m128i b2 = _mm_setr_epi8(10, -1, -1, 11, -1, -1, 12, -1, -1, 13, -1, -1, 14, -1, -1, 15);

  _mm_storeu_si128((__m128i *)o, _mm_or_si128(_mm_or_si128(_mm_shuffle_epi8(r, r0), _mm_shuffle_epi8(g, g0)), _mm_shuffle_epi8(b, b0)));
  _mm_storeu_si128((__m128i *)(o + 16), _mm_or_si128(_mm_or_si128(_mm_shuffle_epi8(r, r1), _mm_shuffle_epi8(g, g1)), _mm_shuffle_epi8(b, b1)));
  _mm_storeu_si128((__m128i *)(o + 32), _mm_or_si128(_mm_or_si128(_mm_shuffle_epi8(r, r2), _mm_shuffle_epi8(g, g2)), _mm_shuffle_epi8(b, b2)));
}

JX_SSSE3 static inline void
jx_store4(uint8_t *o, __m128i c, __m128i m, __m128i y, __m128i k) {
  __m128i cm0 = _mm_unpacklo_epi8(c, m), cm1 = _mm_unpackhi_epi8(c, m);
  __m128i yk0 = _mm_unpacklo_epi8(y, k), yk1 = _mm_unpackhi_epi8(y, k);

  _mm_storeu_si128((__m128i *)o, _mm_unpacklo_epi16(cm0, yk0));
  _mm_storeu_si128((__m128i *)(o + 16), _mm_unpackhi_epi16(cm0, yk0));
  _mm_storeu_si128((__m128i *)(o + 32), _mm_unpacklo_epi16(cm1, yk1));
  _mm_storeu_si128((__m128i *)(o + 48), _mm_unpackhi_epi16(cm1, yk1));
}

/* 8 pixels' R, G, B offsets (16-bit) from centred Cb, Cr */
JX_SSSE3 static inline void
jx_terms(__m128i cb, __m128i cr, __m128i *tr, __m128i *tg, __m128i *tb) {
  const __m128i gk = _mm_setr_epi16(-22554, 18734, -22554, 18734, -22554, 18734, -22554, 18734);
  const __m128i half = _mm_set1_epi32(32768);
  __m128i       gl = _mm_srai_epi32(_mm_add_epi32(_mm_madd_epi16(_mm_unpacklo_epi16(cb, cr), gk), half), 16);
  __m128i       gh = _mm_srai_epi32(_mm_add_epi32(_mm_madd_epi16(_mm_unpackhi_epi16(cb, cr), gk), half), 16);

  *tr = _mm_add_epi16(cr, jx_round16(cr, 26345));
  *tb = _mm_add_epi16(_mm_slli_epi16(cb, 1), jx_round16(cb, -14942));
  *tg = _mm_sub_epi16(_mm_packs_epi32(gl, gh), cr);
}

JX_SSSE3 static uint32_t
jx_ycc(const uint8_t *y, const uint8_t *cb, const uint8_t *cr, const uint8_t *k, uint8_t *o, uint32_t n) {
  const __m128i z = _mm_setzero_si128(), c128 = _mm_set1_epi16(128), ones = _mm_set1_epi8(-1);
  uint32_t      i = 0;

  for (; i + 16u <= n; i += 16u) {
    __m128i yy = _mm_loadu_si128((const __m128i *)(y + i)), b8 = _mm_loadu_si128((const __m128i *)(cb + i));
    __m128i r8 = _mm_loadu_si128((const __m128i *)(cr + i));
    __m128i trl, tgl, tbl, trh, tgh, tbh, yl, yh, R, G, B;

    jx_terms(_mm_sub_epi16(_mm_unpacklo_epi8(b8, z), c128), _mm_sub_epi16(_mm_unpacklo_epi8(r8, z), c128), &trl, &tgl, &tbl);
    jx_terms(_mm_sub_epi16(_mm_unpackhi_epi8(b8, z), c128), _mm_sub_epi16(_mm_unpackhi_epi8(r8, z), c128), &trh, &tgh, &tbh);
    yl = _mm_unpacklo_epi8(yy, z);
    yh = _mm_unpackhi_epi8(yy, z);
    R  = _mm_packus_epi16(_mm_add_epi16(yl, trl), _mm_add_epi16(yh, trh));
    G  = _mm_packus_epi16(_mm_add_epi16(yl, tgl), _mm_add_epi16(yh, tgh));
    B  = _mm_packus_epi16(_mm_add_epi16(yl, tbl), _mm_add_epi16(yh, tbh));
    if (!k) jx_store3(o + 3u * i, R, G, B);
    else jx_store4(o + 4u * i, _mm_xor_si128(R, ones), _mm_xor_si128(G, ones), _mm_xor_si128(B, ones),
                   _mm_loadu_si128((const __m128i *)(k + i)));
  }
  return i;
}

JX_SSSE3 static uint32_t
jx_interleave(const uint8_t *const *row, uint32_t ncomp, uint8_t *o, uint32_t n) {
  uint32_t i = 0;

  if (ncomp == 3u) {
    for (; i + 16u <= n; i += 16u) {
      jx_store3(o + 3u * i, _mm_loadu_si128((const __m128i *)(row[0] + i)), _mm_loadu_si128((const __m128i *)(row[1] + i)),
                _mm_loadu_si128((const __m128i *)(row[2] + i)));
    }
  } else if (ncomp == 4u) {
    for (; i + 16u <= n; i += 16u) {
      jx_store4(o + 4u * i, _mm_loadu_si128((const __m128i *)(row[0] + i)), _mm_loadu_si128((const __m128i *)(row[1] + i)),
                _mm_loadu_si128((const __m128i *)(row[2] + i)), _mm_loadu_si128((const __m128i *)(row[3] + i)));
    }
  }
  return i;
}

#endif /* src_io_jpg_sample_x86_h */
