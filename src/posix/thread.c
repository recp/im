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

#include "thread.h"

#include <unistd.h>

/* WASI built without threads (no -pthread): libc's pthread stubs, one CPU,
   so no pool starts one */
#if defined(__wasi__) && !defined(_REENTRANT)
#  define TH_NONE 1
#endif

typedef struct th_thread_entry {
  void *arg;
  void (*func)(void *);
} th_thread_entry;

static
void*
thread_entry(void *arg) {
  th_thread_entry entry;

  /* to call free before calling thread func */
  memcpy(&entry, arg, sizeof(entry));
  free(arg);

  entry.func(entry.arg);

  return NULL;
}

TH_HIDE
th_thread*
thread_new(void (func)(void *), void *obj) {
  th_thread       *th;
  th_thread_entry *entry;
  pthread_attr_t   attr;

  th    = calloc(1, sizeof(*th));
  entry = calloc(1, sizeof(*entry));

  if (!th || !entry)
    goto err;

  entry->func = func;
  entry->arg  = obj;

  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

  if (pthread_create(&th->id, &attr, thread_entry, entry) != 0) {
    pthread_attr_destroy(&attr);
    goto err;
  }

  pthread_attr_destroy(&attr);

  return th;

err:
  free(entry);
  free(th);
  return NULL;
}

TH_HIDE
void
thread_release(th_thread *th) {
  free(th);
}

TH_HIDE
void
thread_join(th_thread *th) {
  pthread_join(th->id, NULL);
}

TH_HIDE
void
thread_exit(void) {
#if !TH_NONE
  pthread_exit(NULL);
#endif
}

TH_HIDE
uint32_t
thread_ncpu(void) {
  long n;

#if TH_NONE
  return 1;
#endif
  n = sysconf(_SC_NPROCESSORS_ONLN);

  return n > 0 ? (uint32_t)n : 1;
}

TH_HIDE
void
thread_cond_init(th_thread_cond *cond) {
  pthread_cond_init(&cond->cond, NULL);
}

TH_HIDE
void
thread_cond_signal(th_thread_cond *cond) {
  pthread_cond_signal(&cond->cond);
}

TH_HIDE
void
thread_cond_broadcast(th_thread_cond *cond) {
  pthread_cond_broadcast(&cond->cond);
}

TH_HIDE
void
thread_cond_destroy(th_thread_cond *cond) {
  pthread_cond_destroy(&cond->cond);
}

TH_HIDE
void
thread_cond_wait(th_thread_cond *cond, th_thread_mutex *mutex) {
  pthread_cond_wait(&cond->cond, &mutex->mutex);
}

TH_HIDE
void
thread_mutex_init(th_thread_mutex *mutex) {
  pthread_mutex_init(&mutex->mutex, NULL);
}

TH_HIDE
void
thread_mutex_destroy(th_thread_mutex *mutex) {
  pthread_mutex_destroy(&mutex->mutex);
}

TH_HIDE
void
thread_lock(th_thread_mutex *mutex) {
  pthread_mutex_lock(&mutex->mutex);
}

TH_HIDE
void
thread_unlock(th_thread_mutex *mutex) {
  pthread_mutex_unlock(&mutex->mutex);
}

TH_HIDE
void
thread_rwlock_init(th_thread_rwlock *rwlock) {
  pthread_rwlock_init(&rwlock->rwlock, NULL);
}

TH_HIDE
void
thread_rwlock_destroy(th_thread_rwlock *rwlock) {
  pthread_rwlock_destroy(&rwlock->rwlock);
}

TH_HIDE
void
thread_rdlock(th_thread_rwlock *rwlock) {
  pthread_rwlock_rdlock(&rwlock->rwlock);
}

TH_HIDE
void
thread_rwunlock(th_thread_rwlock *rwlock) {
  pthread_rwlock_unlock(&rwlock->rwlock);
}

TH_HIDE
void
thread_wrlock(th_thread_rwlock *rwlock) {
  pthread_rwlock_wrlock(&rwlock->rwlock);
}
