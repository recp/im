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
 * The 8-bit IDCT in NEON: idct.c's arithmetic, the columns first (a row of
 * coefficients a vector, its columns the lanes), then the rows (after a
 * transpose). Products are 16 bits by 16 widened to 32; the sums libjpeg
 * forms before its products are 16-bit in pass 1 and spread over the
 * products in pass 2, each output its own eight. The 8×8's bounds are at
 * jpg_idct8_neon; the 4×4's, in 32-bit lanes, at its own.
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

/* the constants, lanes of three vectors (kc0–kc2: JN_KC), each a
   product's by element: libjpeg's FIX_s and their sums the expanded forms
   below use (2260 = F1175 - F0899, -6436 = F1175 - F1961, …) */
#define JN_KC                                                                  \
  { 4433, 15137, 6270, 9633, 16069, 3196, -7373, -20995,                       \
    2446, 16819, 25172, 12299, 10703, 2260, -6436, 6437,                       \
    -11362, -2259, 11363, 8192, -10704, -11363, 2261, 0 }
#define JN_C4433   kc0, 0 /* F0541 */
#define JN_C15137  kc0, 1 /* F1847 */
#define JN_C6270   kc0, 2 /* F0765 */
#define JN_C9633   kc0, 3 /* F1175 */
#define JN_C16069  kc0, 4 /* F1961 */
#define JN_C3196   kc0, 5 /* F0390 */
#define JN_CM7373  kc0, 6 /* -F0899 */
#define JN_CM20995 kc0, 7 /* -F2562 */
#define JN_C2446   kc1, 0 /* F0298 */
#define JN_C16819  kc1, 1 /* F2053 */
#define JN_C25172  kc1, 2 /* F3072 */
#define JN_C12299  kc1, 3 /* F1501 */
#define JN_C10703  kc1, 4
#define JN_C2260   kc1, 5
#define JN_CM6436  kc1, 6
#define JN_C6437   kc1, 7
#define JN_CM11362 kc2, 0
#define JN_CM2259  kc2, 1
#define JN_C11363  kc2, 2
#define JN_C8192   kc2, 3
#define JN_CM10704 kc2, 4
#define JN_CM11363 kc2, 5
#define JN_C2261   kc2, 6

/* a 16-bit vector's low (L) or high (H) four lanes, widened by a product
   (a constant JN_C…, two arguments once expanded) or a shift */
#define JN_MULL_L(v, c)    JN_MULL_L_(vget_low_s16(v), c)
#define JN_MULL_H(v, c)    JN_MULL_H_(v, c)
#define JN_MLAL_L(a, v, c) JN_MLAL_L_(a, vget_low_s16(v), c)
#define JN_MLAL_H(a, v, c) JN_MLAL_H_(a, v, c)
#define JN_MLSL_L(a, v, c) JN_MLSL_L_(a, vget_low_s16(v), c)
#define JN_MLSL_H(a, v, c) JN_MLSL_H_(a, v, c)
#define JN_MULL_L_(v, k, i)    vmull_laneq_s16(v, k, i)
#define JN_MULL_H_(v, k, i)    vmull_high_laneq_s16(v, k, i)
#define JN_MLAL_L_(a, v, k, i) vmlal_laneq_s16(a, v, k, i)
#define JN_MLAL_H_(a, v, k, i) vmlal_high_laneq_s16(a, v, k, i)
#define JN_MLSL_L_(a, v, k, i) vmlsl_laneq_s16(a, v, k, i)
#define JN_MLSL_H_(a, v, k, i) vmlsl_high_laneq_s16(a, v, k, i)
#define JN_SHLL_L(v)       vshll_n_s16(vget_low_s16(v), 13)
#define JN_SHLL_H(v)       vshll_high_n_s16(v, 13)

/* pass 1 on a half's four columns, x[0–7] the rows, s[] their sums
   (JN_SUMS): o[0–7] the rows out, undescaled */
