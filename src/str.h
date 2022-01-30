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

#ifndef str_h
#define str_h

#include "common.h"

IM_HIDE
const char*
im_strltrim_fast(const char * __restrict str);

IM_HIDE
int
im_strtok_count(char * __restrict buff,
                char * __restrict sep,
                size_t           *len);

IM_HIDE
int
im_strtok_count_fast(char * __restrict buff,
                     size_t            srclen,
                     size_t           *len);

IM_HIDE
unsigned long
im_strtof(char  * __restrict src,
          size_t             srclen,
          unsigned long      n,
          float * __restrict dest);

IM_HIDE
unsigned long
im_strtof_line(char  * __restrict src,
               size_t             srclen,
               unsigned long      n,
               float * __restrict dest);

IM_HIDE
unsigned long
im_strtod(char   * __restrict src,
          size_t              srclen,
          unsigned long       n,
          double * __restrict dest);

IM_HIDE
unsigned long
im_strtoui(char     * __restrict src,
           size_t                srclen,
           unsigned long         n,
           uint32_t * __restrict dest);

IM_HIDE
unsigned long
im_strtoi(char    * __restrict src,
          size_t               srclen,
          unsigned long        n,
          int32_t * __restrict dest);

IM_HIDE
unsigned long
im_strtoi_line(char    * __restrict src,
               size_t               srclen,
               unsigned long        n,
               int32_t * __restrict dest);

IM_HIDE
unsigned long
im_strtob(char  * __restrict src,
          size_t             srclen,
          unsigned long      n,
          bool  * __restrict dest);

IM_HIDE
char*
im_tolower(char *str);

IM_HIDE
char*
im_toupper(char *str);

#endif /* str_h */
