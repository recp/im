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
 * JPEG 2000 (ISO/IEC 15444-1), im's own: a JP2 (or JPH) file or a bare
 * codestream; tiles and tile-parts, precincts, the five progressions and
 * POC, layers, every code-block style, HT code-blocks (ISO/IEC 15444-15: a
 * tile-component's all, one HT set), packet headers in PPM or PPT, SOP and EPH,
 * ROI's max-shift; the 5/3 and 9/7 wavelets, both colour transforms,
 * subsampled components; a JP2's palette, component mapping and channel
 * definitions, sYCC. The arithmetic is OpenJPEG's: its 9/7 constants and
 * order of operations in single precision (nothing fused), its rounding
 * and its sYCC conversion, so 8-bit pixels come out as OpenJPEG's do. A
 * truncated stream decodes as far as it goes.
 *
 * Out: gray, gray + alpha, RGB, RGBA (the opacity channel last, left
 * premultiplied where the JP2 says it is), CMYK; 8 bits a sample, or 16
 * where a channel is deeper than 8; a palette expanded, sYCC made RGB, the
 * JP2's ICC profile kept.
 */

#include "jp2.h"
#if defined(_MSC_VER) && !defined(__clang__)
#  include <intrin.h>
#endif

#if !IM_CODECS_ONLY
#  include "../../file.h"
#  ifdef __APPLE__
#    include "../apple/coreimg.h"
#  endif
#endif

#include <math.h>
#include <stdlib.h>
#include <string.h>

#if (defined(__ARM_NEON) || defined(_M_ARM64)) && !defined(IM_NO_SIMD)
#  include <arm_neon.h>
#  define X_NEON 1
#elif (defined(__SSE2__) || defined(_M_X64)) && !defined(IM_NO_SIMD)
#  include <emmintrin.h>
#  define X_SSE2 1
#elif defined(__wasm_simd128__) && !defined(IM_NO_SIMD)
#  include <wasm_simd128.h>
#  define X_WASM 1
#endif

#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

enum {
  X_COMPS  = 32,
  X_LEVELS = 32,
  X_BANDS  = 1 + 3 * X_LEVELS,
  X_POCS   = 32,
  X_PIXELS = 1u << 30,  /* an image's (as JPEG's a component's) */
  X_BLOCKS = 1u << 22,  /* code-blocks a tile */
  X_PRECS  = 1u << 22,  /* precincts a tile */
  X_PAL    = 1024,      /* a palette's entries */
  X_ARENA  = 1u << 30   /* a tile's structures and data */
};

#ifndef X_BIG
#  define X_BIG ((uint64_t)256u << 20) /* (a test build can lower it) */
#endif
/* a big image's tile-component's samples from this many bytes mapped (less:
   the tile's arena, a small tile not mapped for each) */
#ifndef X_MAP_MIN
#  define X_MAP_MIN ((uint64_t)32u << 20)
#endif

/* -- memory: a bump allocator on malloc ---------------------------------------- */

/* everything a decode (or a tile) parses lives as long as it does, every
   byte charged to its budget */

enum { BLOCK_MIN = 4 * 1024, BLOCK_MAX = 1024 * 1024 }; /* a block's bytes: the first, the most */

typedef struct arena_block arena_block_t;

struct arena_block {
  arena_block_t *next;
  size_t         used;
  size_t         cap;
  bool           mapped; /* a big image's big block: from the system */
  /* data follows */
};

typedef struct arena {
  arena_block_t *head;
  uint64_t       used;  /* charged, blocks included */
  uint64_t       limit;
  size_t         grow;  /* the next block's bytes (0: BLOCK_MIN), twice each
                           block's to BLOCK_MAX */
  bool           big;   /* blocks of X_MAP_MIN or more mapped (given back
                           the moment they're let go) */
} arena_t;

static void*
arena_alloc(arena_t *arena, uint64_t want) {
  arena_block_t *b;
  size_t         cap, size;
  uint8_t       *p;

  if (want > (uint64_t)SIZE_MAX - sizeof(*b) - 64u) return NULL;
  size = ((size_t)want + 15u) & ~(size_t)15u;
  b    = arena->head;

  if (!b || b->cap - b->used < size) {
    size_t step = arena->grow ? arena->grow : BLOCK_MIN;
    bool   map;

    cap = size > step / 4 ? size : step;
    map = arena->big && cap >= X_MAP_MIN;
    if (size > SIZE_MAX - sizeof(*b) - 16u || arena->used + cap + sizeof(*b) > arena->limit
        || !(b = im_big_alloc(sizeof(*b) + 16u + cap, false, map))) {
      return NULL;
    }
    b->mapped    = map;
    b->cap       = cap;
    b->used      = 0u;
    arena->used += cap + sizeof(*b);
    if (cap == step) arena->grow = step < BLOCK_MAX ? step * 2u : BLOCK_MAX;

    /* a large request gets a block of its own behind the current one */
    if (cap > step && arena->head) {
      b->next           = arena->head->next;
      arena->head->next = b;
    } else {
      b->next     = arena->head;
      arena->head = b;
    }
  }

  p        = (uint8_t *)(((uintptr_t)(b + 1) + 15u) & ~(uintptr_t)15u) + b->used;
  b->used += size;

  return p;
}

static void
arena_free_all(arena_t *arena) {
  arena_block_t *b, *next;

  for (b = arena->head; b; b = next) {
    next = b->next;
    im_big_free(b, sizeof(*b) + 16u + b->cap, b->mapped);
  }
  arena->head = NULL;
  arena->used = 0u;
}

/* code-block styles (SPcod) */
enum { CB_BYPASS = 1, CB_RESET = 2, CB_TERMALL = 4, CB_CAUSAL = 8, CB_PTERM = 16, CB_SEGSYM = 32, CB_HT = 64 };

/* progressions */
enum { LRCP, RLCP, RPCL, PCRL, CPRL };

static uint32_t
be16(const uint8_t *p) {
  return (uint32_t)p[0] << 8 | p[1];
}

static uint32_t
be32(const uint8_t *p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static void*
zget(arena_t *a, uint64_t n) {
  void *p = arena_alloc(a, n ? n : 1u);

  if (p) memset(p, 0, (size_t)(n ? n : 1u));
  return p;
}

/* ⌈a / 2^k⌉ and ⌊a / 2^k⌋, a of either sign */
static int64_t
ceil_pow2(int64_t a, uint32_t k) {
  int64_t d = (int64_t)1 << k;

  return a >= 0 ? (a + d - 1) / d : -((-a) / d);
}

static int64_t
floor_pow2(int64_t a, uint32_t k) {
  int64_t d = (int64_t)1 << k;

  return a >= 0 ? a / d : -((-a + d - 1) / d);
}

static int64_t
ceil_div_u(int64_t a, uint32_t b) {
  return (a + (int64_t)b - 1) / (int64_t)b;
}

static uint32_t
floor_log2(uint32_t v) {
  uint32_t n = 0;

  while (v >>= 1) n++;
  return n;
}

/* -- coding parameters (COD, COC, QCD, QCC, RGN, POC) --------------------- */

typedef struct xcc {
  uint8_t  levels;
  uint8_t  xcb, ycb;    /* code-block exponents */
  uint8_t  style;
  uint8_t  reversible;  /* 5/3 */
  uint8_t  qstyle;      /* 0 none, 1 derived, 2 expounded */
  uint8_t  guard;
  uint8_t  roi;         /* RGN's shift */
  uint8_t  pp[X_LEVELS + 1]; /* a resolution's precincts: PPy << 4 | PPx */
  uint16_t steps[X_BANDS];   /* exponent << 11 | mantissa */
} xcc_t;

typedef struct xpoc {
  uint8_t  rs, re, order;
  uint16_t cs, ce, le;
} xpoc_t;

typedef struct xcod {
  uint8_t  scod;   /* 1 precincts given, 2 SOP, 4 EPH */
  uint8_t  order;
  uint8_t  mct;
  uint16_t layers;
  uint32_t npoc;
  xpoc_t   poc[X_POCS];
  xcc_t    cc[X_COMPS];
} xcod_t;

/* a tile-part: its header's markers, its packets, its share of PPM */
typedef struct xpart {
  uint32_t       tile;
  const uint8_t *head;
  size_t         head_len;
  const uint8_t *data;
  size_t         len;
  const uint8_t *ppm;
  size_t         ppm_len;
} xpart_t;

/* what the JP2 boxes say */
typedef struct jp2 {
  uint32_t enumcs;    /* colr's enumerated space; 0 none */
  uint32_t icc;       /* or an ICC profile's data colour space */
  const uint8_t *icc_data; /* and the profile */
  size_t   icc_len;
  bool     colr;
  uint32_t pal_n, pal_cols;
  uint8_t  pal_bits[X_COMPS];
  int32_t *pal;       /* pal_n × pal_cols */
  uint32_t nmap;
  struct {
    uint16_t cmp;
    uint8_t  palette, col;
  } map[X_COMPS];
  uint32_t ndef;
  struct {
    uint16_t cn, typ, asoc;
  } def[X_COMPS];
} jp2_t;

/* a worker's scratch: a code-block's states, values and bytes, a
   wavelet's lanes */
typedef struct xscratch {
  uint32_t *flags;
  int32_t  *cdata;
  uint8_t  *bytes;
  size_t    bytes_cap;
  void     *lift;
  size_t    lift_len;
  uint8_t  *ht;       /* an HT code-block's state (HT_SCRATCH bytes) */
} xscratch_t;

enum { X_THREADS = 16 }; /* a decode's, the caller's among them */

/* pixels a thread at least: none below twice this (a test build can lower
   it); JPEG 2000's a pixel cost about five of JPEG's */
#ifndef X_POOL_MIN
#  define X_POOL_MIN (1u << 17)
#endif

struct jpx;
typedef bool (*xjob_fn)(struct jpx *j, xscratch_t *s, void *ctx, uint32_t item);

/* threads for a large image: items handed out in turn, the caller working
   too; a round ends when every item is done */
typedef struct xpool {
  th_thread_mutex m;
  th_thread_cond  go, idle;
  xjob_fn         fn;
  void           *ctx;
  uint32_t        items, next, busy, round;
  bool            quit, failed;
} xpool_t;

typedef struct xworker {
  struct jpx *j;
  uint32_t    id;
  th_thread  *thread;
} xworker_t;

typedef struct jpx {
  arena_t   arena;
  uint32_t  x1, y1, x0, y0, tw, th, tx0, ty0, ntx, nty, ncomp;
  uint32_t  reduce;        /* the finest levels left out: the image that many halvings smaller */
  uint8_t   prec[X_COMPS], sgnd[X_COMPS], dx[X_COMPS], dy[X_COMPS];
  bool      skip[X_COMPS]; /* not wanted: its code-blocks, wavelet and samples left out */
  xcod_t    cod;
  bool      coc[X_COMPS], qcc[X_COMPS];
  uint8_t  *ppm;
  size_t    ppm_len;
  xpart_t  *parts;
  uint32_t  nparts;
  /* the components decoded: a sample 16 bits, made unsigned */
  uint16_t *plane[X_COMPS];
  uint32_t  pw[X_COMPS], ph[X_COMPS], px0[X_COMPS], py0[X_COMPS];
  /* a big image's (X_BIG bytes or more of planes and a tile's samples):
     both mapped, given back as they're done with (mm/mmap.h) */
  bool      big;
  /* a plane's row y in row y & pmask (a ring's: a big tile by bands; else
     all ones); its bytes, and whether mapped */
  size_t    pmask[X_COMPS], psize[X_COMPS];
  bool      pmap;
  struct xstream *stream; /* the one tile, by bands (NULL: tiles whole) */
  bool      rows;          /* tiles a row at a time, each row composed as it's
                              done (tiles_run); the planes rings */
  uint32_t  ox0, oy0, ox1, oy1; /* the grid as SIZ has it (x0 … y1 made
                                   smaller for compose) */
  xscratch_t scratch[X_THREADS];
  xpool_t    pool;
  xworker_t  worker[X_THREADS - 1];
  uint32_t   workers; /* threads besides the caller's */
  uint32_t   threads; /* at most, the caller's among them; 0 as many as help */
  im_runner_t runner;  /* the caller's threads (run NULL: none) */
  uint8_t    zc[4][512], sc[256];
  uint16_t   htv[2][8][128]; /* HT code-blocks' CxtVLC lookup (ht_tables) */
} jpx_t;

/* SPcod / SPcoc: the coding parts of `c` */
static bool
read_spcod(xcc_t *c, const uint8_t *s, size_t len, bool precincts) {
  uint32_t i;

  if (len < 5u) return false;
  c->levels     = s[0];
  c->xcb        = (uint8_t)((s[1] & 15u) + 2u);
  c->ycb        = (uint8_t)((s[2] & 15u) + 2u);
  c->style      = s[3];
  c->reversible = s[4] == 1u;
  /* HT code-blocks a tile-component's all or none (T.814 A.4: bit 7 their
     mix, not read) */
  if (c->levels > X_LEVELS || c->xcb > 10u || c->ycb > 10u || c->xcb + c->ycb > 12u || s[4] > 1u
      || (c->style & 0x80u)) {
    return false;
  }
  for (i = 0; i <= c->levels; i++) {
    if (!precincts) {
      c->pp[i] = 0xFFu;
      continue;
    }
    if (5u + i >= len) return false;
    c->pp[i] = s[5u + i];
    if (i && (!(c->pp[i] & 15u) || !(c->pp[i] >> 4))) return false;
  }
  return true;
}

static void
copy_spcod(xcc_t *to, const xcc_t *from) {
  to->levels     = from->levels;
  to->xcb        = from->xcb;
  to->ycb        = from->ycb;
  to->style      = from->style;
  to->reversible = from->reversible;
  memcpy(to->pp, from->pp, sizeof(to->pp));
}

/* SPqcd / SPqcc: the quantization parts */
static bool
read_spqcd(xcc_t *c, const uint8_t *s, size_t len) {
  uint32_t style, n, i;

  if (len < 1u) return false;
  style = s[0] & 31u;
  memset(c->steps, 0, sizeof(c->steps));
  if (style == 0u) {
    n = (uint32_t)(len - 1u);
    for (i = 0; i < n && i < X_BANDS; i++) c->steps[i] = (uint16_t)((s[1u + i] >> 3) << 11);
  } else if (style == 1u) {
    uint32_t e;

    if (len < 3u) return false;
    c->steps[0] = (uint16_t)be16(s + 1);
    e = c->steps[0] >> 11;
    for (i = 1; i < X_BANDS; i++) {
      uint32_t ei = e > (i - 1u) / 3u ? e - (i - 1u) / 3u : 0u;

      c->steps[i] = (uint16_t)(ei << 11 | (c->steps[0] & 0x7FFu));
    }
  } else if (style == 2u) {
    n = (uint32_t)((len - 1u) / 2u);
    for (i = 0; i < n && i < X_BANDS; i++) c->steps[i] = (uint16_t)be16(s + 1u + 2u * i);
  } else {
    return false;
  }
  c->qstyle = (uint8_t)style;
  c->guard  = s[0] >> 5;
  return true;
}

/* one marker segment of a main or tile-part header into `cod`; `coc` and
   `qcc` the components that have their own here (a COD or QCD leaves
   those). False: the stream can't be read */
static bool
coding_marker(jpx_t *j, xcod_t *cod, bool *coc, bool *qcc, bool *poc_seen, uint32_t m, const uint8_t *s, size_t len) {
  uint32_t csize = j->ncomp < 257u ? 1u : 2u, c, i;
  xcc_t    t;

  switch (m) {
  case 0xFF52: /* COD */
    if (len < 5u) return false;
    memset(&t, 0, sizeof(t));
    if (!read_spcod(&t, s + 5, len - 5u, s[0] & 1u) || s[1] > 4u || !be16(s + 2)) return false;
    cod->scod   = s[0];
    cod->order  = s[1];
    cod->layers = (uint16_t)be16(s + 2);
    cod->mct    = s[4];
    for (c = 0; c < j->ncomp; c++) {
      if (!coc[c]) copy_spcod(&cod->cc[c], &t);
    }
    return true;
  case 0xFF53: /* COC */
    if (len < csize + 1u) return false;
    c = csize == 1u ? s[0] : be16(s);
    if (c >= j->ncomp) return false;
    memset(&t, 0, sizeof(t));
    if (!read_spcod(&t, s + csize + 1u, len - csize - 1u, s[csize] & 1u)) return false;
    copy_spcod(&cod->cc[c], &t);
    coc[c] = true;
    return true;
  case 0xFF5C: /* QCD */
    memset(&t, 0, sizeof(t));
    if (!read_spqcd(&t, s, len)) return false;
    for (c = 0; c < j->ncomp; c++) {
      if (qcc[c]) continue;
      cod->cc[c].qstyle = t.qstyle;
      cod->cc[c].guard  = t.guard;
      memcpy(cod->cc[c].steps, t.steps, sizeof(t.steps));
    }
    return true;
  case 0xFF5D: /* QCC */
    if (len < csize) return false;
    c = csize == 1u ? s[0] : be16(s);
    if (c >= j->ncomp || !read_spqcd(&cod->cc[c], s + csize, len - csize)) return false;
    qcc[c] = true;
    return true;
  case 0xFF5E: /* RGN */
    if (len < csize + 2u) return false;
    c = csize == 1u ? s[0] : be16(s);
    if (c < j->ncomp && s[csize] == 0u) cod->cc[c].roi = s[csize + 1u];
    return true;
  case 0xFF5F: { /* POC: a tile's own replace the main header's */
    uint32_t step = 5u + 2u * csize;

    if (!*poc_seen) cod->npoc = 0;
    *poc_seen = true;
    for (i = 0; i + step <= len && cod->npoc < X_POCS; i += step) {
      const uint8_t *e = s + i;
      xpoc_t        *p = &cod->poc[cod->npoc++];

      p->rs    = e[0];
      p->cs    = (uint16_t)(csize == 1u ? e[1] : be16(e + 1));
      p->le    = (uint16_t)be16(e + 1 + csize);
      p->re    = e[3 + csize];
      p->ce    = (uint16_t)(csize == 1u ? e[4 + csize] : be16(e + 4 + csize));
      p->order = e[4 + 2 * csize];
      if (csize == 1u && !p->ce) p->ce = 256u;
      if (p->order > 4u) return false;
    }
    return true;
  }
  default:
    return true;
  }
}

/* SIZ */
static bool
read_siz(jpx_t *j, const uint8_t *s, size_t len) {
  uint32_t c;
  uint64_t w, h;

  if (len < 38u) return false;
  j->x1    = be32(s + 2);
  j->y1    = be32(s + 6);
  j->x0    = be32(s + 10);
  j->y0    = be32(s + 14);
  j->tw    = be32(s + 18);
  j->th    = be32(s + 22);
  j->tx0   = be32(s + 26);
  j->ty0   = be32(s + 30);
  j->ncomp = be16(s + 34);
  if (!j->ncomp || j->ncomp > X_COMPS || len < 36u + 3u * j->ncomp || j->x0 >= j->x1 || j->y0 >= j->y1 || !j->tw
      || !j->th || j->tx0 > j->x0 || j->ty0 > j->y0 || (uint64_t)j->tx0 + j->tw <= j->x0
      || (uint64_t)j->ty0 + j->th <= j->y0) {
    return false;
  }
  w = j->x1 - j->x0;
  h = j->y1 - j->y0;
  if (w * h > X_PIXELS) return false;
  j->ntx = (uint32_t)(((uint64_t)j->x1 - j->tx0 + j->tw - 1u) / j->tw);
  j->nty = (uint32_t)(((uint64_t)j->y1 - j->ty0 + j->th - 1u) / j->th);
  if ((uint64_t)j->ntx * j->nty > 65535u) return false;
  for (c = 0; c < j->ncomp; c++) {
    const uint8_t *e = s + 36 + 3 * c;

    j->prec[c] = (uint8_t)((e[0] & 0x7Fu) + 1u);
    j->sgnd[c] = e[0] >> 7;
    j->dx[c]   = e[1];
    j->dy[c]   = e[2];
    if (j->prec[c] > 16u || !j->dx[c] || !j->dy[c]) return false;
  }
  return true;
}

/* the main header, then where each tile-part is */
static bool
read_codestream(jpx_t *j, const uint8_t *p, size_t len) {
  size_t   pos = 2u, cap = 0u;
  bool     siz = false, cod = false, qcd = false, poc_seen = false;
  uint32_t ppm_at = 0u;

  if (len < 4u || be16(p) != 0xFF4Fu) return false;
  /* the main header */
  while (pos + 4u <= len) {
    uint32_t m = be16(p + pos), l;

    if (m == 0xFF90u) break;
    l = be16(p + pos + 2u);
    if (l < 2u || pos + 2u + l > len) return false;
    if (m == 0xFF51u) {
      if (siz || !read_siz(j, p + pos + 4u, l - 2u)) return false;
      siz = true;
    } else if (!siz) {
      return false;
    } else if (m == 0xFF60u) { /* PPM: Zppm, then Nppm and Ippm, across segments */
      uint8_t *grown;

      if (l < 3u) return false;
      if (j->ppm_len + l > cap) {
        cap   = (j->ppm_len + l) * 2u;
        grown = arena_alloc(&j->arena, cap);
        if (!grown) return false;
        if (j->ppm_len) memcpy(grown, j->ppm, j->ppm_len);
        j->ppm = grown;
      }
      memcpy(j->ppm + j->ppm_len, p + pos + 5u, l - 3u);
      j->ppm_len += l - 3u;
    } else {
      if (m == 0xFF52u) cod = true;
      if (m == 0xFF5Cu) qcd = true;
      if (!coding_marker(j, &j->cod, j->coc, j->qcc, &poc_seen, m, p + pos + 4u, l - 2u)) return false;
    }
    pos += 2u + l;
  }
  if (!siz || !cod || !qcd) return false;

  /* the tile-parts */
  {
    size_t   pos0 = pos, n = 0u;
    uint32_t pass;

    for (pass = 0; pass < 2u; pass++) {
      pos = pos0;
      if (pass) {
        if (!n) return false;
        if (!(j->parts = zget(&j->arena, (uint64_t)n * sizeof(*j->parts)))) return false;
      }
      n = 0u;
      while (pos + 12u <= len && be16(p + pos) == 0xFF90u) {
        uint32_t lsot = be16(p + pos + 2u), isot = be16(p + pos + 4u), psot = be32(p + pos + 6u);
        size_t   end  = psot ? pos + psot : len, h;

        if (lsot != 10u) return false;
        if (end > len || end < pos + 14u) end = len;
        /* its header, up to SOD */
        h = pos + 12u;
        while (h + 2u <= end && be16(p + h) != 0xFF93u) {
          if (h + 4u > end) break;
          h += 2u + be16(p + h + 2u);
        }
        if (h + 2u > end || be16(p + h) != 0xFF93u) break;
        if (pass && isot < j->ntx * j->nty) {
          xpart_t *t = &j->parts[n];

          t->tile     = isot;
          t->head     = p + pos + 12u;
          t->head_len = h - pos - 12u;
          t->data     = p + h + 2u;
          t->len      = end - h - 2u;
          /* trailing EOC isn't data */
          if (!psot && t->len >= 2u && be16(t->data + t->len - 2u) == 0xFFD9u) t->len -= 2u;
          /* its packet headers from PPM: Nppm, then as many bytes */
          if (j->ppm && ppm_at + 4u <= j->ppm_len) {
            uint32_t nppm = be32(j->ppm + ppm_at);

            if (nppm > j->ppm_len - ppm_at - 4u) nppm = (uint32_t)(j->ppm_len - ppm_at - 4u);
            t->ppm     = j->ppm + ppm_at + 4u;
            t->ppm_len = nppm;
            ppm_at += 4u + nppm;
          }
        }
        if (isot < j->ntx * j->nty) n++;
        pos = end;
      }
      if (pass) j->nparts = (uint32_t)n;
    }
  }
  return true;
}

/* -- tag trees (B.10.2) and the packet headers' bits ------------------------ */

typedef struct bio {
  const uint8_t *s;
  size_t         pos, len;
  uint32_t       buf, ct;
} bio_t;

static uint32_t
bio_bit(bio_t *b) {
  if (!b->ct) {
    b->buf = (b->buf << 8) & 0xFFFFu;
    b->ct  = b->buf == 0xFF00u ? 7u : 8u; /* a byte after 0xFF has seven */
    if (b->pos < b->len) b->buf |= b->s[b->pos++];
  }
  b->ct--;
  return (b->buf >> b->ct) & 1u;
}

static uint32_t
bio_bits(bio_t *b, uint32_t n) {
  uint32_t v = 0;

  while (n--) v = v << 1 | bio_bit(b);
  return v;
}

static void
bio_align(bio_t *b) {
  if ((b->buf & 0xFFu) == 0xFFu) {
    b->buf = (b->buf << 8) & 0xFFFFu;
    if (b->pos < b->len) b->buf |= b->s[b->pos++];
  }
  b->ct = 0;
}

typedef struct xtag {
  int32_t  value, low;
  uint32_t parent; /* UINT32_MAX the root */
} xtag_t;

static xtag_t*
tag_new(arena_t *a, uint32_t w, uint32_t h) {
  uint32_t n = 0, lw = w, lh = h, at, i, x, y;
  xtag_t  *t;

  for (;;) {
    n += lw * lh;
    if (lw == 1u && lh == 1u) break;
    lw = (lw + 1u) / 2u;
    lh = (lh + 1u) / 2u;
  }
  if (!(t = arena_alloc(a, (uint64_t)n * sizeof(*t)))) return NULL;
  for (i = 0; i < n; i++) {
    t[i].value  = INT32_MAX;
    t[i].low    = 0;
    t[i].parent = UINT32_MAX;
  }
  /* each level's nodes after the one below */
  at = 0u;
  lw = w;
  lh = h;
  while (lw > 1u || lh > 1u) {
    uint32_t pw = (lw + 1u) / 2u, ph = (lh + 1u) / 2u, next = at + lw * lh;

    for (y = 0; y < lh; y++) {
      for (x = 0; x < lw; x++) t[at + y * lw + x].parent = next + (y / 2u) * pw + x / 2u;
    }
    at = next;
    lw = pw;
    lh = ph;
  }
  return t;
}

/* whether the leaf's value is below `threshold`, reading what it takes */
static bool
tag_decode(bio_t *b, xtag_t *t, uint32_t leaf, int32_t threshold) {
  uint32_t stack[48], n = 0, i = leaf;
  int32_t  low = 0;

  while (t[i].parent != UINT32_MAX && n < 48u) {
    stack[n++] = i;
    i          = t[i].parent;
  }
  for (;;) {
    if (low > t[i].low) t[i].low = low;
    else low = t[i].low;
    while (low < threshold && low < t[i].value) {
      if (bio_bit(b)) t[i].value = low;
      else low++;
    }
    t[i].low = low;
    if (!n) break;
    i = stack[--n];
  }
  return t[i].value < threshold;
}

/* -- a tile's structures (B.5 to B.9) ---------------------------------------- */

typedef struct xseg {
  uint32_t len, newlen;
  uint16_t passes, max, newpasses;
} xseg_t;

typedef struct xpiece {
  const uint8_t *p;
  uint32_t       len;
} xpiece_t;

typedef struct xcblk {
  int32_t   x0, y0, x1, y1;
  int32_t   numbps;  /* bit-planes coded */
  uint32_t  lblock;
  uint32_t  nsegs;   /* with data so far; 0 never included */
  uint32_t  segcap;
  xseg_t   *segs;
  uint32_t  newseg, newsegs; /* this packet's */
  xpiece_t *pieces;
  uint32_t  npieces, piececap;
} xcblk_t;

typedef struct xpband { /* a precinct in one band */
  uint32_t cw, ch;
  xcblk_t *blk;
  xtag_t  *incl, *zero;
} xpband_t;

typedef struct xband {
  int32_t  x0, y0, x1, y1;
  uint32_t orient; /* 0 LL, 1 HL, 2 LH, 3 HH */
  int32_t  mb;     /* Mb */
  float    step;   /* Δb as OpenJPEG has it (no gain for 9/7) */
  uint32_t offx, offy;
} xband_t;

typedef struct xres {
  int32_t   x0, y0, x1, y1;
  uint32_t  pw, ph, ppx, ppy, xcb, ycb, nbands;
  xband_t   band[3];
  xpband_t *prec; /* pw × ph × nbands */
  uint16_t *done; /* layers read, a precinct */
} xres_t;

typedef struct xtc {
  int32_t      x0, y0, x1, y1;
  uint32_t     nres, nuse; /* its resolutions; those decoded (the rest left out) */
  xres_t      *res;        /* nres, in the tile's arena */
  void        *data; /* int32, or float for 9/7 */
  size_t       mapped; /* data's bytes where mapped (a big image's big
                          tile), given back as its rows are placed; 0:
                          the tile's arena's */
  const xcc_t *cc;
} xtc_t;

typedef struct xtile {
  arena_t        arena;
  int64_t        x0, y0, x1, y1;
  xcod_t         cod;
  xtc_t         *tc;    /* ncomp, in its arena */
  uint32_t       ncomp;
  const uint8_t *body;
  size_t         body_pos, body_len;
  const uint8_t *hdr; /* PPM's or PPT's headers; NULL in the body */
  size_t         hdr_pos, hdr_len;
  bool           stop;
  uint32_t       blocks, precs;
} xtile_t;

static bool
seg_add(xtile_t *t, xcblk_t *b, uint32_t s, uint8_t style) {
  xseg_t *g;

  if (s >= 256u) return false;
  if (s >= b->segcap) {
    uint32_t cap  = b->segcap ? b->segcap * 2u : 2u;
    xseg_t  *next = arena_alloc(&t->arena, (uint64_t)cap * sizeof(*next));

    if (!next) return false;
    if (b->segcap) memcpy(next, b->segs, b->segcap * sizeof(*next));
    b->segs   = next;
    b->segcap = cap;
  }
  g = &b->segs[s];
  memset(g, 0, sizeof(*g));
  if (style & CB_HT) g->max = !s ? 1u : 109u; /* the cleanup pass alone, then the rest (as OpenJPEG) */
  else if (style & CB_TERMALL) g->max = 1u;
  else if (style & CB_BYPASS) g->max = !s ? 10u : (b->segs[s - 1u].max == 1u || b->segs[s - 1u].max == 10u) ? 2u : 1u;
  else g->max = 109u;
  return true;
}

static bool
piece_add(xtile_t *t, xcblk_t *b, const uint8_t *p, uint32_t len) {
  if (b->npieces >= b->piececap) {
    uint32_t  cap  = b->piececap ? b->piececap * 2u : 1u;
    xpiece_t *next = arena_alloc(&t->arena, (uint64_t)cap * sizeof(*next));

    if (!next) return false;
    if (b->piececap) memcpy(next, b->pieces, b->piececap * sizeof(*next));
    b->pieces   = next;
    b->piececap = cap;
  }
  b->pieces[b->npieces].p   = p;
  b->pieces[b->npieces].len = len;
  b->npieces++;
  return true;
}

/* a tile-component's resolutions, bands, precincts and code-blocks */
static bool
tc_build(jpx_t *j, xtile_t *t, uint32_t c) {
  xtc_t       *tc = &t->tc[c];
  const xcc_t *cc = &t->cod.cc[c];
  uint32_t     r, nres = cc->levels + 1u;

  if (cc->levels < j->reduce) return false; /* fewer than the image was made smaller by */
  if (!(tc->res = zget(&t->arena, (uint64_t)nres * sizeof(*tc->res)))) return false;
  tc->cc   = cc;
  tc->nuse = nres - j->reduce;
  tc->x0   = (int32_t)ceil_div_u(t->x0, j->dx[c]);
  tc->y0   = (int32_t)ceil_div_u(t->y0, j->dy[c]);
  tc->x1   = (int32_t)ceil_div_u(t->x1, j->dx[c]);
  tc->y1   = (int32_t)ceil_div_u(t->y1, j->dy[c]);
  tc->nres = nres;
  for (r = 0; r < nres; r++) {
    xres_t  *res   = &tc->res[r];
    uint32_t level = nres - 1u - r, b, p;

    res->x0     = (int32_t)ceil_pow2(tc->x0, level);
    res->y0     = (int32_t)ceil_pow2(tc->y0, level);
    res->x1     = (int32_t)ceil_pow2(tc->x1, level);
    res->y1     = (int32_t)ceil_pow2(tc->y1, level);
    res->ppx    = cc->pp[r] & 15u;
    res->ppy    = cc->pp[r] >> 4;
    res->nbands = r ? 3u : 1u;
    if (res->x1 > res->x0 && res->y1 > res->y0) {
      res->pw = (uint32_t)(ceil_pow2(res->x1, res->ppx) - floor_pow2(res->x0, res->ppx));
      res->ph = (uint32_t)(ceil_pow2(res->y1, res->ppy) - floor_pow2(res->y0, res->ppy));
    }
    res->xcb = cc->xcb < res->ppx - (r ? 1u : 0u) ? cc->xcb : res->ppx - (r ? 1u : 0u);
    res->ycb = cc->ycb < res->ppy - (r ? 1u : 0u) ? cc->ycb : res->ppy - (r ? 1u : 0u);

    for (b = 0; b < res->nbands; b++) {
      xband_t *band = &res->band[b];
      uint32_t s, expn, mant;

      band->orient = r ? b + 1u : 0u;
      if (!r) {
        band->x0 = res->x0;
        band->y0 = res->y0;
        band->x1 = res->x1;
        band->y1 = res->y1;
      } else {
        int64_t xo = (int64_t)(band->orient & 1u) << level, yo = (int64_t)(band->orient >> 1) << level;

        band->x0   = (int32_t)ceil_pow2(tc->x0 - xo, level + 1u);
        band->y0   = (int32_t)ceil_pow2(tc->y0 - yo, level + 1u);
        band->x1   = (int32_t)ceil_pow2(tc->x1 - xo, level + 1u);
        band->y1   = (int32_t)ceil_pow2(tc->y1 - yo, level + 1u);
        band->offx = band->orient & 1u ? (uint32_t)(tc->res[r - 1u].x1 - tc->res[r - 1u].x0) : 0u;
        band->offy = band->orient & 2u ? (uint32_t)(tc->res[r - 1u].y1 - tc->res[r - 1u].y0) : 0u;
      }
      s        = r ? 1u + 3u * (r - 1u) + b : 0u;
      expn     = cc->steps[s] >> 11;
      mant     = cc->steps[s] & 0x7FFu;
      band->mb = (int32_t)cc->guard + (int32_t)expn - 1;
      band->step = (float)((1.0 + mant / 2048.0) * ldexp(1.0, (int)j->prec[c] - (int)expn));
    }

    if (!res->pw || !res->ph) continue;
    if ((uint64_t)res->pw * res->ph > X_PRECS - t->precs) return false;
    t->precs += res->pw * res->ph;
    res->prec = zget(&t->arena, (uint64_t)res->pw * res->ph * res->nbands * sizeof(*res->prec));
    res->done = zget(&t->arena, (uint64_t)res->pw * res->ph * sizeof(*res->done));
    if (!res->prec || !res->done) return false;
    for (p = 0; p < res->pw * res->ph; p++) {
      int64_t prx = (floor_pow2(res->x0, res->ppx) + p % res->pw) << res->ppx;
      int64_t pry = (floor_pow2(res->y0, res->ppy) + p / res->pw) << res->ppy;

      for (b = 0; b < res->nbands; b++) {
        const xband_t *band = &res->band[b];
        xpband_t      *pb   = &res->prec[p * res->nbands + b];
        int64_t        bx = r ? prx >> 1 : prx, by = r ? pry >> 1 : pry;
        uint32_t       ex = r ? res->ppx - 1u : res->ppx, ey = r ? res->ppy - 1u : res->ppy, u, v;
        int64_t        x0 = bx > band->x0 ? bx : band->x0, y0 = by > band->y0 ? by : band->y0;
        int64_t        x1 = bx + ((int64_t)1 << ex) < band->x1 ? bx + ((int64_t)1 << ex) : band->x1;
        int64_t        y1 = by + ((int64_t)1 << ey) < band->y1 ? by + ((int64_t)1 << ey) : band->y1;
        int64_t        cbx, cby;

        if (x0 >= x1 || y0 >= y1) continue;
        cbx    = floor_pow2(x0, res->xcb);
        cby    = floor_pow2(y0, res->ycb);
        pb->cw = (uint32_t)(ceil_pow2(x1, res->xcb) - cbx);
        pb->ch = (uint32_t)(ceil_pow2(y1, res->ycb) - cby);
        if ((uint64_t)pb->cw * pb->ch > X_BLOCKS - t->blocks) return false;
        t->blocks += pb->cw * pb->ch;
        pb->blk  = zget(&t->arena, (uint64_t)pb->cw * pb->ch * sizeof(*pb->blk));
        pb->incl = tag_new(&t->arena, pb->cw, pb->ch);
        pb->zero = tag_new(&t->arena, pb->cw, pb->ch);
        if (!pb->blk || !pb->incl || !pb->zero) return false;
        for (v = 0; v < pb->ch; v++) {
          for (u = 0; u < pb->cw; u++) {
            xcblk_t *k  = &pb->blk[v * pb->cw + u];
            int64_t  kx = (cbx + u) << res->xcb, ky = (cby + v) << res->ycb;

            k->x0 = (int32_t)(kx > x0 ? kx : x0);
            k->y0 = (int32_t)(ky > y0 ? ky : y0);
            k->x1 = (int32_t)(kx + ((int64_t)1 << res->xcb) < x1 ? kx + ((int64_t)1 << res->xcb) : x1);
            k->y1 = (int32_t)(ky + ((int64_t)1 << res->ycb) < y1 ? ky + ((int64_t)1 << res->ycb) : y1);
          }
        }
      }
    }
  }
  return true;
}

/* -- tier 2: packets (B.10) --------------------------------------------------- */

static uint32_t
num_passes(bio_t *b) {
  uint32_t n;

  if (!bio_bit(b)) return 1u;
  if (!bio_bit(b)) return 2u;
  if ((n = bio_bits(b, 2u)) != 3u) return 3u + n;
  if ((n = bio_bits(b, 5u)) != 31u) return 6u + n;
  return 37u + bio_bits(b, 7u);
}

/* one packet: its header, then its code-blocks' bytes */
static void
packet(xtile_t *t, xtc_t *tc, uint32_t r, uint32_t p, uint32_t layer) {
  xres_t        *res = &tc->res[r];
  bool           inline_hdr = !t->hdr;
  const uint8_t *hs  = inline_hdr ? t->body : t->hdr;
  size_t        *hp  = inline_hdr ? &t->body_pos : &t->hdr_pos;
  size_t         hl  = inline_hdr ? t->body_len : t->hdr_len;
  uint8_t        style = tc->cc->style;
  bio_t          b;
  uint32_t       k, i;
  bool           present;

  if (t->stop) return;
  if ((t->cod.scod & 2u) && t->body_len - t->body_pos >= 6u && t->body[t->body_pos] == 0xFFu
      && t->body[t->body_pos + 1u] == 0x91u) {
    t->body_pos += 6u; /* SOP */
  }
  memset(&b, 0, sizeof(b));
  b.s     = hs + *hp;
  b.len   = hl - *hp;
  present = bio_bit(&b);
  for (k = 0; present && k < res->nbands; k++) {
    const xband_t *band = &res->band[k];
    xpband_t      *pb   = &res->prec[p * res->nbands + k];

    if (band->x0 >= band->x1 || band->y0 >= band->y1) continue;
    for (i = 0; i < pb->cw * pb->ch; i++) {
      xcblk_t *cb = &pb->blk[i];
      uint32_t n, s, inc;

      cb->newsegs = 0;
      inc         = cb->nsegs ? bio_bit(&b) : tag_decode(&b, pb->incl, i, (int32_t)layer + 1);
      if (!inc) continue;
      if (!cb->nsegs) {
        int32_t z = 0;

        while (!tag_decode(&b, pb->zero, i, z)) {
          if (++z > 74) {
            t->stop = true;
            return;
          }
        }
        cb->numbps = band->mb + 1 - z;
        cb->lblock = 3u;
      }
      n = num_passes(&b);
      while (bio_bit(&b)) {
        if (++cb->lblock > 32u) {
          t->stop = true;
          return;
        }
      }
      if (!cb->nsegs) {
        s = 0;
        if (!seg_add(t, cb, 0u, style)) goto stop;
      } else {
        s = cb->nsegs - 1u;
        if (cb->segs[s].passes == cb->segs[s].max && !seg_add(t, cb, ++s, style)) goto stop;
      }
      cb->newseg = s;
      for (;;) {
        xseg_t  *g = &cb->segs[s];
        uint32_t room = (uint32_t)(g->max - g->passes), bits;

        g->newpasses = (uint16_t)(room < n ? room : n);
        bits         = cb->lblock + floor_log2(g->newpasses);
        if (bits > 32u) goto stop;
        g->newlen = bio_bits(&b, bits);
        n -= g->newpasses;
        cb->newsegs++;
        if (!n) break;
        if (!seg_add(t, cb, ++s, style)) goto stop;
      }
    }
  }
  bio_align(&b);
  *hp += b.pos;
  if ((t->cod.scod & 4u) && hl - *hp >= 2u && hs[*hp] == 0xFFu && hs[*hp + 1u] == 0x92u) *hp += 2u; /* EPH */
  if (!present) return;

  /* the bytes, in the header's order */
  for (k = 0; k < res->nbands; k++) {
    const xband_t *band = &res->band[k];
    xpband_t      *pb   = &res->prec[p * res->nbands + k];

    if (band->x0 >= band->x1 || band->y0 >= band->y1) continue;
    for (i = 0; i < pb->cw * pb->ch; i++) {
      xcblk_t *cb = &pb->blk[i];
      uint32_t s;

      for (s = cb->newseg; s < cb->newseg + cb->newsegs; s++) {
        xseg_t *g = &cb->segs[s];

        if (g->newlen > t->body_len - t->body_pos) goto stop;
        if (g->newlen && !piece_add(t, cb, t->body + t->body_pos, g->newlen)) goto stop;
        t->body_pos += g->newlen;
        g->len += g->newlen;
        g->passes = (uint16_t)(g->passes + g->newpasses);
        if (cb->nsegs < s + 1u) cb->nsegs = s + 1u;
      }
      cb->newsegs = 0;
    }
  }
  return;

stop:
  t->stop = true;
}

/* a packet unless that layer of the precinct has been read */
static void
packet_once(xtile_t *t, xtc_t *tc, uint32_t r, uint32_t p, uint32_t l) {
  xres_t *res = &tc->res[r];

  if (res->done[p] != l) return;
  packet(t, tc, r, p, l);
  res->done[p]++;
}

typedef struct xpos {
  uint64_t y, x;
  uint32_t c, r, p;
} xpos_t;

static int
pos_rpcl(const void *a, const void *b) {
  const xpos_t *u = a, *v = b;

  if (u->r != v->r) return u->r < v->r ? -1 : 1;
  if (u->y != v->y) return u->y < v->y ? -1 : 1;
  if (u->x != v->x) return u->x < v->x ? -1 : 1;
  return u->c < v->c ? -1 : u->c > v->c;
}

static int
pos_pcrl(const void *a, const void *b) {
  const xpos_t *u = a, *v = b;

  if (u->y != v->y) return u->y < v->y ? -1 : 1;
  if (u->x != v->x) return u->x < v->x ? -1 : 1;
  if (u->c != v->c) return u->c < v->c ? -1 : 1;
  return u->r < v->r ? -1 : u->r > v->r;
}

static int
pos_cprl(const void *a, const void *b) {
  const xpos_t *u = a, *v = b;

  if (u->c != v->c) return u->c < v->c ? -1 : 1;
  if (u->y != v->y) return u->y < v->y ? -1 : 1;
  if (u->x != v->x) return u->x < v->x ? -1 : 1;
  return u->r < v->r ? -1 : u->r > v->r;
}

/* the packets of a progression volume (B.12): layers [0, le), resolutions
   [rs, re), components [cs, ce) */
static void
progression(jpx_t *j, xtile_t *t, uint32_t order, uint32_t rs, uint32_t re, uint32_t cs, uint32_t ce, uint32_t le) {
  uint32_t l, r, c, p, maxres = 0;

  if (ce > j->ncomp) ce = j->ncomp;
  if (le > t->cod.layers) le = t->cod.layers;
  for (c = cs; c < ce; c++) maxres = t->tc[c].nres > maxres ? t->tc[c].nres : maxres;
  if (re > maxres) re = maxres;
  if (rs >= re || cs >= ce || !le) return;

  if (order == LRCP || order == RLCP) {
    uint32_t outer = order == LRCP ? le : re - rs, inner = order == LRCP ? re - rs : le, a, z;

    for (a = 0; a < outer && !t->stop; a++) {
      for (z = 0; z < inner; z++) {
        l = order == LRCP ? a : z;
        r = rs + (order == LRCP ? z : a);
        for (c = cs; c < ce; c++) {
          xtc_t *tc = &t->tc[c];

          if (r >= tc->nres) continue;
          for (p = 0; p < tc->res[r].pw * tc->res[r].ph; p++) packet_once(t, tc, r, p, l);
        }
      }
    }
    return;
  }

  /* by position: each precinct where it starts on the reference grid, sorted */
  {
    xpos_t  *list;
    uint64_t n = 0;
    uint32_t i;

    for (c = cs; c < ce; c++) {
      for (r = rs; r < re && r < t->tc[c].nres; r++) n += (uint64_t)t->tc[c].res[r].pw * t->tc[c].res[r].ph;
    }
    if (!n || !(list = arena_alloc(&t->arena, n * sizeof(*list)))) return;
    n = 0;
    for (c = cs; c < ce; c++) {
      xtc_t *tc = &t->tc[c];

      for (r = rs; r < re && r < tc->nres; r++) {
        xres_t  *res   = &tc->res[r];
        uint32_t level = tc->nres - 1u - r;
        uint64_t dxl   = (uint64_t)j->dx[c] << level, dyl = (uint64_t)j->dy[c] << level;
        bool     misx  = (res->x0 & ((1 << res->ppx) - 1)) != 0, misy = (res->y0 & ((1 << res->ppy) - 1)) != 0;

        for (p = 0; p < res->pw * res->ph; p++) {
          uint32_t ix = p % res->pw, iy = p / res->pw;
          xpos_t  *e  = &list[n++];

          e->x = ix == 0 && misx ? (uint64_t)t->x0 : (uint64_t)((floor_pow2(res->x0, res->ppx) + ix) << res->ppx) * dxl;
          e->y = iy == 0 && misy ? (uint64_t)t->y0 : (uint64_t)((floor_pow2(res->y0, res->ppy) + iy) << res->ppy) * dyl;
          e->c = c;
          e->r = r;
          e->p = p;
        }
      }
    }
    qsort(list, (size_t)n, sizeof(*list), order == RPCL ? pos_rpcl : order == PCRL ? pos_pcrl : pos_cprl);
    for (i = 0; i < n && !t->stop; i++) {
      for (l = 0; l < le; l++) packet_once(t, &t->tc[list[i].c], list[i].r, list[i].p, l);
    }
  }
}

/* -- the MQ decoder (Annex C) --------------------------------------------------- */

#if defined(_MSC_VER)
#define JPX_INLINE __forceinline
#define JPX_NOINLINE __declspec(noinline)
#else
#define JPX_INLINE inline __attribute__((always_inline))
#define JPX_NOINLINE __attribute__((noinline))
#endif

/* a context's state and its MPS as one: Qe, the MPS, where each exchange
   leads */
typedef struct mqs {
  uint32_t qe;
  uint8_t  mps, nmps, nlps, pad; /* 8 bytes: an index scales in the load */
} mqs_t;

static const mqs_t mq_states[94] = {
  { 0x5601, 0, 2, 3, 0 }, { 0x5601, 1, 3, 2, 0 }, { 0x3401, 0, 4, 12, 0 }, { 0x3401, 1, 5, 13, 0 },
  { 0x1801, 0, 6, 18, 0 }, { 0x1801, 1, 7, 19, 0 }, { 0x0AC1, 0, 8, 24, 0 }, { 0x0AC1, 1, 9, 25, 0 },
  { 0x0521, 0, 10, 58, 0 }, { 0x0521, 1, 11, 59, 0 }, { 0x0221, 0, 76, 66, 0 }, { 0x0221, 1, 77, 67, 0 },
  { 0x5601, 0, 14, 13, 0 }, { 0x5601, 1, 15, 12, 0 }, { 0x5401, 0, 16, 28, 0 }, { 0x5401, 1, 17, 29, 0 },
  { 0x4801, 0, 18, 28, 0 }, { 0x4801, 1, 19, 29, 0 }, { 0x3801, 0, 20, 28, 0 }, { 0x3801, 1, 21, 29, 0 },
  { 0x3001, 0, 22, 34, 0 }, { 0x3001, 1, 23, 35, 0 }, { 0x2401, 0, 24, 36, 0 }, { 0x2401, 1, 25, 37, 0 },
  { 0x1C01, 0, 26, 40, 0 }, { 0x1C01, 1, 27, 41, 0 }, { 0x1601, 0, 58, 42, 0 }, { 0x1601, 1, 59, 43, 0 },
  { 0x5601, 0, 30, 29, 0 }, { 0x5601, 1, 31, 28, 0 }, { 0x5401, 0, 32, 28, 0 }, { 0x5401, 1, 33, 29, 0 },
  { 0x5101, 0, 34, 30, 0 }, { 0x5101, 1, 35, 31, 0 }, { 0x4801, 0, 36, 32, 0 }, { 0x4801, 1, 37, 33, 0 },
  { 0x3801, 0, 38, 34, 0 }, { 0x3801, 1, 39, 35, 0 }, { 0x3401, 0, 40, 36, 0 }, { 0x3401, 1, 41, 37, 0 },
  { 0x3001, 0, 42, 38, 0 }, { 0x3001, 1, 43, 39, 0 }, { 0x2801, 0, 44, 38, 0 }, { 0x2801, 1, 45, 39, 0 },
  { 0x2401, 0, 46, 40, 0 }, { 0x2401, 1, 47, 41, 0 }, { 0x2201, 0, 48, 42, 0 }, { 0x2201, 1, 49, 43, 0 },
  { 0x1C01, 0, 50, 44, 0 }, { 0x1C01, 1, 51, 45, 0 }, { 0x1801, 0, 52, 46, 0 }, { 0x1801, 1, 53, 47, 0 },
  { 0x1601, 0, 54, 48, 0 }, { 0x1601, 1, 55, 49, 0 }, { 0x1401, 0, 56, 50, 0 }, { 0x1401, 1, 57, 51, 0 },
  { 0x1201, 0, 58, 52, 0 }, { 0x1201, 1, 59, 53, 0 }, { 0x1101, 0, 60, 54, 0 }, { 0x1101, 1, 61, 55, 0 },
  { 0x0AC1, 0, 62, 56, 0 }, { 0x0AC1, 1, 63, 57, 0 }, { 0x09C1, 0, 64, 58, 0 }, { 0x09C1, 1, 65, 59, 0 },
  { 0x08A1, 0, 66, 60, 0 }, { 0x08A1, 1, 67, 61, 0 }, { 0x0521, 0, 68, 62, 0 }, { 0x0521, 1, 69, 63, 0 },
  { 0x0441, 0, 70, 64, 0 }, { 0x0441, 1, 71, 65, 0 }, { 0x02A1, 0, 72, 66, 0 }, { 0x02A1, 1, 73, 67, 0 },
  { 0x0221, 0, 74, 68, 0 }, { 0x0221, 1, 75, 69, 0 }, { 0x0141, 0, 76, 70, 0 }, { 0x0141, 1, 77, 71, 0 },
  { 0x0111, 0, 78, 72, 0 }, { 0x0111, 1, 79, 73, 0 }, { 0x0085, 0, 80, 74, 0 }, { 0x0085, 1, 81, 75, 0 },
  { 0x0049, 0, 82, 76, 0 }, { 0x0049, 1, 83, 77, 0 }, { 0x0025, 0, 84, 78, 0 }, { 0x0025, 1, 85, 79, 0 },
  { 0x0015, 0, 86, 80, 0 }, { 0x0015, 1, 87, 81, 0 }, { 0x0009, 0, 88, 82, 0 }, { 0x0009, 1, 89, 83, 0 },
  { 0x0005, 0, 90, 84, 0 }, { 0x0005, 1, 91, 85, 0 }, { 0x0001, 0, 90, 86, 0 }, { 0x0001, 1, 91, 87, 0 },
  { 0x5601, 0, 92, 92, 0 }, { 0x5601, 1, 93, 93, 0 }
};

/* contexts: 0–8 significance, 9–13 sign, 14–16 refinement, 17 run, 18 uniform */
enum { CX_RUN = 17, CX_UNI = 18, CX_COUNT = 19 };

/* a code-block's decoder: the coder's registers and contexts (a
   segment's bytes end in two 0xFF, a marker: ones from there, as OpenJPEG
   has it), the samples' states and values */
typedef struct t1 {
  uint32_t       a, ct;
  uint64_t       c;     /* Annex C's C at bits 32–63 (C.3's Chigh at 48–63),
                           ct bits of the stream under it */
  const uint8_t *bp;
  uint8_t        cx[CX_COUNT];
  uint32_t      *flags; /* a word a column of a stripe, a border round */
  int32_t       *data;
  uint32_t       w, h, fs;
  const uint8_t *zc, *sc;
  bool           causal;
} t1_t;

static void
mq_reset(t1_t *t) {
  memset(t->cx, 0, sizeof(t->cx));
  t->cx[CX_UNI] = 46u << 1;
  t->cx[CX_RUN] = 3u << 1;
  t->cx[0]      = 4u << 1;
}

/* the coder on locals a, c, ct, bp and cxs. C is Annex C's moved up to
   bits 32–63 (Chigh at 48–63), the stream's next ct bits under it, up to
   48: bytes go in while fewer than 33 are, four at a time where none of
   them follows a 0xFF. A byte after 0xFF goes in a bit higher (its top bit
   a carry, C.3.4); one with that bit set only once ct is 0, as the
   bit-at-a-time decoder has it (in a conforming stream there is none: a
   damaged one decodes the same). A marker's 0xFF 0xFF: ones, the pointer
   kept. Each segment ends in one (t1_block's), so the bytes read stop
   there */
#define MQ_FILL()                                                                                                      \
  do {                                                                                                                 \
    while (ct < 33u) {                                                                                                 \
      if (ct <= 16u && bp[0] != 0xFFu && bp[1] != 0xFFu && bp[2] != 0xFFu && bp[3] != 0xFFu) {                         \
        c  += (uint64_t)be32(bp + 1) << (16u - ct);                                                                    \
        ct += 32u;                                                                                                     \
        bp += 4;                                                                                                       \
      } else if (*bp == 0xFFu) {                                                                                       \
        if (bp[1] > 0x8Fu) {                                                                                           \
          c  += (uint64_t)0xFFu << (40u - ct);                                                                         \
          ct += 8u;                                                                                                    \
        } else if (bp[1] < 0x80u || !ct) {                                                                             \
          bp++;                                                                                                        \
          c  += (uint64_t)*bp << (41u - ct);                                                                           \
          ct += 7u;                                                                                                    \
        } else {                                                                                                       \
          break;                                                                                                       \
        }                                                                                                              \
      } else {                                                                                                         \
        bp++;                                                                                                          \
        c  += (uint64_t)*bp << (40u - ct);                                                                             \
        ct += 8u;                                                                                                      \
      }                                                                                                                \
    }                                                                                                                  \
  } while (0)

/* the leading zeros of a nonzero 32-bit word */
static JPX_INLINE uint32_t
mq_clz(uint32_t v) {
#if defined(_MSC_VER) && !defined(__clang__)
  unsigned long i;

  _BitScanReverse(&i, v);
  return 31u - (uint32_t)i;
#else
  return (uint32_t)__builtin_clz(v);
#endif
}

/* A back to 0x8000 and up: the bits it needs, refilled as they run out */
#define MQ_RENORM()                                                                                                    \
  do {                                                                                                                 \
    uint32_t n_ = mq_clz(a) - 16u;                                                                                     \
                                                                                                                       \
    while (n_) {                                                                                                       \
      uint32_t s_;                                                                                                     \
                                                                                                                       \
      if (ct < n_) MQ_FILL();                                                                                          \
      s_ = n_ < ct ? n_ : ct;                                                                                          \
      a <<= s_;                                                                                                        \
      c <<= s_;                                                                                                        \
      ct -= s_;                                                                                                        \
      n_ -= s_;                                                                                                        \
    }                                                                                                                  \
  } while (0)

#define MQ_DECODE(d, ctx)                                                                                              \
  do {                                                                                                                 \
    uint8_t     *cx_ = &cxs[ctx];                                                                                      \
    const mqs_t *s_  = &mq_states[*cx_];                                                                               \
    uint32_t     q_  = s_->qe;                                                                                         \
                                                                                                                       \
    a -= q_;                                                                                                           \
    if ((c >> 48) < q_) {                                                                                              \
      if (a < q_) {                                                                                                    \
        (d)  = s_->mps;                                                                                                \
        *cx_ = s_->nmps;                                                                                               \
      } else {                                                                                                         \
        (d)  = s_->mps ^ 1u;                                                                                           \
        *cx_ = s_->nlps;                                                                                               \
      }                                                                                                                \
      a = q_;                                                                                                          \
      MQ_RENORM();                                                                                                     \
    } else {                                                                                                           \
      c -= (uint64_t)q_ << 48;                                                                                                   \
      if (!(a & 0x8000u)) {                                                                                            \
        if (a < q_) {                                                                                                  \
          (d)  = s_->mps ^ 1u;                                                                                         \
          *cx_ = s_->nlps;                                                                                             \
        } else {                                                                                                       \
          (d)  = s_->mps;                                                                                              \
          *cx_ = s_->nmps;                                                                                             \
        }                                                                                                              \
        MQ_RENORM();                                                                                                   \
      } else {                                                                                                         \
        (d) = s_->mps;                                                                                                 \
      }                                                                                                                \
    }                                                                                                                  \
  } while (0)

/* the same decision without a branch on it, for the passes whose
   decisions are near even (significance propagation, refinement: a branch
   there mispredicted often): an LPS where the exchange's test and A < Qe
   disagree (C.3.2: the conditional exchange, MPS and LPS paths in one), A
   and C by selects; the context's next state stored whenever A
   renormalises (always past an LPS or an exchange). The shift at once
   where C holds the bits, else MQ_RENORM's bytes in */
#define MQ_DECODE_EVEN(d, ctx)                                                                                         \
  do {                                                                                                                 \
    uint8_t     *cx_ = &cxs[ctx];                                                                                      \
    const mqs_t *s_  = &mq_states[*cx_];                                                                               \
    uint32_t     q_  = s_->qe, lt_, lps_, n_;                                                                          \
                                                                                                                       \
    a   -= q_;                                                                                                         \
    lt_  = (c >> 48) < q_;                                                                                             \
    lps_ = lt_ ^ (uint32_t)(a < q_);                                                                                   \
    (d)  = s_->mps ^ lps_;                                                                                             \
    c    = lt_ ? c : c - ((uint64_t)q_ << 48);                                                                                   \
    a    = lt_ ? q_ : a;                                                                                               \
    n_   = mq_clz(a) - 16u;                                                                                            \
    *cx_ = n_ ? (lps_ ? s_->nlps : s_->nmps) : *cx_;                                                                   \
    if (n_ <= ct) {                                                                                                    \
      a <<= n_;                                                                                                        \
      c <<= n_;                                                                                                        \
      ct -= n_;                                                                                                        \
    } else {                                                                                                           \
      MQ_RENORM();                                                                                                     \
    }                                                                                                                  \
  } while (0)

/* a bypassed pass's bits */
#define RAW_DECODE(d)                                                                                                  \
  do {                                                                                                                 \
    if (!ct) {                                                                                                         \
      if (c == 0xFFu) {                                                                                                \
        if (*bp > 0x8Fu) {                                                                                             \
          ct = 8u;                                                                                                     \
        } else {                                                                                                       \
          c  = *bp++;                                                                                                  \
          ct = 7u;                                                                                                     \
        }                                                                                                              \
      } else {                                                                                                         \
        c  = *bp++;                                                                                                    \
        ct = 8u;                                                                                                       \
      }                                                                                                                \
    }                                                                                                                  \
    ct--;                                                                                                              \
    (d) = (c >> ct) & 1u;                                                                                              \
  } while (0)

#define T1_LOAD()                                                                                                      \
  uint32_t       a = t->a, ct = t->ct;                                                                                 \
  uint64_t       c   = t->c;                                                                                           \
  const uint8_t *bp  = t->bp;                                                                                          \
  uint8_t       *cxs = t->cx

#define T1_STORE()                                                                                                     \
  do {                                                                                                                 \
    t->a  = a;                                                                                                         \
    t->c  = c;                                                                                                         \
    t->ct = ct;                                                                                                        \
    t->bp = bp;                                                                                                        \
  } while (0)

static void
mq_start(t1_t *t, const uint8_t *s) {
  T1_LOAD();

  (void)cxs;
  bp = s;
  c  = (uint64_t)*bp << 48;
  ct = 0;
  MQ_FILL();
  c <<= 7;
  ct -= 7u;
  a = 0x8000u;
  T1_STORE();
}

static void
raw_start(t1_t *t, const uint8_t *s) {
  t->bp = s;
  t->c  = 0;
  t->ct = 0;
}

/* -- tier 1: a code-block's bit-planes (Annex D) ----------------------------- */

/* a stripe's column as one word: the significance of the 6 × 3 samples
   round its four (row r of -1–4, column d of -1–1 at bit 3(r + 1) + d + 1),
   and for each of the four, shifted by 3 a row, its sign, whether it's been
   refined and whether this bit-plane visited it; the signs of the rows
   above and below at 18 and 31. A row's window, `word >> 3 × row`, has its
   eight neighbours at the bits round 4 */
enum {
  T_SIG   = 1u << 4,
  T_NEIGH = 0x1EFu,
  T_CHI   = 1u << 19,
  T_MU    = 1u << 20,
  T_PI    = 1u << 21,
  T_UPCHI = 1u << 18,
  T_DNCHI = 1u << 31,
  T_SIGS  = T_SIG | T_SIG << 3 | T_SIG << 6 | T_SIG << 9,
  T_PIS   = T_PI | T_PI << 3 | T_PI << 6 | T_PI << 9
};

static void ht_tables(jpx_t *j);

static void
t1_tables(jpx_t *j) {
  uint32_t i, o;

  ht_tables(j);
  for (o = 0; o < 4u; o++) {
    for (i = 0; i < 512u; i++) {
      uint32_t v = (i >> 1 & 1u) + (i >> 7 & 1u), h = (i >> 3 & 1u) + (i >> 5 & 1u);
      uint32_t d = (i & 1u) + (i >> 2 & 1u) + (i >> 6 & 1u) + (i >> 8 & 1u), n;

      if (o == 1u) { /* HL: vertical and horizontal swapped */
        uint32_t x = h;

        h = v;
        v = x;
      }
      if (o == 3u) {
        uint32_t hv = h + v;

        n = d >= 3u ? 8u : d == 2u ? (hv ? 7u : 6u) : d == 1u ? (hv >= 2u ? 5u : hv ? 4u : 3u) : (hv >= 2u ? 2u : hv);
      } else if (h == 2u) {
        n = 8u;
      } else if (h == 1u) {
        n = v ? 7u : d ? 6u : 5u;
      } else {
        n = v == 2u ? 4u : v == 1u ? 3u : d >= 2u ? 2u : d;
      }
      j->zc[o][i] = (uint8_t)n;
    }
  }
  /* a sign's: N, S, W, E significant at 0–3, negative at 4–7 */
  for (i = 0; i < 256u; i++) {
    int      n = i & 1u ? (i & 16u ? -1 : 1) : 0, s = i & 2u ? (i & 32u ? -1 : 1) : 0;
    int      w = i & 4u ? (i & 64u ? -1 : 1) : 0, e = i & 8u ? (i & 128u ? -1 : 1) : 0;
    int      h = w + e, v = n + s;
    uint32_t cx, x;

    h        = h > 1 ? 1 : h < -1 ? -1 : h;
    v        = v > 1 ? 1 : v < -1 ? -1 : v;
    cx       = h == 0 ? (v == 0 ? 9u : 10u) : (v == 0 ? 12u : (h == v ? 13u : 11u));
    x        = h == 0 ? (v < 0) : (h < 0);
    j->sc[i] = (uint8_t)(cx << 1 | x);
  }
}

/* the passes' locals: the block (w, fs, zc, sc, causal), the column's word
   (f, at fp), its samples (d) */
#define T1_BLOCK()                                                                                                     \
  const uint32_t fs = w + 2u;                                                                                          \
  const uint8_t *zc = t->zc, *sc = t->sc;                                                                              \
  uint32_t      *flags = t->flags;                                                                                     \
  int32_t       *data  = t->data

/* row i's sign context and XOR bit, from its column and the two beside */
#define T1_SC(i)                                                                                                       \
  sc[(((f >> (3u * (i))) >> 1) & 1u) | (((f >> (3u * (i))) >> 6) & 2u) | (((f >> (3u * (i))) >> 1) & 4u)              \
     | (((f >> (3u * (i))) >> 2) & 8u) | ((i) ? (f >> (16u + 3u * (i))) & 1u : (f >> 18) & 1u) << 4                    \
     | (((f >> (3u * (i))) >> 17) & 0x20u) | ((fp[-1] >> (13u + 3u * (i))) & 0x40u)                                    \
     | ((fp[1] >> (12u + 3u * (i))) & 0x80u)]

/* row i become significant: its own word (f, stored with the column), the
   columns beside, the stripes above (vertically causal: not) and below */
#define T1_MARK(i, neg)                                                                                                \
  do {                                                                                                                 \
    f |= (T_SIG | ((neg) ? T_CHI : 0u)) << (3u * (i));                                                                 \
    fp[-1] |= (1u << 5) << (3u * (i));                                                                                 \
    fp[1] |= (1u << 3) << (3u * (i));                                                                                  \
    if ((i) == 0u && !causal) {                                                                                        \
      uint32_t *n_ = fp - fs;                                                                                          \
                                                                                                                       \
      n_[0] |= 1u << 16 | ((neg) ? T_DNCHI : 0u);                                                                      \
      n_[-1] |= 1u << 17;                                                                                              \
      n_[1] |= 1u << 15;                                                                                               \
    }                                                                                                                  \
    if ((i) == 3u) {                                                                                                   \
      uint32_t *s_ = fp + fs;                                                                                          \
                                                                                                                       \
      s_[0] |= 1u << 1 | ((neg) ? T_UPCHI : 0u);                                                                       \
      s_[-1] |= 1u << 2;                                                                                               \
      s_[1] |= 1u << 0;                                                                                                \
    }                                                                                                                  \
  } while (0)

/* a sample's sign and value, made significant at row i */
#define T1_SIGNIFY_BY(i, DEC)                                                                                          \
  do {                                                                                                                 \
    uint32_t neg_, s_ = T1_SC(i);                                                                                      \
                                                                                                                       \
    DEC(neg_, s_ >> 1);                                                                                          \
    neg_ ^= s_ & 1u;                                                                                                   \
    d[(size_t)(i) * w] = neg_ ? -oph : oph;                                                                            \
    T1_MARK(i, neg_);                                                                                                  \
  } while (0)
#define T1_SIGNIFY(i)      T1_SIGNIFY_BY(i, MQ_DECODE)
#define T1_SIGNIFY_EVEN(i) T1_SIGNIFY_BY(i, MQ_DECODE_EVEN)

#define SIG_ROW(i, raw)                                                                                                \
  do {                                                                                                                 \
    uint32_t fi_ = f >> (3u * (i)), v_;                                                                                \
                                                                                                                       \
    if (!(fi_ & (T_SIG | T_PI)) && (fi_ & T_NEIGH)) {                                                                  \
      if (raw) {                                                                                                       \
        RAW_DECODE(v_);                                                                                                \
        if (v_) {                                                                                                      \
          uint32_t neg_;                                                                                               \
                                                                                                                       \
          RAW_DECODE(neg_);                                                                                            \
          d[(size_t)(i) * w] = neg_ ? -oph : oph;                                                                      \
          T1_MARK(i, neg_);                                                                                            \
        }                                                                                                              \
      } else {                                                                                                         \
        MQ_DECODE_EVEN(v_, zc[fi_ & 0x1FFu]);                                                                          \
        if (v_) T1_SIGNIFY_EVEN(i);                                                                                    \
      }                                                                                                                \
      f |= T_PI << (3u * (i));                                                                                         \
    }                                                                                                                  \
  } while (0)

#define REF_ROW(i, raw)                                                                                                \
  do {                                                                                                                 \
    uint32_t fi_ = f >> (3u * (i)), v_;                                                                                \
    int32_t *dv_ = d + (size_t)(i) * w;                                                                                \
                                                                                                                       \
    if ((fi_ & (T_SIG | T_PI)) == T_SIG) {                                                                             \
      if (raw) RAW_DECODE(v_);                                                                                         \
      else MQ_DECODE_EVEN(v_, fi_ & T_MU ? 16u : fi_ & T_NEIGH ? 15u : 14u);                                                \
      *dv_ += (v_ ^ (uint32_t)(*dv_ < 0)) ? half : -half;                                                              \
      f |= T_MU << (3u * (i));                                                                                         \
    }                                                                                                                  \
  } while (0)

#define CLN_ROW(i)                                                                                                     \
  do {                                                                                                                 \
    uint32_t fi_ = f >> (3u * (i)), v_;                                                                                \
                                                                                                                       \
    if (!(fi_ & (T_SIG | T_PI))) {                                                                                     \
      MQ_DECODE(v_, zc[fi_ & 0x1FFu]);                                                                                 \
      if (v_) T1_SIGNIFY(i);                                                                                           \
    }                                                                                                                  \
  } while (0)

/* `bpl` OpenJPEG's bit-plane: one above, so the half below is kept */
static JPX_INLINE void
t1_sigpass_(t1_t *t, uint32_t bpl, bool raw, uint32_t w, uint32_t h, bool causal) {
  int32_t  oph = (int32_t)(3u << (bpl - 1u));
  uint32_t k, x, i;
  T1_BLOCK();
  T1_LOAD();

  for (k = 0; k < h; k += 4u) {
    uint32_t *fp = flags + (size_t)(k / 4u + 1u) * fs + 1u;
    int32_t  *d  = data + (size_t)k * w;

    if (h - k >= 4u) {
      for (x = 0; x < w; x++, fp++, d++) {
        uint32_t f = *fp;

        if (!f) continue;
        SIG_ROW(0u, raw);
        SIG_ROW(1u, raw);
        SIG_ROW(2u, raw);
        SIG_ROW(3u, raw);
        *fp = f;
      }
    } else {
      for (x = 0; x < w; x++, fp++, d++) {
        uint32_t f = *fp;

        if (!f) continue;
        for (i = 0; i < h - k; i++) SIG_ROW(i, raw);
        *fp = f;
      }
    }
  }
  T1_STORE();
  (void)data;
}

static JPX_INLINE void
t1_refpass_(t1_t *t, uint32_t bpl, bool raw, uint32_t w, uint32_t h, bool causal) {
  int32_t  half = (int32_t)(1u << (bpl - 1u));
  uint32_t k, x, i;
  T1_BLOCK();
  T1_LOAD();

  (void)zc;
  (void)sc;
  (void)causal;
  for (k = 0; k < h; k += 4u) {
    uint32_t *fp = flags + (size_t)(k / 4u + 1u) * fs + 1u;
    int32_t  *d  = data + (size_t)k * w;

    for (x = 0; x < w; x++, fp++, d++) {
      uint32_t f = *fp;

      if (!(f & T_SIGS)) continue;
      if (h - k >= 4u) {
        REF_ROW(0u, raw);
        REF_ROW(1u, raw);
        REF_ROW(2u, raw);
        REF_ROW(3u, raw);
      } else {
        for (i = 0; i < h - k; i++) REF_ROW(i, raw);
      }
      *fp = f;
    }
  }
  T1_STORE();
}

static void
t1_sigpass(t1_t *t, uint32_t bpl) {
  t1_sigpass_(t, bpl, false, t->w, t->h, t->causal);
}

static void
t1_sigpass_raw(t1_t *t, uint32_t bpl) {
  t1_sigpass_(t, bpl, true, t->w, t->h, t->causal);
}

static void
t1_refpass(t1_t *t, uint32_t bpl) {
  t1_refpass_(t, bpl, false, t->w, t->h, t->causal);
}

static void
t1_refpass_raw(t1_t *t, uint32_t bpl) {
  t1_refpass_(t, bpl, true, t->w, t->h, t->causal);
}

static JPX_INLINE void
t1_cleanup_(t1_t *t, uint32_t bpl, bool segsym, uint32_t w, uint32_t h, bool causal) {
  int32_t  oph = (int32_t)(3u << (bpl - 1u));
  uint32_t k, x, v, i;
  T1_BLOCK();
  T1_LOAD();

  for (k = 0; k < h; k += 4u) {
    uint32_t *fp = flags + (size_t)(k / 4u + 1u) * fs + 1u;
    int32_t  *d  = data + (size_t)k * w;

    if (h - k < 4u) {
      for (x = 0; x < w; x++, fp++, d++) {
        uint32_t f = *fp;

        for (i = 0; i < h - k; i++) CLN_ROW(i);
        *fp = f & ~(uint32_t)T_PIS;
      }
      continue;
    }
    for (x = 0; x < w; x++, fp++, d++) {
      uint32_t f = *fp;

      if (!f) {
        /* four in a run, none near anything significant */
        MQ_DECODE(v, CX_RUN);
        if (!v) continue;
        MQ_DECODE(v, CX_UNI);
        i = v << 1;
        MQ_DECODE(v, CX_UNI);
        i |= v;
        T1_SIGNIFY(i);
        for (i++; i < 4u; i++) CLN_ROW(i);
      } else {
        CLN_ROW(0u);
        CLN_ROW(1u);
        CLN_ROW(2u);
        CLN_ROW(3u);
      }
      *fp = f & ~(uint32_t)T_PIS;
    }
  }
  if (segsym) {
    MQ_DECODE(v, CX_UNI);
    MQ_DECODE(v, CX_UNI);
    MQ_DECODE(v, CX_UNI);
    MQ_DECODE(v, CX_UNI);
  }
  T1_STORE();
}

static void
t1_cleanup(t1_t *t, uint32_t bpl, bool segsym) {
  t1_cleanup_(t, bpl, segsym, t->w, t->h, t->causal);
}

/* -- HT code-blocks (Rec. ITU-T T.814 | ISO/IEC 15444-15, clause 7) ----------------- */

/* Annex C's CxtVLC tables, an entry a word: c_q (3 bits), rho_q (4),
   u_off (1), e_k (4), e_1 (4), the codeword w (7, its first bit the
   lowest) and its length (3) */
static const uint32_t ht_vlc0[444] = {
  0x2060008, 0x3BF1188, 0x1800010, 0x3FF2290, 0x2910018, 0x3DF2298, 0x39F1398, 0x1820020, 0x31344A0,
  0x28E0028, 0x32344A8, 0x38F15A8, 0x3030030, 0x3EF00B0, 0x3AF0038, 0x3CF22B8, 0x30D02B8, 0x1840040,
  0x33D88C0, 0x31D0048, 0x32D00C8, 0x2810050, 0x33588D0, 0x3F72AD0, 0x3B70058, 0x3D711D8, 0x30901D8,
  0x29E0060, 0x397CCE0, 0x3154CE0, 0x3258CE0, 0x3E70068, 0x3A711E8, 0x3C745E8, 0x3878DE8, 0x3FB0070,
  0x3CB22F0, 0x3058AF0, 0x3BB4EF0, 0x3DB0078, 0x39B99F8, 0x3EBABF8, 0x3ABCFF8, 0x3398FF8, 0x3F36EF8,
  0x3192EF8, 0x38B5FF8, 0x3294FF8, 0x3B31FF8, 0x1000001, 0x20E0009, 0x39F1189, 0x2060011, 0x33B2291,
  0x31B0019, 0x33D0099, 0x20A0021, 0x32B44A1, 0x30B0029, 0x33344A9, 0x3FF15A9, 0x3130031, 0x32300B1,
  0x3BF0039, 0x3DF22B9, 0x30302B9, 0x2020041, 0x31D88C1, 0x32D0049, 0x30D00C9, 0x3350051, 0x31588D1,
  0x3EF2AD1, 0x3AF0059, 0x3CF11D9, 0x31101D9, 0x2810061, 0x32588E1, 0x3054CE1, 0x38F0069, 0x39711E9,
  0x33945E9, 0x3F78DE9, 0x3B70071, 0x3D722F1, 0x3198AF1, 0x3E74EF1, 0x3870079, 0x3298BF9, 0x3A788F9,
  0x3092AF9, 0x3314EF9, 0x3C71FF9, 0x1000002, 0x20E000A, 0x31B118A, 0x2060012, 0x3BF2292, 0x32B001A,
  0x333119A, 0x3FF239A, 0x20A0022, 0x30B44A2, 0x281002A, 0x3AF55AA, 0x31315AA, 0x32345AA, 0x3030032,
  0x3DF00B2, 0x39F003A, 0x3EF22BA, 0x31113BA, 0x3B747BA, 0x2020042, 0x3CF88C2, 0x33D004A, 0x31D00CA,
  0x32D0052, 0x30D00D2, 0x38F005A, 0x3F722DA, 0x33502DA, 0x3150062, 0x32544E2, 0x3D78CE2, 0x397006A,
  0x30588EA, 0x3394CEA, 0x3E71DEA, 0x3A70072, 0x3FB22F2, 0x31902F2, 0x3C7007A, 0x3291FFA, 0x30911FA,
  0x38723FA, 0x33147FA, 0x3BB8FFA, 0x1800003, 0x204000B, 0x33D118B, 0x28C0013, 0x3CF2293, 0x31D001B,
  0x305119B, 0x3FF239B, 0x2960023, 0x32D44A3, 0x286002B, 0x29A55AB, 0x30D15AB, 0x33545AB, 0x3BF0033,
  0x3DF44B3, 0x39F26B3, 0x3EF003B, 0x3AF66BB, 0x31546BB, 0x3F737BB, 0x32517BB, 0x38F27BB, 0x28A0043,
  0x38788C3, 0x339004B, 0x3B711CB, 0x3D789CB, 0x3190053, 0x32988D3, 0x3972AD3, 0x3E7005B, 0x3A71BDB,
  0x3C711DB, 0x30923DB, 0x3FB8BDB, 0x3310063, 0x31144E3, 0x3BB8CE3, 0x3DB006B, 0x39B99EB, 0x3AB5DEB,
  0x3211DEB, 0x3EBCDEB, 0x3014DEB, 0x3CB8DEB, 0x38B0073, 0x3F34EF3, 0x39344F3, 0x33E8CF3, 0x3B32EF3,
  0x3D3007B, 0x30EAAFB, 0x3E39BFB, 0x383CFFB, 0x2928FFB, 0x3A36EFB, 0x31E5FFB, 0x2824FFB, 0x3C33FFB,
  0x29C1FFB, 0x32E2FFB, 0x1000004, 0x20E000C, 0x3BF118C, 0x2060014, 0x31B2294, 0x32B001C, 0x33D229C,
  0x3FF139C, 0x20A0024, 0x3DF44A4, 0x30B002C, 0x33300AC, 0x3130034, 0x32300B4, 0x39F003C, 0x3EF44BC,
  0x30304BC, 0x2020044, 0x31D88C4, 0x311004C, 0x3F700CC, 0x2810054, 0x3AFAAD4, 0x32D2AD4, 0x30D8AD4,
  0x3CF005C, 0x38F2BDC, 0x33500DC, 0x3150064, 0x32588E4, 0x3B74CE4, 0x3D7006C, 0x38711EC, 0x30501EC,
  0x3970074, 0x33944F4, 0x3198CF4, 0x3E72EF4, 0x3A7007C, 0x3C799FC, 0x32919FC, 0x3FB67FC, 0x30927FC,
  0x3318BFC, 0x3BB4FFC, 0x1800005, 0x29A000D, 0x3FF118D, 0x28A0015, 0x31D2295, 0x32D001D, 0x3DF339D,
  0x339239D, 0x3BF139D, 0x2920025, 0x39F44A5, 0x30D002D, 0x33544AD, 0x3EF15AD, 0x3150035, 0x32522B5,
  0x3AF46B5, 0x3CF003D, 0x3D766BD, 0x30546BD, 0x38F37BD, 0x3F727BD, 0x3B717BD, 0x2820045, 0x31988C5,
  0x326004D, 0x39788CD, 0x3E719CD, 0x29C0055, 0x329AAD5, 0x3092AD5, 0x3318AD5, 0x3A7005D, 0x38799DD,
  0x31189DD, 0x3C73BDD, 0x3212BDD, 0x3FB1BDD, 0x3010065, 0x33E88E5, 0x3BB4CE5, 0x3DB006D, 0x3EB99ED,
  0x31E89ED, 0x39B5DED, 0x32E4DED, 0x3AB1DED, 0x3CB0075, 0x38B66F5, 0x3B3AEF5, 0x30E2EF5, 0x3F3CEF5,
  0x3368EF5, 0x3D34EF5, 0x393007D, 0x3C377FD, 0x31667FD, 0x3E357FD, 0x3A3CFFD, 0x28C4FFD, 0x3839DFD,
  0x3BDAFFD, 0x2948FFD, 0x3FD3FFD, 0x2842FFD, 0x3061FFD, 0x1800006, 0x204000E, 0x303118E, 0x28C0016,
  0x30D2296, 0x29A001E, 0x33D339E, 0x31D139E, 0x32D239E, 0x28A0026, 0x3BF44A6, 0x335002E, 0x31511AE,
  0x3FF45AE, 0x3250036, 0x3DF22B6, 0x39F46B6, 0x3EF003E, 0x3CF66BE, 0x30546BE, 0x3AF37BE, 0x33617BE,
  0x3F727BE, 0x2920046, 0x38F88C6, 0x339004E, 0x3B711CE, 0x3D789CE, 0x3190056, 0x32922D6, 0x3978AD6,
  0x3E7005E, 0x3C799DE, 0x30919DE, 0x3A7ABDE, 0x3312BDE, 0x3FB8BDE, 0x3110066, 0x387CCE6, 0x3218CE6,
  0x3BB4CE6, 0x3DB006E, 0x3B355EE, 0x30145EE, 0x39B8CEE, 0x3EB1DEE, 0x3AB0076, 0x3CB2EF6, 0x38B22F6,
  0x3F3CEF6, 0x33E8EF6, 0x3D34EF6, 0x393007E, 0x31E66FE, 0x32EAEFE, 0x30E3FFE, 0x2822FFE, 0x3E39BFE,
  0x316CFFE, 0x3068FFE, 0x3A35FFE, 0x29C1FFE, 0x3264FFE, 0x2920007, 0x305000F, 0x3FF118F, 0x3390017,
  0x3BF2297, 0x3DF001F, 0x39F339F, 0x3EF239F, 0x3AF139F, 0x3CF0027, 0x38F44A7, 0x3D7002F, 0x31911AF,
  0x3F745AF, 0x3B70037, 0x32900B7, 0x397003F, 0x3E766BF, 0x3A737BF, 0x3C727BF, 0x39B57BF, 0x30917BF,
  0x38747BF, 0x3FB0047, 0x3BB88C7, 0x3DB004F, 0x33100CF, 0x3D30057, 0x31122D7, 0x3EB8AD7, 0x3AB005F,
  0x3CB99DF, 0x38B3BDF, 0x3F31BDF, 0x3B3ABDF, 0x3212BDF, 0x3938BDF, 0x3E30067, 0x3A388E7, 0x3C34CE7,
  0x383006F, 0x3FD99EF, 0x3DD5DEF, 0x3011DEF, 0x3BDCDEF, 0x33E4DEF, 0x39D8DEF, 0x3ED0077, 0x3AD66F7,
  0x38DAEF7, 0x31E2EF7, 0x3CDCEF7, 0x30E8EF7, 0x3F54EF7, 0x395007F, 0x286FFFF, 0x3B5DFFF, 0x3D57FFF,
  0x29A5FFF, 0x3A5BFFF, 0x28A3FFF, 0x32E9FFF, 0x2001FFF, 0x3E5EFFF, 0x3366FFF, 0x282AFFF, 0x20C2FFF,
  0x316CFFF, 0x2048FFF, 0x2084FFF
};
static const uint32_t ht_vlc1[358] = {
  0x1800008, 0x3271188, 0x1860010, 0x3172290, 0x28D0018, 0x33B0098, 0x1820020, 0x30744A0, 0x2950028,
  0x32B00A8, 0x2810030, 0x3FF00B0, 0x39F0038, 0x31B00B8, 0x1840040, 0x28588C0, 0x2990048, 0x31300C8,
  0x2890050, 0x30B88D0, 0x3BF2AD0, 0x3DF0058, 0x33300D8, 0x2910060, 0x32388E0, 0x3EF4CE0, 0x38F0068,
  0x30300E8, 0x3AF0070, 0x3CF44F0, 0x33D04F0, 0x3F70078, 0x3B711F8, 0x31D01F8, 0x0800001, 0x2050009,
  0x3FF1189, 0x2090011, 0x39F2291, 0x29D0019, 0x3BF1199, 0x3DF2399, 0x28D0021, 0x3B744A1, 0x3030029,
  0x3EF00A9, 0x3AF0031, 0x3CF00B1, 0x38F0039, 0x3F700B9, 0x2010041, 0x39788C1, 0x30B0049, 0x3D700C9,
  0x3330051, 0x3E700D1, 0x3A70059, 0x3AB00D9, 0x3130061, 0x3C700E1, 0x3870069, 0x3FB00E9, 0x3BB0071,
  0x3DB00F1, 0x39B0079, 0x3EB44F9, 0x32304F9, 0x0800002, 0x209000A, 0x3FF118A, 0x2010012, 0x3232292,
  0x33D001A, 0x3BF229A, 0x39F139A, 0x2950022, 0x3DF44A2, 0x303002A, 0x3EF00AA, 0x3AF0032, 0x3CF00B2,
  0x38F003A, 0x39700BA, 0x2850042, 0x3F788C2, 0x3B7004A, 0x3D700CA, 0x31D0052, 0x3FBAAD2, 0x32D2AD2,
  0x3E78AD2, 0x3A7005A, 0x3C72BDA, 0x38700DA, 0x30D0062, 0x3BB00E2, 0x3DB006A, 0x39B00EA, 0x3EB0072,
  0x3AB44F2, 0x3CB04F2, 0x38B007A, 0x3F344FA, 0x3B315FA, 0x3D327FA, 0x3938FFA, 0x1000003, 0x20A000B,
  0x30B118B, 0x2020013, 0x3232293, 0x28E001B, 0x3FF339B, 0x333239B, 0x313139B, 0x2960023, 0x3BF44A3,
  0x303002B, 0x33D11AB, 0x39F45AB, 0x31D0033, 0x3DF00B3, 0x32D003B, 0x3AF44BB, 0x31E15BB, 0x3EF27BB,
  0x2860043, 0x3CF88C3, 0x30D004B, 0x33500CB, 0x3150053, 0x32522D3, 0x38F8AD3, 0x305005B, 0x33988DB,
  0x3973BDB, 0x3192BDB, 0x3F71BDB, 0x3290063, 0x30900E3, 0x3B7006B, 0x3D744EB, 0x33104EB, 0x3E70073,
  0x3A744F3, 0x3C78CF3, 0x3EB2EF3, 0x311007B, 0x38766FB, 0x3FB37FB, 0x3BBAFFB, 0x3212FFB, 0x3018FFB,
  0x3DB8AFB, 0x39B5FFB, 0x33E1FFB, 0x3AB4FFB, 0x0800004, 0x28D000C, 0x3FF118C, 0x2950014, 0x3BF2294,
  0x3DF001C, 0x3EF009C, 0x2090024, 0x32344A4, 0x333002C, 0x39F00AC, 0x3130034, 0x3AF00B4, 0x3CF003C,
  0x3D700BC, 0x2010044, 0x38F88C4, 0x3F7004C, 0x3B700CC, 0x31D0054, 0x39700D4, 0x3E7005C, 0x3EB00DC,
  0x2850064, 0x3A7CCE4, 0x3C78CE4, 0x3874CE4, 0x3FB006C, 0x3BB00EC, 0x3DB0074, 0x39B22F4, 0x30302F4,
  0x3AB007C, 0x3CB11FC, 0x38B23FC, 0x33D03FC, 0x1000005, 0x29E000D, 0x33B118D, 0x28A0015, 0x3BF2295,
  0x31B001D, 0x30B009D, 0x2020025, 0x32B44A5, 0x28E002D, 0x33344AD, 0x3FF15AD, 0x3130035, 0x3EF00B5,
  0x323003D, 0x3DF22BD, 0x31502BD, 0x2960045, 0x30388C5, 0x33D004D, 0x39F00CD, 0x31D0055, 0x32D00D5,
  0x30D005D, 0x3CF11DD, 0x33501DD, 0x2860065, 0x32544E5, 0x3AF8CE5, 0x305006D, 0x3F711ED, 0x33945ED,
  0x38F8DED, 0x3190075, 0x3D722F5, 0x3018AF5, 0x3B74EF5, 0x29A007D, 0x39799FD, 0x3E75DFD, 0x3873FFD,
  0x3291FFD, 0x3A767FD, 0x309CFFD, 0x3314FFD, 0x3C7AFFD, 0x3118FFD, 0x3212FFD, 0x1800006, 0x202000E,
  0x303118E, 0x20C0016, 0x33D2296, 0x31D001E, 0x30D229E, 0x3FF139E, 0x2040026, 0x32D44A6, 0x28A002E,
  0x33544AE, 0x3AF15AE, 0x3150036, 0x3BF22B6, 0x3DF46B6, 0x325003E, 0x32922BE, 0x39F13BE, 0x3EF47BE,
  0x2960046, 0x30588C6, 0x339004E, 0x31900CE, 0x2860056, 0x38FAAD6, 0x3092AD6, 0x3CF8AD6, 0x30E005E,
  0x3F72BDE, 0x3B722DE, 0x3D78ADE, 0x3C71BDE, 0x29A0066, 0x397CCE6, 0x3E78CE6, 0x3A74CE6, 0x331006E,
  0x3874DEE, 0x3FB44EE, 0x3BB8CEE, 0x3AB1DEE, 0x3110076, 0x3DB4EF6, 0x39B44F6, 0x3EBAEF6, 0x3218EF6,
  0x3B32EF6, 0x301007E, 0x3CB33FE, 0x38B67FE, 0x3F3AFFE, 0x33E2FFE, 0x3D39BFE, 0x3E3CFFE, 0x31E8FFE,
  0x3935FFE, 0x32E4FFE, 0x3A31FFE, 0x2040007, 0x333000F, 0x313118F, 0x3230017, 0x3FF2297, 0x303001F,
  0x3BF119F, 0x3EF239F, 0x32D0027, 0x3DF44A7, 0x296002F, 0x33D11AF, 0x39F45AF, 0x31D0037, 0x3F700B7,
  0x286003F, 0x3AF47BF, 0x3CF44BF, 0x38F37BF, 0x30D17BF, 0x3D727BF, 0x3350047, 0x3B788C7, 0x315004F,
  0x3A700CF, 0x3250057, 0x32900D7, 0x29A005F, 0x3971BDF, 0x3E711DF, 0x30523DF, 0x3FB8BDF, 0x3390067,
  0x31900E7, 0x28C006F, 0x3C71DEF, 0x38711EF, 0x30945EF, 0x39B8DEF, 0x3310077, 0x3BB2EF7, 0x3DB22F7,
  0x33E8AF7, 0x38B4EF7, 0x180007F, 0x3EBFFFF, 0x3AB7FFF, 0x3CBBFFF, 0x3113FFF, 0x32167FF, 0x301AFFF,
  0x28A2FFF, 0x31E9BFF, 0x30ECFFF, 0x2928FFF, 0x32E5FFF, 0x2821FFF, 0x29C4FFF
};

/* a decoder's lookup (jpx_t's htv): by the quad-type, the context and the
   next 7 bits (the first the lowest), its codeword's length (3 bits),
   rho (4), u_off (1), e_k (4), e_1 (4) */
static void
ht_tables(jpx_t *j) {
  uint32_t t, k, v;

  for (t = 0; t < 2u; t++) {
    const uint32_t *tab = t ? ht_vlc1 : ht_vlc0;
    uint32_t        n   = t ? (uint32_t)(sizeof(ht_vlc1) / sizeof(ht_vlc1[0])) : (uint32_t)(sizeof(ht_vlc0) / sizeof(ht_vlc0[0]));

    for (k = 0; k < n; k++) {
      uint32_t e = tab[k], c = e & 7u, w = (e >> 16) & 0x7Fu, lw = e >> 23;

      /* the 7 bits whose low lw are its codeword: w, w + 2^lw, … */
      for (v = w; v < 128u; v += 1u << lw) j->htv[t][c][v] = (uint16_t)(lw | ((e >> 3) & 0x1FFFu) << 3);
    }
  }
}

/* the bit-streams of 7.1, a word of bits at a time: MagSgn and SigProp
   forward, their bits LSB first, a byte after an 0xFF giving 7 (its MSB a
   stuffing 0); VLC and MagRef backward, a byte giving 7 where its seven
   LSBs are 1s and the one before was above 0x8F; MEL forward, MSB first,
   7 after an 0xFF. Past its bytes: MagSgn 0xFFs (the first one an encoder
   may leave out; more, a stream that doesn't conform), MEL 0xFFs, the
   others zeros */
typedef struct htfw {
  const uint8_t *p, *end;
  uint64_t       buf;
  uint32_t       n, last, fill;
} htfw_t;

typedef struct htbw {
  const uint8_t *p, *start;   /* the next byte: p - 1, down to start */
  uint64_t       buf;
  uint32_t       n, last;
} htbw_t;

typedef struct htmel {
  const uint8_t *p, *end;
  uint32_t       bits, tmp;
  int32_t        k, run, one;
} htmel_t;

static const uint8_t mel_e[13] = { 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 4, 5 };

IM_INLINE void
fw_init(htfw_t *r, const uint8_t *p, size_t len, uint32_t fill) {
  r->p    = p;
  r->end  = p + len;
  r->buf  = 0;
  r->n    = 0;
  r->last = 0;
  r->fill = fill;
}

IM_INLINE uint64_t
ht_bswap64(uint64_t w) {
#if defined(_MSC_VER) && !defined(__clang__)
  return _byteswap_uint64(w);
#else
  return __builtin_bswap64(w);
#endif
}

/* eight bytes from p, the first the lowest */
IM_INLINE uint64_t
ht_le64(const uint8_t *p) {
  uint64_t w;

  memcpy(&w, p, 8u);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  w = ht_bswap64(w);
#endif
  return w;
}

#define HT_ONES  0x0101010101010101ull
#define HT_HIGHS 0x8080808080808080ull

/* the bytes that fit added together, where none of them is stuffed (no
   0xFF before one: rare); else a byte at a time */
IM_INLINE void
fw_refill(htfw_t *r) {
  if (r->last != 0xFFu && r->end - r->p >= 8) {
    uint64_t w = ht_le64(r->p);

    if (!((~w - HT_ONES) & w & HT_HIGHS)) {
      uint32_t k = (64u - r->n) >> 3, to = r->n + 8u * k;

      r->buf |= (w << r->n) & (to == 64u ? ~0ull : (1ull << to) - 1u);
      r->p += k;
      r->n  = to;
      r->last = (uint32_t)(w >> (8u * (k - 1u))) & 0xFFu;
      return;
    }
  }
  while (r->n <= 56u) {
    uint32_t b;

    if (r->p < r->end) {
      b = *r->p++;
    } else {
      b       = r->fill;
      r->fill = r->fill == 0xFFu ? 0xFFu : 0u;
    }
    r->buf |= (uint64_t)(r->last == 0xFFu ? b & 0x7Fu : b) << r->n;
    r->n += r->last == 0xFFu ? 7u : 8u;
    r->last = b;
  }
}

/* m bits (up to 32), the first the lowest */
IM_INLINE uint32_t
fw_bits(htfw_t *r, uint32_t m) {
  uint32_t v;

  if (r->n < m) fw_refill(r);
  v = (uint32_t)(r->buf & (((uint64_t)1 << m) - 1u));
  r->buf >>= m;
  r->n -= m;
  return v;
}

/* backwards the same: none of the bytes that fit with its low seven bits
   all ones (a stuffed one's; rare), all of them at once */
IM_INLINE void
bw_refill(htbw_t *r) {
  if (r->p - r->start >= 8) {
    uint64_t w = ht_bswap64(ht_le64(r->p - 8)), t = (w & ~HT_HIGHS) ^ ~HT_HIGHS;

    if (!((t - HT_ONES) & ~t & HT_HIGHS)) {
      uint32_t k = (64u - r->n) >> 3, to = r->n + 8u * k;

      r->buf |= (w << r->n) & (to == 64u ? ~0ull : (1ull << to) - 1u);
      r->p -= k;
      r->n  = to;
      r->last = (uint32_t)(w >> (8u * (k - 1u))) & 0xFFu;
      return;
    }
  }
  while (r->n <= 56u) {
    uint32_t b = r->p > r->start ? *--r->p : 0u, k = 8u;

    if (r->last > 0x8Fu && (b & 0x7Fu) == 0x7Fu) k = 7u;
    r->buf |= (uint64_t)(b & ((1u << k) - 1u)) << r->n;
    r->n += k;
    r->last = b;
  }
}

IM_INLINE uint32_t
bw_peek(htbw_t *r) {
  if (r->n < 16u) bw_refill(r);
  return (uint32_t)r->buf;
}

IM_INLINE void
bw_skip(htbw_t *r, uint32_t m) {
  r->buf >>= m;
  r->n -= m;
}

static uint32_t
mel_bit(htmel_t *s) {
  if (!s->bits) {
    s->bits = s->tmp == 0xFFu ? 7u : 8u;
    /* modDcup: the segment's last byte 0xFF, the one before its 4 LSBs 1s */
    if (s->p + 1 < s->end) s->tmp = s->p + 2 == s->end ? *s->p | 0x0Fu : *s->p;
    else s->tmp = 0xFFu;
    if (s->p < s->end) s->p++;
  }
  s->bits--;
  return (s->tmp >> s->bits) & 1u;
}

/* decodeMELSym (7.3.3) */
static uint32_t
mel_sym(htmel_t *s) {
  if (!s->run && !s->one) {
    uint32_t eval = mel_e[s->k];

    if (mel_bit(s)) {
      s->run = 1 << eval;
      s->k   = s->k + 1 < 12 ? s->k + 1 : 12;
    } else {
      s->run = 0;
      while (eval--) s->run = 2 * s->run + (int32_t)mel_bit(s);
      s->k   = s->k > 0 ? s->k - 1 : 0;
      s->one = 1;
    }
  }
  if (s->run > 0) {
    s->run--;
    return 0u;
  }
  s->one = 0;
  return 1u;
}

/* a U-VLC (7.3.6) from the VLC bits: its prefix 1, 2, 3 or 5 (1 to 3 bits) */
IM_INLINE uint32_t
u_prefix(htbw_t *v) {
  uint32_t b = bw_peek(v);

  if (b & 1u) { bw_skip(v, 1u); return 1u; }
  if (b & 2u) { bw_skip(v, 2u); return 2u; }
  bw_skip(v, 3u);
  return b & 4u ? 3u : 5u;
}

IM_INLINE uint32_t
u_suffix(htbw_t *v, uint32_t pfx) {
  uint32_t b;

  if (pfx < 3u) return 0u;
  b = bw_peek(v);
  if (pfx == 3u) {
    bw_skip(v, 1u);
    return b & 1u;
  }
  bw_skip(v, 5u);
  return b & 31u;
}

IM_INLINE uint32_t
u_ext(htbw_t *v, uint32_t sfx) {
  uint32_t b;

  if (sfx < 28u) return 0u;
  b = bw_peek(v);
  bw_skip(v, 4u);
  return b & 15u;
}

/* the bits v takes (0 for 0) */
IM_INLINE uint32_t
bitlen64(uint64_t v) {
#if defined(_MSC_VER) && !defined(__clang__)
  unsigned long i;

  return _BitScanReverse64(&i, v) ? (uint32_t)i + 1u : 0u;
#else
  return v ? 64u - (uint32_t)__builtin_clzll(v) : 0u;
#endif
}

/* a quad's VLC (decodeSigEMB, 7.3.5): context c's codeword, or none (an
   all-zero context's quad the MEL says is so) */
static JPX_INLINE uint32_t
ht_vlc_quad(htbw_t *vlc, htmel_t *mel, const uint16_t (*tab)[128], uint32_t c) {
  uint32_t e;

  if (!c && (mel->run > 0 ? (mel->run--, 1) : !mel_sym(mel))) return 0u;
  e = tab[c][vlc->buf & 0x7Fu];
  bw_skip(vlc, e & 7u);
  return e >> 3;
}

/* a pair's residuals (7.3.6), its bits known there: by tables, no branch;
   a prefix 1, 2, 3 or 5 from three bits, its suffix's bits (1 for 3, 5 for
   5), four more past 27; none for a quad with no residual */
static JPX_INLINE void
ht_uvlc_pair(htbw_t *vlc, uint32_t i0, uint32_t i1, uint32_t *u0, uint32_t *u1) {
  static const uint8_t upf[8] = { 5, 1, 2, 1, 3, 1, 2, 1 }, ulen[8] = { 3, 1, 2, 1, 3, 1, 2, 1 };
  static const uint8_t usl[6] = { 0, 0, 0, 1, 0, 5 };
  uint32_t             on0 = 0u - ((i0 >> 4) & 1u), on1 = 0u - ((i1 >> 4) & 1u), b, p0, p1, s0, s1, x0, x1, l;

  b  = (uint32_t)vlc->buf & 7u;
  p0 = upf[b] & on0;
  bw_skip(vlc, ulen[b] & on0);
  b  = (uint32_t)vlc->buf & 7u;
  p1 = upf[b] & on1;
  bw_skip(vlc, ulen[b] & on1);
  l  = usl[p0];
  s0 = (uint32_t)vlc->buf & ((1u << l) - 1u);
  bw_skip(vlc, l);
  l  = usl[p1];
  s1 = (uint32_t)vlc->buf & ((1u << l) - 1u);
  bw_skip(vlc, l);
  l  = (uint32_t)(s0 >= 28u) << 2;
  x0 = (uint32_t)vlc->buf & ((1u << l) - 1u);
  bw_skip(vlc, l);
  l  = (uint32_t)(s1 >= 28u) << 2;
  x1 = (uint32_t)vlc->buf & ((1u << l) - 1u);
  bw_skip(vlc, l);
  *u0 = p0 + s0 + 4u * x0;
  *u1 = p1 + s1 + 4u * x1;
}

/* the first row's pair whose quads both have residuals: the MEL says
   whether both are past 2 (7.3.6) */
static JPX_NOINLINE void
ht_uvlc_first(htbw_t *vlc, htmel_t *mel, uint32_t *u0, uint32_t *u1) {
  uint32_t pfx[2] = { 0, 0 }, sfx[2] = { 0, 0 }, ext[2] = { 0, 0 }, k;

  if (mel->run > 0 ? (mel->run--, 0) : mel_sym(mel)) {
    for (k = 0; k < 2u; k++) pfx[k] = u_prefix(vlc);
    for (k = 0; k < 2u; k++) sfx[k] = u_suffix(vlc, pfx[k]);
    for (k = 0; k < 2u; k++) ext[k] = u_ext(vlc, sfx[k]);
    *u0 = 2u + pfx[0] + sfx[0] + 4u * ext[0];
    *u1 = 2u + pfx[1] + sfx[1] + 4u * ext[1];
  } else {
    uint32_t ubit = 0;

    pfx[0] = u_prefix(vlc);
    if (pfx[0] > 2u) {
      ubit = bw_peek(vlc) & 1u;
      bw_skip(vlc, 1u);
      pfx[1] = ubit + 1u;
    } else {
      pfx[1] = u_prefix(vlc);
    }
    for (k = 0; k < 2u; k++) sfx[k] = u_suffix(vlc, pfx[k]);
    for (k = 0; k < 2u; k++) ext[k] = u_ext(vlc, sfx[k]);
    *u0 = pfx[0] + sfx[0] + 4u * ext[0];
    *u1 = pfx[0] > 2u ? ubit + 1u : pfx[1] + sfx[1] + 4u * ext[1];
  }
}

/* a quad's MagSgn values (7.3.7, 7.3.8) and its exponents e[0..1]: ma the
   most of the row above's exponents over it (first: the first row, U 1).
   Every sample written, zeros too, at o0[0..1], o1[0..1]: the cleanup's
   values (mode 0), or where they end, halved (1, 5/3) or by the step (2,
   9/7). False: past the band's bound */
static JPX_INLINE bool
ht_magsgn(htfw_t *ms, uint32_t qf, uint32_t uq, uint32_t ma, bool first, uint32_t bound, uint32_t p, uint8_t *e, int32_t *o0,
          int32_t *o1, uint32_t mode, float step) {
  uint32_t rho = qf & 15u, ek = (qf >> 5) & 15u, e1 = (qf >> 9) & 15u, sh = p - 1u, U, m, v;
  int32_t  q0 = 0, q1 = 0, q2 = 0, q3 = 0;

  if (!rho) {
    e[0] = e[1] = 0u;
  } else {
    U  = first ? 1u : (rho & (rho - 1u)) && ma > 2u ? ma - 1u : 1u; /* gamma: more than one significant */
    U += uq;
    if (U > bound || U + p > 31u) return false;
#if X_NEON
    /* the four samples in the lanes (the scalar way's arithmetic, below):
       each field's length, the sums before it, the window at each, its
       bits, then mu and the sign; the reader moved on by the lengths' sum,
       U a significant sample less one for each EMB bit, in the scalar unit
       (the next quad's start on it alone) */
    if (U <= 14u) {
      static const int32_t lane[4] = { 0, -1, -2, -3 };
      static const uint8_t pc4[16] = { 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4 };
      int32x4_t            at = vld1q_s32(lane);
      uint32x4_t           one = vdupq_n_u32(1u), zero = vdupq_n_u32(0u), mv, av, vv, mag, neg, q, lo, hi;
      uint32x4_t           sgm = vsubq_u32(zero, vandq_u32(vshlq_u32(vdupq_n_u32(rho), at), one));
      uint32x4_t           ekv = vandq_u32(vshlq_u32(vdupq_n_u32(ek), at), one);
      uint32x4_t           e1v = vandq_u32(vshlq_u32(vdupq_n_u32(e1), at), one);
      uint32_t             tot = U * pc4[rho] - pc4[rho & ek];
      uint64x2_t           bf;

      if (ms->n < 4u * U) fw_refill(ms);
      bf = vdupq_n_u64(ms->buf);
      ms->buf >>= tot;
      ms->n -= tot;
      mv = vandq_u32(vsubq_u32(vdupq_n_u32(U), ekv), sgm);
      av = vextq_u32(zero, mv, 3);
      av = vaddq_u32(av, vextq_u32(zero, av, 3));
      av = vaddq_u32(av, vextq_u32(zero, av, 2));
      vv = vcombine_u32(vmovn_u64(vshlq_u64(bf, vnegq_s64(vreinterpretq_s64_u64(vmovl_u32(vget_low_u32(av)))))),
                        vmovn_u64(vshlq_u64(bf, vnegq_s64(vreinterpretq_s64_u64(vmovl_u32(vget_high_u32(av)))))));
      vv  = vandq_u32(vv, vsubq_u32(vshlq_u32(one, vreinterpretq_s32_u32(mv)), one));
      vv  = vorrq_u32(vv, vshlq_u32(e1v, vreinterpretq_s32_u32(mv)));
      mag = vandq_u32(vshlq_u32(vaddq_u32(vbicq_u32(vv, one), vdupq_n_u32(3u)), vdupq_n_s32((int32_t)sh)), sgm);
      neg = vsubq_u32(zero, vandq_u32(vv, one));
      q   = vsubq_u32(veorq_u32(mag, neg), neg);
      /* E: the bits of 2 mu - 1, the quad's bottom row's two */
      {
        uint32x4_t E = vandq_u32(vsubq_u32(vdupq_n_u32(32u), vclzq_u32(vorrq_u32(vv, one))), sgm);

        e[0] = (uint8_t)vgetq_lane_u32(E, 1);
        e[1] = (uint8_t)vgetq_lane_u32(E, 3);
      }
      /* halved toward zero (C's / 2): the sign's bit added first */
      if (mode == 1u) q = vreinterpretq_u32_s32(vshrq_n_s32(vreinterpretq_s32_u32(vaddq_u32(q, vshrq_n_u32(q, 31))), 1));
      if (mode == 2u) q = vreinterpretq_u32_f32(vmulq_f32(vcvtq_f32_s32(vreinterpretq_s32_u32(q)), vdupq_n_f32(step)));
      /* lanes 0 and 2 the top row's, 1 and 3 the bottom's */
      lo = vuzp1q_u32(q, q);
      hi = vuzp2q_u32(q, q);
      vst1_u32((uint32_t *)(void *)o0, vget_low_u32(lo));
      vst1_u32((uint32_t *)(void *)o1, vget_low_u32(hi));
      return true;
    }
#endif
      /* mu = v / 2 + 1, the sign v's lowest bit; (2 mu + 1) = 2 (v >> 1) + 3.
         No branch on a sample's significance: an insignificant one's bits
         none, its magnitude masked to 0. The quad's bits (4 U at most) at
         one refill where they fit */
#define HT_SAMPLE(i, dst, GET)                                                 \
      {                                                                      \
        uint32_t sg = (rho >> (i)) & 1u, mag, neg;                           \
                                                                             \
        m   = (U - ((ek >> (i)) & 1u)) & (0u - sg);                          \
        v   = GET(ms, m) | ((e1 >> (i)) & 1u) << m;                         \
        mag = (((v >> 1) * 2u + 3u) << sh) & (0u - sg);                      \
        neg = v & 1u;                                                        \
        dst = (int32_t)((mag ^ (0u - neg)) + neg);                           \
      }
#define HT_QUAD(GET)                                                           \
      HT_SAMPLE(0, q0, GET)                                                  \
      HT_SAMPLE(1, q1, GET)                                                  \
      e[0] = (uint8_t)(bitlen64(v | 1u) & (0u - ((rho >> 1) & 1u))); /* E: the bits of 2 mu - 1 */ \
      HT_SAMPLE(2, q2, GET)                                                  \
      HT_SAMPLE(3, q3, GET)                                                  \
      e[1] = (uint8_t)(bitlen64(v | 1u) & (0u - ((rho >> 3) & 1u)));
      if (U <= 14u) {
        /* the four fields at once from the window: each at the sum of
           those before it (their arithmetic HT_SAMPLE's) */
        uint32_t m0, m1, m2, m3, a1, a2, a3, v0, v1, v2, v3, sg;
        uint64_t bf;

        if (ms->n < 4u * U) fw_refill(ms);
        bf = ms->buf;
        m0 = (U - (ek & 1u)) & (0u - (rho & 1u));
        m1 = (U - ((ek >> 1) & 1u)) & (0u - ((rho >> 1) & 1u));
        m2 = (U - ((ek >> 2) & 1u)) & (0u - ((rho >> 2) & 1u));
        m3 = (U - ((ek >> 3) & 1u)) & (0u - ((rho >> 3) & 1u));
        a1 = m0;
        a2 = a1 + m1;
        a3 = a2 + m2;
        v0 = ((uint32_t)bf & ((1u << m0) - 1u)) | (e1 & 1u) << m0;
        v1 = ((uint32_t)(bf >> a1) & ((1u << m1) - 1u)) | ((e1 >> 1) & 1u) << m1;
        v2 = ((uint32_t)(bf >> a2) & ((1u << m2) - 1u)) | ((e1 >> 2) & 1u) << m2;
        v3 = ((uint32_t)(bf >> a3) & ((1u << m3) - 1u)) | ((e1 >> 3) & 1u) << m3;
        ms->buf = bf >> (a3 + m3);
        ms->n  -= a3 + m3;
#define HT_VAL(i, vi, dst)                                                     \
        {                                                                    \
          uint32_t mag, neg;                                                 \
                                                                             \
          sg  = (rho >> (i)) & 1u;                                           \
          mag = ((((vi) >> 1) * 2u + 3u) << sh) & (0u - sg);                 \
          neg = (vi) & 1u;                                                   \
          dst = (int32_t)((mag ^ (0u - neg)) + neg);                         \
        }
        HT_VAL(0, v0, q0)
        HT_VAL(1, v1, q1)
        HT_VAL(2, v2, q2)
        HT_VAL(3, v3, q3)
#undef HT_VAL
        e[0]     = (uint8_t)(bitlen64(v1 | 1u) & (0u - ((rho >> 1) & 1u))); /* E: the bits of 2 mu - 1 */
        e[1] = (uint8_t)(bitlen64(v3 | 1u) & (0u - ((rho >> 3) & 1u)));
      } else {
        HT_QUAD(fw_bits)
      }
#undef HT_QUAD
#undef HT_SAMPLE
  }
  if (!mode) {
    o0[0] = q0;
    o1[0] = q1;
    o0[1] = q2;
    o1[1] = q3;
  } else if (mode == 1u) {
    o0[0] = q0 / 2;
    o1[0] = q1 / 2;
    o0[1] = q2 / 2;
    o1[1] = q3 / 2;
  } else {
    float *f0 = (float *)(void *)o0, *f1 = (float *)(void *)o1;

    f0[0] = (float)q0 * step;
    f1[0] = (float)q1 * step;
    f0[1] = (float)q2 * step;
    f1[1] = (float)q3 * step;
  }
  return true;
}


/* an HT code-block's padded size (whole quads): its width W, height H;
   the state ht_block keeps: a byte a sample with two zero columns each
   side and a zero row above (bit 0 the cleanup's significance, 1
   SigProp's), two rows of exponents, the samples (W apart) */
IM_INLINE size_t
ht_state(uint32_t W, uint32_t H) {
  /* the map and the exponents' rows; then the quads' VLC results (W),
     residuals (W / 2), two rows of rho with a zero each end (W + 4), one
     for qi's alignment: 3 W + 5 at most */
  return ((size_t)(W + 4u) * (H + 3u) + (size_t)W * 3u + 8u + 7u) & ~(size_t)7u;
}

/* An HT code-block's samples into `out` (W × H, the padded block's, W
   apart; OpenJPEG's form, as the T.800 passes leave theirs): the cleanup's
   magnitude mu at the plane below p, its bin's centre, (2 mu + 1) <<
   (p - 1); SigProp's newly significant at the plane under it, 3 <<
   (p - 2); MagRef's bit r there, (2 mu + r) << (p - 1) | 1 << (p - 2).
   z the passes (1 to 3), `bound` the largest exponent a quad may have (the
   zero bit-planes and one). False: a stream that doesn't conform (or whose
   magnitudes 31 bits don't hold); `out` then to be cleared */
static JPX_NOINLINE bool
ht_block(const uint16_t (*htv)[8][128], uint8_t *st, const uint8_t *cup, uint32_t lcup, const uint8_t *ref,
         uint32_t lref, uint32_t w, uint32_t h, uint32_t p, uint32_t z, uint32_t bound, bool causal, int32_t *out,
         void *dst, size_t dstride, uint32_t mode, float step) {
  htfw_t   ms;
  htbw_t   vlc;
  htmel_t  mel;
  uint32_t qw = (w + 1u) / 2u, qh = (h + 1u) / 2u, W = qw * 2u, H = qh * 2u, SW = W + 4u, scup, pcup, qx, qy, x, y, last;
  uint8_t *sig = st + SW + 2u, *eprev = st + (size_t)SW * (H + 1u) + 2u, *ecur = eprev + SW;
  uint16_t *qi = (uint16_t *)(void *)(st + (((size_t)SW * (H + 3u) + 1u) & ~(size_t)1u));
  uint8_t  *qu = (uint8_t *)(qi + qw);
  /* the quads' significance (rho) of the row above and this one, a zero
     past each end */
  uint8_t  *rprev = qu + qw + 1u, *rcur = rprev + qw + 2u;

  if (lcup < 2u) return false;
  scup = 16u * cup[lcup - 1u] + (cup[lcup - 2u] & 0x0Fu);
  if (scup < 2u || scup > lcup || scup > 4079u) return false;
  pcup = lcup - scup;
  /* the significance map only where SigProp reads it; the exponents'
     rows' guards, the rows of rho */
  if (z >= 2u) memset(st, 0, (size_t)SW * (H + 1u));
  memset(st + (size_t)SW * (H + 1u), 0, 2u * SW);
  memset(rprev - 1, 0, 2u * (qw + 2u));

  fw_init(&ms, cup, pcup, 0xFFu);
  memset(&mel, 0, sizeof(mel));
  mel.p   = cup + pcup;
  mel.end = cup + lcup;
  /* VLC: Scup's 12 bits skipped, the bytes down to Pcup (the second last
     as modDcup has it) */
  last      = cup[lcup - 2u] | 0x0Fu;
  vlc.start = cup + pcup;
  vlc.p     = cup + lcup - 2u;
  vlc.buf   = last >> 4;
  vlc.n     = ((last >> 4) & 7u) < 7u ? 4u : 3u;
  vlc.last  = last;
  if (vlc.n == 3u) vlc.buf &= 7u;

  /* a row of quads in two passes: its VLC, MEL and U-VLC symbols (the
     significance patterns, EMB patterns, residuals: qi, qu), then its
     MagSgn values */
  for (qy = 0; qy < qh; qy++) {
    const uint16_t (*tab)[128] = htv[qy ? 1u : 0u];
    size_t          os = mode ? dstride : W;
    uint8_t         above[512], emax[512]; /* a quad's context from the row above, its E bound */
    int32_t        *o0 = (mode ? (int32_t *)dst : out) + (size_t)(2u * qy) * os, *o1 = o0 + os;
    uint32_t        L = 0u; /* the quad on the left's rho */

    /* the row above's part of each quad's context (its three neighbours
       there: top left's, top's, top right's bits), and the most of the
       exponents over it (7.3.7): a loop each, no chain */
    if (qy) {
      for (x = 0; x < qw; x++) {
        above[x] = (uint8_t)(((rprev[(ptrdiff_t)x - 1] >> 3 | rprev[x] >> 1) & 1u) | ((rprev[x] >> 3 | rprev[x + 1u] >> 1) & 1u) << 2);
      }
      for (x = 0; x < qw; x++) {
        const uint8_t *e = eprev + 2 * (ptrdiff_t)x; /* e[-1] past the left end: a guard */
        uint32_t       a = e[-1] > e[0] ? e[-1] : e[0], b = e[1] > e[2] ? e[1] : e[2];

        emax[x] = (uint8_t)(a > b ? a : b);
      }
    }

    for (qx = 0; qx < qw; qx += 2u) {
      uint32_t c, i0, i1 = 0u, u0 = 0u, u1 = 0u;
      bool     two = qx + 1u < qw;

      /* the pair's VLC and U-VLC bits there (38 at most); each quad's
         context (7.3.5) from its neighbours' rho (bit 0 a quad's top
         left, 1 its bottom left, 2 top right, 3 bottom right), zero past
         the block: the left quad's, the row above's three */
      if (vlc.n < 38u) bw_refill(&vlc);
      c        = qy ? above[qx] | ((L >> 2 | L >> 3) & 1u) << 1 : ((L & 3u) != 0u) | ((L >> 1) & 6u);
      i0       = ht_vlc_quad(&vlc, &mel, tab, c);
      L        = i0 & 15u;
      rcur[qx] = (uint8_t)L;
      if (two) {
        c             = qy ? above[qx + 1u] | ((L >> 2 | L >> 3) & 1u) << 1 : ((L & 3u) != 0u) | ((L >> 1) & 6u);
        i1            = ht_vlc_quad(&vlc, &mel, tab, c);
        L             = i1 & 15u;
        rcur[qx + 1u] = (uint8_t)L;
      }
      /* the unsigned residuals, the pair's steps interleaved (7.3.4): the
         first row's pair with both by the MEL too (rare: apart, on a copy
         of the reader, so the reader stays in registers) */
      if ((i0 | i1) & 16u) {
        if (!qy && two && (i0 & i1 & 16u)) {
          htbw_t t = vlc;

          ht_uvlc_first(&t, &mel, &u0, &u1);
          vlc = t;
        } else {
          ht_uvlc_pair(&vlc, i0, i1, &u0, &u1);
        }
      }
      /* the pair's MagSgn values (7.3.7, 7.3.8) at once: their stream
         apart from the VLC's, so the two run side by side */
      if (!ht_magsgn(&ms, i0, u0, qy ? emax[qx] : 0u, !qy, bound, p, ecur + 2u * qx, o0 + 2u * qx, o1 + 2u * qx, mode, step)
          || (two && !ht_magsgn(&ms, i1, u1, qy ? emax[qx + 1u] : 0u, !qy, bound, p, ecur + 2u * qx + 2u, o0 + 2u * qx + 2u,
                                o1 + 2u * qx + 2u, mode, step))) {
        return false;
      }
    }
    /* the significance map for SigProp and MagRef */
    if (z >= 2u) {
      uint8_t *s0 = sig + (size_t)(2u * qy) * SW, *s1 = s0 + SW;

      for (qx = 0; qx < qw; qx++) {
        uint32_t r = rcur[qx];

        s0[2u * qx]      = (uint8_t)(r & 1u);
        s1[2u * qx]      = (uint8_t)((r >> 1) & 1u);
        s0[2u * qx + 1u] = (uint8_t)((r >> 2) & 1u);
        s1[2u * qx + 1u] = (uint8_t)((r >> 3) & 1u);
      }
    }

    {
      uint8_t *t = eprev;

      eprev = ecur;
      ecur  = t;
      t     = rprev;
      rprev = rcur;
      rcur  = t;
    }
  }

  /* SigProp (7.4): stripes of four rows, groups of four columns, the
     magnitudes then the signs */
  if (z >= 2u) {
    htfw_t   sp;
    uint32_t sy, gx, one = 3u << (p - 2u);

    fw_init(&sp, ref, lref, 0u);
    for (sy = 0; sy < h; sy += 4u) {
      uint32_t ey = sy + 4u < h ? sy + 4u : h, lim = causal ? ey : h;

      for (gx = 0; gx < w; gx += 4u) {
        uint32_t ex = gx + 4u < w ? gx + 4u : w;

        for (x = gx; x < ex; x++) {
          for (y = sy; y < ey; y++) {
            uint8_t *s = sig + (size_t)y * SW + x;
            uint32_t mbr;

            if (*s & 1u) continue;
            /* past the block: the padding's zeros, the guards' */
            mbr = s[-1] | s[1] | s[-(ptrdiff_t)SW] | s[-(ptrdiff_t)SW - 1] | s[1 - (ptrdiff_t)SW];
            if (y + 1u < lim) mbr |= s[SW] | s[SW - 1u] | s[SW + 1u];
            if (mbr && fw_bits(&sp, 1u)) *s |= 2u;
          }
        }
        for (x = gx; x < ex; x++) {
          for (y = sy; y < ey; y++) {
            if (sig[(size_t)y * SW + x] & 2u) out[(size_t)y * W + x] = fw_bits(&sp, 1u) ? -(int32_t)one : (int32_t)one;
          }
        }
      }
    }
  }
  /* MagRef (7.5): the cleanup's significant samples, the same scan: their
     bit at p - 1 the refinement's, the half under it */
  if (z == 3u) {
    htbw_t   mr;
    uint32_t sy, half = 1u << (p - 2u);

    mr.start = ref;
    mr.p     = ref + lref;
    mr.buf   = 0;
    mr.n     = 0;
    mr.last  = 0xFFu;
    for (sy = 0; sy < h; sy += 4u) {
      uint32_t ey = sy + 4u < h ? sy + 4u : h;

      for (x = 0; x < w; x++) {
        for (y = sy; y < ey; y++) {
          int32_t *o = out + (size_t)y * W + x;
          uint32_t r, mag;

          if (!(sig[(size_t)y * SW + x] & 1u)) continue;
          r = bw_peek(&mr) & 1u;
          bw_skip(&mr, 1u);
          mag = (uint32_t)(*o < 0 ? -*o : *o);
          mag = (mag ^ ((1u - r) << (p - 1u))) | half;
          *o  = *o < 0 ? -(int32_t)mag : (int32_t)mag;
        }
      }
    }
  }
  return true;
}

/* ht_block's state and samples at most (a padded block: W × H at most
   4096 + 1024 + 4 + 1, W + 4 by H + 1 at most 10324 bytes) */
enum { HT_SCRATCH = 10336u + 2u * 1028u + 5200u * 4u };

/* a code-block decoded into the tile-component's buffer, dequantized */
/* a code-block's samples to `out`, `stride` apart (where it has none,
   zeros if `fill`: its place not cleared) */
static bool
t1_block(const jpx_t *j, xscratch_t *sc, xcblk_t *b, const xband_t *band, const xcc_t *cc, uint32_t *out, size_t stride, bool fill) {
  t1_t     t;
  uint32_t w = (uint32_t)(b->x1 - b->x0), h = (uint32_t)(b->y1 - b->y0), x, y, s, passtype = 2u;
  int32_t  bp = (int32_t)cc->roi + b->numbps, *dat;
  size_t   total = 0, at = 0, ds = w; /* dat: the samples, ds apart */
  const uint8_t *src;

  if (!b->nsegs || b->numbps <= 0 || bp >= 31 || !w || !h) {
    for (y = 0; fill && y < h; y++) memset(out + (size_t)y * stride, 0, (size_t)w * 4u);
    return true;
  }
  if (!(cc->style & CB_HT) && !sc->flags
      && (!(sc->flags = malloc((1026u * 4u + 2048u) * sizeof(*sc->flags))) || !(sc->cdata = malloc(4096u * sizeof(*sc->cdata))))) {
    return false;
  }
  /* the bytes in one place, room for the 0xFF 0xFF after each segment (an
     HT block's one piece read where it lies: nothing written there) */
  for (s = 0; s < b->npieces; s++) total += b->pieces[s].len;
  if ((cc->style & CB_HT) && b->npieces == 1u) {
    src = b->pieces[0].p;
  } else {
    if (total + 2u > sc->bytes_cap) {
      uint8_t *grown = realloc(sc->bytes, total + 2u);

      if (!grown) return false;
      sc->bytes     = grown;
      sc->bytes_cap = total + 2u;
    }
    for (s = 0; s < b->npieces; s++) {
      memcpy(sc->bytes + at, b->pieces[s].p, b->pieces[s].len);
      at += b->pieces[s].len;
    }
    src = sc->bytes;
  }

  memset(&t, 0, sizeof(t));
  t.w      = w;
  t.h      = h;
  t.fs     = w + 2u;
  t.flags  = sc->flags;
  t.data   = sc->cdata;
  t.zc     = j->zc[band->orient];
  t.sc     = j->sc;
  t.causal = (cc->style & CB_CAUSAL) != 0;
  if (!(cc->style & CB_HT)) memset(t.flags, 0, (size_t)t.fs * ((h + 3u) / 4u + 2u) * sizeof(*t.flags));
  dat = t.data;
  if (!(cc->style & CB_HT)) memset(t.data, 0, (size_t)w * h * sizeof(*t.data));
  mq_reset(&t);

  if (cc->style & CB_HT) {
    /* T.814: its cleanup segment, then its refinement one; the passes of
       one HT set (OpenJPEG's: placeholders not read), the cleanup alone
       with no bit-plane under it. A stream that doesn't conform: zeros */
    uint32_t passes = 0, lcup = b->segs[0].len <= total ? b->segs[0].len : (uint32_t)total, lref = 0, z;

    for (s = 0; s < b->nsegs; s++) passes += b->segs[s].passes;
    if (b->nsegs > 1u) lref = b->segs[1].len <= total - lcup ? b->segs[1].len : (uint32_t)(total - lcup);
    z = passes < 3u ? passes : 3u;
    if (!lref || bp < 2) z = 1u;
    if (!sc->ht && !(sc->ht = malloc(HT_SCRATCH))) return false;
    ds  = (w + 1u) & ~1u;
    dat = (int32_t *)(void *)(sc->ht + ht_state((uint32_t)ds, (h + 1u) & ~1u));
    /* the cleanup alone, no ROI, whole quads: its samples straight where
       they end (as the copy below has them) */
    {
      uint32_t mode = z == 1u && !cc->roi && !(w & 1u) && !(h & 1u) ? (cc->reversible ? 1u : 2u) : 0u;

      if (!ht_block(j->htv, sc->ht, src, lcup, src + lcup, lref, w, h, (uint32_t)bp, z,
                    (uint32_t)(band->mb + 1 - b->numbps) + 1u + cc->roi, (cc->style & CB_CAUSAL) != 0, dat, out, stride, mode,
                    0.5f * band->step)) {
        if (mode) {
          for (y = 0; y < h; y++) memset(out + (size_t)y * stride, 0, (size_t)w * 4u);
          return true;
        }
        memset(dat, 0, ds * ((h + 1u) & ~1u) * sizeof(*dat));
      }
      if (mode) return true;
    }
  }

  at = 0;
  for (s = 0; !(cc->style & CB_HT) && s < b->nsegs; s++) {
    const xseg_t *g   = &b->segs[s];
    bool          raw = (cc->style & CB_BYPASS) && passtype < 2u && bp <= b->numbps - 4;
    size_t        len = g->len <= total - at ? g->len : total - at;
    uint8_t       keep[2];
    uint32_t      n;

    memcpy(keep, sc->bytes + at + len, 2u);
    sc->bytes[at + len]      = 0xFFu;
    sc->bytes[at + len + 1u] = 0xFFu;
    if (raw) raw_start(&t, sc->bytes + at);
    else mq_start(&t, sc->bytes + at);
    for (n = 0; n < g->passes && bp >= 1; n++) {
      if (passtype == 0u) raw ? t1_sigpass_raw(&t, (uint32_t)bp) : t1_sigpass(&t, (uint32_t)bp);
      else if (passtype == 1u) raw ? t1_refpass_raw(&t, (uint32_t)bp) : t1_refpass(&t, (uint32_t)bp);
      else t1_cleanup(&t, (uint32_t)bp, (cc->style & CB_SEGSYM) != 0);
      if ((cc->style & CB_RESET) && !raw) mq_reset(&t);
      if (++passtype == 3u) {
        passtype = 0u;
        bp--;
      }
    }
    memcpy(sc->bytes + at + len, keep, 2u);
    at += len;
  }

  /* ROI's max-shift: what's above the shift is the region, brought down */
  if (cc->roi) {
    int32_t thresh = (int32_t)1 << cc->roi;

    for (y = 0; y < h; y++) {
      for (x = 0; x < w; x++) {
        int32_t v = dat[(size_t)y * ds + x], m = v < 0 ? -v : v;

        if (m >= thresh) dat[(size_t)y * ds + x] = v < 0 ? -(m >> cc->roi) : m >> cc->roi;
      }
    }
  }

  if (cc->reversible) {
    int32_t *o = (int32_t *)(void *)out;

    for (y = 0; y < h; y++) {
      for (x = 0; x < w; x++) o[(size_t)y * stride + x] = dat[(size_t)y * ds + x] / 2;
    }
  } else {
    float *o    = (float *)(void *)out;
    float  step = 0.5f * band->step;

    for (y = 0; y < h; y++) {
      for (x = 0; x < w; x++) o[(size_t)y * stride + x] = (float)dat[(size_t)y * ds + x] * step;
    }
  }
  return true;
}

/* -- the inverse wavelets (Annex F) ----------------------------------------------- */

/* A level's lifting runs on its lows L[0, sn) and highs H[0, dn) where
   they lie, apart (`cas`: the first sample is a high one); interleaving
   comes after, as one copy. Low i's neighbours are H[i + cas - 1] and
   H[i + cas], high i's L[i - cas] and L[i - cas + 1], each mirrored at the
   ends to the other: the interleaved signal's symmetric extension, so each
   sample's arithmetic is OpenJPEG's on the interleaved one. An element is
   `v` samples wide, `step` apart (a row's samples: 1 and 1; a strip of
   rows: the strip's width and the row's) */

enum { V = 8, STRIP = 64 }; /* rows a horizontal item, columns a vertical one */

/* x += (a + b) * c, and the 5/3's two, over n samples */
static void
run97(float *x, const float *a, const float *b, size_t n, float c) {
  size_t k;

  for (k = 0; k < n; k++) x[k] = x[k] + (a[k] + b[k]) * c;
}

/* the first two with OpenJPEG's scaling of lows (KL) and highs (KH) done
   as they're read: the products a scaling pass would have stored, the
   same roundings, two passes over memory fewer */
#define KL 1.230174105f
#define KH 1.625732422f

static void
run97_first(float *x, const float *a, const float *b, size_t n, float c) {
  size_t k;

  for (k = 0; k < n; k++) x[k] = x[k] * KL + (a[k] * KH + b[k] * KH) * c;
}

static void
run97_second(float *x, const float *a, const float *b, size_t n, float c) {
  size_t k;

  for (k = 0; k < n; k++) x[k] = x[k] * KH + (a[k] + b[k]) * c;
}

static void
run53_low(int32_t *x, const int32_t *a, const int32_t *b, size_t n) {
  size_t k;

  for (k = 0; k < n; k++) x[k] = (int32_t)((uint32_t)x[k] - (uint32_t)((int32_t)((uint32_t)a[k] + (uint32_t)b[k] + 2u) >> 2));
}

static void
run53_high(int32_t *x, const int32_t *a, const int32_t *b, size_t n) {
  size_t k;

  for (k = 0; k < n; k++) x[k] = (int32_t)((uint32_t)x[k] + (uint32_t)((int32_t)((uint32_t)a[k] + (uint32_t)b[k]) >> 1));
}

/* one lifting step over `x`'s n elements from `y`'s m: element i's
   neighbours y[i + off] and y[i + off + 1] (off -1 or 0), mirrored to each
   other past y's ends. Rows (step 1, v 1): the middle as one run */
static void
step_apart(void *x, const void *y, uint32_t n, uint32_t m, int32_t off, size_t step, size_t v, int kind, float c) {
  uint32_t i, lo, hi;

  if (!n || !m) return;
  /* the elements whose neighbours are both there: [lo, hi) */
  lo = off < 0 ? 1u : 0u;
  hi = (int64_t)m - off - 1 < (int64_t)n ? (uint32_t)((int64_t)m - off - 1) : n;
  if (hi < lo) hi = lo;
  for (i = 0; i < n; i++) {
    int64_t a = (int64_t)i + off, b = a + 1;
    size_t  cnt = v;

    if (i == lo && step == 1u && v == 1u && hi > lo) {
      cnt = hi - lo; /* a row's middle, in one run */
    } else {
      if (a < 0) a = b;
      if (b >= (int64_t)m) b = a;
    }
    if (kind == 0) {
      run97((float *)x + (size_t)i * step, (const float *)y + (size_t)a * step, (const float *)y + (size_t)b * step, cnt, c);
    } else if (kind == 3) {
      run97_first((float *)x + (size_t)i * step, (const float *)y + (size_t)a * step, (const float *)y + (size_t)b * step, cnt, c);
    } else if (kind == 4) {
      run97_second((float *)x + (size_t)i * step, (const float *)y + (size_t)a * step, (const float *)y + (size_t)b * step, cnt, c);
    } else if (kind == 1) {
      run53_low((int32_t *)x + (size_t)i * step, (const int32_t *)y + (size_t)a * step, (const int32_t *)y + (size_t)b * step, cnt);
    } else {
      run53_high((int32_t *)x + (size_t)i * step, (const int32_t *)y + (size_t)a * step, (const int32_t *)y + (size_t)b * step, cnt);
    }
    if (cnt > v) i = hi - 1u;
  }
}

/* a level's 1-D inverse on lows and highs apart, `n` = sn + dn samples */
static void
lift_apart(void *low, void *high, uint32_t sn, uint32_t dn, uint32_t cas, size_t step, size_t v, bool reversible) {
  uint32_t i;
  size_t   k;

  if (reversible) {
    if (sn + dn == 1u) {
      if (cas) {
        for (k = 0; k < v; k++) ((int32_t *)high)[k] /= 2;
      }
      return;
    }
    step_apart(low, high, sn, dn, (int32_t)cas - 1, step, v, 1, 0.0f);
    step_apart(high, low, dn, sn, -(int32_t)cas, step, v, 2, 0.0f);
    return;
  }
  if (sn + dn <= 1u) return;
  /* both bands there: the scalings in the first two steps */
  if (sn && dn) {
    step_apart(low, high, sn, dn, (int32_t)cas - 1, step, v, 3, -0.443506852f);
    step_apart(high, low, dn, sn, -(int32_t)cas, step, v, 4, -0.882911075f);
    step_apart(low, high, sn, dn, (int32_t)cas - 1, step, v, 0, 0.052980118f);
    step_apart(high, low, dn, sn, -(int32_t)cas, step, v, 0, 1.586134342f);
    return;
  }
  /* OpenJPEG's: the high band scaled by its historic 2/K (its steps for 9/7
     carry no gain, which this makes up) */
  if (step == 1u && v == 1u) {
    float *x = low, *y = high;

    for (i = 0; i < sn; i++) x[i] *= KL;
    for (i = 0; i < dn; i++) y[i] *= KH;
  } else {
    for (i = 0; i < sn; i++) {
      float *x = (float *)low + (size_t)i * step;

      for (k = 0; k < v; k++) x[k] *= KL;
    }
    for (i = 0; i < dn; i++) {
      float *x = (float *)high + (size_t)i * step;

      for (k = 0; k < v; k++) x[k] *= KH;
    }
  }
  step_apart(low, high, sn, dn, (int32_t)cas - 1, step, v, 0, -0.443506852f);
  step_apart(high, low, dn, sn, -(int32_t)cas, step, v, 0, -0.882911075f);
  step_apart(low, high, sn, dn, (int32_t)cas - 1, step, v, 0, 0.052980118f);
  step_apart(high, low, dn, sn, -(int32_t)cas, step, v, 0, 1.586134342f);
}

/* the worker's scratch for n samples */
static void*
lanes(xscratch_t *s, size_t n) {
  if (n * 4u > s->lift_len) {
    void *grown = realloc(s->lift, n * 4u);

    if (!grown) return NULL;
    s->lift     = grown;
    s->lift_len = n * 4u;
  }
  return s->lift;
}

typedef struct xlevel {
  xtc_t   *tc;
  uint32_t r;
} xlevel_t;

/* a row of `w`, its `sw` lows then its highs: lifted where its halves lie,
   then interleaved through `tmp` (w samples) */
static void
idwt_row(uint8_t *row, uint8_t *tmp, uint32_t w, uint32_t sw, uint32_t cx, bool reversible) {
  const uint32_t *l = (const uint32_t *)(void *)row, *hh = l + sw;
  uint32_t       *t = (uint32_t *)(void *)tmp;
  uint32_t        done = 0, i;

  lift_apart(row, row + (size_t)sw * 4u, sw, w - sw, cx, 1u, 1u, reversible);
  /* lows to cx, cx + 2, …; highs between (32 bits either way): pairs of
     them (a high then a low where the row starts with a high) together */
#if X_NEON
  {
    uint32_t np = sw < w - sw ? sw : w - sw; /* whole pairs */

    for (; done + 4u <= np; done += 4u) {
      uint32x4x2_t v;

      v.val[cx]      = vld1q_u32(l + done);
      v.val[1u - cx] = vld1q_u32(hh + done);
      vst2q_u32(t + 2u * done, v);
    }
  }
#endif
  for (i = done; i < sw; i++) t[cx + 2u * i] = l[i];
  for (i = done; i < w - sw; i++) t[1u - cx + 2u * i] = hh[i];
  memcpy(row, tmp, (size_t)w * 4u);
}

/* level r's rows from `item` × V */
static bool
idwt_rows(jpx_t *j, xscratch_t *s, void *ctx, uint32_t item) {
  const xlevel_t *lv = ctx;
  xtc_t          *tc = lv->tc;
  const xres_t   *res = &tc->res[lv->r], *low = &tc->res[lv->r - 1u];
  size_t          stride = (size_t)(tc->x1 - tc->x0);
  uint32_t        w = (uint32_t)(res->x1 - res->x0), h = (uint32_t)(res->y1 - res->y0);
  uint32_t        sw = (uint32_t)(low->x1 - low->x0), cx = (uint32_t)res->x0 & 1u, y;
  uint8_t        *tmp = lanes(s, w);

  (void)j;
  if (!tmp) return false;
  for (y = item * V; y < h && y < item * V + V; y++) {
    idwt_row((uint8_t *)tc->data + (size_t)y * stride * 4u, tmp, w, sw, cx, tc->cc->reversible);
  }
  return true;
}

/* A strip's columns, a level's whole lifting in one pass down them: at
   step t the first step's sample t, then each later step's sample as soon
   as those it reads are final (its high neighbour's, then the low one's
   after that), each overwriting a value no step after reads. A sample's
   arithmetic is lift_apart's, its order kept; the results interleaved
   into `tmp` (n a row), lows at cas + 2i, highs at 1 - cas + 2j. Both
   bands there. L and H the lows and highs, `stride` apart */
#define MIRROR(a, b, m)                                                        \
  do {                                                                         \
    if ((a) < 0) (a) = (b);                                                    \
    if ((b) >= (int64_t)(m)) (b) = (a);                                        \
  } while (0)

static void
cols97(float *L, float *H, uint32_t sn, uint32_t dn, uint32_t cas, size_t stride, size_t n, float *tmp) {
  int64_t t, tend = (int64_t)sn > (int64_t)dn + 1 - (int64_t)cas ? (int64_t)sn : (int64_t)dn + 1 - (int64_t)cas;
  size_t  k;

  for (t = 0; t <= tend; t++) {
    int64_t i = t, jj = t + (int64_t)cas - 1, a, b;

    /* the lows' first step (KL, KH folded in) */
    if (i < (int64_t)sn) {
      float       *x = L + (size_t)i * stride;
      const float *ya, *yb;

      a = i + (int64_t)cas - 1;
      b = a + 1;
      MIRROR(a, b, dn);
      ya = H + (size_t)a * stride;
      yb = H + (size_t)b * stride;
      for (k = 0; k < n; k++) x[k] = x[k] * KL + (ya[k] * KH + yb[k] * KH) * -0.443506852f;
    }
    /* the highs' first, from the lows just made */
    if (jj >= 0 && jj < (int64_t)dn) {
      float       *x = H + (size_t)jj * stride;
      const float *ya, *yb;

      a = jj - (int64_t)cas;
      b = a + 1;
      MIRROR(a, b, sn);
      ya = L + (size_t)a * stride;
      yb = L + (size_t)b * stride;
      for (k = 0; k < n; k++) x[k] = x[k] * KH + (ya[k] + yb[k]) * -0.882911075f;
    }
    /* the lows' second: final, to tmp too */
    i = t - 1;
    if (i >= 0 && i < (int64_t)sn) {
      float       *x = L + (size_t)i * stride, *o = tmp + (size_t)(cas + 2u * (uint32_t)i) * n;
      const float *ya, *yb;

      a = i + (int64_t)cas - 1;
      b = a + 1;
      MIRROR(a, b, dn);
      ya = H + (size_t)a * stride;
      yb = H + (size_t)b * stride;
      for (k = 0; k < n; k++) o[k] = x[k] = x[k] + (ya[k] + yb[k]) * 0.052980118f;
    }
    /* the highs' second: final */
    jj = t + (int64_t)cas - 2;
    if (jj >= 0 && jj < (int64_t)dn) {
      const float *x = H + (size_t)jj * stride, *ya, *yb;
      float       *o = tmp + (size_t)(1u - cas + 2u * (uint32_t)jj) * n;

      a = jj - (int64_t)cas;
      b = a + 1;
      MIRROR(a, b, sn);
      ya = L + (size_t)a * stride;
      yb = L + (size_t)b * stride;
      for (k = 0; k < n; k++) o[k] = x[k] + (ya[k] + yb[k]) * 1.586134342f;
    }
  }
}

/* the 5/3's two steps the same way */
static void
cols53(int32_t *L, int32_t *H, uint32_t sn, uint32_t dn, uint32_t cas, size_t stride, size_t n, int32_t *tmp) {
  int64_t t, tend = (int64_t)sn - 1 > (int64_t)dn - (int64_t)cas ? (int64_t)sn - 1 : (int64_t)dn - (int64_t)cas;
  size_t  k;

  for (t = 0; t <= tend; t++) {
    int64_t i = t, jj = t + (int64_t)cas - 1, a, b;

    if (i < (int64_t)sn) {
      int32_t       *x = L + (size_t)i * stride, *o = tmp + (size_t)(cas + 2u * (uint32_t)i) * n;
      const int32_t *ya, *yb;

      a = i + (int64_t)cas - 1;
      b = a + 1;
      MIRROR(a, b, dn);
      ya = H + (size_t)a * stride;
      yb = H + (size_t)b * stride;
      for (k = 0; k < n; k++) {
        o[k] = x[k] = (int32_t)((uint32_t)x[k] - (uint32_t)((int32_t)((uint32_t)ya[k] + (uint32_t)yb[k] + 2u) >> 2));
      }
    }
    if (jj >= 0 && jj < (int64_t)dn) {
      const int32_t *x = H + (size_t)jj * stride, *ya, *yb;
      int32_t       *o = tmp + (size_t)(1u - cas + 2u * (uint32_t)jj) * n;

      a = jj - (int64_t)cas;
      b = a + 1;
      MIRROR(a, b, sn);
      ya = L + (size_t)a * stride;
      yb = L + (size_t)b * stride;
      for (k = 0; k < n; k++) o[k] = (int32_t)((uint32_t)x[k] + (uint32_t)((int32_t)((uint32_t)ya[k] + (uint32_t)yb[k]) >> 1));
    }
  }
}

/* rows in a ring: row i in slot (y0 + i) & mask */
typedef struct xring {
  uint8_t *p;
  size_t   row;  /* bytes a row */
  uint32_t mask; /* its rows - 1 */
  uint32_t y0;
} xring_t;

static inline uint8_t*
ring_row(const xring_t *r, uint32_t i) {
  return r->p + (size_t)((r->y0 + i) & r->mask) * r->row;
}

/* cols97's steps [t0, t1) on rings: the lows' rows in L, the highs' in H,
   the level's rows out to D, columns [x, x + n) */
static void
ring97(const xring_t *Lr, const xring_t *Hr, const xring_t *Dr, uint32_t sn, uint32_t dn, uint32_t cas, size_t x, size_t n,
       uint32_t t0, uint32_t t1) {
  int64_t t;
  size_t  k;

  for (t = t0; t < (int64_t)t1; t++) {
    int64_t i = t, jj = t + (int64_t)cas - 1, a, b;

    if (i < (int64_t)sn) {
      float       *xr = (float *)(void *)ring_row(Lr, (uint32_t)i) + x;
      const float *ya, *yb;

      a = i + (int64_t)cas - 1;
      b = a + 1;
      MIRROR(a, b, dn);
      ya = (const float *)(void *)ring_row(Hr, (uint32_t)a) + x;
      yb = (const float *)(void *)ring_row(Hr, (uint32_t)b) + x;
      for (k = 0; k < n; k++) xr[k] = xr[k] * KL + (ya[k] * KH + yb[k] * KH) * -0.443506852f;
    }
    if (jj >= 0 && jj < (int64_t)dn) {
      float       *xr = (float *)(void *)ring_row(Hr, (uint32_t)jj) + x;
      const float *ya, *yb;

      a = jj - (int64_t)cas;
      b = a + 1;
      MIRROR(a, b, sn);
      ya = (const float *)(void *)ring_row(Lr, (uint32_t)a) + x;
      yb = (const float *)(void *)ring_row(Lr, (uint32_t)b) + x;
      for (k = 0; k < n; k++) xr[k] = xr[k] * KH + (ya[k] + yb[k]) * -0.882911075f;
    }
    i = t - 1;
    if (i >= 0 && i < (int64_t)sn) {
      float       *xr = (float *)(void *)ring_row(Lr, (uint32_t)i) + x;
      float       *o  = (float *)(void *)ring_row(Dr, cas + 2u * (uint32_t)i) + x;
      const float *ya, *yb;

      a = i + (int64_t)cas - 1;
      b = a + 1;
      MIRROR(a, b, dn);
      ya = (const float *)(void *)ring_row(Hr, (uint32_t)a) + x;
      yb = (const float *)(void *)ring_row(Hr, (uint32_t)b) + x;
      for (k = 0; k < n; k++) o[k] = xr[k] = xr[k] + (ya[k] + yb[k]) * 0.052980118f;
    }
    jj = t + (int64_t)cas - 2;
    if (jj >= 0 && jj < (int64_t)dn) {
      const float *xr = (const float *)(void *)ring_row(Hr, (uint32_t)jj) + x, *ya, *yb;
      float       *o  = (float *)(void *)ring_row(Dr, 1u - cas + 2u * (uint32_t)jj) + x;

      a = jj - (int64_t)cas;
      b = a + 1;
      MIRROR(a, b, sn);
      ya = (const float *)(void *)ring_row(Lr, (uint32_t)a) + x;
      yb = (const float *)(void *)ring_row(Lr, (uint32_t)b) + x;
      for (k = 0; k < n; k++) o[k] = xr[k] + (ya[k] + yb[k]) * 1.586134342f;
    }
  }
}

/* cols53's the same way */
static void
ring53(const xring_t *Lr, const xring_t *Hr, const xring_t *Dr, uint32_t sn, uint32_t dn, uint32_t cas, size_t x, size_t n,
       uint32_t t0, uint32_t t1) {
  int64_t t;
  size_t  k;

  for (t = t0; t < (int64_t)t1; t++) {
    int64_t i = t, jj = t + (int64_t)cas - 1, a, b;

    if (i < (int64_t)sn) {
      int32_t       *xr = (int32_t *)(void *)ring_row(Lr, (uint32_t)i) + x;
      int32_t       *o  = (int32_t *)(void *)ring_row(Dr, cas + 2u * (uint32_t)i) + x;
      const int32_t *ya, *yb;

      a = i + (int64_t)cas - 1;
      b = a + 1;
      MIRROR(a, b, dn);
      ya = (const int32_t *)(void *)ring_row(Hr, (uint32_t)a) + x;
      yb = (const int32_t *)(void *)ring_row(Hr, (uint32_t)b) + x;
      for (k = 0; k < n; k++) {
        o[k] = xr[k] = (int32_t)((uint32_t)xr[k] - (uint32_t)((int32_t)((uint32_t)ya[k] + (uint32_t)yb[k] + 2u) >> 2));
      }
    }
    if (jj >= 0 && jj < (int64_t)dn) {
      const int32_t *xr = (const int32_t *)(void *)ring_row(Hr, (uint32_t)jj) + x, *ya, *yb;
      int32_t       *o  = (int32_t *)(void *)ring_row(Dr, 1u - cas + 2u * (uint32_t)jj) + x;

      a = jj - (int64_t)cas;
      b = a + 1;
      MIRROR(a, b, sn);
      ya = (const int32_t *)(void *)ring_row(Lr, (uint32_t)a) + x;
      yb = (const int32_t *)(void *)ring_row(Lr, (uint32_t)b) + x;
      for (k = 0; k < n; k++) o[k] = (int32_t)((uint32_t)xr[k] + (uint32_t)((int32_t)((uint32_t)ya[k] + (uint32_t)yb[k]) >> 1));
    }
  }
}

#undef MIRROR

/* level r's columns from `item` × STRIP: the strip's rows lifted where its
   halves lie, a row's STRIP samples at a time, then interleaved */
static bool
idwt_cols(jpx_t *j, xscratch_t *s, void *ctx, uint32_t item) {
  const xlevel_t *lv = ctx;
  xtc_t          *tc = lv->tc;
  const xres_t   *res = &tc->res[lv->r], *low = &tc->res[lv->r - 1u];
  size_t          stride = (size_t)(tc->x1 - tc->x0);
  uint32_t        w = (uint32_t)(res->x1 - res->x0), h = (uint32_t)(res->y1 - res->y0);
  uint32_t        sh = (uint32_t)(low->y1 - low->y0), cy = (uint32_t)res->y0 & 1u, x = item * STRIP, n, i;
  uint32_t       *col, *tmp = lanes(s, (size_t)h * STRIP);

  (void)j;
  if (!tmp) return false;
  n   = w - x < STRIP ? w - x : STRIP;
  col = (uint32_t *)tc->data + x;
  if (sh && h - sh && h >= 2u) {
    /* both bands: one pass, straight into tmp interleaved */
    if (tc->cc->reversible) cols53((int32_t *)col, (int32_t *)col + (size_t)sh * stride, sh, h - sh, cy, stride, n, (int32_t *)tmp);
    else cols97((float *)col, (float *)col + (size_t)sh * stride, sh, h - sh, cy, stride, n, (float *)tmp);
  } else {
    lift_apart(col, col + (size_t)sh * stride, sh, h - sh, cy, stride, n, tc->cc->reversible);
    for (i = 0; i < sh; i++) memcpy(tmp + (size_t)(cy + 2u * i) * n, col + (size_t)i * stride, n * 4u);
    for (i = 0; i < h - sh; i++) memcpy(tmp + (size_t)(1u - cy + 2u * i) * n, col + (size_t)(sh + i) * stride, n * 4u);
  }
  for (i = 0; i < h; i++) memcpy(col + (size_t)i * stride, tmp + (size_t)i * n, n * 4u);
  return true;
}

/* -- the pool ---------------------------------------------------------------------- */

/* the next item, without the lock: a round's start and end are under it,
   its items taken by one atomic add each */
#if defined(_MSC_VER) && !defined(__clang__)
#  define X_TAKE(p)     ((uint32_t)_InterlockedExchangeAdd((volatile long *)(p), 1))
#  define X_RESET(p)    ((void)_InterlockedExchange((volatile long *)(p), 0))
#else
#  define X_TAKE(p)     __atomic_fetch_add((p), 1u, __ATOMIC_RELAXED)
#  define X_RESET(p)    __atomic_store_n((p), 0u, __ATOMIC_RELAXED)
#endif

static void
pool_main(void *arg) {
  xworker_t *w = arg;
  jpx_t     *j = w->j;
  xpool_t   *p = &j->pool;
  uint32_t   seen = 0;

  thread_lock(&p->m);
  for (;;) {
    xjob_fn  fn;
    void    *ctx;
    uint32_t items, i;
    bool     ok = true;

    while (!p->quit && p->round == seen) thread_cond_wait(&p->go, &p->m);
    if (p->quit) break;
    seen  = p->round;
    fn    = p->fn;
    ctx   = p->ctx;
    items = p->items;
    p->busy++;
    thread_unlock(&p->m);
    while ((i = X_TAKE(&p->next)) < items) ok = fn(j, &j->scratch[w->id], ctx, i) && ok;
    thread_lock(&p->m);
    if (!ok) p->failed = true;
    if (!--p->busy) thread_cond_broadcast(&p->idle);
  }
  thread_unlock(&p->m);
}

/* threads for an image this large, as many as the machine has (at most
   X_THREADS) */
static void
pool_start(jpx_t *j) {
  uint32_t cpus = thread_ncpu();
  uint32_t n    = cpus > X_THREADS ? X_THREADS - 1u : cpus > 1u ? cpus - 1u : 0u, i;

  if (j->threads && n > j->threads - 1u) n = j->threads - 1u;
  /* the caller's runner: its workers, no thread of ours */
  if (j->runner.run) {
    if (j->runner.workers >= 2u && j->runner.workers <= X_THREADS && (uint64_t)(j->x1 - j->x0) * (j->y1 - j->y0) >= X_POOL_MIN) {
      j->workers = j->runner.workers - 1u;
    }
    return;
  }

  /* a thread for each X_POOL_MIN pixels at most, the caller's among them
     (below that a thread costs more than it saves) */
  {
    uint64_t each = (uint64_t)(j->x1 - j->x0) * (j->y1 - j->y0) / X_POOL_MIN;

    if (each < 2u) return;
    if (n > each - 1u) n = (uint32_t)(each - 1u);
  }
  if (!n) return;
  thread_mutex_init(&j->pool.m);
  thread_cond_init(&j->pool.go);
  thread_cond_init(&j->pool.idle);
  for (i = 0; i < n; i++) {
    j->worker[i].j  = j;
    j->worker[i].id = i + 1u;
    if (!(j->worker[i].thread = thread_new(pool_main, &j->worker[i]))) break;
  }
  j->workers = i;
  if (!i) {
    thread_cond_destroy(&j->pool.idle);
    thread_cond_destroy(&j->pool.go);
    thread_mutex_destroy(&j->pool.m);
  }
}

static void
pool_stop(jpx_t *j) {
  uint32_t i;

  if (!j->workers || j->runner.run) {
    j->workers = 0;
    return;
  }
  thread_lock(&j->pool.m);
  j->pool.quit = true;
  thread_cond_broadcast(&j->pool.go);
  thread_unlock(&j->pool.m);
  for (i = 0; i < j->workers; i++) {
    thread_join(j->worker[i].thread);
    thread_release(j->worker[i].thread);
  }
  thread_cond_destroy(&j->pool.idle);
  thread_cond_destroy(&j->pool.go);
  thread_mutex_destroy(&j->pool.m);
  j->workers = 0;
}

/* a job on the caller's runner: its worker's scratch */
typedef struct xrun {
  jpx_t  *j;
  xjob_fn fn;
  void   *ctx;
} xrun_t;

static bool
run_item(void *arg, uint32_t worker, uint32_t item) {
  xrun_t *r = arg;

  return r->fn(r->j, &r->j->scratch[worker], r->ctx, item);
}

/* `fn` for items [0, n): on the workers too where there are */
static bool
pool_run(jpx_t *j, xjob_fn fn, void *ctx, uint32_t n) {
  xpool_t *p  = &j->pool;
  bool     ok = true;
  uint32_t i;

  if (!j->workers || n < 2u) {
    for (i = 0; i < n; i++) ok = fn(j, &j->scratch[0], ctx, i) && ok;
    return ok;
  }
  if (j->runner.run) {
    xrun_t r;

    r.j   = j;
    r.fn  = fn;
    r.ctx = ctx;
    return j->runner.run(j->runner.ctx, run_item, &r, n);
  }
  thread_lock(&p->m);
  /* a worker late for the last round may still hold its job: done with it
     first, so the counter it reads is that round's */
  while (p->busy) thread_cond_wait(&p->idle, &p->m);
  p->fn     = fn;
  p->ctx    = ctx;
  p->items  = n;
  p->failed = false;
  X_RESET(&p->next);
  p->round++;
  thread_cond_broadcast(&p->go);
  thread_unlock(&p->m);
  while ((i = X_TAKE(&p->next)) < n) ok = fn(j, &j->scratch[0], ctx, i) && ok;
  thread_lock(&p->m);
  while (p->busy) thread_cond_wait(&p->idle, &p->m);
  ok = ok && !p->failed;
  thread_unlock(&p->m);
  return ok;
}

typedef struct xjob {
  xcblk_t       *b;
  const xband_t *band;
  const xcc_t   *cc;
  uint32_t      *out;    /* its samples' place */
  size_t         stride;
  bool           fill;
} xjob_t;

static bool
t1_job(jpx_t *j, xscratch_t *s, void *ctx, uint32_t item) {
  const xjob_t *job = (const xjob_t *)ctx + item;

  return t1_block(j, s, job->b, job->band, job->cc, job->out, job->stride, job->fill);
}

/* -- into the planes --------------------------------------------------------------- */

/* component c's plane's row y */
static inline uint16_t*
plane_row(const jpx_t *j, uint32_t c, size_t y) {
  return j->plane[c] + (y & j->pmask[c]) * j->pw[c];
}

/* component c's plane's bytes */
static size_t
plane_bytes(const jpx_t *j, uint32_t c) {
  return (size_t)j->pw[c] * j->ph[c] * sizeof(uint16_t);
}

/* a big image's plane rows of component c that rows Y0 … Y1 - 1 out of
   `h` read and no others: given back (the rows at the ends, which the
   bands round them may share, kept) */
static void
plane_release(const jpx_t *j, uint32_t c, uint32_t Y0, uint32_t Y1, uint32_t h) {
  int64_t lo, hi;

  if (!j->pmap || !j->plane[c]) return;
  lo = Y0 ? (int64_t)(j->y0 + Y0) / j->dy[c] - (int64_t)j->py0[c] + 1 : 0;
  hi = Y1 >= h ? (int64_t)j->ph[c] : (int64_t)(j->y0 + Y1 - 1u) / j->dy[c] - (int64_t)j->py0[c];
  lo = lo < 0 ? 0 : lo;
  hi = hi > (int64_t)j->ph[c] ? (int64_t)j->ph[c] : hi;
  if (hi > lo) im_release(true, j->plane[c] + (size_t)lo * j->pw[c], (size_t)(hi - lo) * j->pw[c] * sizeof(uint16_t));
}

/* compose's job by bands of rows where the image is big: each band's rows'
   pieces, then its planes' rows given back */
enum { X_BAND = 16u };

typedef struct xbands {
  xjob_fn  fn;
  void    *ctx;
  uint32_t pieces, h;
  uint32_t Y0, Y1; /* the rows run */
} xbands_t;

static bool
band_job(jpx_t *j, xscratch_t *s, void *ctx, uint32_t item) {
  const xbands_t *b  = ctx;
  uint32_t        Y0 = b->Y0 + item * X_BAND, Y1 = b->Y1 - Y0 > X_BAND ? Y0 + X_BAND : b->Y1, k, c;
  bool            ok = true;

  for (k = Y0 * b->pieces; k < Y1 * b->pieces; k++) ok = b->fn(j, s, b->ctx, k) && ok;
  for (c = 0; c < j->ncomp; c++) plane_release(j, c, Y0, Y1, b->h);
  return ok;
}

/* the pieces of rows [Y0, Y1), an item each */
static bool
span_job(jpx_t *j, xscratch_t *s, void *ctx, uint32_t item) {
  const xbands_t *b = ctx;

  return b->fn(j, s, b->ctx, b->Y0 * b->pieces + item);
}

static bool stream_run(jpx_t *j, xbands_t *b);
static bool tiles_run(jpx_t *j, xbands_t *b);

/* `fn` for items [0, pieces × h) (a row's pieces in turn): by bands where
   the image is big; where its tile is made by bands, each band of the
   image's rows as soon as the tile's rows under it are */
static bool
compose_run(jpx_t *j, xjob_fn fn, void *ctx, uint32_t pieces, uint32_t h) {
  xbands_t b;

  b.fn     = fn;
  b.ctx    = ctx;
  b.pieces = pieces;
  b.h      = h;
  b.Y0     = 0u;
  b.Y1     = h;
  if (j->stream) return stream_run(j, &b);
  if (j->rows) return tiles_run(j, &b);
  if (!j->big) return pool_run(j, fn, ctx, pieces * h);
  return pool_run(j, band_job, &b, (h + X_BAND - 1u) / X_BAND);
}

/* A big image of one tile is made by bands of rows, its samples never all
   there at once: each component's code-block rows decoded as the rows
   above them need them, into rings a few code-block rows tall; each
   level's rows made whole across, then its lifting run down the columns
   step by step, into the level above's ring (the top level's into its
   own); those placed into the planes' rings, and the image's rows composed
   from them. Each sample's arithmetic is the whole tile's */
#ifndef X_STRIPE
#  define X_STRIPE 64u /* the image's rows a band (a test build can lower it) */
#endif
enum { X_ROUND = 1u << 18 }; /* a level's samples a round at least */

typedef struct xsband { /* a band's code-block rows, decoded in turn */
  const xres_t *res;
  xring_t      *ring;
  uint32_t      b;     /* the band in res */
  uint32_t      col;   /* its first column in the ring's rows */
  uint32_t      py, v; /* the next: its precinct row, its code-block row there */
  uint32_t      have;  /* rows decoded */
} xsband_t;

typedef struct xslv { /* a level, r ≥ 1 */
  xring_t  L, H;       /* its lows' rows (LL then HL) and its highs' (LH, HH) */
  xring_t *D;          /* its rows: the level above's L, or the top's ring */
  uint32_t w, h, sw, sn, dn, cx, cy, steps;
  uint32_t lsyn, hsyn; /* rows made whole across */
  uint32_t t, done;    /* steps run, rows made */
  uint32_t t1, lto, hto; /* a band's: steps to, rows needed */
  uint32_t gran;       /* rows out a round at least (a narrow level's
                          rounds fewer: each of more rows) */
  xsband_t band[3];    /* HL into L; LH and HH into H */
} xslv_t;

typedef struct xscomp {
  bool     on;
  uint32_t top;        /* its levels */
  xsband_t ll;         /* the coarsest band: into level 1's L (none: the top's ring) */
  xring_t  out;        /* the top level's rows */
  uint32_t placed, done;
  xslv_t   lv[X_LEVELS + 1];
} xscomp_t;

typedef struct xsrow {
  xring_t *ring;
  uint32_t i, w, sw, cx;
  bool     rev;
} xsrow_t;

typedef struct xsstrip {
  xslv_t  *lv;
  uint32_t x, n;
  bool     rev;
} xsstrip_t;

typedef struct xstream {
  xtile_t   *t;       /* NULL: a tile with no data */
  xscomp_t  *c;
  const struct xbands *also; /* compose's rows run with the code-blocks */
  xjob_t    *jobs;
  xsrow_t   *rows;
  xsstrip_t *strips;
  uint32_t   njobs, jcap, nrows, rcap, nstrips, scap, maxtop;
  bool       mct, failed;
} xstream_t;

typedef struct xplace {
  xtile_t *t;
  bool     mct;
  uint32_t rows;                    /* items: rows base, base + 1, … */
  uint32_t base;
  uint32_t lo[X_COMPS], hi[X_COMPS]; /* each component's rows: [lo, hi) */
} xplace_t;

/* tile-component c's samples' row y, as decoded (its top level's) */
static const void*
tc_row(const jpx_t *j, const xtile_t *t, uint32_t c, uint32_t y) {
  const xtc_t *tc = &t->tc[c];

  if (j->stream) return ring_row(&j->stream->c[c].out, y);
  return (const uint32_t *)tc->data + (size_t)y * (size_t)(tc->x1 - tc->x0);
}

/* a component's samples to its plane's: the DC level shift, clamped to its
   depth, made unsigned. 5/3's clamped before the shift (the same values, in
   32 bits); 9/7's rounded as OpenJPEG's lrintf (to nearest, ties to even),
   the float held to just past the depth first, so the loop has no branch,
   four at a time where the CPU has SIMD (the same roundings) */
static void
place_samples(const jpx_t *j, uint32_t c, bool reversible, const void *src, uint16_t *out, uint32_t w) {
  int32_t  prec = j->prec[c], lo = j->sgnd[c] ? -(1 << (prec - 1)) : 0, hi = j->sgnd[c] ? (1 << (prec - 1)) - 1 : (1 << prec) - 1;
  int32_t  shift = j->sgnd[c] ? 0 : 1 << (prec - 1), lift = j->sgnd[c] ? 1 << (prec - 1) : 0;
  uint32_t x = 0;

  if (reversible) {
    const int32_t *s = src;
    int32_t        a = lo - shift, b = hi - shift, add = shift + lift;

    for (; x < w; x++) {
      int32_t v = s[x];

      v      = v < a ? a : v > b ? b : v;
      out[x] = (uint16_t)(v + add);
    }
  } else {
    const float *s   = src;
    const float  flo = (float)(lo - shift - 1), fhi = (float)(hi - shift + 1);

#if X_NEON
    {
      float32x4_t fl = vdupq_n_f32(flo), fh = vdupq_n_f32(fhi);
      int32x4_t   il = vdupq_n_s32(lo), ih = vdupq_n_s32(hi), sh = vdupq_n_s32(shift), lf = vdupq_n_s32(lift);

      for (; x + 8u <= w; x += 8u) {
        int32x4_t a = vcvtnq_s32_f32(vminq_f32(vmaxq_f32(vld1q_f32(s + x), fl), fh));
        int32x4_t b = vcvtnq_s32_f32(vminq_f32(vmaxq_f32(vld1q_f32(s + x + 4u), fl), fh));

        a = vaddq_s32(vminq_s32(vmaxq_s32(vaddq_s32(a, sh), il), ih), lf);
        b = vaddq_s32(vminq_s32(vmaxq_s32(vaddq_s32(b, sh), il), ih), lf);
        vst1q_u16(out + x, vcombine_u16(vmovn_u32(vreinterpretq_u32_s32(a)), vmovn_u32(vreinterpretq_u32_s32(b))));
      }
    }
#elif X_SSE2
    {
      __m128 fl = _mm_set1_ps(flo), fh = _mm_set1_ps(fhi);
      __m128i sh = _mm_set1_epi32(shift - 32768 + lift), bias = _mm_set1_epi16((short)-32768);
      __m128i il = _mm_set1_epi16((short)(lo + lift - 32768)), ih = _mm_set1_epi16((short)(hi + lift - 32768));

      /* to 16 bits signed around the middle (packs saturates there), held
         to the depth, then back */
      for (; x + 8u <= w; x += 8u) {
        __m128i a = _mm_add_epi32(_mm_cvtps_epi32(_mm_min_ps(_mm_max_ps(_mm_loadu_ps(s + x), fl), fh)), sh);
        __m128i b = _mm_add_epi32(_mm_cvtps_epi32(_mm_min_ps(_mm_max_ps(_mm_loadu_ps(s + x + 4u), fl), fh)), sh);
        __m128i v = _mm_min_epi16(_mm_max_epi16(_mm_packs_epi32(a, b), il), ih);

        _mm_storeu_si128((__m128i *)(void *)(out + x), _mm_sub_epi16(v, bias));
      }
    }
#elif X_WASM
    {
      v128_t fl = wasm_f32x4_splat(flo), fh = wasm_f32x4_splat(fhi);
      v128_t il = wasm_i32x4_splat(lo), ih = wasm_i32x4_splat(hi), sh = wasm_i32x4_splat(shift), lf = wasm_i32x4_splat(lift);

      for (; x + 8u <= w; x += 8u) {
        v128_t a = wasm_i32x4_trunc_sat_f32x4(wasm_f32x4_nearest(wasm_f32x4_min(wasm_f32x4_max(wasm_v128_load(s + x), fl), fh)));
        v128_t b = wasm_i32x4_trunc_sat_f32x4(wasm_f32x4_nearest(wasm_f32x4_min(wasm_f32x4_max(wasm_v128_load(s + x + 4u), fl), fh)));

        a = wasm_i32x4_add(wasm_i32x4_min(wasm_i32x4_max(wasm_i32x4_add(a, sh), il), ih), lf);
        b = wasm_i32x4_add(wasm_i32x4_min(wasm_i32x4_max(wasm_i32x4_add(b, sh), il), ih), lf);
        wasm_v128_store(out + x, wasm_u16x8_narrow_i32x4(a, b));
      }
    }
#endif
    for (; x < w; x++) {
      float   f = s[x];
      int32_t v;

      f      = f < flo ? flo : f > fhi ? fhi : f;
      v      = (int32_t)lrintf(f) + shift;
      v      = v < lo ? lo : v > hi ? hi : v;
      out[x] = (uint16_t)(v + lift);
    }
  }
}

/* a tile-component's resolution decoded last: its samples' region (made
   smaller, a coarser level's; the rows still the whole one's apart) */
static const xres_t*
top(const xtc_t *tc) {
  return &tc->res[tc->nuse - 1u];
}

/* row base + `item` of every component that has it: the colour transform
   first where the tile has one (its three rows at once) */
static bool
place_row(jpx_t *j, xscratch_t *s, void *ctx, uint32_t item) {
  const xplace_t *pl = ctx;
  xtile_t        *t  = pl->t;
  uint32_t        y  = pl->base + item, c = 0;

  if (pl->mct && y >= pl->lo[0] && y < pl->hi[0]) {
    const xtc_t  *tc = &t->tc[0];
    const xres_t *rs = top(tc);
    uint32_t      w  = (uint32_t)(rs->x1 - rs->x0), x;
    uint16_t     *out[3];
    void         *row = lanes(s, (size_t)w * 3u);

    if (!row) return false;
    for (c = 0; c < 3u; c++) {
      out[c] = plane_row(j, c, (size_t)(top(&t->tc[c])->y0 - (int64_t)j->py0[c]) + y) + (size_t)(top(&t->tc[c])->x0 - (int64_t)j->px0[c]);
    }
    if (tc->cc->reversible) {
      const int32_t *c0 = tc_row(j, t, 0u, y), *c1 = tc_row(j, t, 1u, y), *c2 = tc_row(j, t, 2u, y);
      int32_t       *r = row, *g = r + w, *b = g + w;

      for (x = 0; x < w; x++) {
        int32_t yy = c0[x], u = c1[x], v = c2[x];
        int32_t gg = (int32_t)((uint32_t)yy - (uint32_t)((int32_t)((uint32_t)u + (uint32_t)v) >> 2));

        r[x] = (int32_t)((uint32_t)v + (uint32_t)gg);
        g[x] = gg;
        b[x] = (int32_t)((uint32_t)u + (uint32_t)gg);
      }
    } else {
      const float *c0 = tc_row(j, t, 0u, y), *c1 = tc_row(j, t, 1u, y), *c2 = tc_row(j, t, 2u, y);
      float       *r = row, *g = r + w, *b = g + w;

      for (x = 0; x < w; x++) {
        float yy = c0[x], u = c1[x], v = c2[x];

        r[x] = yy + v * 1.402f;
        g[x] = yy - u * 0.34413f - v * 0.71414f;
        b[x] = yy + u * 1.772f;
      }
    }
    for (c = 0; c < 3u; c++) place_samples(j, c, tc->cc->reversible, (uint32_t *)row + (size_t)c * w, out[c], w);
  }
  for (; c < j->ncomp; c++) {
    const xtc_t  *tc = &t->tc[c];
    const xres_t *rs = top(tc);

    if (y < pl->lo[c] || y >= pl->hi[c] || j->skip[c]) continue;
    place_samples(j, c, tc->cc->reversible, tc_row(j, t, c, y),
                  plane_row(j, c, (size_t)(rs->y0 - (int64_t)j->py0[c]) + y) + (size_t)(rs->x0 - (int64_t)j->px0[c]),
                  (uint32_t)(rs->x1 - rs->x0));
  }
  return true;
}

/* the rows of a band in turn; a mapped tile-component's rows placed given
   back */
static bool
place_band(jpx_t *j, xscratch_t *s, void *ctx, uint32_t item) {
  const xplace_t *pl = ctx;
  uint32_t        r0 = item * X_BAND, r1 = r0 + X_BAND < pl->rows ? r0 + X_BAND : pl->rows, r, c;

  for (r = r0; r < r1; r++) {
    if (!place_row(j, s, ctx, r)) return false;
  }
  for (c = 0; c < j->ncomp; c++) {
    const xtc_t *tc  = &pl->t->tc[c];
    size_t       row = (size_t)(tc->x1 - tc->x0) * 4u;

    if (tc->mapped) im_release(true, (uint8_t *)tc->data + (size_t)r0 * row, (size_t)(r1 - r0) * row);
  }
  return true;
}

/* -- a tile ----------------------------------------------------------------------- */

static void
tile_close(xtile_t *t) {
  uint32_t c;

  if (!t) return;
  for (c = 0; t->tc && c < t->ncomp; c++) {
    if (t->tc[c].mapped) im_big_free(t->tc[c].data, t->tc[c].mapped, true);
  }
  arena_free_all(&t->arena);
  free(t);
}

/* a tile's headers, its data (where it's one part with its headers inline,
   read where it lies), its components' structures and its packets; *out
   NULL where it has no parts */
static bool
tile_open(jpx_t *j, uint32_t tile, xtile_t **out) {
  xtile_t  *t;
  bool      coc[X_COMPS] = { false }, qcc[X_COMPS] = { false }, poc_seen = false;
  uint32_t  i, c, nparts = 0, one = 0, p, q;
  size_t    body = 0, ppt = 0, ppm = 0;
  uint8_t  *buf;

  *out = NULL;
  if (!(t = calloc(1, sizeof(*t)))) return false;
  t->arena.limit = X_ARENA;
  t->arena.big   = j->big;
  t->cod         = j->cod;

  /* its tile-parts' headers, in order; their packets and packet headers */
  for (i = 0; i < j->nparts; i++) {
    const xpart_t *part = &j->parts[i];
    size_t         pos  = 0;

    if (part->tile != tile) continue;
    nparts++;
    one   = i;
    body += part->len;
    ppm  += part->ppm_len;
    while (pos + 4u <= part->head_len) {
      uint32_t m = be16(part->head + pos), l = be16(part->head + pos + 2u);

      if (l < 2u || pos + 2u + l > part->head_len) break;
      if (m == 0xFF61u && l >= 3u) ppt += l - 3u;
      else if (!coding_marker(j, &t->cod, coc, qcc, &poc_seen, m, part->head + pos + 4u, l - 2u)) goto fail;
      pos += 2u + l;
    }
  }
  if (!nparts) {
    free(t);
    return true;
  }
  if (nparts == 1u && !ppt && !j->ppm) {
    t->body     = j->parts[one].data;
    t->body_len = j->parts[one].len;
  } else {
    if (!(buf = arena_alloc(&t->arena, body + ppt + ppm + 1u))) goto fail;
    t->body = buf;
    t->hdr  = j->ppm ? buf + body : ppt ? buf + body + ppm : NULL;
    for (i = 0; i < j->nparts; i++) {
      const xpart_t *part = &j->parts[i];
      size_t         pos  = 0;

      if (part->tile != tile) continue;
      memcpy(buf + t->body_len, part->data, part->len);
      t->body_len += part->len;
      if (j->ppm) {
        memcpy(buf + body + t->hdr_len, part->ppm, part->ppm_len);
        t->hdr_len += part->ppm_len;
      }
      while (!j->ppm && pos + 4u <= part->head_len) {
        uint32_t m = be16(part->head + pos), l = be16(part->head + pos + 2u);

        if (l < 2u || pos + 2u + l > part->head_len) break;
        if (m == 0xFF61u && l >= 3u) {
          memcpy(buf + body + ppm + t->hdr_len, part->head + pos + 5u, l - 3u);
          t->hdr_len += l - 3u;
        }
        pos += 2u + l;
      }
    }
  }

  /* where it lies, and its components' structures */
  p     = tile % j->ntx;
  q     = tile / j->ntx;
  t->x0 = (int64_t)j->tx0 + (int64_t)p * j->tw;
  t->y0 = (int64_t)j->ty0 + (int64_t)q * j->th;
  t->x1 = t->x0 + j->tw;
  t->y1 = t->y0 + j->th;
  if (t->x0 < j->ox0) t->x0 = j->ox0;
  if (t->y0 < j->oy0) t->y0 = j->oy0;
  if (t->x1 > j->ox1) t->x1 = j->ox1;
  if (t->y1 > j->oy1) t->y1 = j->oy1;
  if (!(t->tc = zget(&t->arena, (uint64_t)j->ncomp * sizeof(*t->tc)))) goto fail;
  t->ncomp = j->ncomp;
  for (c = 0; c < j->ncomp; c++) {
    if (!tc_build(j, t, c)) goto fail;
  }

  /* the packets */
  if (t->cod.npoc) {
    for (i = 0; i < t->cod.npoc && !t->stop; i++) {
      const xpoc_t *v = &t->cod.poc[i];

      progression(j, t, v->order, v->rs, v->re, v->cs, v->ce, v->le);
    }
  } else {
    progression(j, t, t->cod.order, 0u, X_LEVELS + 1u, 0u, j->ncomp, t->cod.layers);
  }
  *out = t;
  return true;

fail:
  tile_close(t);
  return false;
}

/* whether the tile's three first components go through its colour
   transform (one size, one wavelet) */
static bool
tile_mct(const jpx_t *j, const xtile_t *t) {
  return t->cod.mct && j->ncomp >= 3u && t->tc[0].x1 - t->tc[0].x0 == t->tc[1].x1 - t->tc[1].x0
      && t->tc[0].x1 - t->tc[0].x0 == t->tc[2].x1 - t->tc[2].x0 && t->tc[0].y1 - t->tc[0].y0 == t->tc[1].y1 - t->tc[1].y0
      && t->tc[0].y1 - t->tc[0].y0 == t->tc[2].y1 - t->tc[2].y0 && t->tc[1].cc->reversible == t->tc[0].cc->reversible
      && t->tc[2].cc->reversible == t->tc[0].cc->reversible;
}

/* the tile whole: every component's samples, its code-blocks, each level's
   rows and columns, into the planes */
static bool
tile_whole(jpx_t *j, xtile_t *t) {
  uint64_t bytes;
  uint32_t c;

  for (c = 0; c < j->ncomp; c++) {
    xtc_t *tc = &t->tc[c];

    bytes = (uint64_t)(tc->x1 - tc->x0) * (uint64_t)(tc->y1 - tc->y0) * 4u;
    if (j->big && bytes >= X_MAP_MIN && bytes <= SIZE_MAX) {
      if (!(tc->data = im_big_alloc((size_t)bytes, true, true))) return false;
      tc->mapped = (size_t)bytes;
    } else if (!(tc->data = zget(&t->arena, bytes))) {
      return false;
    }
  }

  /* the code-blocks, then each level's rows and columns */
  {
    xjob_t  *jobs;
    uint32_t n = 0, r;

    if (!(jobs = arena_alloc(&t->arena, (uint64_t)(t->blocks ? t->blocks : 1u) * sizeof(*jobs)))) return false;
    for (c = 0; c < j->ncomp; c++) {
      xtc_t *tc = &t->tc[c];

      if (j->skip[c]) continue;
      for (r = 0; r < tc->nuse; r++) {
        xres_t  *res = &tc->res[r];
        uint32_t k, b, m;

        for (k = 0; k < res->pw * res->ph; k++) {
          for (b = 0; b < res->nbands; b++) {
            xpband_t *pb = &res->prec[k * res->nbands + b];

            for (m = 0; m < pb->cw * pb->ch; m++) {
              const xband_t *band = &res->band[b];
              const xcblk_t *kb   = &pb->blk[m];
              size_t         st   = (size_t)(tc->x1 - tc->x0);

              if (!kb->nsegs || n >= t->blocks) continue;
              jobs[n].b      = &pb->blk[m];
              jobs[n].band   = band;
              jobs[n].cc     = tc->cc;
              jobs[n].stride = st;
              jobs[n].fill   = false;
              jobs[n].out    = (uint32_t *)tc->data + (band->offy + (size_t)(kb->y0 - band->y0)) * st
                             + band->offx + (size_t)(kb->x0 - band->x0);
              n++;
            }
          }
        }
      }
    }
    if (!pool_run(j, t1_job, jobs, n)) return false;
    for (c = 0; c < j->ncomp; c++) {
      xtc_t *tc = &t->tc[c];

      if (j->skip[c]) continue;
      for (r = 1; r < tc->nuse; r++) {
        xlevel_t lv;
        uint32_t w = (uint32_t)(tc->res[r].x1 - tc->res[r].x0), h = (uint32_t)(tc->res[r].y1 - tc->res[r].y0);

        lv.tc = tc;
        lv.r  = r;
        if (!w || !h) continue;
        if (!pool_run(j, idwt_rows, &lv, (h + V - 1u) / V) || !pool_run(j, idwt_cols, &lv, (w + STRIP - 1u) / STRIP)) return false;
      }
    }
  }

  /* the colour transform, the DC level shift, into the planes: a row an item */
  {
    xplace_t pl;

    memset(&pl, 0, sizeof(pl));
    pl.t   = t;
    pl.mct = tile_mct(j, t);
    for (c = 0; c < j->ncomp; c++) {
      pl.hi[c] = (uint32_t)(top(&t->tc[c])->y1 - top(&t->tc[c])->y0);
      if (pl.hi[c] > pl.rows) pl.rows = pl.hi[c];
    }
    /* a mapped tile-component's by bands, its rows given back as they go */
    for (c = 0; c < j->ncomp && !t->tc[c].mapped; c++) {}
    if (c < j->ncomp ? !pool_run(j, place_band, &pl, (pl.rows + X_BAND - 1u) / X_BAND) : !pool_run(j, place_row, &pl, pl.rows)) {
      return false;
    }
  }
  return true;
}

static bool
tile_decode(jpx_t *j, uint32_t tile) {
  xtile_t *t;
  bool     ok;

  if (!tile_open(j, tile, &t)) return false;
  ok = !t || tile_whole(j, t);
  tile_close(t);
  return ok;
}

/* tile `tile`'s place in component c's plane: rows [*Y0, *Y1), columns
   [*X0, *X1) */
static void
tile_rect(const jpx_t *j, uint32_t tile, uint32_t c, int64_t *X0, int64_t *Y0, int64_t *X1, int64_t *Y1) {
  int64_t p = tile % j->ntx, q = tile / j->ntx;
  int64_t x0 = (int64_t)j->tx0 + p * j->tw, y0 = (int64_t)j->ty0 + q * j->th, x1 = x0 + j->tw, y1 = y0 + j->th;

  x0  = x0 > j->ox0 ? x0 : j->ox0;
  y0  = y0 > j->oy0 ? y0 : j->oy0;
  x1  = x1 < j->ox1 ? x1 : j->ox1;
  y1  = y1 < j->oy1 ? y1 : j->oy1;
  *X0 = ceil_pow2(ceil_div_u(x0, j->dx[c]), j->reduce) - j->px0[c];
  *Y0 = ceil_pow2(ceil_div_u(y0, j->dy[c]), j->reduce) - j->py0[c];
  *X1 = ceil_pow2(ceil_div_u(x1, j->dx[c]), j->reduce) - j->px0[c];
  *Y1 = ceil_pow2(ceil_div_u(y1, j->dy[c]), j->reduce) - j->py0[c];
}

/* compose's rows as the tiles are made, a row of tiles at a time: after
   each, the image's rows all of whose components' plane rows are there
   (the rest with the next row's); a tile with no data its place zeroed
   (the planes' rings hold an earlier row's) */
static bool
tiles_run(jpx_t *j, xbands_t *b) {
  uint32_t q, p, c, Y = 0, Y1;

  for (q = 0; q < j->nty; q++) {
    int64_t done[X_COMPS], X0, Y0, X1;

    for (p = 0; p < j->ntx; p++) {
      uint32_t tile = q * j->ntx + p;
      xtile_t *t;
      bool     ok;

      if (!tile_open(j, tile, &t)) return false;
      if (!t) {
        for (c = 0; c < j->ncomp; c++) {
          int64_t Y1r, y;

          tile_rect(j, tile, c, &X0, &Y0, &X1, &Y1r);
          for (y = Y0; y < Y1r && X1 > X0; y++) memset(plane_row(j, c, (size_t)y) + X0, 0, (size_t)(X1 - X0) * sizeof(uint16_t));
        }
        continue;
      }
      ok = tile_whole(j, t);
      tile_close(t);
      if (!ok) return false;
    }
    for (c = 0; c < j->ncomp; c++) tile_rect(j, q * j->ntx, c, &X0, &Y0, &X1, &done[c]);
    for (Y1 = Y; Y1 < b->h; Y1++) {
      for (c = 0; c < j->ncomp; c++) {
        int64_t sy = (int64_t)(j->y0 + Y1) / j->dy[c] - (int64_t)j->py0[c];

        sy = sy < 0 ? 0 : sy >= (int64_t)j->ph[c] ? (int64_t)j->ph[c] - 1 : sy;
        if (sy >= done[c]) break;
      }
      if (c < j->ncomp) break;
    }
    if (q + 1u == j->nty) Y1 = b->h;
    if (Y1 > Y) {
      b->Y0 = Y;
      b->Y1 = Y1;
      if (!pool_run(j, span_job, b, (Y1 - Y) * b->pieces)) return false;
      Y = Y1;
    }
  }
  return true;
}

/* -- the one tile by bands ------------------------------------------------------- */

static bool
grow(void **p, uint32_t *cap, uint64_t want, size_t each) {
  uint64_t n = *cap ? *cap : 64u;
  void    *q;

  if (want <= *cap) return true;
  while (n < want) n *= 2u;
  if (n > UINT32_MAX || n > SIZE_MAX / each || !(q = realloc(*p, (size_t)n * each))) return false;
  *p   = q;
  *cap = (uint32_t)n;
  return true;
}

/* the band's next code-block row, its blocks' jobs; false past its last */
static bool
sband_next(xstream_t *st, xsband_t *sb, const xcc_t *cc) {
  const xres_t  *res  = sb->res;
  const xband_t *band = &res->band[sb->b];

  while (sb->py < res->ph) {
    const xpband_t *any = NULL;
    uint32_t        px, u, ch = 0;

    for (px = 0; px < res->pw; px++) {
      const xpband_t *pb = &res->prec[((size_t)sb->py * res->pw + px) * res->nbands + sb->b];

      if (pb->cw && pb->ch > ch) {
        ch  = pb->ch;
        any = pb;
      }
    }
    if (sb->v >= ch) {
      sb->py++;
      sb->v = 0u;
      continue;
    }
    for (px = 0; px < res->pw; px++) {
      const xpband_t *pb = &res->prec[((size_t)sb->py * res->pw + px) * res->nbands + sb->b];

      if (!pb->cw || sb->v >= pb->ch) continue;
      if (!grow((void **)&st->jobs, &st->jcap, (uint64_t)st->njobs + pb->cw, sizeof(*st->jobs))) {
        st->failed = true;
        return false;
      }
      for (u = 0; u < pb->cw; u++) {
        xcblk_t *k  = &pb->blk[(size_t)sb->v * pb->cw + u];
        xjob_t  *jb = &st->jobs[st->njobs++];

        jb->b      = k;
        jb->band   = band;
        jb->cc     = cc;
        jb->fill   = true;
        jb->stride = sb->ring->row / 4u;
        jb->out    = (uint32_t *)(void *)ring_row(sb->ring, (uint32_t)(k->y0 - band->y0)) + sb->col + (uint32_t)(k->x0 - band->x0);
      }
    }
    sb->have = (uint32_t)(any->blk[(size_t)sb->v * any->cw].y1 - band->y0);
    sb->v++;
    return true;
  }
  return false;
}

/* the steps a level runs to for `need` rows out, and the rows out after
   `t1` steps (ring97's and ring53's: a low and a high final a step, the
   9/7's a step later) */
static uint32_t
lv_steps(const xslv_t *lv, uint32_t need, bool rev) {
  uint32_t t1;

  if (need <= lv->done) return lv->t;
  if (need >= lv->h) return lv->steps;
  t1 = rev ? (need + 2u - lv->cy) / 2u : (need + 4u - lv->cy) / 2u;
  t1 = t1 < lv->steps ? t1 : lv->steps;
  return t1 > lv->t ? t1 : lv->t;
}

static uint32_t
lv_done(const xslv_t *lv, uint32_t t1, bool rev) {
  int64_t d;

  if (t1 >= lv->steps) return lv->h;
  d = 2 * (int64_t)t1 - (rev ? 1 : 3) + (int64_t)lv->cy;
  return d <= 0 ? 0u : d >= (int64_t)lv->h ? lv->h : (uint32_t)d;
}

static bool
srow_job(jpx_t *j, xscratch_t *s, void *ctx, uint32_t item) {
  const xsrow_t *r   = &((const xstream_t *)ctx)->rows[item];
  uint8_t       *tmp = lanes(s, r->w);

  (void)j;
  if (!tmp) return false;
  idwt_row(ring_row(r->ring, r->i), tmp, r->w, r->sw, r->cx, r->rev);
  return true;
}

static bool
strip_job(jpx_t *j, xscratch_t *s, void *ctx, uint32_t item) {
  const xsstrip_t *sp = &((const xstream_t *)ctx)->strips[item];
  const xslv_t    *lv = sp->lv;

  (void)j;
  (void)s;
  if (lv->h == 1u) {
    /* a row alone: a low as it is, a high halved where reversible (as
       lift_apart has them) */
    const int32_t *src = (const int32_t *)(void *)ring_row(lv->cy ? &lv->H : &lv->L, 0u) + sp->x;
    int32_t       *o   = (int32_t *)(void *)ring_row(lv->D, 0u) + sp->x;
    uint32_t       k;

    if (lv->cy && sp->rev) {
      for (k = 0; k < sp->n; k++) o[k] = src[k] / 2;
    } else {
      memcpy(o, src, (size_t)sp->n * 4u);
    }
    return true;
  }
  if (sp->rev) ring53(&lv->L, &lv->H, lv->D, lv->sn, lv->dn, lv->cy, sp->x, sp->n, lv->t, lv->t1);
  else ring97(&lv->L, &lv->H, lv->D, lv->sn, lv->dn, lv->cy, sp->x, sp->n, lv->t, lv->t1);
  return true;
}

/* a code-block, or (past them) a piece of the rows composed alongside */
static bool
mix_job(jpx_t *j, xscratch_t *s, void *ctx, uint32_t item) {
  const xstream_t *st = ctx;

  if (item < st->njobs) return t1_job(j, s, st->jobs, item);
  return span_job(j, s, (void *)st->also, item - st->njobs);
}

/* each component's top rows made to need[c] (at least), and placed; the
   rows `also` has composed with the code-blocks */
static bool
stream_to(jpx_t *j, const uint32_t *need) {
  xstream_t *st = j->stream;
  xtile_t   *t  = st->t;
  uint32_t   c, r, i;

  /* what each level needs, from the top down, and the code-block rows
     under it */
  st->njobs = 0;
  for (c = 0; c < j->ncomp; c++) {
    xscomp_t    *sc   = &st->c[c];
    const xtc_t *tc   = &t->tc[c];
    bool         rev  = tc->cc->reversible;
    uint32_t     want = need[c];

    if (!sc->on || want <= sc->done) continue;
    for (r = sc->top; r >= 1u; r--) {
      xslv_t *lv = &sc->lv[r];

      if (want > lv->done && want - lv->done < lv->gran) want = lv->h - lv->done > lv->gran ? lv->done + lv->gran : lv->h;
      lv->t1  = lv_steps(lv, want, rev);
      lv->lto = lv->t1 < lv->sn ? lv->t1 : lv->sn;
      lv->hto = lv->t1 + lv->cy < lv->dn ? lv->t1 + lv->cy : lv->dn;
      while (lv->band[0].have < lv->lto && sband_next(st, &lv->band[0], tc->cc)) {}
      while (lv->band[1].have < lv->hto && sband_next(st, &lv->band[1], tc->cc)) {}
      while (lv->band[2].have < lv->hto && sband_next(st, &lv->band[2], tc->cc)) {}
      want = lv->lto;
    }
    while (sc->ll.have < want && sband_next(st, &sc->ll, tc->cc)) {}
  }
  if (st->failed || !pool_run(j, mix_job, st, st->njobs + (st->also ? (st->also->Y1 - st->also->Y0) * st->also->pieces : 0u))) {
    return false;
  }
  st->also = NULL;

  /* each level up: its new rows made whole across, then its steps */
  for (r = 1; r <= st->maxtop; r++) {
    st->nrows = 0;
    for (c = 0; c < j->ncomp; c++) {
      xscomp_t *sc = &st->c[c];
      xslv_t   *lv = &sc->lv[r];
      bool      rev;

      if (!sc->on || sc->top < r) continue;
      rev = t->tc[c].cc->reversible;
      if (lv->w) {
        if (!grow((void **)&st->rows, &st->rcap, (uint64_t)st->nrows + (lv->lto - lv->lsyn) + (lv->hto - lv->hsyn), sizeof(*st->rows))) {
          return false;
        }
        for (i = lv->lsyn; i < lv->lto; i++) {
          xsrow_t *w = &st->rows[st->nrows++];

          w->ring = &lv->L;
          w->i    = i;
          w->w    = lv->w;
          w->sw   = lv->sw;
          w->cx   = lv->cx;
          w->rev  = rev;
        }
        for (i = lv->hsyn; i < lv->hto; i++) {
          xsrow_t *w = &st->rows[st->nrows++];

          w->ring = &lv->H;
          w->i    = i;
          w->w    = lv->w;
          w->sw   = lv->sw;
          w->cx   = lv->cx;
          w->rev  = rev;
        }
      }
      lv->lsyn = lv->lto;
      lv->hsyn = lv->hto;
    }
    if (!pool_run(j, srow_job, st, st->nrows)) return false;

    st->nstrips = 0;
    for (c = 0; c < j->ncomp; c++) {
      xscomp_t *sc = &st->c[c];
      xslv_t   *lv = &sc->lv[r];
      uint32_t  x;

      if (!sc->on || sc->top < r || lv->t1 <= lv->t || !lv->w) continue;
      if (!grow((void **)&st->strips, &st->scap, (uint64_t)st->nstrips + (lv->w + STRIP - 1u) / STRIP, sizeof(*st->strips))) return false;
      for (x = 0; x < lv->w; x += STRIP) {
        xsstrip_t *sp = &st->strips[st->nstrips++];

        sp->lv  = lv;
        sp->x   = x;
        sp->n   = lv->w - x < STRIP ? lv->w - x : STRIP;
        sp->rev = t->tc[c].cc->reversible;
      }
    }
    if (!pool_run(j, strip_job, st, st->nstrips)) return false;
    for (c = 0; c < j->ncomp; c++) {
      xscomp_t *sc = &st->c[c];
      xslv_t   *lv = &sc->lv[r];

      if (!sc->on || sc->top < r) continue;
      lv->t    = lv->t1;
      lv->done = lv_done(lv, lv->t, t->tc[c].cc->reversible);
    }
  }

  /* the rows made, placed (the colour transform's three as far as all
     three are) */
  {
    xplace_t pl;
    uint32_t lo = UINT32_MAX, hi = 0;

    memset(&pl, 0, sizeof(pl));
    pl.t   = t;
    pl.mct = st->mct;
    for (c = 0; c < j->ncomp; c++) {
      xscomp_t *sc = &st->c[c];

      if (!sc->on) continue;
      sc->done = sc->top ? sc->lv[sc->top].done : sc->ll.have;
      pl.lo[c] = sc->placed;
      pl.hi[c] = sc->done;
    }
    if (pl.mct) {
      uint32_t m = pl.hi[0] < pl.hi[1] ? pl.hi[0] : pl.hi[1];

      m        = m < pl.hi[2] ? m : pl.hi[2];
      pl.hi[0] = pl.hi[1] = pl.hi[2] = m > pl.lo[0] ? m : pl.lo[0];
    }
    for (c = 0; c < j->ncomp; c++) {
      if (!st->c[c].on || pl.hi[c] <= pl.lo[c]) continue;
      lo = pl.lo[c] < lo ? pl.lo[c] : lo;
      hi = pl.hi[c] > hi ? pl.hi[c] : hi;
    }
    if (hi > lo) {
      pl.base = lo;
      pl.rows = hi - lo;
      if (!pool_run(j, place_row, &pl, pl.rows)) return false;
    }
    for (c = 0; c < j->ncomp; c++) {
      if (st->c[c].on && pl.hi[c] > st->c[c].placed) st->c[c].placed = pl.hi[c];
    }
  }
  return true;
}

/* compose's bands, each as soon as the planes' rows under it are made:
   with the next band's code-blocks (the output's fresh pages are the
   kernel's to give, one at a time), the last alone */
static bool
stream_run(jpx_t *j, xbands_t *b) {
  xstream_t *st = j->stream;
  xbands_t   prev = *b;
  uint32_t   need[X_COMPS], c, h = b->h, Y0, Y1;

  for (Y0 = 0; Y0 < h; Y0 = Y1) {
    Y1 = h - Y0 > X_STRIPE ? Y0 + X_STRIPE : h;
    if (st->t) {
      for (c = 0; c < j->ncomp; c++) {
        int64_t sy = (int64_t)(j->y0 + Y1 - 1u) / j->dy[c] - (int64_t)j->py0[c];

        sy      = sy < 0 ? 0 : sy >= (int64_t)j->ph[c] ? (int64_t)j->ph[c] - 1 : sy;
        need[c] = (uint32_t)sy + 1u;
      }
      st->also = Y0 ? &prev : NULL;
      if (!stream_to(j, need)) return false;
    } else if (Y0 && !pool_run(j, span_job, &prev, (prev.Y1 - prev.Y0) * prev.pieces)) {
      return false;
    }
    prev.Y0 = Y0;
    prev.Y1 = Y1;
  }
  return h ? pool_run(j, span_job, &prev, (prev.Y1 - prev.Y0) * prev.pieces) : true;
}

/* the smallest power of two at least n */
static uint32_t
pow2_at_least(uint64_t n) {
  uint32_t p = 1u;

  while (p < n && p < (1u << 31)) p <<= 1;
  return p;
}

static bool
ring_new(xring_t *r, uint32_t cap, uint32_t w, uint32_t y0) {
  r->row  = (size_t)w * 4u;
  r->mask = cap - 1u;
  r->y0   = y0;
  return (r->p = malloc((size_t)cap * r->row ? (size_t)cap * r->row : 1u)) != NULL;
}

static void
stream_close(jpx_t *j) {
  xstream_t *st = j->stream;
  uint32_t   c, r;

  if (!st) return;
  for (c = 0; st->c && c < j->ncomp; c++) {
    for (r = 1; r <= X_LEVELS; r++) {
      free(st->c[c].lv[r].L.p);
      free(st->c[c].lv[r].H.p);
    }
    free(st->c[c].out.p);
  }
  free(st->jobs);
  free(st->rows);
  free(st->strips);
  tile_close(st->t);
  free(st->c);
  free(st);
  j->stream = NULL;
}

/* the one tile opened, each component's rings and the planes' (a band's
   rows and a few) */
static bool
stream_open(jpx_t *j) {
  xstream_t *st;
  xtile_t   *t;
  uint32_t   c, r, made[X_LEVELS + 1]; /* a level's rows a round at most */

  if (!(st = calloc(1, sizeof(*st)))) return false;
  j->stream = st;
  if (!(st->c = calloc(j->ncomp, sizeof(*st->c))) || !tile_open(j, 0u, &st->t)) return false;
  t = st->t;
  for (c = 0; c < j->ncomp; c++) {
    xscomp_t *sc   = &st->c[c];
    uint32_t  rows = X_STRIPE / j->dy[c] + 4u, lead = 0u, cap; /* rows: the top's a band */
    size_t    bytes;

    if (t && !j->skip[c]) {
      xtc_t        *tc = &t->tc[c];
      const xres_t *tr = top(tc);
      bool          rev = tc->cc->reversible;

      /* the planes' rows are the top level's (the image one tile) */
      if (tr->y0 != (int64_t)j->py0[c] || tr->x0 != (int64_t)j->px0[c] || (uint32_t)(tr->x1 - tr->x0) != j->pw[c]
          || (uint32_t)(tr->y1 - tr->y0) != j->ph[c]) {
        return false;
      }
      sc->on  = true;
      sc->top = tc->nuse - 1u;
      st->maxtop = sc->top > st->maxtop ? sc->top : st->maxtop;
      /* each level's rows out a round at most: the top's a band's, a lower
         one's what the one above reads a round, or (a narrow one) more */
      {
        uint32_t want = rows;

        for (r = sc->top; r >= 1u; r--) {
          xslv_t  *lv = &sc->lv[r];
          uint32_t w = (uint32_t)(tc->res[r].x1 - tc->res[r].x0), h = (uint32_t)(tc->res[r].y1 - tc->res[r].y0);

          lv->gran = r < sc->top ? (X_ROUND / (w ? w : 1u) < h ? X_ROUND / (w ? w : 1u) : h) : 0u;
          want     = (want > lv->gran ? want : lv->gran) + 2u;
          made[r]  = want;
          want     = want / 2u + 4u;
        }
      }
      for (r = 1; r <= sc->top; r++) {
        xslv_t       *lv  = &sc->lv[r];
        const xres_t *res = &tc->res[r], *low = &tc->res[r - 1u];
        uint32_t      ahead = 1u << res->ycb, full, b;

        /* its rows decoded ahead (a code-block row's), or the level below's
           made ahead */
        if (r == 1u && (1u << low->ycb) > ahead) ahead = 1u << low->ycb;
        if (r > 1u && made[r - 1u] > ahead) ahead = made[r - 1u];
        lv->w  = (uint32_t)(res->x1 - res->x0);
        lv->h  = (uint32_t)(res->y1 - res->y0);
        lv->sw = (uint32_t)(low->x1 - low->x0);
        lv->sn = (uint32_t)(low->y1 - low->y0);
        lv->dn = lv->h - lv->sn;
        lv->cx = (uint32_t)res->x0 & 1u;
        lv->cy = (uint32_t)res->y0 & 1u;
        if (lv->h >= 2u) {
          int64_t a = rev ? (int64_t)lv->sn - 1 : (int64_t)lv->sn, d = rev ? (int64_t)lv->dn - lv->cy : (int64_t)lv->dn + 1 - lv->cy;

          lv->steps = (uint32_t)(a > d ? a : d) + 1u;
        } else {
          lv->steps = lv->h;
        }
        /* a band's rows here at most, the code-block rows decoded ahead of
           them, those its steps still read */
        cap  = pow2_at_least((uint64_t)made[r] / 2u + ahead + 12u);
        full = pow2_at_least(lv->sn > lv->dn ? lv->sn : lv->dn);
        full = full > (1u << res->ycb) ? full : 1u << res->ycb;
        full = r == 1u && full < (1u << low->ycb) ? 1u << low->ycb : full;
        cap  = cap < full ? cap : full;
        if (!ring_new(&lv->L, cap, lv->w, (uint32_t)res->band[0].y0) || !ring_new(&lv->H, cap, lv->w, (uint32_t)res->band[1].y0)) {
          return false;
        }
        for (b = 0; b < 3u; b++) {
          lv->band[b].res  = res;
          lv->band[b].b    = b;
          lv->band[b].ring = b ? &lv->H : &lv->L;
          lv->band[b].col  = b == 1u ? 0u : lv->sw;
        }
      }
      for (r = 1; r < sc->top; r++) sc->lv[r].D = &sc->lv[r + 1u].L;
      if (!sc->top) lead = 1u << tc->res[0].ycb; /* no level: its code-block rows ahead */
      cap = pow2_at_least((uint64_t)rows + lead + 8u);
      if (!ring_new(&sc->out, cap, (uint32_t)(tr->x1 - tr->x0), sc->top ? 0u : (uint32_t)tc->res[0].band[0].y0)) return false;
      if (sc->top) sc->lv[sc->top].D = &sc->out;
      sc->ll.res  = &tc->res[0];
      sc->ll.b    = 0u;
      sc->ll.ring = sc->top ? &sc->lv[1].L : &sc->out;
      sc->ll.col  = 0u;
    }
    /* the plane: a ring past a band's rows and those decoded ahead */
    cap = pow2_at_least((uint64_t)rows + (t && !j->skip[c] && !st->c[c].top ? 1u << t->tc[c].res[0].ycb : 0u) + 8u);
    if (cap >= j->ph[c]) {
      cap          = j->ph[c];
      j->pmask[c] = SIZE_MAX;
    } else {
      j->pmask[c] = cap - 1u;
    }
    bytes = (size_t)cap * j->pw[c] * sizeof(uint16_t);
    if (!(j->plane[c] = im_big_alloc(bytes, true, false))) return false;
    j->psize[c] = bytes;
  }
  st->mct = t && tile_mct(j, t) && st->c[0].on && st->c[1].on && st->c[2].on;
  return true;
}

/* -- JP2's boxes ------------------------------------------------------------------- */

/* the boxes in [p, p + len): `jp2c` the first codestream */
static bool
read_boxes(jpx_t *j, jp2_t *x, const uint8_t *p, size_t len, const uint8_t **cs, size_t *cs_len, uint32_t depth) {
  size_t pos = 0;

  while (pos + 8u <= len) {
    uint64_t       lbox = be32(p + pos), hdr = 8u;
    uint32_t       tbox = be32(p + pos + 4u);
    const uint8_t *s;
    size_t         n;

    if (lbox == 1u) {
      if (pos + 16u > len) return false;
      lbox = (uint64_t)be32(p + pos + 8u) << 32 | be32(p + pos + 12u);
      hdr  = 16u;
    } else if (lbox == 0u) {
      lbox = len - pos;
    }
    if (lbox < hdr || lbox > len - pos) {
      if (tbox != 0x6A703263u) return false; /* a codestream may run short */
      lbox = len - pos;
    }
    s = p + pos + hdr;
    n = (size_t)(lbox - hdr);
    switch (tbox) {
    case 0x6A703263u: /* jp2c: read to its EOC, past its box if it says less (as OpenJPEG does) */
      if (!*cs) {
        *cs     = s;
        *cs_len = len - (size_t)(s - p);
      }
      break;
    case 0x6A703268u: /* jp2h */
      if (depth < 2u && !read_boxes(j, x, s, n, cs, cs_len, depth + 1u)) return false;
      break;
    case 0x636F6C72u: /* colr: the first one */
      if (x->colr || n < 3u) break;
      x->colr = true;
      if (s[0] == 1u && n >= 7u) x->enumcs = be32(s + 3);
      else if ((s[0] == 2u || s[0] == 3u) && n >= 3u + 20u) {
        x->icc      = be32(s + 3 + 16);
        x->icc_data = s + 3;
        x->icc_len  = n - 3u;
      }
      break;
    case 0x70636C72u: { /* pclr */
      uint32_t ne, nc, e, k, at;

      if (n < 3u) break;
      ne = be16(s);
      nc = s[2];
      if (!ne || ne > X_PAL || !nc || nc > X_COMPS || n < 3u + nc) break;
      for (k = 0, at = 0; k < nc; k++) {
        x->pal_bits[k] = (uint8_t)((s[3 + k] & 0x7Fu) + 1u);
        if (x->pal_bits[k] > 16u) return false;
        at += (x->pal_bits[k] + 7u) / 8u;
      }
      if (n < 3u + nc + (size_t)at * ne || !(x->pal = arena_alloc(&j->arena, (uint64_t)ne * nc * sizeof(*x->pal)))) break;
      {
        const uint8_t *v = s + 3 + nc;

        for (e = 0; e < ne; e++) {
          for (k = 0; k < nc; k++) {
            uint32_t bytes = (x->pal_bits[k] + 7u) / 8u, val = 0, b;

            for (b = 0; b < bytes; b++) val = val << 8 | *v++;
            x->pal[e * nc + k] = (int32_t)(val & ((1u << x->pal_bits[k]) - 1u));
          }
        }
      }
      x->pal_n    = ne;
      x->pal_cols = nc;
      break;
    }
    case 0x636D6170u: { /* cmap */
      uint32_t k;

      for (k = 0; k < n / 4u && k < X_COMPS; k++) {
        x->map[k].cmp     = (uint16_t)be16(s + 4 * k);
        x->map[k].palette = s[4 * k + 2] == 1u;
        x->map[k].col     = s[4 * k + 3];
      }
      x->nmap = k;
      break;
    }
    case 0x63646566u: { /* cdef */
      uint32_t k, m;

      if (n < 2u) break;
      m = be16(s);
      for (k = 0; k < m && k < X_COMPS && 2u + 6u * (k + 1u) <= n; k++) {
        x->def[k].cn   = (uint16_t)be16(s + 2 + 6 * k);
        x->def[k].typ  = (uint16_t)be16(s + 4 + 6 * k);
        x->def[k].asoc = (uint16_t)be16(s + 6 + 6 * k);
      }
      x->ndef = k;
      break;
    }
    default:
      break;
    }
    pos += (size_t)lbox;
  }
  return true;
}

/* -- out: the channels to 8 or 16 bits, into im's image ----------------------------- */

typedef struct xchan {
  uint32_t comp;
  int32_t  col;  /* a palette's column; -1 none */
  uint32_t bits;
} xchan_t;

/* a sample of 8 bits or fewer to 8 (fewer spread over
   0–255) */
static uint8_t
eight(int32_t v, uint32_t bits) {
  int32_t max;

  if (bits == 8u) return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
  max = (1 << bits) - 1;
  v   = v < 0 ? 0 : v > max ? max : v;
  return (uint8_t)((v * 255 + max / 2) / max);
}

/* OpenJPEG's 8 bits: a deeper sample shifted, rounded by the bit below; a
   shallower one spread, or (indexes) as it is */
static uint8_t
eight_shift(int32_t v, uint32_t bits, bool raw) {
  if (bits > 8u) {
    uint32_t adj = bits - 8u;
    int32_t  px  = (v >> adj) + ((v >> (adj - 1u)) & 1);

    return (uint8_t)(px < 0 ? 0 : px > 255 ? 255 : px);
  }
  if (bits == 8u || raw) return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
  return eight(v, bits);
}

/* a sample of 16 bits or fewer to 16, fewer spread over 0–65535 as above */
static uint16_t
sixteen(int32_t v, uint32_t bits) {
  int32_t max = (int32_t)((1u << bits) - 1u);

  v = v < 0 ? 0 : v > max ? max : v;
  if (bits == 16u) return (uint16_t)v;
  return (uint16_t)(((int64_t)v * 65535 + max / 2) / max);
}

static void
sycc(int32_t offset, int32_t upb, int32_t y, int32_t cb, int32_t cr, int32_t *r, int32_t *g, int32_t *b) {
  int32_t v;

  cb -= offset;
  cr -= offset;
  v  = y + (int32_t)(1.402f * (float)cr);
  *r = v < 0 ? 0 : v > upb ? upb : v;
  v  = y - (int32_t)(0.344f * (float)cb + 0.714f * (float)cr);
  *g = v < 0 ? 0 : v > upb ? upb : v;
  v  = y + (int32_t)(1.772f * (float)cb);
  *b = v < 0 ? 0 : v > upb ? upb : v;
}

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#  define X_HOST_BIG true
#else
#  define X_HOST_BIG false
#endif

/* what a caller asks of a decode: threads, a palette's indexes as they
   are (one gray channel, the first component's samples unscaled), RGB as
   RGB0 */
typedef struct jxopts {
  ImByteOrder order;
  uint32_t    threads;
  bool        raw, pad;
  bool        eight;   /* deeper samples in 8 bits, OpenJPEG's way */
  int32_t     opacity; /* ImOpacity */
  im_runner_t runner;  /* the caller's threads */
  uint8_t    *dest;    /* the caller's memory, `dest_stride` a row; NULL: im's */
  size_t      dest_stride, dest_size;
  uint32_t    scale;   /* 1 (or 0), 2, 4, 8: levels left out, as many as halve it */
} jxopts_t;

/* which channel is which: through a palette where the JP2 maps one; the
   colours (by colr, else an ICC profile's space, else the count) and an
   opacity (cdef's, else the first past the colours); sYCC. False: the
   mapping doesn't fit */
typedef struct xpick {
  xchan_t  ch[X_COMPS];
  uint32_t nch, ncol, space, colour[4];
  int32_t  opacity;
  bool     ycc, premult;
} xpick_t;

static bool
pick(const jpx_t *j, const jp2_t *x, bool raw, xpick_t *pk) {
  xchan_t  *ch = pk->ch;
  uint32_t  nch = 0, ncol, space, *colour = pk->colour, k;
  int32_t   opacity = -1;
  bool      ycc = false, premult = false;

  /* the channels: through a palette where the JP2 maps one */
  if (!raw && x->nmap && x->pal) {
    for (k = 0; k < x->nmap; k++) {
      if (x->map[k].cmp >= j->ncomp || (x->map[k].palette && x->map[k].col >= x->pal_cols)) return false;
      ch[nch].comp = x->map[k].cmp;
      ch[nch].col  = x->map[k].palette ? x->map[k].col : -1;
      ch[nch].bits = x->map[k].palette ? x->pal_bits[x->map[k].col] : j->prec[x->map[k].cmp];
      nch++;
    }
  } else {
    for (k = 0; k < j->ncomp; k++) {
      ch[nch].comp = k;
      ch[nch].col  = -1;
      ch[nch].bits = j->prec[k];
      nch++;
    }
  }

  /* which are colour, which opacity; the indexes asked for: the first */
  if (raw) {
    space     = 1u;
    ncol      = 1u;
    colour[0] = 0u;
  } else {
    uint32_t found = 0;

    space = x->enumcs == 17u ? 1u : x->enumcs == 12u ? 4u
          : x->enumcs == 16u || x->enumcs == 18u || x->enumcs == 14u || x->enumcs == 24u ? 3u
          : x->icc == 0x47524159u ? 1u : x->icc == 0x434D594Bu ? 4u : x->icc == 0x52474220u ? 3u
          : nch <= 2u ? 1u : nch == 3u ? 3u : 4u;

    ncol = space;
    if (ncol > nch) ncol = nch >= 3u ? 3u : 1u;
    for (k = 0; k < ncol; k++) colour[k] = k;
    if (x->ndef) {
      /* by their associations: colour k+1 is channel ... */
      for (k = 0; k < x->ndef; k++) {
        uint32_t cn = x->def[k].cn;

        if (cn >= nch) continue;
        if (x->def[k].typ == 0u && x->def[k].asoc >= 1u && x->def[k].asoc <= ncol) {
          colour[x->def[k].asoc - 1u] = cn;
          found |= 1u << (x->def[k].asoc - 1u);
        } else if ((x->def[k].typ == 1u || x->def[k].typ == 2u) && opacity < 0) {
          opacity = (int32_t)cn;
          premult = x->def[k].typ == 2u;
        }
      }
      if (found != (1u << ncol) - 1u) {
        for (k = 0; k < ncol; k++) colour[k] = k;
      }
    } else if (nch > ncol) {
      opacity = (int32_t)ncol; /* the first one past the colours */
    }
    /* sYCC: said so, or three with the second subsampled (opj_decompress's guess) */
    ycc = ncol == 3u && (x->enumcs == 18u || (!x->colr && j->ncomp == 3u && j->dx[0] == j->dy[0] && j->dx[1] != 1u));
  }

  pk->nch     = nch;
  pk->ncol    = ncol;
  pk->space   = space;
  pk->opacity = opacity;
  pk->ycc     = ycc;
  pk->premult = premult;
  return true;
}

/* a sample of channel `c` as compose makes it 8 bits: through the
   palette, then the depth to 8 (OpenJPEG's way where asked), or an index */
static uint8_t
sample8(const jpx_t *j, const jp2_t *x, const xchan_t *c, const jxopts_t *o, int32_t s) {
  if (c->col >= 0) {
    if (j->sgnd[c->comp]) s -= 1 << (j->prec[c->comp] - 1u);
    s = s < 0 ? 0 : s >= (int32_t)x->pal_n ? (int32_t)x->pal_n - 1 : s;
    s = x->pal[(uint32_t)s * x->pal_cols + (uint32_t)c->col];
  }
  if (o->raw) {
    int32_t t = s < 0 ? 0 : s;

    return o->eight ? eight_shift(s, c->bits, true) : (uint8_t)(t > 255 ? 255 : t);
  }
  return o->eight ? eight_shift(s, c->bits, false) : eight(s, c->bits);
}

enum { X_PIECE = 1u << 15 }; /* pixels of a row a pool item lays */

/* 8-bit pixels laid where they end: the samples of `nuse` channels, each
   through its table (sample8's for each value its plane holds) */
typedef struct xlay {
  uint32_t        nuse, ncol, w, step, pieces;
  uint32_t        comp[5];
  const uint32_t *cols[5]; /* the plane's column for each X; NULL: X */
  const uint8_t  *lut[5];
  const uint8_t  *whole;   /* one component for all, full size: a pixel's
                              bytes by its sample (four a value) */
  uint8_t        *px, *alpha;
  size_t          pitch;
  bool            apart, pad;
  bool            ident;   /* every channel a full-size component's 8-bit
                              samples as they are: narrowed and interleaved */
  /* sYCC (the first three channels): sycc()'s products by the samples, the
     same values (G's two floats summed, then truncated, as there) */
  bool            ycc, yident; /* yident: 8-bit, its tables changing none */
  int32_t         upb, off;
  const int32_t  *rt, *bt;
  const float    *ga, *gb;
} xlay_t;

static bool
lay_piece(jpx_t *j, xscratch_t *s, void *ctx, uint32_t item) {
  const xlay_t *l = ctx;
  uint32_t      Y = item / l->pieces, X0 = (item % l->pieces) * X_PIECE, X1, X, u;
  uint8_t      *o = l->px + (size_t)Y * l->pitch + (size_t)X0 * l->step, *q;

  (void)s;
  X1 = l->w - X0 > X_PIECE ? X0 + X_PIECE : l->w;
  if (l->ident) {
    const uint16_t *src[4] = { NULL, NULL, NULL, NULL };

    for (u = 0; u < l->nuse; u++) {
      uint32_t cp = l->comp[u];
      int64_t  sy = (int64_t)(j->y0 + Y) / j->dy[cp] - (int64_t)j->py0[cp];

      sy     = sy < 0 ? 0 : sy >= j->ph[cp] ? (int64_t)j->ph[cp] - 1 : sy;
      src[u] = plane_row(j, cp, (size_t)sy);
    }
    X = X0;
    if (l->nuse == 1u) {
      for (; X < X1; X++) o[X - X0] = (uint8_t)src[0][X];
    } else if (l->nuse == 3u && l->step == 3u) {
#if X_NEON
      for (; X + 16u <= X1; X += 16u, o += 48) {
        uint8x16x3_t v;

        v.val[0] = vcombine_u8(vmovn_u16(vld1q_u16(src[0] + X)), vmovn_u16(vld1q_u16(src[0] + X + 8u)));
        v.val[1] = vcombine_u8(vmovn_u16(vld1q_u16(src[1] + X)), vmovn_u16(vld1q_u16(src[1] + X + 8u)));
        v.val[2] = vcombine_u8(vmovn_u16(vld1q_u16(src[2] + X)), vmovn_u16(vld1q_u16(src[2] + X + 8u)));
        vst3q_u8(o, v);
      }
#endif
      for (; X < X1; X++, o += 3) {
        o[0] = (uint8_t)src[0][X];
        o[1] = (uint8_t)src[1][X];
        o[2] = (uint8_t)src[2][X];
      }
    } else {
      /* four a pixel: RGBA, CMYK, or RGB padded with 0xFF */
      const uint16_t *a = l->nuse == 4u ? src[3] : NULL;

#if X_NEON
      for (; X + 16u <= X1; X += 16u, o += 64) {
        uint8x16x4_t v;

        v.val[0] = vcombine_u8(vmovn_u16(vld1q_u16(src[0] + X)), vmovn_u16(vld1q_u16(src[0] + X + 8u)));
        v.val[1] = vcombine_u8(vmovn_u16(vld1q_u16(src[1] + X)), vmovn_u16(vld1q_u16(src[1] + X + 8u)));
        v.val[2] = vcombine_u8(vmovn_u16(vld1q_u16(src[2] + X)), vmovn_u16(vld1q_u16(src[2] + X + 8u)));
        v.val[3] = a ? vcombine_u8(vmovn_u16(vld1q_u16(a + X)), vmovn_u16(vld1q_u16(a + X + 8u))) : vdupq_n_u8(0xFFu);
        vst4q_u8(o, v);
      }
#endif
      for (; X < X1; X++, o += 4) {
        o[0] = (uint8_t)src[0][X];
        o[1] = (uint8_t)src[1][X];
        o[2] = (uint8_t)src[2][X];
        o[3] = a ? (uint8_t)a[X] : 0xFFu;
      }
    }
    return true;
  }
  if (l->ycc) {
    /* the three to RGB (sycc), each to its 8 bits by its table */
    const uint16_t *src[3];
    const uint32_t *cl[3];

    for (u = 0; u < 3u; u++) {
      uint32_t cp = l->comp[u];
      int64_t  sy = (int64_t)(j->y0 + Y) / j->dy[cp] - (int64_t)j->py0[cp];

      sy     = sy < 0 ? 0 : sy >= j->ph[cp] ? (int64_t)j->ph[cp] - 1 : sy;
      src[u] = plane_row(j, cp, (size_t)sy);
      cl[u]  = l->cols[u];
    }
    X = X0;
    q = o;
#if X_NEON
    /* full size, 8 bits as they are: eight a time, sycc()'s floats in the
       same operations (nothing fused; truncated toward zero, as its casts) */
    if (l->yident && !cl[0] && !cl[1] && !cl[2] && (l->step == 3u || l->step == 4u)) {
      const int32x4_t off = vdupq_n_s32(l->off), top = vdupq_n_s32(l->upb), zero = vdupq_n_s32(0);

      for (; X + 8u <= X1; X += 8u, q += 8u * l->step) {
        uint16x8_t Yv = vld1q_u16(src[0] + X), Cb = vld1q_u16(src[1] + X), Cr = vld1q_u16(src[2] + X);
        uint16x8_t rgb[3];
        uint32_t   hlf;

        for (hlf = 0; hlf < 2u; hlf++) {
          uint16x4_t  y4  = hlf ? vget_high_u16(Yv) : vget_low_u16(Yv);
          uint16x4_t  cb4 = hlf ? vget_high_u16(Cb) : vget_low_u16(Cb);
          uint16x4_t  cr4 = hlf ? vget_high_u16(Cr) : vget_low_u16(Cr);
          int32x4_t   y   = vreinterpretq_s32_u32(vmovl_u16(y4));
          float32x4_t fb  = vcvtq_f32_s32(vsubq_s32(vreinterpretq_s32_u32(vmovl_u16(cb4)), off));
          float32x4_t fr  = vcvtq_f32_s32(vsubq_s32(vreinterpretq_s32_u32(vmovl_u16(cr4)), off));
          int32x4_t   r   = vaddq_s32(y, vcvtq_s32_f32(vmulq_n_f32(fr, 1.402f)));
          int32x4_t   g   = vsubq_s32(y, vcvtq_s32_f32(vaddq_f32(vmulq_n_f32(fb, 0.344f), vmulq_n_f32(fr, 0.714f))));
          int32x4_t   b   = vaddq_s32(y, vcvtq_s32_f32(vmulq_n_f32(fb, 1.772f)));
          uint16x4_t  r4  = vmovn_u32(vreinterpretq_u32_s32(vminq_s32(vmaxq_s32(r, zero), top)));
          uint16x4_t  g4  = vmovn_u32(vreinterpretq_u32_s32(vminq_s32(vmaxq_s32(g, zero), top)));
          uint16x4_t  b4  = vmovn_u32(vreinterpretq_u32_s32(vminq_s32(vmaxq_s32(b, zero), top)));

          if (!hlf) {
            rgb[0] = vcombine_u16(r4, r4);
            rgb[1] = vcombine_u16(g4, g4);
            rgb[2] = vcombine_u16(b4, b4);
          } else {
            rgb[0] = vcombine_u16(vget_low_u16(rgb[0]), r4);
            rgb[1] = vcombine_u16(vget_low_u16(rgb[1]), g4);
            rgb[2] = vcombine_u16(vget_low_u16(rgb[2]), b4);
          }
        }
        if (l->step == 3u) {
          uint8x8x3_t v = { { vmovn_u16(rgb[0]), vmovn_u16(rgb[1]), vmovn_u16(rgb[2]) } };

          vst3_u8(q, v);
        } else {
          uint8x8x4_t v = { { vmovn_u16(rgb[0]), vmovn_u16(rgb[1]), vmovn_u16(rgb[2]), vdup_n_u8(0xFFu) } };

          vst4_u8(q, v); /* the 4th: a pad's, or the opacity's after */
        }
      }
    }
#endif
    for (; X < X1; X++, q += l->step) {
      int32_t yy = src[0][cl[0] ? cl[0][X] : X], cb = src[1][cl[1] ? cl[1][X] : X], cr = src[2][cl[2] ? cl[2][X] : X];
      int32_t r = yy + l->rt[cr], g = yy - (int32_t)(l->ga[cb] + l->gb[cr]), b = yy + l->bt[cb];

      r    = r < 0 ? 0 : r > l->upb ? l->upb : r;
      g    = g < 0 ? 0 : g > l->upb ? l->upb : g;
      b    = b < 0 ? 0 : b > l->upb ? l->upb : b;
      q[0] = l->lut[0][r];
      q[1] = l->lut[1][g];
      q[2] = l->lut[2][b];
    }
  }
  if (l->whole) {
    uint32_t        cp = l->comp[0];
    int64_t         sy = (int64_t)(j->y0 + Y) - (int64_t)j->py0[cp];
    const uint16_t *src;

    sy  = sy < 0 ? 0 : sy >= j->ph[cp] ? (int64_t)j->ph[cp] - 1 : sy;
    src = plane_row(j, cp, (size_t)sy);
    if (l->step == 4u) {
      for (X = X0; X < X1; X++, o += 4) memcpy(o, l->whole + (size_t)src[X] * 4u, 4);
    } else {
      for (X = X0; X < X1; X++, o += l->step) memcpy(o, l->whole + (size_t)src[X] * 4u, l->step);
    }
    return true;
  }
  for (u = l->ycc ? 3u : 0u; u < l->nuse; u++) {
    uint32_t        cp = l->comp[u];
    int64_t         sy = (int64_t)(j->y0 + Y) / j->dy[cp] - (int64_t)j->py0[cp];
    const uint16_t *src;
    const uint8_t  *lut = l->lut[u];
    const uint32_t *cl  = l->cols[u];
    size_t          st  = l->step;

    sy  = sy < 0 ? 0 : sy >= j->ph[cp] ? (int64_t)j->ph[cp] - 1 : sy;
    src = plane_row(j, cp, (size_t)sy);
    q   = o + u;
    if (l->apart && u == l->ncol) {
      q  = l->alpha + (size_t)Y * l->w + X0;
      st = 1u;
    }
    if (cl) {
      for (X = X0; X < X1; X++, q += st) *q = lut[src[cl[X]]];
    } else if (st == 1u) {
      for (X = X0; X < X1; X++) q[X - X0] = lut[src[X]];
    } else {
      for (X = X0; X < X1; X++, q += st) *q = lut[src[X]];
    }
  }
  if (l->pad) {
    for (X = X0, q = o + 3; X < X1; X++, q += 4) *q = 0xFFu;
  }
  return true;
}

/* compose's 8-bit pixels (no sYCC, no premultiplied opacity apart) the
   quick way: the tables, then the pieces of rows on the pool, straight into
   RGB0, the opacity's own plane, the caller's memory. False with `*px`
   NULL: no memory; `*px` NULL and true: laid in the caller's */
static bool
compose8(jpx_t *j, const jp2_t *x, const jxopts_t *o, const xchan_t *ch, const uint32_t *use, uint32_t nuse,
         uint32_t ncol, bool apart, bool ycc, uint8_t **pxp, void **alphap, uint32_t *stepp, size_t *lenp, uint32_t *padp) {
  xlay_t    l;
  uint32_t  w = j->x1 - j->x0, h = j->y1 - j->y0, u, X, total = 0;
  uint32_t *cols = NULL;
  uint8_t  *luts, *px;
  int32_t  *yt = NULL;
  size_t    n = (size_t)w * h, row;
  bool      ok;

  memset(&l, 0, sizeof(l));
  /* sYCC: the products by each sample value (the three at one depth) */
  if (ycc) {
    uint32_t prec = j->prec[ch[use[0]].comp], nv = 1u << prec, v;
    int32_t  off = 1 << (prec - 1u);

    if (!(yt = malloc((size_t)nv * 4u * sizeof(*yt)))) return false;
    l.ycc = true;
    l.upb = (1 << prec) - 1;
    l.off = off;
    l.rt  = yt;
    l.bt  = yt + nv;
    l.ga  = (const float *)(void *)(yt + 2u * nv);
    l.gb  = (const float *)(void *)(yt + 3u * nv);
    for (v = 0; v < nv; v++) {
      int32_t c = (int32_t)v - off;

      yt[v]                            = (int32_t)(1.402f * (float)c);
      yt[nv + v]                       = (int32_t)(1.772f * (float)c);
      ((float *)(void *)(yt + 2u * nv))[v] = 0.344f * (float)c;
      ((float *)(void *)(yt + 3u * nv))[v] = 0.714f * (float)c;
    }
  }
  l.nuse  = nuse;
  l.ncol  = ncol;
  l.w     = w;
  l.apart = apart;
  l.step  = apart ? ncol : nuse;
  l.pad   = o->pad && l.step == 3u;
  l.step  = l.pad ? 4u : l.step;
  row     = (size_t)w * l.step;
  for (u = 0; u < nuse; u++) total += 1u << j->prec[ch[use[u]].comp];
  if (!(luts = malloc(total + ((size_t)4u << j->prec[ch[use[0]].comp])))) {
    free(yt);
    return false;
  }
  total = 0;
  for (u = 0; u < nuse; u++) {
    const xchan_t *c = &ch[use[u]];
    uint32_t       cp = c->comp, v;
    uint8_t       *lut = luts + total;

    for (v = 0; v < 1u << j->prec[cp]; v++) lut[v] = sample8(j, x, c, o, (int32_t)v);
    total     += 1u << j->prec[cp];
    l.lut[u]  = lut;
    l.comp[u] = cp;
    /* the plane's columns: X itself where the component is full size */
    if (j->dx[cp] != 1u || j->px0[cp] != j->x0 || j->pw[cp] < w) {
      if (!cols && !(cols = malloc((size_t)w * sizeof(*cols) * 5u))) {
        free(luts);
        free(yt);
        return false;
      }
      for (X = 0; X < w; X++) {
        int64_t sx = (int64_t)(j->x0 + X) / j->dx[cp] - (int64_t)j->px0[cp];

        cols[u * w + X] = (uint32_t)(sx < 0 ? 0 : sx >= j->pw[cp] ? (int64_t)j->pw[cp] - 1 : sx);
      }
      l.cols[u] = cols + (size_t)u * w;
    }
  }
  /* sYCC's 8 bits as they are (its three tables changing none) */
  if (ycc && j->prec[l.comp[0]] == 8u) {
    uint32_t v;

    l.yident = true;
    for (u = 0; u < 3u; u++) {
      for (v = 0; l.yident && v < 256u; v++) l.yident = l.lut[u][v] == v;
    }
  }
  /* every channel a full-size component's 8-bit samples as they are (a
     table that changes none): narrowed and interleaved */
  if (!ycc && !apart && !cols && (nuse == 1u || nuse == 3u || nuse == 4u) && (l.step == nuse || (l.pad && nuse == 3u))) {
    l.ident = true;
    for (u = 0; u < nuse && l.ident; u++) {
      uint32_t cp = l.comp[u], v;

      if (j->prec[cp] > 8u) l.ident = false;
      for (v = 0; l.ident && v < 1u << j->prec[cp]; v++) l.ident = l.lut[u][v] == v;
    }
  }
  /* every channel one full-size component's (a palette's colours, gray):
     each value's pixel whole, four bytes a value */
  if (!ycc && !l.ident && !apart && l.step > 1u && !cols && j->dy[l.comp[0]] == 1u) {
    uint32_t v, nv = 1u << j->prec[l.comp[0]];
    uint8_t *whole = luts + total;

    for (u = 1; u < nuse && l.comp[u] == l.comp[0]; u++) {
    }
    if (u == nuse) {
      for (v = 0; v < nv; v++) {
        for (u = 0; u < 4u; u++) whole[v * 4u + u] = u < nuse ? l.lut[u][v] : 0xFFu;
      }
      l.whole = whole;
    }
  }
  if (o->dest) {
    if (o->dest_stride < row || o->dest_size < o->dest_stride * (h - 1u) + row) {
      free(luts);
      free(cols);
      free(yt);
      return false;
    }
    px      = o->dest;
    l.pitch = o->dest_stride;
  } else {
    px      = malloc(n * l.step);
    l.pitch = row;
  }
  if (!px || (apart && !(l.alpha = malloc(n)))) {
    if (px != o->dest) free(px);
    free(luts);
    free(cols);
    free(yt);
    return false;
  }
  l.px     = px;
  l.pieces = (w + X_PIECE - 1u) / X_PIECE;
  ok       = compose_run(j, lay_piece, &l, l.pieces, h);
  free(luts);
  free(cols);
  free(yt);
  if (!ok) {
    if (px != o->dest) free(px);
    free(l.alpha);
    return false;
  }
  *pxp    = px == o->dest ? NULL : px;
  *alphap = l.alpha;
  *stepp  = l.step;
  *lenp   = o->dest ? o->dest_stride * (h - 1u) + row : n * l.step;
  *padp   = o->dest ? (uint32_t)(o->dest_stride - row) : 0u;
  return true;
}

/* compose's general way, a piece of a row at a time on the pool: each
   pixel's channels (a palette's colours, sYCC to RGB, to 8 or 16 bits as
   asked), laid where they end (the opacity apart and un-premultiplied
   where it's asked apart, RGB padded, 16-bit samples in the order asked,
   the caller's memory) */
typedef struct xgen {
  const jp2_t    *x;
  const xchan_t  *ch;
  const uint32_t *use, *cols; /* cols: a channel's plane column for each X */
  uint32_t        nuse, ncol, w, pieces, fstep, ybits;
  bool            ycc, raw, eight, wide, apart, premult, pad, swap;
  uint8_t        *px, *alpha;
  size_t          pitch;
  /* where the image pays for them: each channel's value as it's laid, by
     its sample (through the palette), or by sYCC's R, G or B; sycc()'s
     products by Cb and Cr */
  const uint16_t *lut[5];
  const int32_t  *rt, *bt;
  const float    *ga, *gb;
  bool            atx[5]; /* a channel's plane column is X */
} xgen_t;

/* a channel's value made 8 or 16 bits, as compose lays it */
static inline uint32_t
gen_depth(const xgen_t *g, int32_t v, uint32_t bits) {
  if (g->raw) {
    /* indexes (and an opacity with them): as they are, as compose8 */
    int32_t t = v < 0 ? 0 : v;

    return g->eight ? eight_shift(v, bits, true) : g->wide ? (uint32_t)(t > 65535 ? 65535 : t) : (uint32_t)(t > 255 ? 255 : t);
  }
  if (g->wide) return sixteen(v, bits);
  return g->eight ? eight_shift(v, bits, false) : eight(v, bits);
}

/* a channel's sample through the palette, where it has one */
static inline int32_t
gen_pal(const jpx_t *j, const jp2_t *x, const xchan_t *ch, int32_t sm) {
  if (ch->col >= 0) {
    if (j->sgnd[ch->comp]) sm -= 1 << (j->prec[ch->comp] - 1u);
    sm = sm < 0 ? 0 : sm >= (int32_t)x->pal_n ? (int32_t)x->pal_n - 1 : sm;
    sm = x->pal[(uint32_t)sm * x->pal_cols + (uint32_t)ch->col];
  }
  return sm;
}

/* a piece of a row by the tables, the opacity with the colours: all of it
   held here (the stores could be anything's to the compiler); 16-bit
   values come out of their tables in the order asked */
static void
gen_lut(const xgen_t *g, const uint16_t *const *row, uint32_t X0, uint32_t X1, uint8_t *o) {
  const bool      wide = g->wide, ycc = g->ycc;
  const uint32_t  nuse = g->nuse, fstep = g->fstep, top = wide ? 0xFFFFu : 0xFFu, u0 = ycc ? 3u : 0u;
  const int32_t   upb = (1 << g->ybits) - 1, *rt = g->rt, *bt = g->bt;
  const float    *ga = g->ga, *gb = g->gb;
  const uint16_t *l[5], *r[5];
  const uint32_t *cl[5];
  uint32_t        X, u;

  for (u = 0; u < nuse; u++) {
    l[u]  = g->lut[u];
    r[u]  = row[u];
    cl[u] = g->atx[u] ? NULL : g->cols + (size_t)u * g->w;
  }
  for (X = X0; X < X1; X++) {
    uint32_t c[5];

    if (ycc) {
      int32_t yy = cl[0] ? r[0][cl[0][X]] : r[0][X], cb = cl[1] ? r[1][cl[1][X]] : r[1][X], cr = cl[2] ? r[2][cl[2][X]] : r[2][X];
      int32_t R = yy + rt[cr], G = yy - (int32_t)(ga[cb] + gb[cr]), B = yy + bt[cb];

      c[0] = l[0][R < 0 ? 0 : R > upb ? upb : R];
      c[1] = l[1][G < 0 ? 0 : G > upb ? upb : G];
      c[2] = l[2][B < 0 ? 0 : B > upb ? upb : B];
    }
    for (u = u0; u < nuse; u++) c[u] = l[u][cl[u] ? r[u][cl[u][X]] : r[u][X]];
    if (fstep > nuse) c[3] = top;
    if (wide) {
      uint16_t *p = (uint16_t *)(void *)o;

      for (u = 0; u < fstep; u++) p[u] = (uint16_t)c[u];
      o += 2u * fstep;
    } else {
      for (u = 0; u < fstep; u++) o[u] = (uint8_t)c[u];
      o += fstep;
    }
  }
}

static bool
gen_piece(jpx_t *j, xscratch_t *s, void *ctx, uint32_t item) {
  const xgen_t   *g = ctx;
  const jp2_t    *x = g->x;
  uint32_t        Y = item / g->pieces, X0 = (item % g->pieces) * X_PIECE, X1, X, u;
  uint32_t        es = g->wide ? 2u : 1u, top = g->wide ? 65535u : 255u, nout = g->apart ? g->ncol : g->nuse;
  bool            pad = g->pad && nout == 3u;
  const uint16_t *row[5];

  (void)s;
  X1 = g->w - X0 > X_PIECE ? X0 + X_PIECE : g->w;
  for (u = 0; u < g->nuse; u++) {
    uint32_t cp = g->ch[g->use[u]].comp;
    int64_t  sy = (int64_t)(j->y0 + Y) / j->dy[cp] - (int64_t)j->py0[cp];

    sy     = sy < 0 ? 0 : sy >= j->ph[cp] ? (int64_t)j->ph[cp] - 1 : sy;
    row[u] = plane_row(j, cp, (size_t)sy);
  }
  if (g->lut[0] && !g->apart) {
    gen_lut(g, row, X0, X1, g->px + (size_t)Y * g->pitch + (size_t)X0 * g->fstep * es);
    return true;
  }
  for (X = X0; X < X1; X++) {
    uint32_t c[5];
    uint8_t *at = g->px + (size_t)Y * g->pitch + (size_t)X * g->fstep * es;

    if (g->lut[0]) {
      /* by the tables */
      u = 0;
      if (g->ycc) {
        int32_t yy = row[0][g->cols[X]], cb = row[1][g->cols[g->w + X]], cr = row[2][g->cols[2u * g->w + X]];
        int32_t r = yy + g->rt[cr], gg = yy - (int32_t)(g->ga[cb] + g->gb[cr]), b = yy + g->bt[cb], upb = (1 << g->ybits) - 1;

        c[0] = g->lut[0][r < 0 ? 0 : r > upb ? upb : r];
        c[1] = g->lut[1][gg < 0 ? 0 : gg > upb ? upb : gg];
        c[2] = g->lut[2][b < 0 ? 0 : b > upb ? upb : b];
        u    = 3u;
      }
      for (; u < g->nuse; u++) c[u] = g->lut[u][row[u][g->cols[(size_t)u * g->w + X]]];
    } else {
      int32_t v[5];

      for (u = 0; u < g->nuse; u++) v[u] = gen_pal(j, x, &g->ch[g->use[u]], row[u][g->cols[(size_t)u * g->w + X]]);
      if (g->ycc) sycc(1 << (g->ybits - 1u), (1 << g->ybits) - 1, v[0], v[1], v[2], &v[0], &v[1], &v[2]);
      for (u = 0; u < g->nuse; u++) c[u] = gen_depth(g, v[u], g->ch[g->use[u]].bits);
    }
    if (g->apart) {
      uint32_t al = c[g->ncol];

      for (u = 0; g->premult && u < g->ncol; u++) {
        uint64_t cc = c[u];

        c[u] = al ? (uint32_t)(cc > al ? top : (cc * top + al / 2u) / al) : 0u;
      }
      if (g->wide) ((uint16_t *)(void *)g->alpha)[(size_t)Y * g->w + X] = (uint16_t)(g->swap ? bswapu16((uint16_t)al) : al);
      else g->alpha[(size_t)Y * g->w + X] = (uint8_t)al;
    }
    if (pad) c[3] = top;
    for (u = 0; u < g->fstep; u++) {
      if (g->wide) ((uint16_t *)(void *)at)[u] = g->swap ? bswapu16((uint16_t)c[u]) : (uint16_t)c[u];
      else at[u] = (uint8_t)c[u];
    }
  }
  return true;
}

/* the planes as `im`'s pixels: the colours then the opacity, a sample 8
   bits or (any channel deeper) 16 in `order` */
static bool
compose(jpx_t *j, const jp2_t *x, ImImage *im, const jxopts_t *o) {
  ImByteOrder order = o->order == IM_BYTEORDER_BIG || o->order == IM_BYTEORDER_LITTLE ? o->order : IM_BYTEORDER_HOST;
  xchan_t   ch[X_COMPS];
  uint32_t  ncol, space, colour[4], use[5], nuse, k, u, w = j->x1 - j->x0, h = j->y1 - j->y0, X, step, rowpad = 0;
  int32_t   opacity = -1;
  bool      ycc = false, premult = false, wide = false;
  uint32_t *cols;
  uint8_t  *px, *icc = NULL;
  void     *alpha = NULL;
  size_t    n = (size_t)w * h, len;

  {
    xpick_t pk;

    if (!pick(j, x, o->raw, &pk)) return false;
    memcpy(ch, pk.ch, sizeof(ch));
    ncol    = pk.ncol;
    space   = pk.space;
    opacity = pk.opacity;
    ycc     = pk.ycc;
    premult = pk.premult;
    for (k = 0; k < 4u; k++) colour[k] = pk.colour[k];
  }

  /* not asked for: not there; CMYK, interleaved, goes without (im has no
     format for the two), apart keeps it */
  if (o->opacity == IM_OPACITY_NONE || (ncol == 4u && o->opacity != IM_OPACITY_APART)) opacity = -1;
  nuse = ncol;
  for (u = 0; u < ncol; u++) use[u] = colour[u];
  if (opacity >= 0) use[nuse++] = (uint32_t)opacity;
  for (u = 0; u < nuse; u++) wide = wide || (ch[use[u]].bits > 8u && !o->eight);

  /* 8 bits, no premultiplied opacity apart: the quick way, laid where it
     ends (small deep images, whose tables are more than their samples,
     compose's own); sYCC too where its three are one depth's and no
     palette's */
  {
    bool quick = ycc ? ncol == 3u && nuse >= 3u : true;

    for (u = 0; quick && ycc && u < 3u; u++) {
      quick = ch[use[u]].col < 0 && j->prec[ch[use[u]].comp] == j->prec[ch[use[0]].comp] && ch[use[u]].bits == j->prec[ch[use[0]].comp];
    }
    if (!quick) goto general;
  }
  if (!wide && !(premult && opacity >= 0 && o->opacity == IM_OPACITY_APART)) {
    size_t lut = 0;

    for (u = 0; u < nuse; u++) lut += (size_t)1 << j->prec[ch[use[u]].comp];
    if (lut <= 1024u || n >= lut * 4u) {
      bool apart = opacity >= 0 && o->opacity == IM_OPACITY_APART;

      px = NULL;
      if (!compose8(j, x, o, ch, use, nuse, ncol, apart, ycc, &px, &alpha, &step, &len, &rowpad)) return false;
      if (apart) opacity = -1;
      goto made;
    }
  }

general:
  {
    xgen_t g;
    bool   apart = opacity >= 0 && o->opacity == IM_OPACITY_APART, ok;
    size_t es = wide ? 2u : 1u, row;
    void  *tabs = NULL;

    memset(&g, 0, sizeof(g));
    g.x       = x;
    g.ch      = ch;
    g.use     = use;
    g.nuse    = nuse;
    g.ncol    = ncol;
    g.w       = w;
    g.pieces  = (w + X_PIECE - 1u) / X_PIECE;
    g.ycc     = ycc;
    g.ybits   = ch[colour[0]].bits;
    g.raw     = o->raw;
    g.eight   = o->eight;
    g.wide    = wide;
    g.apart   = apart;
    g.premult = premult;
    g.fstep   = apart ? ncol : nuse;
    g.pad     = o->pad && g.fstep == 3u;
    g.fstep   = g.pad ? 4u : g.fstep;
    g.swap    = wide && order != IM_BYTEORDER_HOST && (order == IM_BYTEORDER_BIG) != X_HOST_BIG;
    row       = (size_t)w * g.fstep * es;
    /* each channel's plane column for each X */
    if (!(cols = malloc((size_t)w * sizeof(*cols) * 5u))) return false;
    for (u = 0; u < nuse; u++) {
      uint32_t cp = ch[use[u]].comp, dx = j->dx[cp];

      for (X = 0; X < w; X++) {
        int64_t sx = (int64_t)(j->x0 + X) / dx - (int64_t)j->px0[cp];

        cols[u * w + X] = (uint32_t)(sx < 0 ? 0 : sx >= j->pw[cp] ? (int64_t)j->pw[cp] - 1 : sx);
      }
      /* the column X all along: none to look up */
      for (X = 0; X < w && cols[u * w + X] == X; X++) {}
      g.atx[u] = X == w;
    }
    g.cols = cols;
    /* the tables, where there are more pixels than their entries (not
       for sYCC from a palette) */
    {
      size_t   T = 0, nb = 0, nr = 0;
      bool     can = true;
      uint32_t v;

      for (u = 0; u < nuse; u++) {
        can = can && !(ycc && u < 3u && ch[use[u]].col >= 0);
        T  += ycc && u < 3u ? (size_t)1 << g.ybits : (size_t)1 << j->prec[ch[use[u]].comp];
      }
      if (ycc) {
        nb = (size_t)1 << j->prec[ch[use[1]].comp];
        nr = (size_t)1 << j->prec[ch[use[2]].comp];
      }
      if (can && n >= T + nb + nr && (tabs = malloc(T * sizeof(uint16_t) + (nb + nr) * 2u * sizeof(int32_t)))) {
        int32_t  *it  = tabs, off = 1 << (g.ybits - 1u);
        uint16_t *lt;

        if (ycc) {
          float *ft = (float *)(void *)(it + nb + nr);

          g.rt = it;
          g.bt = it + nr;
          g.ga = ft;
          g.gb = ft + nb;
          for (v = 0; v < nr; v++) {
            it[v]           = (int32_t)(1.402f * (float)((int32_t)v - off));
            ft[nb + v]      = 0.714f * (float)((int32_t)v - off);
          }
          for (v = 0; v < nb; v++) {
            it[nr + v] = (int32_t)(1.772f * (float)((int32_t)v - off));
            ft[v]      = 0.344f * (float)((int32_t)v - off);
          }
        }
        lt = (uint16_t *)(void *)(it + 2u * (nb + nr));
        for (u = 0; u < nuse; u++) {
          const xchan_t *c  = &ch[use[u]];
          uint32_t       nv = ycc && u < 3u ? 1u << g.ybits : 1u << j->prec[c->comp];

          for (v = 0; v < nv; v++) {
            lt[v] = (uint16_t)gen_depth(&g, ycc && u < 3u ? (int32_t)v : gen_pal(j, x, c, (int32_t)v), c->bits);
            if (g.swap && !apart) lt[v] = bswapu16(lt[v]);
          }
          g.lut[u] = lt;
          lt      += nv;
        }
      }
    }
    if (o->dest) {
      if (o->dest_stride < row || o->dest_size < o->dest_stride * (h - 1u) + row
          || (wide && (((uintptr_t)o->dest | o->dest_stride) & 1u))) {
        free(cols);
        free(tabs);
        return false;
      }
      px      = o->dest;
      g.pitch = o->dest_stride;
    } else if (!(px = malloc(n * g.fstep * es))) {
      free(cols);
      free(tabs);
      return false;
    } else {
      g.pitch = row;
    }
    g.px = px;
    if (apart && !(g.alpha = malloc(n * es))) {
      if (px != o->dest) free(px);
      free(cols);
      free(tabs);
      return false;
    }
    ok = compose_run(j, gen_piece, &g, g.pieces, h);
    free(cols);
    free(tabs);
    if (!ok) {
      if (px != o->dest) free(px);
      free(g.alpha);
      return false;
    }
    alpha  = g.alpha;
    step   = g.fstep;
    len    = o->dest ? o->dest_stride * (h - 1u) + row : n * g.fstep * es;
    rowpad = o->dest ? (uint32_t)(o->dest_stride - row) : 0u;
    if (px == o->dest) px = NULL;
    if (apart) opacity = -1;
  }

made:
  /* the colours' profile, where the JP2 gives one for them (not sYCC's,
     nor for indexes) */
  if (!o->raw && !ycc && x->icc_data && x->icc_len) {
    if (!(icc = malloc(x->icc_len))) {
      free(px);
      free(alpha);
      return false;
    }
    memcpy(icc, x->icc_data, x->icc_len);
  }

  /* a row's padding where it's laid in the caller's memory, at its stride */
  im->row_pad_last       = rowpad;
  im->width              = w;
  im->height             = h;
  im->data.data          = px;
  im->len                = len;
  im->componentsPerPixel = step;
  im->bitsPerComponent   = wide ? 16u : 8u;
  im->bitsPerPixel       = step * im->bitsPerComponent;
  im->bytesPerPixel      = im->bitsPerPixel / 8u;
  im->byteOrder          = order;
  im->alphaInfo          = opacity >= 0 ? (premult ? IM_ALPHA_PREMUL_LAST : IM_ALPHA_LAST)
                         : ncol == 3u && step == 4u ? IM_ALPHA_NONE_SKIP_LAST : IM_ALPHA_NONE;
  im->format             = ncol == 1u ? (opacity < 0 ? IM_FORMAT_GRAY : IM_FORMAT_GRAY_ALPHA)
                         : ncol == 3u ? (opacity >= 0 ? IM_FORMAT_RGBA : step == 4u ? IM_FORMAT_RGB0 : IM_FORMAT_RGB)
                         : IM_FORMAT_CMYK;
  im->iccProfile         = icc;
  im->iccProfileSize     = icc ? x->icc_len : 0u;
  im->alpha              = alpha;

  /* the colour space as colr says, where it fits the colours (a profile
     may say more); by the colours where there's no colr or it doesn't */
  if (o->raw)                             im->colorSpace = IM_COLORSPACE_UNKNOWN; /* indexes */
  else if (!x->colr || space != ncol)     im->colorSpace = ncol == 1u ? IM_COLORSPACE_GRAY
                                                         : ncol == 3u ? IM_COLORSPACE_sRGB
                                                         : IM_COLORSPACE_CMYK;
  else if (ycc || x->enumcs == 16u)       im->colorSpace = IM_COLORSPACE_sRGB;
  else if (x->enumcs == 17u)              im->colorSpace = IM_COLORSPACE_GRAY;
  else if (x->enumcs == 12u)              im->colorSpace = IM_COLORSPACE_CMYK;
  else if (icc && x->icc == 0x47524159u)  im->colorSpace = IM_COLORSPACE_GRAY;
  else if (icc && x->icc == 0x434D594Bu)  im->colorSpace = IM_COLORSPACE_CMYK;
  else if (icc && x->icc == 0x52474220u)  im->colorSpace = IM_COLORSPACE_sRGB;
  else                                    im->colorSpace = IM_COLORSPACE_UNKNOWN;
  return true;
}

/* `data`, a JP2 or a bare codestream, into `im` */
static bool
jpx_decode(const uint8_t *data, size_t len, ImImage *im, const jxopts_t *o) {
  jpx_t         *j;
  jp2_t          x;
  const uint8_t *cs     = NULL;
  size_t         cs_len = 0;
  uint32_t       c, tile;
  bool           ok = false, bands;

  if (!data || len < 12u) return false;
  memset(&x, 0, sizeof(x));
  if (!(j = calloc(1, sizeof(*j)))) return false;
  j->arena.limit = X_ARENA;
  j->threads     = o->threads;
  j->runner      = o->runner;

  if (be32(data) == 12u && be32(data + 4) == 0x6A502020u) {
    if (!read_boxes(j, &x, data, len, &cs, &cs_len, 0u) || !cs) goto done;
    im->fileFormatType = IM_FILEFORMATTYPE_JP2;
  } else {
    cs     = data;
    cs_len = len;
    im->fileFormatType = IM_FILEFORMATTYPE_J2K;
  }
  if (!read_codestream(j, cs, cs_len)) goto done;
  /* the grid as SIZ has it: the tiles' (compose's made smaller below) */
  j->ox0 = j->x0;
  j->oy0 = j->y0;
  j->ox1 = j->x1;
  j->oy1 = j->y1;

  /* made smaller: a level left out for each halving asked, no more than
     every component has (a tile's own fewer: refused, as OpenJPEG); none
     where a palette maps a component (a coarser level's samples aren't
     indexes: its own size, indexes or colours) */
  j->reduce = o->scale == 8u ? 3u : o->scale == 4u ? 2u : o->scale == 2u ? 1u : 0u;
  if (x.pal && x.nmap) j->reduce = 0u;
  for (c = 0; c < j->ncomp; c++) {
    if (j->cod.cc[c].levels < j->reduce) j->reduce = j->cod.cc[c].levels;
  }
  {
    uint64_t all = 0u, tile = 0u;

    for (c = 0; c < j->ncomp; c++) {
      j->px0[c] = (uint32_t)ceil_pow2(ceil_div_u(j->x0, j->dx[c]), j->reduce);
      j->py0[c] = (uint32_t)ceil_pow2(ceil_div_u(j->y0, j->dy[c]), j->reduce);
      j->pw[c]  = (uint32_t)ceil_pow2(ceil_div_u(j->x1, j->dx[c]), j->reduce) - j->px0[c];
      j->ph[c]  = (uint32_t)ceil_pow2(ceil_div_u(j->y1, j->dy[c]), j->reduce) - j->py0[c];
      all      += (uint64_t)j->pw[c] * j->ph[c] * sizeof(uint16_t);
      tile     += ((uint64_t)j->tw / j->dx[c] + 1u) * ((uint64_t)j->th / j->dy[c] + 1u) * 4u;
    }
    /* the planes and a tile's samples: past X_BIG, the image a big one:
       one of one tile made by bands, its planes rings (stream_open); one of
       rows of tiles a row of tiles at a time, its planes rings a row of
       tiles tall; else its planes mapped, given back as composed */
    j->big  = all + tile >= X_BIG;
    bands   = j->big && j->ntx == 1u && j->nty == 1u;
    j->rows = j->big && !bands && j->nty > 1u;
    j->pmap = j->big && !bands && !j->rows;
    for (c = 0; c < j->ncomp; c++) {
      j->pmask[c] = SIZE_MAX;
      if (!j->pw[c] || !j->ph[c]) goto done;
      if (bands) continue;
      j->psize[c] = plane_bytes(j, c);
      if (j->rows) {
        uint32_t cap = pow2_at_least((uint64_t)((j->th + j->dy[c] - 1u) / j->dy[c] >> j->reduce) + 3u + 8u);

        if (cap < j->ph[c]) {
          j->pmask[c] = cap - 1u;
          j->psize[c] = (size_t)cap * j->pw[c] * sizeof(uint16_t);
        }
      }
      if (!(j->plane[c] = im_big_alloc(j->psize[c], true, j->pmap))) goto done;
    }
  }
  /* no opacity wanted: its component (no colour's, past the colour
     transform's three) not decoded */
  if (o->opacity == IM_OPACITY_NONE && !o->raw) {
    xpick_t pk;

    if (pick(j, &x, false, &pk) && pk.opacity >= 0 && pk.ch[pk.opacity].col < 0) {
      uint32_t comp = pk.ch[pk.opacity].comp, k;
      bool     colour = comp < 3u;

      for (k = 0; k < pk.ncol && !colour; k++) colour = pk.ch[pk.colour[k]].comp == comp;
      if (!colour) j->skip[comp] = true;
    }
  }
  t1_tables(j);
  pool_start(j);
  if (bands) {
    if (!stream_open(j)) goto done;
  } else if (!j->rows) {
    for (tile = 0; tile < j->ntx * j->nty; tile++) {
      if (!tile_decode(j, tile)) goto done;
    }
  }
  /* the image's grid made smaller with it, for compose */
  j->x0 = (uint32_t)ceil_pow2(j->x0, j->reduce);
  j->y0 = (uint32_t)ceil_pow2(j->y0, j->reduce);
  j->x1 = (uint32_t)ceil_pow2(j->x1, j->reduce);
  j->y1 = (uint32_t)ceil_pow2(j->y1, j->reduce);
  ok = compose(j, &x, im, o);

done:
  stream_close(j);
  pool_stop(j);
  for (c = 0; c < X_COMPS; c++) im_big_free(j->plane[c], j->psize[c], j->pmap);
  for (c = 0; c < X_THREADS; c++) {
    free(j->scratch[c].flags);
    free(j->scratch[c].cdata);
    free(j->scratch[c].bytes);
    free(j->scratch[c].lift);
    free(j->scratch[c].ht);
  }
  arena_free_all(&j->arena);
  free(j);
  return ok;
}

/* the headers' word: the JP2's boxes, the codestream's SIZ and COD */
IM_HIDE bool
jp2_info(const uint8_t *data, size_t len, ImInfo *info) {
  jpx_t         *j;
  jp2_t          x;
  const uint8_t *cs = data;
  size_t         cs_len = len, pos = 2u;
  bool           ok = false, siz = false;
  uint32_t       c;

  memset(info, 0, sizeof(*info));
  memset(&x, 0, sizeof(x));
  if (!data || len < 12u || !(j = calloc(1, sizeof(*j)))) return false;
  j->arena.limit = X_ARENA;
  info->fileFormatType = IM_FILEFORMATTYPE_J2K;
  if (be32(data) == 12u && be32(data + 4) == 0x6A502020u) {
    cs = NULL;
    if (!read_boxes(j, &x, data, len, &cs, &cs_len, 0u) || !cs) goto done;
    info->fileFormatType = IM_FILEFORMATTYPE_JP2;
  }
  if (cs_len < 4u || be16(cs) != 0xFF4Fu) goto done;
  /* the main header: SIZ, COD */
  while (pos + 4u <= cs_len) {
    uint32_t m = be16(cs + pos), l = be16(cs + pos + 2u);

    if (m == 0xFF90u || l < 2u || pos + 2u + l > cs_len) break;
    if (m == 0xFF51u) {
      if (!read_siz(j, cs + pos + 4u, l - 2u)) goto done;
      siz = true;
    } else if (m == 0xFF52u && l >= 12u) {
      info->levels = cs[pos + 9u] + 1u; /* Scod, progression, layers (2), MCT, then the decompositions */
    }
    pos += 2u + l;
  }
  if (!siz) goto done;
  info->coding     = IM_CODING_JPEG2000;
  info->width      = j->x1 - j->x0;
  info->height     = j->y1 - j->y0;
  info->components = j->ncomp;
  for (c = 0; c < j->ncomp; c++) {
    if (j->prec[c] > info->bitsPerComponent) info->bitsPerComponent = j->prec[c];
  }
  /* an opacity channel, as compose finds it */
  {
    xpick_t pk;

    info->opacity = pick(j, &x, false, &pk) && pk.opacity >= 0;
  }
  ok = true;

done:
  arena_free_all(&j->arena);
  free(j);
  return ok;
}

static jxopts_t
opts_of(const im_open_config_t *c) {
  jxopts_t o;

  o.order   = c->byteOrder;
  o.threads = c->threads;
  o.raw     = c->paletteIndexes;
  o.pad     = c->rgbPadded;
  o.eight   = c->eightBits;
  o.opacity = c->opacity;
  o.runner  = c->runner;
  o.dest        = c->dest;
  o.dest_stride = c->destStride;
  o.dest_size   = c->destSize;
  o.scale       = c->scale;
  return o;
}

/* decoded into a new image; NULL where it won't */
static ImImage*
dec(const uint8_t *data, size_t len, const im_open_config_t *conf) {
  jxopts_t o  = opts_of(conf);
  ImImage *im = calloc(1, sizeof(*im));

  if (im && !jpx_decode(data, len, im, &o)) {
    free(im);
    return NULL;
  }
  if (im) {
    im->openIntent = conf->openIntent;
    im->ori        = IM_ORIENTATION_UP;
  }
  return im;
}

IM_HIDE
ImResult
jp2_dec_mem(ImImage               ** __restrict dest,
            const uint8_t          * __restrict data,
            size_t                              len,
            const im_open_config_t * __restrict open_config) {
  *dest = dec(data, len, open_config);
  return *dest ? IM_OK : IM_EBADF;
}

#if !IM_CODECS_ONLY
IM_HIDE
ImResult
jp2_dec(ImImage         ** __restrict dest,
        const char       * __restrict path,
        im_open_config_t * __restrict open_config) {
  ImFileResult fres;

  *dest = NULL;
  fres  = im_readfile(path, open_config->openIntent != IM_OPEN_INTENT_READWRITE);

  if (fres.ret != IM_OK || !fres.raw) {
    return IM_ERR;
  }

  *dest = dec(fres.raw, fres.size, open_config);

  /* the pixels don't point into the file: it goes now */
  if (fres.mmap) { im_unmap(fres.raw, fres.size); }
  else           { free(fres.raw);                }

  if (!*dest) {
#ifdef __APPLE__
    /* what isn't read here (JPX's extensions, a tile-component's HT and
       T.800 code-blocks mixed) Core Image may read */
    {
      ImResult ret;

      ret = coreimg_dec(dest, path, open_config);
      if (*dest) {
        (*dest)->fileFormatType = IM_FILEFORMATTYPE_JP2;
      }

      return ret;
    }
#else
    return IM_ERR;
#endif
  }

  return IM_OK;
}
#endif /* !IM_CODECS_ONLY */
