/*
 * Copyright (C) 2026 Recep Aslantas
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
 * A decode's worker threads: items [0, n) of a job handed out in turn,
 * the caller working too; a run ends when every item is done. Items are
 * meant to be large (a band of rows, a restart interval or more), so the
 * lock is taken twice an item at most.
 */

#ifndef src_thread_pool_h
#define src_thread_pool_h

#include "../common.h"

enum { IM_POOL_MAX = 16 }; /* threads a pool, the caller's among them */

/* `worker` 0 (the caller) to im_pool_size() - 1: its own scratch */
typedef bool (*im_job_fn)(void *ctx, uint32_t worker, uint32_t item);

typedef struct im_pool im_pool_t;

/* up to `threads` (the caller's counted, at most the CPUs); NULL where
   there'd be none besides the caller's */
IM_HIDE im_pool_t *im_pool_new(uint32_t threads);
IM_HIDE uint32_t   im_pool_size(const im_pool_t *p);

/* `fn` for items [0, n); false if any item's was. With no pool, on the
   caller alone */
IM_HIDE bool im_pool_run(im_pool_t *p, im_job_fn fn, void *ctx, uint32_t n);
IM_HIDE void im_pool_free(im_pool_t *p);

#endif /* src_thread_pool_h */
