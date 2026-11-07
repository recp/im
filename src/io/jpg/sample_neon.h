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
 * sample.c's 8-bit loops in NEON, each the plain loop's bytes.
 *
 * Fancy upsampling is its general formula with the edge sample repeated
 * past each end (what libjpeg's special first and last columns give), so a
 * row is one vector loop over a copy with a sample of room each side.
 * YCbCr to RGB is libjpeg's tables computed in 16-bit lanes, each constant
 * split so its products fit: (91881 x + 32768) >> 16 = x + ((26345 x +
 * 32768) >> 16), (116130 x + 32768) >> 16 = 2x + ((-14942 x + 32768) >>
 * 16) (both one rounding multiply-high each: 26346 gives 26345's for every
 * centred x), and green's -46802 cr = -65536 cr + 18734 cr.
 */

#ifndef src_io_jpg_sample_neon_h
#define src_io_jpg_sample_neon_h

#include <arm_neon.h>

/* h2v1: in[-1] and in[n] the edge samples repeated; 2n out */
IM_INLINE void
jn_h2v1(const uint8_t *in, uint32_t n, uint8_t *out) {
  uint32_t i = 0;

  for (; i + 16u <= n; i += 16u) {
    uint8x16_t a = vld1q_u8(in + i), l = vld1q_u8((in + i) - 1), r = vld1q_u8(in + i + 1);
    uint16x8_t a3l = vmull_u8(vget_low_u8(a), vdup_n_u8(3)), a3h = vmull_high_u8(a, vdupq_n_u8(3));
    uint8x16x2_t o;

    o.val[0] = vcombine_u8(vshrn_n_u16(vaddq_u16(vaddw_u8(a3l, vget_low_u8(l)), vdupq_n_u16(1)), 2),
                           vshrn_n_u16(vaddq_u16(vaddw_high_u8(a3h, l), vdupq_n_u16(1)), 2));
    o.val[1] = vcombine_u8(vrshrn_n_u16(vaddw_u8(a3l, vget_low_u8(r)), 2), vrshrn_n_u16(vaddw_high_u8(a3h, r), 2));
    vst2q_u8(out + 2u * i, o);
  }
  for (; i < n; i++) {
    uint32_t v = in[i] * 3u;

    out[2u * i]      = (uint8_t)((v + (in + i)[-1] + 1u) >> 2);
    out[2u * i + 1u] = (uint8_t)((v + (in + i)[1] + 2u) >> 2);
  }
}

/* the column sums 3·near + far of h2v2 and h1v2: s[-1], s[n] the edges' */
IM_INLINE void
jn_colsum(const uint8_t *rnear, const uint8_t *rfar, uint32_t n, uint16_t *s) {
  uint32_t i = 0;

  for (; i + 16u <= n; i += 16u) {
    uint8x16_t a = vld1q_u8(rnear + i), b = vld1q_u8(rfar + i);

    vst1q_u16(s + i, vmlal_u8(vmovl_u8(vget_low_u8(b)), vget_low_u8(a), vdup_n_u8(3)));
    vst1q_u16(s + i + 8u, vmlal_high_u8(vmovl_high_u8(b), a, vdupq_n_u8(3)));
  }
  for (; i < n; i++) s[i] = (uint16_t)(rnear[i] * 3u + rfar[i]);
  s[-1] = s[0];
  s[n]  = s[n - 1u];
}

/* h2v2 from the column sums: 2n out */
IM_INLINE void
jn_h2v2(const uint16_t *s, uint32_t n, uint8_t *out) {
  uint32_t i = 0;

  for (; i + 8u <= n; i += 8u) {
    uint16x8_t   c = vld1q_u16(s + i), l = vld1q_u16((s + i) - 1), r = vld1q_u16(s + i + 1);
    uint16x8_t   c3 = vmulq_n_u16(c, 3);
    uint8x8x2_t  o;

    o.val[0] = vrshrn_n_u16(vaddq_u16(c3, l), 4);
    o.val[1] = vshrn_n_u16(vaddq_u16(vaddq_u16(c3, r), vdupq_n_u16(7)), 4);
    vst2_u8(out + 2u * i, o);
  }
  for (; i < n; i++) {
    uint32_t v = s[i] * 3u;

    out[2u * i]      = (uint8_t)((v + (s + i)[-1] + 8u) >> 4);
    out[2u * i + 1u] = (uint8_t)((v + (s + i)[1] + 7u) >> 4);
  }
}

/* h1v2 from the column sums, `bias` 1 (upper row) or 2 */
IM_INLINE void
jn_h1v2(const uint16_t *s, uint32_t n, uint32_t bias, uint8_t *out) {
  uint32_t   i = 0;
  uint16x8_t b = vdupq_n_u16((uint16_t)bias);

  for (; i + 8u <= n; i += 8u) vst1_u8(out + i, vshrn_n_u16(vaddq_u16(vld1q_u16(s + i), b), 2));
  for (; i < n; i++) out[i] = (uint8_t)((s[i] + bias) >> 2);
}

/* libjpeg's R, G and B offsets from Cb and Cr (centred) */
IM_INLINE void
jn_ycc_terms(int16x8_t cb, int16x8_t cr, int16x8_t *tr, int16x8_t *tg, int16x8_t *tb) {
  int32x4_t gl = vmlal_n_s16(vmull_n_s16(vget_low_s16(cb), -22554), vget_low_s16(cr), 18734);
  int32x4_t gh = vmlal_high_n_s16(vmull_high_n_s16(cb, -22554), cr, 18734);

  /* (k x + 2^15) >> 16 in one rounding doubling multiply (k/2): 26346 is
     26345 for every centred 8-bit x, -14942 itself */
  *tr = vaddq_s16(cr, vqrdmulhq_n_s16(cr, 13173));
  *tb = vaddq_s16(vshlq_n_s16(cb, 1), vqrdmulhq_n_s16(cb, -7471));
  *tg = vsubq_s16(vcombine_s16(vrshrn_n_s32(gl, 16), vrshrn_n_s32(gh, 16)), cr);
}

/* YCbCr to RGB (`k` NULL) or YCCK to CMYK: CMY 255 minus RGB, K as it is;
   `n` pixels, 3 or 4 bytes each */
IM_INLINE uint32_t
jn_ycc(const uint8_t *y, const uint8_t *cb, const uint8_t *cr, const uint8_t *k, uint8_t *o, uint32_t n) {
  const int16x8_t c128 = vdupq_n_s16(128);
  uint32_t        i = 0;

  for (; i + 16u <= n; i += 16u) {
    uint8x16_t yy = vld1q_u8(y + i), b8 = vld1q_u8(cb + i), r8 = vld1q_u8(cr + i);
    int16x8_t  trl, tgl, tbl, trh, tgh, tbh, yl, yh;
    uint8x16_t R, G, B;

    jn_ycc_terms(vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(b8))), c128),
                 vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(r8))), c128), &trl, &tgl, &tbl);
    jn_ycc_terms(vsubq_s16(vreinterpretq_s16_u16(vmovl_high_u8(b8)), c128),
                 vsubq_s16(vreinterpretq_s16_u16(vmovl_high_u8(r8)), c128), &trh, &tgh, &tbh);
    yl = vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(yy)));
    yh = vreinterpretq_s16_u16(vmovl_high_u8(yy));
    R  = vcombine_u8(vqmovun_s16(vaddq_s16(yl, trl)), vqmovun_s16(vaddq_s16(yh, trh)));
    G  = vcombine_u8(vqmovun_s16(vaddq_s16(yl, tgl)), vqmovun_s16(vaddq_s16(yh, tgh)));
    B  = vcombine_u8(vqmovun_s16(vaddq_s16(yl, tbl)), vqmovun_s16(vaddq_s16(yh, tbh)));
    if (!k) {
      uint8x16x3_t px = { { R, G, B } };

      vst3q_u8(o + 3u * i, px);
    } else {
      uint8x16x4_t px = { { vmvnq_u8(R), vmvnq_u8(G), vmvnq_u8(B), vld1q_u8(k + i) } };

      vst4q_u8(o + 4u * i, px);
    }
  }
  return i;
}

