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

#ifndef im_h
#define im_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

typedef unsigned char ImByte;

typedef enum ImFileType {
  IM_FILE_TYPE_AUTO = 0,
  IM_FILE_TYPE_JPEG,
  IM_FILE_TYPE_PNG,

  IM_FILE_TYPE_PBM,
  IM_FILE_TYPE_PGM,
  IM_FILE_TYPE_PPM
} ImFileType;

typedef enum ImFormat {
  IM_FORMAT_NONE       = 0,
  IM_FORMAT_BLACKWHITE = 1,
  IM_FORMAT_GRAY       = 2,
  IM_FORMAT_RGB        = 3,
  IM_FORMAT_RGBA       = 4,
  IM_FORMAT_CMYK       = 5,
  IM_FORMAT_YCbCr      = 6,
  
  IM_FORMAT_MONOCHROME = IM_FORMAT_GRAY

  /* TODO: */
} ImFormat;

/* same as CGImageAlphaInfo */
typedef enum ImAlphaInfo {
  IM_ALPHA_NONE,               /* For example, RGB. */
  IM_ALPHA_PREMUL_LAST,        /* For example, premultiplied RGBA */
  IM_ALPHA_PREMUL_FIRST,       /* For example, premultiplied ARGB */
  IM_ALPHA_LAST,               /* For example, non-premultiplied RGBA */
  IM_ALPHA_FIRST,              /* For example, non-premultiplied ARGB */
  IM_ALPHA_NONE_SKIP_LAST,     /* For example, RBGX. */
  IM_ALPHA_NONE_SKIP_FIRST,    /* For example, XRGB. */
  IM_ALPHA_ONLY                /* No color data, alpha data only */
} ImAlphaInfo;

/* same as CGImageAlphaInfo */
typedef enum ImFileFormatType {
  IM_FILEFORMATTYPE_UNKNOWN,
  IM_FILEFORMATTYPE_JPEG,
  IM_FILEFORMATTYPE_PNG,

  IM_FILEFORMATTYPE_PBM_ASCII,
  IM_FILEFORMATTYPE_PBM_BIN,
  IM_FILEFORMATTYPE_PGM_ASCII,
  IM_FILEFORMATTYPE_PGM_BIN,
  IM_FILEFORMATTYPE_PPM_ASCII,
  IM_FILEFORMATTYPE_PPM_BIN,
  IM_FILEFORMATTYPE_PFM,
  IM_FILEFORMATTYPE_PFM_ALPHA,
  IM_FILEFORMATTYPE_PAM_BIN,

  IM_FILEFORMATTYPE_BMP_Windows,
  IM_FILEFORMATTYPE_BMP_OS2_StructBitmapArray,
  IM_FILEFORMATTYPE_BMP_OS2_StructColorIcon,
  IM_FILEFORMATTYPE_BMP_OS2_ConstColorPointer,
  IM_FILEFORMATTYPE_BMP_OS2_Icon,
  IM_FILEFORMATTYPE_BMP_OS2_Pointer
} ImFileFormatType;

typedef struct ImImageData {
  void *data;
  void *udata;
} ImImageData;

typedef struct ImImage {
  ImImageData      data;
  size_t           len;
  uint32_t         width;
  uint32_t         height;
  uint32_t         bytesPerPixel;
  uint32_t         bitsPerPixel;
  uint32_t         bitsPerComponent;
  ImFormat         format;
  ImAlphaInfo      alphaInfo;
  ImFileFormatType fileFormatType;
} ImImage;

IM_EXPORT
void*
im_init_data(ImImage * __restrict im, size_t size);

IM_EXPORT
ImResult
im_load(ImImage ** __restrict dest, const char * __restrict url, ...);

IM_EXPORT
ImImage*
im_load_hex(const char * __restrict hexdata);

IM_EXPORT
ImImage*
im_load_base64(const char * __restrict base64data);

#ifdef __cplusplus
}
#endif
#endif /* im_h */
