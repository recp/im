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

#include "pam.h"
#include "pnm.h"
#include "ppm.h"
#include "../../file.h"
#include "../../str.h"

IM_HIDE
ImResult
pam_dec(ImImage ** __restrict dest, const char * __restrict path) {
  ImImage        *im;
  char           *p, *end;
  im_pam_header_t header;
  ImFileResult    fres;

  char           *pd;
  int32_t         count, i, R, maxRef, bytesPerCompoment;
  float           pe;

  im   = NULL;
  fres = im_readfile(path);

  if (fres.ret != IM_OK) {
    goto err;
  }

  /* decode, this process will be optimized after decoding is done */
  im  = calloc(1, sizeof(*im));
  p   = fres.raw;
  end = p + fres.size;

  /* PAM HEADER */
  if (p[0] == 'P' && p[1] == '7') {
    p += 2;
    header            = pam_dec_header(im, &p, end);
    
    if (header.failed) {
      goto err;
    }

    i                 = 0;
    count             = header.count;
    bytesPerCompoment = header.bytesPerCompoment;
    pe                = header.pe;
    maxRef            = header.maxRef;
    pd                = im->data;
    
    if (pe == 1.0f && maxRef == 255) {
      im_memcpy(pd, p, count * header.depth);
    } else {
      count *= header.depth;

      if (bytesPerCompoment == 1) {
        do {
          *pd++ = min(*p++ * pe, maxRef);
        } while (--count > 0);
      } else if (bytesPerCompoment == 2) {
        do {
          memcpy(&R, p, 2);  p += 2;
          pd[i++] = min(R * pe, maxRef);
        } while (--count > 0);
      }
    }
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
