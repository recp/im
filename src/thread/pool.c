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

#include "pool.h"

typedef struct im_worker {
  struct im_pool *p;
  uint32_t        id;
  th_thread      *thread;
} im_worker_t;

struct im_pool {
  th_thread_mutex m;
  th_thread_cond  go, idle;
  im_job_fn       fn;
  void           *ctx;
  uint32_t        items, next, busy, round, nworkers;
  bool            quit, failed;
  im_worker_t     worker[IM_POOL_MAX - 1];
  im_runner_t     ext;      /* the caller's (ext.run): no thread here */
};

static void
pool_main(void *arg) {
  im_worker_t *w = arg;
  im_pool_t   *p = w->p;
  uint32_t     seen = 0;

  thread_lock(&p->m);
  for (;;) {
    while (!p->quit && p->round == seen) thread_cond_wait(&p->go, &p->m);
    if (p->quit) break;
    seen = p->round;
    p->busy++;
    while (p->next < p->items) {
      uint32_t i = p->next++;
      bool     ok;

      thread_unlock(&p->m);
      ok = p->fn(p->ctx, w->id, i);
      thread_lock(&p->m);
      if (!ok) p->failed = true;
    }
    if (!--p->busy) thread_cond_broadcast(&p->idle);
  }
  thread_unlock(&p->m);
}

IM_HIDE im_pool_t*
im_pool_new(uint32_t threads) {
  im_pool_t *p;
  uint32_t   cpus = thread_ncpu(), n, i;

  if (threads > IM_POOL_MAX) threads = IM_POOL_MAX;
  if (threads > cpus) threads = cpus;
  if (threads < 2u || !(p = calloc(1, sizeof(*p)))) return NULL;
  n = threads - 1u;
  thread_mutex_init(&p->m);
  thread_cond_init(&p->go);
  thread_cond_init(&p->idle);
  for (i = 0; i < n; i++) {
    p->worker[i].p  = p;
    p->worker[i].id = i + 1u;
    if (!(p->worker[i].thread = thread_new(pool_main, &p->worker[i]))) break;
  }
  p->nworkers = i;
  if (!i) {
    im_pool_free(p);
    return NULL;
  }
  return p;
}

IM_HIDE im_pool_t*
im_pool_wrap(const im_runner_t *r) {
  im_pool_t *p;

  if (!r || !r->run || r->workers < 2u || r->workers > IM_POOL_MAX || !(p = calloc(1, sizeof(*p)))) return NULL;
  p->ext = *r;
  return p;
}

IM_HIDE uint32_t
im_pool_size(const im_pool_t *p) {
  return !p ? 1u : p->ext.run ? p->ext.workers : p->nworkers + 1u;
}

IM_HIDE bool
im_pool_run(im_pool_t *p, im_job_fn fn, void *ctx, uint32_t n) {
  bool     ok = true;
  uint32_t i;

  if (!p || n < 2u) {
    for (i = 0; i < n; i++) ok = fn(ctx, 0u, i) && ok;
    return ok;
  }
  if (p->ext.run) return p->ext.run(p->ext.ctx, fn, ctx, n);
  thread_lock(&p->m);
  p->fn     = fn;
  p->ctx    = ctx;
  p->items  = n;
  p->next   = 0;
  p->failed = false;
  p->round++;
  thread_cond_broadcast(&p->go);
  while (p->next < p->items) {
    i = p->next++;
    thread_unlock(&p->m);
    ok = fn(ctx, 0u, i) && ok;
    thread_lock(&p->m);
  }
  while (p->busy) thread_cond_wait(&p->idle, &p->m);
  ok = ok && !p->failed;
  thread_unlock(&p->m);
  return ok;
}

IM_HIDE void
im_pool_free(im_pool_t *p) {
  uint32_t i;

  if (!p) return;
  if (p->ext.run) {
    free(p);
    return;
  }
  if (p->nworkers) {
    thread_lock(&p->m);
    p->quit = true;
    thread_cond_broadcast(&p->go);
    thread_unlock(&p->m);
    for (i = 0; i < p->nworkers; i++) {
      thread_join(p->worker[i].thread);
      thread_release(p->worker[i].thread);
    }
  }
  thread_cond_destroy(&p->idle);
  thread_cond_destroy(&p->go);
  thread_mutex_destroy(&p->m);
  free(p);
}
