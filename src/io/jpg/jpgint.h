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

/* pixels a thread at least: none below twice this, the rest one thread for
   each (a test build can lower it) */
#ifndef J_POOL_MIN
#  define J_POOL_MIN (1u << 19)
#endif

/* a frame's working memory (planes, coefficients) from which it's given
   back as it's done with (a test build can lower it) */
#ifndef J_BIG
#  define J_BIG ((uint64_t)256u << 20)
#endif

/* a frame's pixels a byte of its data past which the stream is read through
   first, its scans' data passed over (a test build can lower it, 0: always) */
#ifndef J_DRY
#  define J_DRY 64u
#endif

/* a sequential scan's blocks from which its AC tables get two-symbol
   look-ups (a test build can lower it) */
#ifndef J_PAIR_MIN
#  define J_PAIR_MIN 4096u
#endif

/* a cold path kept out of its caller (whose fast path then saves no
   registers for it) */
#if defined(_MSC_VER)
#  define J_NOINLINE __declspec(noinline)
#else
#  define J_NOINLINE __attribute__((noinline))
#endif

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
                            its value's too (bits 5–6 zero: `e & 63` is
                            that, which a 64-bit shift takes for free)
     J_FUSED (bit 7)        the value is in bits 16–31 (signed)
     bits 8–15              the symbol */
typedef struct jhuff {
  uint32_t  fast[J_HSIZE];
  int32_t   maxcode[18];   /* by length, -1 none; [17] ends any search */
  int32_t   valoff[18];
  uint8_t   vals[256];
  uint8_t   maxsym;
  bool      ok;
  uint64_t *pair;          /* two-symbol look-ups (huff.c's JP_*), made for
                              a big sequential scan; NULL none. An AC
                              table's its own pairs; a DC table's its code
                              with the first AC symbol of `pair_ac`'s, made
                              again each such scan. Its own: freed where
                              the table is built again or freed */
  const struct jhuff *pair_ac;
} jhuff_t;

enum { J_FUSED = 1u << 7 };

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

typedef void (*jpg_idct_fn)(const int16_t *in, const uint16_t *q, void *out, size_t stride);

typedef struct jcomp {
  uint8_t   id, h, v, tq;     /* SOF's */
  uint8_t   ss;               /* a block's samples across in its plane: 8, or
                                 fewer decoded smaller (a sample lossless) */
  uint8_t   uh, uv;           /* h and v as its upsampling sees them */
  uint32_t  ow, oh;           /* its samples across and down in its plane */
  jpg_idct_fn idct;           /* to its plane, ss by ss */
  uint8_t   td, ta;           /* the current scan's tables */
  uint32_t  bw, bh;           /* blocks across and down, every MCU's */
  uint32_t  cw, ch;           /* its samples across and down (downsampled) */
  uint32_t  sw, sh;           /* blocks a scan of it alone covers */
  uint32_t  rbx0, rbx1, rby0, rby1; /* the blocks transformed: sw × sh, or
                                 the region's (and its upsampling's
                                 neighbours) */
  void     *plane;            /* bw × 8 by bh × 8 samples: uint8 or uint16 */
  size_t    stride;           /* samples a plane row */
  size_t    plane_size;       /* its bytes */
  int16_t  *coefs;            /* multi-scan: 64 a block, natural order */
  uint16_t  q[128];           /* its quantization table, as at its first scan;
                                 then each coefficient's bound (jpg_qlim) */
  bool      latched;
  int8_t    bits[64];         /* progressive: a coefficient's last Al; -1 none */
  int8_t    prev[10];         /* bits[1–9] before the last scan of it */
} jcomp_t;

