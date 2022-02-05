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

/*
 References:
 [0] http://netpbm.sourceforge.net/doc/
 [1] https://en.wikipedia.org/wiki/Netpbm
 [2] https://www.cse.cuhk.edu.hk/~ttwong/data/hdrfire/hdrfire.html
 [3] http://www.pauldebevec.com/Research/HDR/PFM/
 */

#include "pfm.h"
#include "pnm.h"
#include "../../file.h"
#include "../../str.h"

IM_HIDE
ImResult
pfm_dec_rgb(ImImage * __restrict im, char * __restrict p, const char * __restrict end);

IM_HIDE
ImResult
pfm_dec_mono(ImImage * __restrict im, char * __restrict p, const char * __restrict end);

IM_HIDE
ImResult
pfm_dec_rgba(ImImage * __restrict im, char * __restrict p, const char * __restrict end);

IM_HIDE
ImResult
pfm_dec(ImImage ** __restrict dest, const char * __restrict path) {
  ImImage      *im;
  char         *p, *end;
  ImFileResult  fres;
  
  im   = NULL;
  fres = im_readfile(path);
  
  if (fres.ret != IM_OK) {
    goto err;
  }
  
  /* decode, this process will be optimized after decoding is done */
  im  = calloc(1, sizeof(*im));
  p   = fres.raw;
  end = p + fres.size;
  
  /* PPM ASCII */
  if (p[0] == 'P' && p[1] == 'F') {
    if (p[2] != '4') {
      p += 2;
      pfm_dec_rgb(im, p, end);
    } else {
      p += 3;
      pfm_dec_rgba(im, p, end);
    }
  }

  /* PPM Binary */
  else if (p[0] == 'P' && p[1] == 'f') {
    p += 2;
    pfm_dec_mono(im, p, end);
  } else {
    goto err;
  }
  
  *dest = im;
  
  if (fres.mmap) {
    im_unmap(fres.raw, fres.size);
  }
  
  return IM_OK;
err:
  if (fres.mmap) {
    im_unmap(fres.raw, fres.size);
  }
  
  if (im) {
    free(im);
  }
  
  *dest = NULL;
  return IM_ERR;
}

IM_HIDE
ImResult
pfm_dec_rgb(ImImage * __restrict im, char * __restrict p, const char * __restrict end) {
  im_pfm_header_t header;
  ImByte         *pd;
  int32_t         count, i, maxRef;
  float           R, G, B;
  bool            isLittleEndian;

  i                 = 0;
  header            = pfm_dec_header(im, 3, &p, end);
  count             = header.count;
  im->format        = IM_FORMAT_RGB;
  im->bytesPerPixel = header.bytesPerCompoment * 3;
  pd                = im->data;
  maxRef            = header.maxRef;
  isLittleEndian    = header.byteOrderHint < 0;

  if (isLittleEndian) {
    do {
      R = im_get_f32_endian(p, true);  p += 4;
      G = im_get_f32_endian(p, true);  p += 4;
      B = im_get_f32_endian(p, true);  p += 4;
      
      pd[i++] = im_clampf_zo(R) * maxRef;
      pd[i++] = im_clampf_zo(G) * maxRef;
      pd[i++] = im_clampf_zo(B) * maxRef;
    } while (--count > 0);
  } else {
    do {
      R = im_get_f32_endian(p, false);  p += 4;
      G = im_get_f32_endian(p, false);  p += 4;
      B = im_get_f32_endian(p, false);  p += 4;

      pd[i++] = im_clampf_zo(R) * maxRef;
      pd[i++] = im_clampf_zo(G) * maxRef;
      pd[i++] = im_clampf_zo(B) * maxRef;
    } while (--count > 0);
  }

  return IM_OK;
}

IM_HIDE
ImResult
pfm_dec_mono(ImImage * __restrict im, char * __restrict p, const char * __restrict end) {
  im_pfm_header_t header;
  ImByte         *pd;
  int32_t         count, i, maxRef;
  float           R;
  bool            isLittleEndian;

  i                 = 0;
  header            = pfm_dec_header(im, 1, &p, end);
  count             = header.count;
  im->format        = IM_FORMAT_MONOCHROME;
  im->bytesPerPixel = header.bytesPerCompoment;
  pd                = im->data;
  maxRef            = header.maxRef;
  isLittleEndian    = header.byteOrderHint < 0;

  if (isLittleEndian) {
    do {
      R       = im_get_f32_endian(p, true);
      p      += 4;
      pd[i++] = im_clampf_zo(R) * maxRef;
    } while (--count > 0);
  } else {
    do {
      R       = im_get_f32_endian(p, false);
      p      += 4;
      pd[i++] = im_clampf_zo(R) * maxRef;
    } while (--count > 0);
  }

  return IM_OK;
}

IM_HIDE
ImResult
pfm_dec_rgba(ImImage * __restrict im, char * __restrict p, const char * __restrict end) {
  im_pfm_header_t header;
  ImByte         *pd;
  int32_t         count, i, maxRef;
  float           R, G, B, A;
  bool            isLittleEndian;
  
  i                 = 0;
  header            = pfm_dec_header(im, 4, &p, end);
  count             = header.count;
  im->format        = IM_FORMAT_RGBA;
  im->alphaInfo     = IM_ALPHA_LAST;
  im->bytesPerPixel = header.bytesPerCompoment * 4;
  pd                = im->data;
  maxRef            = header.maxRef;
  isLittleEndian    = header.byteOrderHint < 0;

  if (isLittleEndian) {
    do {
      R = im_get_f32_endian(p, true);  p += 4;
      G = im_get_f32_endian(p, true);  p += 4;
      B = im_get_f32_endian(p, true);  p += 4;
      A = im_get_f32_endian(p, true);  p += 4;
      
      pd[i++] = im_clampf_zo(R) * maxRef;
      pd[i++] = im_clampf_zo(G) * maxRef;
      pd[i++] = im_clampf_zo(B) * maxRef;
      pd[i++] = im_clampf_zo(A) * maxRef;
    } while (--count > 0);
  } else {
    do {
      R = im_get_f32_endian(p, false);  p += 4;
      G = im_get_f32_endian(p, false);  p += 4;
      B = im_get_f32_endian(p, false);  p += 4;
      A = im_get_f32_endian(p, false);  p += 4;
      
      pd[i++] = im_clampf_zo(R) * maxRef;
      pd[i++] = im_clampf_zo(G) * maxRef;
      pd[i++] = im_clampf_zo(B) * maxRef;
      pd[i++] = im_clampf_zo(A) * maxRef;
    } while (--count > 0);
  }
  
  return IM_OK;
}
