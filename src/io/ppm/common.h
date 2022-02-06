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
 References:
 [0] http://netpbm.sourceforge.net/doc/
 */

#ifndef pnm_common_h
#define pnm_common_h

#include "../common.h"
#include "../../str.h"

typedef struct im_pnm_header_t {
  uint32_t width;
  uint32_t height;
  uint32_t count;
  uint32_t bytesPerCompoment;
  uint32_t maxRef;
  float    pe;
} im_pnm_header_t;

typedef struct im_pfm_header_t {
  uint32_t width;
  uint32_t height;
  uint32_t count;
  uint32_t bytesPerCompoment;
  uint32_t maxRef;
  float    byteOrderHint; /* negative is Little-Endian otherwise Big-Endian */
} im_pfm_header_t;

typedef enum pam_tuple_type_t {
  PAM_TUPLE_TYPE_UNKNOWN       = 0,
  PAM_TUPLE_TYPE_BLACKANDWHITE = 1,
  PAM_TUPLE_TYPE_GRAYSCALE     = 2,
  PAM_TUPLE_TYPE_RGB           = 3,
  PAM_TUPLE_TYPE_RGB_ALPHA     = 4
} pam_tuple_type_t;

typedef struct im_pam_header_t {
  uint32_t         width;
  uint32_t         height;
  uint32_t         depth;
  uint32_t         maxval;
  pam_tuple_type_t tupltype;
  uint32_t         count;
  uint32_t         bytesPerCompoment;
  uint32_t         maxRef;
  float            pe;
  bool             failed;
} im_pam_header_t;

#endif /* pnm_common_h */
