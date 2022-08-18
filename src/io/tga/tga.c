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
#include "../../pp/pp.h"

IM_INLINE
uint8_t
tga_compc(ImFormat * __restrict format, uint8_t imtype, uint8_t imdesc, uint8_t depth) {
  uint8_t ncomp;

  /* TODO: use imdesc and handle error */
  ncomp = 0;

  switch (depth) {
    case 8:           ncomp = 1; break;
    case 15: case 16: ncomp = 2; break;
    case 24:          ncomp = 3; break;
    case 32:          ncomp = 4; break;
    default: break;
  }

  switch (imtype) {
    case 3: case 11:                 *format = IM_FORMAT_BLACKWHITE; break;
    case 1: case 2: case 9: case 10: *format = IM_FORMAT_RGB;        break;
    default: break;
  }

  return ncomp;
}

IM_HIDE
ImResult
tga_dec(ImImage         ** __restrict dest,
        const char       * __restrict path,
        im_open_config_t * __restrict open_config) {
  ImImage            *im;
  im_pal_t           *pal;
  uint8_t            *p;
  ImFileResult        fres;
  bool                safemem, usemmap;

  usemmap = false; /* TODO: */
  im      = NULL;
  fres    = im_readfile(path, usemmap);

  if (fres.ret != IM_OK) {
    goto err;
  }
  
  safemem = !open_config->releaseFile
              && open_config->openIntent == IM_OPEN_INTENT_READONLY
              && !open_config->bgr2rgb
  ;

  /* decode, this process will be optimized after decoding is done */
  p                  = fres.raw;
  im                 = calloc(1, sizeof(*im));
  im->fileFormatType = IM_FILEFORMATTYPE_TGA;

  uint8_t *id, idlen, cmap_type, imtype, pal_entry_size, depth, imdesc, ncomp;
  uint16_t pal_first_idx, pal_len, pos_x, pos_y, width, height;

  idlen     = *p++;
  cmap_type = *p++;
  imtype  = *p++;
  id        = alloca(idlen);
  pal_len   = pal_entry_size = 0;

  if (cmap_type && imtype) {
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
    p += idlen;
  }

  /* palette */
  if (cmap_type && imtype && pal_len > 0) {
    im->pal    = pal = calloc(1, sizeof(*pal));
    pal->count = pal_len;
    pal->len   = pal_len * (pal_entry_size / 8);
    pal->pal   = malloc(pal->len);
    
    memcpy(pal->pal, p, pal->len);

    p += pal_len;
  }

  im->data.data = malloc(width * height * depth);
  im->width     = width;
  im->height    = height;

  switch ((imdesc >> 3) & 0x1) {
    case 0x0: im->ori = IM_ORIENTATION_LEFT;  break;
    case 0x1: im->ori = IM_ORIENTATION_RIGHT; break;
  }

  switch ((imdesc >> 4) & 0x1) {
    case 0x0: im->ori |= IM_ORIENTATION_DOWN; break;
    case 0x1: im->ori |= IM_ORIENTATION_UP;   break;
  }
  
  ncomp                  = tga_compc(&im->format, imtype, imdesc, depth);
  im->componentsPerPixel = ncomp;
  im->bitsPerComponent   = 8; /* TODO: */
  im->bitsPerPixel       = ncomp * 8;
  im->bytesPerPixel      = ncomp;
  im->alphaInfo          = IM_ALPHA_NONE; /* TODO: check alpha bits */

  if (likely(open_config->bgr2rgb)) {
    im->data.data = p;
    rgb8_to_bgr8_all(p, width * height);
  } else {
    im->data.data = p;
  }

  *dest = im;
  im->file = fres;

  if (open_config->releaseFile && fres.mmap) {
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
