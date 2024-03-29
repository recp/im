/*
 * Copyright (C) 2020 Recep Aslantas
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

#include "quant.h"

#if defined(__SSE__) || defined(__SSE2__)
# include <emmintrin.h>
#endif

extern uint32_t unzig[64];

IM_INLINE
void
jpg_quant8(ImByte * __restrict pRaw, uint16_t qt[64]) {
//#if defined(__SSE__) || defined(__SSE2__)
//  __m128i zero, src, lo, hi;
//
//  zero = _mm_setzero_si128();
//
//  src = _mm_loadu_si128(( __m128i *)pRaw);
//  lo  = _mm_unpacklo_epi8(src, zero);
//  hi  = _mm_unpackhi_epi8(src, zero);
//
//  _mm_store_si128((__m128i *)&qt[0], lo);
//  _mm_store_si128((__m128i *)&qt[8], hi);
//
//  src = _mm_loadu_si128((__m128i *)&pRaw[16]);
//  lo  = _mm_unpacklo_epi8(src, zero);
//  hi  = _mm_unpackhi_epi8(src, zero);
//
//  _mm_store_si128((__m128i *)&qt[16], lo);
//  _mm_store_si128((__m128i *)&qt[24], hi);
//
//  src = _mm_loadu_si128((__m128i *)&pRaw[32]);
//  lo  = _mm_unpacklo_epi8(src, zero);
//  hi  = _mm_unpackhi_epi8(src, zero);
//
//  _mm_store_si128((__m128i *)&qt[32], lo);
//  _mm_store_si128((__m128i *)&qt[40], hi);
//
//  src = _mm_loadu_si128((__m128i *)&pRaw[48]);
//  lo  = _mm_unpacklo_epi8(src, zero);
//  hi  = _mm_unpackhi_epi8(src, zero);
//
//  _mm_store_si128((__m128i *)&qt[48], lo);
//  _mm_store_si128((__m128i *)&qt[56], hi);
//#else
  int i;
  for (i = 0; i < 64; i++) {
    qt[unzig[i]] = pRaw[i];
  }
//#endif
}

IM_INLINE
void
jpg_quant16(ImByte * __restrict pRaw, uint16_t qt[64]) {
  int i;
  for (i = 0; i < 64; i++)
    qt[i] = jpg_get_ui16(&pRaw[i]);
}

IM_HIDE
ImByte*
jpg_dqt(ImByte * __restrict pRaw,
        ImJpeg * __restrict jpg) {
  ImQuantTbl *dqt;
  ImByte     *pRawEnd;
  uint16_t    len;
  uint8_t     pq, tq, tmp;

  len     = jpg_get_ui16(pRaw);
  pRawEnd = pRaw + len;
  pRaw   += 2;

  while (pRawEnd > pRaw) {
    tmp   = pRaw[0];
    tq    = tmp & 0x0F;
    pq    = tmp >> 4;
    pRaw += 1;

    /* invalid table location ? ignore it. */
    if (tq > 3)
      continue;

    dqt = &jpg->dqt[tq];

    /* 0: 8 bit, 1: 16 bit | 8bit: LUMINANCE, 16bit: CHROMINANCE */
    if (!pq) {
      jpg_quant8(pRaw, dqt->qt);
      pRaw += 64;
    } else {
      jpg_quant16(pRaw, dqt->qt);
      pRaw += 128;
    }

    dqt->valid = true;
  }

  return pRaw;
}
