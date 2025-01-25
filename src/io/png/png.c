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

IM_INLINE int paeth(int a, int b, int c) {
  int p  = a + b - c;
  int pa = abs(p - a);
  int pb = abs(p - b);
  int pc = abs(p - c);

  return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}

/*
#if defined(__ARM_NEON)
#  include "arch/neon.h"
#elif defined(__x86_64__) || defined(_M_X64)
#  include "arch/x86.h"
#endif
*/

static
void
undo_filters_adam7(ImByte *pass_data, uint32_t pass_width, uint32_t pass_height, uint32_t bpp, uint8_t bitdepth) {
  ImByte  *p, *row, *pri;
  uint32_t bpr, x, y;

  bpr = bpp * pass_width * ((float)im_minu8(bitdepth, 8) / 8.0f);
  row = pri = pass_data;
  p   = row + 1;

  /* handle the first row as a special case to improve other rows */
  /* first row special case */
  switch (row[0]) {
    case FILT_NONE:
      memmove(p, row + 1, bpr);
      break;
    case FILT_SUB:
      memmove(p, row + 1, bpp);
      for (x=bpp; x<bpr; x++) p[x] = row[x+1] + p[x-bpp];
      break;
    case FILT_UP:
      memmove(p, row + 1, bpr);
      break;
    case FILT_AVG:
      memmove(p, row + 1, bpp);
      for (x=bpp; x<bpr; x++) p[x] = row[x+1] + (p[x-bpp]>>1);
      break;
    case FILT_PAETH:
      memmove(p, row + 1, bpp);
      for (x=bpp; x<bpr; x++) p[x] = row[x+1] + paeth(p[x-bpp], 0, 0);
      break;
  }

  /* move to the next row */
  pri  = row;
  row += bpr + 1;
  p    = row + 1;

  /* process remaining rows */
  for (y = 1; y < pass_height; y++) {
    switch (row[0]) {
      case FILT_SUB:
        for (x=bpp; x<bpr; x++) p[x] += p[x-bpp];
        break;
      case FILT_UP:
        for (x=0; x<bpr; x++)   p[x] += pri[x+1];
        break;
      case FILT_AVG:
        for (x=0;   x<bpp; x++) p[x] += pri[x+1] >> 1;
        for (x=bpp; x<bpr; x++) p[x] += ((uint16_t)p[x-bpp] + pri[x+1]) >> 1;
        break;
      case FILT_PAETH:
        for (x=0;   x<bpp; x++) p[x] += paeth(0, pri[x+1], 0);
        for (x=bpp; x<bpr; x++) p[x] += paeth(p[x-bpp], pri[x+1], pri[x-bpp+1]);
        break;
    }
    /* move to the next row */
    pri  = row;
    row += bpr + 1;
    p    = row + 1;
  }
}

static
void
undo_filters(ImByte *data, uint32_t width, uint32_t height, uint32_t bpp, uint8_t bitdepth) {
  ImByte  *p, *row, *pri;
  uint32_t bpr, x, y;

  bpr = bpp * width * ((float)im_minu8(bitdepth, 8) / 8.0f);
  /* bpr = width * bpp; */
  row = p = data;
  pri = NULL;

  /* first row special case */
  switch (row[0]) {
    case FILT_NONE:
      memmove(p, row + 1, bpr);
      break;
    case FILT_SUB:
      memmove(p, row + 1, bpp);
      for (x=bpp; x<bpr; x++) p[x] = row[x+1] + p[x-bpp];
      break;
    case FILT_UP:
      memmove(p, row + 1, bpr);
      break;
    case FILT_AVG:
      memmove(p, row + 1, bpp);
      for (x=bpp; x<bpr; x++) p[x] = row[x+1] + (p[x-bpp]>>1);
      break;
    case FILT_PAETH:
      memmove(p, row + 1, bpp);
      for (x=bpp; x<bpr; x++) p[x] = row[x+1] + paeth(p[x-bpp], 0, 0);
      break;
  }

  /* remaining rows */
  for (y = 1; y < height; y++) {
    row += bpr + 1;
    pri  = p;
    p   += bpr;

    switch (row[0]) {
      case FILT_NONE:
        memmove(p, row + 1, bpr);
        break;
      case FILT_SUB:
        memmove(p, row + 1, bpp);
        for (x=bpp; x<bpr; x++) p[x] = row[x+1] + p[x-bpp];
        break;
      case FILT_UP:
        for (x=0; x<bpr; x++)   p[x] = row[x+1] + pri[x];
        break;
      case FILT_AVG:
        for (x=0;   x<bpp; x++) p[x] = row[x+1] + (pri[x]>>1);
        for (x=bpp; x<bpr; x++) p[x] = row[x+1] + ((p[x-bpp] + pri[x])>>1);
        break;
      case FILT_PAETH:
        for (x=0;   x<bpp; x++) p[x] = row[x+1] + paeth(0,pri[x],0);
        for (x=bpp; x<bpr; x++) p[x] = row[x+1] + paeth(p[x-bpp],pri[x],pri[x-bpp]);
        break;
    }
  }
}

