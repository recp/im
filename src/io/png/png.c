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
#include <defl/infl.h>

#include "../../file.h"
#include "../../endian.h"
#include "../../zz/zz.h"

#define IM_PNG_TYPE(a,b,c,d)  (((unsigned)(a) << 24) + ((unsigned)(b) << 16)  \
                             + ((unsigned)(c) << 8)  + (unsigned)(d))

typedef enum im_png_filter_t {
  FILT_NONE  = 0,
  FILT_SUB   = 1,
  FILT_UP    = 2,
  FILT_AVG   = 3,
  FILT_PAETH = 4
} im_png_filter_t;

IM_INLINE ImByte mod256u8(ImByte b) { return b & 0xFF; }
IM_INLINE int    mod256i(int b)     { return b & 0xFF; }


//#if defined(__ARM_NEON)
//#  include "arch/neon.h"
//#elif defined(__x86_64__) || defined(_M_X64)
//#  include "arch/x86.h"
//#endif

IM_INLINE int paeth(int a, int b, int c) {
  int p  = a + b - c;
  int pa = abs(p - a);
  int pb = abs(p - b);
  int pc = abs(p - c);

  return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}

static
void
undo_filters(ImByte *pass_data, uint32_t pass_width, uint32_t pass_height, uint32_t bpp) {
  ImByte  *ptr, *row, *pri;
  uint32_t bpr, x, y;

  bpr = pass_width * bpp;
  row = pri = pass_data;
  ptr = row + 1;

  /* handle the first row as a special case to improve other rows */
  switch (row[0]) {
    case FILT_SUB:   for(x=bpp; x<bpr; x++) ptr[x] += ptr[x-bpp];            break;
    case FILT_AVG:   for(x=bpp; x<bpr; x++) ptr[x] += ptr[x-bpp] >> 1;       break;
    case FILT_PAETH: for(x=bpp; x<bpr; x++) ptr[x] += paeth(ptr[x-bpp],0,0); break;
  }

  /* move to the next row */
  pri  = row;
  row += bpr + 1;
  ptr  = row + 1;

  /* process remaining rows */
  for (y = 1; y < pass_height; y++) {
    switch (row[0]) {
      case FILT_SUB:
        for (x=bpp; x<bpr; x++) ptr[x] += ptr[x-bpp];
        break;
      case FILT_UP:
        for (x=0; x<bpr; x++)   ptr[x] += pri[x+1];
        break;
      case FILT_AVG:
        for (x=0;   x<bpp; x++) ptr[x] += pri[x+1] >> 1;
        for (x=bpp; x<bpr; x++) ptr[x] += ((uint16_t)ptr[x-bpp] + pri[x+1]) >> 1;
        break;
      case FILT_PAETH:
        for (x=0;   x<bpp; x++) ptr[x] += paeth(0, pri[x+1], 0);
        for (x=bpp; x<bpr; x++) ptr[x] += paeth(ptr[x-bpp], pri[x+1], pri[x-bpp+1]);
        break;
    }
    /* move to the next row */
    pri  = row;
    row += bpr + 1;
    ptr  = row + 1;
  }
}

static
ImByte*
deinterlace_adam7(ImImage *im, ImByte *src) {
  const uint8_t x_start[7]  = {0,4,0,2,0,1,0};
  const uint8_t y_start[7]  = {0,0,4,0,2,0,1};
  const uint8_t x_delta[7]  = {8,8,4,4,2,2,1};
  const uint8_t y_delta[7]  = {8,8,8,4,4,2,2};

  ImByte  *dest;
  ImByte  *pass_data, *src_row, pass;
  uint32_t width, height, pass_w, pass_h, bpp, stride, x, y, dest_x, dest_y;

  width  = im->width;
  height = im->height;
  bpp    = im->bytesPerPixel;

  /* TODO: calloc vs malloc */
  if (!(dest = calloc(1, width * height * bpp))) return NULL;

  pass_data = src;

  /* process each pass */
  for (pass = 0; pass < 7; pass++) {
    pass_w = (width  - x_start[pass] + x_delta[pass] - 1) / x_delta[pass];
    pass_h = (height - y_start[pass] + y_delta[pass] - 1) / y_delta[pass];

    if (!pass_w || !pass_h)
      continue;

    /* process this pass's filters */
    undo_filters(pass_data, pass_w, pass_h, bpp);

    /* Copy pixels to final positions */
    stride = pass_w * bpp + 1;
    for (y = 0; y < pass_h; y++) {
      src_row = pass_data + y * stride + 1;  /* skip filter byte */
      dest_y  = y * y_delta[pass] + y_start[pass];

      if (dest_y >= height)
        continue;

      for (x = 0; x < pass_w; x++) {
        dest_x = x * x_delta[pass] + x_start[pass];
        if (dest_x < width)
          memcpy(&dest[(dest_y * width + dest_x) * bpp], &src_row[x * bpp], bpp);
      }
    }

    /* move to next pass */
    pass_data += pass_h * (pass_w * bpp + 1);
  }

  return dest;
}

