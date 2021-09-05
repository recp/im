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

#if DEBUG
#  include <assert.h>
#endif

/* Annex C */
IM_HIDE
uint32_t
jpg_huffcodes(ImByte    * __restrict pRaw,
              ImHuffTbl * __restrict huff) {
  int32_t i, j, k, Li, code, si, count;
  uint8_t huffsizes[256];
  uint8_t huffcodes[256];
  uint8_t lastk;

  /* HUFFSIZE: Figure C.1 */
  count = k = i = 0;

  for (; i < 16; i++) {
    j      = 1;
    Li     = pRaw[i];
    count += Li;
    while (j <= Li) {
      huffsizes[k] = i + 1;
      k++;
      j++;
    }
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
  
  return count;
}

void
jpg_handle_scanmarker(ImScan * __restrict scan, uint16_t marker) {
#if DEBUG
  printf("\n\nFound Marker in Scan: 0x%X\n", marker);
#endif
  
  switch (marker) {
    case JPG_EOI:
#if DEBUG
      printf("Found JPG_EOI\n");
#endif
      scan->jpg->result = IM_JPEG_EOI;
      goto ex;
    case JPG_DNL:
#if DEBUG
      printf("TODO, Found JPG_DNL\n");
#endif
      goto ex; /* TODO: remove this */
    default:
#if DEBUG
      printf("TODO, nextbit: process error\n");
#endif
      scan->jpg->result = IM_JPEG_UKNOWN_MARKER_IN_SCAN;
      goto ex;
  }
  
ex:
  thread_cond_signal(&scan->jpg->cond);
  thread_exit();
}

uint8_t
jpg_nextbit(ImScan * __restrict scan) {
  ImByte b, b2;
  uint8_t bit;

  b = scan->b;

  if (scan->cnt == 0) {
  again:

    scan->b   = b = *scan->pRaw++;
    scan->cnt = 8;

    if (b == 0xFF && (b2 = *scan->pRaw++) != 0) {
      jpg_handle_scanmarker(scan, ((int16_t)b2 << 8) | b);
      goto again;
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
  int32_t i, j, code;
  
  i    = 0;
  code = jpg_nextbit(scan);

  while (code > huff->maxcode[i]) {
    code = (code << 1) | jpg_nextbit(scan);
    i++;

#if DEBUG
    assert(i < 16);
#endif
  }

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

int32_t
jpg_receive(ImScan    * __restrict scan,
            ImHuffTbl * __restrict huff,
            int32_t                ssss) {
  int32_t i, v;

  for (v = i = 0; i < ssss; i++)
    v = (v << 1) | jpg_nextbit(scan);

  return v;
}

int32_t
jpg_extend(int32_t v, int32_t t) {
  /* vt = ipow(2, t - 1); */

  if (v < (1u << (t - 1)))
    return v + (-1u << t) + 1;

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
