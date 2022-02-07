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

  while (p && p[0] != '\0' && end > p
         && (IM_ALLSPACES || c == '#') && (c = *++p) != '\0') {
    if (c == '#') {
      NEXT_LINE
    }
  }

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
  size_t          imlen;
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
  
  bytesPerPixel        = header.bytesPerCompoment * ncomponents;
  header.count         = width * height;
  imlen                = header.count * bytesPerPixel;
  im->data.data        = im_init_data(im, imlen); /* malloc(imlen); */
  im->format           = IM_FORMAT_GRAY;
  im->len              = imlen;
  im->width            = width;
  im->height           = height;
  im->bytesPerPixel    = bytesPerPixel;
  im->bitsPerComponent = header.bytesPerCompoment * 8;

  header.width         = width;
  header.height        = height;

  if (header.maxRef != maxval) {
    header.pe = ((float)header.maxRef) / ((float)maxval);
  } else {
    header.pe = 1.0f;
  }

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
  size_t          imlen;
  char           *p;
  uint32_t        width, height, bytesPerPixel;

  /* Actually there are no comments and
     "each of the three lines of text ends with a 1-byte Unix-style
      carriage return: 0x0a in hex, not the Windows/DOS CR/LF combination"
     
     but let's make it more safe by using more generic way to ignore spaces
     and comments
   */

  p                    = *start;
  p                    = im_skip_spaces_and_comments(p, end);
  width                = im_getu32(&p, end);
  p                    = im_skip_spaces_and_comments(p, end);
  height               = im_getu32(&p, end);
  p                    = im_skip_spaces_and_comments(p, end);
  header.byteOrderHint = strtof(p, &p);

  /* TODO: option to  provide more range since its float RGB */
  header.bytesPerCompoment = 4;
  header.maxRef            = 255;

  bytesPerPixel        = header.bytesPerCompoment * ncomponents;
  header.count         = width * height;
  imlen                = header.count * bytesPerPixel;
  im->data.data        = im_init_data(im, imlen); /* malloc(imlen); */
  im->format           = IM_FORMAT_GRAY;
  im->len              = imlen;
  im->width            = width;
  im->height           = height;
  
  /* TODO: use option to use/set higher bits */
  im->bytesPerPixel    = 1 * ncomponents;
  im->bitsPerPixel     = im->bytesPerPixel * 8;

  im->bitsPerComponent = 8;

  header.width         = width;
  header.height        = height;

  *start               = im_skip_spaces_and_comments(p, end);
  
  return header;
}

