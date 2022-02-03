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
    p += 2;
    pfm_dec_rgb(im, p, end);
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
  char           *pd;
  int32_t         count, i, maxRef;
  float           R, G, B;

  i                 = 0;
  header            = pfm_dec_header(im, 3, &p, end);
  count             = header.count;
  im->format        = IM_FORMAT_RGB;
  im->bytesPerPixel = header.bytesPerCompoment * 3;
  pd                = im->data;
  maxRef            = header.maxRef;
  
  do {
    memcpy(&R, p, 4);  p += 4;
    memcpy(&G, p, 4);  p += 4;
    memcpy(&B, p, 4);  p += 4;


    pd[i++] = min((ImByte)(im_clampf_zo(R) * 255), maxRef);
    pd[i++] = min((ImByte)(im_clampf_zo(G) * 255), maxRef);
    pd[i++] = min((ImByte)(im_clampf_zo(B) * 255), maxRef);
  } while (--count > 0);

  return IM_OK;
}

IM_HIDE
ImResult
pfm_dec_mono(ImImage * __restrict im, char * __restrict p, const char * __restrict end) {
  im_pfm_header_t header;
  char           *pd;
  int32_t         count, i, maxRef;
  float           R;
  
  i                 = 0;
  header            = pfm_dec_header(im, 1, &p, end);
  count             = header.count;
  im->format        = IM_FORMAT_MONOCHROME;
  im->bytesPerPixel = header.bytesPerCompoment;
  pd                = im->data;
  maxRef            = header.maxRef;
  
  do {
    memcpy(&R, p, 4);
    p      += 4;
    pd[i++] = min(R, maxRef);
  } while (--count > 0);
  
  return IM_OK;
}
