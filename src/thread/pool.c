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

#if !IM_NO_THREADS && !defined(_WIN32)
#  include <unistd.h>
#endif

/* items taken by one atomic add each (many small ones: code-blocks); kept
   pools by one exchange */
#if defined(_MSC_VER) && !defined(__clang__)
#  include <intrin.h>
#  define P_TAKE(p)       ((uint32_t)_InterlockedExchangeAdd((volatile long *)(p), 1))
#  define P_RESET(p)      ((void)_InterlockedExchange((volatile long *)(p), 0))
#  define P_SWAP(p, v)    ((im_pool_t *)_InterlockedExchangePointer((void *volatile *)(p), (v)))
#  define P_LOAD(p)       (*(volatile uint32_t *)(p))
#  define P_STORE(p, v)   ((void)(*(volatile uint32_t *)(p) = (v)))
#else
#  define P_TAKE(p)       __atomic_fetch_add((p), 1u, __ATOMIC_RELAXED)
#  define P_RESET(p)      __atomic_store_n((p), 0u, __ATOMIC_RELAXED)
#  define P_SWAP(p, v)    __atomic_exchange_n((p), (v), __ATOMIC_ACQ_REL)
#  define P_LOAD(p)       __atomic_load_n((p), __ATOMIC_RELAXED)
#  define P_STORE(p, v)   __atomic_store_n((p), (v), __ATOMIC_RELAXED)
#endif

/* a pool given back is kept for the next decode that wants threads
   (making and joining them costs tens of µs, a mid-sized image's decode a
   few hundred): IM_POOL_KEEP at most, their threads waiting, not running;
   one taken runs as many of its threads as asked (the others not woken),
   and grows where asked for more. 0: none kept */
#ifndef IM_POOL_KEEP
#  define IM_POOL_KEEP 2u
#endif

typedef struct im_worker {
  struct im_pool *p;
  uint32_t        id, seen;
  th_thread      *thread;
  th_thread_cond  go;       /* this worker's: woken alone */
} im_worker_t;

struct im_pool {
  th_thread_mutex m;
  th_thread_cond  idle;
  im_job_fn       fn;
  void           *ctx;
  uint32_t        items, next, busy, round, nworkers;
  uint32_t        active;   /* the workers this decode runs (1 … active) */
  bool            quit, failed;
  long            pid;      /* the process that made its threads (a fork's
                               child has none of them) */
  im_worker_t     worker[IM_POOL_MAX - 1];
  im_runner_t     ext;      /* the caller's (ext.run): no thread here */
};

static im_pool_t *pool_kept[IM_POOL_KEEP ? IM_POOL_KEEP : 1];

static long
pool_pid(void) {
#if !IM_NO_THREADS && !defined(_WIN32)
  return (long)getpid();
#else
  return 0;
#endif
}

static void
pool_main(void *arg) {
  im_worker_t *w = arg;
  im_pool_t   *p = w->p;

  thread_lock(&p->m);
  for (;;) {
    im_job_fn fn;
    void     *ctx;
    uint32_t  items, i;
    bool      ok = true;

    while (!p->quit && (p->round == w->seen || w->id > p->active)) thread_cond_wait(&w->go, &p->m);
    if (p->quit) break;
    w->seen = p->round;
    fn      = p->fn;
    ctx     = p->ctx;
    items   = p->items;
    p->busy++;
    thread_unlock(&p->m);
    while ((i = P_TAKE(&p->next)) < items) ok = fn(ctx, w->id, i) && ok;
    thread_lock(&p->m);
    if (!ok) p->failed = true;
    if (!--p->busy) thread_cond_broadcast(&p->idle);
  }
  thread_unlock(&p->m);
}

/* workers up to n (no run going on): false where none could be made */
static bool
pool_grow(im_pool_t *p, uint32_t n) {
  uint32_t i = p->nworkers;

  for (; i < n; i++) {
    im_worker_t *w = &p->worker[i];

    w->p    = p;
    w->id   = i + 1u;
    w->seen = p->round; /* no round of its own to catch up on */
    thread_cond_init(&w->go);
    if (!(w->thread = thread_new(pool_main, w))) {
      thread_cond_destroy(&w->go);
      break;
    }
  }
  p->nworkers = i;
  return i != 0;
}

