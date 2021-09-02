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

#ifndef thread_common_h
#define thread_common_h

#if defined(_WIN32)
#  ifdef LIBIM_DLL
#    define TH_EXPORT __declspec(dllexport)
#  else
#    define TH_EXPORT __declspec(dllimport)
#  endif
#  define TH_HIDE
#  define TH_INLINE __forceinline
#  define TH_ALIGN(X) __declspec(align(X))
#else
#  define TH_EXPORT  __attribute__((visibility("default")))
#  define TH_HIDE    __attribute__((visibility("hidden")))
#  define TH_INLINE inline __attribute((always_inline))
#  define TH_ALIGN(X) __attribute((aligned(X)))
#endif

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define TH__UNUSED(X) (void)X

#if defined(_WIN32) || defined(WIN32)

 /* Exclude rarely-used stuff from Windows headers */
 /* Windows Header Files: */
#  define WIN32_LEAN_AND_MEAN
#  include <SDKDDKVer.h>
#  include <windows.h>

#endif

#endif /* thread_commo_h */
