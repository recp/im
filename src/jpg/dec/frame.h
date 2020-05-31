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

#ifndef src_jpg_bsdct_h
#define src_jpg_bsdct_h

#include "../common.h"

IM_HIDE
ImByte*
jpg_sof(ImByte * __restrict pRaw,
        ImJpeg * __restrict jpg);

IM_HIDE
ImByte*
jpg_sos(ImByte * __restrict pRaw,
        ImJpeg * __restrict jpg);

IM_HIDE
ImByte*
jpg_scan(ImByte * __restrict pRaw,
         ImJpeg * __restrict jpg,
         ImScan * __restrict scan);

IM_HIDE
ImByte*
jpg_scan_intr(ImByte * __restrict pRaw,
              ImJpeg * __restrict jpg,
              ImScan * __restrict scan);

#endif /* src_jpg_bsdct_h */
