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

#ifndef im_options_h
#define im_options_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

typedef enum im_option_type_t {
  IM_OPTION_ROW_PAD_LAST           = 0,
  IM_OPTION_SUPPORTED_FORMATS      = 1,
  IM_OPTION_SUPPORTED_ORIENTATIONS = 2,
  IM_OPTION_SUPPORTED_COMPRESSIONS = 3,
  IM_OPTION_USE_MMAP_FOR_WINDOWS   = 4,
  
  
  /*
   from bmpsuite:
   Some viewers make undefined pixels transparent, others make them black,
   and others assign them palette color 0 (purple, in this case).
   
   STATUS: TODO.
   */
  IM_OPTION_BMP_SKIPPED_MODE,
} im_option_type_t;

typedef struct im_option_base_t {
  im_option_type_t type;
} im_option_base_t;

#define IM_OPTIONS (im_option_base_t *[])

typedef struct im_option_rowpadding_t {
  im_option_base_t base;
  uint32_t         pad;
} im_option_rowpadding_t;

IM_INLINE
im_option_rowpadding_t
im_option_row_padding(uint32_t last) {
  im_option_rowpadding_t pad;
  
  pad.base.type = IM_OPTION_ROW_PAD_LAST;
  pad.pad       = last;

  return pad;
}

#ifdef __cplusplus
}
#endif
#endif /* im_options_h */
