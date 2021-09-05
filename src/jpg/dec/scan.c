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
uint32_t unzig[64] = {
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

IM_HIDE
void
jpg_decode_dc(ImJpeg    * __restrict jpg,
              ImScan    * __restrict scan,
              ImHuffTbl * __restrict huff,
              int16_t   * __restrict zz) {
  int32_t t, diff;

  t     = jpg_decode(scan, huff);
  diff  = jpg_receive(scan, huff, t);
  diff  = jpg_extend(diff, t);

  zz[0] = diff;
}

IM_HIDE
void
jpg_decode_ac(ImJpeg    * __restrict jpg,
              ImScan    * __restrict scan,
              ImHuffTbl * __restrict huff,
              int16_t   * __restrict zz) {
  int32_t k, rs, ssss, r;

  k = 1;

  do {
    rs   = jpg_decode(scan, huff);
    ssss = rs & 15; /* eq uals to rs % 16 */
    r    = rs  >> 4;

    if (ssss == 0) {
      if (rs != 15)
        break;

      k += 16;
      continue;
    }

    k    += r;
    zz[unzig[k]] = jpg_extend(jpg_receive(scan, huff, ssss), ssss);

    k++;
  } while (k < 65);
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

IM_HIDE
void
jpg_dequant(ImQuantTbl * __restrict qt,
            int16_t    * __restrict data) {
  int16_t i;
  for (i = 0; i < 64; i++) {
    data[i] *= qt->qt[i];
  }
}

int min(int a, int b) {
  if (a < b)
    return a;
  return b;
}

int max(int a, int b) {
  if (a > b)
    return a;
  return b;
}

IM_EXPORT
void
im_YCbCrToRGB_8x8(int16_t blk[3][64], ImByte * __restrict dest) {
  float Y, Cb, Cr;
  int   R, G, B;

  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      Y  = blk[0][y * 8 + x];
      Cb = blk[1][y * 8 + x];
      Cr = blk[2][y * 8 + x];
      
      R = floorf(Y + 1.402 * (1.0 * Cr - 128.0));
      G = floorf(Y - 0.344136 * (1.0 * Cb - 128.0) - 0.714136 * (1.0 * Cr - 128.0));
      B = floorf(Y + 1.772 * (1.0 * Cb - 128.0));
      
      R = max(0, min(R, 255));
      G = max(0, min(G, 255));
      B = max(0, min(B, 255));

      dest[3 * (y * 8 + x) + 0] = R;
      dest[3 * (y * 8 + x) + 1] = G;
      dest[3 * (y * 8 + x) + 2] = B;
    }
  }
}

IM_HIDE
ImByte*
jpg_scan_intr(ImByte * __restrict pRaw,
              ImJpeg * __restrict jpg,
              ImScan * __restrict scan) {
  ImFrm  *frm;
  int16_t data[3][64];
  int     mcux, mcuy, i, j, k, hmax, vmax, prev;

  frm  = &jpg->frm;
  hmax = frm->hmax * 8;
  vmax = frm->vmax * 8;

  mcux = (frm->width  + hmax - 1) / hmax;
  mcuy = (frm->height + vmax - 1) / vmax;

  prev       = 0;
  scan->cnt  = 0;
  scan->pRaw = pRaw;

  for (i = 0; i < mcuy; i++) {
    for (j = 0; j < mcux; j++) {
      for (k = 0; k < scan->Ns; k++) {
        ImQuantTbl     *qt;
        ImComponentSel *icomp;
        int32_t          Csj, Tqi;

        icomp     = &scan->compo.comp[k];

        Csj  = icomp->id;
       
        Tqi  = jpg->frm.compo[Csj].Tq;
        qt   = &jpg->dqt[Tqi];

        memset(data[k], 0, sizeof(data[k]));

        jpg_scan_block(jpg, scan, icomp, data[k]);

        int p = data[k][0];
        data[k][0] += icomp->prev;
        
        icomp->prev = p;

        jpg_dequant(qt, data[k]);
      }
      
      jpg_idct2(data);
      im_YCbCrToRGB_8x8(data, scan->blk);
      
      thread_lock(&scan->blkmutex);
      scan->blk_mcux = j;
      scan->blk_mcuy = i;

      thread_unlock(&scan->blkmutex);

      thread_cond_signal(&jpg->cond);
    }
  }

  return scan->pRaw;
}

IM_HIDE
ImByte*
jpg_scan(ImByte * __restrict pRaw,
         ImJpeg * __restrict jpg,
         ImScan * __restrict scan) {
  return NULL;
}
