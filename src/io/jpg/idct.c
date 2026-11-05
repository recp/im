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
 * The inverse DCT: the Loeffler–Ligtenberg–Moschytz factorisation in
 * 13-bit fixed point, libjpeg's accurate integer one (jidctint.c) to the
 * value, its arithmetic in 64 bits as its C build's (so damaged blocks
 * come out as there too); the columns first, kept to PASS1_BITS more,
 * then the rows. A sample past the range wraps within four times it
 * before it's clamped (libjpeg's range-limit table).
 */

#include "jpgint.h"

#define FIX_0_298631336 ((int64_t)2446)
#define FIX_0_390180644 ((int64_t)3196)
#define FIX_0_541196100 ((int64_t)4433)
#define FIX_0_765366865 ((int64_t)6270)
#define FIX_0_899976223 ((int64_t)7373)
#define FIX_1_175875602 ((int64_t)9633)
#define FIX_1_501321110 ((int64_t)12299)
#define FIX_1_847759065 ((int64_t)15137)
#define FIX_1_961570560 ((int64_t)16069)
#define FIX_2_053119869 ((int64_t)16819)
#define FIX_2_562915447 ((int64_t)20995)
#define FIX_3_072711026 ((int64_t)25172)

#define CONST_BITS 13
#define DESCALE(x, n) (((x) + ((int64_t)1 << ((n) - 1))) >> (n))
#define LSHIFT(a, b)  ((int64_t)((uint64_t)(a) << (b)))

/* one 1-D pass's even and odd parts on x0–x7 into t0–t7 (out[i] = the
   sums before their descale) */
#define IDCT_1D(x0, x1, x2, x3, x4, x5, x6, x7, o0, o1, o2, o3, o4, o5, o6, o7)  \
  do {                                                                         \
    int64_t z1, z2, z3, z4, z5, t0, t1, t2, t3, t10, t11, t12, t13;           \
                                                                               \
    z2  = (x2);                                                                \
    z3  = (x6);                                                                \
    z1  = (z2 + z3) * FIX_0_541196100;                                         \
    t2  = z1 + z3 * -FIX_1_847759065;                                          \
    t3  = z1 + z2 * FIX_0_765366865;                                           \
    t0  = LSHIFT((x0) + (x4), CONST_BITS);                                     \
    t1  = LSHIFT((x0) - (x4), CONST_BITS);                                     \
    t10 = t0 + t3;                                                             \
    t13 = t0 - t3;                                                             \
    t11 = t1 + t2;                                                             \
    t12 = t1 - t2;                                                             \
                                                                               \
    t0 = (x7);                                                                 \
    t1 = (x5);                                                                 \
    t2 = (x3);                                                                 \
    t3 = (x1);                                                                 \
    z1 = t0 + t3;                                                              \
    z2 = t1 + t2;                                                              \
    z3 = t0 + t2;                                                              \
    z4 = t1 + t3;                                                              \
    z5 = (z3 + z4) * FIX_1_175875602;                                          \
    t0 = t0 * FIX_0_298631336;                                                 \
    t1 = t1 * FIX_2_053119869;                                                 \
    t2 = t2 * FIX_3_072711026;                                                 \
    t3 = t3 * FIX_1_501321110;                                                 \
    z1 = z1 * -FIX_0_899976223;                                                \
    z2 = z2 * -FIX_2_562915447;                                                \
    z3 = z3 * -FIX_1_961570560;                                                \
    z4 = z4 * -FIX_0_390180644;                                                \
    z3 += z5;                                                                  \
    z4 += z5;                                                                  \
    t0 += z1 + z3;                                                             \
    t1 += z2 + z4;                                                             \
    t2 += z2 + z3;                                                             \
    t3 += z1 + z4;                                                             \
                                                                               \
    o0 = t10 + t3;                                                             \
    o7 = t10 - t3;                                                             \
    o1 = t11 + t2;                                                             \
    o6 = t11 - t2;                                                             \
    o2 = t12 + t1;                                                             \
    o5 = t12 - t1;                                                             \
    o3 = t13 + t0;                                                             \
    o4 = t13 - t0;                                                             \
  } while (0)

