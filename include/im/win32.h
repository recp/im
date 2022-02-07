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

#ifndef im_cocoa_h
#define im_cocoa_h
#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "im.h"

/* Exclude rarely - used stuff from Windows headers */
#  define WIN32_LEAN_AND_MEAN 
#  include <SDKDDKVer.h>

/* Windows Header Files : */
#  include <windows.h>

IM_INLINE
HDC
im_win32_bitmap(ImImage* __restrict im, HDC hdc) {
  HDC        hdcImage;
  HBITMAP    bitmap;
  BITMAPINFO dbmi;
  void      *ppvBits = NULL;

  if (im == NULL || hdc == NULL) {
    return NULL;
  }

  memset(&dbmi, 0, sizeof(BITMAPINFO));

  dbmi.bmiHeader.biSize          = sizeof(BITMAPINFOHEADER);
  dbmi.bmiHeader.biWidth         = im->width;
  dbmi.bmiHeader.biHeight        = im->height;
  dbmi.bmiHeader.biPlanes        = 1;
  dbmi.bmiHeader.biBitCount      = im->bitsPerPixel;
  dbmi.bmiHeader.biCompression   = BI_RGB;
  dbmi.bmiHeader.biSizeImage     = 0;
  dbmi.bmiHeader.biXPelsPerMeter = 0;
  dbmi.bmiHeader.biYPelsPerMeter = 0;
  dbmi.bmiHeader.biClrUsed       = 0;
  dbmi.bmiHeader.biClrImportant  = 0;
 
  dbmi.bmiColors->rgbBlue        = 0;
  dbmi.bmiColors->rgbGreen       = 0;
  dbmi.bmiColors->rgbRed         = 0;
  dbmi.bmiColors->rgbReserved    = 0;
 
  /* TODO: use file map? */
  /*
  HANDLE hmap;
  if (!((hmap = CreateFileMapping(INVALID_HANDLE_VALUE, 0, PAGE_READWRITE, 0, 0, 0))))
      return NULL;
  */

  bitmap = CreateDIBSection(hdc, &dbmi, DIB_RGB_COLORS, &ppvBits, NULL, 0);
  if (ppvBits == NULL || bitmap == NULL) {
     /* TODO: release memory */
     return NULL;
  }
  
  memcpy(ppvBits, im->data.data, im->width * im->height * im->bytesPerPixel);
  
  hdcImage = CreateCompatibleDC(hdc);
  SelectObject(hdcImage, bitmap);
  
  return hdcImage;
}

#ifdef __cplusplus
}
#endif
#endif
#endif /* im_cocoa_h */
