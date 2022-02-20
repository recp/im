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

#include "mmap.h"
#include "../common.h"

#ifndef IM_WINAPI
#  include <sys/mman.h>
#else
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <io.h>
#endif

IM_HIDE
void*
im_mmap_rdonly(int fd, size_t size) {
  void *mapped;
  
  mapped = NULL;
  
#ifndef IM_WINAPI
  mapped = mmap(0, size, PROT_READ, MAP_SHARED, fd, 0);
  if (!mapped || mapped == MAP_FAILED)
    return NULL;
  
  madvise(mapped, size, MADV_SEQUENTIAL);
#else
  HANDLE hmap;
  if (!((hmap = CreateFileMapping((HANDLE)_get_osfhandle(fd), 0, PAGE_READONLY, 0, 0, 0))
        && (mapped = MapViewOfFileEx(hmap, FILE_MAP_READ, 0, 0, size, 0))))
    return NULL;
  
  CloseHandle(hmap);
#endif
  
  return mapped;
}

IM_HIDE
void
im_unmap(void *file, size_t size) {
#ifndef IM_WINAPI
  munmap(file, size);
#else
  IM__UNUSED(size);
  UnmapViewOfFile(file);
#endif
}
