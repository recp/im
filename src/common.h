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

#include "endian.h"
#include "bitwise.h"
#include "thread/thread.h"
#include "mm/mmap.h"
#include "arch/intrin.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

#define IM_ARRAY_LEN(ARR) (sizeof(ARR) / sizeof(ARR[0]))

#ifdef __GNUC__
#  define IM_DESTRUCTOR  __attribute__((destructor))
#  define IM_CONSTRUCTOR __attribute__((constructor))
#  define unlikely(expr) __builtin_expect(!!(expr), 0)
#  define likely(expr)   __builtin_expect(!!(expr), 1)
#else
#  define IM_DESTRUCTOR
#  define IM_CONSTRUCTOR
#  define unlikely(expr) (expr)
#  define likely(expr)   (expr)
#endif

#ifdef IM_WINAPI
/*
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
*/
#  define strncasecmp _strnicmp
#  define strcasecmp  _stricmp
#endif

#define IM__UNUSED(X) (void)X

#define IM_ARRAY_SEP_CHECK (c == ' ' || c == '\n' || c == '\t' \
                            || c == '\r' || c == '\f' || c == '\v')

#define IM_ARRAY_SEPLINE_CHECK (c == ' ' || c == '\t' || c == '\f' || c == '\v')
#define IM_ARRAY_SPACE_CHECK   (c == ' ' || c == '\t' || c == '\f' || c == '\v')
#define IM_ARRAY_NLINE_CHECK   (c == '\n' || c == '\r')

typedef struct im_open_config_t {
  ImOpenIntent      openIntent;
  ImByteOrder       byteOrder;
  uint32_t          rowPadding;
  bool              supportsPal;
  bool              releaseFile;
  bool              bgr2rgb;
  im_option_base_t **options;
} im_open_config_t;

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
im_min_i32(int a, int b) {
  if (a < b)
    return a;
  return b;
}

IM_INLINE
int
im_max_i32(int a, int b) {
  if (a > b)
    return a;
  return b;
}

IM_INLINE
int
im_clamp_i32(int a, int minVal, int maxVal) {
  return im_max_i32(im_min_i32(a, maxVal), minVal);
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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"

IM_INLINE
int16_t
im_get_i16_endian(void * __restrict p, bool isLittleEndian) {
  uint16_t buf;
  memcpy_endian16(isLittleEndian, buf, p);
  return *(int16_t *)(void *)&buf;
}

IM_INLINE
uint16_t
im_get_u16_endian(void * __restrict p, bool isLittleEndian) {
  uint16_t buf;
  memcpy_endian16(isLittleEndian, buf, p);
  return *(uint16_t *)(void *)&buf;
}

IM_INLINE
int32_t
im_get_i32_endian(void * __restrict p, bool isLittleEndian) {
  uint32_t buf;
  memcpy_endian32(isLittleEndian, buf, p);
  return *(int32_t *)(void *)&buf;
}

IM_INLINE
uint32_t
im_get_u32_endian(void * __restrict p, bool isLittleEndian) {
  uint32_t buf;
  memcpy_endian32(isLittleEndian, buf, p);
  return *(uint32_t *)(void *)&buf;
}

IM_INLINE uint32_t u32be(ImByte ** __restrict p) {uint32_t b;be_32(b,*p);return b;}
IM_INLINE uint32_t u32le(ImByte ** __restrict p) {uint32_t b;le_32(b,*p);return b;}
IM_INLINE uint32_t u16be(ImByte ** __restrict p) {uint16_t b;be_16(b,*p);return b;}
IM_INLINE uint32_t u16le(ImByte ** __restrict p) {uint16_t b;le_16(b,*p);return b;}
IM_INLINE float    f32be(ImByte ** __restrict p) {float    b;be_32(b,*p);return b;}
IM_INLINE float    f32le(ImByte ** __restrict p) {float    b;le_32(b,*p);return b;}
IM_INLINE double   f64be(ImByte ** __restrict p) {double   b;be_64(b,*p);return b;}
IM_INLINE double   f64le(ImByte ** __restrict p) {double   b;le_64(b,*p);return b;}

IM_INLINE
float
im_get_f32_endian(void * __restrict p, bool isLittleEndian) {
  uint32_t buf;
  memcpy_endian32(isLittleEndian, buf, p);
  return *(float *)(void *)&buf;
}

IM_INLINE
double
im_get_f64_endian(void * __restrict p, bool isLittleEndian) {
  uint64_t buf;
  memcpy_endian64(isLittleEndian, buf, p);
  return *(double *)(void *)&buf;
}

#pragma GCC diagnostic pop

IM_INLINE
void
im_memcpy(char * __restrict dst, const char * __restrict src, size_t size) {
  /*
  int32_t m, c, i;

  m     = size % 16;
  size -= m;
  c     = size / 16;
  
  for (i = 0; i < c; i+=2) {
    _mm_storeu_si128(&dst[i*16],       _mm_loadu_si128((__m128i *)&src[i*16]));
    _mm_storeu_si128(&dst[(i + 1)*16], _mm_loadu_si128((__m128i *)&src[(i + 1)*16]));
  }
   
   //      int n = (count + 7) / 8;      // n is now 3.  (The "while" is going
   //      //              to be run three times.)
   //
   //      switch (count % 8) {          // The remainder is 4 (20 modulo 8) so
   //          // jump to the case 4
   //
   //        case 0:
   //          do {
   //            *pd++ = *p++;
   //          case 7:      *pd++ = *p++;
   //          case 6:      *pd++ = *p++;
   //          case 5:      *pd++ = *p++;
   //          case 4:      *pd++ = *p++;
   //          case 3:      *pd++ = *p++;
   //          case 2:      *pd++ = *p++;
   //          case 1:      *pd++ = *p++;
   //          } while (--n > 0);
   //      }
  */
  
  memcpy(dst, src, size);
}

IM_INLINE
void
im_pixcpy(void * __restrict dst, const void * __restrict src, uint8_t bpp) {
  /*
   int32_t m, c, i;
   
   m     = size % 16;
   size -= m;
   c     = size / 16;
   
   for (i = 0; i < c; i+=2) {
   _mm_storeu_si128(&dst[i*16],       _mm_loadu_si128((__m128i *)&src[i*16]));
   _mm_storeu_si128(&dst[(i + 1)*16], _mm_loadu_si128((__m128i *)&src[(i + 1)*16]));
   }
   
   //      int n = (count + 7) / 8;      // n is now 3.  (The "while" is going
   //      //              to be run three times.)
   //
   //      switch (count % 8) {          // The remainder is 4 (20 modulo 8) so
   //          // jump to the case 4
   //
   //        case 0:
   //          do {
   //            *pd++ = *p++;
   //          case 7:      *pd++ = *p++;
   //          case 6:      *pd++ = *p++;
   //          case 5:      *pd++ = *p++;
   //          case 4:      *pd++ = *p++;
   //          case 3:      *pd++ = *p++;
   //          case 2:      *pd++ = *p++;
   //          case 1:      *pd++ = *p++;
   //          } while (--n > 0);
   //      }
   */
  
  memmove(dst, src, bpp);
}

#endif /* src_common_h */
