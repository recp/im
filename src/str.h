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

#define IM_ALLSPACES (c == ' ' || c == '\n' || c == '\t' \
                       || c == '\r' || c == '\f' || c == '\v')

IM_INLINE
char*
im_skip_spaces(char * __restrict p) {
  char c;
  c = *p;
  while (IM_ALLSPACES) {
    c = *++p;
  }
  return p;
}

IM_INLINE
bool
im_isdigit(char c) {
  return c >= '0' && c <= '9';
}

#define IM_DEFINE_GET_INTEGER(TYPE, NAME)                                     \
IM_INLINE                                                                     \
TYPE                                                                          \
im_ ## NAME(char * __restrict * __restrict src,                               \
            const char * __restrict end) {                                    \
  char   *p;                                                                  \
  TYPE    value;                                                              \
  char    c;                                                                  \
                                                                              \
  value = 0;                                                                  \
  p     = *src;                                                               \
  c     = *p;                                                                 \
                                                                              \
  while (p < end && c != '\0' && im_isdigit(c)) {                             \
    value = value * 10 + (c - '0');                                           \
    c     = *++p;                                                             \
  }                                                                           \
                                                                              \
  *src = p;                                                                   \
  return value;                                                               \
}

#define IM_DEFINE_GET_INTEGER_WITH_IGORING_ALL_SPACES(TYPE, NAME)             \
IM_INLINE                                                                     \
TYPE                                                                          \
im_ ## NAME ## _skipspaces(char * __restrict * __restrict src,                \
                           const char * __restrict end) {                     \
  char   *p;                                                                  \
  TYPE    value;                                                              \
  char    c;                                                                  \
                                                                              \
  value = 0;                                                                  \
  p     = im_skip_spaces(*src);                                               \
  c     = *p;                                                                 \
                                                                              \
  while (p < end && c != '\0' && im_isdigit(c)) {                             \
    value = value * 10 + (c - '0');                                           \
    c     = *++p;                                                             \
  }                                                                           \
                                                                              \
  *src = p;                                                                   \
  return value;                                                               \
}

IM_DEFINE_GET_INTEGER(int32_t,  geti32)
IM_DEFINE_GET_INTEGER(uint32_t, getu32)

IM_DEFINE_GET_INTEGER_WITH_IGORING_ALL_SPACES(int32_t,  geti32)
IM_DEFINE_GET_INTEGER_WITH_IGORING_ALL_SPACES(uint32_t, getu32)
IM_DEFINE_GET_INTEGER_WITH_IGORING_ALL_SPACES(uint8_t,  getu8)

#endif /* str_h */
