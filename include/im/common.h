/*
 * Copyright (c), Recep Aslantas.
 * MIT License (MIT), http://opensource.org/licenses/MIT
 */

#ifndef common_h
#define common_h

#if defined(_WIN32)
#  ifdef LIBIM_DLL
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
