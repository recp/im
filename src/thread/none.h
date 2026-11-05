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
 * No threads (IM_NO_THREADS; WASI built without them): one CPU, no thread
 * ever started, locks that do nothing. A pool runs its items inline.
 */

#ifndef src_thread_none_h
#define src_thread_none_h

#include "common.h"

typedef struct th_thread        { int unused; } th_thread;
typedef struct th_thread_cond   { int unused; } th_thread_cond;
typedef struct th_thread_mutex  { int unused; } th_thread_mutex;
typedef struct th_thread_rwlock { int unused; } th_thread_rwlock;

static inline th_thread* thread_new(void (*func)(void *), void *obj) { (void)func; (void)obj; return NULL; }
static inline void thread_release(th_thread *th) { (void)th; }
static inline void thread_join(th_thread *th) { (void)th; }
static inline void thread_exit(void) { }
static inline uint32_t thread_ncpu(void) { return 1u; }
static inline void thread_cond_init(th_thread_cond *c) { (void)c; }
static inline void thread_cond_signal(th_thread_cond *c) { (void)c; }
static inline void thread_cond_broadcast(th_thread_cond *c) { (void)c; }
static inline void thread_cond_destroy(th_thread_cond *c) { (void)c; }
static inline void thread_cond_wait(th_thread_cond *c, th_thread_mutex *m) { (void)c; (void)m; }
static inline void thread_mutex_init(th_thread_mutex *m) { (void)m; }
static inline void thread_mutex_destroy(th_thread_mutex *m) { (void)m; }
static inline void thread_lock(th_thread_mutex *m) { (void)m; }
static inline void thread_unlock(th_thread_mutex *m) { (void)m; }
static inline void thread_rwlock_init(th_thread_rwlock *l) { (void)l; }
static inline void thread_rwlock_destroy(th_thread_rwlock *l) { (void)l; }
static inline void thread_rwunlock(th_thread_rwlock *l) { (void)l; }
static inline void thread_rdlock(th_thread_rwlock *l) { (void)l; }
static inline void thread_wrlock(th_thread_rwlock *l) { (void)l; }

#endif /* src_thread_none_h */
