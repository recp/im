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

typedef struct ImFrm {
  uint16_t width;
  uint16_t height;
  uint8_t  precision;
  uint8_t  compcount;
  uint8_t  comp[256][4];
} ImFrm;

typedef struct ImScan {
  uint16_t width;
  uint16_t height;
  uint8_t  startOfSpectral;
  uint8_t  endOfSpectral;
  uint8_t  apprxHi;
  uint8_t  apprxLo;
  uint8_t  compcount;
  uint8_t *comp;
} ImScan;

typedef struct ImJpeg {
  ImQuantTbl dqt[4];
  ImHuffTbl  dht[2][4]; /* class | table */
  ImFrm      frm;
  ImScan    *scan;
} ImJpeg;

#endif /* src_common_h */
