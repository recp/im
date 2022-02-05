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

#include "io/jpg/dec/dec.h"
#include "io/ppm/ppm.h"
#include "io/ppm/pgm.h"
#include "io/ppm/pbm.h"
#include "io/ppm/pfm.h"
#include "io/ppm/pam.h"

#include "file.h"

typedef struct floader_t {
  const char * fext;
  ImResult (*floader_fn)(ImImage ** __restrict, const char * __restrict);
} floader_t;

IM_EXPORT
ImResult
im_load(ImImage ** __restrict dest, const char * __restrict url, ...) {
  floader_t  *floader;
  const char *localurl;
  int         file_type;
  int         _err_no;

  va_list pref_args;
  va_start(pref_args, url);
  file_type = va_arg(pref_args, int);
  va_end(pref_args);

  /* TODO: remote or another kind URL than file URL ? */
  localurl = url;
  if (!localurl)
    return IM_EBADF;

  floader_t floaders[] = {
    {"pbm",  pbm_dec},
    {"pgm",  pgm_dec},
    {"ppm",  ppm_dec},
    {"pfm",  pfm_dec},
    {"pam",  pam_dec},
    {"jpeg", jpg_dec},
    {"jpg",  jpg_dec}
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
        if (strcmp(file_ext, floaders[i].fext) == 0) {
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
    _err_no = floader->floader_fn(dest, localurl);
  else
    goto err;

  return _err_no;
err:
  *dest = NULL;
  return IM_ERR;
}
