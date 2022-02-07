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

/*
 typedef struct tagBITMAPINFOHEADER {
   DWORD biSize;
   LONG  biWidth;
   LONG  biHeight;
   WORD  biPlanes;
   WORD  biBitCount;
   DWORD biCompression;
   DWORD biSizeImage;
   LONG  biXPelsPerMeter;
   LONG  biYPelsPerMeter;
   DWORD biClrUsed;
   DWORD biClrImportant;
 } BITMAPINFOHEADER, *PBITMAPINFOHEADER;
 
 typedef struct tagCIEXYZ {
   FXPT2DOT30 ciexyzX;
   FXPT2DOT30 ciexyzY;
   FXPT2DOT30 ciexyzZ;
 } CIEXYZ;

 typedef struct tagICEXYZTRIPLE {
   CIEXYZ ciexyzRed;
   CIEXYZ ciexyzGreen;
   CIEXYZ ciexyzBlue;
 } CIEXYZTRIPLE;
 
 typedef struct {
   DWORD        bV5Size;
   LONG         bV5Width;
   LONG         bV5Height;
   WORD         bV5Planes;
   WORD         bV5BitCount;
   DWORD        bV5Compression;
   DWORD        bV5SizeImage;
   LONG         bV5XPelsPerMeter;
   LONG         bV5YPelsPerMeter;
   DWORD        bV5ClrUsed;
   DWORD        bV5ClrImportant;

   DWORD        bV5RedMask;
   DWORD        bV5GreenMask;
   DWORD        bV5BlueMask;
   DWORD        bV5AlphaMask;
   DWORD        bV5CSType;
   CIEXYZTRIPLE bV5Endpoints;
   DWORD        bV5GammaRed;
   DWORD        bV5GammaGreen;
   DWORD        bV5GammaBlue;
   DWORD        bV5Intent;
   DWORD        bV5ProfileData;
   DWORD        bV5ProfileSize;
   DWORD        bV5Reserved;
 } BITMAPV5HEADER, *LPBITMAPV5HEADER, *PBITMAPV5HEADER;
 */
typedef struct im_bmp_dip_header_t {
  uint32_t size;
  uint32_t width;
  uint32_t height;
  uint16_t nColorPlanes;         /* the number of color planes (must be 1) */
  uint16_t nBitsPerPixel;        /* the number of bits per pixel, which is the color depth of the image. Typical values are 1, 4, 8, 16, 24 and 32 */
  uint32_t compressionMethod;
  
  uint32_t imageSize;
  int32_t  xPixelsPerMeter;      /* horizontal resolution of the image. (pixel per metre, signed integer) */
  int32_t  yPixelsPerMeter;      /* vertical resolution of the image. (pixel per metre, signed integer)   */
  uint32_t nColorsInPalette;     /* the number of colors in the color palette, or 0 to default to 2n */
  uint32_t nImportantColors;     /* the number of important colors used, or 0 when every color is important; generally ignored */

  uint32_t        bV5Size;
  int32_t         bV5Width;
  int32_t         bV5Height;
  uint16_t        bV5Planes;
  uint16_t        bV5BitCount;
  uint32_t        bV5Compression;
  uint32_t        bV5SizeImage;
  int32_t         bV5XPelsPerMeter;
  int32_t         bV5YPelsPerMeter;
  uint32_t        bV5ClrUsed;
  uint32_t        bV5ClrImportant;

  uint32_t        bV5RedMask;
  uint32_t        bV5GreenMask;
  uint32_t        bV5BlueMask;
  uint32_t        bV5AlphaMask;
  uint32_t        bV5CSType;
//  CIEXYZTRIPLE    bV5Endpoints;
  uint32_t        bV5GammaRed;
  uint32_t        bV5GammaGreen;
  uint32_t        bV5GammaBlue;
  uint32_t        bV5Intent;
  uint32_t        bV5ProfileData;
  uint32_t        bV5ProfileSize;
  uint32_t        bV5Reserved;
  
} im_bmp_dip_header_t;

IM_HIDE
ImResult
bmp_dec(ImImage ** __restrict dest, const char * __restrict path) {
  ImImage            *im;
  char               *p, *end;
  ImFileResult        fres;
  im_bmp_dip_header_t dip_header;
  uint32_t            fileSizes, dataOffset;
  
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
  
  memset(&dip_header, 0, sizeof(dip_header));
  
  fileSizes         = im_get_u32_endian(p, true);  p += 4;
  /* reserved 4 bytes (unused) */
  p += 4;
  dataOffset        = im_get_u32_endian(p, true);  p += 4;
  
  /* DIP header */
  dip_header.size   = im_get_u32_endian(p, true);  p += 4;
  dip_header.width  = im_get_u32_endian(p, true);  p += 4;
  dip_header.height = im_get_u32_endian(p, true);  p += 4;

  *dest = im;
  
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