/* 16 pixels' RGB from their Y and centred Cb, Cr (two vectors each) */
IM_INLINE void
jn_rgb16(uint8x16_t y, const int16x8_t *cb, const int16x8_t *cr, uint8x16x3_t *px) {
  int16x8_t tr0, tg0, tb0, tr1, tg1, tb1;

  jn_ycc_terms(cb[0], cr[0], &tr0, &tg0, &tb0);
  jn_ycc_terms(cb[1], cr[1], &tr1, &tg1, &tb1);
#define JN_ADDY(t0, t1)                                                        \
  vqmovun_high_s16(vqmovun_s16(vreinterpretq_s16_u16(vaddw_u8(vreinterpretq_u16_s16(t0), vget_low_u8(y)))), \
                   vreinterpretq_s16_u16(vaddw_high_u8(vreinterpretq_u16_s16(t1), y)))
  px->val[0] = JN_ADDY(tr0, tr1);
  px->val[1] = JN_ADDY(tg0, tg1);
  px->val[2] = JN_ADDY(tb0, tb1);
#undef JN_ADDY
}

/* h2v2's two chroma rows straight into YCbCr to RGB: sb, sr their column
   sums (jn_colsum's: [-1] and [n] the edges'), w pixels from y. A sum's
   even output (3c + l + 8) >> 4 and odd (3c + r + 7) >> 4 are (t + l + 1)
   >> 4 and (t + r) >> 4 for t = 3c + 7, a halving add and a shift each;
   t less 128 · 16 gives them centred, as the colour wants. 32 pixels a
   step (Y as even and odd ones, zipped back at the end); those done back */
