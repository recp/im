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

#include "tga.h"
#include "../../file.h"
#include "../../endian.h"

IM_HIDE
ImResult
tga_dec(ImImage         ** __restrict dest,
        const char       * __restrict path,
        im_open_config_t * __restrict open_config) {
  ImImage            *im;
  im_pal_t           *pal;
  char               *p;
  uint32_t            dataoff;
  ImFileResult        fres;

  im   = NULL;
  fres = im_readfile(path, open_config->openIntent != IM_OPEN_INTENT_READWRITE);

  if (fres.ret != IM_OK) {
    goto err;
  }

  /* decode, this process will be optimized after decoding is done */
  p                  = fres.raw;
  im                 = calloc(1, sizeof(*im));
  im->fileFormatType = IM_FILEFORMATTYPE_TGA;

  uint8_t *id, idlen, cmap_type, img_type, pal_entry_size, depth, imdesc;
  uint16_t pal_first_idx, pal_len, pos_x, pos_y, width, height;

  idlen     = *p++;
  cmap_type = *p++;
  img_type  = *p++;
  id        = alloca(idlen);
  pal_len   = pal_entry_size = 0;

  if (img_type) {
    pal_first_idx  = im_get_u16_endian(p, true);
    pal_len        = im_get_u16_endian(p + 2, true);
    pal_entry_size = *(p + 4);
  }

  p += 5;

  pos_x  = im_get_u16_endian(p, true);  p += 2;
  pos_y  = im_get_u16_endian(p, true);  p += 2;
  width  = im_get_u16_endian(p, true);  p += 2;
  height = im_get_u16_endian(p, true);  p += 2;
  depth  = *p++;
  imdesc = *p++;

  /* TODO: option to ignore reading ID */
  if (idlen > 0) {
    memcpy(id, p, idlen);
  }

  /* palette */
  if (img_type && pal_len > 0) {
    im->pal    = pal = calloc(1, sizeof(*pal));
    pal->count = pal_len;
    pal->len   = pal_len * (pal_entry_size / 8);
    pal->pal   = malloc(pal->len);
    
    memcpy(pal->pal, p, pal->len);
  }
  
  im->data.data = malloc(width * height * depth);
  im->width     = width;
  im->height    = height;
  
  /* TODO: just to see the result for now */
  im->format             = IM_FORMAT_RGB;
  im->componentsPerPixel = 3;
  im->bitsPerComponent   = 8;
  im->bitsPerPixel       = 24;
  im->bytesPerPixel      = 3;
  
  memcpy(im->data.data, p, width * height * (depth / 8));
  switch ((imdesc >> 3) & 0x1) {
    case 0x0: im->ori = IM_ORIENTATION_LEFT;  break;
    case 0x1: im->ori = IM_ORIENTATION_RIGHT; break;
  }

  switch ((imdesc >> 4) & 0x1) {
    case 0x0: im->ori |= IM_ORIENTATION_DOWN; break;
    case 0x1: im->ori |= IM_ORIENTATION_UP;   break;
  }


  *dest = im;
  im->file = fres;

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
