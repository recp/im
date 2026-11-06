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

#ifndef mem_mmap_h
#define mem_mmap_h

#include "../common.h"

IM_HIDE
void*
im_mmap_rdonly(int fd, size_t size);

IM_HIDE
void
im_unmap(void *file, size_t size);

/* A big image's working memory (planes, coefficients, kept blocks) where
   the decoder calls it big: mapped from the system, zeros, its pages
   given back as its parts are done with (the whole pages inside a range;
   what they held gone) and the moment it's let go, so the image's peak is
   little more than its pixels (an allocator keeps freed big blocks for
   later, counted against the process). Not big, or no such call: the
   allocator's, nothing given back (its pages used again by the next
   image, not cleared again by the system: quicker where memory isn't the
   worry). Let go with the same size and say */
#if (defined(__APPLE__) || defined(__linux__)) && !defined(IM_NO_RELEASE)
#  include <sys/mman.h>
/* (a strict C build may hide them: then none) */
#  if defined(MADV_DONTNEED) && (defined(MAP_ANON) || defined(MAP_ANONYMOUS))
#    define IM_RELEASE 1
#    ifndef MAP_ANON
#      define MAP_ANON MAP_ANONYMOUS
#    endif
#  endif
#endif
#ifndef IM_RELEASE
#  define IM_RELEASE 0
#endif

enum { IM_RELEASE_PAGE = 16384u };

IM_INLINE void
im_release(bool big, void *p, size_t len) {
#if IM_RELEASE
  uintptr_t a = ((uintptr_t)p + IM_RELEASE_PAGE - 1u) & ~(uintptr_t)(IM_RELEASE_PAGE - 1u);
  uintptr_t b = ((uintptr_t)p + len) & ~(uintptr_t)(IM_RELEASE_PAGE - 1u);

  if (!big || b <= a) return;
#  if defined(MADV_FREE_REUSABLE)
  (void)madvise((void *)a, b - a, MADV_FREE_REUSABLE); /* out of the footprint now */
#  else
  (void)madvise((void *)a, b - a, MADV_DONTNEED);
#  endif
#else
  (void)big;
  (void)p;
  (void)len;
#endif
}

IM_INLINE void*
im_big_alloc(size_t n, bool zero, bool big) {
#if IM_RELEASE
  if (big && n) {
    void *p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

    return p == MAP_FAILED ? NULL : p;
  }
#endif
  (void)big;
  return zero ? calloc(1u, n ? n : 1u) : malloc(n ? n : 1u);
}

IM_INLINE void
im_big_free(void *p, size_t n, bool big) {
  if (!p) return;
#if IM_RELEASE
  if (big && n) {
    (void)munmap(p, n);
    return;
  }
#endif
  (void)n;
  (void)big;
  free(p);
}

/* grown from `old` bytes to `n`, what it held kept; NULL (`p` let go):
   no memory */
IM_INLINE void*
im_big_grow(void *p, size_t old, size_t n, bool big) {
  void *q;

  if (IM_RELEASE && big) {
    if ((q = im_big_alloc(n, false, true)) && p) memcpy(q, p, old < n ? old : n);
    im_big_free(p, old, true);
    return q;
  }
  if (!(q = realloc(p, n))) free(p);
  return q;
}

#endif /* mem_mmap_h */
