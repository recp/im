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
im_YCbCrToRGB(ImByte * __restrict src, size_t npixels) {
  ImByte *p;
  size_t  i;
  float   Y, Cb, Cr;
  int     R, G, B;
  
  p = src;
  
  npixels /= 3;

  for (i = 0; i < npixels; i++) {
    Y    = p[0];
    Cb   = p[1];
    Cr   = p[2];

    R    = floorf(Y + 1.402 * (1.0 * Cr - 128.0));
    G    = floorf(Y - 0.344136 * (1.0 * Cb - 128.0) - 0.714136 * (1.0 * Cr - 128.0));
    B    = floorf(Y + 1.772 * (1.0 * Cb - 128.0));

    p[0] = max(0, min(R, 255));
    p[1] = max(0, min(G, 255));
    p[2] = max(0, min(B, 255));

    p += 3;
  }
}
