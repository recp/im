/*
 * Copyright (c), Recep Aslantas.
 * MIT License (MIT), http://opensource.org/licenses/MIT
 */

#include "frame.h"
#include "scan.h"
#include <stdio.h>

IM_HIDE
ImByte*
jpg_sof(ImByte * __restrict pRaw,
        ImJpeg * __restrict jpg) {
  ImFrm   *frm;
  uint8_t (*comp)[4], *pc, tmp, ci;
  int      len, i, Nf, idx;

  len             = jpg_get_ui16(pRaw);
  frm             = &jpg->frm;
  frm->precision  = pRaw[2];
  frm->height     = jpg_get_ui16(&pRaw[3]);
  frm->width      = jpg_get_ui16(&pRaw[5]);
  frm->Nf         = Nf = pRaw[7];
  comp            = frm->comp;
  frm->hmax       = 0;
  frm->vmax       = 0;

  jpg->im->data   = malloc(Nf * frm->height * frm->width);
  jpg->im->width  = frm->width;
  jpg->im->height = frm->height;
  jpg->im->len    = Nf * frm->height * frm->width;

  /* if two IDs are same, last one will override first one */
  for (i = 0; i < Nf; i++) {
    idx   = 8 + i * 3;
    ci    = pRaw[idx];
    pc    = comp[ci];
    tmp   = pRaw[idx + 1];

    pc[0] = ci;              /* Ci  */
    pc[2] = tmp & 0x0F;      /* Vi  */
    pc[1] = tmp >> 4;        /* Hi  */
    pc[3] = pRaw[idx + 2];   /* Tqi */

    frm->hmax = im_maxiu8(frm->hmax, pc[1]);
    frm->vmax = im_maxiu8(frm->vmax, pc[2]);
  }

  return pRaw + len;
}

IM_HIDE
ImByte*
jpg_sos(ImByte * __restrict pRaw,
        ImJpeg * __restrict jpg) {
  ImScan  *scan;
  ImByte  *pRawEnd;
  uint8_t *comp, *pc, tmp;
  int      len, i, Ns, idx;

  len        = jpg_get_ui16(pRaw);
  pRawEnd    = pRaw + len;
  scan       = calloc(1, sizeof(*scan));
  scan->Ns   = Ns = pRaw[2];
  scan->comp = comp = malloc(Ns * 3);

  for (i = 0; i < Ns; i++) {
    idx   = 3 + i * 2;
    pc    = comp + 3 * i;
    tmp   = pRaw[idx + 1];

    pc[0] = pRaw[idx];  /* Csj  */
    pc[2] = tmp & 0x0F; /* Taj  */
    pc[1] = tmp >> 4;   /* Tdj  */
  }

  pRaw += 3 + Ns * 2;

  scan->startOfSpectral = pRaw[0];
  scan->endOfSpectral   = pRaw[1];
  tmp                   = pRaw[2];
  scan->apprxLo         = tmp & 0x0F;
  scan->apprxHi         = tmp >> 4;

  jpg->scan = scan;

  /* interleaved */
  if (Ns > 1) {
    pRawEnd = jpg_scan_intr(pRawEnd, jpg, scan);
  } else {

  }

  /* next sos or EOI */
  return pRawEnd;
}
