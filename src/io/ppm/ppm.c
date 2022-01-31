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
#include "../../file.h"
#include "../../str.h"

IM_HIDE
ImResult
ppm_dec_ascii(ImImage * __restrict im, char * __restrict p);

IM_HIDE
ImResult
ppm_dec(ImImage ** __restrict dest, const char * __restrict path) {
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
  
  /* PPM ASCII */
  if (p[0] == 'P' && p[1] == '3') {
    p += 2;
    ppm_dec_ascii(im, p);
  }
  
  /* PPM Binary */
  else if (p[0] == 'P' && p[1] == '6') {
    
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
ppm_dec_ascii(ImImage * __restrict im, char * __restrict p) {
  char    *pd;
  uint32_t width, height, maxpix, count, i, R, G, B, maxRef, bytesPerPixel;
  float    pe;
  char     c;
  bool     parsedHeader;
  
  parsedHeader = false;
  c            = *p;
  count        = i = 0;
  pd           = NULL;
  maxRef       = 255;
  pe           = 1.0f;
  
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
      im_strtoui(&p, 0, 1, &maxpix);
      parsedHeader = true;
      
      if (maxpix > 255) {
        maxRef        = 65535;
        bytesPerPixel = 2;
      } else {
        maxRef        = 255;
        bytesPerPixel = 1;
      }
      
      bytesPerPixel    *= 3;
      im->data          = malloc(width * height * bytesPerPixel);
      im->format        = IM_FORMAT_RGB;
      im->len           = count = width * height;
      im->width         = width;
      im->height        = height;
      im->bytesPerPixel = bytesPerPixel;
      pd                = im->data;
      
      pe = ((float)maxRef) / ((float)maxpix);
    }
    
    /* TODO: improve seepd of parsing int arrays (parse it manually, optimize loop...) */
    im_strtoui(&p, 0, 1, &R);
    im_strtoui(&p, 0, 1, &G);
    im_strtoui(&p, 0, 1, &B);
    
    pd[i++] = min(R * pe, maxRef);
    pd[i++] = min(G * pe, maxRef);
    pd[i++] = min(B * pe, maxRef);
  } while (p && p[0] != '\0'/* && (c = *++p) != '\0'*/ && (--count) > 0);
  
  /* ensure that unhandled pixels are black. */
  for (; i < count * 3; i++) {
    pd[i] = 0;
  }
  
  return IM_OK;
}
