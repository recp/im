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

uint8_t
jpg_decode(ImScan    * __restrict scan,
           ImHuffTbl * __restrict huff);

int32_t
jpg_receive(ImScan    * __restrict scan,
            ImHuffTbl * __restrict huff,
            int32_t                ssss);

int32_t
jpg_extend(int32_t v, int32_t t);

uint8_t
jpg_nextbit(ImScan * __restrict scan) ;

#endif /* src_jpg_huff_h */
