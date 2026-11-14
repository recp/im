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

#include "mmap.h"
#include "../common.h"

/* WASI has no mmap: the file is read instead (file.c) */
#if defined(__wasi__)
#  define IM_NO_MMAP 1
#elif !defined(IM_WINAPI)
#  include <sys/mman.h>
#else
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <io.h>
#endif

#if IM_RELEASE
#  include <pthread.h>

/* blocks let go, kept for the next image's (mmap.h). IM_KEPT_WARM bytes of
   them stay warm, in the footprint: no madvise either way (a small
   image's decode spends more in those calls than in much of its work);
   the rest out of it while kept */
enum { IM_KEPT_COUNT = 16u };
#  define IM_KEPT_BYTES ((size_t)64u << 20)
#  ifndef IM_KEPT_WARM
#    define IM_KEPT_WARM ((size_t)16u << 20)
#  endif

typedef struct im_kept_t {
  uint8_t *p;
  size_t   n;
  bool     warm;
} im_kept_t;

static pthread_mutex_t im_kept_lock = PTHREAD_MUTEX_INITIALIZER;
static im_kept_t       im_kept[IM_KEPT_COUNT];
static uint32_t        im_kept_count;
static size_t          im_kept_bytes, im_warm_bytes;

IM_HIDE
void*
im_kept_take(size_t n) {
  im_kept_t got = {NULL, 0u, false}, rest = {NULL, 0u, false};
  uint32_t  i, best = IM_KEPT_COUNT;

  pthread_mutex_lock(&im_kept_lock);
  for (i = 0u; i < im_kept_count; i++) {
    if (im_kept[i].n >= n && (best == IM_KEPT_COUNT || im_kept[i].n < im_kept[best].n)) best = i;
  }
  if (best != IM_KEPT_COUNT) {
    got             = im_kept[best];
    im_kept[best]   = im_kept[--im_kept_count];
    im_kept_bytes  -= got.n;
    if (got.warm) im_warm_bytes -= got.n;
    if (got.n > n) { /* the rest kept apart, or let go when too small to keep */
      rest = (im_kept_t){got.p + n, got.n - n, got.warm};
      if (rest.n >= IM_KEPT_MIN) {
        im_kept[im_kept_count++] = rest;
        im_kept_bytes           += rest.n;
        if (rest.warm) im_warm_bytes += rest.n;
        rest                     = (im_kept_t){NULL, 0u, false};
      }
    }
  }
  pthread_mutex_unlock(&im_kept_lock);
  if (rest.p) (void)munmap(rest.p, rest.n);
  if (!got.p) return NULL;
#  if defined(MADV_FREE_REUSE)
  if (!got.warm) (void)madvise(got.p, n, MADV_FREE_REUSE); /* back in the footprint */
#  endif
  return got.p;
}

IM_HIDE
void
im_kept_give(void *p, size_t n) {
  im_kept_t gone[IM_KEPT_COUNT];
  uint32_t  i, smallest, count = 0u;
  bool      kept = false, warm;

  if (n <= IM_KEPT_BYTES) {
    /* warm while there's room for it (its bytes counted now, before
       another thread can take it); else out of the footprint first */
    pthread_mutex_lock(&im_kept_lock);
    warm = n <= IM_KEPT_WARM - im_warm_bytes;
    if (warm) im_warm_bytes += n;
    pthread_mutex_unlock(&im_kept_lock);
#  if defined(MADV_FREE_REUSABLE)
    if (!warm) (void)madvise(p, n, MADV_FREE_REUSABLE); /* out of the footprint while kept */
#  endif
    pthread_mutex_lock(&im_kept_lock);
    /* smaller ones let go to make room */
    while (im_kept_count && (im_kept_count == IM_KEPT_COUNT || n > IM_KEPT_BYTES - im_kept_bytes)) {
      for (i = 1u, smallest = 0u; i < im_kept_count; i++) {
        if (im_kept[i].n < im_kept[smallest].n) smallest = i;
      }
      if (im_kept[smallest].n >= n) break;
      gone[count++]      = im_kept[smallest];
      im_kept_bytes     -= im_kept[smallest].n;
      if (im_kept[smallest].warm) im_warm_bytes -= im_kept[smallest].n;
      im_kept[smallest]  = im_kept[--im_kept_count];
    }
    if (im_kept_count < IM_KEPT_COUNT && n <= IM_KEPT_BYTES - im_kept_bytes) {
      im_kept[im_kept_count++] = (im_kept_t){p, n, warm};
      im_kept_bytes           += n;
      kept                     = true;
    } else if (warm) {
      im_warm_bytes -= n;
    }
    pthread_mutex_unlock(&im_kept_lock);
  }
  for (i = 0u; i < count; i++) (void)munmap(gone[i].p, gone[i].n);
  if (!kept) (void)munmap(p, n);
}
#endif

IM_HIDE
void*
im_mmap_rdonly(int fd, size_t size) {
  void *mapped;
  
  mapped = NULL;
  
#if IM_NO_MMAP
  IM__UNUSED(fd);
  IM__UNUSED(size);
#elif !defined(IM_WINAPI)
  mapped = mmap(0, size, PROT_READ, MAP_SHARED, fd, 0);
  if (!mapped || mapped == MAP_FAILED)
    return NULL;
  
  madvise(mapped, size, MADV_SEQUENTIAL);
#else
  HANDLE hmap;
  if (!((hmap = CreateFileMapping((HANDLE)_get_osfhandle(fd), 0, PAGE_READONLY, 0, 0, 0))
        && (mapped = MapViewOfFileEx(hmap, FILE_MAP_READ, 0, 0, size, 0))))
    return NULL;
  
  CloseHandle(hmap);
#endif
  
  return mapped;
}

IM_HIDE
void
im_unmap(void *file, size_t size) {
#if IM_NO_MMAP
  IM__UNUSED(file);
  IM__UNUSED(size);
#elif !defined(IM_WINAPI)
  munmap(file, size);
#else
  IM__UNUSED(size);
  UnmapViewOfFile(file);
#endif
}
