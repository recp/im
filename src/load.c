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
 * From memory: im_load_mem (JPEG, JPEG 2000, JBIG2; raw CCITT fax and JBIG2's
 * embedded segments where the caller says so), im_info_mem (JPEG, JPEG 2000,
 * JBIG2), im_free, and the options as
 * decoders read them. All a codec-only build has
 * (IM_CODECS_ONLY: no file layer, no Core Image, no other formats).
 */

#include "common.h"
#include "formats.h"
#if IM_WITH_JPG
#  include "io/jpg/jpg.h"
#endif
#if IM_WITH_JP2
#  include "io/jp2/jp2.h"
#endif
#if IM_WITH_CCITT
#  include "io/ccitt/ccitt.h"
#endif
#if IM_WITH_JBIG2
#  include "io/jbig2/jbig2.h"
#endif

/* the options as a decoder reads them */
IM_HIDE im_open_config_t
im_config(im_option_base_t *options[], ImOpenIntent openIntent) {
  im_open_config_t  conf;
  im_option_base_t *opt;

  memset(&conf, 0, sizeof(conf));
  conf.openIntent    = openIntent;
  conf.byteOrder     = IM_BYTEORDER_ANY;
  conf.supportsPal   = true;
  conf.options       = options;
  conf.jpegTransform = -1;

  if (options) {
    for (int i = 0; options[i]; i++) {
      opt = options[i];
      switch (opt->type) {
        case IM_OPTION_ROW_PAD_LAST:         conf.rowPadding     = ((im_option_rowpadding_t*)opt)->pad;  break;
        case IM_OPTION_BYTE_ORDER:           conf.byteOrder      = ((im_option_byteorder_t*)opt)->order; break;
        case IM_OPTION_SUPPORTS_PALETTE:     conf.supportsPal    = ((im_option_bool_t*)opt)->on;         break;
        case IM_OPTION_BGR_TO_RGB:           conf.bgr2rgb        = ((im_option_bool_t*)opt)->on;         break;
        case IM_OPTION_THREADS:              conf.threads        = (uint32_t)((im_option_int_t*)opt)->value; break;
        case IM_OPTION_JPEG_COLOR_TRANSFORM: conf.jpegTransform  = ((im_option_int_t*)opt)->value;       break;
        case IM_OPTION_CMYK_AS_STORED:       conf.cmykAsStored   = ((im_option_bool_t*)opt)->on;         break;
        case IM_OPTION_RGB_PADDED:           conf.rgbPadded      = ((im_option_bool_t*)opt)->on;         break;
        case IM_OPTION_PALETTE_INDEXES:      conf.paletteIndexes = ((im_option_bool_t*)opt)->on;         break;
        case IM_OPTION_EIGHT_BITS:           conf.eightBits      = ((im_option_bool_t*)opt)->on;         break;
        case IM_OPTION_OPACITY:              conf.opacity        = ((im_option_int_t*)opt)->value;       break;
        case IM_OPTION_RUNNER:               conf.runner         = ((im_option_runner_t*)opt)->runner;   break;
        case IM_OPTION_SCALE:                conf.scale          = (uint32_t)((im_option_int_t*)opt)->value; break;
        case IM_OPTION_REGION:
          conf.region  = true;
          conf.regionX = ((im_option_region_t*)opt)->x;
          conf.regionY = ((im_option_region_t*)opt)->y;
          conf.regionW = ((im_option_region_t*)opt)->width;
          conf.regionH = ((im_option_region_t*)opt)->height;
          break;
        case IM_OPTION_MEMORY_LIMIT:         conf.memoryLimit    = ((im_option_size_t*)opt)->value;      break;
        case IM_OPTION_FORMAT:               conf.format         = ((im_option_int_t*)opt)->value;       break;
        case IM_OPTION_CCITT:                conf.ccitt          = ((im_option_ccitt_t*)opt)->params;    break;
        case IM_OPTION_JBIG2_GLOBALS:
          conf.jbig2Globals    = ((im_option_data_t*)opt)->data;
          conf.jbig2GlobalsLen = ((im_option_data_t*)opt)->len;
          break;
        case IM_OPTION_DEST:
          conf.dest       = ((im_option_dest_t*)opt)->pixels;
          conf.destStride = ((im_option_dest_t*)opt)->stride;
          conf.destSize   = ((im_option_dest_t*)opt)->size;
          break;
        default: break;
      }
    }
  }
  return conf;
}

