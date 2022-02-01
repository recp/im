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

#ifndef src_jpg_huff_h
#define src_jpg_huff_h

#include "../common.h"

IM_HIDE
ImByte*
jpg_dht(ImByte * __restrict pRaw,
         ImJpeg * __restrict jpg);

IM_HIDE
uint8_t
jpg_decode(ImScan    * __restrict scan,
           ImHuffTbl * __restrict huff);

IM_HIDE
uint8_t
jpg_nextbit(ImScan * __restrict scan);

IM_INLINE
int16_t
jpg_receive(ImScan    * __restrict scan,
            ImHuffTbl * __restrict huff,
            int16_t                ssss) {
  int16_t i, v;

  for (v = i = 0; i < ssss; i++)
    v = (v << 1) | jpg_nextbit(scan);

  return v;
}

IM_INLINE
int16_t
jpg_extend(int16_t v, int16_t t) {
  /* vt = ipow(2, t - 1); */

  return (v < (1u << (t - 1))) ? v + (-1u << t) + 1 : v;
}

#endif /* src_jpg_huff_h */
