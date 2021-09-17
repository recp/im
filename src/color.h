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

#ifndef src_color_h
#define src_color_h

#include "common.h"

IM_EXPORT
void
im_YCbCrToRGB(ImByte * __restrict src, uint32_t width, uint32_t height);

IM_INLINE
void
im_YCbCrToRGB_8x8(ImByte blk[3][64], ImByte * __restrict dest) {
  float Y, Cb, Cr;
  int   R, G, B;

  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      Y  = blk[0][y * 8 + x];
      Cb = blk[1][y * 8 + x];
      Cr = blk[2][y * 8 + x];

      R = Y + 1.402 * (1.0 * Cr - 128.0);
      G = Y - 0.344136 * (1.0 * Cb - 128.0) - 0.714136 * (1.0 * Cr - 128.0);
      B = Y + 1.772 * (1.0 * Cb - 128.0);

      R = max(0, min(R, 255));
      G = max(0, min(G, 255));
      B = max(0, min(B, 255));

      dest[3 * (y * 8 + x) + 0] = R;
      dest[3 * (y * 8 + x) + 1] = G;
      dest[3 * (y * 8 + x) + 2] = B;
    }
  }
}

#endif /* src_color_h */
