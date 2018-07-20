/*
 * Copyright (c), Recep Aslantas.
 * MIT License (MIT), http://opensource.org/licenses/MIT
 */

#include "idct.h"

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

IM_HIDE
void
jpg_idct(int16_t * __restrict blk) {
  int x, y, x0, x1, x2, x3, x4, x5, x6, x7, x8,
  y0, y1, y2, y3, y4, y5, y6, y7, y8;

  /* Horizontal 1-D IDCT. */
  for (y = 0; y < 8; y++) {
    y8 = y * 8;

    /* if all the AC components are zero, then the IDCT is trivial */
    if (blk[y8 + 1] == 0 && blk[y8 + 2] == 0 && blk[y8 + 3] == 0 &&
        blk[y8 + 4] == 0 && blk[y8 + 5] == 0 && blk[y8 + 6] == 0 &&
        blk[y8 + 7] == 0) {
      blk[y8 + 0] =
      blk[y8 + 1] =
      blk[y8 + 2] =
      blk[y8 + 3] =
      blk[y8 + 4] =
      blk[y8 + 5] =
      blk[y8 + 6] =
      blk[y8 + 7] = blk[y8 + 0] << 3;
      return;
    }

    x0 = (blk[y8 + 0] << 11) + 128;
    x1 = blk[y8 + 4] << 11;
    x2 = blk[y8 + 6];
    x3 = blk[y8 + 2];
    x4 = blk[y8 + 1];
    x5 = blk[y8 + 7];
    x6 = blk[y8 + 5];
    x7 = blk[y8 + 3];

    /* stage 1 */
    x8 = w7 * (x4 + x5);
    x4 = x8 + w1mw7 * x4;
    x5 = x8 - w1pw7 * x5;
    x8 = w3 * (x6 + x7);
    x6 = x8 - w3mw5 * x6;
    x7 = x8 - w3pw5 * x7;

    /* stage 2 */
    x8 = x0 + x1;
    x0 -= x1;
    x1 = w6 * (x3 + x2);
    x2 = x1 - w2pw6 * x2;
    x3 = x1 + w2mw6 * x3;
    x1 = x4 + x6;
    x4 -= x6;
    x6 = x5 + x7;
    x5 -= x7;

    /* stage 3 */
    x7 = x8 + x3;
    x8 -= x3;
    x3 = x0 + x2;
    x0 -= x2;
    x2 = (r2 * (x4 + x5) + 128) >> 8;
    x4 = (r2 * (x4 - x5) + 128) >> 8;

    /* stage 4 */
    blk[y8 + 0] = (x7 + x1) >> 8;
    blk[y8 + 1] = (x3 + x2) >> 8;
    blk[y8 + 2] = (x0 + x4) >> 8;
    blk[y8 + 3] = (x8 + x6) >> 8;
    blk[y8 + 4] = (x8 - x6) >> 8;
    blk[y8 + 5] = (x0 - x4) >> 8;
    blk[y8 + 6] = (x3 - x2) >> 8;
    blk[y8 + 7] = (x7 - x1) >> 8;
  }

  for (x = 0; x < 8; x++) {
    /*
     similar to the horizontal 1-D IDCT case, if all the AC components are zero,
     then the IDCT is trivial.
     however, after performing the horizontal 1-D IDCT, there are typically
     non-zero AC components, so
     we do not bother to check for the all-zero case. */
    y0 = (blk[8 * 0 + x] << 8) + 8192;
    y1 = blk[8 * 4 + x] << 8;
    y2 = blk[8 * 6 + x];
    y3 = blk[8 * 2 + x];
    y4 = blk[8 * 1 + x];
    y5 = blk[8 * 7 + x];
    y6 = blk[8 * 5 + x];
    y7 = blk[8 * 3 + x];

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
    blk[8 * 0 + x] = (y7 + y1) >> 14;
    blk[8 * 1 + x] = (y3 + y2) >> 14;
    blk[8 * 2 + x] = (y0 + y4) >> 14;
    blk[8 * 3 + x] = (y8 + y6) >> 14;
    blk[8 * 4 + x] = (y8 - y6) >> 14;
    blk[8 * 5 + x] = (y0 - y4) >> 14;
    blk[8 * 6 + x] = (y3 - y2) >> 14;
    blk[8 * 7 + x] = (y7 - y1) >> 14;
  }
}
