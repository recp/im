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
  uint16_t len;

  len            = jpg_read_uint16(pRaw);
  frm            = &jpg->frm;
  frm->precision = pRaw[2];
  frm->height    = jpg_read_uint16(&pRaw[3]);
  frm->width     = jpg_read_uint16(&pRaw[5]);
  frm->compcount = pRaw[7];

  /* Ci, Hi, Vi, Tqi */
  if (!frm->comp)
    frm->comp = malloc(sizeof(uint8_t) * frm->compcount);
  memcpy(frm->comp, &pRaw[8], sizeof(uint8_t) * frm->compcount * 3);

  return pRaw + len;
}