IM_HIDE
ImResult
png_dec(ImImage         ** __restrict dest,
        const char       * __restrict path,
        im_open_config_t * __restrict open_config) {
  infl_stream_t  *imdefl;
  ImImage        *im;
  ImByte         *zipped;
  ImByte         *p, *p_chk, *row, *pri, bitdepth, color, compr, interlace;
  im_png_filter_t filter;
  size_t          len;
  uint32_t        chk_len, chk_type, pal_len, i, j, width, height, src_bpr, bpp, bpc, zippedlen;
  uint16_t       *p16;
  ImFileResult    fres;
  bool            is_cgbi;

  im        = NULL;
  row       = NULL;
  zipped    = NULL;
  imdefl    = NULL;
  interlace = 0;
  fres      = im_readfile(path, open_config->openIntent != IM_OPEN_INTENT_READWRITE);

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

  if (p[0] != 0x89 ||
      p[1] != 0x50 || p[2] != 0x4E || p[3] != 0x47 ||
      p[4] != 0x0D || p[5] != 0x0A ||
      p[6] != 0x1A ||
      p[7] != 0x0A) {
    goto err;
  }

  p += 8;

  im->file           = fres;
  im->openIntent     = open_config->openIntent;
  im->byteOrder      = open_config->byteOrder;
  im->ori            = IM_ORIENTATION_UP;
  im->fileFormatType = IM_FILEFORMATTYPE_PNG;
  height             = width = bpp = bpc = zippedlen = 0;
  bitdepth           = 8;

  for (;;) {
    chk_len  = im_get_u32_endian(p, false); p += 4;
    chk_type = im_get_u32_endian(p, false); p += 4;
    p_chk    = p;

    switch (chk_type) {
      case IM_PNG_TYPE('C','g','B','I'):
        is_cgbi = true;
        break;
      case IM_PNG_TYPE('I','H','D','R'): {
        im->width            = width  = im_get_u32_endian(p, false); p += 4;
        im->height           = height = im_get_u32_endian(p, false); p += 4;
        bitdepth             = *p++;
        color                = *p++;
        compr                = *p++;
        filter               = *p++;
        interlace            = *p;

        bpc                  = im_maxiu8(bitdepth / 8, 1);
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
            im->bitsPerPixel       = bitdepth;
            im->bytesPerPixel      = bpp = bpc;
            im->format             = IM_FORMAT_GRAY;
            im->alphaInfo          = IM_ALPHA_NONE;
            im->componentsPerPixel = 1;
            break;
          case 2:
            im->bitsPerPixel       = bitdepth * 3;
            im->bytesPerPixel      = bpp = bpc * 3;
            im->format             = IM_FORMAT_RGB;
            im->alphaInfo          = IM_ALPHA_NONE;
            im->componentsPerPixel = 3;
            break;
          case 3: {
            /* palette */
            im_pal_t *pal;

            im->bitsPerPixel       = bitdepth;
            im->bytesPerPixel      = bpp = bpc;
            im->format             = IM_FORMAT_RGB;
            im->alphaInfo          = IM_ALPHA_NONE;
            im->componentsPerPixel = 3;

            pal     = calloc(1, sizeof(*pal));
            im->pal = pal;
            break;
          }
          case 4:
            im->bitsPerPixel       = bitdepth * 2;
            im->bytesPerPixel      = bpp = bpc * 2;
            im->format             = IM_FORMAT_GRAY_ALPHA;
            im->alphaInfo          = IM_ALPHA_LAST;
            im->componentsPerPixel = 2;
            break;
          case 6:
            im->bitsPerPixel       = bitdepth * 4;
            im->bytesPerPixel      = bpp = bpc * 4;
            im->format             = IM_FORMAT_RGBA;
            im->alphaInfo          = IM_ALPHA_LAST;
            im->componentsPerPixel = 4;
            break;
          default:
            goto err;  /* invalid color type */
        }

        /* TODO: */
        // im->len       = len = (bpp + im->row_pad_last) * (width + 1) * height;
        //        im->len       = len = (bpp * width + im->row_pad_last + 1) * height;
        im->len       = len = (bpp + im->row_pad_last) * (width + 1) * height;
        im->data.data = malloc(len);
        zipped        = row = malloc(len);

        imdefl        = infl_init(im->data.data, (uint32_t)im->len, 1);
        break;
      }
      case IM_PNG_TYPE('P','L','T','E'): {
        if (im->pal) {
          ImByte *pal;

          if (chk_len > 256 * 3 || (pal_len = chk_len / 3) * 3 != chk_len)
            goto err; /* invalid PLTE corrupt PNG */

          im->pal->len   = chk_len;
          im->pal->pal   = pal = malloc(chk_len);
          im->pal->white = bitdepth;
          im->pal->count = pal_len - 1;
          im->alphaInfo  = IM_ALPHA_NONE;

          memcpy(pal, p, chk_len);
        }
        break;
      }
      case IM_PNG_TYPE('t','R','N','S'): {
        printf("\nTRNS not implemented yet\n");
        break;
      }
      case IM_PNG_TYPE('I','D','A','T'): {
        memcpy(row, p, chk_len);
        row       += chk_len;
        zippedlen += chk_len;

        //        infl_include(imdefl, p, chk_len);
        break;
      }
      case IM_PNG_TYPE('I','E','N','D'): {
        goto nx;
      }
    }

    p = p_chk + chk_len + 4; /* 4: CRC */
  }

