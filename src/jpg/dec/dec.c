/*
 * Copyright (c), Recep Aslantas.
 * MIT License (MIT), http://opensource.org/licenses/MIT
 */

#include "dec.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

/* file formats */
#include "jfif/jfif.h"
#include "exif/exif.h"

IM_HIDE
void
jpg_dec(ImByte *raw, ImByte *data) {
  ImByte   *pRaw;
  JPGMarker mrk;

  pRaw = raw;

  /* No jpeg */
  if (!jpg_marker_eq(pRaw, JPG_SOI))
    return;

  pRaw += JPP_MARKER_SIZE;

  /* Frame */
  mrk   = jpg_marker(pRaw);
  pRaw += JPP_MARKER_SIZE;

  switch (mrk) {
    /* we have found JFIF file */
    case JPG_APPn(0):
      jfif_dec(pRaw, data);
      break;
    /* we have found EXIF file */
    case JPG_APPn(1):
      exif_dec(pRaw, data);
      break;
    default:
      assert("unsupported file!");
      break;
  }
}
