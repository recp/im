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
#ifdef __cplusplus
extern "C" {
#endif

#ifndef _USE_MATH_DEFINES
#  define _USE_MATH_DEFINES       /* for windows */
#endif

#ifndef _CRT_SECURE_NO_WARNINGS
#  define _CRT_SECURE_NO_WARNINGS /* for windows */
#endif

#ifndef _CRT_NONSTDC_NO_DEPRECATE
#  define _CRT_NONSTDC_NO_DEPRECATE /* for windows */
#endif

/* since C99 or compiler ext */
#include <stdint.h>
#include <stddef.h>
#include <float.h>
#include <stdbool.h>
#include <errno.h>
#include <stdlib.h>

#ifdef DEBUG
#  include <assert.h>
#  include <stdio.h>
#endif

#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
#  define IM_WINAPI
#  pragma warning (disable : 4068) /* disable unknown pragma warnings */
#endif

#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
#  ifdef IM_STATIC
#    define IM_EXPORT
#  elif defined(IM_EXPORTS)
#    define IM_EXPORT __declspec(dllexport)
#  else
#    define IM_EXPORT __declspec(dllimport)
#  endif
#  define IM_HIDE
#else
#  define IM_EXPORT   __attribute__((visibility("default")))
#  define IM_HIDE     __attribute__((visibility("hidden")))
#endif

#if defined(_MSC_VER)
#  define IM_INLINE   __forceinline
#  define IM_ALIGN(X) __declspec(align(X))
#else
#  define IM_ALIGN(X) __attribute((aligned(X)))
#  define IM_INLINE   static inline __attribute((always_inline))
#endif

#ifndef __has_builtin
#  define __has_builtin(x) 0
#endif

typedef enum ImResult {
  IM_NOOP     =  1,     /* no operation needed */
  IM_OK       =  0,
  IM_ERR      = -1,     /* UKNOWN ERR */
  IM_EFOUND   = -1000,
  IM_ENOMEM   = -ENOMEM,
  IM_EPERM    = -EPERM,
  IM_EBADF    = -EBADF  /* file couldn't parsed / loaded */
} ImResult;

#ifdef __cplusplus
}
#endif
#endif /* common_h */
