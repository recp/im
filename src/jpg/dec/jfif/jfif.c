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

#include "jfif.h"
#include "../quant.h"
#include "../huff.h"
#include "../frame.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

IM_HIDE
void
jfif_dec(ImByte *raw, ImImage *im) {
  ImJpeg   *jpg;
  ImByte   *pRaw;
  JPGMarker mrk;
  uint16_t  APP0len, Xdensity, Ydensity;
  char      identifier[5];
  uint8_t   vmajor, vminor, units, Xthumb, Ythumb;

  pRaw    = raw;
  APP0len = jpg_get_ui16(pRaw);
  pRaw   += 2;

  /* TODO: add options to get JPEG infos */
  memcpy(identifier, pRaw, 5);
  pRaw += 5;

  vmajor = pRaw[0];
  vminor = pRaw[1];
  units  = pRaw[3];
  pRaw  += 3;

  Xdensity = jpg_get_ui16(pRaw);
  pRaw    += 2;

  Ydensity = jpg_get_ui16(pRaw);
  pRaw    += 2;

  Xthumb = pRaw[0];
  Ythumb = pRaw[1];
  pRaw  += 2 + Xthumb * Ythumb;

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

  jpg     = calloc(1, sizeof(*jpg));
  jpg->im = im;

  while (mrk != JPG_EOI && pRaw) {
    switch (mrk) {
      case JPG_DQT:
        pRaw = jpg_dqt(pRaw, jpg);
        break;
      case JPG_DHT:
        pRaw = jpg_huff(pRaw, jpg);
        break;
      case JPG_SOF0:
      case JPG_SOF1:
      case JPG_SOF2:
      case JPG_SOF3:

      case JPG_SOF5:
      case JPG_SOF6:
      case JPG_SOF7:

      case JPG_SOF8:
      case JPG_SOF9:
      case JPG_SOF10:
      case JPG_SOF11:

      case JPG_SOF13:
      case JPG_SOF14:
      case JPG_SOF15:
        pRaw = jpg_sof(pRaw, jpg);
        break;
      case JPG_SOS:
        pRaw = jpg_sos(pRaw, jpg);
        break;
      default:
        goto fr;
    }

    mrk   = jpg_marker(pRaw);
    pRaw += 2;
  }


fr:
    assert(mrk == JPG_EOI);
  free(jpg);
}
