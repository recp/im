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

#include "bmp.h"
#include "../../file.h"
#include "../../endian.h"


/*
 References:
 [0] https://en.wikipedia.org/wiki/BMP_file_format
 [1] http://www.edm2.com/0107/os2bmp.html
 [2] http://netghost.narod.ru/gff/graphics/summary/os2bmp.htm
 [3] http://www.redwoodsoft.com/~dru/museum/gfx/gff/sample/code/os2bmp/os2_code.txt
 [4] https://gibberlings3.github.io/iesdp/file_formats/ie_formats/bmp.htm
 [5] http://www.martinreddy.net/gfx/2d/BMP.txt
 */

typedef enum im_bmp_compression_method_t {
  IM_BMP_COMPR_RGB            = 0,  /* none                           | Most common */
  IM_BMP_COMPR_RLE8           = 1,  /* RLE 8-bit/pixel                | Can be used only with 8-bit/pixel bitmaps */
  IM_BMP_COMPR_RLE4           = 2,  /* RLE 4-bit/pixel                | Can be used only with 4-bit/pixel bitmaps */
  IM_BMP_COMPR_BITFIELDS      = 3,  /* OS22XBITMAPHEADER: Huffman 1D  | BITMAPV2INFOHEADER: RGB bit field masks, BITMAPV3INFOHEADER+: RGBA */
  IM_BMP_COMPR_JPEG           = 4,  /* OS22XBITMAPHEADER: RLE-24      | BITMAPV4INFOHEADER+: JPEG image for printing[14] */
  IM_BMP_COMPR_PNG            = 5,  /*                                | BITMAPV4INFOHEADER+: PNG image for printing[14] */
  IM_BMP_COMPR_ALPHABITFIELDS = 6,  /* RGBA bit field masks           | only Windows CE 5.0 with .NET 4.0 or later */
  IM_BMP_COMPR_CMYK           = 11, /* none                           | only Windows Metafile CMYK[4] */
  IM_BMP_COMPR_CMYKRLE8       = 12, /* RLE-8                          | only Windows Metafile CMYK */
  IM_BMP_COMPR_CMYKRLE4       = 13, /* RLE-4                          | only Windows Metafile CMYK */
  
  IM_BMP_COMPR_HUFFMAN1D      = 3,
  IM_BMP_COMPR_RLE24          = 4
} im_bmp_compression_method_t;

IM_INLINE
ImByte *
im_setpx3_8(ImByte * __restrict dst, char * __restrict plt) {
  dst[0] = plt[2];
  dst[1] = plt[1];
  dst[2] = plt[0];
  return dst + 3;
}

