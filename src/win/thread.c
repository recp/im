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

typedef struct th_thread_entry {
  void *arg;
  void (*func)(void *);
} th_thread_entry;

static
DWORD
WINAPI
thread_entry(void *arg) {
  th_thread_entry entry;

  /* to call free before calling thread func */
  memcpy(&entry, arg, sizeof(entry));
  free(arg);

  entry.func(entry.arg);

  return 0;
}

TH_HIDE
th_thread*
thread_new(void (*func)(void *), void *obj) {
  th_allocator   *alc;
  th_thread       *th;
  th_thread_entry *entry;


  alc         = th_get_allocator();
  th          = alc->calloc(1, sizeof(*th));
  entry       = calloc(1, sizeof(*entry));
  entry->func = func;
  entry->arg  = obj;

  th->id = CreateThread(NULL, 0, thread_entry, entry, 0, NULL);

  return th;
}

TH_HIDE
void
thread_release(th_thread *th) {
  free(th);
}

TH_HIDE
void
thread_join(th_thread *th) {
  WaitForSingleObject(th->id, INFINITE);
  CloseHandle(th->id);
}

TH_HIDE
void
thread_exit(void) {
  TerminateThread(GetCurrentThread(), 0);
}

TH_HIDE
void
thread_cond_init(th_thread_cond *cond) {
  InitializeConditionVariable(&cond->cond);
}

TH_HIDE
void
thread_cond_signal(th_thread_cond *cond) {
  WakeConditionVariable(&cond->cond);
}

TH_HIDE
void
thread_cond_destroy(th_thread_cond *cond) {
  (void)cond->cond;
}

TH_HIDE
void
thread_cond_wait(th_thread_cond *cond, th_thread_mutex *mutex) {
  SleepConditionVariableCS(&cond->cond, &mutex->mutex, INFINITE);
}

TH_HIDE
void
thread_mutex_init(th_thread_mutex *mutex) {
  InitializeCriticalSection(&mutex->mutex);
}

TH_HIDE
void
thread_mutex_destroy(th_thread_mutex *mutex) {
  DeleteCriticalSection(&mutex->mutex);
}

TH_HIDE
void
thread_lock(th_thread_mutex *mutex) {
  EnterCriticalSection(&mutex->mutex);
}

TH_HIDE
void
thread_unlock(th_thread_mutex *mutex) {
  LeaveCriticalSection(&mutex->mutex);
}

TH_HIDE
void
thread_rwlock_init(th_thread_rwlock *rwlock) {
  InitializeSRWLock(&rwlock->rwlock);
}

TH_HIDE
void
thread_rwlock_destroy(th_thread_rwlock *rwlock) {
  (void)*&rwlock->rwlock;
}

TH_HIDE
void
thread_rwunlock(th_thread_rwlock *rwlock) {
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

TH_HIDE
void
thread_rdlock(th_thread_rwlock *rwlock) {
  AcquireSRWLockShared(&rwlock->rwlock);
}

TH_HIDE
void
thread_wrlock(th_thread_rwlock *rwlock) {
  AcquireSRWLockExclusive(&rwlock->rwlock);
}
