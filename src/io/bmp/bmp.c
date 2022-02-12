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
  IM_BMP_COMPR_CMYKRLE4       = 13  /* RLE-4                          | only Windows Metafile CMYK */
} im_bmp_compression_method_t;

IM_HIDE
ImResult
bmp_dec(ImImage ** __restrict dest, const char * __restrict path) {
  ImImage            *im;
  char               *p, *p_back, *end, *pd, *palette;
  size_t              imlen;
  ImFileResult        fres;
  ImByte              bpp;
  uint32_t            dataoff, hsz, imsz, width, height, hres, vres, compr,
                      i, j, idx, src_ncomp, dst_ncomp, pltst,
                      src_pad, dst_rem, dst_pad, src_rowst, dst_rowst;
  bool                hasPalette;

  im   = NULL;
  fres = im_readfile(path);
  
  if (fres.ret != IM_OK) {
    goto err;
  }
  
  /* decode, this process will be optimized after decoding is done */
  im  = calloc(1, sizeof(*im));
  p   = fres.raw;
  end = p + fres.size;
  
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

  p += 2;
  im->fileFormatType = IM_FILEFORMATTYPE_BMP_Windows;
  im->row_pad_last   = 4;

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
  } else if (hsz == 16 || hsz == 64) { /* BITMAPINFOHEADER2, OS22XBITMAPHEADER */
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
  imsz  = im_get_u32_endian(p, true);  p += 4;
  hres  = im_get_i32_endian(p, true);  p += 4;
  vres  = im_get_i32_endian(p, true);  p += 4;

  p    += 4; /* color used: uint32 */
  p    += 4; /* color important: uint32 */

  palette    = p = p_back + hsz;
  hasPalette = bpp < 8;
  dst_ncomp  = 3;

  if      (bpp <= 8)  { src_ncomp = 1; }
  else if (bpp == 24) { src_ncomp = 3; }
  else if (bpp == 32) { src_ncomp = 4; }
  else                { goto err;      }

  if      (compr == IM_BMP_COMPR_BITFIELDS)      { palette += 12; }
  else if (compr == IM_BMP_COMPR_ALPHABITFIELDS) { palette += 16; }

  src_rowst  = width * src_ncomp;
  src_pad    = src_rowst & 3;
  src_rowst += src_pad;

  dst_rem   = width * dst_ncomp % im->row_pad_last;
  dst_pad   = dst_rem == 0 ? 0 : im->row_pad_last - dst_rem;
  dst_rowst = dst_pad + width * dst_ncomp;

  imlen                = (width * dst_ncomp + dst_pad) * height;
  im->format           = IM_FORMAT_RGB;
  im->len              = imlen;
  im->width            = width;
  im->height           = height;
  im->row_pad_last     = dst_pad;

  if      (dst_ncomp == 3) { im->format = IM_FORMAT_RGB;  }
  else if (dst_ncomp == 4) { im->format = IM_FORMAT_RGBA; }

  /* TODO: -
   DEST Image configuration but may change in the future by options,
   e.g bit per pixel
   */

  im->bytesPerPixel    = dst_ncomp;
  im->bitsPerComponent = 8;
  im->bitsPerPixel     = dst_ncomp * 8;
  p                    = (char *)fres.raw + dataoff;
  
  /* short path */
  if (dst_pad == 0 && src_pad == 0 && (bpp == 24 || bpp == 32)) {
    im->data.data = p;
    goto ok;
  }

  im->data.data        = im_init_data(im, imlen);
  pd                   = im->data.data;
  palette              = p_back + hsz;

  if (bpp == 8) {
    for (i = 0; i < height; i++) {
      for (j = 0; j < width; j++) {
        idx = ((uint8_t)p[i * src_rowst + j]) * pltst;
 
        pd[i * dst_rowst + j * dst_ncomp + 0] = palette[idx + 0];
        pd[i * dst_rowst + j * dst_ncomp + 1] = palette[idx + 1];
        pd[i * dst_rowst + j * dst_ncomp + 2] = palette[idx + 2];
      }
    }
  } else if (bpp == 24) {
    for (i = 0; i < height; i++) {
      im_memcpy(pd, p, dst_rowst);
      p  += dst_rowst;
      pd += dst_rowst;
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
