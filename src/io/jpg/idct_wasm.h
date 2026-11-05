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
 * The 8-bit IDCT in WebAssembly's SIMD128: idct_x86.h's SSE4.1 one in
 * four 32-bit lanes (the same bounds: inputs within 35081 and 35079, else
 * the 64-bit path), its zero halves and lone DCs the same.
 */

#ifndef src_io_jpg_idct_wasm_h
#define src_io_jpg_idct_wasm_h

#include <wasm_simd128.h>

#include "idct_pass.h"

#define JW_ADD(a, b) wasm_i32x4_add(a, b)
#define JW_SUB(a, b) wasm_i32x4_sub(a, b)
#define JW_MUL(a, k) wasm_i32x4_mul(a, wasm_i32x4_splat(k))
#define JW_SHL(a, n) wasm_i32x4_shl(a, n)

/* DESCALE(x, n): (x + 2^(n-1)) >> n, arithmetic */
#define JW_DESCALE(x, n) wasm_i32x4_shr(wasm_i32x4_add(x, wasm_i32x4_splat(1 << ((n) - 1))), n)

IM_INLINE uint32_t
jw_maxabs(const v128_t *v, uint32_t n) {
  v128_t   m = wasm_i32x4_splat(0);
  uint32_t i;

  for (i = 0; i < n; i++) m = wasm_u32x4_max(m, wasm_i32x4_abs(v[i]));
  m = wasm_u32x4_max(m, wasm_i32x4_shuffle(m, m, 2, 3, 0, 1));
  m = wasm_u32x4_max(m, wasm_i32x4_shuffle(m, m, 1, 0, 3, 2));
  return wasm_u32x4_extract_lane(m, 0);
}

IM_INLINE void
jw_tr(v128_t *a, v128_t *b, v128_t *c, v128_t *d) {
  v128_t t0 = wasm_i32x4_shuffle(*a, *b, 0, 4, 1, 5), t1 = wasm_i32x4_shuffle(*c, *d, 0, 4, 1, 5);
  v128_t t2 = wasm_i32x4_shuffle(*a, *b, 2, 6, 3, 7), t3 = wasm_i32x4_shuffle(*c, *d, 2, 6, 3, 7);

  *a = wasm_i64x2_shuffle(t0, t1, 0, 2);
  *b = wasm_i64x2_shuffle(t0, t1, 1, 3);
  *c = wasm_i64x2_shuffle(t2, t3, 0, 2);
  *d = wasm_i64x2_shuffle(t2, t3, 1, 3);
}

