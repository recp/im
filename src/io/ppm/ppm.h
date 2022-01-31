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

#ifndef ppm_h
#define ppm_h

#include "../../common.h"

IM_HIDE
ImResult
pbm_dec(ImImage ** __restrict im, const char * __restrict path);

IM_HIDE
ImResult
pgm_dec(ImImage ** __restrict im, const char * __restrict path);

IM_HIDE
ImResult
ppm_dec(ImImage ** __restrict im, const char * __restrict path);

#define SKIP_SPACES                                                           \
  {                                                                           \
    while (c != '\0' && IM_ARRAY_SEP_CHECK) c = *++p;                         \
    if (c == '\0')                                                            \
      break; /* to break loop */                                              \
  }

/* spaces in single line */
#define SKIP_HSPACES                                                          \
  do {                                                                        \
    while (c == ' ' || c == '\t' || c == '\v')                                \
      c = *++p;                                                               \
  } while (0)

/* newline */
#define SKIP_VSPACES                                                          \
  do {                                                                        \
    while (c == '\n' || c == '\r' || c == '\f' || c == '\v')                  \
      c = *++p;                                                               \
  } while (0)

#define NEXT_LINE                                                             \
  do {                                                                        \
    c = p ? *p : '\0';                                                        \
    while (p                                                                  \
           && p[0] != '\0'                                                    \
           && !IM_ARRAY_NLINE_CHECK                                           \
           && (c = *++p) != '\0'                                              \
           && !IM_ARRAY_NLINE_CHECK);                                         \
  } while(0);

#endif /* ppm_h */
