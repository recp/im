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
#include "mem/mmap.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

#define IM_ARRAY_LEN(ARR) (sizeof(ARR) / sizeof(ARR[0]))

#ifdef __GNUC__
#  define IM_DESTRUCTOR  __attribute__((destructor))
#  define IM_CONSTRUCTOR __attribute__((constructor))
#else
#  define IM_DESTRUCTOR
#  define IM_CONSTRUCTOR
#endif

#define IM__UNUSED(X) (void)X

#define IM_ARRAY_SEP_CHECK (c == ' ' || c == '\n' || c == '\t' \
                            || c == '\r' || c == '\f' || c == '\v')

#define IM_ARRAY_SEPLINE_CHECK (c == ' ' || c == '\t' || c == '\f' || c == '\v')
#define IM_ARRAY_SPACE_CHECK   (c == ' ' || c == '\t' || c == '\f' || c == '\v')
#define IM_ARRAY_NLINE_CHECK   (c == '\n' || c == '\r')

typedef struct ImQuantTbl {
  IM_ALIGN(16) uint16_t qt[64]; /* zig-zag order */
  bool                  valid;
} ImQuantTbl;

typedef struct ImHuffTbl {
  IM_ALIGN(16) uint8_t  huffval[256];
  IM_ALIGN(16) int32_t  maxcode[16];
  IM_ALIGN(16) int32_t  delta[16]; /* VALPTR(I) - MINCODE(I) */
  bool                  valid;
} ImHuffTbl;

typedef enum ImSamplerClass {
  IM_SAMPLER_22 = 2 << 1 | 2,
  IM_SAMPLER_11 = 1 << 1 | 1
} ImSamplerClass;

typedef struct ImSampleFactor {
  int32_t H;
  int32_t V;
} ImSampleFactor;

typedef struct ImComponent {
  int32_t        id;
  int32_t        Tq;
  ImSampleFactor sf;
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
  uint8_t       samp[4];
} ImFrm;

typedef struct ImBlockComponent {
  int16_t         blk[64];
} ImBlockComponent;

typedef struct ImThreadedBlock {
  ImBlockComponent blk[4][4][4]; /* RGB, CMYK for 4 samplers */
  ImSampleFactor   sf[4];
  th_thread_mutex  mutex;
  int32_t          mcuy;
  int32_t          mcux;
  int8_t           xi;
  int8_t           yi;
  bool             avail;
} ImThreadedBlock;

typedef struct ImScan {
  struct ImJpeg  *jpg;
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
  th_thread_cond    dec_cond;
  th_thread_mutex   wrkmutex;
  th_thread_mutex   decmutex;
  uint32_t          nScans;
  bool              failed;
  
  ImThreadedBlock   blkpool[3];
  int               dec_index;
  int               wrk_index;
  int               avail_index;
} ImJpeg;

IM_INLINE
ImComponent*
jpg_component_byid(ImFrm * __restrict frm, uint32_t id) {
  int32_t i, Nf;

  Nf = frm->Nf;
  for (i = 0; i < Nf; i++) {
    if (frm->compo[i].id == id) {
      return &frm->compo[i];
    }
  }

  return NULL;
}

IM_INLINE
uint8_t
im_maxiu8(uint8_t a, uint8_t b) {
  if (a > b)
    return a;
  return b;
}

IM_INLINE
uint8_t
im_minu8(uint8_t a, uint8_t b) {
  if (a < b)
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

IM_INLINE
int
min(int a, int b) {
  if (a < b)
    return a;
  return b;
}

IM_INLINE
int
max(int a, int b) {
  if (a > b)
    return a;
  return b;
}

IM_INLINE
int
im_clamp(int a, int minVal, int maxVal) {
  return max(min(a, maxVal), minVal);
}

IM_INLINE
float
im_minf(float a, float b) {
  if (a < b)
    return a;
  return b;
}

IM_INLINE
float
im_maxf(float a, float b) {
  if (a > b)
    return a;
  return b;
}

IM_INLINE
float
im_clampf(float a, float minVal, float maxVal) {
  return im_maxf(im_minf(a, maxVal), minVal);
}

IM_INLINE
float
im_clampf_zo(float a) {
  return im_maxf(im_minf(a, 1.0f), 0.0f);
}

IM_INLINE
int
clampi(int num, int minVal, int maxVal) {
  return max(min(num, maxVal), minVal);
}

#endif /* src_common_h */
