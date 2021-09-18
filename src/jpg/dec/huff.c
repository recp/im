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
jpg_huffcodes(ImByte    * __restrict BITS,
              ImHuffTbl * __restrict huff) {
  int32_t  count;
  uint16_t code;
  uint8_t  i, j, Li;
  
  for (i = 0, j = 0, code = 0, count = 0; i < 16; i++) {
    Li = BITS[i];

    if (Li) {
      huff->delta[i]   = j - code;
      huff->maxcode[i] = code + Li - 1;
      j               += Li;
      code            += Li;
      count           += Li;
    }

    code <<= 1;
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
      scan->jpg->nScans--;
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

#if DEBUG
  assert(bit == 0 || bit == 1);
#endif

  return bit;
}

uint8_t
jpg_decode(ImScan    * __restrict scan,
           ImHuffTbl * __restrict huff) {
  int32_t i, code;

  code = jpg_nextbit(scan);
  i    = 0;

  for (; code > huff->maxcode[i] && i < 16; i++) {
    code = (code << 1) + jpg_nextbit(scan);
  }

  return huff->huffval[code + huff->delta[i]];
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
