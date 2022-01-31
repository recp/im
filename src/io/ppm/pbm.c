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
#include "../../file.h"
#include "../../str.h"

IM_HIDE
ImResult
pbm_dec_ascii(ImImage * __restrict im, char * __restrict p);

IM_HIDE
ImResult
pbm_dec(ImImage ** __restrict dest, const char * __restrict path) {
  ImImage      *im;
  char         *p;
  ImFileResult  fres;
  
  fres = im_readfile(path);
  if (fres.ret != IM_OK) {
    goto err;
  }
  
  /* decode, this process will be optimized after decoding is done */
  im = calloc(1, sizeof(*im));
  p  = fres.raw;
  
  /* PBM ASCII */
  if (p[0] == 'P' && p[1] == '1') {
    p += 2;
    pbm_dec_ascii(im, p);
  }
  
  /* PBM Binary */
  else if (p[0] == 'P' && p[1] == '4') {
    
  }
  
  *dest = im;
  
  if (fres.mmap) {
    im_unmap(fres.raw, fres.size);
  }
  
  return IM_OK;
err:
  *dest = NULL;
  return IM_ERR;
}

IM_HIDE
ImResult
pbm_dec_ascii(ImImage * __restrict im, char * __restrict p) {
  char    *pd;
  uint32_t width, height, count, i, bytesPerPixel;
  char     c;
  bool     parsedHeader;
  
  parsedHeader  = false;
  c             = *p;
  count         = i = 0;
  pd            = NULL;
  bytesPerPixel = 1;
  
  /* parse ASCII STL */
  do {
    /* skip spaces */
    SKIP_SPACES
    
    if (p[0] == '#') {
      NEXT_LINE
      SKIP_SPACES
    }
    
    if (!parsedHeader) {
      im_strtoui(&p, 0, 1, &width);
      im_strtoui(&p, 0, 1, &height);
      parsedHeader = true;
      
      im->data          = malloc(width * height * bytesPerPixel);
      im->format        = IM_FORMAT_GRAY;
      im->len           = count = width * height;
      im->width         = width;
      im->height        = height;
      im->bytesPerPixel = bytesPerPixel;
      pd                = im->data;
    }

    /* TODO: improve seepd of parsing int arrays (parse it manually, optimize loop...) */

    pd[i++] = (!(*p - '0')) * 255;
  } while (p && p[0] != '\0' && (c = *++p) != '\0' && (--count) > 0);
  
  /* ensure that unhandled pixels are black. */
  for (; i < count; i++) {
    pd[i] = 0;
  }

  return IM_OK;
}