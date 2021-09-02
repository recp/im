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

#ifndef src_posix_thread_h
#define src_posix_thread_h

#include "../common.h"

#include <pthread.h>

typedef struct tm_thread {
  pthread_t id;
} tm_thread;

typedef struct tm_thread_cond {
  pthread_cond_t cond;
} tm_thread_cond;

typedef struct tm_thread_mutex {
  pthread_mutex_t mutex;
} tm_thread_mutex;

typedef struct tm_thread_rwlock {
  pthread_rwlock_t rwlock;
} tm_thread_rwlock;

IM_HIDE
tm_thread*
thread_new(void (*func)(void *), void *obj);

IM_HIDE
void
thread_release(tm_thread *th);

IM_HIDE
void
thread_join(tm_thread *th);

IM_HIDE
void
thread_exit(void);

IM_HIDE
void
thread_cond_init(tm_thread_cond *cond);

IM_HIDE
void
thread_cond_signal(tm_thread_cond *cond);

IM_HIDE
void
thread_cond_destroy(tm_thread_cond *cond);

IM_HIDE
void
thread_cond_wait(tm_thread_cond *cond, tm_thread_mutex *mutex);

IM_HIDE
void
thread_mutex_init(tm_thread_mutex *mutex);

IM_HIDE
void
thread_mutex_destroy(tm_thread_mutex *mutex);

IM_HIDE
void
thread_lock(tm_thread_mutex *mutex);

IM_HIDE
void
thread_unlock(tm_thread_mutex *mutex);

IM_HIDE
void
thread_rwlock_init(tm_thread_rwlock *rwlock);

IM_HIDE
void
thread_rwlock_destroy(tm_thread_rwlock *rwlock);

IM_HIDE
void
thread_rwunlock(tm_thread_rwlock *rwlock);

IM_HIDE
void
thread_rdlock(tm_thread_rwlock *rwlock);

IM_HIDE
void
thread_wrlock(tm_thread_rwlock *rwlock);

#endif /* src_posix_thread_h */
