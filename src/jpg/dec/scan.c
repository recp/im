//
//  scan.c
//  im
//
//  Created by Recep Aslantas on 5/16/18.
//  Copyright © 2018 Recep Aslantas. All rights reserved.
//

#include "scan.h"
#include "huff.h"
#include "idct.h"
#include <stdio.h>

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
  uint8_t t, diff;

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
    ssss = rs & 15; /* equals to rs % 16 */
    r    = ssss >> 4;

    if (ssss == 0) {
      if (rs != JPG_ZRL)
        break;

      k += 16;
      continue;
    }

    k    += r;
    zz[k] = jpg_extend(jpg_receive(scan, huff, ssss), ssss);
  } while (k < 64);
}

IM_HIDE
void
jpg_scan_block(ImJpeg  * __restrict jpg,
               ImScan  * __restrict scan,
               uint8_t * __restrict scanComp,
               int16_t * __restrict data) {
  ImHuffTbl *huff_dc, *huff_ac;
  int32_t    Tdi, Tai;

  Tdi     = scanComp[1];
  Tai     = scanComp[2];
  huff_dc = jpg->dht[Tdi];
  huff_ac = jpg->dht[Tai];

  jpg_decode_dc(jpg, scan, huff_dc, data);
  jpg_decode_ac(jpg, scan, huff_ac, data);
}

IM_HIDE
void
jpg_dequant(ImQuantTbl * __restrict qt,
            int16_t    * __restrict data) {
  int32_t i;
  for (i = 0; i < 64; i++)
    data[i] *= qt->qt[i];
}

IM_HIDE
ImByte*
jpg_scan_intr(ImByte * __restrict pRaw,
              ImJpeg * __restrict jpg,
              ImScan * __restrict scan) {
  ImFrm  *frm;
  int16_t data[64];
  int     mcux, mcuy, i, j, k, hmax, vmax;

  frm  = &jpg->frm;
  hmax = frm->hmax * 8;
  vmax = frm->vmax * 8;

  mcux = (frm->width  + hmax - 1) / hmax;
  mcuy = (frm->height + vmax - 1) / vmax;

  scan->cnt  = 0;
  scan->pRaw = pRaw;

  for (i = 0; i < mcuy; i++) {
    for (j = 0; j < mcux; j++) {
      for (k = 0; k < scan->Ns; k++) {
        ImQuantTbl *qt;
        uint8_t    *comp;
        int32_t     Csj, Tqi;

        comp = &scan->comp[k * scan->Ns];
        Csj  = comp[0];
        Tqi  = jpg->frm.comp[Csj][3];
        qt   = &jpg->dqt[Tqi];

        memset(data, 0, sizeof(*data) * 64);

        jpg_scan_block(jpg, scan, comp, data);
        jpg_dequant(qt, data);
        jpg_idct(data);
      }
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
