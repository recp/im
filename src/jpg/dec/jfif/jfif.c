/*
 * Copyright (c), Recep Aslantas.
 * MIT License (MIT), http://opensource.org/licenses/MIT
 */

#include "jfif.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

IM_INLINE
ImByte*
jfif_dec_skip_ext(ImByte *raw) {
  return raw + jpg_read_uint16(raw);
}

IM_HIDE
void
jfif_dec(ImByte *raw, ImByte *data) {
  ImByte   *pRaw;
  JPGMarker mrk;
  uint16_t  APP0len, Xdensity, Ydensity;
  char      identifier[5];
  uint8_t   vmajor, vminor, units, Xthumb, Ythumb;

  pRaw    = raw;
  APP0len = jpg_read_uint16(pRaw);
  pRaw += 2;

  /* TODO: add options to get JPEG infos */
  memcpy(identifier, pRaw, 5);
  pRaw += 5;

  vmajor = pRaw[0];
  vminor = pRaw[1];
  units  = pRaw[3];
  pRaw += 3;

  Xdensity = jpg_read_uint16(pRaw);
  pRaw += 2;

  Ydensity = jpg_read_uint16(pRaw);
  pRaw += 2;

  Xthumb = pRaw[0];
  Ythumb = pRaw[1];
  pRaw += 2 + Xthumb * Ythumb;

  /* Next Marker */
  mrk   = jpg_marker(pRaw);
  pRaw += 2;

  /* skip all JFIF extensions until supproted */
  while (mrk == JPG_APPn(0)) {
    pRaw  = jfif_dec_skip_ext(pRaw);
    mrk   = jpg_marker(pRaw);
    pRaw += 2;
  }

  /* skip other APPn for now */
  while (jpg_is_app_marker(mrk)) {
    pRaw  = jfif_dec_skip_ext(pRaw);
    mrk   = jpg_marker(pRaw);
    pRaw += 2;
  }

  /* Frame */

  /* No Frame, TODO: Add Errors? */
  if (!jpg_is_sof_marker(mrk))
    return;
}
