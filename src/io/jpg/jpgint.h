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
 * The JPEG decoder's parts (ITU-T T.81 | ISO/IEC 10918-1) and what they
 * share: the frame, its components, the tables, a scan's state.
 *
 * The pixels are libjpeg-turbo's to the byte (its default decompression:
 * the accurate integer IDCT, "fancy" upsampling, its fixed-point colour
 * conversion, what it does where data runs out or is damaged); that's
 * what every part's arithmetic is chosen for.
 */

#ifndef src_io_jpg_jpgint_h
#define src_io_jpg_jpgint_h

#include "../../common.h"
#include "../../thread/pool.h"

enum {
  J_COMPS   = 4,         /* components a frame; more aren't decoded */
  J_HBITS   = 11,        /* bits a Huffman table's one look takes */
  J_HSIZE   = 1 << J_HBITS,
  J_SAMPLES = 1u << 30,  /* a component's samples at most */
  J_TAIL    = 64          /* bytes readable past a segment's end */
};

/* coding processes (SOFn) */
enum {
  J_SEQUENTIAL  = 0,
  J_PROGRESSIVE = 1,
  J_LOSSLESS    = 2
};

/* the colour space the samples are in, libjpeg's guess (jdapimin.c) */
typedef enum jspace {
  J_GRAY = 0,
  J_RGB,
  J_YCC,
  J_CMYK,
  J_YCCK,
  J_OTHER
} jspace_t;

/* A Huffman table. One look at the next J_HBITS bits, `fast[]`:
     0                      a longer code: maxcode[] and valoff[] find it
     bits 0–4               bits to take: the code's, and with J_FUSED
                            its value's too
     J_FUSED (bit 5)        the value is in bits 16–31 (signed)
     bits 8–15              the symbol */
typedef struct jhuff {
  uint32_t fast[J_HSIZE];
  int32_t  maxcode[18];   /* by length, -1 none; [17] ends any search */
  int32_t  valoff[18];
  uint8_t  vals[256];
  uint8_t  maxsym;
  bool     ok;
} jhuff_t;

enum { J_FUSED = 1u << 5 };

/* A scan's entropy-coded data, its stuffed 0xFFs and fill bytes taken
   out before it's read: segments one after another in `buf`, each ended
   by a marker (an RSTn, or what ends the scan; EOI where data ends) */
typedef struct jseg {
  size_t   start, end;   /* in `buf` */
  uint32_t marker;
} jseg_t;

typedef struct jdata {
  uint8_t       *buf;    /* J_TAIL bytes of room past the last */
  size_t         cap;
  jseg_t        *seg;
  uint32_t       nseg, segcap;
  const uint8_t *next;   /* the scan's ending marker in the file (its 0xFF) */
} jdata_t;

/* bits, MSB first: `buf` holds `n` of them (more below, the next byte's,
   loaded early); eight bytes can be read at `p` */
typedef struct jbits {
  const uint8_t *p;
  uint64_t       buf;
  uint32_t       n;
} jbits_t;

typedef struct jcomp {
  uint8_t   id, h, v, tq;     /* SOF's */
  uint8_t   td, ta;           /* the current scan's tables */
  uint32_t  bw, bh;           /* blocks across and down, every MCU's */
  uint32_t  cw, ch;           /* its samples across and down (downsampled) */
  uint32_t  sw, sh;           /* blocks a scan of it alone covers */
  void     *plane;            /* bw × 8 by bh × 8 samples: uint8 or uint16 */
  size_t    stride;           /* samples a plane row */
  int16_t  *coefs;            /* multi-scan: 64 a block, natural order */
  uint16_t  q[64];            /* its quantization table, as at its first scan */
  bool      latched;
  int8_t    bits[64];         /* progressive: a coefficient's last Al; -1 none */
  int8_t    prev[10];         /* bits[1–9] before the last scan of it */
} jcomp_t;

typedef struct jpg {
  const uint8_t *data, *end;

  /* the frame */
  uint32_t  width, height, ncomp, prec;
  uint32_t  hmax, vmax, mcux, mcuy;
  uint32_t  process;          /* J_SEQUENTIAL, J_PROGRESSIVE, J_LOSSLESS */
  bool      arith, frame, multiscan;
  jcomp_t   comp[J_COMPS];

  /* tables */
  uint16_t  quant[4][64];     /* natural order */
  jhuff_t  *dc[4], *ac[4];    /* NULL: not defined */
  uint32_t  restart;          /* DRI's interval in MCUs; 0 none */
  uint8_t   dc_l[16], dc_u[16], ac_k[16]; /* DAC's conditioning */

  /* markers that say what the samples are */
  bool      jfif, adobe;
  uint8_t   transform;        /* Adobe's */
  uint8_t  *icc;              /* APP2's chunks, joined */
  size_t    icc_len;
  const uint8_t *icc_part[256]; /* the chunks by sequence number (in the data) */
  uint32_t  icc_part_len[256], icc_count;
  bool      icc_bad;
  uint16_t  density[2];       /* JFIF's, and its unit */
  uint8_t   density_unit;

  /* the scan */
  uint32_t  scomp[J_COMPS], nscomp;
  uint32_t  ss, se, ah, al;
  jdata_t   sd;               /* its data, unstuffed */
  uint32_t  nscans;
  uint32_t  last_good;        /* the iMCU row a scan last decoded with data */

  /* threads: at most `threads`, the pool's (NULL: the caller's alone) */
  uint32_t   threads;
  im_pool_t *pool;

  /* the IDCT's: 8- or 12-bit */
  void    (*idct)(const int16_t *in, const uint16_t *q, void *out, size_t stride);
} jpg_t;

/* jpg.c */
IM_HIDE bool jpg_decode_mem(const uint8_t *data, size_t len, ImImage *im, ImByteOrder order, uint32_t threads);

/* huff.c */
IM_HIDE bool jpg_huff_build(jhuff_t *h, const uint8_t counts[16], const uint8_t *vals, bool dc, bool lossless);
IM_HIDE void jpg_huff_std(jpg_t *j);
IM_HIDE bool jpg_unstuff(jdata_t *d, const uint8_t *p, const uint8_t *end);
IM_HIDE bool jpg_scan_huff(jpg_t *j, im_pool_t *pool);
IM_HIDE bool jpg_resync(const jdata_t *d, uint32_t *next_rst, uint32_t *cur);

IM_HIDE bool jpg_scan_lossless(jpg_t *j);

/* arith.c */
IM_HIDE bool jpg_scan_arith(jpg_t *j);

/* idct.c */
IM_HIDE void jpg_idct8(const int16_t *in, const uint16_t *q, void *out, size_t stride);
IM_HIDE void jpg_idct12(const int16_t *in, const uint16_t *q, void *out, size_t stride);

/* sample.c */
IM_HIDE bool jpg_out(jpg_t *j, jspace_t space, ImImage *im, ImByteOrder order);

/* a block of the scan's component `c` at (bx, by) done: into its plane
   (one pass) or kept (multi-scan) */
IM_HIDE void jpg_put(jpg_t *j, jcomp_t *c, int16_t *blk, uint32_t bx, uint32_t by);

/* the natural order of zig-zag position k, and 16 more for a damaged
   run past the end (libjpeg's jpeg_natural_order) */
IM_HIDE extern const uint8_t jpg_natural[64 + 16];

#endif /* src_io_jpg_jpgint_h */
