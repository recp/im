/*
 * Copyright (c), Recep Aslantas.
 * MIT License (MIT), http://opensource.org/licenses/MIT
 */

#include "quant.h"

#if defined(__SSE__) || defined(__SSE2__)
# include <emmintrin.h>
#endif

IM_INLINE
void
jpg_quant8(ImByte * __restrict pRaw, uint16_t qt[64]) {
#if defined(__SSE__) || defined(__SSE2__)
  __m128i zero, src, lo, hi;

  zero = _mm_setzero_si128();

  src = _mm_loadu_si128(( __m128i *)pRaw);
  lo  = _mm_unpacklo_epi8(src, zero);
  hi  = _mm_unpackhi_epi8(src, zero);

  _mm_store_si128((__m128i *)&qt[0], lo);
  _mm_store_si128((__m128i *)&qt[8], hi);

  src = _mm_loadu_si128((__m128i *)&pRaw[16]);
  lo  = _mm_unpacklo_epi8(src, zero);
  hi  = _mm_unpackhi_epi8(src, zero);

  _mm_store_si128((__m128i *)&qt[16], lo);
  _mm_store_si128((__m128i *)&qt[24], hi);

  src = _mm_loadu_si128((__m128i *)&pRaw[32]);
  lo  = _mm_unpacklo_epi8(src, zero);
  hi  = _mm_unpackhi_epi8(src, zero);

  _mm_store_si128((__m128i *)&qt[32], lo);
  _mm_store_si128((__m128i *)&qt[40], hi);

  src = _mm_loadu_si128((__m128i *)&pRaw[48]);
  lo  = _mm_unpacklo_epi8(src, zero);
  hi  = _mm_unpackhi_epi8(src, zero);

  _mm_store_si128((__m128i *)&qt[48], lo);
  _mm_store_si128((__m128i *)&qt[56], hi);
#else
  int i;
  for (i = 0; i < 64; i++)
    qt[i] = pRaw[i];
#endif
}

IM_INLINE
void
jpg_quant16(ImByte * __restrict pRaw, uint16_t qt[64]) {
  int i;
  for (i = 0; i < 64; i++)
    qt[i] = jpg_read_uint16(&pRaw[i]);
}

IM_HIDE
ImByte*
jpg_dqt(ImByte * __restrict pRaw,
        ImJpeg * __restrict jpg) {
  ImQuantTbl *dqt;
  ImByte     *pRawEnd;
  uint16_t    len;
  uint8_t     precision, dest, tmp;

  len     = jpg_read_uint16(pRaw);
  pRawEnd = pRaw + len;
  pRaw   += 2;

  if (pRawEnd == pRaw)
    return pRaw;

  do {
    tmp       = pRaw[0];
    dest      = tmp & 0x0F;
    precision = tmp >> 4;

    /* invalid Quant Table Location ? ignore it. */
    if (dest > 3)
      continue;

    pRaw += 1;
    dqt   = &jpg->dqt[dest];

    /* 0: 8 bit, 1: 16 bit */
    if (!precision) {
      jpg_quant8(pRaw, dqt->qt);
      pRaw += 64;
    } else {
      jpg_quant16(pRaw, dqt->qt);
      pRaw += 128;
    }

    dqt->valid = true;
  } while (pRawEnd > pRaw);

  return pRaw;
}
