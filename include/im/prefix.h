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

/*
 * IM_PREFIX (an identifier's start, e.g. -DIM_PREFIX=app_): im's public
 * functions under that prefix, for a program that links two copies of im
 * (a codec-only one inside a library, and its own); src/prefix.h does the
 * same for the rest. The same IM_PREFIX for im and for what includes it.
 */

#ifndef im_prefix_h
#define im_prefix_h

#ifdef IM_PREFIX
#  define IM_PFX_JOIN_(a, b) a##b
#  define IM_PFX_JOIN(a, b)  IM_PFX_JOIN_(a, b)
#  define IM_PFX(name)       IM_PFX_JOIN(IM_PREFIX, name)

#  define im_free        IM_PFX(im_free)
#  define im_info_mem    IM_PFX(im_info_mem)
#  define im_load_mem    IM_PFX(im_load_mem)
#  define im_load        IM_PFX(im_load)
#  define im_init_data   IM_PFX(im_init_data)
#  define im_load_hex    IM_PFX(im_load_hex)
#  define im_load_base64 IM_PFX(im_load_base64)
#endif

#endif /* im_prefix_h */
