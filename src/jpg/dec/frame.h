/*
 * Copyright (c), Recep Aslantas.
 * MIT License (MIT), http://opensource.org/licenses/MIT
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

#endif /* src_jpg_bsdct_h */
