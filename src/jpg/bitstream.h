/*
 * Copyright (c), Recep Aslantas.
 * MIT License (MIT), http://opensource.org/licenses/MIT
 */

#ifndef src_jpg_bitstream_h
#define src_jpg_bitstream_h

#include "../common.h"

uint8_t
im_bit_r2(ImByte * __restrict buff) {
  return buff[0] & 0xF;
}

#endif /* src_jpg_bitstream_h */
