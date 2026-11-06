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
 *
 * Decoded smaller, a block to 4×4, 2×2 or one sample: libjpeg's reduced
 * IDCTs (jidctred.c), the same fixed point, the coefficients a smaller
 * block can't show left out.
 */

#include "jpgint.h"

#if (defined(__ARM_NEON) || defined(_M_ARM64)) && defined(__aarch64__) && !defined(IM_NO_SIMD)
#  define J_NEON 1
#  include "idct_neon.h"
#elif (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__)) && !defined(IM_NO_SIMD)
#  define J_X86 1
#  include "idct_x86.h"
#elif defined(__wasm_simd128__) && !defined(IM_NO_SIMD)
#  define J_WASM 1
#  include "idct_wasm.h"
#endif

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
/* the reduced IDCTs' */
#define FIX_0_211164243 ((int64_t)1730)
#define FIX_0_509795579 ((int64_t)4176)
#define FIX_0_601344887 ((int64_t)4926)
#define FIX_0_720959822 ((int64_t)5906)
#define FIX_0_850430095 ((int64_t)6967)
#define FIX_1_061594337 ((int64_t)8697)
#define FIX_1_272758580 ((int64_t)10426)
#define FIX_1_451774981 ((int64_t)11893)
#define FIX_2_172734803 ((int64_t)17799)
#define FIX_3_624509785 ((int64_t)29692)

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

static void
idct8_plain(const int16_t *in, const uint16_t *q, void *out, size_t stride) {
  IDCT_BODY(uint8_t, 2, 128, 255)
}

/* a block with only its DC: every sample this (the shortcuts' sums above,
   which are what the whole computation gives) */
IM_HIDE uint8_t
jpg_dc8(int16_t dc, uint16_t q) {
  int32_t ws = (int32_t)LSHIFT((int32_t)dc * (int16_t)q, 2);

  return (uint8_t)LIMIT(DESCALE((int64_t)ws, 5), 128, 255);
}

IM_HIDE void
jpg_idct8(const int16_t *in, const uint16_t *q, void *out, size_t stride) {
#if J_NEON
  if (jpg_idct8_neon(in, q, out, stride)) return;
#elif J_WASM
  if (jpg_idct8_wasm(in, q, out, stride)) return;
#endif
  idct8_plain(in, q, out, stride);
}

#if J_X86
static void
idct8_sse41(const int16_t *in, const uint16_t *q, void *out, size_t stride) {
  if (!jpg_idct8_sse41(in, q, out, stride)) idct8_plain(in, q, out, stride);
}

static void
idct8_avx2(const int16_t *in, const uint16_t *q, void *out, size_t stride) {
  if (!jpg_idct8_avx2(in, q, out, stride)) idct8_plain(in, q, out, stride);
}
#endif

/* the 8-bit IDCT this CPU runs fastest */
IM_HIDE jpg_idct_fn
jpg_idct8_best(void) {
#if J_X86
  if (__builtin_cpu_supports("avx2")) return idct8_avx2;
  if (__builtin_cpu_supports("sse4.1")) return idct8_sse41;
#endif
  return jpg_idct8;
}

IM_HIDE void
jpg_idct12(const int16_t *in, const uint16_t *q, void *out, size_t stride) {
  IDCT_BODY(uint16_t, 1, 2048, 4095)
}

/* -- smaller blocks ----------------------------------------------------------------- */

/* a coefficient dequantized (the table's value as a 16-bit signed one) */
#define DQ(k) ((int64_t)c[k] * qq[k])

