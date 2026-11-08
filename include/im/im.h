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
#include "options.h"

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

  /* RGB layout */
  IM_FORMAT_RGB        = 3,
  IM_FORMAT_RGB0       = 4,
  IM_FORMAT_RGBA       = 5,
  IM_FORMAT_ARGB       = 6,
  
  /* BGR layout */
  IM_FORMAT_BGR        = 7,
  IM_FORMAT_BGR0       = 8,
  IM_FORMAT_BGRA       = 9,
  IM_FORMAT_ABGR       = 10,
  
  IM_FORMAT_CMYK       = 11,
  IM_FORMAT_YCbCr      = 12,
  IM_FORMAT_GRAY_ALPHA = 13,
  
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

typedef enum ImCompressionType {
  IM_COMPRESSION_NONE       = 0
} ImCompressionType;

typedef enum ImOrientationType {
  IM_ORIENTATION_UP    = 0 << 0,
  IM_ORIENTATION_DOWN  = 1 << 1,
  IM_ORIENTATION_LEFT  = 1 << 2,
  IM_ORIENTATION_RIGHT = 1 << 3
} ImOrientationType;

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
  IM_FILEFORMATTYPE_BMP_OS2_Pointer,

  IM_FILEFORMATTYPE_TGA,
  IM_FILEFORMATTYPE_QOI,
  
  IM_FILEFORMATTYPE_HEIC,
  IM_FILEFORMATTYPE_JXL,
  IM_FILEFORMATTYPE_JP2,
  IM_FILEFORMATTYPE_J2K,  /* a bare JPEG 2000 codestream */
  IM_FILEFORMATTYPE_CCITT, /* raw CCITT fax: Group 3 or 4 (IM_OPTION_CCITT) */
  IM_FILEFORMATTYPE_JBIG2  /* JBIG2: a file, or a stream's embedded segments */
} ImFileFormatType;

typedef enum ImColorSpace {
  IM_COLORSPACE_UNKNOWN = 0,
  IM_COLORSPACE_sRGB,
  IM_COLORSPACE_LINEAR,
  IM_COLORSPACE_GRAY,
  IM_COLORSPACE_CMYK,
  IM_COLORSPACE_P3
} ImColorSpace;

typedef struct ImFileResult {
  void    *raw;
  size_t   size;
  ImResult ret;
  bool     mmap;
  bool     mustfree;
} ImFileResult;

typedef struct ImImageData {
  void *data;
  
  /* TODO: close handle on win32 if it is opened */
  void *udata;
  void *reserved0;
} ImImageData;

typedef enum ImOpenIntent {
  IM_OPEN_INTENT_READONLY,
  IM_OPEN_INTENT_READONLY_SIZE,
  IM_OPEN_INTENT_READONLY_HEADER,
  IM_OPEN_INTENT_READWRITE
} ImOpenIntent;

typedef struct im_pal_t {
  ImByte   *pal;
  uint32_t  len;
  uint32_t  count;
  uint32_t  white;
} im_pal_t;

typedef struct ImTransparency {
  union {
    struct {
      uint16_t gray;    /* For grayscale */
    } gray;
    struct {
      uint16_t red;     /* For RGB */
      uint16_t green;
      uint16_t blue;
    } rgb;
    struct {
      uint8_t* alpha;   /* For palette */
      size_t   count;
    } pal;
  } value;
} ImTransparency;

typedef struct ImBackground {
  union {
    struct { uint16_t gray;             } gray;
    struct { uint16_t red, green, blue; } rgb;
    struct { uint8_t  index;            } palette;
  } value;
} ImBackground;

typedef struct ImChromaticity {
  double whiteX, whiteY;
  double redX,   redY;
  double greenX, greenY;
  double blueX,  blueY;
} ImChromaticity;

typedef struct ImPhysicalDim {
  uint32_t pixelsPerUnitX;
  uint32_t pixelsPerUnitY;
  uint8_t  unit; /* 0: unknown, 1: meter */
} ImPhysicalDim;

typedef struct ImTimeStamp {
  uint16_t year;
  uint8_t  month;
  uint8_t  day;
  uint8_t  hour;
  uint8_t  minute;
  uint8_t  second;
} ImTimeStamp;

