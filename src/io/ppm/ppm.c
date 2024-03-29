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

#include "ppm.h"
#include "pnm.h"
#include "../../file.h"
#include "../../str.h"

IM_HIDE
ImResult
ppm_dec_ascii(ImImage * __restrict im, char * __restrict p, const char * __restrict end);

IM_HIDE
ImResult
ppm_dec_bin(ImImage * __restrict im, char * __restrict p, const char * __restrict end);

IM_HIDE
ImResult
ppm_dec(ImImage         ** __restrict dest,
        const char       * __restrict path,
        im_open_config_t * __restrict open_config) {
  ImImage      *im;
  char         *p, *end;
  ImFileResult  fres;
  
  im   = NULL;
  fres = im_readfile(path, open_config->openIntent != IM_OPEN_INTENT_READWRITE);

  if (fres.ret != IM_OK) {
    goto err;
  }
  
  /* decode, this process will be optimized after decoding is done */
  im  = calloc(1, sizeof(*im));
  p   = fres.raw;
  end = p + fres.size;

  /* PPM ASCII */
  if (p[0] == 'P' && p[1] == '3') {
    p += 2;
    ppm_dec_ascii(im, p, end);
  }
  
  /* PPM Binary */
  else if (p[0] == 'P' && p[1] == '6') {
    p += 2;
    ppm_dec_bin(im, p, end);
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
ppm_dec_bin(ImImage * __restrict im, char * __restrict p, const char * __restrict end) {
  im_pnm_header_t header;
  char           *pd;
  int32_t         count, i, R, G, B, maxRef, bytesPerCompoment;
  float           pe;
  
  i                 = 0;
  header            = pnm_dec_header(im, 3, &p, end, true);
  count             = header.count;
  bytesPerCompoment = header.bytesPerCompoment;
  im->format        = IM_FORMAT_RGB;
  im->bytesPerPixel = bytesPerCompoment * 3;
  im->bitsPerPixel  = im->bytesPerPixel * 8;
  pd                = im->data.data;
  pe                = header.pe;
  maxRef            = header.maxRef;

  if (bytesPerCompoment == 1) {
    if (pe == 1.0f && maxRef == 255) {
      im_memcpy(pd, p, count * 3);
    } else {
      do {
        pd[0]  = im_min_i32((uint32_t)(p[0] * pe), maxRef);
        pd[1]  = im_min_i32((uint32_t)(p[1] * pe), maxRef);
        pd[2]  = im_min_i32((uint32_t)(p[2] * pe), maxRef);

        pd    += 3;
        p     += 3;
      } while (--count > 0);
    }
  } else if (bytesPerCompoment == 2) {
    do {
      memcpy(&R, p, 2);  p += 2;
      memcpy(&G, p, 2);  p += 2;
      memcpy(&B, p, 2);  p += 2;

      pd[i++] = im_min_i32((uint32_t)(R * pe), maxRef);
      pd[i++] = im_min_i32((uint32_t)(G * pe), maxRef);
      pd[i++] = im_min_i32((uint32_t)(B * pe), maxRef);
    } while (--count > 0);
  }

  return IM_OK;
}

IM_HIDE
ImResult
ppm_dec_ascii(ImImage * __restrict im, char * __restrict p, const char * __restrict end) {
  im_pnm_header_t header;
  char           *pd;
  uint32_t        count, i, R, G, B, maxRef;
  float           pe;

  i                 = 0;
  header            = pnm_dec_header(im, 3, &p, end, true);
  count             = header.count;
  im->format        = IM_FORMAT_RGB;
  im->bytesPerPixel = header.bytesPerCompoment * 3;
  im->bitsPerPixel  = im->bytesPerPixel * 8;
  pd                = im->data.data;
  pe                = header.pe;
  maxRef            = header.maxRef;

  do {
    R = im_getu32_skipspaces(&p, end);
    G = im_getu32_skipspaces(&p, end);
    B = im_getu32_skipspaces(&p, end);

    pd[i++] = im_min_i32((uint32_t)(R * pe), maxRef);
    pd[i++] = im_min_i32((uint32_t)(G * pe), maxRef);
    pd[i++] = im_min_i32((uint32_t)(B * pe), maxRef);
  } while (p && p[0] != '\0' && *++p != '\0' && (--count) > 0);
  
  /* ensure that unhandled pixels are black. */
  for (; i < count * 3; i++) {
    pd[i] = 0;
  }

  return IM_OK;
}
