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

#include "common.h"
#include "../include/im/options.h"

im_option_rowpadding_t im__opt_pad = {
  .base = {
    .type = IM_OPTION_ROW_PAD_LAST
  },
  .pad = 0
};

im_option_base_t* options[] =
{
  &im__opt_pad,                    /* 0:  _ROW_PAD_LAST                */
  0,                               /* 1:  _SUPPORTED_FORMATS           */
  false,                           /* 2:  _SUPPORTED_ORIENTATIONS      */
  false,                           /* 3:  _SUPPORTED_COMPRESSIONS      */

#ifndef _MSC_VER
  true                             /* 4: _USE_MMAP_FOR_WINDOWS         */
#else
  false
#endif
};

im_option_base_t * __restrict IM_OPT_SIMPLE[] = {
  &((im_option_rowpadding_t){
    .base = {
      .type = IM_OPTION_ROW_PAD_LAST
    },
    .pad = 0
  }).base,
  &((im_option_byteorder_t){
    .base = {
      .type = IM_OPTION_BYTE_ORDER
    },
    .order = IM_BYTEORDER_HOST
  }).base,
  &((im_option_bool_t){
    .base = {
      .type = IM_OPTION_SUPPORTS_PALETTE
    },
    .on = false
  }).base,
  NULL
};
