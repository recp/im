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
  uint32_t width;
  uint32_t height;
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
