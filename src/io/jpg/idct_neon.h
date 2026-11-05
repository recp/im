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
 * 64-bit path; none of a valid image's does. libjpeg's zero-column and
 * zero-row shortcuts give what the full computation gives, so there are
 * none here.
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
  uint32_t  i;

  /* dequantized, a row (its eight columns) two vectors */
  for (i = 0; i < 8u; i++) {
    int16x8_t c  = vld1q_s16(in + 8u * i);
    int16x8_t qq = vreinterpretq_s16_u16(vld1q_u16(q + 8u * i));

    lo[i] = vmull_s16(vget_low_s16(c), vget_low_s16(qq));
    hi[i] = vmull_high_s16(c, qq);
  }
  if (jn_maxabs(lo, 8u) > 35081u || jn_maxabs(hi, 8u) > 35081u) return false;

  /* the columns: lanes are columns, x[k] the k-th row */
  JN_PASS(lo, olo);
  JN_PASS(hi, ohi);
  for (i = 0; i < 8u; i++) {
    olo[i] = vrshrq_n_s32(olo[i], 11);
    ohi[i] = vrshrq_n_s32(ohi[i], 11);
  }
  if (jn_maxabs(olo, 8u) > 35079u || jn_maxabs(ohi, 8u) > 35079u) return false;

  /* transposed: lo[k] / hi[k] the k-th column's rows 0–3 / 4–7 */
  jn_tr4(&olo[0], &olo[1], &olo[2], &olo[3]);
  jn_tr4(&olo[4], &olo[5], &olo[6], &olo[7]);
  jn_tr4(&ohi[0], &ohi[1], &ohi[2], &ohi[3]);
  jn_tr4(&ohi[4], &ohi[5], &ohi[6], &ohi[7]);
  for (i = 0; i < 4u; i++) {
    lo[i]     = olo[i];      /* columns 0–3, rows 0–3 */
    hi[i]     = olo[i + 4u]; /* columns 0–3, rows 4–7 */
    lo[i + 4u] = ohi[i];     /* columns 4–7, rows 0–3 */
    hi[i + 4u] = ohi[i + 4u];
  }

  /* the rows: lanes are rows, x[k] the k-th column */
  JN_PASS(lo, olo);
  JN_PASS(hi, ohi);

  /* descaled, wrapped as libjpeg's range limit, to bytes: o[x] column x's
     eight rows, then transposed to rows */
  {
    const int32x4_t wrap = vdupq_n_s32(1023), off = vdupq_n_s32(512), back = vdupq_n_s32(384);
    uint8x8_t       c[8];

    for (i = 0; i < 8u; i++) {
      int32x4_t a = vsubq_s32(vandq_s32(vaddq_s32(vrshrq_n_s32(olo[i], 18), off), wrap), back);
      int32x4_t b = vsubq_s32(vandq_s32(vaddq_s32(vrshrq_n_s32(ohi[i], 18), off), wrap), back);

      c[i] = vqmovn_u16(vcombine_u16(vqmovun_s32(a), vqmovun_s32(b)));
    }
    {
      uint8x8x2_t  t01 = vtrn_u8(c[0], c[1]), t23 = vtrn_u8(c[2], c[3]);
      uint8x8x2_t  t45 = vtrn_u8(c[4], c[5]), t67 = vtrn_u8(c[6], c[7]);
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

#endif /* src_io_jpg_idct_neon_h */
