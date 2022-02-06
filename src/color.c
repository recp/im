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

#include "common.h"
#include "color.h"
#include <math.h>

IM_EXPORT
void
im_YCbCrToRGB(ImByte * __restrict src, uint32_t width, uint32_t height) {
  ImByte  *p;
  size_t   i;
  float    Y, Cb, Cr;
  int      R, G, B;
  uint32_t npixels;
  
  p       = src;
  npixels = width * height;

  for (i = 0; i < npixels; i++) {
    Y    = p[0];
    Cb   = p[1];
    Cr   = p[2];

    R    = Y + 1.402 * (Cr - 128.0);
    G    = Y - 0.344136 * (Cb - 128.0) - 0.714136 * (Cr - 128.0);
    B    = Y + 1.772 * (Cb - 128.0);

    p[0] = im_clamp_i32(R, 0, 255);
    p[1] = im_clamp_i32(G, 0, 255);
    p[2] = im_clamp_i32(B, 0, 255);

    p += 3;
  }
}
