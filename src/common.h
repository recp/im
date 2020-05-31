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
  IM_ALIGN(16) uint8_t  huffval[256];
  IM_ALIGN(16) int32_t  valptr[16];
  IM_ALIGN(16) int32_t  maxcode[16];
  IM_ALIGN(16) int32_t  delta[16]; /* VALPTR(I) - MINCODE(I) */
  bool                  valid;
} ImHuffTbl;

typedef struct ImFrm {
  uint16_t width;
  uint16_t height;
  uint8_t  precision;
  uint8_t  Nf;
  uint8_t  comp[256][4];
  uint8_t  hmax;
  uint8_t  vmax;
} ImFrm;

typedef struct ImScan {
  uint16_t width;
  uint16_t height;
  uint8_t  startOfSpectral;
  uint8_t  endOfSpectral;
  uint8_t  apprxHi;
  uint8_t  apprxLo;
  uint8_t  Ns;
  uint8_t *comp;
  uint8_t  offword;
  int32_t  cnt;
  uint8_t  b;
  ImByte *pRaw;
} ImScan;

typedef struct ImJpeg {
  ImQuantTbl dqt[4];
  ImHuffTbl  dht[2][4]; /* class | table */
  ImFrm      frm;
  ImScan    *scan;
  ImImage   *im;
} ImJpeg;

IM_INLINE
uint8_t
im_maxiu8(uint8_t a, uint8_t b) {
  if (a > b)
    return a;
  return b;
}

IM_INLINE
uint8_t
im_1bit(ImByte * __restrict pbyte,
        int32_t             offword) {
  return (pbyte[0] & offword) == offword;
}

//uint32_t
//im_bits(ImByte * __restrict pbyte,
//        int32_t             off,
//        int32_t             count) {
//  int32_t i;
//
//  for (i = 0; i < count; i++) {
//    
//  }
//
//  return (pbyte[0] & offword) == offword;
//}

#endif /* src_common_h */
