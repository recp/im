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

#include "dec.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

/* file formats */
#include "jfif/jfif.h"
#include "exif/exif.h"

#include "../../../file.h"

typedef struct worker_arg_t {
  const char  *path;
  ImImage     *image;
  ImJpeg      *jpg;
  bool         failed;
  ImFileResult fres;
} worker_arg_t;

IM_HIDE
void
jpg_dec_start(ImJpeg *jpg, ImByte *raw) {
  ImByte   *pRaw;
  JPGMarker mrk;

  pRaw = raw;

  /* No jpeg */
  if (!jpg_marker_eq(pRaw, JPG_SOI))
    return;

  pRaw += JPP_MARKER_SIZE;

  /* Find APPn(0) */
  mrk   = jpg_marker(pRaw);
  pRaw += JPP_MARKER_SIZE;

  switch (mrk) {
    /* we have found JFIF file */
    case JPG_APPn(0):
      jfif_dec(pRaw, jpg);
      break;
    /* we have found EXIF file */
    case JPG_APPn(1):
      exif_dec(pRaw, jpg);
      break;
    default:
      assert("unsupported file!");
      break;
  }
}

IM_HIDE
void
im_on_worker(void *argv) {
  worker_arg_t *arg;
  ImImage      *im;
  ImJpeg       *jpg;
  ImFileResult  fres;
  
  arg  = argv;
  fres = im_readfile(arg->path, im->openIntent != IM_OPEN_INTENT_READWRITE);
  
  if (fres.ret != IM_OK) {
    goto err;
  }
  
  /* decode, this process will be optimized after decoding is done */
  im         = calloc(1, sizeof(*im));
  im->format = IM_FORMAT_RGB; /* TODO: override this in frame header */
  jpg        = arg->jpg;
  jpg->im    = im;
  
  arg->image = im;
  arg->fres  = fres;
  
  jpg_dec_start(jpg, fres.raw);
  
  return;

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
    
    int row, col, width, height, xi, yi, k, Ns;
    
    /* consume avail blocks */
    for (;;) {
      blk = &jpg->blkpool[jpg->wrk_index];
      if (blk->avail)
        break;
      
      thread_lock(&blk->mutex);
      
      width  = jpg->frm.width;
      height = jpg->frm.height;
      p      = ((ImByte *)im->data.data);
      Ns     = jpg->scan->Ns;
      
      for (k = 0; k < Ns; k++) {
        int Hi, Vi, V, H, hmax, vmax, samplerClass;
        
        hmax         = jpg->frm.hmax;
        vmax         = jpg->frm.vmax;
        H            = blk->sf[k].H;
        V            = blk->sf[k].V;
        Hi           = jpg->frm.hmax / H;
        Vi           = jpg->frm.vmax / V;
        samplerClass = Hi << 1 | Vi;
        
        yi           = im_min_i32(8, (height - blk->mcuy * 8 * vmax) * V / vmax);
        xi           = im_min_i32(8, (width  - blk->mcux * 8 * hmax) * H / hmax);
        row          = blk->mcuy * 8 * V;
        col          = blk->mcux * 8 * H;
        
        H            = im_max_i32(1, H * (float)im_min_i32((width  - blk->mcux * 8 * hmax), hmax * 8) / (hmax * 8));
        V            = im_max_i32(1, V * (float)im_min_i32((height - blk->mcuy * 8 * vmax), vmax * 8) / (vmax * 8));
        
        for (int v = 0; v < V; v++) {
          for (int h = 0; h < H; h++) {
            p2 = blk->blk[v][h][k].blk;
            for (int i = 0; i < yi; i++) {
              for (int j = 0; j < xi; j++) {
                pix = *p2++;
                pi  = &p[Ns * (((row + i) * Vi + v * 8) * width
                               + ((col + j) * Hi + h * 8))
                         + k];
                
                switch (samplerClass) {
                  case IM_SAMPLER_11:
                    pi[0] = pix;
                    break;
                  default:
                    /* horizontal */
                    for (int sh = 0; sh < Hi; sh++) {
                      pi[Ns * sh] = pix;
                    }
                    
                    /* vertical */
                    for (int sv = 1; sv < Vi; sv++) {
                      pi[width * Ns * sv]      = pix;
                      pi[width * Ns * sv + Ns] = pix;
                    }
                    break;
                }
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
    im_YCbCrToRGB(jpg->im->data.data, im->width, im->height);
  }
}

IM_HIDE
ImResult
jpg_dec(ImImage         ** __restrict dest,
        const char       * __restrict path,
        im_open_config_t * __restrict open_config) {
  ImJpeg      *jpg;
  th_thread   *scan_worker, *idct_worker;
  worker_arg_t arg;
  
  memset(&arg, 0, sizeof(arg));

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
  
  if (!arg.failed) {
    idct_worker = thread_new(im_on_worker_idct, &arg);
    thread_join(scan_worker);
    thread_cond_signal(&jpg->cond);
    thread_join(idct_worker);
  } else {
    idct_worker = NULL;
  }
  
  thread_release(scan_worker);
  thread_release(idct_worker);
  
  if (arg.fres.mmap) {
    im_unmap(arg.fres.raw, arg.fres.size);
  }
  
  *dest = arg.image;

  return IM_OK;
}
