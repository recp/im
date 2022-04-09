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

#include "png.h"
#include "../../file.h"
#include "../../endian.h"

#define IM_PNG_TYPE(a,b,c,d)  (((unsigned)(a) << 24) + ((unsigned)(b) << 16)  \
                             + ((unsigned)(c) << 8)  + (unsigned)(d))

IM_HIDE
ImResult
png_dec(ImImage         ** __restrict dest,
        const char       * __restrict path,
        im_open_config_t * __restrict open_config) {
  ImImage            *im;
  ImByte             *p, *p_chk, bitdepth, color, compr, filter, interlace;
  uint32_t            dataoff, chk_len, chk_type;
  ImFileResult        fres;
  bool                is_cgbi;
  
//  Bit depth  1 byte
//  Colour type  1 byte
//  Compression method  1 byte
//  Filter method  1 byte
//  Interlace method  1 byte

  
  im   = NULL;
  fres = im_readfile(path, open_config->openIntent != IM_OPEN_INTENT_READWRITE);
  
  if (fres.ret != IM_OK) {
    goto err;
  }
  
  /* decode, this process will be optimized after decoding is done */
  im  = calloc(1, sizeof(*im));
  p   = fres.raw;
  
  /*
     Magic number types:
     -------------------
     89 50 4E 47 0D 0A 1A 0A
   */

  if (p[0] != 0x89
      && p[1] != 0x50 && p[2] != 0x4E && p[3] != 0x47
      && p[4] != 0x0D && p[5] != 0x0A
      && p[6] != 0x1A
      && p[7] != 0x0A) {
    goto err;
  }

  p += 8;

  im->fileFormatType = IM_FILEFORMATTYPE_PNG;
  
  for (;;) {
    chk_len  = im_get_u32_endian(p, false); p += 4;
    chk_type = im_get_u32_endian(p, false); p += 4;
    p_chk    = p;

    switch (chk_type) {
      case IM_PNG_TYPE('C','g','B','I'):
        is_cgbi = true;
        break;
      case IM_PNG_TYPE('I','H','D','R'): {
        im->width  = im_get_u32_endian(p, false); p += 4;
        im->height = im_get_u32_endian(p, false); p += 4;
        bitdepth   = *p++;
        color      = *p++;
        compr      = *p++;
        filter     = *p++;
        interlace  = *p;
        break;
      }
      case IM_PNG_TYPE('P','L','T','E'): {
        break;
      }
      case IM_PNG_TYPE('I','D','A','T'): {
        break;
      }
      case IM_PNG_TYPE('I','E','N','D'): {
        goto nx;
      }
    }
    
    p = p_chk + chk_len + 4; /* 4: CRC */
  }

nx:
//  if (dib_dec_mem(im,
//                  p,
//                  (char *)fres.raw + dataoff,
//                  (char *)fres.raw + fres.size - 1) != IM_OK) {
//    goto err;
//  }
  
  *dest = im;
  im->file = fres;
  
  //  if (fres.mmap) {
  //    im_unmap(fres.raw, fres.size);
  //  }
  
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
