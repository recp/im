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

typedef enum ImByteOrder {
  IM_BYTEORDER_HOST    = 0, /* default */
  IM_BYTEORDER_LITTLE  = 1,
  IM_BYTEORDER_BIG     = 2,
  IM_BYTEORDER_ANY     = 3
} ImByteOrder;

typedef enum im_option_type_t {
  IM_OPTION_ROW_PAD_LAST           = 0,
  IM_OPTION_SUPPORTED_FORMATS      = 1,
  IM_OPTION_SUPPORTED_ORIENTATIONS = 2,
  IM_OPTION_SUPPORTED_COMPRESSIONS = 3,
  IM_OPTION_USE_MMAP_FOR_WINDOWS   = 4,
  IM_OPTION_BYTE_ORDER             = 5, /* any  */
  IM_OPTION_SUPPORTS_PALETTE       = 6, /* true */

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

typedef struct im_option_bool_t {
  im_option_base_t base;
  bool             on;
} im_option_bool_t;

typedef struct im_option_rowpadding_t {
  im_option_base_t base;
  uint32_t         pad;
} im_option_rowpadding_t;

typedef struct im_option_byteorder_t {
  im_option_base_t base;
  ImByteOrder      order;
} im_option_byteorder_t;

IM_INLINE
im_option_rowpadding_t
im_option_row_padding(uint32_t last) {
  im_option_rowpadding_t pad;
  
  pad.base.type = IM_OPTION_ROW_PAD_LAST;
  pad.pad       = last;

  return pad;
}

IM_INLINE
im_option_bool_t
im_option_bool(im_option_type_t optype, bool on) {
  im_option_bool_t opt;
  
  opt.base.type = optype;
  opt.on        = on;
  
  return opt;
}

IM_INLINE
im_option_byteorder_t
im_option_row_byteorder(ImByteOrder order) {
  im_option_byteorder_t op;
  
  op.base.type = IM_OPTION_BYTE_ORDER;
  op.order     = order;
  
  return op;
}

/* pre-defined option sets */

/*
 - no palette
 - no BGR, ABGR (convert to RGB, RGBA)
 - no row pad
 - no bottom-to-top or RTL images
 - no compression
 - minimum 1byte for pixel
 - byte order is HOST
 
 + no release file called
 + 8bit, 16bit, 24bit, 32bit, 64bit allowed
 */
extern im_option_base_t * __restrict IM_OPT_SIMPLE[];

#ifdef __cplusplus
}
#endif
#endif /* im_options_h */
