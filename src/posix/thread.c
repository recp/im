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

typedef struct tm_thread_entry {
  void *arg;
  void (*func)(void *);
} tm_thread_entry;

static
void*
thread_entry(void *arg) {
  tm_thread_entry entry;

  /* to call free before calling thread func */
  memcpy(&entry, arg, sizeof(entry));
  free(arg);

  entry.func(entry.arg);

  return NULL;
}

IM_HIDE
tm_thread*
thread_new(void (func)(void *), void *obj) {
  tm_thread       *th;
  tm_thread_entry *entry;
  pthread_attr_t   attr;

  th          = calloc(1, sizeof(*th));
  entry       = calloc(1, sizeof(*entry));
  entry->func = func;
  entry->arg  = obj;

  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

  pthread_create(&th->id, &attr, thread_entry, entry);
  pthread_attr_destroy(&attr);

  return th;
}

IM_HIDE
void
thread_join(tm_thread *th) {
  pthread_join(th->id, NULL);
}

IM_HIDE
void
thread_cond_init(tm_thread_cond *cond) {
  pthread_cond_init(&cond->cond, NULL);
}

IM_HIDE
void
thread_cond_signal(tm_thread_cond *cond) {
  pthread_cond_signal(&cond->cond);
}

IM_HIDE
void
thread_cond_destroy(tm_thread_cond *cond) {
  pthread_cond_destroy(&cond->cond);
}

IM_HIDE
void
thread_cond_wait(tm_thread_cond *cond, tm_thread_mutex *mutex) {
  pthread_cond_wait(&cond->cond, &mutex->mutex);
}

IM_HIDE
void
thread_mutex_init(tm_thread_mutex *mutex) {
  pthread_mutex_init(&mutex->mutex, NULL);
}

IM_HIDE
void
thread_mutex_destroy(tm_thread_mutex *mutex) {
  pthread_mutex_destroy(&mutex->mutex);
}

IM_HIDE
void
thread_lock(tm_thread_mutex *mutex) {
  pthread_mutex_lock(&mutex->mutex);
}

IM_HIDE
void
thread_unlock(tm_thread_mutex *mutex) {
  pthread_mutex_unlock(&mutex->mutex);
}

IM_HIDE
void
thread_rwlock_init(tm_thread_rwlock *rwlock) {
  pthread_rwlock_init(&rwlock->rwlock, NULL);
}

IM_HIDE
void
thread_rwlock_destroy(tm_thread_rwlock *rwlock) {
  pthread_rwlock_destroy(&rwlock->rwlock);
}

IM_HIDE
void
thread_rdlock(tm_thread_rwlock *rwlock) {
  pthread_rwlock_rdlock(&rwlock->rwlock);
}

IM_HIDE
void
thread_rwunlock(tm_thread_rwlock *rwlock) {
  pthread_rwlock_unlock(&rwlock->rwlock);
}

IM_HIDE
void
thread_wrlock(tm_thread_rwlock *rwlock) {
  pthread_rwlock_wrlock(&rwlock->rwlock);
}
