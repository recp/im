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
  uint8_t *comp, *pc, tmp;
  int      len, i, compcount, idx, ci;

  len            = jpg_get_ui16(pRaw);
  frm            = &jpg->frm;
  frm->precision = pRaw[2];
  frm->height    = jpg_get_ui16(&pRaw[3]);
  frm->width     = jpg_get_ui16(&pRaw[5]);
  frm->compcount = compcount = pRaw[7];

  /* Ci, Hi, Vi, Tqi */
  if (!(comp = frm->comp))
    frm->comp = comp = malloc(frm->compcount);

  /* TODO: id validation or fix ? */
  for (i = 0; i < compcount; i++) {
    idx   = 8 + i * 3;
    ci    = pRaw[idx];
    pc    = comp + 4 * ci;
    tmp   = pRaw[idx + 1];

    pc[0] = pRaw[idx];       /* Ci  */
    pc[2] = tmp & 0x0F;      /* Vi  */
    pc[1] = tmp >> 4;        /* Hi  */
    pc[3] = pRaw[idx + 2];   /* Tqi */
  }

  return pRaw + len;
}
