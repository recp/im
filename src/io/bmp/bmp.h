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

#ifndef sc_bmp_h
#define sc_bmp_h

#include "../common.h"

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

IM_HIDE
ImResult
bmp_dec(ImImage         ** __restrict dest,
        const char       * __restrict path,
        im_open_config_t * __restrict open_config);

#endif /* sc_bmp_h */
