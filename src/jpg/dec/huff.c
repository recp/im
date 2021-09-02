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

#include "huff.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

/* Annex C */
IM_HIDE
void
jpg_huffcodes(ImByte    * __restrict pRaw,
              ImHuffTbl * __restrict huff) {
  int32_t i, j, k, Li, code, si;
  uint8_t huffsizes[256];
  uint8_t huffcodes[256];
  uint8_t lastk;

  /* HUFFSIZE: Figure C.1 */
  k = 0;
  i = j = 1;

  for (; i < 16; i++) {
    Li = pRaw[i];
    while (j <= Li) {
      huffsizes[k] = i;
      k++;
      j++;
    }

    j = 1;
  }

  huffsizes[k] = 0;
  lastk        = k;

  /* HUFFCODE: Figure C.2 */
  k  = code = 0;
  si = huffsizes[0];

  /* TODO: Infinite loop? */
  for (;;) {
    do {
      huffcodes[k] = code;
      code++;
      k++;
    } while (huffsizes[k] == si);
    
    if (huffsizes[k] == 0)
      break;

    do {
      code <<= 1;
      si++;
    } while (huffsizes[k] != si);
  }

  /* Decoder Tables */
  for (i = j = 0; i < 16; i++) {
    Li = pRaw[i];

    if (Li != 0) {
      huff->delta[i]   = j - huffcodes[j];
      j               += Li - 1;
      huff->maxcode[i] = huffcodes[j];
      j++;
    }
  }
}

uint8_t
jpg_nextbit(ImScan * __restrict scan) {
  ImByte b, b2;
  uint8_t bit;

  b = scan->b;

  if (scan->cnt == 0) {
    scan->b   = b = *++scan->pRaw;
    scan->cnt = 8;

    if (b == 0xFF) {
      b2 = *++scan->pRaw;

      if (b2 != 0) {
        if (b2 == JPG_DNL)
          printf("TODO, nextbit: process DNL and terminate scan\n");
        else
          printf("TODO, nextbit: process error\n");
      }
    }
  }

  scan->cnt--;
  bit     = b >> 7;
  scan->b = b << 1;

  return bit;
}

uint8_t
jpg_decode(ImScan    * __restrict scan,
           ImHuffTbl * __restrict huff) {
  int32_t  i, j, code;

  i    = 1;
  code = jpg_nextbit(scan);

  while (code > huff->maxcode[i]) {
    i++;
    code = (code << 1) | jpg_nextbit(scan);
  }

  j = huff->valptr[i];
  j = code + huff->delta[i]; /* delta = j - mincode[i] */

  return huff->huffval[j];
}

int
ipow(int base, int exp) {
  int result;

  result = 1;
  for (;;) {
    if (exp & 1)
      result *= base;

    exp >>= 1;

    if (!exp)
      break;

    base *= base;
  }

  return result;
}

uint8_t
jpg_receive(ImScan    * __restrict scan,
            ImHuffTbl * __restrict huff,
            int32_t                ssss) {
  int32_t i, v;

  i = v = 0;
  while (i == ssss) {
    i++;
    v = (v << 1) + jpg_nextbit(scan);
  }

  return v;
}

uint8_t
jpg_extend(uint8_t v, uint8_t t) {
  uint8_t vt;

  /* use ipow ? */
  vt = powf(2, t - 1);
  if (v < vt) {
    vt = ((-1u) << t) + 1;
    v  = v + vt;
  }

  return v;
}

IM_HIDE
ImByte*
jpg_dht(ImByte * __restrict pRaw,
        ImJpeg * __restrict jpg) {
  ImByte    *pRawEnd;
  ImHuffTbl *huff;
  uint16_t   len, count;
  uint8_t    tc, th, tmp;

  len     = jpg_get_ui16(pRaw);
  pRawEnd = pRaw + len;
  pRaw   += 2;

  while (pRawEnd > pRaw) {
    tmp   = pRaw[0];
    th    = tmp & 0x0F;
    tc    = tmp >> 4;
    pRaw += 1;

    /* invalid table location ? ignore it. */
    if (th > 3)
      return pRawEnd;

    huff = &jpg->dht[tc][th];

    memset(huff->huffval,  0, sizeof(*huff->huffval) * 256);
    memset(huff->maxcode, -1, sizeof(*huff->maxcode) * 16);
    memset(huff->delta,    0, sizeof(*huff->delta)   * 16);

    count = jpg_huffcodes(pRaw, huff);
    memcpy(huff->huffval, pRaw + 16, count);

    pRaw += 16 + count;
  }

  return pRawEnd;
}