nx:

  /* TODO: */
  //   mz_uncompress(im->data.data, &len, zipped, zippedlen);
  //  decompress_idat(zipped, zippedlen, im->data.data, im->len);
  //  zsinflate(im->data.data, (int)im->len, zipped, zippedlen);

  defl_include(imdefl, zipped, zippedlen);
  if (infl(imdefl)) {
    goto err;
  }

  if (unlikely(interlace)) {
    ImByte *deinterlaced;

    if (!(deinterlaced = deinterlace_adam7(im, im->data.data)))
      goto err;

    free(im->data.data);
    im->data.data = deinterlaced;

    goto af;
  }

  /* non-interlaced: undo filter in the usual way */
  src_bpr = bpp * width * ((float)im_minu8(bitdepth, 8) / 8.0f);
  row     = p = im->data.data;
  pri     = p;
  i       = 0;

  /* undo filter */

  switch ((int)*row) {
    case FILT_UP:
      memmove(p, row + 1, src_bpr);
      goto nx_row;
    case FILT_AVG:
      im_pixcpy(p, row + 1, bpp);
      for (j = bpp; j < src_bpr; j++) p[j] = row[j+1] + (p[j-bpp] >> 1);
      goto nx_row;
    case FILT_PAETH:
      im_pixcpy(p, row + 1, bpp);
      for (j = bpp; j < src_bpr; j++) p[j] = row[j+1] + paeth(p[j-bpp], 0, 0);
      goto nx_row;
    default: break;
  }

  for (; i < height; ) {
    switch (row[0]) {
      case FILT_NONE:
        memmove(row - i, row + 1, src_bpr);
        break;
      case FILT_SUB:
        memmove(p, row + 1, bpp);
        for (j=bpp; j<src_bpr; j++) p[j] = row[j+1] + p[j - bpp];
        break;
      case FILT_UP:
        for (j=0; j<src_bpr; j++)   p[j] = row[j+1] + pri[j];
        break;
      case FILT_AVG:
        for (j=0;   j<bpp;     j++) p[j] = row[j+1] + ((pri[j]) >> 1);
        for (j=bpp; j<src_bpr; j++) p[j] = row[j+1] + ((p[j-bpp] + pri[j]) >> 1);
        break;
      case FILT_PAETH:
        for (j=0;   j<bpp; j++)     p[j] = row[j+1] + paeth(0, pri[j], 0);
        for (j=bpp; j<src_bpr; j++) p[j] = row[j+1] + paeth(p[j-bpp], pri[j], pri[j-bpp]);
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

af:
  /* fix byte order */
  if (unlikely(bpc > 1)) {
    switch (open_config->byteOrder) {
      case IM_BYTEORDER_LITTLE:
        if (bpc == 2) {
          for (p16 = im->data.data, i = 0; i < (len >> 1); i++) {
            p16[i] = bswapu16(p16[i]);
          }
        }
        break;
      case IM_BYTEORDER_HOST:
#if __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
        if (bpc == 2) {
          for (p16 = im->data.data, i = 0; i < (len >> 1); i++) {
            p16[i] = bswapu16(p16[i]);
          }
        }
#endif
        break;
      default: break; /* _ANY, _BIG_ENDIAN == noop */
    }
  }

  if (im->pal && !open_config->supportsPal) {
    /* TODO: */
  }

  if (fres.mmap) { im_unmap(fres.raw, fres.size); }

  im->file = fres;
  *dest    = im;

  //  unzip_cleanup(zip);

  return IM_OK;

err:
  if (fres.mmap) { im_unmap(fres.raw, fres.size); }
  if (im)        { free(im);                      }
  *dest = NULL;

  infl_destroy(imdefl);

  return IM_ERR;
}
