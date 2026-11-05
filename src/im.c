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

#include "formats.h"
#include "io/apple/coreimg.h"
#if IM_WITH_JPG
#  include "io/jpg/jpg.h"
#endif
#if IM_WITH_PPM
#  include "io/ppm/ppm.h"
#  include "io/ppm/pgm.h"
#  include "io/ppm/pbm.h"
#  include "io/ppm/pfm.h"
#  include "io/ppm/pam.h"
#endif
#if IM_WITH_BMP
#  include "io/bmp/bmp.h"
#  include "io/bmp/dib.h"
#endif
#if IM_WITH_PNG
#  include "io/png/png.h"
#endif
#if IM_WITH_TGA
#  include "io/tga/tga.h"
#endif
#if IM_WITH_QOI
#  include "io/qoi/qoi.h"
#endif
#if IM_WITH_HEIC
#  include "io/heic/heic.h"
#endif
#if IM_WITH_JXL
#  include "io/jxl/jxl.h"
#endif
#if IM_WITH_JP2
#  include "io/jp2/jp2.h"
#endif

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

/* fast and secure extension hash - uses first 4 chars max */
static inline int hash_ext(const char * __restrict ext) {
  uint32_t h = 0x811C9DC5; /* FNV offset basis */
  for (int i = 0; i < 4 && ext[i]; i++) {
    h ^= (uint8_t)(ext[i] | 0x20);
    h *= 0x01000193;
  }
  
  return (uint8_t)((h ^ (h >> 16)) & 0xFF);
}

typedef ImResult (*imloader)(ImImage**, const char*, im_open_config_t*);

/* static const struct { imloader fn; } extmap[256] = { */
static const imloader extmap[256] = {
#if IM_WITH_JP2
  [0x04] = jp2_dec,         /* jpf  */
#endif
#if IM_WITH_BMP
  [0x06] = dib_dec,         /* dib  */
#endif
#if IM_WITH_PPM
  [0x1F] = ppm_dec,         /* ppm  */
#endif
#if IM_WITH_TGA
  [0x30] = tga_dec,         /* vda  */
#endif
#if IM_WITH_PPM
  [0x3C] = pbm_dec,         /* pbm  */
#endif
#if IM_WITH_HEIC
  [0x54] = heic_dec,        /* heic */
#endif
#if IM_WITH_JP2
  [0x58] = jp2_dec,         /* jp2  */
#endif
#if IM_WITH_JP2
  [0x5A] = jp2_dec,         /* jpx  */
#endif
#if IM_WITH_TGA
  [0x64] = tga_dec,         /* icb  */
#endif
#if IM_WITH_PPM
  [0x68] = pgm_dec,         /* pgm  */
#endif
#if IM_WITH_JP2
  [0x69] = jp2_dec,         /* j2k  */
#endif
#if IM_WITH_BMP
  [0x79] = bmp_dec,         /* bmp  */
#endif
#if IM_WITH_JXL
  [0x7A] = jxl_dec,         /* jxl  */
#endif
#if IM_WITH_QOI
  [0x99] = qoi_dec,         /* qoi  */
#endif
#if IM_WITH_TGA
  [0x9D] = tga_dec,         /* vst  */
#endif
#if IM_WITH_PNG
  [0xA9] = png_dec,         /* png  */
#endif
#if IM_WITH_JP2
  [0xBB] = jp2_dec,         /* jpc  */
#endif
#if IM_WITH_TGA
  [0xEA] = tga_dec,         /* tga  */
#endif
#if IM_WITH_PPM
  [0xF0] = pam_dec,         /* pam  */
#endif
#if IM_WITH_JP2
  [0xF1] = jp2_dec,         /* j2c  */
#endif
#if IM_WITH_JPG
  [0xF7] = jpg_dec,         /* jpg  */
#endif
#if IM_WITH_PPM
  [0xFB] = pfm_dec,         /* pfm  */
#endif
#if IM_WITH_JPG
  [0xFC] = jpg_dec,         /* jpeg */
#endif
};

#if 0
static void print_ext_hash(const char *ext) {
  int h = 0x811C9DC5;
  for (int i = 0; i < 4 && ext[i]; i++) {
    h ^= (uint8_t)ext[i] | 0x20;
    h *= 0x01000193;
  }
  h = (uint8_t)((h ^ (h >> 16)) & 0xFF);
  printf("Extension: %-4s -> Hash: %d (0x%02x)\n", ext, h, h);
}

static void __attribute__((constructor)) verify_hashes(void) {
  printf("Verifying extension hashes:\n");
  print_ext_hash("pbm");
  print_ext_hash("pgm");
  print_ext_hash("ppm");
  print_ext_hash("pfm");
  print_ext_hash("pam");
  print_ext_hash("png");
  print_ext_hash("bmp");
  print_ext_hash("dib");
  print_ext_hash("tga");
  print_ext_hash("tpic");
  print_ext_hash("icb");
  print_ext_hash("vda");
  print_ext_hash("vst");
  print_ext_hash("qoi");
  print_ext_hash("heic");
  print_ext_hash("jpg");
  print_ext_hash("jpeg");
  print_ext_hash("jxl");
  print_ext_hash("jp2");
  print_ext_hash("j2k");
  print_ext_hash("j2c");
  print_ext_hash("jpc");
  print_ext_hash("jpf");
  print_ext_hash("jpx");
  printf("-------------------\n");
}
#endif

IM_EXPORT
ImResult
im_load(ImImage         ** __restrict dest,
        const char       * __restrict url,
        im_option_base_t *            options[],
        ImOpenIntent                  openIntent) {
  const char       *ext;
  imloader          fn;
  int               filetype;
  im_open_config_t  conf;

  if (!url || !dest) return IM_EBADF;

  /* TODO: currently file_type from file ext.  */
  filetype = IM_FILE_TYPE_AUTO;
  conf     = im_config(options, openIntent);

  if (!filetype && (ext=strrchr(url,'.')) && (fn=extmap[hash_ext(ext+1)]))
    return fn(dest, url, &conf);

#ifdef __APPLE__
  /* unknown source; let CoreGraphics/CoreImage decode if it can on Apple */
  return coreimg_dec(dest, url, &conf);
#else
  *dest = NULL;
  return IM_ERR;
#endif
}

