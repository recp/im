/*
 * Copyright (C) 2026 Recep Aslantas
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
 * IM_OPTION_INDEX's index (im.h's ImIndex): a JPEG's (jpg.c: each MCU
 * row's start, or each restart interval's) or a JPEG 2000's (jp2.c: each
 * tile's packets as read, so a decode's tiles skip their headers)
 */

#ifndef src_index_h
#define src_index_h

#include "common.h"

enum {
  IM_INDEX_JPEG = 0,
  IM_INDEX_JP2  = 1
};

struct jcp;
struct xindex;

struct ImIndex {
  uint32_t kind;          /* IM_INDEX_JPEG, IM_INDEX_JP2 */

  /* a JPEG's: rows 0 … rows - 1 of the one-pass sequential Huffman scan of
     the bytes it's for, `cols` places a row (its start and every J_IXCOL
     MCUs along it): `len` bytes, `scan` the offset of the scan's data (the
     headers before it hashed: FNV-1a 64), `end` its marker's */
  uint64_t     len, hash, scan, end;
  uint32_t     w, rows, cols, nscomp;
  struct jcp  *cp;

  uint64_t    *offsets;
  uint64_t     scan_bytes;
  uint32_t     restart, intervals;

  /* a JPEG 2000's (jp2.c) */
  struct xindex *jp2;
};

#endif /* src_index_h */
