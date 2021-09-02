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

#include "jpg/dec/dec.h"

#include "thread/thread.h"

typedef struct worker_arg_t {
  const char *path;
  ImImage    *image;
  ImJpeg     *jpg;
  bool        failed;
} worker_arg_t;

IM_HIDE
void
im_on_worker(void *argv) {
  worker_arg_t *arg;
  ImByte       *raw;
  FILE         *infile;
  ImImage      *im;
  ImJpeg       *jpg;
  size_t        blksize;
  size_t        fsize;
  size_t        fcontents_size;
  size_t        total_read;
  size_t        nread;
  struct stat   infile_st;
  int           infile_no;
  
  arg = argv;

  infile = fopen(arg->path, "rb");
  if (!infile)
    goto err;

  infile_no = fileno(infile);

  if (fstat(infile_no, &infile_st) != 0)
    goto err;

#ifndef _MSC_VER
  blksize = infile_st.st_blksize;
#else
  blksize = 512;
#endif

  fsize          = infile_st.st_size;
  fcontents_size = sizeof(char) * fsize;

  raw  = malloc(fcontents_size + 1);

  assert(raw && "malloc failed");

  memset(raw + fcontents_size, '\0', 1);

  total_read = 0;

  do {
    if ((fcontents_size - total_read) < blksize)
      blksize = fcontents_size - total_read;

    nread = fread(raw + total_read,
                  sizeof(char),
                  blksize,
                  infile);

    total_read += nread;
  } while (nread > 0 && total_read < fsize);

  fclose(infile);

  /* decode, this process will be optimized after decoding is done */
  im      = calloc(1, sizeof(*im));
  jpg     = arg->jpg;
  jpg->im = im;

  arg->image = im;

  jpg_dec(jpg, raw);

err:
  arg->failed = true;
}

IM_HIDE
void
im_on_worker_idct(void *argv) {
  worker_arg_t *arg;
  ImImage      *im;
  ImJpeg       *jpg;
  
  arg = argv;
  jpg = arg->jpg;
  im  = arg->image;
  
  thread_lock(&jpg->mutex);

  while (!jpg->finished) {
    thread_cond_wait(&jpg->cond, &jpg->mutex);
    
    printf("JPEG IDCT\n");

    thread_rdlock(&jpg->rwlock);
   
    thread_rwunlock(&jpg->rwlock);
  }

  thread_unlock(&jpg->mutex);
}

IM_EXPORT
ImImage*
im_load(const char * __restrict path) {
  ImJpeg      *jpg;
  th_thread   *scan_worker, *idct_worker;
  worker_arg_t arg;

  jpg       = calloc(1, sizeof(*jpg));
  arg.path  = path;
  arg.jpg   = jpg;
  arg.image = NULL;

  thread_cond_init(&jpg->cond);
  thread_mutex_init(&jpg->mutex);
  thread_rwlock_init(&jpg->rwlock);
  
  scan_worker = thread_new(im_on_worker, &arg);
  idct_worker = thread_new(im_on_worker_idct, &arg);

  thread_join(scan_worker);
  thread_join(idct_worker);

  thread_release(scan_worker);
  thread_release(idct_worker);

  return arg.image;
}
