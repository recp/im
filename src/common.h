/*
 * Copyright (c), Recep Aslantas.
 * MIT License (MIT), http://opensource.org/licenses/MIT
 */

#ifndef src_common_h
#define src_common_h

#include "../include/im/common.h"
#include "../include/im/im.h"

#include <string.h>
#include <stdlib.h>

typedef struct ImQuantTbl {
  IM_ALIGN(16) uint16_t qt[64]; /* zig-zag order */
  bool                  valid;
} ImQuantTbl;

typedef struct ImJpeg {
  ImQuantTbl dqt[4];
} ImJpeg;

#endif /* src_common_h */
