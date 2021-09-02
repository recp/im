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

#ifndef src_win_thread_h
#define src_win_thread_h

#include "../thread/common.h"

typedef struct th_thread {
  HANDLE id;
} th_thread;

typedef struct th_thread_cond {
  CONDITION_VARIABLE cond;
} th_thread_cond;

typedef struct th_thread_mutex {
	CRITICAL_SECTION mutex;
} th_thread_mutex;

typedef struct th_thread_rwlock {
	SRWLOCK rwlock;
} th_thread_rwlock;

TH_HIDE
th_thread*
thread_new(void (*func)(void *), void *obj);

TH_HIDE
void
thread_release(th_thread *th);

TH_HIDE
void
thread_join(th_thread *th);

TH_HIDE
void
thread_exit(void);

TH_HIDE
void
thread_cond_init(th_thread_cond *cond);

TH_HIDE
void
thread_cond_signal(th_thread_cond *cond);

TH_HIDE
void
thread_cond_destroy(th_thread_cond *cond);

TH_HIDE
void
thread_cond_wait(th_thread_cond *cond, th_thread_mutex *mutex);

TH_HIDE
void
thread_mutex_init(th_thread_mutex *mutex);

TH_HIDE
void
thread_mutex_destroy(th_thread_mutex *mutex);

TH_HIDE
void
thread_lock(th_thread_mutex *mutex);

TH_HIDE
void
thread_unlock(th_thread_mutex *mutex);

TH_HIDE
void
thread_rwlock_init(th_thread_rwlock *rwlock);

TH_HIDE
void
thread_rwlock_destroy(th_thread_rwlock *rwlock);

TH_HIDE
void
thread_rwunlock(th_thread_rwlock *rwlock);

TH_HIDE
void
thread_rdlock(th_thread_rwlock *rwlock);

TH_HIDE
void
thread_wrlock(th_thread_rwlock *rwlock);

#endif /* src_win_thread_h */
