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
 
 typedef struct _BITMAPINFOHEADER2 {
   ULONG cbFix;
   ULONG cx;
   ULONG cy;
   USHORT cPlanes;
   USHORT cBitCount;
   ULONG ulCompression;
   ULONG cbImage;
   ULONG cxResolution;
   ULONG cyResolution;
   ULONG cclrUsed;
   ULONG cclrImportant;
   USHORT usUnits;
   USHORT usReserved;
   USHORT usRecording;
   USHORT usRendering;
   ULONG cSize1;
   ULONG cSize2;
   ULONG ulColorEncoding;
   ULONG ulIdentifier;
 } BITMAPINFOHEADER2;
 
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
  uint32_t        compression;
  uint32_t        imageSize;
  int32_t         xResolution;
  int32_t         yResolution;
  uint32_t        clrUsed;
  uint32_t        clrImportant;

  /*
   TODO:
  uint32_t        redMask;
  uint32_t        greenMask;
  uint32_t        blueMask;
  uint32_t        alphaMask;
  uint32_t        cSType;
//  CIEXYZTRIPLE    bV5Endpoints;
  uint32_t        gammaRed;
  uint32_t        gammaGreen;
  uint32_t        gammaBlue;
  uint32_t        intent;
  uint32_t        profileData;
  uint32_t        profileSize;
  uint32_t        reserved;
   */
} im_bmp_dip_header_t;

IM_HIDE
ImResult
bmp_dec(ImImage ** __restrict dest, const char * __restrict path) {
  ImImage            *im;
  char               *p, *p_back, *end, *pd, *palette;
  ImFileResult        fres;
  im_bmp_dip_header_t dip_header;
  ImByte              bpp;
  uint32_t            fileSizes, dataOffset, hsz, width, height, i, j, ncomp, row_pad_last, rowst;
  size_t              imlen;
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

  memset(&dip_header, 0, sizeof(dip_header));
  
  fileSizes         = im_get_u32_endian(p, true);  p += 4;
  /* reserved 4 bytes (unused) */
  p += 4;
  dataOffset        = im_get_u32_endian(p, true);  p += 4;

  p_back = p;

  /* DIP header */
  hsz               = im_get_u32_endian(p, true);  p += 4;

  if (hsz == 12) {
    width  = im_get_u16_endian(p, true);  p += 2;
    height = im_get_u16_endian(p, true);  p += 2;
  } else if (hsz == 64) {
    width  = im_get_u32_endian(p, true);  p += 4;
    height = im_get_u32_endian(p, true);  p += 4;
  } else {
    width  = im_get_i32_endian(p, true);  p += 4;
    height = im_get_i32_endian(p, true);  p += 4;
  }

  /* ignore planes field */
  im_get_u16_endian(p, true);  p += 2;

  bpp         = im_get_u16_endian(p, true);  p += 2;
  dip_header.compression      = im_get_u32_endian(p, true);  p += 4;
  dip_header.imageSize        = im_get_u32_endian(p, true);  p += 4;
  dip_header.xResolution      = im_get_i32_endian(p, true);  p += 4;
  dip_header.yResolution      = im_get_i32_endian(p, true);  p += 4;

  dip_header.clrUsed          = im_get_u32_endian(p, true);  p += 4;
  dip_header.clrImportant     = im_get_u32_endian(p, true);  p += 4;

  palette    = p = p_back + hsz;
  hasPalette = bpp < 8;
  ncomp      = 3;

  if (dip_header.compression == IM_BMP_COMPR_BITFIELDS) {
    palette += 12;
  } else if (dip_header.compression == IM_BMP_COMPR_ALPHABITFIELDS) {
    palette += 16;
  }

  uint32_t rem         = width * ncomp % 4;
  uint32_t row_pad     = 0 == rem ? 0 : 4 - rem;
  
  uint32_t dst_rem     = width * ncomp % im->row_pad_last;
  uint32_t dst_row_pad = 0 == dst_rem ? 0 : im->row_pad_last - dst_rem;

  rowst = dst_row_pad + width * ncomp;

  imlen                = (width * ncomp + dst_row_pad) * height;
  im->data.data        = im_init_data(im, imlen);
  im->format           = IM_FORMAT_RGB;
  im->len              = imlen;
  im->width            = width;
  im->height           = height;
  im->row_pad_last     = dst_row_pad;

  if (ncomp == 3) {
    im->format = IM_FORMAT_RGB;
  } else if (ncomp == 4) {
    im->format = IM_FORMAT_RGBA;
  }

  /* TODO: -
   DEST Image configuration but may change in the future by options,
   e.g bit per pixel
   */

  im->bytesPerPixel    = ncomp;
  im->bitsPerComponent = 8;
  im->bitsPerPixel     = ncomp * 8;

  pd                   = im->data.data;

  p                    = (char *)fres.raw + dataOffset;
  palette              = p_back + hsz;

  if (bpp == 8) {
    for (i = 0; i < height; i++) {
      for (j = 0; j < width; j++) {
        uint8_t index = p[i * width + j] * 4;
 
        pd[i * width * 3 + j * 3 + 0] = palette[index + 0];
        pd[i * width * 3 + j * 3 + 1] = palette[index + 1];
        pd[i * width * 3 + j * 3 + 2] = palette[index + 2];
      }
    }
  } else if (bpp == 24) {
    for (i = 0; i < height; i++) {
      im_memcpy(pd, p, rowst);
      p  += rowst;
      pd += rowst;
    }
  }

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