static
ImByte*
adam7(ImImage * __restrict im,
      ImByte  * __restrict src,
      uint32_t             width,
      uint32_t             height,
      uint8_t              bpp,
      uint8_t              bitdepth) {
  const uint8_t xstart[7]  = {0,4,0,2,0,1,0};
  const uint8_t ystart[7]  = {0,0,4,0,2,0,1};
  const uint8_t xdelta[7]  = {8,8,4,4,2,2,1};
  const uint8_t ydelta[7]  = {8,8,8,4,4,2,2};

  ImByte  *dest;
  ImByte  *pass_data, *src_row, pass;
  uint32_t pass_w, pass_h, stride, x, y, dest_x, dest_y;

  /* TODO: calloc vs malloc */
  if (!(dest = calloc(1, width * height * bpp))) return NULL;

  pass_data = src;

  /* process each pass */
  for (pass = 0; pass < 7; pass++) {
    pass_w = (width -xstart[pass]+xdelta[pass]-1) / xdelta[pass];
    pass_h = (height-ystart[pass]+ydelta[pass]-1) / ydelta[pass];

    if (!pass_w || !pass_h)
      continue;

    /* process this pass's filters */
    undo_filters_adam7(pass_data, pass_w, pass_h, bpp, bitdepth);

    /* Copy pixels to final positions */
    stride = pass_w * bpp + 1;
    for (y = 0; y < pass_h; y++) {
      src_row = pass_data + y * stride + 1;  /* skip filter byte */
      dest_y  = y * ydelta[pass] + ystart[pass];

      if (dest_y >= height)
        continue;

      for (x = 0; x < pass_w; x++) {
        dest_x = x * xdelta[pass] + xstart[pass];
        if (dest_x < width)
          memcpy(&dest[(dest_y*width+dest_x)*bpp], &src_row[x*bpp], bpp);
      }
    }

    /* move to next pass */
    pass_data += pass_h*(pass_w*bpp+1);
  }

  return dest;
}

static
void
fix_endianness(ImImage *im) {
  uint16_t *data;
  size_t    pixels;

  if (im->bitsPerComponent <= 8)
    return;

  pixels = im->width * im->height * im->componentsPerPixel;
  data   = im->data.data;

  switch (im->byteOrder) {
    case IM_BYTEORDER_LITTLE:
      for (size_t i = 0; i < pixels; i++)
        data[i] = bswapu16(data[i]);
      break;
    case IM_BYTEORDER_HOST:
#if __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
      for (size_t i = 0; i < pixels; i++)
        data[i] = bswapu16(data[i]);
#endif
      break;
    default: /* BIG_ENDIAN or ANY - no change needed */
      break;
  }
}

static
bool
expand_palette(ImImage *im) {
  uint8_t *new_data, *src, *dst, *pal, *alpha, idx;
  size_t   new_size, alpha_count, i;
  bool     has_alpha;

  if (!im->pal || !im->pal->pal)
    return false;

  /* Check both the transparency data and alphaInfo */
  has_alpha = (im->alphaInfo == IM_ALPHA_LAST || im->alphaInfo == IM_ALPHA_FIRST) 
              || (im->transparency && im->transparency->value.pal.alpha);

  new_size = im->width * im->height * (has_alpha ? 4 : 3); /* RGBA or RGB output */
  new_data = malloc(new_size);

  if (!new_data)
    return false;

  src         = im->data.data;
  dst         = new_data;
  pal         = im->pal->pal;
  alpha       = has_alpha ? im->transparency->value.pal.alpha : NULL;
  alpha_count = has_alpha ? im->transparency->value.pal.count : 0;

  for (i = 0; i < im->width * im->height; i++) {
    idx = src[i];
    if (idx >= im->pal->count) {
      free(new_data);
      return false;
    }

    memcpy(dst, pal + idx * 3, 3);
    if (has_alpha) {
      dst[3] = (idx < alpha_count) ? alpha[idx] : 255;
      dst += 4;
    } else {
      dst += 3;
    }
  }

  free(im->data.data);

  im->data.data          = new_data;
  im->format             = has_alpha ? IM_FORMAT_RGBA : IM_FORMAT_RGB;
  im->alphaInfo          = has_alpha ? IM_ALPHA_LAST  : IM_ALPHA_NONE;
  im->bytesPerPixel      = has_alpha ? 4  : 3;
  im->bitsPerPixel       = has_alpha ? 32 : 24;
  im->componentsPerPixel = has_alpha ? 4  : 3;
  im->len                = new_size;

  return true;
}

