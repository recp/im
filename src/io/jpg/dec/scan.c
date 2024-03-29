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
#include <assert.h>
#include "scan.h"
#include "huff.h"
#include "idct.h"
#include <stdio.h>
#include <math.h>

#if defined(__SSE__) || defined(__SSE2__)
# include <emmintrin.h>
#endif

IM_ALIGN(16) uint32_t unzig[64] = {
  0,  1,  8,  16, 9,  2,  3,  10,
  17, 24, 32, 25, 18, 11, 4,  5,
  12, 19, 26, 33, 40, 48, 41, 34,
  27, 20, 13, 6,  7,  14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36,
  29, 22, 15, 23, 30, 37, 44, 51,
  58, 59, 52, 45, 38, 31, 39, 46,
  53, 60, 61, 54, 47, 55, 62, 63
};

IM_HIDE
ImByte*
jpg_entropy(ImByte * __restrict pRaw,
            ImJpeg * __restrict jpg,
            ImScan * __restrict scan) {
  return NULL;
}

IM_INLINE
void
jpg_decode_dc(ImJpeg    * __restrict jpg,
              ImScan    * __restrict scan,
              ImHuffTbl * __restrict huff,
              int16_t   * __restrict zz) {
  int16_t t;

  if ((t = jpg_decode(scan, huff))) {
    zz[0] = jpg_extend(jpg_receive(scan, huff, t), t);
  }
}

IM_INLINE
void
jpg_decode_ac(ImJpeg    * __restrict jpg,
              ImScan    * __restrict scan,
              ImHuffTbl * __restrict huff,
              int16_t   * __restrict zz) {
  uint8_t k, rs, ssss, r;

  k = 1;

  do {
    rs   = jpg_decode(scan, huff);
    ssss = rs & 15; /* eq uals to rs % 16 */
    r    = rs >> 4;

    if (ssss == 0) {
      if (r != 15)
        break;

      k += 16;
      continue;
    }

    k += r;
    zz[unzig[k++]] = jpg_extend(jpg_receive(scan, huff, ssss), ssss);
  } while (k < 64);
}

IM_HIDE
void
jpg_scan_block(ImJpeg         * __restrict jpg,
               ImScan         * __restrict scan,
               ImComponentSel * __restrict scanComp,
               int16_t        * __restrict data) {
  ImHuffTbl *huff_dc, *huff_ac;
  int32_t    Tdi, Tai;

  Tdi     = scanComp->Td;
  Tai     = scanComp->Ta;
  huff_dc = &jpg->dht[0][Tdi];
  huff_ac = &jpg->dht[1][Tai];

  jpg_decode_dc(jpg, scan, huff_dc, data);
  jpg_decode_ac(jpg, scan, huff_ac, data);
}

IM_INLINE
void
jpg_dequant(ImQuantTbl * __restrict qt,
            int16_t    * __restrict data) {
#if defined(__SSE__) || defined(__SSE2__)
  __m128i r0, r1, r2, r3, l0, l1, l2, l3;
  
  l0 = _mm_load_si128((__m128i *)&data[0]);
  l1 = _mm_load_si128((__m128i *)&data[8]);
  l2 = _mm_load_si128((__m128i *)&data[16]);
  l3 = _mm_load_si128((__m128i *)&data[24]);

  r0 = _mm_load_si128((__m128i *)&qt->qt[0]);
  r1 = _mm_load_si128((__m128i *)&qt->qt[8]);
  r2 = _mm_load_si128((__m128i *)&qt->qt[16]);
  r3 = _mm_load_si128((__m128i *)&qt->qt[24]);
  
  l0 = _mm_mullo_epi16(l0, r0);
  l1 = _mm_mullo_epi16(l1, r1);
  l2 = _mm_mullo_epi16(l2, r2);
  l3 = _mm_mullo_epi16(l3, r3);
  
  _mm_store_si128((__m128i *)&data[0],  l0);
  _mm_store_si128((__m128i *)&data[8],  l1);
  _mm_store_si128((__m128i *)&data[16], l2);
  _mm_store_si128((__m128i *)&data[24], l3);
  
  l0 = _mm_load_si128((__m128i *)&data[32]);
  l1 = _mm_load_si128((__m128i *)&data[40]);
  l2 = _mm_load_si128((__m128i *)&data[48]);
  l3 = _mm_load_si128((__m128i *)&data[56]);

  r0 = _mm_load_si128((__m128i *)&qt->qt[32]);
  r1 = _mm_load_si128((__m128i *)&qt->qt[40]);
  r2 = _mm_load_si128((__m128i *)&qt->qt[48]);
  r3 = _mm_load_si128((__m128i *)&qt->qt[56]);

  l0 = _mm_mullo_epi16(l0, r0);
  l1 = _mm_mullo_epi16(l1, r1);
  l2 = _mm_mullo_epi16(l2, r2);
  l3 = _mm_mullo_epi16(l3, r3);

  _mm_store_si128((__m128i *)&data[32], l0);
  _mm_store_si128((__m128i *)&data[40], l1);
  _mm_store_si128((__m128i *)&data[48], l2);
  _mm_store_si128((__m128i *)&data[56], l3);
#else
  int16_t i;
  for (i = 0; i < 64; i++) {
    data[i] *= qt->qt[i];
  }
#endif
}

