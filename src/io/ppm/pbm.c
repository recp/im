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

#include "pbm.h"
#include "pnm.h"
#include "../../file.h"
#include "../../str.h"

IM_HIDE
ImResult
pbm_dec_ascii(ImImage * __restrict im, char * __restrict p, const char * __restrict end);

IM_HIDE
ImResult
pbm_dec_bin(ImImage * __restrict im, char * __restrict p, const char * __restrict end);

IM_HIDE
ImResult
pbm_dec(ImImage         ** __restrict dest,
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
  
  /* PBM ASCII */
  if (p[0] == 'P' && p[1] == '1') {
    p += 2;
    pbm_dec_ascii(im, p, end);
  }
  
  /* PBM Binary */
  else if (p[0] == 'P' && p[1] == '4') {
    p += 2;
    pbm_dec_bin(im, p, end);
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
pbm_dec_bin(ImImage * __restrict im, char * __restrict p, const char * __restrict end) {
  im_pnm_header_t header;
  ImByte         *pd;
  uint32_t        i, j, k, bitOff, width, height;
  ImByte          c;

  i                 = bitOff = 0;
  header            = pnm_dec_header(im, 1, &p, end, false);
  im->format        = IM_FORMAT_BLACKWHITE;
  im->bytesPerPixel = header.bytesPerCompoment;
  im->bitsPerPixel  = im->bytesPerPixel * 8;
  width             = header.width;
  height            = header.height;
  pd                = im->data.data;
  c                 = *p;
  
  /* parse raster */
  
  for (j = 0; j < height; j++) {
    for (k = 0; k < width; k++) {
      pd[i++] = (!(c >> 7)) * 255;
      c     <<= 1;

      if (++bitOff > 7) {
        bitOff = 0;
        c      = *++p;
      }
    }

    if (bitOff != 0) {
      c      = *++p;
      bitOff = 0;
    }
  }

  return IM_OK;
}

IM_HIDE
ImResult
pbm_dec_ascii(ImImage * __restrict im, char * __restrict p, const char * __restrict end) {
  im_pnm_header_t header;
  char           *pd;
  uint32_t        count, i;
  char            c;

  i                 = 0;
  header            = pnm_dec_header(im, 1, &p, end, false);
  count             = header.count;
  im->format        = IM_FORMAT_BLACKWHITE;
  im->bytesPerPixel = header.bytesPerCompoment;
  im->bitsPerPixel  = im->bytesPerPixel * 8;
  pd                = im->data.data;
  c                 = *p;

  /* parse raster */
  do {
    /* skip spaces */
    while (IM_ALLSPACES) { c = *++p; }

    pd[i++] = (!(*p - '0')) * 255;
  } while (p && p[0] != '\0' && (c = *++p) != '\0' && (--count) > 0);
  
  /* ensure that unhandled pixels are black. */
  for (; i < count; i++) {
    pd[i] = 0;
  }

  return IM_OK;
}
