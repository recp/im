/*
 * Copyright (C) 2024 Recep Aslantas
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

#include "qoi.h"
#include "../../file.h"
#include "../../endian.h"

#define QOI_HEADER_SIZE   14
#define QOI_PADDING_SIZE  8
#define QOI_MINSIZE       22

#define QOI_OP_INDEX      0x00 /* 00xxxxxx */
#define QOI_OP_DIFF       0x40 /* 01xxxxxx */
#define QOI_OP_LUMA       0x80 /* 10xxxxxx */
#define QOI_OP_RUN        0xc0 /* 11xxxxxx */
#define QOI_OP_RGB        0xfe /* 11111110 */
#define QOI_OP_RGBA       0xff /* 11111111 */
#define QOI_MASK_2        0xc0 /* 11000000 */

#define QOI_PIXELS_MAX    ((unsigned int)400000000)
#define QOI_COLOR_HASH(C) (C.rgba.r*3 + C.rgba.g*5 + C.rgba.b*7 + C.rgba.a*11)

typedef union {
  struct { unsigned char r, g, b, a; } rgba;
  unsigned int v;
} qoi_rgba_t;

IM_HIDE
ImResult
qoi_dec(ImImage         ** __restrict dest,
        const char       * __restrict path,
        im_open_config_t * __restrict open_config) {
  ImImage        *im;
  ImByte         *p, *p_end, ch;
  char           *pd;
  size_t          len;
  uint32_t        magic, width, height;
  int             run, px_pos, b1, b2, vg;
  ImFileResult    fres;
  qoi_rgba_t      index[64] = {{0}};
  qoi_rgba_t      px;

  im     = NULL;
  fres   = im_readfile(path, open_config->openIntent != IM_OPEN_INTENT_READWRITE);

  if (fres.ret != IM_OK || fres.size < QOI_MINSIZE) {
    goto err;
  }

  /* decode, this process will be optimized after decoding is done */
  im    = calloc(1, sizeof(*im));
  p     = fres.raw;
  p_end = p + fres.size - QOI_PADDING_SIZE;
  run   = 0;

  /**
   * struct qoi_header_t {
   *   char     magic[4];   // magic bytes "qoif"
   *   uint32_t width;      // image width in pixels (BE)
   *   uint32_t height;     // image height in pixels (BE)
   *   uint8_t  channels;   // 3 = RGB, 4 = RGBA
   *   uint8_t  colorspace; // 0 = sRGB with linear alpha, 1 = all channels linear
   * };
   */
  be_32(magic,  p); if (magic != 0x716F6966) { goto err; }
  be_32(width,  p);
  be_32(height, p);

  switch ((ch = *p++)) {
    case 3:
      im->format             = IM_FORMAT_RGB;
      im->bytesPerPixel      = 3;
      im->bitsPerPixel       = 24;
      im->bitsPerComponent   = 8;
      im->componentsPerPixel = 3;
      break;
    case 4:
      im->format             = IM_FORMAT_RGBA;
      im->bytesPerPixel      = 4;
      im->bitsPerPixel       = 32;
      im->bitsPerComponent   = 8;
      im->componentsPerPixel = 4;
      im->alphaInfo          = IM_ALPHA_LAST;
      break;
    default: goto err;
  }

  switch (*p++) {
    case 0:  im->colorSpace  = IM_COLORSPACE_sRGB;    break;
    case 1:  im->colorSpace  = IM_COLORSPACE_LINEAR;  break;
    default: im->colorSpace  = IM_COLORSPACE_UNKNOWN; break;
  }

  im->file           = fres;
  im->openIntent     = open_config->openIntent;
  im->byteOrder      = open_config->byteOrder;
  im->ori            = IM_ORIENTATION_UP;
  im->fileFormatType = IM_FILEFORMATTYPE_QOI;

  im->width          = width;
  im->height         = height;

  im->len            = len = im->bytesPerPixel * width * height;
  im->data.data      = malloc(len);
  pd                 = im->data.data;

  px.rgba.r = 0;
  px.rgba.g = 0;
  px.rgba.b = 0;
  px.rgba.a = 255;
  
  /* px_len == len */
  for (px_pos = 0; px_pos < len; px_pos += ch) {
    if (run > 0) {
      run--;
    } else if (p < p_end) {
      switch ((b1 = *p++)) {
        case QOI_OP_RGB:
          px.rgba.r = *p++;
          px.rgba.g = *p++;
          px.rgba.b = *p++;
          break;
        case QOI_OP_RGBA:
          px.rgba.r = *p++;
          px.rgba.g = *p++;
          px.rgba.b = *p++;
          px.rgba.a = *p++;
          break;
        default:
          switch (b1 & QOI_MASK_2) {
            case QOI_OP_INDEX:
              px = index[b1];
              break;
            case QOI_OP_DIFF:
              px.rgba.r += ((b1 >> 4) & 0x03) - 2;
              px.rgba.g += ((b1 >> 2) & 0x03) - 2;
              px.rgba.b += ( b1       & 0x03) - 2;
              break;
            case QOI_OP_LUMA:
              b2         = *p++;
              vg         = (b1 & 0x3f) - 40;
              px.rgba.r += vg + ((b2 >> 4) & 0x0f);
              px.rgba.g += vg + 8;
              px.rgba.b += vg + (b2        & 0x0f);
              break;
            case QOI_OP_RUN:
              run = (b1 & 0x3f);
              break;
          }
          break;
      }

      index[QOI_COLOR_HASH(px) % 64] = px;
    }

    pd[px_pos + 0] = px.rgba.r;
    pd[px_pos + 1] = px.rgba.g;
    pd[px_pos + 2] = px.rgba.b;

    if (ch == 4) {
      pd[px_pos + 3] = px.rgba.a;
    }
  }

  if (fres.mmap) { im_unmap(fres.raw, fres.size); }

  im->file = fres;
  *dest    = im;

  return IM_OK;

err:
  if (fres.mmap) { im_unmap(fres.raw, fres.size); }
  if (im)        { free(im);                      }

  *dest = NULL;

  return IM_ERR;
}
