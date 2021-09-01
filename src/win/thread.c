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

/*
  References:
   [0] https://locklessinc.com/articles/pthreads_on_windows/
   [1] http://www.cs.wustl.edu/~schmidt/win32-cv-1.html
*/

typedef struct tm_thread_entry {
  void *arg;
  void (*func)(void *);
} tm_thread_entry;

static
DWORD
WINAPI
thread_entry(void *arg) {
  tm_thread_entry entry;

  /* to call free before calling thread func */
  memcpy(&entry, arg, sizeof(entry));
  free(arg);

  entry.func(entry.arg);

  return 0;
}

IM_HIDE
tm_thread*
thread_new(void (*func)(void *), void *obj) {
  tm_allocator   *alc;
  tm_thread       *th;
  tm_thread_entry *entry;


  alc         = tm_get_allocator();
  th          = alc->calloc(1, sizeof(*th));
  entry       = calloc(1, sizeof(*entry));
  entry->func = func;
  entry->arg  = obj;

  th->id = CreateThread(NULL, 0, thread_entry, entry, 0, NULL);

  return th;
}

IM_HIDE
void
thread_join(tm_thread *th) {
  WaitForSingleObject(th->id, INFINITE);
  CloseHandle(th->id);
}

IM_HIDE
void
thread_cond_init(tm_thread_cond *cond) {
  InitializeConditionVariable(&cond->cond);
}

IM_HIDE
void
thread_cond_signal(tm_thread_cond *cond) {
  WakeConditionVariable(&cond->cond);
}

IM_HIDE
void
thread_cond_destroy(tm_thread_cond *cond) {
  (void)cond->cond;
}

IM_HIDE
void
thread_cond_wait(tm_thread_cond *cond, tm_thread_mutex *mutex) {
  SleepConditionVariableCS(&cond->cond, &mutex->mutex, INFINITE);
}

IM_HIDE
void
thread_mutex_init(tm_thread_mutex *mutex) {
  InitializeCriticalSection(&mutex->mutex);
}

IM_HIDE
void
thread_mutex_destroy(tm_thread_mutex *mutex) {
  DeleteCriticalSection(&mutex->mutex);
}

IM_HIDE
void
thread_lock(tm_thread_mutex *mutex) {
  EnterCriticalSection(&mutex->mutex);
}

IM_HIDE
void
thread_unlock(tm_thread_mutex *mutex) {
  LeaveCriticalSection(&mutex->mutex);
}

IM_HIDE
void
thread_rwlock_init(tm_thread_rwlock *rwlock) {
  InitializeSRWLock(&rwlock->rwlock);
}

IM_HIDE
void
thread_rwlock_destroy(tm_thread_rwlock *rwlock) {
  (void)*&rwlock->rwlock;
}

IM_HIDE
void
thread_rwunlock(tm_thread_rwlock *rwlock) {
  void *state;

  state = *(void **)&rwlock->rwlock;

  if (state == (void *)1) {
    /* Known to be an exclusive lock */
    ReleaseSRWLockExclusive(&rwlock->rwlock);
  } else {
    /* A shared unlock will work */
    ReleaseSRWLockShared(&rwlock->rwlock);
  }
}

IM_HIDE
void
thread_rdlock(tm_thread_rwlock *rwlock) {
  AcquireSRWLockShared(&rwlock->rwlock);
}

IM_HIDE
void
thread_wrlock(tm_thread_rwlock *rwlock) {
  AcquireSRWLockExclusive(&rwlock->rwlock);
}