IM_HIDE
ImResult
png_dec(ImImage         ** __restrict dest,
        const char       * __restrict path,
        im_open_config_t * __restrict oconfig) {
  infl_stream_t  *imdefl;
  ImImage        *im;
  ImByte         *zipped;
  ImByte         *p, *p_chk, *row, bitdepth, color, compr, interlace;
  im_png_filter_t filter;
  size_t          len;
  uint32_t        chk_len, chk_type, pal_len, width, height, bpp, bpc, zippedlen;
  ImFileResult    fres;
  bool            is_cgbi;

  im        = NULL;
  row       = NULL;
  zipped    = NULL;
  imdefl    = NULL;
  interlace = 0;
  fres      = im_readfile(path, oconfig->openIntent != IM_OPEN_INTENT_READWRITE);

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

  p    += 8;
  color = 0;
  len   = 0;

  im->file           = fres;
  im->openIntent     = oconfig->openIntent;
  im->byteOrder      = oconfig->byteOrder;
  im->ori            = IM_ORIENTATION_UP;
  im->fileFormatType = IM_FILEFORMATTYPE_PNG;
  height             = width = bpp = bpc = zippedlen = 0;
  bitdepth           = 8;

  for (;;) {
    chk_len  = u32be(&p);
    chk_type = u32be(&p);
    p_chk    = p;

    switch (chk_type) {
      case IM_PNG_TYPE('C','g','B','I'):
        is_cgbi = true;
        break;
      case IM_PNG_TYPE('I','H','D','R'): {
        im->width            = width  = u32be(&p);
        im->height           = height = u32be(&p);
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
          } break;
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
      } break;
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
      } break;
      case IM_PNG_TYPE('t','R','N','S'): {
        ImTransparency* trans;

        /* tRNS must come after IHDR and before IDAT */
        if (!(width > 0 && height > 0) || (zippedlen > 0))
          goto err;

        /**
         * don't allow tRNS for images that already have alpha
         * instead of error just ignore chunk.
         */
        if (im->alphaInfo != IM_ALPHA_NONE)
          goto co;

        if (!(trans = calloc(1, sizeof(*trans))))
          goto err;
      
        switch (color) {
          case 0: { /* grayscale */
            uint16_t gray;

            if (chk_len != 2)
              goto err;
            
            gray = im_get_u16_endian(p, false);
            if (bitdepth < 16)
              gray = gray & ((1 << bitdepth) - 1);

            trans->value.gray.gray = gray;
            im->alphaInfo = IM_ALPHA_LAST;
            im->format    = IM_FORMAT_GRAY_ALPHA;
          } break;
          case 2: { /* RGB */
            if (chk_len != 6)
              goto err;
              
            if (bitdepth == 16) {
              trans->value.rgb.red   = im_get_u16_endian(p, false);
              trans->value.rgb.green = im_get_u16_endian(p + 2, false);
              trans->value.rgb.blue  = im_get_u16_endian(p + 4, false);
            } else {
              trans->value.rgb.red   = (im_get_u16_endian(p, false) & 0xFF);
              trans->value.rgb.green = (im_get_u16_endian(p + 2, false) & 0xFF);
              trans->value.rgb.blue  = (im_get_u16_endian(p + 4, false) & 0xFF);
            }
            
            im->alphaInfo = IM_ALPHA_LAST;
            im->format    = IM_FORMAT_RGBA;
          } break;
          case 3: { /* palette */
            if (!im->pal || chk_len > 256)
              goto err;
            
            if (chk_len > im->pal->len / 3)
              goto err; /* tRNS length must not exceed palette length */

            trans->value.pal.alpha = malloc(chk_len);
            trans->value.pal.count = chk_len;
            memcpy(trans->value.pal.alpha, p, chk_len);
            
            im->alphaInfo = IM_ALPHA_LAST;

            /* format remains RGB since alpha is in palette */
          } break;
          default:
            free(trans);
            goto err;
        }
        
        im->transparency = trans;
      } break;
      case IM_PNG_TYPE('I','D','A','T'): {
        memcpy(row, p, chk_len);
        row       += chk_len;
        zippedlen += chk_len;

        //        infl_include(imdefl, p, chk_len);
      } break;
      case IM_PNG_TYPE('I','E','N','D'): {
        goto nx;
      }
      case IM_PNG_TYPE('b','K','G','D'): {
        ImBackground* bg;

        if (!(bg = calloc(1, sizeof(*bg))))
          goto err;

        switch (color) {
          case 0: /* grayscale */
            bg->value.gray.gray = im_get_u16_endian(p, false);
            break;
          case 2: /* RGB */
            bg->value.rgb.red   = im_get_u16_endian(p, false);
            bg->value.rgb.green = im_get_u16_endian(p + 2, false);
            bg->value.rgb.blue  = im_get_u16_endian(p + 4, false);
            break;
          case 3: /* palette */
            bg->value.palette.index = *p;
            break;
          default:
            free(bg);
            goto err;
        }
        im->background = bg;
      } break;
      case IM_PNG_TYPE('g','A','M','A'): {
        if (chk_len != 4) goto err;
        im->gamma = u32be(&p) / 100000.0;
      } break;
      case IM_PNG_TYPE('c','H','R','M'): {
        ImChromaticity *chrm;

        if (chk_len != 32 || !(chrm = calloc(1, sizeof(*chrm))))
          goto err;

        chrm->whiteX = u32be(&p) / 100000.0;
        chrm->whiteY = u32be(&p) / 100000.0;
        chrm->redX   = u32be(&p) / 100000.0;
        chrm->redY   = u32be(&p) / 100000.0;
        chrm->greenX = u32be(&p) / 100000.0;
        chrm->greenY = u32be(&p) / 100000.0;
        chrm->blueX  = u32be(&p) / 100000.0;
        chrm->blueY  = u32be(&p) / 100000.0;

        im->chrm = chrm;
      } break;
      case IM_PNG_TYPE('s','R','G','B'): {
        if (chk_len != 1) goto err;
        im->srgbIntent = *p;
        im->colorSpace = IM_COLORSPACE_sRGB;
      } break;
      case IM_PNG_TYPE('i','C','C','P'): {
        ImByte  *name_end;
        uint8_t *zprofile, *profile;
        uint32_t name_len, zprofile_len, profile_len;

        if (!(name_end = memchr(p, 0, chk_len)))
          goto err;

        name_len = (uint32_t)(name_end - p);

        /* compression method must be 0 */
        if (*(p + name_len + 1) != 0)
          goto err;

        zprofile     = p + name_len + 2;
        zprofile_len = chk_len - (name_len + 2);

        /* TODO: libdefl doesnt support to export actual length for now */
        profile_len  = zprofile_len * 4;
        profile      = calloc(1, profile_len);

        if (!infl_buf(zprofile, zprofile_len, profile, profile_len, 1))
          goto err;

        im->iccProfile     = profile;
        im->iccProfileSize = profile_len;

        /* default, can be overridden by profile */
        im->colorSpace     = IM_COLORSPACE_sRGB;
      } break;
      case IM_PNG_TYPE('p','H','Y','s'): {
        ImPhysicalDim *phys;

        if (chk_len != 9 || !(phys = calloc(1, sizeof(*phys))))
          goto err;

        phys->pixelsPerUnitX = u32be(&p);
        phys->pixelsPerUnitY = u32be(&p);
        phys->unit           = p[8];

        im->physicalDim      = phys;
      } break;
      case IM_PNG_TYPE('t','I','M','E'): {
        ImTimeStamp *ts;

        if (chk_len != 7 || !(ts = calloc(1, sizeof(*ts))))
          goto err;

        ts->year      = im_get_u16_endian(p, false);
        ts->month     = p[2];
        ts->day       = p[3];
        ts->hour      = p[4];
        ts->minute    = p[5];
        ts->second    = p[6];
        im->timeStamp = ts;
      } break;
    }

  co:
    p = p_chk + chk_len + 4; /* 4: CRC */
  }