typedef struct ImImage {
  ImFileResult      file;
  ImImageData       data;
  size_t            len;
  uint32_t          width;
  uint32_t          height;
  uint32_t          bytesPerPixel;
  uint32_t          bitsPerPixel;
  uint32_t          bitsPerComponent;
  uint32_t          componentsPerPixel;
  uint32_t          hres;
  uint32_t          vres;
  ImFormat          format; /* Pixel layout (RGB, RGBA, etc.) */
  ImOrientationType ori;
  ImAlphaInfo       alphaInfo;
  ImFileFormatType  fileFormatType;
  ImByteOrder       byteOrder;
  ImOpenIntent      openIntent;
  uint32_t          row_pad_last;

  ImColorSpace      colorSpace;

  /* Monochrome color table (between 0-255),
     Default: BLACK and WHITE
   */
  ImByte            monochrome_colors[2];
  im_pal_t         *pal;

  /* Some PNG ancillary chunks but can be used with other formats too  */
  /* optional transparency information */
  ImTransparency   *transparency;

  /* ancillary chunk info */
  ImBackground     *background;
  double            gamma;
  ImChromaticity   *chrm;
  uint8_t           srgbIntent; /* 0: perceptual, 1: relative, 2: saturation, 3: absolute */
  uint8_t          *iccProfile;
  size_t            iccProfileSize;
  ImPhysicalDim    *physicalDim;
  ImTimeStamp      *timeStamp;

  /* the opacity apart (IM_OPTION_OPACITY's IM_OPACITY_APART): a sample a
     pixel, as deep as the colours', `width` a row; malloc'd, NULL none */
  void             *alpha;
} ImImage;

IM_EXPORT
void*
im_init_data(ImImage * __restrict im, uint32_t size);

IM_EXPORT
ImResult
im_load(ImImage         ** __restrict dest,
        const char       * __restrict url,
        im_option_base_t *            options[],
        ImOpenIntent                  openIntent);

/* how an image's coded (im_info_mem) */
typedef enum ImCoding {
  IM_CODING_UNKNOWN = 0,
  IM_CODING_BASELINE,     /* JPEG: SOF0 */
  IM_CODING_EXTENDED,     /* SOF1, SOF9 */
  IM_CODING_PROGRESSIVE,  /* SOF2, SOF10 */
  IM_CODING_LOSSLESS,     /* SOF3, SOF11 */
  IM_CODING_HIERARCHICAL, /* DHP's frames: then SOF5–7, SOF13–15 ones */
  IM_CODING_JPEG2000
} ImCoding;

/* what an image's headers say, nothing decoded */
typedef struct ImInfo {
  ImFileFormatType fileFormatType;   /* JPEG, JP2 or J2K */
  ImCoding         coding;
  uint32_t         width, height;    /* a JPEG's height from its DNL where the frame has none */
  uint32_t         components;       /* in the file (JPEG 2000: the codestream's) */
  uint32_t         bitsPerComponent; /* the deepest component's */
  uint32_t         levels;           /* JPEG 2000: resolution levels, the main header's decompositions + 1 */
  bool             arithmetic;       /* JPEG: arithmetic coded */
  bool             opacity;          /* JPEG 2000: a channel of opacity (cdef, or more than the colours) */
  uint32_t         pages;            /* a JBIG2 file's (IM_OPTION_PAGE); 1 the others */
} ImInfo;

IM_EXPORT
ImResult
im_info_mem(ImInfo * __restrict info, const void * __restrict data, size_t len);

/* an image in memory: JPEG, JPEG 2000 (a JP2 file or a codestream).
   `data` is read, not kept: the image owns nothing of it. IM_EBADF where
   it isn't one of those or doesn't decode */
IM_EXPORT
ImResult
im_load_mem(ImImage         ** __restrict dest,
            const void       * __restrict data,
            size_t                        len,
            im_option_base_t *            options[],
            ImOpenIntent                  openIntent);

IM_EXPORT
ImImage*
im_load_hex(const char * __restrict hexdata);

IM_EXPORT
ImImage*
im_load_base64(const char * __restrict base64data);

IM_EXPORT
ImResult
im_free(ImImage * __restrict im);

#ifdef __cplusplus
}
#endif
#endif /* im_h */
