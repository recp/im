/*
 * Copyright (c), Recep Aslantas.
 * MIT License (MIT), http://opensource.org/licenses/MIT
 */

#ifndef src_common_h
#define src_common_h

#include "../include/im/common.h"
#include "../include/im/im.h"

#include <string.h>
#include <stdlib.h>

typedef struct ImQuantTbl {
  IM_ALIGN(16) uint16_t qt[64]; /* zig-zag order */
  bool                  valid;
} ImQuantTbl;

typedef struct ImHuffTbl {
  IM_ALIGN(16) uint8_t huff[16][256];
  bool                 valid;
} ImHuffTbl;

typedef struct ImJpeg {
  ImQuantTbl dqt[4];
  ImHuffTbl  dht[2][4]; /* class | table */
} ImJpeg;

#endif /* src_common_h */