IM_HIDE
ImByte*
jpg_scan_intr(ImByte * __restrict pRaw,
              ImJpeg * __restrict jpg,
              ImScan * __restrict scan) {
  ImFrm               *frm;
  ImThreadedBlock     *tb;
  IM_ALIGN(16) int16_t data[64];
  int                  mcux, mcuy, i, j, k, hmax, vmax, Ns, X, Y;

  frm  = &jpg->frm;
  hmax = frm->hmax;
  vmax = frm->vmax;

  mcux = (frm->width  + (hmax * 8) - 1) / (hmax * 8);
  mcuy = (frm->height + (vmax * 8) - 1) / (vmax * 8);
  X    = frm->width;
  Y    = frm->height;

  scan->cnt  = 0;
  scan->pRaw = pRaw;
  Ns         = scan->Ns;
  
  for (i = 0; i < mcuy; i++) {
    for (j = 0; j < mcux; j++) {
      uint32_t Vi, Hi, h, v;
      
      tb = &jpg->blkpool[jpg->dec_index];
      if (!tb->avail) {
        /* thread_cond_wait(&jpg->dec_cond, &jpg->decmutex); */
        while (!tb->avail) { }
      }

      thread_lock(&tb->mutex);

      for (k = 0; k < Ns; k++) {
        ImQuantTbl     *qt;
        ImComponentSel *icomp;
        ImComponent    *comp;
        int32_t         Csj, Tqi;

        icomp = &scan->compo.comp[k];
        Csj   = icomp->id;
        
        if (!(comp = jpg_component_byid(&jpg->frm, Csj))) {
          thread_exit();
          return NULL;
        }

        Tqi         = comp->Tq;
        qt          = &jpg->dqt[Tqi];
        Vi          = comp->sf.V;
        Hi          = comp->sf.H;
        tb->sf[k].H = Hi;
        tb->sf[k].V = Vi;

        for (v = 0; v < Vi; v++) {
          for (h = 0; h < Hi; h++) {
            memset(data, 0, sizeof(data));

            jpg_scan_block(jpg, scan, icomp, data);

            icomp->pred = (data[0] += icomp->pred);

            jpg_dequant(qt, data);
            jpg_idct(data);

            memcpy(tb->blk[v][h][k].blk, data, sizeof(data));
          }
        }
      }

      tb->mcux = j;
      tb->mcuy = i;
      tb->xi   = im_min_i32(X - 8 * j, 8);
      tb->yi   = im_min_i32(Y - 8 * i, 8);
      
      if (++jpg->dec_index > 2)
        jpg->dec_index = 0;

      tb->avail = false;
      thread_unlock(&tb->mutex);
      thread_cond_signal(&jpg->cond);
    }
  }

  thread_unlock(&jpg->decmutex);

  return scan->pRaw;
}

IM_HIDE
ImByte*
jpg_scan(ImByte * __restrict pRaw,
         ImJpeg * __restrict jpg,
         ImScan * __restrict scan) {
  return NULL;
}
