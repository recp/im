/*
 * Copyright (c), Recep Aslantas.
 * MIT License (MIT), http://opensource.org/licenses/MIT
 */

#ifndef src_jpg_huff_h
#define src_jpg_huff_h

#include "../common.h"

IM_HIDE
ImByte*
jpg_huff(ImByte * __restrict pRaw,
         ImJpeg * __restrict jpg);

uint8_t
jpg_decode(ImScan    * __restrict scan,
           ImHuffTbl * __restrict huff);

uint8_t
jpg_receive(ImScan    * __restrict scan,
            ImHuffTbl * __restrict huff,
            int32_t                ssss);

uint8_t
jpg_extend(uint8_t v, uint8_t t);

#endif /* src_jpg_huff_h */