typedef struct jpg {
  const uint8_t *data, *end;

  /* the frame */
  uint32_t  width, height, ncomp, prec;
  uint32_t  scale;            /* the size divided by this: 1, 2, 4, 8 */
  uint32_t  ds, ow, oh;       /* the smallest block's side; the frame's
                                 pixels (scaled) */
  bool      region;           /* only reg_x, reg_y, reg_w by reg_h of them
                                 out (IM_OPTION_REGION; cut to the frame;
                                 without one, all of it) */
  uint32_t  reg_x, reg_y, reg_w, reg_h;
  size_t    mem_limit;        /* IM_OPTION_MEMORY_LIMIT's bytes; 0: none */
  bool      over;             /* refused for it */
  size_t    used;             /* the data's bytes the markers were read to */
  uint32_t  hmax, vmax, mcux, mcuy;
  uint32_t  process;          /* J_SEQUENTIAL, J_PROGRESSIVE, J_LOSSLESS */
  bool      arith, frame, multiscan;
  bool      big;              /* its working memory given back as it goes
                                 (mm/mmap.h's im_big_alloc) */
  jcomp_t   comp[J_COMPS];

  /* hierarchical (Annex J): DHP's frame; each of its components' samples
     as the frames so far made them (modulo 2^16), at their size */
  bool      hier, diff;       /* a DHP came; the frame is a differential one */
  bool      hdct;             /* the first frame DCT-based (lossless: false) */
  uint8_t   expand;           /* EXP's for the next frame: 1 across, 2 down */
  uint8_t   grow;             /* and this frame's */
  uint32_t  nframes;
  uint8_t   hseen;            /* DHP's components a first frame has had (a mask) */
  uint32_t  hprec, hwidth, hheight, hncomp;
  uint8_t   hid[J_COMPS], hh[J_COMPS], hv[J_COMPS];
  uint16_t *ref[J_COMPS];
  uint32_t  rw[J_COMPS], rh[J_COMPS];

  /* tables */
  uint16_t  quant[4][64];     /* natural order */
  uint8_t   qdef;             /* those a DQT gave (a mask) */
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
  bool      dry;              /* the stream read through, no data decoded */

  /* the caller's: the colour transform (-1 as the file says, 0, 1), an
     Adobe CMYK left inverted, RGB as RGB0 */
  int32_t    want_transform;
  bool       cmyk_stored, rgb_pad;
  /* threads: at most `threads`, the pool's (NULL: the caller's alone), or
     the caller's runner */
  uint32_t   threads;
  im_runner_t runner;
  uint8_t   *dest;              /* the caller's memory for the pixels, or NULL */
  size_t     dest_stride, dest_size;
  im_pool_t *pool;
  /* a multi-scan image's scans on threads: those kept; a kept one's
     place among them, the earlier ones it waits for (a mask) */
  struct jdeferred *defer;
  uint32_t   ndefer;
  struct jflow *flow;
  bool       idct_done;         /* the blocks' IDCTs made with the scans */
  bool       out_done;          /* and the pixels out */
  struct jsout *out;            /* those made ready for it */
  uint32_t   flow_self;
  uint64_t   flow_deps;

} jpg_t;

/* jpg.c */
/* what a caller asks of a decode */
typedef struct jopts {
  ImByteOrder order;
  uint32_t    threads;     /* 0: as many as help */
  int32_t     transform;   /* -1: as the file says; 0 none; 1 YCbCr/YCCK to RGB/CMYK */
  bool        cmyk_stored; /* an Adobe CMYK left inverted */
  bool        rgb_pad;     /* RGB as RGB0 */
  im_runner_t runner;      /* the caller's threads (run NULL: none) */
  uint8_t    *dest;        /* the caller's memory, `stride` a row; NULL: im's */
  size_t      dest_stride, dest_size;
  uint32_t    scale;       /* 1 (or 0), 2, 4, 8: the size divided by it */
  bool        region;      /* only x, y, w by h out (after the scale) */
  uint32_t    rx, ry, rw, rh;
  size_t      mem_limit;   /* bytes the image may take; 0: no limit */
  bool       *over;        /* set where refused for mem_limit (NULL: none) */
  size_t     *used;        /* the bytes through EOI (all, none there) where it decodes (NULL: none) */
} jopts_t;

IM_HIDE bool jpg_decode_mem(const uint8_t *data, size_t len, ImImage *im, const jopts_t *o);
IM_HIDE bool jpg_region_cut(jpg_t *j);
IM_HIDE uint64_t jpg_frame_bytes(const jpg_t *j);
IM_HIDE uint32_t jpg_region_rows(const jpg_t *j, uint32_t w, uint32_t h, uint32_t *first);

