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
 * sample.c's 8-bit loops in WebAssembly's SIMD128: sample_neon.h's, the
 * same sums (so the plain loop's bytes); three and four samples a pixel
 * interleaved by shuffles, SIMD128 having no such stores.
 */

#ifndef src_io_jpg_sample_wasm_h
#define src_io_jpg_sample_wasm_h

#include <wasm_simd128.h>

/* a and b's bytes interleaved: a0 b0 a1 b1 …, the low eight's or the high */
#define JW_ZIP_LO(a, b) wasm_i8x16_shuffle(a, b, 0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23)
#define JW_ZIP_HI(a, b) wasm_i8x16_shuffle(a, b, 8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 31)

/* 16 pixels of three samples, 48 bytes */
IM_INLINE void
jw_store3(uint8_t *o, v128_t a, v128_t b, v128_t c) {
  v128_t lo = JW_ZIP_LO(a, b), hi = JW_ZIP_HI(a, b), t;

  wasm_v128_store(o, wasm_i8x16_shuffle(lo, c, 0, 1, 16, 2, 3, 17, 4, 5, 18, 6, 7, 19, 8, 9, 20, 10));
  t = wasm_i8x16_shuffle(lo, hi, 11, 0, 12, 13, 0, 14, 15, 0, 16, 17, 0, 18, 19, 0, 20, 21);
  wasm_v128_store(o + 16, wasm_i8x16_shuffle(t, c, 0, 21, 2, 3, 22, 5, 6, 23, 8, 9, 24, 11, 12, 25, 14, 15));
  wasm_v128_store(o + 32, wasm_i8x16_shuffle(hi, c, 26, 6, 7, 27, 8, 9, 28, 10, 11, 29, 12, 13, 30, 14, 15, 31));
}

/* 16 pixels of four samples, 64 bytes */
IM_INLINE void
jw_store4(uint8_t *o, v128_t a, v128_t b, v128_t c, v128_t d) {
  v128_t ablo = JW_ZIP_LO(a, b), abhi = JW_ZIP_HI(a, b), cdlo = JW_ZIP_LO(c, d), cdhi = JW_ZIP_HI(c, d);

  wasm_v128_store(o, wasm_i16x8_shuffle(ablo, cdlo, 0, 8, 1, 9, 2, 10, 3, 11));
  wasm_v128_store(o + 16, wasm_i16x8_shuffle(ablo, cdlo, 4, 12, 5, 13, 6, 14, 7, 15));
  wasm_v128_store(o + 32, wasm_i16x8_shuffle(abhi, cdhi, 0, 8, 1, 9, 2, 10, 3, 11));
  wasm_v128_store(o + 48, wasm_i16x8_shuffle(abhi, cdhi, 4, 12, 5, 13, 6, 14, 7, 15));
}

/* h2v1: in[-1] and in[n] the edge samples repeated; 2n out */
IM_INLINE void
jw_h2v1(const uint8_t *in, uint32_t n, uint8_t *out) {
  const v128_t three = wasm_i16x8_splat(3), one = wasm_i16x8_splat(1), two = wasm_i16x8_splat(2);
  uint32_t     i = 0;

  for (; i + 16u <= n; i += 16u) {
    v128_t a = wasm_v128_load(in + i), l = wasm_v128_load((in + i) - 1), r = wasm_v128_load(in + i + 1);
    v128_t a3l = wasm_i16x8_mul(wasm_u16x8_extend_low_u8x16(a), three);
    v128_t a3h = wasm_i16x8_mul(wasm_u16x8_extend_high_u8x16(a), three);
    v128_t e = wasm_u8x16_narrow_i16x8(
      wasm_u16x8_shr(wasm_i16x8_add(wasm_i16x8_add(a3l, wasm_u16x8_extend_low_u8x16(l)), one), 2),
      wasm_u16x8_shr(wasm_i16x8_add(wasm_i16x8_add(a3h, wasm_u16x8_extend_high_u8x16(l)), one), 2));
    v128_t d = wasm_u8x16_narrow_i16x8(
      wasm_u16x8_shr(wasm_i16x8_add(wasm_i16x8_add(a3l, wasm_u16x8_extend_low_u8x16(r)), two), 2),
      wasm_u16x8_shr(wasm_i16x8_add(wasm_i16x8_add(a3h, wasm_u16x8_extend_high_u8x16(r)), two), 2));

    wasm_v128_store(out + 2u * i, JW_ZIP_LO(e, d));
    wasm_v128_store(out + 2u * i + 16u, JW_ZIP_HI(e, d));
  }
  for (; i < n; i++) {
    uint32_t v = in[i] * 3u;

    out[2u * i]      = (uint8_t)((v + (in + i)[-1] + 1u) >> 2);
    out[2u * i + 1u] = (uint8_t)((v + (in + i)[1] + 2u) >> 2);
  }
}

/* the column sums 3·near + far of h2v2 and h1v2: s[-1], s[n] the edges' */
IM_INLINE void
jw_colsum(const uint8_t *near, const uint8_t *far, uint32_t n, uint16_t *s) {
  const v128_t three = wasm_i16x8_splat(3);
  uint32_t     i = 0;

  for (; i + 16u <= n; i += 16u) {
    v128_t a = wasm_v128_load(near + i), b = wasm_v128_load(far + i);

    wasm_v128_store(s + i, wasm_i16x8_add(wasm_i16x8_mul(wasm_u16x8_extend_low_u8x16(a), three), wasm_u16x8_extend_low_u8x16(b)));
    wasm_v128_store(s + i + 8u,
                    wasm_i16x8_add(wasm_i16x8_mul(wasm_u16x8_extend_high_u8x16(a), three), wasm_u16x8_extend_high_u8x16(b)));
  }
  for (; i < n; i++) s[i] = (uint16_t)(near[i] * 3u + far[i]);
  s[-1] = s[0];
  s[n]  = s[n - 1u];
}

/* h2v2 from the column sums: 2n out */
IM_INLINE void
jw_h2v2(const uint16_t *s, uint32_t n, uint8_t *out) {
  const v128_t three = wasm_i16x8_splat(3), eight = wasm_i16x8_splat(8), seven = wasm_i16x8_splat(7);
  uint32_t     i = 0;

  for (; i + 8u <= n; i += 8u) {
    v128_t c3 = wasm_i16x8_mul(wasm_v128_load(s + i), three);
    v128_t e  = wasm_u16x8_shr(wasm_i16x8_add(wasm_i16x8_add(c3, wasm_v128_load((s + i) - 1)), eight), 4);
    v128_t d  = wasm_u16x8_shr(wasm_i16x8_add(wasm_i16x8_add(c3, wasm_v128_load(s + i + 1)), seven), 4);

    wasm_v128_store(out + 2u * i, JW_ZIP_LO(wasm_u8x16_narrow_i16x8(e, e), wasm_u8x16_narrow_i16x8(d, d)));
  }
  for (; i < n; i++) {
    uint32_t v = s[i] * 3u;

    out[2u * i]      = (uint8_t)((v + (s + i)[-1] + 8u) >> 4);
    out[2u * i + 1u] = (uint8_t)((v + (s + i)[1] + 7u) >> 4);
  }
}

/* h1v2 from the column sums, `bias` 1 (upper row) or 2 */
IM_INLINE void
jw_h1v2(const uint16_t *s, uint32_t n, uint32_t bias, uint8_t *out) {
  const v128_t b = wasm_i16x8_splat((int16_t)bias);
  uint32_t     i = 0;

  for (; i + 8u <= n; i += 8u) {
    v128_t v = wasm_u16x8_shr(wasm_i16x8_add(wasm_v128_load(s + i), b), 2);

    wasm_v128_store64_lane(out + i, wasm_u8x16_narrow_i16x8(v, v), 0);
  }
  for (; i < n; i++) out[i] = (uint8_t)((s[i] + bias) >> 2);
}

/* (a · k + 2^15) >> 16 for eight int16 a, eight int16 out */
IM_INLINE v128_t
jw_mulr16(v128_t a, int16_t k) {
  const v128_t kk = wasm_i16x8_splat(k), half = wasm_i32x4_splat(32768);

  return wasm_i16x8_narrow_i32x4(wasm_i32x4_shr(wasm_i32x4_add(wasm_i32x4_extmul_low_i16x8(a, kk), half), 16),
                                 wasm_i32x4_shr(wasm_i32x4_add(wasm_i32x4_extmul_high_i16x8(a, kk), half), 16));
}

/* libjpeg's R, G and B offsets from Cb and Cr (centred), as jn_ycc_terms */
IM_INLINE void
jw_ycc_terms(v128_t cb, v128_t cr, v128_t *tr, v128_t *tg, v128_t *tb) {
  const v128_t kb = wasm_i16x8_splat(-22554), kr = wasm_i16x8_splat(18734), half = wasm_i32x4_splat(32768);
  v128_t       gl = wasm_i32x4_add(wasm_i32x4_extmul_low_i16x8(cb, kb), wasm_i32x4_extmul_low_i16x8(cr, kr));
  v128_t       gh = wasm_i32x4_add(wasm_i32x4_extmul_high_i16x8(cb, kb), wasm_i32x4_extmul_high_i16x8(cr, kr));

  *tr = wasm_i16x8_add(cr, jw_mulr16(cr, 26345));
  *tb = wasm_i16x8_add(wasm_i16x8_shl(cb, 1), jw_mulr16(cb, -14942));
  *tg = wasm_i16x8_sub(wasm_i16x8_narrow_i32x4(wasm_i32x4_shr(wasm_i32x4_add(gl, half), 16),
                                               wasm_i32x4_shr(wasm_i32x4_add(gh, half), 16)),
                       cr);
}

/* 16 pixels' R, G and B from their Y, Cb and Cr */
IM_INLINE void
jw_ycc16(v128_t yy, v128_t b8, v128_t r8, v128_t *R, v128_t *G, v128_t *B) {
  const v128_t c128 = wasm_i16x8_splat(128);
  v128_t       trl, tgl, tbl, trh, tgh, tbh, yl, yh;

  jw_ycc_terms(wasm_i16x8_sub(wasm_u16x8_extend_low_u8x16(b8), c128), wasm_i16x8_sub(wasm_u16x8_extend_low_u8x16(r8), c128),
               &trl, &tgl, &tbl);
  jw_ycc_terms(wasm_i16x8_sub(wasm_u16x8_extend_high_u8x16(b8), c128),
               wasm_i16x8_sub(wasm_u16x8_extend_high_u8x16(r8), c128), &trh, &tgh, &tbh);
  yl = wasm_u16x8_extend_low_u8x16(yy);
  yh = wasm_u16x8_extend_high_u8x16(yy);
  *R = wasm_u8x16_narrow_i16x8(wasm_i16x8_add(yl, trl), wasm_i16x8_add(yh, trh));
  *G = wasm_u8x16_narrow_i16x8(wasm_i16x8_add(yl, tgl), wasm_i16x8_add(yh, tgh));
  *B = wasm_u8x16_narrow_i16x8(wasm_i16x8_add(yl, tbl), wasm_i16x8_add(yh, tbh));
}

/* YCbCr to RGB (`k` NULL) or YCCK to CMYK: CMY 255 minus RGB, K as it is;
   `n` pixels, 3 or 4 bytes each */
IM_INLINE uint32_t
jw_ycc(const uint8_t *y, const uint8_t *cb, const uint8_t *cr, const uint8_t *k, uint8_t *o, uint32_t n) {
  uint32_t i = 0;

  for (; i + 16u <= n; i += 16u) {
    v128_t R, G, B;

    jw_ycc16(wasm_v128_load(y + i), wasm_v128_load(cb + i), wasm_v128_load(cr + i), &R, &G, &B);
    if (!k) jw_store3(o + 3u * i, R, G, B);
    else jw_store4(o + 4u * i, wasm_v128_not(R), wasm_v128_not(G), wasm_v128_not(B), wasm_v128_load(k + i));
  }
  return i;
}

/* components side by side: 3 or 4 rows interleaved */
IM_INLINE uint32_t
jw_interleave(const uint8_t *const *row, uint32_t ncomp, uint8_t *o, uint32_t n) {
  uint32_t i = 0;

  if (ncomp == 3u) {
    for (; i + 16u <= n; i += 16u) {
      jw_store3(o + 3u * i, wasm_v128_load(row[0] + i), wasm_v128_load(row[1] + i), wasm_v128_load(row[2] + i));
    }
  } else if (ncomp == 4u) {
    for (; i + 16u <= n; i += 16u) {
      jw_store4(o + 4u * i, wasm_v128_load(row[0] + i), wasm_v128_load(row[1] + i), wasm_v128_load(row[2] + i),
                wasm_v128_load(row[3] + i));
    }
  }
  return i;
}

/* RGB to RGB0 (the 4th 255), four pixels a step; the pixels done */
IM_INLINE uint32_t
jw_pad(const uint8_t *in, uint32_t w, uint8_t *out) {
  const v128_t ff = wasm_i8x16_splat(-1);
  uint32_t     i = 0;

  for (; 3u * i + 16u <= 3u * w; i += 4u) {
    wasm_v128_store(out + 4u * i,
                    wasm_i8x16_shuffle(wasm_v128_load(in + 3u * i), ff, 0, 1, 2, 16, 3, 4, 5, 16, 6, 7, 8, 16, 9, 10, 11, 16));
  }
  return i;
}

#endif /* src_io_jpg_sample_wasm_h */
