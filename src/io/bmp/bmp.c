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

#include "bmp.h"
#include "dib.h"
#include "../../file.h"
#include "../../endian.h"

/*
 References:
 [0] https://en.wikipedia.org/wiki/BMP_file_format
 [1] http://www.edm2.com/0107/os2bmp.html
 [2] http://netghost.narod.ru/gff/graphics/summary/os2bmp.htm
 [3] http://www.redwoodsoft.com/~dru/museum/gfx/gff/sample/code/os2bmp/os2_code.txt
 [4] https://gibberlings3.github.io/iesdp/file_formats/ie_formats/bmp.htm
 [5] http://www.martinreddy.net/gfx/2d/BMP.txt
 [6] https://www.fileformat.info/format/bmp/egff.htm
 */

IM_HIDE
ImResult
bmp_dec(ImImage         ** __restrict dest,
        const char       * __restrict path,
        im_open_config_t * __restrict open_config) {
  ImImage            *im;
  char               *p;
  uint32_t            dataoff;
  ImFileResult        fres;

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
   BM Windows 3.1x, 95, NT, ... etc.
   BA OS/2 struct bitmap array
   CI OS/2 struct color icon
   CP OS/2 const color pointer
   IC OS/2 struct icon
   PT OS/2 pointer
   */

  if (p[0] != 'B' && p[1] != 'M') {
    goto err;
  }

  p      += 2;
  im->fileFormatType = IM_FILEFORMATTYPE_BMP_Windows;

  p      += 4; /* file size: uint32 */
  p      += 4; /* reserved 4 bytes (unused) */

  dataoff = im_get_u32_endian(p, true);  p += 4;

  if (dib_dec_mem(im,
                  p,
                  (char *)fres.raw + dataoff,
                  (char *)fres.raw + fres.size - 1) != IM_OK) {
    goto err;
  }

ok:
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

IM_HIDE
ImResult
dib_dec(ImImage         ** __restrict dest,
        const char       * __restrict path,
        im_open_config_t * __restrict open_config) {
  ImImage     *im;
  char        *p;
  ImFileResult fres;

  im   = NULL;
  fres = im_readfile(path, path != NULL);
  
  if (fres.ret != IM_OK) {
    goto err;
  }
  
  /* decode, this process will be optimized after decoding is done */
  im  = calloc(1, sizeof(*im));
  p   = fres.raw;

  if (dib_dec_mem(im,
                  p,
                  NULL,
                  (char *)fres.raw + fres.size - 1) != IM_OK) {
    goto err;
  }

ok:
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