IM_INLINE uint32_t
jn_ycc_h2v2(const uint8_t *y, const uint16_t *sb, const uint16_t *sr, uint32_t w, uint8_t *o) {
  const int16x8_t k = vdupq_n_s16(7 - 2048);
  uint32_t        x = 0;

  for (; x + 32u <= w; x += 32u) {
    const int16_t *b = (const int16_t *)sb + x / 2u, *r = (const int16_t *)sr + x / 2u;
    uint8x16x2_t   yy = vld2q_u8(y + x);
    int16x8_t      be[2], bo[2], re[2], ro[2];
    uint8x16x3_t   pe, po, p0, p1;
    uint32_t       h;

    for (h = 0; h < 2u; h++) {
      int16x8_t tb = vmlaq_n_s16(k, vld1q_s16(b + 8u * h), 3), tr = vmlaq_n_s16(k, vld1q_s16(r + 8u * h), 3);

      be[h] = vshrq_n_s16(vrhaddq_s16(tb, vld1q_s16(b + 8u * h - 1)), 3);
      bo[h] = vshrq_n_s16(vhaddq_s16(tb, vld1q_s16(b + 8u * h + 1u)), 3);
      re[h] = vshrq_n_s16(vrhaddq_s16(tr, vld1q_s16(r + 8u * h - 1)), 3);
      ro[h] = vshrq_n_s16(vhaddq_s16(tr, vld1q_s16(r + 8u * h + 1u)), 3);
    }
    jn_rgb16(yy.val[0], be, re, &pe);
    jn_rgb16(yy.val[1], bo, ro, &po);
    for (h = 0; h < 3u; h++) {
      p0.val[h] = vzip1q_u8(pe.val[h], po.val[h]);
      p1.val[h] = vzip2q_u8(pe.val[h], po.val[h]);
    }
    vst3q_u8(o + 3u * x, p0);
    vst3q_u8(o + 3u * x + 48u, p1);
  }
  return x;
}

/* components side by side: 3 or 4 rows interleaved */
IM_INLINE uint32_t
jn_interleave(const uint8_t *const *row, uint32_t ncomp, uint8_t *o, uint32_t n) {
  uint32_t i = 0;

  if (ncomp == 3u) {
    for (; i + 16u <= n; i += 16u) {
      uint8x16x3_t px = { { vld1q_u8(row[0] + i), vld1q_u8(row[1] + i), vld1q_u8(row[2] + i) } };

      vst3q_u8(o + 3u * i, px);
    }
  } else if (ncomp == 4u) {
    for (; i + 16u <= n; i += 16u) {
      uint8x16x4_t px = { { vld1q_u8(row[0] + i), vld1q_u8(row[1] + i), vld1q_u8(row[2] + i), vld1q_u8(row[3] + i) } };

      vst4q_u8(o + 4u * i, px);
    }
  }
  return i;
}

#endif /* src_io_jpg_sample_neon_h */