/* huff.c */
IM_HIDE bool jpg_huff_build(jhuff_t *h, const uint8_t counts[16], const uint8_t *vals, bool dc, bool lossless);
IM_HIDE void jpg_huff_free(jhuff_t *h);
IM_HIDE void jpg_huff_std(jpg_t *j);
IM_HIDE bool jpg_unstuff(jdata_t *d, const uint8_t *p, const uint8_t *end);
IM_HIDE const uint8_t *jpg_scan_end(const uint8_t *p, const uint8_t *end);
IM_HIDE bool jpg_scan_huff(jpg_t *j, im_pool_t *pool);
IM_HIDE bool jpg_resync(const jdata_t *d, uint32_t *next_rst, uint32_t *cur);

/* lossless (H.2): a scan's components' rows of differences, the
   predictors on them (libjpeg's jddiffct.c, jdlossls.c): an MCU row of
   differences at a time (restarts come between rows); each component
   predicted row by row from the row above (kept as it was predicted,
   modulo 2^16) and the sample before, the first row after a scan's start,
   a restart or a row of zeros by the one before alone, from
   2^(P - Pt - 1); samples out shifted by Pt */
typedef struct jloss {
  int32_t  *diff[J_COMPS];  /* a component's rows of an MCU row */
  uint16_t *prev[J_COMPS];  /* its row above, unshifted */
  bool      first[J_COMPS]; /* its next row predicted as a first one */
  uint32_t  y[J_COMPS];     /* its next sample row */
} jloss_t;

IM_HIDE bool jpg_loss_start(jpg_t *j, jloss_t *L);
IM_HIDE void jpg_loss_end(jloss_t *L);
/* a row of component ci's differences predicted, to its plane */
IM_HIDE void jpg_undiff(jpg_t *j, jloss_t *L, uint32_t ci, const int32_t *d);
IM_HIDE bool jpg_scan_lossless(jpg_t *j, im_pool_t *pool);

/* arith.c */
IM_HIDE void jpg_flow_row(jpg_t *j, uint32_t row);
IM_HIDE bool jpg_scan_arith(jpg_t *j);
IM_HIDE bool jpg_scan_arith_lossless(jpg_t *j, im_pool_t *pool);

/* idct.c */

/* a block with only its DC: its every sample */
IM_HIDE uint8_t jpg_dc8(int16_t dc, uint16_t q);
IM_HIDE void jpg_qlim(uint16_t q[128]);
IM_HIDE void jpg_idct8(const int16_t *in, const uint16_t *q, void *out, size_t stride);
IM_HIDE jpg_idct_fn jpg_idct8_best(void);
IM_HIDE void jpg_idct12(const int16_t *in, const uint16_t *q, void *out, size_t stride);

/* the IDCT to a block `side` samples across (8, 4, 2, 1) at `prec` bits */
IM_HIDE jpg_idct_fn jpg_idct_for(uint32_t side, uint32_t prec);
/* a differential frame's IDCT (hierarchical): signed 32-bit samples out */
IM_HIDE jpg_idct_fn jpg_idct_diff(uint32_t prec);

/* sample.c */
IM_HIDE bool jpg_out_ok(const jpg_t *j, jspace_t space);
IM_HIDE bool jpg_out(jpg_t *j, jspace_t space, ImImage *im, ImByteOrder order);
typedef struct jsout jsout_t;
IM_HIDE jsout_t *jpg_out_begin(jpg_t *j, jspace_t space, ImByteOrder order);
IM_HIDE void jpg_out_rows(jsout_t *o, uint32_t worker, uint32_t y0, uint32_t y1);
IM_HIDE bool jpg_out_end(jsout_t *o, ImImage *im, bool run);

/* a block of the scan's component `c` at (bx, by) done: into its plane
   (one pass) or kept (multi-scan) */
IM_HIDE void jpg_put(jpg_t *j, jcomp_t *c, int16_t *blk, uint32_t bx, uint32_t by);

/* the natural order of zig-zag position k, and 16 more for a damaged
   run past the end (libjpeg's jpeg_natural_order) */
IM_HIDE extern const uint8_t jpg_natural[128];

#endif /* src_io_jpg_jpgint_h */
