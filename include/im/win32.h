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

/* TODO: separate function to return HBITMAP ?*/

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

  bitmap = CreateDIBSection(hdc, &dbmi, DIB_RGB_COLORS, &ppvBits, im->data.udata, 0);
  if (ppvBits == NULL || bitmap == NULL) {
     /* TODO: release memory */
     return NULL;
  }

  if (!im->data.udata) {
    memcpy(ppvBits, im->data.data, im->len);
  }

  hdcImage = CreateCompatibleDC(hdc);
  SelectObject(hdcImage, bitmap);

  /* TODO: provide option to call DeleteObject() by user */

  im->data.reserved0 = hdcImage;

  return hdcImage;
}

IM_INLINE
BOOL
im_win32_draw_fixed(ImImage * __restrict im, HDC destHDC, uint32_t x, uint32_t y) {
  if (!(im->data.data && im->data.reserved0))
    return false;

  return BitBlt(destHDC,
                x,
                y,
                im->width,
                im->height,
                (HDC)im->data.reserved0,
                0,
                0,
                SRCCOPY);
}

IM_INLINE
BOOL
im_win32_draw(ImImage * __restrict im,
              HDC                  destHDC,
              uint32_t             x,
              uint32_t             y,
              uint32_t             width,
              uint32_t             height,
              uint32_t             maxWidth,
              uint32_t             maxHeight,
              bool                 keepAspectRatio,
              bool                 centerIfNeeded) {
  float    _aspect;
  uint32_t _w, _h, _hf, nWidth, nHeight;
  RECT     rcCli;

  if (!(im->data.data && im->data.reserved0))
    return false;
  
  _aspect = (float)im->height / (float)im->width;
  
  if (width > maxWidth) {
    _w = maxWidth;
  } else {
    _w = width;
  }
  
  if (height > maxHeight) {
    _h = maxHeight;
  } else {
    _h = height;
  }
  
  if (keepAspectRatio) {
    _hf = _w * _aspect;

    if (_h < _hf) {
      _hf = _h;
      _w  = _hf / _aspect;
    }

    _h = _hf;
  }
  
  if (centerIfNeeded) {
    GetClientRect(WindowFromDC(destHDC), &rcCli);
    
    nWidth  = rcCli.right  - rcCli.left;
    nHeight = rcCli.bottom - rcCli.top;
    
    x += (nWidth  - _w) * 0.5;
    y += (nHeight - _h) * 0.5;
  }
  
  return StretchBlt(destHDC,
                    x,
                    y,
                    _w,
                    _h,
                    (HDC)im->data.reserved0,
                    0,
                    0,
                    im->width,
                    im->height,
                    SRCCOPY);
}

IM_INLINE
BOOL
im_win32_draw_centered_in(ImImage * __restrict im, HDC destHDC) {
  RECT rcCli;
  
  if (!(im->data.data && im->data.reserved0))
    return false;
  
  GetClientRect(WindowFromDC(destHDC), &rcCli);

  return im_win32_draw(im,
                       destHDC,
                       0,
                       0,
                       im->width,
                       im->height,
                       rcCli.right  - rcCli.left,
                       rcCli.bottom - rcCli.top,
                       true,
                       true);
}

#ifdef __cplusplus
}
#endif
#endif
#endif /* im_cocoa_h */