IM_HIDE
ImResult
bmp_dec(ImImage ** __restrict dest, const char * __restrict path) {
  ImImage            *im;
  char               *p, *p_end, *p_back, *plt, *bfi;
  size_t              imlen;
  ImFileResult        fres;
  ImByte              bpp, c, *pd;
  uint32_t            dataoff, hsz, width, min_bytes, height, compr,
                      i, j, idx, src_ncomp, dst_ncomp, pltst,
                      src_pad, dst_rem, dst_pad, src_rowst, dst_rowst, bitoff,
                      rmask, gmask, bmask, amask, rshift, gshift, bshift,
                      ashift, rcount, gcount, bcount, acount, px, dst_x, dst_y;
  float               pe_r, pe_g, pe_b, pe_a;

  im   = NULL;
  fres = im_readfile(path);
  
  if (fres.ret != IM_OK) {
    goto err;
  }
  
  /* decode, this process will be optimized after decoding is done */
  im  = calloc(1, sizeof(*im));
  p   = fres.raw;
  
  /*
   Magic number types:
   -------------------
   BM Windows 3.1x, 95, NT, ... etc.
   BA OS/2 struct bitmap array
   CI OS/2 struct color icon
   CP OS/2 const color pointer
   IC OS/2 struct icon
   PT OS/2 pointer
   */

  if (p[0] != 'B' && p[1] != 'M') {
    goto err;
  }

  p      += 2;
  im->fileFormatType = IM_FILEFORMATTYPE_BMP_Windows;

  p      += 4; /* file size: uint32 */
  p      += 4; /* reserved 4 bytes (unused) */

  dataoff = im_get_u32_endian(p, true);  p += 4;
  p_back  = p;

  /* DIP header */
  hsz     = im_get_u32_endian(p, true);  p += 4;
  pltst   = 4;

  if (hsz == 12) { /* BITMAPCOREHEADER, OS21XBITMAPHEADER */
    width  = im_get_u16_endian(p, true);  p += 2;
    height = im_get_u16_endian(p, true);  p += 2;
    pltst  = 3;
  }
  /* OS/2 2.x: any multiple of 4 between 16 and 64, inclusive, or 42 or 46 -> OS22x */
  /* BITMAPINFOHEADER2, OS22XBITMAPHEADER */
  else if ((hsz >= 16) && (hsz <= 64) && (!(hsz & 3) || (hsz == 42) || (hsz == 46))) {
    width  = im_get_u32_endian(p, true);  p += 4;
    height = im_get_u32_endian(p, true);  p += 4;
  } else { /* BITMAPINFOHEADER <= ... <= BITMAPV5HEADER */
    width  = im_get_i32_endian(p, true);  p += 4;
    height = im_get_i32_endian(p, true);  p += 4;
  }

  /* ignore planes field: uint16 */
  p    += 2;
  bpp   = im_get_u16_endian(p, true);  p += 2;

  compr = im_get_u32_endian(p, true);  p += 4;
  /* imsz  = im_get_u32_endian(p, true); */ p += 4;
  im->hres  = im_get_i32_endian(p, true);  p += 4;
  im->vres  = im_get_i32_endian(p, true);  p += 4;

  p    += 4; /* color used: uint32 */
  p    += 4; /* color important: uint32 */
  bfi   = p; /* bitfield maks */

re_comp:
  if      (bpp == 1)                                   { src_ncomp = 1; dst_ncomp = 1; }
  else if (bpp > 1 && bpp <= 8)                        { src_ncomp = 1; dst_ncomp = 3; }
  else if (bpp == 24)                                  { src_ncomp = 3; dst_ncomp = 3; }
  else if (bpp == 16) {


    if (compr == IM_BMP_COMPR_BITFIELDS || compr == 0) { src_ncomp = 1; dst_ncomp = 3; }
    else if (compr == IM_BMP_COMPR_ALPHABITFIELDS)     { src_ncomp = 1; dst_ncomp = 4; }
    else                                               { goto err;                     }

  } else if (bpp == 32) {

    if (compr == IM_BMP_COMPR_BITFIELDS || compr == 0) { src_ncomp = 1; dst_ncomp = 3; }
    else if (compr == IM_BMP_COMPR_ALPHABITFIELDS)     { src_ncomp = 1; dst_ncomp = 4; }
    else                                               { goto err;                     }

  } else {
    goto err;
  }
  
  plt = p_back + hsz;
  
  if      (compr == IM_BMP_COMPR_BITFIELDS)      { plt += 12; }
  else if (compr == IM_BMP_COMPR_ALPHABITFIELDS) { plt += 16; }
  
  if (compr != IM_BMP_COMPR_RLE4
      && compr != IM_BMP_COMPR_RLE8
      && compr != IM_BMP_COMPR_RLE24) {
    im->row_pad_last = 4;
  }

  /* minimum bytes to contsruct one row */
  min_bytes = ceilf(width * src_ncomp * im_minf((float)bpp / 8.0f, 8));

  /* pad to power of 4 */
  src_pad   = 4 - min_bytes & 3;

  /* padded one row in bytes */
  src_rowst = min_bytes + src_pad;

  dst_rem   = im->row_pad_last == 0 ? 0 : width * dst_ncomp % im->row_pad_last;
  dst_pad   = dst_rem == 0 ? 0 : im->row_pad_last - dst_rem;
  dst_rowst = dst_pad + width * dst_ncomp;

  imlen                = (width * dst_ncomp + dst_pad) * height;
  im->format           = IM_FORMAT_RGB;
  im->len              = imlen;
  im->width            = width;
  im->height           = height;
  im->row_pad_last     = dst_pad;

  if      (dst_ncomp == 3) { im->format = IM_FORMAT_RGB;                                       }
  else if (dst_ncomp == 4) { im->format = IM_FORMAT_RGBA;       im->alphaInfo = IM_ALPHA_LAST; }
  else if (dst_ncomp == 1) { im->format = IM_FORMAT_MONOCHROME;                                }

  /* TODO: -
   DEST Image configuration but may change in the future by options,
   e.g bit per pixel
   */

  im->bytesPerPixel    = dst_ncomp;
  im->bitsPerComponent = 8;
  im->bitsPerPixel     = dst_ncomp * 8;
  p                    = (char *)fres.raw + dataoff;
  p_end                = (char *)fres.raw + fres.size - 1;

  /* short path */
  if (dst_pad == 0 && src_pad == 0 && (bpp == 24 || bpp == 32)) {
    im->data.data = p;
    goto ok;
  }

  rmask  = 0x7c00;
  bmask  = 0x1f;
  gmask  = 0x3e0;
  rshift = 10;
  gshift = 5;
  bshift = 0;
  ashift = 0;
  amask  = 0;

  acount = 0;
  rcount = gcount = bcount = 5;
  pe_r   = pe_g = pe_b = pe_a = 1.0f;

  if (bpp == 16 || bpp == 32) {
    if (compr == 0) {
      if (bpp == 32) {
        rshift = 20; gshift = 10; bshift = 0;
        rcount = gcount = bcount = 10;
      }
    } else {
      rmask  = im_get_u32_endian(bfi,     true);
      gmask  = im_get_u32_endian(bfi + 4, true);
      bmask  = im_get_u32_endian(bfi + 8, true);
      amask  = im_get_u32_endian(bfi + 12, true);
      
      if (amask != 0 && compr != IM_BMP_COMPR_ALPHABITFIELDS) {
        /* include alpha? */
        compr = IM_BMP_COMPR_ALPHABITFIELDS;
        goto re_comp;
      }

      rshift = im_bitw_ctz(rmask);
      gshift = im_bitw_ctz(gmask);
      bshift = im_bitw_ctz(bmask);
      ashift = im_bitw_ctz(amask);

      rcount = 32 - im_bitw_clz(rmask) - rshift;
      gcount = 32 - im_bitw_clz(gmask) - gshift;
      bcount = 32 - im_bitw_clz(bmask) - bshift;
      acount = 32 - im_bitw_clz(amask) - ashift;
    }
    
    pe_r = (float)255.0f/(pow(2, rcount) - 1);
    pe_g = (float)255.0f/(pow(2, gcount) - 1);
    pe_b = (float)255.0f/(pow(2, bcount) - 1);
    pe_a = (float)255.0f/(pow(2, acount) - 1);
  }

  im->data.data = im_init_data(im, imlen);
  pd            = im->data.data;

  if (bpp == 24) {
    for (i = 0; i < height; i++) {
      im_memcpy((char *)pd, p, dst_rowst);
      p  += dst_rowst;
      pd += dst_rowst;
    }
  } else if (bpp == 16) {
    if (compr != IM_BMP_COMPR_ALPHABITFIELDS) {
      for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++) {
          px = im_get_u16_endian(p + i * src_rowst + j * 2, true);
          
          pd[i * dst_rowst + j * dst_ncomp + 2] = ((px & bmask) >> bshift) * pe_b;
          pd[i * dst_rowst + j * dst_ncomp + 1] = ((px & gmask) >> gshift) * pe_g;
          pd[i * dst_rowst + j * dst_ncomp + 0] = ((px & rmask) >> rshift) * pe_r;
        }
      }
    } else {
      for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++) {
          px = im_get_u16_endian(p + i * src_rowst + j * 2, true);
          
          pd[i * dst_rowst + j * dst_ncomp + 2] = ((px & bmask) >> bshift) * pe_b;
          pd[i * dst_rowst + j * dst_ncomp + 1] = ((px & gmask) >> gshift) * pe_g;
          pd[i * dst_rowst + j * dst_ncomp + 0] = ((px & rmask) >> rshift) * pe_r;
          pd[i * dst_rowst + j * dst_ncomp + 3] = ((px & amask) >> ashift) * pe_a;
        }
      }
    }
  } else if (bpp == 32) {
    if (compr != IM_BMP_COMPR_ALPHABITFIELDS) {
      for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++) {
          px = im_get_u32_endian(p + i * src_rowst + j * 4, true);
          
          pd[i * dst_rowst + j * dst_ncomp + 2] = ((px & bmask) >> bshift) * pe_b;
          pd[i * dst_rowst + j * dst_ncomp + 1] = ((px & gmask) >> gshift) * pe_g;
          pd[i * dst_rowst + j * dst_ncomp + 0] = ((px & rmask) >> rshift) * pe_r;
        }
      }
    } else {
      for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++) {
          px = im_get_u32_endian(p + i * src_rowst + j * 4, true);
          
          pd[i * dst_rowst + j * dst_ncomp + 2] = ((px & bmask) >> bshift) * pe_b;
          pd[i * dst_rowst + j * dst_ncomp + 1] = ((px & gmask) >> gshift) * pe_g;
          pd[i * dst_rowst + j * dst_ncomp + 0] = ((px & rmask) >> rshift) * pe_r;
          pd[i * dst_rowst + j * dst_ncomp + 3] = ((px & amask) >> ashift) * pe_a;
        }
      }
    }
  } else if (bpp == 8) {
    for (i = 0; i < height; i++) {
      for (j = 0; j < width; j++) {
        idx = ((uint8_t)p[i * src_rowst + j]) * pltst;
 
        pd[i * dst_rowst + j * dst_ncomp + 0] = plt[idx + 0];
        pd[i * dst_rowst + j * dst_ncomp + 1] = plt[idx + 1];
        pd[i * dst_rowst + j * dst_ncomp + 2] = plt[idx + 2];
      }
    }
  } else if (bpp == 1) {
    c      = *p;
    bitoff = 0;

    for (i = 0; i < height; i++) {
      for (j = 0; j < width; j++) {
        pd[i * dst_rowst + j * dst_ncomp] = (c >> 7) * 255;
        c <<= 1;

        if (++bitoff > 7) {
          bitoff = 0;
          if (src_pad > 0 && j == width - 1) {
            p += src_pad;
          }
          c = *++p;
        }
      }

      if (bitoff != 0) {
        c      = *++p;
        bitoff = 0;
      }
    }
  } else if (bpp <= 4 && compr == IM_BMP_COMPR_RLE4) {
    ImByte cnt, code, idx_a, idx_b, dx, dy;

    dst_x = dst_y = 0;
    p     = (char *)fres.raw + dataoff;

    while (p < p_end) {
      cnt  = *p++;
      code = *p++;

      if (!cnt) {
        /* TODO: . */
        switch (code) {
          case 0x00: {
            while(dst_y) {
              pd = im_setpx3_8(pd, plt);
              if (++dst_y >= width) {
                dst_x++;
                dst_y = 0;
                break;
              }
            }
            break;
          }
          case 0x01: {
            dst_rem = im_max_i32(width * height - (dst_x * width + dst_y), 0);
            for (i = 0; i < dst_rem; i++) {
              pd = im_setpx3_8(pd, plt);
            }
            goto ok;
          }
          case 0x02: {
            dy = *p++;
            dx = *p++;

            dst_rem = width * dx + dy;

            for (i = 0; i < dst_rem; i++) {
              pd = im_setpx3_8(pd, plt);
              
              if (++dst_y >= width) {
                dst_x++;
                dst_y = 0;
              }
            }
            break;
          }
          default: {
            for (i = 0, c = 0; i < code; i++) {
              if (!(i & 1)) { c = *p++; }
              
              idx = (c >> 4) * pltst;
              pd  = im_setpx3_8(pd, plt + idx);
              
              if (++dst_y < width) {
                c <<= 4;
              } else {
                dst_x++;
                dst_y = 0;
                break;
              }
            }

            p += (code - i + 1) / 2 + ((code - i + 1) / 2) & 1;
            p += (((code + 1) / 2) & 1);
            break;
          }
        }
      } else {
        /* Encoded mode */
        idx_a = code >> 4;
        idx_b = code & 0xf;

        for (int32_t k = 0; k < cnt; k++) {
          idx = ((uint32_t)((k & 1) ? idx_b : idx_a)) * pltst;
          pd  = im_setpx3_8(pd, plt + idx);

          if (++dst_y >= width) {
            dst_x++;
            dst_y = 0;
            break;
          }
        }
      }
    }
  } else if (bpp <= 8 && compr == IM_BMP_COMPR_RLE8) {
    ImByte cnt, code, dx, dy;

    dst_x = dst_y = 0;
    p     = (char *)fres.raw + dataoff;

    while (p < p_end) {
      cnt  = *p++;
      code = *p++;
      
      if (!cnt) {
        /* TODO: . */
        switch (code) {
          case 0x00: {
            while(dst_y) {
              pd = im_setpx3_8(pd, plt);
              if (++dst_y >= width) {
                dst_x++;
                dst_y = 0;
                break;
              }
            }
            break;
          }
          case 0x01: {
            dst_rem = im_max_i32(width * height - (dst_x * width + dst_y), 0);
            for (i = 0; i < dst_rem; i++) {
              pd = im_setpx3_8(pd, plt);
            }
            goto ok;
          }
          case 0x02: {
            dy = *p++;
            dx = *p++;
            
            dst_rem = width * dx + dy;
            
            for (i = 0; i < dst_rem; i++) {
              pd = im_setpx3_8(pd, plt);
              
              if (++dst_y >= width) {
                dst_x++;
                dst_y = 0;
              }
            }
            break;
          }
          default: {
            /* Absolute mode */
            for (i = 0; i < code; i++) {
              idx = ((ImByte)*p++) * pltst;
              pd  = im_setpx3_8(pd, plt + idx);
              
              if (++dst_y >= width) {
                dst_x++;
                dst_y = 0;
                break;
              }
            }
            
            p += (code - i) + (code - i + 1) & 1;
            p += code & 1;
            break;
          }
        }
      } else {
        /* Encoded mode */
        for (i = 0; i < cnt; i++) {
          idx = code * pltst;
          pd  = im_setpx3_8(pd, plt + idx);
          
          if (++dst_y >= width) {
            dst_x++;
            dst_y = 0;
            break;
          }
        }
      }
    }
  } else if (bpp < 8) {
    c      = *p;
    bitoff = 0;
    
    for (i = 0; i < height; i++) {
      for (j = 0; j < width; j++) {
        idx = (c >> (8 - bpp)) * pltst;
        c <<= bpp;

        pd[i * dst_rowst + j * dst_ncomp + 0] = plt[idx + 0];
        pd[i * dst_rowst + j * dst_ncomp + 1] = plt[idx + 1];
        pd[i * dst_rowst + j * dst_ncomp + 2] = plt[idx + 2];
        
        if ((bitoff += bpp) > 7) {
          bitoff = 0;
          if (src_pad > 0 && j == width - 1) {
            p += src_pad;
          }
          c = *++p;
        }
      }

      if (bitoff != 0) {
        c      = *++p;
        bitoff = 0;
      }
    }
  }

ok:
  *dest = im;
  im->file = fres;
  
//  if (fres.mmap) {
//    im_unmap(fres.raw, fres.size);
//  }

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
