/*
 * Copyright (c), Recep Aslantas.
 * MIT License (MIT), http://opensource.org/licenses/MIT
 */

#ifndef im_h
#define im_h

#include "common.h"

typedef unsigned char ImByte;

typedef enum ImFormat {
  IM_FORMAT_RGB  = 1,
  IM_FORMAT_RGBA = 2

  /* TODO: */
} ImFormat;

typedef struct ImImage {
  void    *data;
  size_t   len;
  size_t   width;
  size_t   height;
  ImFormat format;
} ImImage;

IM_EXPORT
ImImage*
im_load(const char * __restrict path);

IM_EXPORT
ImImage*
im_load_hex(const char * __restrict hexdata);

IM_EXPORT
ImImage*
im_load_base64(const char * __restrict base64data);

#endif /* im_h */
