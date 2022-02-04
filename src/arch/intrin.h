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

#ifndef im_intrin_h
#define im_intrin_h

#if defined( _MSC_VER )
#  if (defined(_M_AMD64) || defined(_M_X64)) || _M_IX86_FP == 2
#    ifndef __SSE2__
#      define __SSE2__
#    endif
#  elif _M_IX86_FP == 1
#    ifndef __SSE__
#      define __SSE__
#    endif
#  endif
/* do not use alignment for older visual studio versions */
#  if _MSC_VER < 1913     /* Visual Studio 2017 version 15.6 */
#    define IM_ALL_UNALIGNED
#  endif
#endif

#if defined( __SSE__ ) || defined( __SSE2__ )
#  include <xmmintrin.h>
#  include <emmintrin.h>
#  define IM_SSE_FP 1
#  ifndef IM_SIMD_x86
#    define IM_SIMD_x86
#  endif
#endif

#if defined(__SSE3__)
#  include <pmmintrin.h>
#  ifndef IM_SIMD_x86
#    define IM_SIMD_x86
#  endif
#endif

#if defined(__SSE4_1__)
#  include <smmintrin.h>
#  ifndef IM_SIMD_x86
#    define IM_SIMD_x86
#  endif
#endif

#if defined(__SSE4_2__)
#  include <nmmintrin.h>
#  ifndef IM_SIMD_x86
#    define IM_SIMD_x86
#  endif
#endif

#ifdef __AVX__
#  include <immintrin.h>
#  define IM_AVX_FP 1
#  ifndef IM_SIMD_x86
#    define IM_SIMD_x86
#  endif
#endif

/* ARM Neon */
#if defined(__ARM_NEON)
#  include <arm_neon.h>
#  if defined(__ARM_NEON_FP)
#    define IM_NEON_FP 1
#    ifndef IM_SIMD_ARM
#      define IM_SIMD_ARM
#    endif
#  endif
#endif

#if defined(IM_SIMD_x86) || defined(IM_NEON_FP)
#  ifndef IM_SIMD
#    define IM_SIMD
#  endif
#endif

//#if defined(IM_SIMD_x86)
//#  include "x86.h"
//#endif
//
//#if defined(IM_SIMD_ARM)
//#  include "arm.h"
//#endif

#endif /* im_intrin_h */
