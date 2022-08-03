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

#include "png.h"
#include "../../file.h"
#include "../../endian.h"
#include "../../zz/miniz/miniz.h"

#define IM_PNG_TYPE(a,b,c,d)  (((unsigned)(a) << 24) + ((unsigned)(b) << 16)  \
                             + ((unsigned)(c) << 8)  + (unsigned)(d))

typedef enum im_png_filter_t {
  IM_PNG_FILTER_NONE  = 0,
  IM_PNG_FILTER_SUB   = 1,
  IM_PNG_FILTER_UP    = 2,
  IM_PNG_FILTER_AVG   = 3,
  IM_PNG_FILTER_PAETH = 4
} im_png_filter_t;

IM_INLINE int paeth(int a, int b, int c) {
  int p, pa, pb, pc;

  p  = a + b - c;
  pa = abs(p - a);
  pb = abs(p - b);
  pc = abs(p - c);

  if (pa <= pb && pa <= pc) return a;
  else if (pb <= pc)        return b;
  else                      return c;
}

IM_HIDE
ImResult
png_dec(ImImage         ** __restrict dest,
        const char       * __restrict path,
        im_open_config_t * __restrict open_config) {
  ImImage            *im;
  ImByte             *p, *p_chk, *row, *pri, bitdepth, color, compr, interlace;
  im_png_filter_t     filter;
  uint32_t            dataoff, chk_len, chk_type, pal_len, i, j, width, height, src_bpr, bpp;
  ImFileResult        fres;
  ImByte              pal[1024], pal_img_n;
  bool                is_cgbi;

  im   = NULL;
  fres = im_readfile(path, open_config->openIntent != IM_OPEN_INTENT_READWRITE);
  
  if (fres.ret != IM_OK) {
    goto err;
  }
  
  /* decode, this process will be optimized after decoding is done */
  im  = calloc(1, sizeof(*im));
  p   = fres.raw;
  
  /*
     Magic number types:
     -------------------
     89 50 4E 47 0D 0A 1A 0A
   */

  if (p[0] != 0x89
      && p[1] != 0x50 && p[2] != 0x4E && p[3] != 0x47
      && p[4] != 0x0D && p[5] != 0x0A
      && p[6] != 0x1A
      && p[7] != 0x0A) {
    goto err;
  }

  p += 8;

  im->file           = fres;
  im->openIntent     = open_config->openIntent;
  im->byteOrder      = IM_BYTEORDER_HOST; /* convert to host */
  im->ori            = IM_ORIENTATION_UP;
  im->fileFormatType = IM_FILEFORMATTYPE_PNG;
  pal_img_n          = height = width = bpp = 0;

  for (;;) {
    chk_len  = im_get_u32_endian(p, false); p += 4;
    chk_type = im_get_u32_endian(p, false); p += 4;
    p_chk    = p;

    switch (chk_type) {
      case IM_PNG_TYPE('C','g','B','I'):
        is_cgbi = true;
        break;
      case IM_PNG_TYPE('I','H','D','R'): {
        im->width  = width  = im_get_u32_endian(p, false); p += 4;
        im->height = height = im_get_u32_endian(p, false); p += 4;
        bitdepth   = *p++;
        color      = *p++;
        compr      = *p++;
        filter     = *p++;
        interlace  = *p;
        
        im->bitsPerComponent = bitdepth;
        
        /*
         Color    Allowed    Interpretation
         Type    Bit Depths
         ----------------------------------------------------------------------
         0       1,2,4,8,16  Each pixel is a grayscale sample.
         2       8,16        Each pixel is an R,G,B triple.
         3       1,2,4,8     Each pixel is a palette index; a PLTE chunk must appear.
         4       8,16        Each pixel is a grayscale sample, followed by an alpha sample.
         6       8,16        Each pixel is an R,G,B triple, followed by an alpha sample.
         */
        
        switch (color) {
          case 0:
            im->bitsPerPixel  = im->bitsPerComponent;
            im->bytesPerPixel = bpp = im->bitsPerComponent == 16 ? 2 : 1;
            im->format        = IM_FORMAT_GRAY;
            im->alphaInfo     = IM_ALPHA_NONE;
            break;
          case 2:
            im->bitsPerPixel  = im->bitsPerComponent * 3;
            im->bytesPerPixel = bpp = (im->bitsPerComponent == 16 ? 2 : 1) * 3;
            im->format        = IM_FORMAT_RGB;
            im->alphaInfo     = IM_ALPHA_NONE;
            break;
          case 3:
            im->bitsPerPixel  = im->bitsPerComponent * 4; /* TODO: check plt to get color type */
            im->bytesPerPixel = bpp = (im->bitsPerComponent == 16 ? 2 : 1) * 4;
            im->format        = IM_FORMAT_RGB;
            im->alphaInfo     = IM_ALPHA_NONE;
            break;
          case 4:
            im->bitsPerPixel  = im->bitsPerComponent * 2;
            im->bytesPerPixel = bpp = (im->bitsPerComponent == 16 ? 2 : 1) * 2;
            im->format        = IM_FORMAT_GRAY_ALPHA;
            im->alphaInfo     = IM_ALPHA_LAST;
            break;
          case 6:
            im->bitsPerPixel  = im->bitsPerComponent * 4;
            im->bytesPerPixel = bpp = (im->bitsPerComponent == 16 ? 2 : 1) * 4;
            im->format        = IM_FORMAT_RGBA;
            im->alphaInfo     = IM_ALPHA_LAST;
            break;
          default:
            goto err;  /* invalid color type */
        }
        
        /* TODO: */
        im->len       = (bpp + im->row_pad_last) * (width + 1) * height;
        im->data.data = malloc(im->len);
        break;
      }
      case IM_PNG_TYPE('P','L','T','E'): {
        if (chk_len > 256 * 3)
          goto err; /* invalid PLTE corrupt PNG */

        pal_len = chk_len / 3;
        if (pal_len * 3 != chk_len)
          goto err; /* invalid PLTE corrupt PNG */

        for (i = 0; i < pal_len; ++i) {
          pal[i * 4 + 0] = *p++;
          pal[i * 4 + 1] = *p++;
          pal[i * 4 + 2] = *p++;
          pal[i * 4 + 3] = 255;
        }
        break;
      }
      case IM_PNG_TYPE('t','R','N','S'): {
        printf("\nTRNS not implemented yet\n");
        break;
      }
      case IM_PNG_TYPE('I','D','A','T'): {
        printf("idat -- p: %p\n", p);

        mz_uncompress(im->data.data, &im->len, p, chk_len);
        break;
      }
      case IM_PNG_TYPE('I','E','N','D'): {
        goto nx;
      }
    }

    p = p_chk + chk_len + 4; /* 4: CRC */
  }

nx:

  src_bpr = bpp * width;
  row = p = im->data.data;
  pri = p;
  i   = 0;

  /*undo filter */

  switch ((int)*row) {
    case IM_PNG_FILTER_UP:
      memmove(p, row + 1, src_bpr);
      goto nx_row;
    case IM_PNG_FILTER_AVG:
      im_pixcpy(p, row + 1, bpp);
      for (j = bpp; j < src_bpr; j++) {
        p[j] = row[j + 1] + (p[j - bpp] >> 1);
      }
      goto nx_row;
    case IM_PNG_FILTER_PAETH:
      im_pixcpy(p, row + 1, bpp);
      for (j = bpp; j < src_bpr; j++) {
        p[j] = row[j + 1] + paeth(p[j - bpp], 0, 0);
      }
      goto nx_row;
    default: break;
  }

  for (; i < height;) {
    switch ((int)*row) {
      case IM_PNG_FILTER_NONE:
        memmove(row - i, row + 1, src_bpr);
        break;
      case IM_PNG_FILTER_SUB:
        im_pixcpy(p, row + 1, bpp);
        for (j = bpp; j < src_bpr; j++) {
          p[j] = row[j + 1] + p[j - bpp];
        }
        break;
      case IM_PNG_FILTER_UP:
        for (j = 0; j < src_bpr; j++) {
          p[j] = row[j + 1] + pri[j];
        }
        break;
      case IM_PNG_FILTER_AVG:
        for (j = 0; j < bpp; j++) {
          p[j] = row[j + 1] + ((pri[j]) >> 1);
        }

        for (j = bpp; j < src_bpr; j++) {
          p[j] = row[j + 1] + ((p[j - bpp] + pri[j]) >> 1);
        }
        break;
      case IM_PNG_FILTER_PAETH:
        for (j = 0; j < bpp; j++) {
          p[j] = row[j + 1] + paeth(0, pri[j], 0);
        }

        for (j = bpp; j < src_bpr; j++) {
          p[j] = row[j + 1] + paeth(p[j - bpp], pri[j], pri[j - bpp]);
        }
        break;
      default:
        goto err; /* unknown or unimplemented filter */
    }

  nx_row:
    row += src_bpr + 1;
    pri  = p;
    p   += src_bpr;
    i++;
  }

  *dest = im;
  im->file = fres;

  if (fres.mmap) {
    im_unmap(fres.raw, fres.size);
  }

  return IM_OK;
err:
  if (fres.mmap) {
    im_unmap(fres.raw, fres.size);
  }
  
  if (im) {
    free(im);
  }
  
  *dest = NULL;
  return IM_ERR;
}
