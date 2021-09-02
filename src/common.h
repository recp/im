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

#ifndef src_common_h
#define src_common_h

#include "../include/im/common.h"
#include "../include/im/im.h"

#include "thread/thread.h"

#include <string.h>
#include <stdlib.h>

typedef struct ImQuantTbl {
  IM_ALIGN(16) uint16_t qt[64]; /* zig-zag order */
  bool                  valid;
} ImQuantTbl;

typedef struct ImHuffTbl {
  IM_ALIGN(16) uint8_t  huffval[256];
  IM_ALIGN(16) int16_t  maxcode[16];
  IM_ALIGN(16) int16_t  delta[16]; /* VALPTR(I) - MINCODE(I) */
  bool                  valid;
} ImHuffTbl;

typedef struct ImComponent {
  int32_t id;
  int32_t Tq;
  int32_t H;
  int32_t V;
} ImComponent;

typedef struct ImComponentSel {
  ImComponent *comp;
  int32_t      id;
  int32_t      pred;
  int32_t      Td;
  int32_t      Ta;
} ImComponentSel;

typedef struct ImFrm {
  uint16_t      width;
  uint16_t      height;
  uint8_t       precision;
  uint8_t       Nf;
  uint8_t       hmax;
  uint8_t       vmax;
  ImComponent   compo[256];
} ImFrm;

typedef struct ImScan {
  struct ImJpeg *jpg;
  struct {
    ImComponentSel comp[4];
    uint32_t       ncomp;
  } compo;

  uint16_t width;
  uint16_t height;
  uint8_t  startOfSpectral;
  uint8_t  endOfSpectral;
  uint8_t  apprxHi;
  uint8_t  apprxLo;
  uint8_t  Ns;
  uint8_t  offword;
  int32_t  cnt;
  uint8_t  b;
  ImByte  *pRaw;
} ImScan;

typedef struct ImComment {
  struct ImComment *next;
  uint16_t          len;
  ImByte            buff[];
} ImComment;

typedef enum ImJpegResult {
  IM_JPEG_NONE                            = 0,
  IM_JPEG_INVALID                         = -1,
  IM_JPEG_EOI                             = 1,
  IM_JPEG_UKNOWN_MARKER_IN_SCAN           = 2,
  IM_JPEG_INVALID_COMPONENT_COUNT_IN_SCAN = 3
} ImJpegResult;

typedef struct ImJpeg {
  ImQuantTbl        dqt[4];
  ImHuffTbl         dht[2][4]; /* class | table */
  ImFrm             frm;
  ImScan           *scan;
  ImImage          *im;
  ImComment        *comments;
  ImJpegResult      result;
  th_thread_cond    cond;
  th_thread_mutex   mutex;
  th_thread_rwlock  rwlock;
  uint32_t          nScans;
  bool              huffFinished;
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
