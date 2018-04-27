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
jpg_quant8(ImByte * __restrict pRaw, ImQuantTbl * __restrict dqt) {
#if defined(__SSE__) || defined(__SSE2__)
  __m128i zero, src, lo, hi;

  zero = _mm_setzero_si128();

  src = _mm_loadu_si128(( __m128i *)pRaw);
  lo  = _mm_unpacklo_epi8(src, zero);
  hi  = _mm_unpackhi_epi8(src, zero);

  _mm_store_si128((__m128i *)&dqt->qt[0], lo);
  _mm_store_si128((__m128i *)&dqt->qt[8], hi);

  src = _mm_loadu_si128((__m128i *)&pRaw[16]);
  lo  = _mm_unpacklo_epi8(src, zero);
  hi  = _mm_unpackhi_epi8(src, zero);

  _mm_store_si128((__m128i *)&dqt->qt[16], lo);
  _mm_store_si128((__m128i *)&dqt->qt[24], hi);

  src = _mm_loadu_si128((__m128i *)&pRaw[32]);
  lo  = _mm_unpacklo_epi8(src, zero);
  hi  = _mm_unpackhi_epi8(src, zero);

  _mm_store_si128((__m128i *)&dqt->qt[32], lo);
  _mm_store_si128((__m128i *)&dqt->qt[40], hi);

  src = _mm_loadu_si128((__m128i *)&pRaw[48]);
  lo  = _mm_unpacklo_epi8(src, zero);
  hi  = _mm_unpackhi_epi8(src, zero);

  _mm_store_si128((__m128i *)&dqt->qt[48], lo);
  _mm_store_si128((__m128i *)&dqt->qt[56], hi);
#else
  int i;
  for (i = 0; i < 64; i++)
    dqt->qt[i] = pRaw[i];
#endif
}

IM_INLINE
void
jpg_quant16(ImByte * __restrict pRaw, ImQuantTbl * __restrict dqt) {
  int i;
  for (i = 0; i < 64; i++)
    dqt->qt[i] = jpg_read_uint16(&pRaw[i]);
}

IM_HIDE
void
jpg_dqt(ImByte * __restrict pRaw,
        ImJpeg * __restrict jpg) {
  ImQuantTbl *dqt;
  uint16_t    len;
  uint8_t     precision, dest;

  len   = jpg_read_uint16(pRaw);
  pRaw += 2;

  precision = pRaw[0] >> 4;
  dest      = precision & 0x0F;

  /* invalid Quant Table Location ? */
  if (dest > 3)
    return;

  precision = precision == 0 ? 8 : 16;
  pRaw     += 1;

  dqt  = calloc(1, sizeof(*dqt));

  if (precision == 8) {
    jpg_quant8(pRaw, dqt);
    pRaw += 64;
  } else {
    jpg_quant16(pRaw, dqt);
    pRaw += 128;
  }
}