#define JN_SUMS(x, s)                                                          \
  do {                                                                         \
    s[0] = vaddq_s16(x[2], x[6]);                                              \
    s[1] = vaddq_s16(x[0], x[4]);                                              \
    s[2] = vsubq_s16(x[0], x[4]);                                              \
    s[3] = vaddq_s16(x[7], x[1]);                                              \
    s[4] = vaddq_s16(x[5], x[3]);                                              \
    s[5] = vaddq_s16(x[7], x[3]);                                              \
    s[6] = vaddq_s16(x[5], x[1]);                                              \
    s[7] = vaddq_s16(s[5], s[6]);                                              \
  } while (0)

#define JN_P1(H, x, s, o)                                                      \
  do {                                                                         \
    int32x4_t z1 = JN_MULL_##H(s[0], JN_C4433), z5 = JN_MULL_##H(s[7], JN_C9633); \
    int32x4_t t2 = JN_MLSL_##H(z1, x[6], JN_C15137), t3 = JN_MLAL_##H(z1, x[2], JN_C6270); \
    int32x4_t t0 = JN_SHLL_##H(s[1]), t1 = JN_SHLL_##H(s[2]);                \
    int32x4_t t10 = vaddq_s32(t0, t3), t13 = vsubq_s32(t0, t3);                \
    int32x4_t t11 = vaddq_s32(t1, t2), t12 = vsubq_s32(t1, t2);                \
    int32x4_t z3 = JN_MLSL_##H(z5, s[5], JN_C16069), z4 = JN_MLSL_##H(z5, s[6], JN_C3196); \
    int32x4_t w1 = JN_MULL_##H(s[3], JN_CM7373), w2 = JN_MULL_##H(s[4], JN_CM20995); \
    int32x4_t u0 = vaddq_s32(JN_MLAL_##H(w1, x[7], JN_C2446), z3);           \
    int32x4_t u1 = vaddq_s32(JN_MLAL_##H(w2, x[5], JN_C16819), z4);           \
    int32x4_t u2 = vaddq_s32(JN_MLAL_##H(w2, x[3], JN_C25172), z3);           \
    int32x4_t u3 = vaddq_s32(JN_MLAL_##H(w1, x[1], JN_C12299), z4);           \
                                                                               \
    o[0] = vaddq_s32(t10, u3);                                                 \
    o[7] = vsubq_s32(t10, u3);                                                 \
    o[1] = vaddq_s32(t11, u2);                                                 \
    o[6] = vsubq_s32(t11, u2);                                                 \
    o[2] = vaddq_s32(t12, u1);                                                 \
    o[5] = vsubq_s32(t12, u1);                                                 \
    o[3] = vaddq_s32(t13, u0);                                                 \
    o[4] = vsubq_s32(t13, u0);                                                 \
  } while (0)

/* JN_P1 where rows 4–7 are zero: its sums without them, the odd part's
   per row (2260 = F1175 - F0899, 6436 = F1961 - F1175, …) */
#define JN_P1_HALF(H, x, o)                                                    \
  do {                                                                         \
    int32x4_t t0 = JN_SHLL_##H(x[0]);                                          \
    int32x4_t t2 = JN_MULL_##H(x[2], JN_C4433), t3 = JN_MULL_##H(x[2], JN_C10703); \
    int32x4_t t10 = vaddq_s32(t0, t3), t13 = vsubq_s32(t0, t3);                \
    int32x4_t t11 = vaddq_s32(t0, t2), t12 = vsubq_s32(t0, t2);                \
    int32x4_t u0 = JN_MLAL_##H(JN_MULL_##H(x[1], JN_C2260), x[3], JN_CM6436);         \
    int32x4_t u1 = JN_MLAL_##H(JN_MULL_##H(x[1], JN_C6437), x[3], JN_CM11362);        \
    int32x4_t u2 = JN_MLAL_##H(JN_MULL_##H(x[1], JN_C9633), x[3], JN_CM2259);         \
    int32x4_t u3 = JN_MLAL_##H(JN_MULL_##H(x[1], JN_C11363), x[3], JN_C9633);         \
                                                                               \
    o[0] = vaddq_s32(t10, u3);                                                 \
    o[7] = vsubq_s32(t10, u3);                                                 \
    o[1] = vaddq_s32(t11, u2);                                                 \
    o[6] = vsubq_s32(t11, u2);                                                 \
    o[2] = vaddq_s32(t12, u1);                                                 \
    o[5] = vsubq_s32(t12, u1);                                                 \
    o[3] = vaddq_s32(t13, u0);                                                 \
    o[4] = vsubq_s32(t13, u0);                                                 \
  } while (0)

/* pass 2 on a half's four rows, x[0–7] the columns (up to 32759: no sums
   in 16 bits, each output its eight products), `k` the rounding and
   range offset; each out's top 16 bits by ADDHN (L, to `yl`) or ADDHN2
   (H, beside it in `y`) */
#define JN_HN_L(k, a, b, op) (yl[k] = v##op##hn_s32(a, b))
#define JN_HN_H(k, a, b, op) (y[k] = v##op##hn_high_s32(yl[k], a, b))

#define JN_P2_OUT(H, t10, t11, t12, t13, u0, u1, u2, u3)                      \
  do {                                                                         \
    JN_HN_##H(0, t10, u3, add);                                                \
    JN_HN_##H(7, t10, u3, sub);                                                \
    JN_HN_##H(1, t11, u2, add);                                                \
    JN_HN_##H(6, t11, u2, sub);                                                \
    JN_HN_##H(2, t12, u1, add);                                                \
    JN_HN_##H(5, t12, u1, sub);                                                \
    JN_HN_##H(3, t13, u0, add);                                                \
    JN_HN_##H(4, t13, u0, sub);                                                \
  } while (0)

#define JN_P2(H, x, k)                                                         \
  do {                                                                         \
    int32x4_t a  = JN_MLAL_##H(k, x[0], JN_C8192);                                 \
    int32x4_t e0 = JN_MLAL_##H(a, x[4], JN_C8192), e1 = JN_MLSL_##H(a, x[4], JN_C8192); \
    int32x4_t e2 = JN_MLAL_##H(JN_MULL_##H(x[2], JN_C4433), x[6], JN_CM10704);    \
    int32x4_t e3 = JN_MLAL_##H(JN_MULL_##H(x[2], JN_C10703), x[6], JN_C4433);     \
    int32x4_t u0 = JN_MULL_##H(x[7], JN_CM11363), u1 = JN_MULL_##H(x[5], JN_C2261);   \
    int32x4_t u2 = JN_MULL_##H(x[3], JN_CM2259), u3 = JN_MULL_##H(x[1], JN_C11363);   \
                                                                               \
    u0 = JN_MLAL_##H(JN_MLAL_##H(JN_MLAL_##H(u0, x[1], JN_C2260), x[3], JN_CM6436), x[5], JN_C9633);   \
    u1 = JN_MLAL_##H(JN_MLAL_##H(JN_MLAL_##H(u1, x[3], JN_CM11362), x[7], JN_C9633), x[1], JN_C6437);  \
    u2 = JN_MLAL_##H(JN_MLAL_##H(JN_MLAL_##H(u2, x[5], JN_CM11362), x[7], JN_CM6436), x[1], JN_C9633); \
    u3 = JN_MLAL_##H(JN_MLAL_##H(JN_MLAL_##H(u3, x[7], JN_C2260), x[5], JN_C6437), x[3], JN_C9633);    \
    JN_P2_OUT(H, vaddq_s32(e0, e3), vaddq_s32(e1, e2), vsubq_s32(e1, e2), vsubq_s32(e0, e3), \
              u0, u1, u2, u3);                                                 \
  } while (0)

/* JN_P2 where columns 4–7 are zero */
#define JN_P2_HALF(H, x, k)                                                    \
  do {                                                                         \
    int32x4_t a  = JN_MLAL_##H(k, x[0], JN_C8192);                                 \
    int32x4_t e2 = JN_MULL_##H(x[2], JN_C4433), e3 = JN_MULL_##H(x[2], JN_C10703); \
    int32x4_t u0 = JN_MLAL_##H(JN_MULL_##H(x[1], JN_C2260), x[3], JN_CM6436);         \
    int32x4_t u1 = JN_MLAL_##H(JN_MULL_##H(x[1], JN_C6437), x[3], JN_CM11362);        \
    int32x4_t u2 = JN_MLAL_##H(JN_MULL_##H(x[1], JN_C9633), x[3], JN_CM2259);         \
    int32x4_t u3 = JN_MLAL_##H(JN_MULL_##H(x[1], JN_C11363), x[3], JN_C9633);         \
                                                                               \
    JN_P2_OUT(H, vaddq_s32(a, e3), vaddq_s32(a, e2), vsubq_s32(a, e2), vsubq_s32(a, e3), \
              u0, u1, u2, u3);                                                 \
  } while (0)

/* four vectors, each a row's four 16-bit values beside the row four down's
   ([r | r + 4], r 0–3), to four columns' eight: a 0, b 1, c 2, d 3 */
IM_INLINE void
jn_tr84(int16x8_t *a, int16x8_t *b, int16x8_t *c, int16x8_t *d) {
  int16x8x2_t ab = vtrnq_s16(*a, *b), cd = vtrnq_s16(*c, *d);
  int32x4x2_t e  = vtrnq_s32(vreinterpretq_s32_s16(ab.val[0]), vreinterpretq_s32_s16(cd.val[0]));
  int32x4x2_t f  = vtrnq_s32(vreinterpretq_s32_s16(ab.val[1]), vreinterpretq_s32_s16(cd.val[1]));

  *a = vreinterpretq_s16_s32(e.val[0]);
  *b = vreinterpretq_s16_s32(f.val[0]);
  *c = vreinterpretq_s16_s32(e.val[1]);
  *d = vreinterpretq_s16_s32(f.val[1]);
}

/* pass 1's eight rows of a half, descaled to 16 bits as [r | r + 4] */
#define JN_NARROW(o, t)                                                        \
  do {                                                                         \
    uint32_t r_;                                                               \
    for (r_ = 0; r_ < 4u; r_++) t[r_] = vrshrn_high_n_s32(vrshrn_n_s32(o[r_], 11), o[r_ + 4u], 11); \
  } while (0)

/* rows 0 to n - 1 dequantized, their coefficients past their bounds
   (q + 64) in `bad` */
#define JN_BOUND(n)                                                            \
  do {                                                                         \
    uint32_t i_;                                                               \
                                                                               \
    for (i_ = 0; i_ < (n); i_++) {                                             \
      uint16x8_t a_ = vreinterpretq_u16_s16(vabsq_s16(c[i_]));                 \
                                                                               \
      bad   = vorrq_u16(bad, vqsubq_u16(a_, vld1q_u16(q + 64u + 8u * i_)));   \
      x[i_] = vmulq_s16(c[i_], vreinterpretq_s16_u16(vld1q_u16(q + 8u * i_))); \
    }                                                                          \
  } while (0)

/* a constant vector the compiler can't see into, so its products stay by
   element (it would make each lane a register of its own) */
#if defined(__GNUC__) || defined(__clang__)
#  define JN_OPAQUE(v) __asm__("" : "+w"(v))
#else
#  define JN_OPAQUE(v) (void)0
#endif

static const int16_t jn_kc[24] = JN_KC;

/* false: past a coefficient's bound (q[64 + k], 1096 over its quantizer:
   jpg_qlim), nothing written. Within them every dequantized value is at
   most 1096, which no valid image passes (8-bit samples give 1024 and
   quantizing rounds by at most half a step), so pass 1's 16-bit sums hold
   and its outputs, within (1096 · 61214 + 1024) >> 11 = 32759, are 16
   bits; pass 2's products of those, with its offsets, stay under 2^31.

   Most of a photo's blocks are sparse: where the right half (columns 4–7)
   or the bottom half (rows 4–7) is zero a pass drops those terms (each
   value left is one the full pass has), a zero half's vectors aren't
   computed at all, and a block with only its DC is one value (idct.c's,
   jpg_dc8).

   The last descale, libjpeg's (x + 2^17) >> 18 range-limited as x & 1023
   from 128 up (idct.c's limit), is one offset in pass 2, (2^17 + 2^27):
   ADDHN's top half y = 4 (m + 512) + r (r 0–3, m the descaled value), so
   (y & 4095) - 1536 is 4 times m's wrapped place less 384, and one
   saturating narrow by 2 the byte */
IM_INLINE bool
jpg_idct8_neon(const int16_t *in, const uint16_t *q, uint8_t *out, size_t stride) {
  int16x8_t  c[8], x[8], t[8], y[8], low, all;
  int16x4_t  yl[8];
  uint16x8_t bad = vdupq_n_u16(0);
  int32x4_t  o[8], k = vdupq_n_s32((1 << 17) + (1 << 27));
  int16x8_t  kc0 = vld1q_s16(jn_kc), kc1 = vld1q_s16(jn_kc + 8), kc2 = vld1q_s16(jn_kc + 16);
  bool       right, bottom;
  uint32_t   i;

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

  /* each row a fixed count (the arrays stay in registers) */
  if (bottom) JN_BOUND(8u);
  else JN_BOUND(4u);
  if (vmaxvq_u16(bad)) return false;
  JN_OPAQUE(kc0);
  JN_OPAQUE(kc1);
  JN_OPAQUE(kc2);

  /* the columns: lanes are columns, x[k] the k-th row; t[0–3] the left
     half's rows out, t[4–7] the right's, then transposed: t[k] column k */
  if (bottom) {
    int16x8_t s[8];

    JN_SUMS(x, s);
    JN_P1(L, x, s, o);
    JN_NARROW(o, t);
    if (right) {
      JN_P1(H, x, s, o);
      JN_NARROW(o, (t + 4));
    }
  } else {
    JN_P1_HALF(L, x, o);
    JN_NARROW(o, t);
    if (right) {
      JN_P1_HALF(H, x, o);
      JN_NARROW(o, (t + 4));
    }
  }
  jn_tr84(&t[0], &t[1], &t[2], &t[3]);
  if (right) jn_tr84(&t[4], &t[5], &t[6], &t[7]);

  /* the rows: lanes are rows, t[k] the k-th column; y[k] column k out */
  if (right) {
    JN_P2(L, t, k);
    JN_P2(H, t, k);
  } else {
    JN_P2_HALF(L, t, k);
    JN_P2_HALF(H, t, k);
  }

  /* range-limited, to bytes two columns a vector ([k | k + 4]), then
     transposed to rows */
  {
    const int16x8_t m = vdupq_n_s16(0x0FFF), off = vdupq_n_s16(1536);
    uint8x16_t      b[4];

    for (i = 0; i < 4u; i++)
      b[i] = vqshrun_high_n_s16(vqshrun_n_s16(vsubq_s16(vandq_s16(y[i], m), off), 2),
                                vsubq_s16(vandq_s16(y[i + 4u], m), off), 2);
    {
      uint8x16x2_t t01 = vtrnq_u8(b[0], b[1]), t23 = vtrnq_u8(b[2], b[3]);
      uint16x8x2_t u0  = vtrnq_u16(vreinterpretq_u16_u8(t01.val[0]), vreinterpretq_u16_u8(t23.val[0]));
      uint16x8x2_t u1  = vtrnq_u16(vreinterpretq_u16_u8(t01.val[1]), vreinterpretq_u16_u8(t23.val[1]));
      uint8x16_t   r02 = vreinterpretq_u8_u32(vuzp1q_u32(vreinterpretq_u32_u16(u0.val[0]), vreinterpretq_u32_u16(u0.val[1])));
      uint8x16_t   r46 = vreinterpretq_u8_u32(vuzp2q_u32(vreinterpretq_u32_u16(u0.val[0]), vreinterpretq_u32_u16(u0.val[1])));
      uint8x16_t   r13 = vreinterpretq_u8_u32(vuzp1q_u32(vreinterpretq_u32_u16(u1.val[0]), vreinterpretq_u32_u16(u1.val[1])));
      uint8x16_t   r57 = vreinterpretq_u8_u32(vuzp2q_u32(vreinterpretq_u32_u16(u1.val[0]), vreinterpretq_u32_u16(u1.val[1])));

      vst1_u8(out, vget_low_u8(r02));
      vst1_u8(out + stride, vget_low_u8(r13));
      vst1_u8(out + 2u * stride, vget_high_u8(r02));
      vst1_u8(out + 3u * stride, vget_high_u8(r13));
      vst1_u8(out + 4u * stride, vget_low_u8(r46));
      vst1_u8(out + 5u * stride, vget_low_u8(r57));
      vst1_u8(out + 6u * stride, vget_high_u8(r46));
      vst1_u8(out + 7u * stride, vget_high_u8(r57));
    }
  }
  return true;
}

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
