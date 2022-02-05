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

#ifndef pnm_h
#define pnm_h

#include "common.h"

IM_HIDE
im_pnm_header_t
pnm_dec_header(ImImage                 * __restrict im,
               uint32_t                             ncomponents,
               char       * __restrict * __restrict start,
               const char              * __restrict end,
               bool                                 includeMaxVal);

IM_HIDE
im_pfm_header_t
pfm_dec_header(ImImage                 * __restrict im,
               uint32_t                             ncomponents,
               char       * __restrict * __restrict start,
               const char              * __restrict end);

IM_HIDE
im_pam_header_t
pam_dec_header(ImImage                 * __restrict im,
               char       * __restrict * __restrict start,
               const char              * __restrict end);

#endif /* pnm_h */