/* 4×4: columns 0–3 and 5–7 (4 can't show), then four rows */
#define IDCT4_BODY(T, P, C, M)                                                 \
  int32_t  ws[32];                                                             \
  T       *o = out;                                                            \
  uint32_t i;                                                                  \
                                                                               \
  for (i = 0; i < 8u; i++) {                                                   \
    const int16_t *c  = in + i;                                                \
    const int16_t *qq = (const int16_t *)q + i;                                \
    int64_t        t0, t2, t10, t12, z1, z2, z3, z4;                           \
                                                                               \
    if (i == 4u) continue;                                                     \
    if (!(c[8] | c[16] | c[24] | c[40] | c[48] | c[56])) {                     \
      int32_t dc = (int32_t)LSHIFT(DQ(0), P);                                  \
                                                                               \
      ws[i] = ws[i + 8] = ws[i + 16] = ws[i + 24] = dc;                        \
      continue;                                                                \
    }                                                                          \
    t0  = LSHIFT(DQ(0), CONST_BITS + 1);                                       \
    t2  = DQ(16) * FIX_1_847759065 + DQ(48) * -FIX_0_765366865;                \
    t10 = t0 + t2;                                                             \
    t12 = t0 - t2;                                                             \
    z1  = DQ(56);                                                              \
    z2  = DQ(40);                                                              \
    z3  = DQ(24);                                                              \
    z4  = DQ(8);                                                               \
    t0  = z1 * -FIX_0_211164243 + z2 * FIX_1_451774981 + z3 * -FIX_2_172734803 \
          + z4 * FIX_1_061594337;                                              \
    t2  = z1 * -FIX_0_509795579 + z2 * -FIX_0_601344887 + z3 * FIX_0_899976223 \
          + z4 * FIX_2_562915447;                                              \
    ws[i]      = (int32_t)DESCALE(t10 + t2, CONST_BITS - P + 1);               \
    ws[i + 24] = (int32_t)DESCALE(t10 - t2, CONST_BITS - P + 1);               \
    ws[i + 8]  = (int32_t)DESCALE(t12 + t0, CONST_BITS - P + 1);               \
    ws[i + 16] = (int32_t)DESCALE(t12 - t0, CONST_BITS - P + 1);               \
  }                                                                            \
  for (i = 0; i < 4u; i++, o += stride) {                                      \
    const int32_t *w = ws + i * 8u;                                            \
    int64_t        t0, t2, t10, t12, z1, z2, z3, z4;                           \
                                                                               \
    if (!(w[1] | w[2] | w[3] | w[5] | w[6] | w[7])) {                          \
      T v = LIMIT(DESCALE((int64_t)w[0], P + 3), C, M);                        \
                                                                               \
      o[0] = o[1] = o[2] = o[3] = v;                                           \
      continue;                                                                \
    }                                                                          \
    t0  = LSHIFT((int64_t)w[0], CONST_BITS + 1);                               \
    t2  = (int64_t)w[2] * FIX_1_847759065 + (int64_t)w[6] * -FIX_0_765366865;  \
    t10 = t0 + t2;                                                             \
    t12 = t0 - t2;                                                             \
    z1  = w[7];                                                                \
    z2  = w[5];                                                                \
    z3  = w[3];                                                                \
    z4  = w[1];                                                                \
    t0  = z1 * -FIX_0_211164243 + z2 * FIX_1_451774981 + z3 * -FIX_2_172734803 \
          + z4 * FIX_1_061594337;                                              \
    t2  = z1 * -FIX_0_509795579 + z2 * -FIX_0_601344887 + z3 * FIX_0_899976223 \
          + z4 * FIX_2_562915447;                                              \
    o[0] = LIMIT(DESCALE(t10 + t2, CONST_BITS + P + 3 + 1), C, M);             \
    o[3] = LIMIT(DESCALE(t10 - t2, CONST_BITS + P + 3 + 1), C, M);             \
    o[1] = LIMIT(DESCALE(t12 + t0, CONST_BITS + P + 3 + 1), C, M);             \
    o[2] = LIMIT(DESCALE(t12 - t0, CONST_BITS + P + 3 + 1), C, M);             \
  }

