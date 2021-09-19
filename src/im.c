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

#include "color.h"
#include "sampler.h"

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
  ImByte       *p, *pi;
  int16_t      *p2, pix;
  bool          started;
  
  started = false;
  arg     = argv;
  jpg     = arg->jpg;
  im      = arg->image;
  
  thread_lock(&jpg->wrkmutex);

  while (!started || jpg->nScans > 0) {
    ImThreadedBlock *blk;

    thread_cond_wait(&jpg->cond, &jpg->wrkmutex);

    if (jpg->failed)
      break;

    if (!(im = jpg->im))
      continue;
    
    int row, col, width, xi, yi, k, Ns, stride;
    
    /* consume avail blocks */
    for (;;) {
      blk = &jpg->blkpool[jpg->wrk_index];
      if (blk->avail)
        break;

      thread_lock(&blk->mutex);

      stride = 3;
      row   = blk->mcuy * 8;
      col   = blk->mcux * 8;
      width = jpg->frm.width;
      p     = ((ImByte *)im->data);
      Ns    = jpg->scan->Ns;
      xi    = blk->xi;
      yi    = blk->yi;

      for (k = 0; k < Ns; k++) {
        int Hi, Vi, samplerClass;

        Hi = jpg->frm.hmax / blk->sf[k].H;
        Vi = jpg->frm.vmax / blk->sf[k].V;
        samplerClass = Hi << 1 | Vi;

        for (int v = 0; v < blk->sf[k].V; v++) {
          for (int h = 0; h < blk->sf[k].H; h++) {
            p2 = blk->blk[v][h][k].blk;
            for (int i = 0; i < yi; i++) {
              for (int j = 0; j < xi; j++) {
                pix = *p2++;
                pi  = &p[3 * ((row + i * Vi + v * 8) * width + (col + j * Hi + h * 8)) + k];
                switch (samplerClass) {
                  case IM_SAMPLER_11:
                    p[3 * ((row + i * Vi + v * 8) * width + (col + j * Hi + h * 8)) + k] = pix;
                    break;
                  default:
                    /* horizontal */
                    for (int sh = 0; sh < Hi; sh++) {
                      pi[Ns * sh] = pix;
                    }
                    
                    /* vertical */
                    for (int sv = 1; sv < Vi; sv++) {
                      pi[width*3 * sv]      = pix;
                      pi[width*3 * sv + Ns] = pix;
                    }
                    break;
                }

//                switch (samplerClass) {
//                  case IM_SAMPLER_11:
//                    p[3 * ((row + i * Vi + v * 8) * width + (col + j * Hi + h * 8)) + k] = pix;
//                    break;
//                  case IM_SAMPLER_22:
//                    im_upsample8_2x2(pix,
//                                     &p[3 * ((row + i * Vi + v * 8) * width + (col + j * Hi + h * 8)) + k],
//                                     3,
//                                     width * 3);
//                    break;
//                  default:
//                    printf("olamazzzz\n");
//                    break;
//
//
//                }
              } /* for j */
            } /* for i */
          } /* for h */
        } /* for v */
      } /* for k */

      if (++jpg->wrk_index > 2)
        jpg->wrk_index = 0;
      
      blk->avail = true;
      thread_unlock(&blk->mutex);
      
      /* thread_cond_signal(&jpg->dec_cond); */
    }

    started = true;
  }

  thread_unlock(&jpg->wrkmutex);

  if (im) {
    im_YCbCrToRGB(jpg->im->data, im->width, im->height);
  }

//  im_resample(jpg->im->data,
//              im->width,
//              im->height,
//              (ImSampleFactor[]){
//                jpg->frm.compo[0].sf,
//                jpg->frm.compo[1].sf,
//                jpg->frm.compo[2].sf
//              });
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
  thread_cond_init(&jpg->dec_cond);
  thread_mutex_init(&jpg->wrkmutex);
  thread_mutex_init(&jpg->decmutex);
  
  jpg->blkpool[0].avail = true;
  jpg->blkpool[1].avail = true;
  jpg->blkpool[2].avail = true;
  
  thread_mutex_init(&jpg->blkpool[0].mutex);
  thread_mutex_init(&jpg->blkpool[1].mutex);
  thread_mutex_init(&jpg->blkpool[2].mutex);

  scan_worker = thread_new(im_on_worker, &arg);
  idct_worker = thread_new(im_on_worker_idct, &arg);

  thread_join(scan_worker);
  thread_cond_signal(&jpg->cond);
  thread_join(idct_worker);

  thread_release(scan_worker);
  thread_release(idct_worker);
  
  return arg.image;
}