/* `T` the sample type, `P` PASS1_BITS, `C` the centre, `M` the maximum */
#define IDCT_BODY(T, P, C, M)                                                  \
  int32_t  ws[64];                                                             \
  T       *o = out;                                                            \
  uint32_t i;                                                                  \
                                                                               \
  for (i = 0; i < 8u; i++) {                                                   \
    const int16_t *c  = in + i;                                                \
    const int16_t *qq = (const int16_t *)q + i;                                \
    int64_t        r0, r1, r2, r3, r4, r5, r6, r7;                             \
                                                                               \
    if (!(c[8] | c[16] | c[24] | c[32] | c[40] | c[48] | c[56])) {             \
      int32_t dc = (int32_t)LSHIFT((int32_t)c[0] * qq[0], P);                  \
                                                                               \
      ws[i] = ws[i + 8] = ws[i + 16] = ws[i + 24] = dc;                        \
      ws[i + 32] = ws[i + 40] = ws[i + 48] = ws[i + 56] = dc;                  \
      continue;                                                                \
    }                                                                          \
    IDCT_1D((int64_t)c[0] * qq[0], (int64_t)c[8] * qq[8],                      \
            (int64_t)c[16] * qq[16], (int64_t)c[24] * qq[24],                  \
            (int64_t)c[32] * qq[32], (int64_t)c[40] * qq[40],                  \
            (int64_t)c[48] * qq[48], (int64_t)c[56] * qq[56],                  \
            r0, r1, r2, r3, r4, r5, r6, r7);                                   \
    ws[i]      = (int32_t)DESCALE(r0, CONST_BITS - P);                         \
    ws[i + 8]  = (int32_t)DESCALE(r1, CONST_BITS - P);                         \
    ws[i + 16] = (int32_t)DESCALE(r2, CONST_BITS - P);                         \
    ws[i + 24] = (int32_t)DESCALE(r3, CONST_BITS - P);                         \
    ws[i + 32] = (int32_t)DESCALE(r4, CONST_BITS - P);                         \
    ws[i + 40] = (int32_t)DESCALE(r5, CONST_BITS - P);                         \
    ws[i + 48] = (int32_t)DESCALE(r6, CONST_BITS - P);                         \
    ws[i + 56] = (int32_t)DESCALE(r7, CONST_BITS - P);                         \
  }                                                                            \
                                                                               \
  for (i = 0; i < 8u; i++, o += stride) {                                      \
    const int32_t *w = ws + i * 8u;                                            \
    int64_t        r[8];                                                       \
    uint32_t       k;                                                          \
                                                                               \
    if (!(w[1] | w[2] | w[3] | w[4] | w[5] | w[6] | w[7])) {                   \
      T v = LIMIT(DESCALE((int64_t)w[0], P + 3), C, M);                        \
                                                                               \
      for (k = 0; k < 8u; k++) o[k] = v;                                       \
      continue;                                                                \
    }                                                                          \
    IDCT_1D((int64_t)w[0], (int64_t)w[1], (int64_t)w[2], (int64_t)w[3],        \
            (int64_t)w[4], (int64_t)w[5], (int64_t)w[6], (int64_t)w[7],        \
            r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);                   \
    for (k = 0; k < 8u; k++) o[k] = LIMIT(DESCALE(r[k], CONST_BITS + P + 3), C, M); \
  }

/* libjpeg's range limit: the value's low bits (two more than a sample's)
   as a signed number, centred, clamped */
#define LIMIT(x, C, M) limit((int64_t)(x), C, M)

IM_INLINE int32_t
limit(int64_t x, int32_t centre, int32_t max) {
  int32_t span = (max + 1) * 4, v = (int32_t)((uint32_t)x & (uint32_t)(span - 1));

  if (v >= span / 2) v -= span;
  v += centre;
  return v < 0 ? 0 : v > max ? max : v;
}

IM_HIDE void
jpg_idct8(const int16_t *in, const uint16_t *q, void *out, size_t stride) {
  IDCT_BODY(uint8_t, 2, 128, 255)
}

IM_HIDE void
jpg_idct12(const int16_t *in, const uint16_t *q, void *out, size_t stride) {
  IDCT_BODY(uint16_t, 1, 2048, 4095)
}
