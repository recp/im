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
 * The 8-bit IDCT in NEON: idct.c's arithmetic on four 32-bit lanes, the
 * columns first (a row of coefficients a vector), then the rows (after a
 * transpose). 32 bits hold every intermediate exactly while a pass's
 * inputs are within 35081 (pass 1) and 35079 (pass 2): the largest L1 norm
 * of a pass's linear forms is 61214. A block past either goes to the
 * 64-bit path; none of a valid image's does.
 *
 * Most of a photo's blocks are sparse: where the right half (columns 4–7)
 * or the bottom half (rows 4–7) is zero a pass drops those terms (each
 * value left is one the full pass has, so the bounds hold), a zero half's
 * vectors aren't computed at all, and a block with only its DC is one
 * value (idct.c's, jpg_dc8).
 */

#ifndef src_io_jpg_idct_neon_h
#define src_io_jpg_idct_neon_h

#include <arm_neon.h>

#define JN_F0298 2446
#define JN_F0390 3196
#define JN_F0541 4433
#define JN_F0765 6270
#define JN_F0899 7373
#define JN_F1175 9633
#define JN_F1501 12299
#define JN_F1847 15137
#define JN_F1961 16069
#define JN_F2053 16819
#define JN_F2562 20995
#define JN_F3072 25172

/* one 1-D pass over x[0–7] (each four lanes), out in o[0–7], undescaled */
#define JN_PASS(x, o)                                                          \
  do {                                                                         \
    int32x4_t z1, z2, z3, z4, z5, t0, t1, t2, t3, t10, t11, t12, t13;          \
                                                                               \
    z1  = vmulq_n_s32(vaddq_s32(x[2], x[6]), JN_F0541);                        \
    t2  = vmlsq_n_s32(z1, x[6], JN_F1847);                                     \
    t3  = vmlaq_n_s32(z1, x[2], JN_F0765);                                     \
    t0  = vshlq_n_s32(vaddq_s32(x[0], x[4]), 13);                              \
    t1  = vshlq_n_s32(vsubq_s32(x[0], x[4]), 13);                              \
    t10 = vaddq_s32(t0, t3);                                                   \
    t13 = vsubq_s32(t0, t3);                                                   \
    t11 = vaddq_s32(t1, t2);                                                   \
    t12 = vsubq_s32(t1, t2);                                                   \
                                                                               \
    z1 = vaddq_s32(x[7], x[1]);                                                \
    z2 = vaddq_s32(x[5], x[3]);                                                \
    z3 = vaddq_s32(x[7], x[3]);                                                \
    z4 = vaddq_s32(x[5], x[1]);                                                \
    z5 = vmulq_n_s32(vaddq_s32(z3, z4), JN_F1175);                             \
    z3 = vmlsq_n_s32(z5, z3, JN_F1961);                                        \
    z4 = vmlsq_n_s32(z5, z4, JN_F0390);                                        \
    z1 = vmulq_n_s32(z1, -JN_F0899);                                           \
    z2 = vmulq_n_s32(z2, -JN_F2562);                                           \
    t0 = vaddq_s32(vmlaq_n_s32(z1, x[7], JN_F0298), z3);                       \
    t1 = vaddq_s32(vmlaq_n_s32(z2, x[5], JN_F2053), z4);                       \
    t2 = vaddq_s32(vmlaq_n_s32(z2, x[3], JN_F3072), z3);                       \
    t3 = vaddq_s32(vmlaq_n_s32(z1, x[1], JN_F1501), z4);                       \
                                                                               \
    o[0] = vaddq_s32(t10, t3);                                                 \
    o[7] = vsubq_s32(t10, t3);                                                 \
    o[1] = vaddq_s32(t11, t2);                                                 \
    o[6] = vsubq_s32(t11, t2);                                                 \
    o[2] = vaddq_s32(t12, t1);                                                 \
    o[5] = vsubq_s32(t12, t1);                                                 \
    o[3] = vaddq_s32(t13, t0);                                                 \
    o[4] = vsubq_s32(t13, t0);                                                 \
  } while (0)

/* JN_PASS where x[4–7] are zero: the same sums without their terms */
#define JN_HALF(x, o)                                                          \
  do {                                                                         \
    int32x4_t z1, z2, z3, z4, z5, t0, t2, t3, t10, t11, t12, t13;              \
                                                                               \
    z1  = vmulq_n_s32(x[2], JN_F0541);                                         \
    t2  = z1;                                                                  \
    t3  = vmlaq_n_s32(z1, x[2], JN_F0765);                                     \
    t0  = vshlq_n_s32(x[0], 13);                                               \
    t10 = vaddq_s32(t0, t3);                                                   \
    t13 = vsubq_s32(t0, t3);                                                   \
    t11 = vaddq_s32(t0, t2);                                                   \
    t12 = vsubq_s32(t0, t2);                                                   \
                                                                               \
    z5 = vmulq_n_s32(vaddq_s32(x[3], x[1]), JN_F1175);                         \
    z3 = vmlsq_n_s32(z5, x[3], JN_F1961);                                      \
    z4 = vmlsq_n_s32(z5, x[1], JN_F0390);                                      \
    z1 = vmulq_n_s32(x[1], -JN_F0899);                                         \
    z2 = vmulq_n_s32(x[3], -JN_F2562);                                         \
    t0 = vaddq_s32(z1, z3);                                                    \
    z5 = vaddq_s32(z2, z4);                                                    \
    t2 = vaddq_s32(vmlaq_n_s32(z2, x[3], JN_F3072), z3);                       \
    t3 = vaddq_s32(vmlaq_n_s32(z1, x[1], JN_F1501), z4);                       \
                                                                               \
    o[0] = vaddq_s32(t10, t3);                                                 \
    o[7] = vsubq_s32(t10, t3);                                                 \
    o[1] = vaddq_s32(t11, t2);                                                 \
    o[6] = vsubq_s32(t11, t2);                                                 \
    o[2] = vaddq_s32(t12, z5);                                                 \
    o[5] = vsubq_s32(t12, z5);                                                 \
    o[3] = vaddq_s32(t13, t0);                                                 \
    o[4] = vsubq_s32(t13, t0);                                                 \
  } while (0)

IM_INLINE void
jn_tr4(int32x4_t *a, int32x4_t *b, int32x4_t *c, int32x4_t *d) {
  int32x4x2_t ab = vtrnq_s32(*a, *b), cd = vtrnq_s32(*c, *d);

  *a = vcombine_s32(vget_low_s32(ab.val[0]), vget_low_s32(cd.val[0]));
  *b = vcombine_s32(vget_low_s32(ab.val[1]), vget_low_s32(cd.val[1]));
  *c = vcombine_s32(vget_high_s32(ab.val[0]), vget_high_s32(cd.val[0]));
  *d = vcombine_s32(vget_high_s32(ab.val[1]), vget_high_s32(cd.val[1]));
}

IM_INLINE uint32_t
jn_maxabs(const int32x4_t *v, uint32_t n) {
  uint32x4_t m = vdupq_n_u32(0);
  uint32_t   i;

  for (i = 0; i < n; i++) m = vmaxq_u32(m, vreinterpretq_u32_s32(vabsq_s32(v[i])));
  return vmaxvq_u32(m);
}

/* false: out of the 32-bit range, nothing written */
IM_INLINE bool
jpg_idct8_neon(const int16_t *in, const uint16_t *q, uint8_t *out, size_t stride) {
  int32x4_t lo[8], hi[8], olo[8], ohi[8];
  int16x8_t c[8], low, all;
  bool      right, bottom;
  uint32_t  i;

  for (i = 0; i < 8u; i++) c[i] = vld1q_s16(in + 8u * i);
  low = vorrq_s16(vorrq_s16(c[1], c[2]), c[3]);
  all = vorrq_s16(vorrq_s16(c[4], c[5]), vorrq_s16(c[6], c[7]));
  bottom = vmaxvq_u16(vreinterpretq_u16_s16(all)) != 0u;
  all    = vorrq_s16(all, low);
  if (!vmaxvq_u16(vreinterpretq_u16_s16(vorrq_s16(all, vsetq_lane_s16(0, c[0], 0))))) {
    uint8x8_t v = vdup_n_u8(jpg_dc8(in[0], q[0]));

    for (i = 0; i < 8u; i++) vst1_u8(out + i * stride, v);
    return true;
  }
  all   = vorrq_s16(all, c[0]);
  right = vmaxv_u16(vreinterpret_u16_s16(vget_high_s16(all))) != 0u;

  /* dequantized, a row (its eight columns) two vectors; the right ones
     only where there's something */
  for (i = 0; i < 8u; i++) {
    int16x8_t qq = vreinterpretq_s16_u16(vld1q_u16(q + 8u * i));

    lo[i] = vmull_s16(vget_low_s16(c[i]), vget_low_s16(qq));
    if (right) hi[i] = vmull_high_s16(c[i], qq);
  }
  if (jn_maxabs(lo, 8u) > 35081u || (right && jn_maxabs(hi, 8u) > 35081u)) return false;

  /* the columns: lanes are columns, x[k] the k-th row */
  if (bottom) {
    JN_PASS(lo, olo);
    if (right) JN_PASS(hi, ohi);
  } else {
    JN_HALF(lo, olo);
    if (right) JN_HALF(hi, ohi);
  }
  for (i = 0; i < 8u; i++) olo[i] = vrshrq_n_s32(olo[i], 11);
  if (jn_maxabs(olo, 8u) > 35079u) return false;
  if (right) {
    for (i = 0; i < 8u; i++) ohi[i] = vrshrq_n_s32(ohi[i], 11);
    if (jn_maxabs(ohi, 8u) > 35079u) return false;
  }

  /* transposed: lo[k] / hi[k] the k-th column's rows 0–3 / 4–7 */
  jn_tr4(&olo[0], &olo[1], &olo[2], &olo[3]);
  jn_tr4(&olo[4], &olo[5], &olo[6], &olo[7]);
  for (i = 0; i < 4u; i++) {
    lo[i] = olo[i];      /* columns 0–3, rows 0–3 */
    hi[i] = olo[i + 4u]; /* columns 0–3, rows 4–7 */
  }

  /* the rows: lanes are rows, x[k] the k-th column */
  if (right) {
    jn_tr4(&ohi[0], &ohi[1], &ohi[2], &ohi[3]);
    jn_tr4(&ohi[4], &ohi[5], &ohi[6], &ohi[7]);
    for (i = 0; i < 4u; i++) {
      lo[i + 4u] = ohi[i]; /* columns 4–7, rows 0–3 */
      hi[i + 4u] = ohi[i + 4u];
    }
    JN_PASS(lo, olo);
    JN_PASS(hi, ohi);
  } else {
    JN_HALF(lo, olo);
    JN_HALF(hi, ohi);
  }

  /* descaled, wrapped as libjpeg's range limit, to bytes: o[x] column x's
     eight rows, then transposed to rows */
  {
    const int32x4_t wrap = vdupq_n_s32(1023), off = vdupq_n_s32(512), back = vdupq_n_s32(384);
    uint8x8_t       r[8];

    for (i = 0; i < 8u; i++) {
      int32x4_t a = vsubq_s32(vandq_s32(vaddq_s32(vrshrq_n_s32(olo[i], 18), off), wrap), back);
      int32x4_t b = vsubq_s32(vandq_s32(vaddq_s32(vrshrq_n_s32(ohi[i], 18), off), wrap), back);

      r[i] = vqmovn_u16(vcombine_u16(vqmovun_s32(a), vqmovun_s32(b)));
    }
    {
      uint8x8x2_t  t01 = vtrn_u8(r[0], r[1]), t23 = vtrn_u8(r[2], r[3]);
      uint8x8x2_t  t45 = vtrn_u8(r[4], r[5]), t67 = vtrn_u8(r[6], r[7]);
      uint16x4x2_t u02 = vtrn_u16(vreinterpret_u16_u8(t01.val[0]), vreinterpret_u16_u8(t23.val[0]));
      uint16x4x2_t u13 = vtrn_u16(vreinterpret_u16_u8(t01.val[1]), vreinterpret_u16_u8(t23.val[1]));
      uint16x4x2_t u46 = vtrn_u16(vreinterpret_u16_u8(t45.val[0]), vreinterpret_u16_u8(t67.val[0]));
      uint16x4x2_t u57 = vtrn_u16(vreinterpret_u16_u8(t45.val[1]), vreinterpret_u16_u8(t67.val[1]));
      uint32x2x2_t v04 = vtrn_u32(vreinterpret_u32_u16(u02.val[0]), vreinterpret_u32_u16(u46.val[0]));
      uint32x2x2_t v26 = vtrn_u32(vreinterpret_u32_u16(u02.val[1]), vreinterpret_u32_u16(u46.val[1]));
      uint32x2x2_t v15 = vtrn_u32(vreinterpret_u32_u16(u13.val[0]), vreinterpret_u32_u16(u57.val[0]));
      uint32x2x2_t v37 = vtrn_u32(vreinterpret_u32_u16(u13.val[1]), vreinterpret_u32_u16(u57.val[1]));

      vst1_u8(out, vreinterpret_u8_u32(v04.val[0]));
      vst1_u8(out + stride, vreinterpret_u8_u32(v15.val[0]));
      vst1_u8(out + 2u * stride, vreinterpret_u8_u32(v26.val[0]));
      vst1_u8(out + 3u * stride, vreinterpret_u8_u32(v37.val[0]));
      vst1_u8(out + 4u * stride, vreinterpret_u8_u32(v04.val[1]));
      vst1_u8(out + 5u * stride, vreinterpret_u8_u32(v15.val[1]));
      vst1_u8(out + 6u * stride, vreinterpret_u8_u32(v26.val[1]));
      vst1_u8(out + 7u * stride, vreinterpret_u8_u32(v37.val[1]));
    }
  }
  return true;
}

/* The 4×4 (idct.c's IDCT4_BODY, libjpeg's jpeg_idct_4x4) the same way: a
   pass's outputs are linear forms whose coefficients' magnitudes sum to at
   most 77910 (16384 + 15137 + 6270 + 1730 + 11893 + 17799 + 8697, with the
   rounding's 2^18), so 32 bits hold every intermediate while a pass's
   inputs are within 27000; past it, false (the 64-bit path). Row and
   column 4 take no part; the DC-only columns' and rows' shortcuts give
   what the sums do */
#define JN_R0211 1730
#define JN_R0509 4176
#define JN_R0601 4926
#define JN_R1061 8697
#define JN_R1451 11893
#define JN_R2172 17799

/* one 1-D pass over x[0–3, 5–7], out o[0–3], undescaled */
#define JN_PASS4(x, o)                                                         \
  do {                                                                         \
    int32x4_t t0, t2, t10, t12, u0, u2;                                        \
                                                                               \
    t0  = vshlq_n_s32(x[0], 14);                                               \
    t2  = vmlsq_n_s32(vmulq_n_s32(x[2], JN_F1847), x[6], JN_F0765);            \
    t10 = vaddq_s32(t0, t2);                                                   \
    t12 = vsubq_s32(t0, t2);                                                   \
    u0  = vmlaq_n_s32(vmlsq_n_s32(vmlaq_n_s32(vmulq_n_s32(x[7], -JN_R0211), x[5], JN_R1451), x[3], JN_R2172),  \
                      x[1], JN_R1061);                                         \
    u2  = vmlaq_n_s32(vmlaq_n_s32(vmlsq_n_s32(vmulq_n_s32(x[7], -JN_R0509), x[5], JN_R0601), x[3], JN_F0899),  \
                      x[1], JN_F2562);                                         \
    o[0] = vaddq_s32(t10, u2);                                                 \
    o[3] = vsubq_s32(t10, u2);                                                 \
    o[1] = vaddq_s32(t12, u0);                                                 \
    o[2] = vsubq_s32(t12, u0);                                                 \
  } while (0)

IM_INLINE bool
jpg_idct4_neon(const int16_t *in, const uint16_t *q, uint8_t *out, size_t stride) {
  int32x4_t lo[8], hi[8], olo[4], ohi[4], col[8], o[4];
  uint32_t  i;

  /* dequantized: row k's columns 0–3 in lo[k], 4–7 in hi[k] */
  for (i = 0; i < 8u; i++) {
    int16x8_t c  = vld1q_s16(in + 8u * i);
    int16x8_t qq = vreinterpretq_s16_u16(vld1q_u16(q + 8u * i));

    if (i == 4u) continue;
    lo[i] = vmull_s16(vget_low_s16(c), vget_low_s16(qq));
    hi[i] = vmull_high_s16(c, qq);
  }
  lo[4] = hi[4] = vdupq_n_s32(0);
  if (jn_maxabs(lo, 8u) > 27000u || jn_maxabs(hi, 8u) > 27000u) return false;

  /* the columns: lanes are columns; four rows out */
  JN_PASS4(lo, olo);
  JN_PASS4(hi, ohi);
  for (i = 0; i < 4u; i++) {
    olo[i] = vrshrq_n_s32(olo[i], 12);
    ohi[i] = vrshrq_n_s32(ohi[i], 12);
  }
  if (jn_maxabs(olo, 4u) > 27000u || jn_maxabs(ohi, 4u) > 27000u) return false;

  /* transposed: col[k] column k's four rows */
  jn_tr4(&olo[0], &olo[1], &olo[2], &olo[3]);
  jn_tr4(&ohi[0], &ohi[1], &ohi[2], &ohi[3]);
  for (i = 0; i < 4u; i++) {
    col[i]      = olo[i];
    col[i + 4u] = ohi[i];
  }

  /* the rows: lanes are rows; o[k] column k's out */
  JN_PASS4(col, o);
  {
    const int32x4_t wrap = vdupq_n_s32(1023), off = vdupq_n_s32(512), back = vdupq_n_s32(384);
    uint8_t         r[16];

    for (i = 0; i < 4u; i++) o[i] = vsubq_s32(vandq_s32(vaddq_s32(vrshrq_n_s32(o[i], 19), off), wrap), back);
    jn_tr4(&o[0], &o[1], &o[2], &o[3]); /* o[r] row r's four */
    vst1q_u8(r, vqmovn_high_u16(vqmovn_u16(vcombine_u16(vqmovun_s32(o[0]), vqmovun_s32(o[1]))),
                                vcombine_u16(vqmovun_s32(o[2]), vqmovun_s32(o[3]))));
    for (i = 0; i < 4u; i++) memcpy(out + i * stride, r + 4u * i, 4u);
  }
  return true;
}

#endif /* src_io_jpg_idct_neon_h */