static void
pool_end(im_pool_t *p) {
  uint32_t i;

  if (p->nworkers) {
    thread_lock(&p->m);
    p->quit = true;
    for (i = 0; i < p->nworkers; i++) thread_cond_signal(&p->worker[i].go);
    thread_unlock(&p->m);
    for (i = 0; i < p->nworkers; i++) {
      thread_join(p->worker[i].thread);
      thread_release(p->worker[i].thread);
      thread_cond_destroy(&p->worker[i].go);
    }
  }
  thread_cond_destroy(&p->idle);
  thread_mutex_destroy(&p->m);
  free(p);
}

#if !IM_NO_THREADS && !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
/* the kept pools' threads ended at exit, or as the library is unloaded
   (they'd wait in its code). Not on Windows: there a DLL's threads can't
   be joined as it's unloaded */
__attribute__((destructor)) static void
pool_drain(void) {
  uint32_t k;

  for (k = 0; k < IM_POOL_KEEP; k++) {
    im_pool_t *p = P_SWAP(&pool_kept[k], NULL);

    if (p && p->pid == pool_pid()) pool_end(p);
  }
}
#endif

IM_HIDE im_pool_t*
im_pool_new(uint32_t threads) {
  static uint32_t ncpu;
  im_pool_t      *p = NULL;
  uint32_t        k, cpus = P_LOAD(&ncpu);

  if (!cpus) P_STORE(&ncpu, cpus = thread_ncpu()); /* asked once */
  if (threads > IM_POOL_MAX) threads = IM_POOL_MAX;
  if (threads > cpus) threads = cpus;
  if (threads < 2u) return NULL;

  /* a kept one, its threads if this process made them (a fork's child:
     lost with the fork, the pool too) */
  for (k = 0; k < IM_POOL_KEEP && !p; k++) {
    if ((p = P_SWAP(&pool_kept[k], NULL)) && p->pid != pool_pid()) p = NULL;
  }
  if (!p) {
    if (!(p = calloc(1, sizeof(*p)))) return NULL;
    p->pid = pool_pid();
    thread_mutex_init(&p->m);
    thread_cond_init(&p->idle);
  }
  if (p->nworkers < threads - 1u && !pool_grow(p, threads - 1u)) {
    pool_end(p);
    return NULL;
  }
  thread_lock(&p->m); /* its threads read it as they wait */
  p->active = threads - 1u < p->nworkers ? threads - 1u : p->nworkers;
  thread_unlock(&p->m);
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
  return !p ? 1u : p->ext.run ? p->ext.workers : p->active + 1u;
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
  /* a worker late for the last round may still hold its job: done with it
     first, so the counter it reads is that round's */
  while (p->busy) thread_cond_wait(&p->idle, &p->m);
  p->fn     = fn;
  p->ctx    = ctx;
  p->items  = n;
  p->failed = false;
  P_RESET(&p->next);
  p->round++;
  /* no more woken than there are items for besides the caller's */
  for (i = 0; i < p->active && i + 1u < n; i++) thread_cond_signal(&p->worker[i].go);
  thread_unlock(&p->m);
  while ((i = P_TAKE(&p->next)) < n) ok = fn(ctx, 0u, i) && ok;
  thread_lock(&p->m);
  while (p->busy) thread_cond_wait(&p->idle, &p->m);
  ok = ok && !p->failed;
  thread_unlock(&p->m);
  return ok;
}

IM_HIDE void
im_pool_free(im_pool_t *p) {
  uint32_t k;

  if (!p) return;
  if (p->ext.run) {
    free(p);
    return;
  }
  /* a fork's child: the threads aren't there to join */
  if (p->pid != pool_pid()) return;
  /* kept first, the ones kept before moved down a place; the one pushed
     out past the last ended (a fork's parent's: left) */
  for (k = 0; k < IM_POOL_KEEP && p; k++) p = P_SWAP(&pool_kept[k], p);
  if (p && p->pid == pool_pid()) pool_end(p);
}
