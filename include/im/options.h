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
  IM_OPTION_BGR_TO_RGB             = 7, /* false */

  /*
   from bmpsuite:
   Some viewers make undefined pixels transparent, others make them black,
   and others assign them palette color 0 (purple, in this case).
   
   STATUS: TODO.
   */
  IM_OPTION_BMP_SKIPPED_MODE,

  /* threads a decoder may use (uint): 0 as many as it finds useful (the
     default), 1 the caller's alone */
  IM_OPTION_THREADS,

  /* JPEG: whether its YCbCr (YCCK) becomes RGB (CMYK), over what the file
     says (int): -1 as the file says (the default), 0 no, 1 yes: a
     container's word on it, where it gives one */
  IM_OPTION_JPEG_COLOR_TRANSFORM,

  /* CMYK as the file stores it (bool): an Adobe JPEG's inverted CMYK left
     inverted (for a caller that inverts it itself); false: 0 no ink */
  IM_OPTION_CMYK_AS_STORED,

  /* RGB as RGB0 (bool): a 4th sample after each pixel's three, its
     maximum (IM_FORMAT_RGB0); false */
  IM_OPTION_RGB_PADDED,

  /* JPEG 2000: a palette's indexes as they are (bool), one gray channel,
     not its colours; false */
  IM_OPTION_PALETTE_INDEXES,

  /* samples deeper than 8 bits in 8 (bool), OpenJPEG's way:
     (v >> d) + ((v >> (d - 1)) & 1), held to 255; false: 16 bits */
  IM_OPTION_EIGHT_BITS,

  /* an opacity channel (int, ImOpacity): interleaved (the default; CMYK's
     dropped), apart in ImImage's `alpha` (the colours un-premultiplied;
     CMYK's kept), or not decoded at all */
  IM_OPTION_OPACITY,

  /* the caller's threads for a decode's parallel work (im_option_runner);
     im starts none of its own then */
  IM_OPTION_RUNNER,

  /* the pixels into the caller's memory (im_option_dest): rows `stride`
     bytes apart from `pixels`, `size` bytes there in all (too few: the
     load fails); im->data.data stays NULL, row_pad_last the rows' gap.
     16-bit samples: `pixels` and `stride` even */
  IM_OPTION_DEST,

  /* decoded smaller (uint): the width and height over 1 (the default), 2,
     4 or 8, rounded up; other values 1. JPEG by a smaller IDCT (libjpeg's
     scaled decode; a lossless one keeps its size); JPEG 2000 its finest
     resolution levels left out (OpenJPEG's reduce; no more than it has).
     The image's width and height say what came */
  IM_OPTION_SCALE,
} im_option_type_t;


typedef enum ImOpacity {
  IM_OPACITY_INTERLEAVED = 0,
  IM_OPACITY_APART       = 1,
  IM_OPACITY_NONE        = 2
} ImOpacity;

typedef struct im_option_base_t {
  im_option_type_t type;
} im_option_base_t;

/* one item of a decode's parallel work: `worker` below the runner's
   `workers`; false: the decode fails */
typedef bool (*im_job_fn)(void *arg, uint32_t worker, uint32_t item);

/* The caller's threads. run() calls job(arg, w, i) once for each i < n and
   returns when all are done (false if any job returned false): w below
   `workers` (2 to 16) and none shared by two jobs running at once; items
   started in index order, a started one run to its end (a job may wait for
   an earlier one: JPEG's progressive scans do, row by row). An image
   smaller than 256K pixels doesn't call it. */
typedef struct im_runner {
  void     *ctx;
  uint32_t  workers;
  bool    (*run)(void *ctx, im_job_fn job, void *arg, uint32_t n);
} im_runner_t;

typedef struct im_option_runner_t {
  im_option_base_t base;
  im_runner_t      runner;
} im_option_runner_t;

typedef struct im_option_dest_t {
  im_option_base_t base;
  void            *pixels;
  size_t           stride, size;
} im_option_dest_t;

#define IM_OPTIONS (im_option_base_t *[])

typedef struct im_option_bool_t {
  im_option_base_t base;
  bool             on;
} im_option_bool_t;

typedef struct im_option_int_t {
  im_option_base_t base;
  int32_t          value;
} im_option_int_t;

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

/* an option with a number: IM_OPTION_THREADS, IM_OPTION_JPEG_COLOR_TRANSFORM,
   IM_OPTION_OPACITY, IM_OPTION_SCALE */
IM_INLINE
im_option_int_t
im_option_int(im_option_type_t optype, int32_t value) {
  im_option_int_t opt;

  opt.base.type = optype;
  opt.value     = value;

  return opt;
}

IM_INLINE
im_option_runner_t
im_option_runner(im_runner_t runner) {
  im_option_runner_t opt;

  opt.base.type = IM_OPTION_RUNNER;
  opt.runner    = runner;

  return opt;
}

IM_INLINE
im_option_dest_t
im_option_dest(void *pixels, size_t stride, size_t size) {
  im_option_dest_t opt;

  opt.base.type = IM_OPTION_DEST;
  opt.pixels    = pixels;
  opt.stride    = stride;
  opt.size      = size;

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