nx:

  /* TODO: */
  //   mz_uncompress(im->data.data, &len, zipped, zippedlen);
  //  decompress_idat(zipped, zippedlen, im->data.data, im->len);
  //  zsinflate(im->data.data, (int)im->len, zipped, zippedlen);

  infl_include(imdefl, zipped, zippedlen);
  if (infl(imdefl)) {
    goto err;
  }

  if (unlikely(interlace)) {
    ImByte *deint;

    if (!(deint = adam7(im, im->data.data, width, height, bpp, bitdepth)))
      goto err;

    free(im->data.data);
    im->data.data = deint;

    goto af;
  }

  /* non-interlaced: undo filter in the usual way */
  undo_filters(im->data.data, width, height, bpp, bitdepth);

af:
  /* fix byte order */
  fix_endianness(im);

  if (im->pal && !oconfig->supportsPal) {
    if (!expand_palette(im))
      goto err;
  }

  if (fres.mmap) { im_unmap(fres.raw, fres.size); }

  im->file = fres;
  *dest    = im;

  infl_destroy(imdefl);

  return IM_OK;

err:
  if (fres.mmap) { im_unmap(fres.raw, fres.size); }
  if (im) {
    if (im->transparency) free(im->transparency);
    if (im->iccProfile)   free(im->iccProfile);
    if (im->iccProfile)   free(im->iccProfile);

    /* free other mallocs/callocs... */
    free(im);
  }
  *dest = NULL;

  infl_destroy(imdefl);

  return IM_ERR;
}
