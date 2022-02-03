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
  uint32_t        width, height, maxval, bytesPerPixel;

  p                 = *start;
  maxval            = 0;

  p                 = im_skip_spaces_and_comments(p, end);
  width             = im_getu32(&p, end);
  p                 = im_skip_spaces_and_comments(p, end);
  height            = im_getu32(&p, end);
  
  if (includeMaxVal) {
    p      = im_skip_spaces_and_comments(p, end);
    maxval = im_getu32(&p, end);
  }
  
  if (maxval > 255) {
    header.maxRef            = 65535;
    header.bytesPerCompoment = 2;
  } else {
    header.maxRef            = 255;
    header.bytesPerCompoment = 1;
  }
  
  bytesPerPixel     = header.bytesPerCompoment * ncomponents;
  im->data          = malloc(width * height * bytesPerPixel);
  im->format        = IM_FORMAT_GRAY;
  im->len           = header.count = width * height;
  im->width         = width;
  im->height        = height;
  im->bytesPerPixel = bytesPerPixel;

  header.width      = width;
  header.height     = height;
  header.pe         = ((float)header.maxRef) / ((float)maxval);

  *start            = im_skip_spaces_and_comments(p, end);

  return header;
}

IM_HIDE
im_pfm_header_t
pfm_dec_header(ImImage                 * __restrict im,
               uint32_t                             ncomponents,
               char       * __restrict * __restrict start,
               const char              * __restrict end) {
  im_pfm_header_t header;
  char           *p;
  uint32_t        width, height, maxval, bytesPerPixel;
  
  p                    = *start;
  maxval               = 0;

  /* Actually there are no comments and
     "each of the three lines of text ends with a 1-byte Unix-style
      carriage return: 0x0a in hex, not the Windows/DOS CR/LF combination"
     
     but let's make it more safe by using more generic way to ignore spaces
     and comments
   */

  p                    = im_skip_spaces_and_comments(p, end);
  width                = im_getu32(&p, end);
  p                    = im_skip_spaces_and_comments(p, end);
  height               = im_getu32(&p, end);
  p                    = im_skip_spaces_and_comments(p, end);
  header.byteOrderHint = strtof(p, &p);

  /* TODO: option to  provide more range since its float RGB */
  header.bytesPerCompoment = 4;
  header.maxRef            = 255;

  bytesPerPixel     = header.bytesPerCompoment * ncomponents;
  im->data          = malloc(width * height * bytesPerPixel);
  im->format        = IM_FORMAT_GRAY;
  im->len           = header.count = width * height;
  im->width         = width;
  im->height        = height;
  im->bytesPerPixel = bytesPerPixel;

  header.width      = width;
  header.height     = height;
  
  *start            = im_skip_spaces_and_comments(p, end);
  
  return header;
}

