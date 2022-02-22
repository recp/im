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

#include "pgm.h"
#include "pnm.h"
#include "../../file.h"
#include "../../str.h"

IM_HIDE
ImResult
pgm_dec_ascii(ImImage * __restrict im, char * __restrict p, const char * __restrict end);

IM_HIDE
ImResult
pgm_dec_bin(ImImage * __restrict im, char * __restrict p, const char * __restrict end);

IM_HIDE
ImResult
pgm_dec(ImImage         ** __restrict dest,
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
  
  /* PGM ASCII */
  if (p[0] == 'P' && p[1] == '2') {
    p += 2;
    pgm_dec_ascii(im, p, end);
  }
  
  /* PGM Binary */
  else if (p[0] == 'P' && p[1] == '5') {
    p += 2;
    pgm_dec_bin(im, p, end);
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
pgm_dec_bin(ImImage * __restrict im, char * __restrict p, const char * __restrict end) {
  im_pnm_header_t header;
  char           *pd;
  int32_t         count, i, maxRef, bytesPerCompoment, tmp;
  float           pe;

  i                 = 0;
  header            = pnm_dec_header(im, 1, &p, end, true);
  count             = header.count;
  bytesPerCompoment = header.bytesPerCompoment;
  im->format        = IM_FORMAT_GRAY;
  im->bytesPerPixel = header.bytesPerCompoment;
  im->bitsPerPixel  = im->bytesPerPixel * 8;
  pd                = im->data.data;
  pe                = header.pe;
  maxRef            = header.maxRef;
  
  if (bytesPerCompoment == 1) {
    if (pe == 1.0f && maxRef == 255) {
      im_memcpy(pd, p, count);
    } else {
      do {
        pd[i++] = im_min_i32((uint32_t)(*p++ * pe), maxRef);
      } while (--count > 0);
    }
  } else if (bytesPerCompoment == 2) {
    do {
      memcpy(&tmp, p, 2);
      p      += 2;
      pd[i++] = im_min_i32((uint32_t)(tmp * pe), maxRef);
    } while (--count > 0);
  }

  return IM_OK;
}

IM_HIDE
ImResult
pgm_dec_ascii(ImImage * __restrict im, char * __restrict p, const char * __restrict end) {
  im_pnm_header_t header;
  char           *pd;
  int32_t         count, i, maxRef;
  float           pe;

  i                 = 0;
  header            = pnm_dec_header(im, 1, &p, end, true);
  count             = header.count;
  im->format        = IM_FORMAT_GRAY;
  im->bytesPerPixel = header.bytesPerCompoment;
  im->bitsPerPixel  = im->bytesPerPixel * 8;
  pd                = im->data.data;
  pe                = header.pe;
  maxRef            = header.maxRef;

  do {
    pd[i++] = im_min_i32((uint32_t)(im_getu8_skipspaces(&p, end) * pe), maxRef);
  } while (p && p[0] != '\0' && *++p != '\0' && (--count) > 0);

  /* ensure that unhandled pixels are black. */
  for (; i < count; i++) {
    pd[i] = 0;
  }

  return IM_OK;
}
