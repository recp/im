/*
 * Copyright (c), Recep Aslantas.
 * MIT License (MIT), http://opensource.org/licenses/MIT
 */

#include "huff.h"
#include <stdlib.h>
#include <stdio.h>

IM_HIDE
ImByte*
jpg_huff(ImByte * __restrict pRaw,
         ImJpeg * __restrict jpg) {
  ImByte     *pRawEnd;
  int32_t     i, count;
  uint16_t    len;
  uint8_t     Li, tc, th, tmp;

  len     = jpg_read_uint16(pRaw);
  pRawEnd = pRaw + len;
  pRaw   += 2;

  if (pRawEnd == pRaw)
    return pRaw;

  do {
    tmp   = pRaw[0];
    th    = tmp & 0x0F;
    tc    = tmp >> 4;
    pRaw += 1;

    count = 0;
    for (i = 0; i < 16; i++) {
      if ((Li = pRaw[i])) {
        memcpy(jpg->dht[tc][th].huff[i], pRaw + 16 + i, Li);
        count += Li;
      }
    }

    pRaw += 16 + count;
  } while (pRawEnd > pRaw);

  return pRaw;
}