IM_HIDE
im_pam_header_t
pam_dec_header(ImImage                 * __restrict im,
               char       * __restrict * __restrict start,
               const char              * __restrict end) {
  im_pam_header_t header;
  size_t          imlen;
  char           *p, c;
  uint32_t        width, height, depth, maxval, bytesPerPixel;
  bool            foundENDHDR;

  p                 = *start;
  maxval            = depth = width = height = 0;
  foundENDHDR       = false;
  header.tupltype   = PAM_TUPLE_TYPE_UNKNOWN;
  
  do {
    p = im_skip_spaces_and_comments(p, end);
    if (   p[0] == 'W'
        && p[1] == 'I'
        && p[2] == 'D'
        && p[3] == 'T'
        && p[4] == 'H') {
      p    += 5;
      p     = im_skip_spaces_and_comments(p, end);
      width = im_getu32(&p, end);
    } else if (   p[0] == 'H'
               && p[1] == 'E'
               && p[2] == 'I'
               && p[3] == 'G'
               && p[4] == 'H'
               && p[5] == 'T') {
      p     += 6;
      p      = im_skip_spaces_and_comments(p, end);
      height = im_getu32(&p, end);
    } else if (   p[0] == 'D'
               && p[1] == 'E'
               && p[2] == 'P'
               && p[3] == 'T'
               && p[4] == 'H') {
      p           += 5;
      p            = im_skip_spaces_and_comments(p, end);
      header.depth = depth = im_getu32(&p, end);
    } else if (   p[0] == 'M'
               && p[1] == 'A'
               && p[2] == 'X'
               && p[3] == 'V'
               && p[4] == 'A'
               && p[5] == 'L') {
      p            += 6;
      p             = im_skip_spaces_and_comments(p, end);
      header.maxval = maxval = im_getu32(&p, end);
    } else if (   p[0] == 'T'
               && p[1] == 'U'
               && p[2] == 'P'
               && p[3] == 'L'
               && p[4] == 'T'
               && p[5] == 'Y'
               && p[6] == 'P'
               && p[7] == 'E') {
      p              += 8;
      p               = im_skip_spaces_and_comments(p, end);
      
      if (   p[0]  == 'B'
          && p[1]  == 'L'
          && p[2]  == 'A'
          && p[3]  == 'C'
          && p[4]  == 'K'
          && p[5]  == 'A'
          && p[6]  == 'N'
          && p[7]  == 'D'
          && p[8]  == 'W'
          && p[9]  == 'H'
          && p[10] == 'I'
          && p[11] == 'T'
          && p[12] == 'E') {
        header.tupltype = PAM_TUPLE_TYPE_BLACKANDWHITE;
      } else if (   p[0]  == 'G'
                 && p[1]  == 'R'
                 && p[2]  == 'A'
                 && p[3]  == 'Y'
                 && p[4]  == 'S'
                 && p[5]  == 'C'
                 && p[6]  == 'A'
                 && p[7]  == 'L'
                 && p[8]  == 'E') {
        header.tupltype = PAM_TUPLE_TYPE_GRAYSCALE;
      } else if (   p[0]  == 'R'
                 && p[1]  == 'G'
                 && p[2]  == 'B') {
        header.tupltype = PAM_TUPLE_TYPE_RGB;
      } else if (   p[0]  == 'R'
                 && p[1]  == 'G'
                 && p[2]  == 'B'
                 && p[3]  == '_'
                 && p[4]  == 'A'
                 && p[5]  == 'L'
                 && p[6]  == 'P'
                 && p[7]  == 'H'
                 && p[8]  == 'A') {
        header.tupltype = PAM_TUPLE_TYPE_RGB_ALPHA;
      }
      NEXT_LINE
    } else if (   p[0] == 'E'
               && p[1] == 'N'
               && p[2] == 'D'
               && p[3] == 'H'
               && p[4] == 'D'
               && p[5] == 'R') {
      p += 6;
      foundENDHDR = true;
      break;
    }
  } while (p && p[0] != '\0' && end > p && *++p != '\0');

  if (!foundENDHDR || width == 0 || height == 0) {
    goto err;
  }

  if (depth == 0) {
    if (header.tupltype != PAM_TUPLE_TYPE_UNKNOWN) {
      if (header.tupltype == PAM_TUPLE_TYPE_GRAYSCALE
          || header.tupltype == PAM_TUPLE_TYPE_BLACKANDWHITE) {
        depth = 1;
      } else {
        depth = header.tupltype;
      }
    } else {
      goto err;
    }
  }

  if (maxval > 255) {
    header.maxRef            = 65535;
    header.bytesPerCompoment = 2;
  } else {
    header.maxRef            = 255;
    header.bytesPerCompoment = 1;
  }

  bytesPerPixel        = header.bytesPerCompoment * depth;
  header.count         = width * height;
  imlen                = header.count * bytesPerPixel;
  im->data.data        = im_init_data(im, imlen); /* malloc(imlen); */
  im->format           = IM_FORMAT_GRAY;
  im->len              = imlen;
  im->width            = width;
  im->height           = height;
  im->bytesPerPixel    = bytesPerPixel;
  im->bitsPerPixel     = bytesPerPixel * 8;
  im->bitsPerComponent = 8;

  switch (header.tupltype) {
    case PAM_TUPLE_TYPE_BLACKANDWHITE: im->format = IM_FORMAT_BLACKWHITE; break;
    case PAM_TUPLE_TYPE_GRAYSCALE:     im->format = IM_FORMAT_GRAY;       break;
    case PAM_TUPLE_TYPE_RGB:           im->format = IM_FORMAT_RGB;        break;
    case PAM_TUPLE_TYPE_RGB_ALPHA:
      im->format    = IM_FORMAT_RGBA;
      im->alphaInfo = IM_ALPHA_LAST;
      break;
    default:
      switch (depth) {
        case 1: im->format = IM_FORMAT_GRAY;       break;
        case 3: im->format = IM_FORMAT_RGB;        break;
        case 4:
          im->format    = IM_FORMAT_RGBA;
          im->alphaInfo = IM_ALPHA_LAST;
          break;
        default:
          goto err;
      }
      break;
  }

  header.width      = width;
  header.height     = height;
  
  if (header.maxRef != maxval) {
    header.pe = ((float)header.maxRef) / ((float)maxval);
  } else {
    header.pe = 1.0f;
  }
  
  *start = im_skip_spaces_and_comments(p, end);

  return header;

err:
  if (im->data.data) {
    im->data.data = NULL;
    free(im->data.data);
  }

  header.failed = true;
  return header;
}