/* 2×2: columns 0, 1, 3, 5, 7, then two rows */
#define IDCT2_BODY(T, P, C, M)                                                 \
  int32_t  ws[16];                                                             \
  T       *o = out;                                                            \
  uint32_t i;                                                                  \
                                                                               \
  for (i = 0; i < 8u; i++) {                                                   \
    const int16_t *c  = in + i;                                                \
    const int16_t *qq = (const int16_t *)q + i;                                \
    int64_t        t0, t10;                                                    \
                                                                               \
    if (i == 2u || i == 4u || i == 6u) continue;                               \
    if (!(c[8] | c[24] | c[40] | c[56])) {                                     \
      ws[i] = ws[i + 8] = (int32_t)LSHIFT(DQ(0), P);                           \
      continue;                                                                \
    }                                                                          \
    t10 = LSHIFT(DQ(0), CONST_BITS + 2);                                       \
    t0  = DQ(56) * -FIX_0_720959822;                                           \
    t0 += DQ(40) * FIX_0_850430095;                                            \
    t0 += DQ(24) * -FIX_1_272758580;                                           \
    t0 += DQ(8) * FIX_3_624509785;                                             \
    ws[i]     = (int32_t)DESCALE(t10 + t0, CONST_BITS - P + 2);                \
    ws[i + 8] = (int32_t)DESCALE(t10 - t0, CONST_BITS - P + 2);                \
  }                                                                            \
  for (i = 0; i < 2u; i++, o += stride) {                                      \
    const int32_t *w = ws + i * 8u;                                            \
    int64_t        t0, t10;                                                    \
                                                                               \
    if (!(w[1] | w[3] | w[5] | w[7])) {                                        \
      o[0] = o[1] = LIMIT(DESCALE((int64_t)w[0], P + 3), C, M);                \
      continue;                                                                \
    }                                                                          \
    t10 = LSHIFT((int64_t)w[0], CONST_BITS + 2);                               \
    t0  = (int64_t)w[7] * -FIX_0_720959822 + (int64_t)w[5] * FIX_0_850430095   \
          + (int64_t)w[3] * -FIX_1_272758580 + (int64_t)w[1] * FIX_3_624509785; \
    o[0] = LIMIT(DESCALE(t10 + t0, CONST_BITS + P + 3 + 2), C, M);             \
    o[1] = LIMIT(DESCALE(t10 - t0, CONST_BITS + P + 3 + 2), C, M);             \
  }

/* one sample: the block's mean, its DC an eighth */
#define IDCT1_BODY(T, C, M)                                                    \
  int32_t dc = (int32_t)((int64_t)in[0] * (int16_t)q[0]);                      \
                                                                               \
  (void)stride;                                                                \
  *(T *)out = LIMIT(DESCALE((int64_t)dc, 3), C, M);

static void idct4_8(const int16_t *in, const uint16_t *q, void *out, size_t stride) { IDCT4_BODY(uint8_t, 2, 128, 255) }
static void idct2_8(const int16_t *in, const uint16_t *q, void *out, size_t stride) { IDCT2_BODY(uint8_t, 2, 128, 255) }
static void idct1_8(const int16_t *in, const uint16_t *q, void *out, size_t stride) { IDCT1_BODY(uint8_t, 128, 255) }
static void idct4_12(const int16_t *in, const uint16_t *q, void *out, size_t stride) { IDCT4_BODY(uint16_t, 1, 2048, 4095) }
static void idct2_12(const int16_t *in, const uint16_t *q, void *out, size_t stride) { IDCT2_BODY(uint16_t, 1, 2048, 4095) }
static void idct1_12(const int16_t *in, const uint16_t *q, void *out, size_t stride) { IDCT1_BODY(uint16_t, 2048, 4095) }

IM_HIDE jpg_idct_fn
jpg_idct_for(uint32_t side, uint32_t prec) {
  bool eight = prec <= 8u;

  switch (side) {
  case 4:  return eight ? idct4_8 : idct4_12;
  case 2:  return eight ? idct2_8 : idct2_12;
  case 1:  return eight ? idct1_8 : idct1_12;
  default: return eight ? jpg_idct8_best() : jpg_idct12;
  }
}