IM_EXPORT
ImResult
im_info_mem(ImInfo * __restrict info, const void * __restrict data, size_t len) {
  static const uint8_t jp2sig[12] = { 0, 0, 0, 12, 'j', 'P', ' ', ' ', 13, 10, 0x87, 10 };
  static const uint8_t jbig2sig[8] = { 0x97, 'J', 'B', '2', 0x0D, 0x0A, 0x1A, 0x0A };
  const uint8_t       *p = data;

  if (!info) return IM_EBADF;
  memset(info, 0, sizeof(*info));
  if (!p || len < 4u) return IM_EBADF;
#if IM_WITH_JPG
  if (p[0] == 0xFFu && p[1] == 0xD8u) return jpg_info(p, len, info) ? IM_OK : IM_EBADF;
#endif
#if IM_WITH_JP2
  if ((len >= 12u && !memcmp(p, jp2sig, 12u)) || (p[0] == 0xFFu && p[1] == 0x4Fu && p[2] == 0xFFu && p[3] == 0x51u)) {
    return jp2_info(p, len, info) ? IM_OK : IM_EBADF;
  }
#endif
#if IM_WITH_JBIG2
  if (len >= 9u && !memcmp(p, jbig2sig, 8u)) return jbig2_info(p, len, info) ? IM_OK : IM_EBADF;
#endif
  (void)jp2sig;
  (void)jbig2sig;
  return IM_EBADF;
}

IM_EXPORT
ImResult
im_load_mem(ImImage         ** __restrict dest,
            const void       * __restrict data,
            size_t                        len,
            im_option_base_t *            options[],
            ImOpenIntent                  openIntent) {
  static const uint8_t jp2sig[12] = { 0, 0, 0, 12, 'j', 'P', ' ', ' ', 13, 10, 0x87, 10 };
  static const uint8_t jbig2sig[8] = { 0x97, 'J', 'B', '2', 0x0D, 0x0A, 0x1A, 0x0A };
  const uint8_t       *p = data;
  im_open_config_t     conf;

  if (!dest) return IM_EBADF;
  *dest = NULL;
  if (!p || !len) return IM_EBADF;
  conf = im_config(options, openIntent);

  /* where the caller says what it is (no signature to tell it by; a
     stream of a byte or two can be a whole image) */
#if IM_WITH_CCITT
  if (conf.format == IM_FILEFORMATTYPE_CCITT) return ccitt_dec_mem(dest, p, len, &conf);
#endif
#if IM_WITH_JBIG2
  if (conf.format == IM_FILEFORMATTYPE_JBIG2 || (len >= 9u && !memcmp(p, jbig2sig, 8u))) return jbig2_dec_mem(dest, p, len, &conf);
#endif
  if (len < 4u) return IM_EBADF;

  /* by its first bytes */
#if IM_WITH_JPG
  if (p[0] == 0xFFu && p[1] == 0xD8u) return jpg_dec_mem(dest, p, len, &conf);
#endif
#if IM_WITH_JP2
  if ((len >= 12u && !memcmp(p, jp2sig, 12u)) || (p[0] == 0xFFu && p[1] == 0x4Fu && p[2] == 0xFFu && p[3] == 0x51u)) {
    return jp2_dec_mem(dest, p, len, &conf);
  }
#endif
  (void)jp2sig;
  (void)conf;
  return IM_EBADF;
}

IM_EXPORT
ImResult
im_free(ImImage * __restrict im) {
  if (!im) return IM_OK;

#if !IM_CODECS_ONLY
  if (im->file.mmap) {
    im_unmap(im->file.raw, im->file.size);
  } else if (im->file.mustfree) {
    free(im->file.raw);
  }
#endif

  if (im->data.data) {
    free(im->data.data);
  }

  if (im->background)
    free(im->background);

  if (im->chrm)
    free(im->chrm);

  if (im->iccProfile)
    free(im->iccProfile);

  if (im->physicalDim)
    free(im->physicalDim);

  if (im->timeStamp)
    free(im->timeStamp);

  free(im->alpha);

  if (im->transparency) {
    if (im->transparency->value.pal.alpha)
      free(im->transparency->value.pal.alpha);
    free(im->transparency);
  }

  free(im);

  return IM_OK;
}
