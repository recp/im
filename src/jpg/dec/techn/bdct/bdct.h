/*
 * Copyright (c), Recep Aslantas.
 * MIT License (MIT), http://opensource.org/licenses/MIT
 */

#ifndef src_jpg_bdct_h
#define src_jpg_bdct_h

#include "../../../common.h"

/* BCDT:  Baseline DCT (discrete cosine transform) */

IM_HIDE
void
dec_bdct(ImByte *raw, ImByte *data);

#endif /* src_jpg_bdct_h */
