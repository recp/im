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

#include "idct.h"
#include <stdio.h>
/*
 References:
 [0]: https://golang.org/src/image/jpeg/idct.go
 [1]: http://standards.iso.org/ittf/PubliclyAvailableStandards/ISO_IEC_13818-4_2004_Conformance_Testing/Video/verifier/mpeg2decode_960109.tar.gz
 */

#define w1    2841 /* 2048*sqrt(2)*cos(1*pi/16) */
#define w2    2676 /* 2048*sqrt(2)*cos(2*pi/16) */
#define w3    2408 /* 2048*sqrt(2)*cos(3*pi/16) */
#define w5    1609 /* 2048*sqrt(2)*cos(5*pi/16) */
#define w6    1108 /* 2048*sqrt(2)*cos(6*pi/16) */
#define w7    565  /* 2048*sqrt(2)*cos(7*pi/16) */
#define w1pw7 3406 /* w1 + w7                   */
#define w1mw7 2276 /* w1 - w7                   */
#define w2pw6 3784 /* w2 + w6                   */
#define w2mw6 1568 /* w2 - w6                   */
#define w3pw5 4017 /* w3 + w5                   */
#define w3mw5 799  /* w3 - w5                   */
#define r2    181  /* 256/sqrt(2)               */

/*
  Comments are borrowed from Go implementation
 */

/*
  idct performs a 2-D Inverse Discrete Cosine Transformation.
  The input coefficients should already have been multiplied by the
  appropriate quantization table. We use fixed-point computation, with the
  number of bits for the fractional component varying over the intermediate
  stages.

  For more on the actual algorithm, see Z. Wang, "Fast algorithms for the
  discrete W transform and for the discrete Fourier transform", IEEE Trans. on
  ASSP, Vol. ASSP- 32, pp. 803-816, Aug. 1984
*/
#include <math.h>

IM_HIDE
void
jpg_idct3(int16_t blk[64]) {
  int16_t data[64];
  
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      float sum = 0.0;
      
      for (int u = 0; u < 8; ++u) {
        for (int v = 0; v < 8; ++v) {
          float Cu = u == 0 ? 1.0 / sqrtf(2.0) : 1.0;
          float Cv = v == 0 ? 1.0 / sqrtf(2.0) : 1.0;
          
          sum += Cu * Cv * blk[u * 8 + v] * cosf((2 * x + 1) * u * M_PI / 16.0) *
          cosf((2 * y + 1) * v * M_PI / 16.0);
        }
      }
      
      data[x * 8 + y] = 0.25 * sum;
    }
  }

  for (int y = 0; y < 64; y++) {
    blk[y] = clampi(roundl(data[y]) + 128, 0, 255);
  }
}

IM_HIDE
void
jpg_idct(int16_t * __restrict blk) {
  int16_t *s;
  int32_t x, y,
          x0, x1, x2, x3, x4, x5, x6, x7, x8,
          y0, y1, y2, y3, y4, y5, y6, y7, y8;

  /* Horizontal 1-D IDCT. */
  for (y = 0; y < 8; y++) {
    s = blk + y * 8;

    /* if all the AC components are zero, then the IDCT is trivial */
    if (!((x1 = (s[4] << 11)) | (x2 = s[6]) | (x3 = s[2]) | (x4 = s[1])
        | (x5 = s[7]) | (x6 = s[5]) | (x7 = s[3]))) {
      s[0] = s[1] = s[2] = s[3] = s[4] = s[5] = s[6] = s[7] = s[0] << 3;
      continue;
    }

    x0  = (s[0] << 11) + 128;

    /* stage 1 */
    x8  = w7 * (x4 + x5);
    x4  = x8 + w1mw7 * x4;
    x5  = x8 - w1pw7 * x5;
    x8  = w3 * (x6 + x7);
    x6  = x8 - w3mw5 * x6;
    x7  = x8 - w3pw5 * x7;

    /* stage 2 */
    x8  = x0 + x1;
    x0 -= x1;
    x1  = w6 * (x3 + x2);
    x2  = x1 - w2pw6 * x2;
    x3  = x1 + w2mw6 * x3;
    x1  = x4 + x6;
    x4 -= x6;
    x6  = x5 + x7;
    x5 -= x7;

    /* stage 3 */
    x7  = x8 + x3;
    x8 -= x3;
    x3  = x0 + x2;
    x0 -= x2;
    x2  = (r2 * (x4 + x5) + 128) >> 8;
    x4  = (r2 * (x4 - x5) + 128) >> 8;

    /* stage 4 */
    s[0] = (x7 + x1) >> 8;
    s[1] = (x3 + x2) >> 8;
    s[2] = (x0 + x4) >> 8;
    s[3] = (x8 + x6) >> 8;
    s[4] = (x8 - x6) >> 8;
    s[5] = (x0 - x4) >> 8;
    s[6] = (x3 - x2) >> 8;
    s[7] = (x7 - x1) >> 8;
  }

  for (x = 0; x < 8; x++) {
    s = blk + x;
    /*
     similar to the horizontal 1-D IDCT case, if all the AC components are zero,
     then the IDCT is trivial.
     however, after performing the horizontal 1-D IDCT, there are typically
     non-zero AC components, so
     we do not bother to check for the all-zero case. */
    y0 = (s[8 * 0] << 8) + 8192;
    y1 = s[8 * 4] << 8;
    y2 = s[8 * 6];
    y3 = s[8 * 2];
    y4 = s[8 * 1];
    y5 = s[8 * 7];
    y6 = s[8 * 5];
    y7 = s[8 * 3];

    /* stage 1 */
    y8 = w7 * (y4 + y5) + 4;
    y4 = (y8 + w1mw7 * y4) >> 3;
    y5 = (y8 - w1pw7 * y5) >> 3;
    y8 = w3 * (y6 + y7) + 4;
    y6 = (y8 - w3mw5 * y6) >> 3;
    y7 = (y8 - w3pw5 * y7) >> 3;

    /* stage 2 */
    y8 = y0 + y1;
    y0 -= y1;
    y1 = w6 * (y3 + y2) + 4;
    y2 = (y1 - w2pw6 * y2) >> 3;
    y3 = (y1 + w2mw6 * y3) >> 3;
    y1 = y4 + y6;
    y4 -= y6;
    y6 = y5 + y7;
    y5 -= y7;

    /* stage 3 */
    y7 = y8 + y3;
    y8 -= y3;
    y3 = y0 + y2;
    y0 -= y2;
    y2 = (r2 * (y4 + y5) + 128) >> 8;
    y4 = (r2 * (y4 - y5) + 128) >> 8;

    /* stage 4 */
    s[8 * 0] = (y7 + y1) >> 14;
    s[8 * 1] = (y3 + y2) >> 14;
    s[8 * 2] = (y0 + y4) >> 14;
    s[8 * 3] = (y8 + y6) >> 14;
    s[8 * 4] = (y8 - y6) >> 14;
    s[8 * 5] = (y0 - y4) >> 14;
    s[8 * 6] = (y3 - y2) >> 14;
    s[8 * 7] = (y7 - y1) >> 14;
  }

  for (int y = 0; y < 64; y++) {
    blk[y] = clampi(roundl(blk[y]) + 128, 0, 255);
  }
}
