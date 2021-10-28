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

#ifndef common_h
#define common_h

#if defined(_MSC_VER)
#  ifdef IM_STATIC
#    define IM_EXPORT
#  elif defined(IM_EXPORTS)
#    define IM_EXPORT __declspec(dllexport)
#  else
#    define IM_EXPORT __declspec(dllimport)
#  endif
#  define IM_HIDE
#  define IM_INLINE __forceinline
#  define IM_ALIGN(X) __declspec(align(X))
#else
#  define IM_EXPORT  __attribute__((visibility("default")))
#  define IM_HIDE    __attribute__((visibility("hidden")))
#  define IM_INLINE inline __attribute((always_inline))
#  define IM_ALIGN(X) __attribute((aligned(X)))
#endif

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#endif /* common_h */
