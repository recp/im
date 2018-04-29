/*
 * Copyright (c), Recep Aslantas.
 * MIT License (MIT), http://opensource.org/licenses/MIT
 */

#include "frame.h"

IM_HIDE
ImByte*
jpg_sof(ImByte * __restrict pRaw,
        ImJpeg * __restrict jpg) {
  ImFrm   *frm;
  uint8_t (*comp)[4], *pc, tmp, ci;
  int      len, i, compcount, idx;

  len            = jpg_get_ui16(pRaw);
  frm            = &jpg->frm;
  frm->precision = pRaw[2];
  frm->height    = jpg_get_ui16(&pRaw[3]);
  frm->width     = jpg_get_ui16(&pRaw[5]);
  frm->compcount = compcount = pRaw[7];
  comp           = frm->comp;

  /* if two IDs are same, last one will override first one */
  for (i = 0; i < compcount; i++) {
    idx   = 8 + i * 3;
    ci    = pRaw[idx];
    pc    = comp[ci];
    tmp   = pRaw[idx + 1];

    pc[0] = ci;              /* Ci  */
    pc[2] = tmp & 0x0F;      /* Vi  */
    pc[1] = tmp >> 4;        /* Hi  */
    pc[3] = pRaw[idx + 2];   /* Tqi */
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
  int      len, i, compcount, idx;

  len             = jpg_get_ui16(pRaw);
  pRawEnd         = pRaw + len;
  scan            = malloc(sizeof(*scan));
  scan->compcount = compcount = pRaw[2];
  scan->comp = comp = malloc(compcount * 3);

  for (i = 0; i < compcount; i++) {
    idx   = 3 + i * 2;
    pc    = comp + 3 * i;
    tmp   = pRaw[idx + 1];

    pc[0] = pRaw[idx];  /* Csj  */
    pc[2] = tmp & 0x0F; /* Taj  */
    pc[1] = tmp >> 4;   /* Tdj  */
  }

  pRaw += 3 + compcount * 2;

  scan->startOfSpectral = pRaw[0];
  scan->endOfSpectral   = pRaw[1];
  tmp                   = pRaw[2];
  scan->apprxLo         = tmp & 0x0F;
  scan->apprxHi         = tmp >> 4;

  jpg->scan = scan;

  return pRawEnd;
}
