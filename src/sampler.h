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

#ifndef src_sampler_h
#define src_sampler_h

#include "common.h"

IM_INLINE
void
im_upsample8_2x2(int16_t              pix,
                 ImByte  * __restrict dst,
                 uint32_t             stride,
                 uint32_t             width) {
  dst[0]              = pix;
  dst[stride]         = pix;
  dst[width]          = pix;
  dst[width + stride] = pix;
}

IM_INLINE
void
im_upsample8_2x1(int16_t              pix,
                 ImByte  * __restrict dst,
                 uint32_t             stride,
                 uint32_t             width) {
  dst[0]      = pix;
  dst[stride] = pix;
}

IM_INLINE
void
im_upsample8_1x2(int16_t              pix,
                 ImByte  * __restrict dst,
                 uint32_t             stride,
                 uint32_t             width) {
  dst[0]      = pix;
  dst[stride] = pix;
}

IM_INLINE
void
im_upsample8_2x3(int16_t              pix,
                 ImByte  * __restrict dst,
                 uint32_t             stride,
                 uint32_t             width) {
  dst[0]                  = pix;
  dst[stride]             = pix;
  dst[width]              = pix;
  dst[width + stride]     = pix;
  dst[width * 2]          = pix;
  dst[width * 2 + stride] = pix;
}

IM_INLINE
void
im_upsample8_3x2(int16_t              pix,
                 ImByte  * __restrict dst,
                 uint32_t             stride,
                 uint32_t             width) {
  dst[0]                  = pix;
  dst[stride]             = pix;
  dst[stride * 2]         = pix;
  dst[width]              = pix;
  dst[width + stride]     = pix;
  dst[width * 2]          = pix;
}

IM_INLINE
void
im_upsample8_3x3(int16_t              pix,
                 ImByte  * __restrict dst,
                 uint32_t             stride,
                 uint32_t             width) {
  dst[0]                  = pix;
  dst[stride]             = pix;
  dst[stride * 2]         = pix;
  dst[width]              = pix;
  dst[width + stride]     = pix;
  dst[width * 2]          = pix;
  dst[width * 2 + stride] = pix;
}

IM_INLINE
void
im_upsample8_4x4(int16_t              pix,
                 ImByte  * __restrict dst,
                 uint32_t             stride,
                 uint32_t             width) {
  dst[0]                  = pix;
  dst[stride]             = pix;
  dst[stride * 2]         = pix;
  dst[width]              = pix;
  dst[width + stride]     = pix;
  dst[width * 2]          = pix;
  dst[width * 2 + stride] = pix;
}

#endif /* sampler_h */
