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

#include "dec.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

/* file formats */
#include "jfif/jfif.h"
#include "exif/exif.h"

IM_HIDE
void
jpg_dec(ImJpeg *jpg, ImByte *raw) {
  ImByte   *pRaw;
  JPGMarker mrk;

  pRaw = raw;

  /* No jpeg */
  if (!jpg_marker_eq(pRaw, JPG_SOI))
    return;

  pRaw += JPP_MARKER_SIZE;

  /* Find APPn(0) */
  mrk   = jpg_marker(pRaw);
  pRaw += JPP_MARKER_SIZE;

  switch (mrk) {
    /* we have found JFIF file */
    case JPG_APPn(0):
      jfif_dec(pRaw, jpg);
      break;
    /* we have found EXIF file */
    case JPG_APPn(1):
      exif_dec(pRaw, jpg);
      break;
    default:
      assert("unsupported file!");
      break;
  }
}
