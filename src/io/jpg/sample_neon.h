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
 * 16), and green's -46802 cr = -65536 cr + 18734 cr.
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
jn_colsum(const uint8_t *near, const uint8_t *far, uint32_t n, uint16_t *s) {
  uint32_t i = 0;

  for (; i + 16u <= n; i += 16u) {
    uint8x16_t a = vld1q_u8(near + i), b = vld1q_u8(far + i);

    vst1q_u16(s + i, vmlal_u8(vmovl_u8(vget_low_u8(b)), vget_low_u8(a), vdup_n_u8(3)));
    vst1q_u16(s + i + 8u, vmlal_high_u8(vmovl_high_u8(b), a, vdupq_n_u8(3)));
  }
  for (; i < n; i++) s[i] = (uint16_t)(near[i] * 3u + far[i]);
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

  *tr = vaddq_s16(cr, vcombine_s16(vrshrn_n_s32(vmull_n_s16(vget_low_s16(cr), 26345), 16),
                                   vrshrn_n_s32(vmull_high_n_s16(cr, 26345), 16)));
  *tb = vaddq_s16(vshlq_n_s16(cb, 1), vcombine_s16(vrshrn_n_s32(vmull_n_s16(vget_low_s16(cb), -14942), 16),
                                                   vrshrn_n_s32(vmull_high_n_s16(cb, -14942), 16)));
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
