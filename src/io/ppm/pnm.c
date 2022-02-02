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

/*
 References:
 [0] http://netpbm.sourceforge.net/doc/
 */

#include "common.h"

IM_INLINE
char*
im_skip_spaces_and_comments(char * __restrict p, const char * __restrict end) {
  char c;
  c = *p;

  do {
    if (c == '#') {
      NEXT_LINE
    }
  } while (p && p[0] != '\0' && end > p
           && (c = *++p) != '\0' && (IM_ALLSPACES || c == '#'));
  return p;
}

IM_HIDE
im_pnm_header_t
pnm_dec_header(ImImage                 * __restrict im,
               uint32_t                             ncomponents,
               char       * __restrict * __restrict start,
               const char              * __restrict end,
               bool                                 includeMaxVal) {
  im_pnm_header_t header;
  char           *p;
  uint32_t        width, height, maxval, bytesPerCompoment;

  p                 = *start;
  maxval            = 0;
  bytesPerCompoment = 1;

  p                 = im_skip_spaces_and_comments(p, end);
  width             = im_getu32(&p, end);
  p                 = im_skip_spaces_and_comments(p, end);
  height            = im_getu32(&p, end);
  
  if (includeMaxVal) {
    p      = im_skip_spaces_and_comments(p, end);
    maxval = im_getu32(&p, end);
  }

  im->data          = malloc(width * height * bytesPerCompoment * ncomponents);
  im->format        = IM_FORMAT_GRAY;
  im->len           = header.count = width * height;
  im->width         = width;
  im->height        = height;
  im->bytesPerPixel = bytesPerCompoment;
  
  header.width  = width;
  header.height = height;

  if (maxval > 255) {
    header.maxRef            = 65535;
    header.bytesPerCompoment = 2;
  } else {
    header.maxRef            = 255;
    header.bytesPerCompoment = 1;
  }

  header.pe = ((float)header.maxRef) / ((float)maxval);
  *start    = im_skip_spaces_and_comments(p, end);

  return header;
}