/* false: out of the 32-bit range, nothing written */
IM_INLINE bool
jpg_idct8_wasm(const int16_t *in, const uint16_t *q, uint8_t *out, size_t stride) {
  v128_t   c[8], lo[8], hi[8], olo[8], ohi[8], all, low;
  uint32_t i;
  bool     right, bottom;

  for (i = 0; i < 8u; i++) c[i] = wasm_v128_load(in + 8u * i);
  low    = wasm_v128_or(wasm_v128_or(c[1], c[2]), c[3]);
  all    = wasm_v128_or(wasm_v128_or(c[4], c[5]), wasm_v128_or(c[6], c[7]));
  bottom = wasm_v128_any_true(all);
  all    = wasm_v128_or(all, low);
  if (!wasm_v128_any_true(wasm_v128_or(all, wasm_i16x8_replace_lane(c[0], 0, 0)))) {
    uint64_t v = 0x0101010101010101ull * jpg_dc8(in[0], q[0]);

    for (i = 0; i < 8u; i++) memcpy(out + i * stride, &v, 8u);
    return true;
  }
  all   = wasm_v128_or(all, c[0]);
  right = wasm_i64x2_extract_lane(all, 1) != 0;

  /* dequantized, a row (its eight columns) two vectors; the right ones
     only where there's something */
  for (i = 0; i < 8u; i++) {
    v128_t qq = wasm_v128_load(q + 8u * i);

    lo[i] = wasm_i32x4_extmul_low_i16x8(c[i], qq);
    if (right) hi[i] = wasm_i32x4_extmul_high_i16x8(c[i], qq);
  }
  if (jw_maxabs(lo, 8u) > 35081u || (right && jw_maxabs(hi, 8u) > 35081u)) return false;

  /* the columns: lanes are columns, x[k] the k-th row */
  if (bottom) {
    JX_PASS(v128_t, JW_ADD, JW_SUB, JW_MUL, JW_SHL, lo, olo);
    if (right) JX_PASS(v128_t, JW_ADD, JW_SUB, JW_MUL, JW_SHL, hi, ohi);
  } else {
    JX_HALF(v128_t, JW_ADD, JW_SUB, JW_MUL, JW_SHL, lo, olo);
    if (right) JX_HALF(v128_t, JW_ADD, JW_SUB, JW_MUL, JW_SHL, hi, ohi);
  }
  for (i = 0; i < 8u; i++) olo[i] = JW_DESCALE(olo[i], 11);
  if (jw_maxabs(olo, 8u) > 35079u) return false;
  if (right) {
    for (i = 0; i < 8u; i++) ohi[i] = JW_DESCALE(ohi[i], 11);
    if (jw_maxabs(ohi, 8u) > 35079u) return false;
  }

  /* transposed: lo[k] / hi[k] the k-th column's rows 0–3 / 4–7 */
  jw_tr(&olo[0], &olo[1], &olo[2], &olo[3]);
  jw_tr(&olo[4], &olo[5], &olo[6], &olo[7]);
  for (i = 0; i < 4u; i++) {
    lo[i] = olo[i];
    hi[i] = olo[i + 4u];
  }
  if (right) {
    jw_tr(&ohi[0], &ohi[1], &ohi[2], &ohi[3]);
    jw_tr(&ohi[4], &ohi[5], &ohi[6], &ohi[7]);
    for (i = 0; i < 4u; i++) {
      lo[i + 4u] = ohi[i];
      hi[i + 4u] = ohi[i + 4u];
    }
    JX_PASS(v128_t, JW_ADD, JW_SUB, JW_MUL, JW_SHL, lo, olo);
    JX_PASS(v128_t, JW_ADD, JW_SUB, JW_MUL, JW_SHL, hi, ohi);
  } else {
    JX_HALF(v128_t, JW_ADD, JW_SUB, JW_MUL, JW_SHL, lo, olo);
    JX_HALF(v128_t, JW_ADD, JW_SUB, JW_MUL, JW_SHL, hi, ohi);
  }

  /* o[x]: column x's rows; back to rows, wrapped and clamped as libjpeg */
  for (i = 0; i < 8u; i++) {
    olo[i] = JW_DESCALE(olo[i], 18);
    ohi[i] = JW_DESCALE(ohi[i], 18);
  }
  jw_tr(&olo[0], &olo[1], &olo[2], &olo[3]);
  jw_tr(&olo[4], &olo[5], &olo[6], &olo[7]);
  jw_tr(&ohi[0], &ohi[1], &ohi[2], &ohi[3]);
  jw_tr(&ohi[4], &ohi[5], &ohi[6], &ohi[7]);
  {
    const v128_t wrap = wasm_i32x4_splat(1023), off = wasm_i32x4_splat(512), back = wasm_i32x4_splat(384);

    for (i = 0; i < 8u; i++) {
      /* row i: columns 0–3 in olo (rows 0–3) or ohi (rows 4–7), 4–7 beside */
      v128_t a = i < 4u ? olo[i] : ohi[i - 4u], b = i < 4u ? olo[i + 4u] : ohi[i];
      v128_t p;

      a = wasm_i32x4_sub(wasm_v128_and(wasm_i32x4_add(a, off), wrap), back);
      b = wasm_i32x4_sub(wasm_v128_and(wasm_i32x4_add(b, off), wrap), back);
      p = wasm_i16x8_narrow_i32x4(a, b);
      p = wasm_u8x16_narrow_i16x8(p, p);
      wasm_v128_store64_lane(out + (size_t)i * stride, p, 0);
    }
  }
  return true;
}

#endif /* src_io_jpg_idct_wasm_h */
