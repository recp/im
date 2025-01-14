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

#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <assert.h>

#include <stdarg.h>

#include "thread/thread.h"

#include "color.h"
#include "sampler.h"

//#include "io/jpg/dec/dec.h"
#include "io/ppm/ppm.h"
#include "io/ppm/pgm.h"
#include "io/ppm/pbm.h"
#include "io/ppm/pfm.h"
#include "io/ppm/pam.h"
#include "io/bmp/bmp.h"
#include "io/bmp/dib.h"
#include "io/png/png.h"
#include "io/tga/tga.h"
#include "io/qoi/qoi.h"
#include "io/heic/heic.h"
#include "io/jxl/jxl.h"
#include "io/jp2/jp2.h"

#include "file.h"

#ifdef IM_WINAPI
/* Exclude rarely - used stuff from Windows headers */
#  define WIN32_LEAN_AND_MEAN
#  include <SDKDDKVer.h>

/* Windows Header Files : */
#  include <windows.h>
#endif

typedef struct floader_t {
  const char * fext;
  ImResult (*floader_fn)(ImImage ** __restrict, const char * __restrict, im_open_config_t * __restrict open_config);
} floader_t;

IM_EXPORT
void*
im_init_data(ImImage * __restrict im, uint32_t size) {
#ifdef IM_WINAPI
  /* TODO: disable mapping with option */
  /* https://docs.microsoft.com/en-us/windows/win32/memory/creating-named-shared-memory */
  HANDLE hMapFile;
  LPVOID pBuf;

  hMapFile = CreateFileMapping(INVALID_HANDLE_VALUE, /* use paging file                        */
                               NULL,                 /* default security                       */
                               PAGE_READWRITE,       /* read/write access                      */
                               0,                    /* maximum object size (high-order DWORD) */
                               (DWORD)size,          /* maximum object size (low-order DWORD)  */
                               NULL);                /* name of mapping object                 */

  if (hMapFile == NULL) {
    im->data.data = malloc(size);
    return im->data.data;
  }

  pBuf = MapViewOfFile(hMapFile,            /* handle to map object */
                       FILE_MAP_ALL_ACCESS, /* read/write permission */
                       0,
                       0,
                       size);

  if (pBuf == NULL) {
    CloseHandle(hMapFile);
    im->data.data = malloc(size);
    return im->data.data;
  } else {
    im->data.data  = pBuf;
    im->data.udata = hMapFile;
  }

  return im->data.data;

#else
  if (im->data.data)
    return im->data.data;

  im->data.data = malloc(size);

  return im->data.data;
#endif
}

IM_EXPORT
ImResult
im_load(ImImage         ** __restrict dest,
        const char       * __restrict url,
        im_option_base_t *            options[],
        ImOpenIntent                  openIntent) {
  im_option_base_t *opt;
  floader_t        *floader;
  const char       *localurl;
  int               file_type;
  int               _err_no, i;

  file_type = IM_FILE_TYPE_AUTO;

  /* TODO: remote or another kind URL than file URL ? */
  localurl = url;
  if (!localurl)
    return IM_EBADF;
  
  im_open_config_t open_conf = {0};
  
  /*defaults  */
  open_conf.openIntent  = openIntent;
  open_conf.byteOrder   = IM_BYTEORDER_ANY;
  open_conf.rowPadding  = 0;
  open_conf.supportsPal = true;
  
  /* override defaults */
  open_conf.options    = options;

  i = 0;
  if (options && (opt = options[i])) {
    do {
      switch (opt->type) {
        case IM_OPTION_ROW_PAD_LAST:     open_conf.rowPadding = ((im_option_rowpadding_t *)opt)->pad;     break;
        case IM_OPTION_BYTE_ORDER:       open_conf.byteOrder = ((im_option_byteorder_t *)opt)->order;     break;
        case IM_OPTION_SUPPORTS_PALETTE: open_conf.supportsPal = ((im_option_bool_t *)opt)->on;           break;
        case IM_OPTION_BGR_TO_RGB:       open_conf.bgr2rgb = ((im_option_bool_t *)opt)->on;               break;
        default: break;
      }
    } while ((opt = options[++i]));
  }

  floader_t floaders[] = {
    
    {"pbm",  pbm_dec},
    {"pgm",  pgm_dec},
    {"ppm",  ppm_dec},
    {"pfm",  pfm_dec},
    {"pam",  pam_dec},
//    {"jpeg", jpg_dec},
//    {"jpg",  jpg_dec},
    {"png",  png_dec},
    {"bmp",  bmp_dec},
    {"dib",  dib_dec},

    /* tga */
    {"tga",  tga_dec},
    {"tpic", tga_dec},

    /* tga (old extensions) */
    {"icb",  tga_dec},
    {"vda",  tga_dec},
    {"vst",  tga_dec},
    
    {"qoi",  qoi_dec},
    
    {"heic", heic_dec},
    {"jxl",  jxl_dec},
    {"jp2",  jp2_dec}
  };

  floader = NULL;

  if (file_type == IM_FILE_TYPE_AUTO) {
    char * file_ext;
    file_ext = strrchr(localurl, '.');
    if (file_ext) {
      int floader_len;
      int i;

      ++file_ext;
      floader_len = IM_ARRAY_LEN(floaders);
      for (i = 0; i < floader_len; i++) {
        if (strcasecmp(file_ext, floaders[i].fext) == 0) {
          floader = &floaders[i];
          break;
        }
      }
    } else {
      /* TODO */
    }
  } else {
    switch (file_type) {
      case IM_FILE_TYPE_PPM: {
        floader = &floaders[0];
        break;
      }
      case IM_FILE_TYPE_PGM: {
        floader = &floaders[1];
        break;
      }
      case IM_FILE_TYPE_PBM:
        floader = &floaders[3];
        break;
      case IM_FILE_TYPE_JPEG:
        floader = &floaders[4];
        break;
      default:
        *dest = NULL;
        break;
    }
  }

  if (floader)
    _err_no = floader->floader_fn(dest, localurl, &open_conf);
  else
    goto err;

  return _err_no;
err:
  *dest = NULL;
  return IM_ERR;
}

IM_EXPORT
ImResult
im_free(ImImage * __restrict im) {
  if (im->file.mmap) {
    im_unmap(im->file.raw, im->file.size);
  } else if (im->file.mustfree) {
    free(im->file.raw);
  }

  if (im->data.data) {
    free(im->data.data);
  }

  free(im);

  return IM_OK;
}
