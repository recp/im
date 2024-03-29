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

#ifndef pp_h
#define pp_h

#include "../common.h"

IM_INLINE
void
rgb8_to_bgr8(uint8_t * __restrict p) {
  uint8_t t;
  
  t    = p[0];
  p[0] = p[2];
  p[2] = t;
}

IM_INLINE
void
rgb8_to_bgr8_copy(uint8_t * __restrict d, uint8_t * __restrict p, uint32_t len) {
  for (; len > 0; len--) {
    d[0] = p[2];
    d[1] = p[1];
    d[2] = p[0];

    d += 3;
    p += 3;
  }
}

IM_INLINE
void
rgb8_to_bgr8_all(uint8_t * __restrict d, uint32_t len) {
  uint8_t t;

  for (; len > 0; len--) {
    t    = d[0];
    d[0] = d[2];
    d[2] = t;

    d += 3;
  }
}

#endif /* pp_h */
