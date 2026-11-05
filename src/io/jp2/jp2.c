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
 * JPEG 2000 (ISO/IEC 15444-1), im's own: a JP2 file or a bare codestream;
 * tiles and tile-parts, precincts, the five progressions and POC, layers,
 * every code-block style but HT, packet headers in PPM or PPT, SOP and EPH,
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
  X_PIXELS = 1u << 26,  /* an image's */
  X_BLOCKS = 1u << 22,  /* code-blocks a tile */
  X_PRECS  = 1u << 22,  /* precincts a tile */
  X_PAL    = 1024,      /* a palette's entries */
  X_ARENA  = 1u << 30   /* a tile's structures and data */
};

/* -- memory: a bump allocator on malloc ---------------------------------------- */

/* everything a decode (or a tile) parses lives as long as it does, every
   byte charged to its budget */

enum { BLOCK_BYTES = 64 * 1024 };

typedef struct arena_block arena_block_t;

struct arena_block {
  arena_block_t *next;
  size_t         used;
  size_t         cap;
  /* data follows */
};

typedef struct arena {
  arena_block_t *head;
  uint64_t       used;  /* charged, blocks included */
  uint64_t       limit;
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
    cap = size > BLOCK_BYTES / 4 ? size : BLOCK_BYTES;
    if (size > SIZE_MAX - sizeof(*b) - 16u || arena->used + cap + sizeof(*b) > arena->limit
        || !(b = malloc(sizeof(*b) + 16u + cap))) {
      return NULL;
    }
    b->cap       = cap;
    b->used      = 0u;
    arena->used += cap + sizeof(*b);

    /* a large request gets a block of its own behind the current one */
    if (cap > BLOCK_BYTES && arena->head) {
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
    free(b);
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
} xscratch_t;

enum { X_THREADS = 16 }; /* a decode's, the caller's among them */

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
  uint8_t   prec[X_COMPS], sgnd[X_COMPS], dx[X_COMPS], dy[X_COMPS];
  xcod_t    cod;
  bool      coc[X_COMPS], qcc[X_COMPS];
  uint8_t  *ppm;
  size_t    ppm_len;
  xpart_t  *parts;
  uint32_t  nparts;
  /* the components decoded: a sample 16 bits, made unsigned */
  uint16_t *plane[X_COMPS];
  uint32_t  pw[X_COMPS], ph[X_COMPS], px0[X_COMPS], py0[X_COMPS];
  xscratch_t scratch[X_THREADS];
  xpool_t    pool;
  xworker_t  worker[X_THREADS - 1];
  uint32_t   workers; /* threads besides the caller's */
  uint32_t   threads; /* at most, the caller's among them; 0 as many as help */
  im_runner_t runner;  /* the caller's threads (run NULL: none) */
  uint8_t    zc[4][512], sc[256];
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
  if (c->levels > X_LEVELS || c->xcb > 10u || c->ycb > 10u || c->xcb + c->ycb > 12u || s[4] > 1u
      || (c->style & CB_HT)) {
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
  uint32_t     nres;
  xres_t       res[X_LEVELS + 1];
  void        *data; /* int32, or float for 9/7 */
  const xcc_t *cc;
} xtc_t;

typedef struct xtile {
  arena_t        arena;
  int64_t        x0, y0, x1, y1;
  xcod_t         cod;
  xtc_t          tc[X_COMPS];
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
  if (style & CB_TERMALL) g->max = 1u;
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

  tc->cc   = cc;
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
#else
#define JPX_INLINE inline __attribute__((always_inline))
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
  uint32_t       a, c, ct;
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

/* the coder on locals a, c, ct, bp and cxs */
#define MQ_BYTEIN()                                                                                                    \
  do {                                                                                                                 \
    if (*bp == 0xFFu) {                                                                                                \
      if (bp[1] > 0x8Fu) {                                                                                             \
        c += 0xFF00u;                                                                                                  \
        ct = 8u;                                                                                                       \
      } else {                                                                                                         \
        bp++;                                                                                                          \
        c += (uint32_t)*bp << 9;                                                                                       \
        ct = 7u;                                                                                                       \
      }                                                                                                                \
    } else {                                                                                                           \
      bp++;                                                                                                            \
      c += (uint32_t)*bp << 8;                                                                                         \
      ct = 8u;                                                                                                         \
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

/* a back to 0x8000 and up: the bits it needs at once, as many as the byte
   holds (a byte in where it's done) — the bit-at-a-time loop's steps */
#define MQ_RENORM()                                                                                                    \
  do {                                                                                                                 \
    uint32_t n_ = mq_clz(a) - 16u;                                                                                     \
                                                                                                                       \
    while (n_) {                                                                                                       \
      uint32_t s_;                                                                                                     \
                                                                                                                       \
      if (!ct) MQ_BYTEIN();                                                                                            \
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
    if ((c >> 16) < q_) {                                                                                              \
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
      c -= q_ << 16;                                                                                                   \
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
  uint32_t       a = t->a, c = t->c, ct = t->ct;                                                                       \
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
  c  = (uint32_t)*bp << 16;
  ct = 0;
  MQ_BYTEIN();
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

static void
t1_tables(jpx_t *j) {
  uint32_t i, o;

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
#define T1_SIGNIFY(i)                                                                                                  \
  do {                                                                                                                 \
    uint32_t neg_, s_ = T1_SC(i);                                                                                      \
                                                                                                                       \
    MQ_DECODE(neg_, s_ >> 1);                                                                                          \
    neg_ ^= s_ & 1u;                                                                                                   \
    d[(size_t)(i) * w] = neg_ ? -oph : oph;                                                                            \
    T1_MARK(i, neg_);                                                                                                  \
  } while (0)

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
        MQ_DECODE(v_, zc[fi_ & 0x1FFu]);                                                                               \
        if (v_) T1_SIGNIFY(i);                                                                                         \
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
      else MQ_DECODE(v_, fi_ & T_MU ? 16u : fi_ & T_NEIGH ? 15u : 14u);                                                \
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

/* a code-block decoded into the tile-component's buffer, dequantized */
static bool
t1_block(const jpx_t *j, xscratch_t *sc, xcblk_t *b, const xband_t *band, const xcc_t *cc, xtc_t *tc) {
  t1_t     t;
  uint32_t w = (uint32_t)(b->x1 - b->x0), h = (uint32_t)(b->y1 - b->y0), x, y, s, passtype = 2u;
  int32_t  bp = (int32_t)cc->roi + b->numbps;
  size_t   total = 0, at = 0, stride = (size_t)(tc->x1 - tc->x0);

  if (!b->nsegs || b->numbps <= 0 || bp >= 31 || !w || !h) return true;
  if (!sc->flags && (!(sc->flags = malloc((1026u * 4u + 2048u) * sizeof(*sc->flags)))
                     || !(sc->cdata = malloc(4096u * sizeof(*sc->cdata))))) {
    return false;
  }
  /* the bytes in one place, room for the 0xFF 0xFF after each segment */
  for (s = 0; s < b->npieces; s++) total += b->pieces[s].len;
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

  memset(&t, 0, sizeof(t));
  t.w      = w;
  t.h      = h;
  t.fs     = w + 2u;
  t.flags  = sc->flags;
  t.data   = sc->cdata;
  t.zc     = j->zc[band->orient];
  t.sc     = j->sc;
  t.causal = (cc->style & CB_CAUSAL) != 0;
  memset(t.flags, 0, (size_t)t.fs * ((h + 3u) / 4u + 2u) * sizeof(*t.flags));
  memset(t.data, 0, (size_t)w * h * sizeof(*t.data));
  mq_reset(&t);

  at = 0;
  for (s = 0; s < b->nsegs; s++) {
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

    for (x = 0; x < w * h; x++) {
      int32_t v = t.data[x], m = v < 0 ? -v : v;

      if (m >= thresh) t.data[x] = v < 0 ? -(m >> cc->roi) : m >> cc->roi;
    }
  }

  {
    size_t ox = band->offx + (size_t)(b->x0 - band->x0), oy = band->offy + (size_t)(b->y0 - band->y0);

    if (cc->reversible) {
      int32_t *out = (int32_t *)tc->data + oy * stride + ox;

      for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) out[(size_t)y * stride + x] = t.data[(size_t)y * w + x] / 2;
      }
    } else {
      float *out  = (float *)tc->data + oy * stride + ox;
      float  step = 0.5f * band->step;

      for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) out[(size_t)y * stride + x] = (float)t.data[(size_t)y * w + x] * step;
      }
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

/* level r's rows from `item` × V: each lifted where its halves lie, then
   interleaved through the scratch */
static bool
idwt_rows(jpx_t *j, xscratch_t *s, void *ctx, uint32_t item) {
  const xlevel_t *lv = ctx;
  xtc_t          *tc = lv->tc;
  const xres_t   *res = &tc->res[lv->r], *low = &tc->res[lv->r - 1u];
  size_t          stride = (size_t)(tc->x1 - tc->x0);
  uint32_t        w = (uint32_t)(res->x1 - res->x0), h = (uint32_t)(res->y1 - res->y0);
  uint32_t        sw = (uint32_t)(low->x1 - low->x0), cx = (uint32_t)res->x0 & 1u, y, i;
  uint8_t        *tmp = lanes(s, w);

  (void)j;
  if (!tmp) return false;
  for (y = item * V; y < h && y < item * V + V; y++) {
    uint8_t *row = (uint8_t *)tc->data + (size_t)y * stride * 4u;

    lift_apart(row, row + (size_t)sw * 4u, sw, w - sw, cx, 1u, 1u, tc->cc->reversible);
    /* lows to cx, cx + 2, …; highs between (32 bits either way) */
    {
      const uint32_t *l = (const uint32_t *)row, *hh = l + sw;
      uint32_t       *t = (uint32_t *)tmp;

      for (i = 0; i < sw; i++) t[cx + 2u * i] = l[i];
      for (i = 0; i < w - sw; i++) t[1u - cx + 2u * i] = hh[i];
    }
    memcpy(row, tmp, (size_t)w * 4u);
  }
  return true;
}

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
  lift_apart(col, col + (size_t)sh * stride, sh, h - sh, cy, stride, n, tc->cc->reversible);
  for (i = 0; i < sh; i++) memcpy(tmp + (size_t)(cy + 2u * i) * n, col + (size_t)i * stride, n * 4u);
  for (i = 0; i < h - sh; i++) memcpy(tmp + (size_t)(1u - cy + 2u * i) * n, col + (size_t)(sh + i) * stride, n * 4u);
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
    if (j->runner.workers >= 2u && j->runner.workers <= X_THREADS && (uint64_t)(j->x1 - j->x0) * (j->y1 - j->y0) >= (1u << 18)) {
      j->workers = j->runner.workers - 1u;
    }
    return;
  }

  if (!n || (uint64_t)(j->x1 - j->x0) * (j->y1 - j->y0) < (1u << 18)) return;
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
  xtc_t         *tc;
} xjob_t;

static bool
t1_job(jpx_t *j, xscratch_t *s, void *ctx, uint32_t item) {
  const xjob_t *job = (const xjob_t *)ctx + item;

  return t1_block(j, s, job->b, job->band, job->tc->cc, job->tc);
}

/* -- into the planes --------------------------------------------------------------- */

typedef struct xplace {
  xtile_t *t;
  bool     mct;
  uint32_t rows;
} xplace_t;

/* a component's samples to its plane's: the DC level shift, clamped to its
   depth, made unsigned. 9/7's rounded as OpenJPEG's lrintf, the float held
   to just past the depth first, so the loop has no branch */
static void
place_samples(const jpx_t *j, uint32_t c, bool reversible, const void *src, uint16_t *out, uint32_t w) {
  int32_t  prec = j->prec[c], lo = j->sgnd[c] ? -(1 << (prec - 1)) : 0, hi = j->sgnd[c] ? (1 << (prec - 1)) - 1 : (1 << prec) - 1;
  int32_t  shift = j->sgnd[c] ? 0 : 1 << (prec - 1), lift = j->sgnd[c] ? 1 << (prec - 1) : 0;
  uint32_t x;

  if (reversible) {
    const int32_t *s = src;

    for (x = 0; x < w; x++) {
      int64_t v = (int64_t)s[x] + shift;

      v      = v < lo ? lo : v > hi ? hi : v;
      out[x] = (uint16_t)(v + lift);
    }
  } else {
    const float *s   = src;
    const float  flo = (float)(lo - shift - 1), fhi = (float)(hi - shift + 1);

    for (x = 0; x < w; x++) {
      float   f = s[x];
      int32_t v;

      f      = f < flo ? flo : f > fhi ? fhi : f;
      v      = (int32_t)lrintf(f) + shift;
      v      = v < lo ? lo : v > hi ? hi : v;
      out[x] = (uint16_t)(v + lift);
    }
  }
}

/* row `item` of every component: the colour transform first where the
   tile has one (its three rows at once) */
static bool
place_row(jpx_t *j, xscratch_t *s, void *ctx, uint32_t item) {
  const xplace_t *pl = ctx;
  xtile_t        *t  = pl->t;
  uint32_t        c  = 0;

  if (pl->mct && item < (uint32_t)(t->tc[0].y1 - t->tc[0].y0)) {
    const xtc_t *tc = &t->tc[0];
    uint32_t     w  = (uint32_t)(tc->x1 - tc->x0), x;
    size_t       at = (size_t)item * w;
    uint16_t    *out[3];
    void        *row = lanes(s, (size_t)w * 3u);

    if (!row) return false;
    for (c = 0; c < 3u; c++) {
      out[c] = j->plane[c] + ((size_t)(t->tc[c].y0 - (int64_t)j->py0[c]) + item) * j->pw[c]
             + (size_t)(t->tc[c].x0 - (int64_t)j->px0[c]);
    }
    if (tc->cc->reversible) {
      const int32_t *c0 = (int32_t *)t->tc[0].data + at, *c1 = (int32_t *)t->tc[1].data + at, *c2 = (int32_t *)t->tc[2].data + at;
      int32_t       *r = row, *g = r + w, *b = g + w;

      for (x = 0; x < w; x++) {
        int32_t y = c0[x], u = c1[x], v = c2[x];
        int32_t gg = (int32_t)((uint32_t)y - (uint32_t)((int32_t)((uint32_t)u + (uint32_t)v) >> 2));

        r[x] = (int32_t)((uint32_t)v + (uint32_t)gg);
        g[x] = gg;
        b[x] = (int32_t)((uint32_t)u + (uint32_t)gg);
      }
    } else {
      const float *c0 = (float *)t->tc[0].data + at, *c1 = (float *)t->tc[1].data + at, *c2 = (float *)t->tc[2].data + at;
      float       *r = row, *g = r + w, *b = g + w;

      for (x = 0; x < w; x++) {
        float y = c0[x], u = c1[x], v = c2[x];

        r[x] = y + v * 1.402f;
        g[x] = y - u * 0.34413f - v * 0.71414f;
        b[x] = y + u * 1.772f;
      }
    }
    for (c = 0; c < 3u; c++) place_samples(j, c, tc->cc->reversible, (uint32_t *)row + (size_t)c * w, out[c], w);
  }
  for (; c < j->ncomp; c++) {
    const xtc_t *tc = &t->tc[c];
    uint32_t     w = (uint32_t)(tc->x1 - tc->x0);

    if (item >= (uint32_t)(tc->y1 - tc->y0)) continue;
    place_samples(j, c, tc->cc->reversible, (uint32_t *)tc->data + (size_t)item * w,
                  j->plane[c] + ((size_t)(tc->y0 - (int64_t)j->py0[c]) + item) * j->pw[c] + (size_t)(tc->x0 - (int64_t)j->px0[c]), w);
  }
  return true;
}

/* -- a tile ----------------------------------------------------------------------- */

static bool
tile_decode(jpx_t *j, uint32_t tile) {
  xtile_t  *t;
  bool      coc[X_COMPS] = { false }, qcc[X_COMPS] = { false }, poc_seen = false, ok = false;
  uint32_t  i, c, nparts = 0, p, q;
  size_t    body = 0, ppt = 0, ppm = 0;
  uint8_t  *buf;

  if (!(t = calloc(1, sizeof(*t)))) return false;
  t->arena.limit = X_ARENA;
  t->cod         = j->cod;

  /* its tile-parts' headers, in order; their packets and packet headers */
  for (i = 0; i < j->nparts; i++) {
    const xpart_t *part = &j->parts[i];
    size_t         pos  = 0;

    if (part->tile != tile) continue;
    nparts++;
    body += part->len;
    ppm += part->ppm_len;
    while (pos + 4u <= part->head_len) {
      uint32_t m = be16(part->head + pos), l = be16(part->head + pos + 2u);

      if (l < 2u || pos + 2u + l > part->head_len) break;
      if (m == 0xFF61u && l >= 3u) ppt += l - 3u;
      else if (!coding_marker(j, &t->cod, coc, qcc, &poc_seen, m, part->head + pos + 4u, l - 2u)) goto done;
      pos += 2u + l;
    }
  }
  if (!nparts) {
    ok = true;
    goto done;
  }
  if (!(buf = arena_alloc(&t->arena, body + ppt + ppm + 1u))) goto done;
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

  /* where it lies, and its components' structures */
  p     = tile % j->ntx;
  q     = tile / j->ntx;
  t->x0 = (int64_t)j->tx0 + (int64_t)p * j->tw;
  t->y0 = (int64_t)j->ty0 + (int64_t)q * j->th;
  t->x1 = t->x0 + j->tw;
  t->y1 = t->y0 + j->th;
  if (t->x0 < j->x0) t->x0 = j->x0;
  if (t->y0 < j->y0) t->y0 = j->y0;
  if (t->x1 > j->x1) t->x1 = j->x1;
  if (t->y1 > j->y1) t->y1 = j->y1;
  for (c = 0; c < j->ncomp; c++) {
    xtc_t *tc = &t->tc[c];

    if (!tc_build(j, t, c)) goto done;
    if (!(tc->data = zget(&t->arena, (uint64_t)(tc->x1 - tc->x0) * (uint64_t)(tc->y1 - tc->y0) * 4u))) goto done;
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

  /* the code-blocks, then each level's rows and columns */
  {
    xjob_t  *jobs;
    uint32_t n = 0, r;

    if (!(jobs = arena_alloc(&t->arena, (uint64_t)(t->blocks ? t->blocks : 1u) * sizeof(*jobs)))) goto done;
    for (c = 0; c < j->ncomp; c++) {
      xtc_t *tc = &t->tc[c];

      for (r = 0; r < tc->nres; r++) {
        xres_t  *res = &tc->res[r];
        uint32_t k, b, m;

        for (k = 0; k < res->pw * res->ph; k++) {
          for (b = 0; b < res->nbands; b++) {
            xpband_t *pb = &res->prec[k * res->nbands + b];

            for (m = 0; m < pb->cw * pb->ch; m++) {
              if (!pb->blk[m].nsegs || n >= t->blocks) continue;
              jobs[n].b    = &pb->blk[m];
              jobs[n].band = &res->band[b];
              jobs[n].tc   = tc;
              n++;
            }
          }
        }
      }
    }
    if (!pool_run(j, t1_job, jobs, n)) goto done;
    for (c = 0; c < j->ncomp; c++) {
      xtc_t *tc = &t->tc[c];

      for (r = 1; r < tc->nres; r++) {
        xlevel_t lv;
        uint32_t w = (uint32_t)(tc->res[r].x1 - tc->res[r].x0), h = (uint32_t)(tc->res[r].y1 - tc->res[r].y0);

        lv.tc = tc;
        lv.r  = r;
        if (!w || !h) continue;
        if (!pool_run(j, idwt_rows, &lv, (h + V - 1u) / V) || !pool_run(j, idwt_cols, &lv, (w + STRIP - 1u) / STRIP)) goto done;
      }
    }
  }

  /* the colour transform, the DC level shift, into the planes: a row an item */
  {
    xplace_t pl;

    pl.t   = t;
    pl.mct = t->cod.mct && j->ncomp >= 3u && t->tc[0].x1 - t->tc[0].x0 == t->tc[1].x1 - t->tc[1].x0
          && t->tc[0].x1 - t->tc[0].x0 == t->tc[2].x1 - t->tc[2].x0 && t->tc[0].y1 - t->tc[0].y0 == t->tc[1].y1 - t->tc[1].y0
          && t->tc[0].y1 - t->tc[0].y0 == t->tc[2].y1 - t->tc[2].y0 && t->tc[1].cc->reversible == t->tc[0].cc->reversible
          && t->tc[2].cc->reversible == t->tc[0].cc->reversible;
    pl.rows = 0;
    for (c = 0; c < j->ncomp; c++) {
      if ((uint32_t)(t->tc[c].y1 - t->tc[c].y0) > pl.rows) pl.rows = (uint32_t)(t->tc[c].y1 - t->tc[c].y0);
    }
    if (!pool_run(j, place_row, &pl, pl.rows)) goto done;
  }
  ok = true;

done:
  arena_free_all(&t->arena);
  free(t);
  return ok;
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
} jxopts_t;

/* the planes as `im`'s pixels: the colours then the opacity, a sample 8
   bits or (any channel deeper) 16 in `order` */
static bool
compose(jpx_t *j, const jp2_t *x, ImImage *im, const jxopts_t *o) {
  ImByteOrder order = o->order;
  xchan_t   ch[X_COMPS];
  uint32_t  nch = 0, ncol, space, colour[4], use[5], nuse, k, u, w = j->x1 - j->x0, h = j->y1 - j->y0, X, Y, step;
  int32_t   opacity = -1;
  bool      ycc = false, premult = false, wide = false, direct;
  uint32_t *cols;
  uint8_t  *px, *icc = NULL;
  void     *alpha = NULL;
  size_t    n = (size_t)w * h, len, i;

  /* the channels: through a palette where the JP2 maps one */
  if (!o->raw && x->nmap && x->pal) {
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
  if (o->raw) {
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

  /* not asked for: not there; CMYK, interleaved, goes without (im has no
     format for the two), apart keeps it */
  if (o->opacity == IM_OPACITY_NONE || (ncol == 4u && o->opacity != IM_OPACITY_APART)) opacity = -1;
  nuse = ncol;
  for (u = 0; u < ncol; u++) use[u] = colour[u];
  if (opacity >= 0) use[nuse++] = (uint32_t)opacity;
  for (u = 0; u < nuse; u++) wide = wide || (ch[use[u]].bits > 8u && !o->eight);

  step = nuse;
  len  = n * step * (wide ? 2u : 1u);
  if (!(px = malloc(len))) return false;

  /* 8-bit channels at full size, as they are: straight across */
  direct = !ycc && !wide;
  for (u = 0; u < nuse && direct; u++) {
    const xchan_t *c = &ch[use[u]];

    direct = c->col < 0 && c->bits == 8u && j->dx[c->comp] == 1u && j->dy[c->comp] == 1u;
  }
  if (direct) {
    if (step == 3u) {
      const uint16_t *r = j->plane[ch[use[0]].comp], *g = j->plane[ch[use[1]].comp], *b = j->plane[ch[use[2]].comp];

      for (i = 0; i < n; i++) {
        px[3u * i]      = (uint8_t)r[i];
        px[3u * i + 1u] = (uint8_t)g[i];
        px[3u * i + 2u] = (uint8_t)b[i];
      }
    } else if (step == 4u) {
      const uint16_t *r = j->plane[ch[use[0]].comp], *g = j->plane[ch[use[1]].comp], *b = j->plane[ch[use[2]].comp];
      const uint16_t *a = j->plane[ch[use[3]].comp];

      for (i = 0; i < n; i++) {
        px[4u * i]      = (uint8_t)r[i];
        px[4u * i + 1u] = (uint8_t)g[i];
        px[4u * i + 2u] = (uint8_t)b[i];
        px[4u * i + 3u] = (uint8_t)a[i];
      }
    } else {
      for (u = 0; u < step; u++) {
        const uint16_t *p = j->plane[ch[use[u]].comp];

        for (i = 0; i < n; i++) px[i * step + u] = (uint8_t)p[i];
      }
    }
    goto made;
  }

  if (!(cols = malloc((size_t)w * sizeof(*cols) * 5u))) {
    free(px);
    return false;
  }
  for (Y = 0; Y < h; Y++) {
    const uint16_t *row[5];

    for (u = 0; u < nuse; u++) {
      const xchan_t *c  = &ch[use[u]];
      uint32_t       cp = c->comp, dx = j->dx[cp], dy = j->dy[cp];
      int64_t        sy = (int64_t)(j->y0 + Y) / dy - (int64_t)j->py0[cp];

      sy     = sy < 0 ? 0 : sy >= j->ph[cp] ? (int64_t)j->ph[cp] - 1 : sy;
      row[u] = j->plane[cp] + (size_t)sy * j->pw[cp];
      if (Y == 0u) {
        for (X = 0; X < w; X++) {
          int64_t sx = (int64_t)(j->x0 + X) / dx - (int64_t)j->px0[cp];

          cols[u * w + X] = (uint32_t)(sx < 0 ? 0 : sx >= j->pw[cp] ? (int64_t)j->pw[cp] - 1 : sx);
        }
      }
    }
    for (X = 0; X < w; X++) {
      int32_t v[5];
      size_t  at = ((size_t)Y * w + X) * step;

      for (u = 0; u < nuse; u++) {
        const xchan_t *c = &ch[use[u]];
        int32_t        s = row[u][cols[u * w + X]];

        if (c->col >= 0) {
          if (j->sgnd[c->comp]) s -= 1 << (j->prec[c->comp] - 1u);
          s = s < 0 ? 0 : s >= (int32_t)x->pal_n ? (int32_t)x->pal_n - 1 : s;
          s = x->pal[(uint32_t)s * x->pal_cols + (uint32_t)c->col];
        }
        v[u] = s;
      }
      if (ycc) {
        int32_t prec = (int32_t)ch[colour[0]].bits;

        sycc(1 << (prec - 1), (1 << prec) - 1, v[0], v[1], v[2], &v[0], &v[1], &v[2]);
      }
      if (o->raw) {
        /* indexes: as they are */
        int32_t s = v[0] < 0 ? 0 : v[0];

        if (o->eight) px[at] = eight_shift(v[0], ch[use[0]].bits, true);
        else if (wide) ((uint16_t *)px)[at] = (uint16_t)(s > 65535 ? 65535 : s);
        else px[at] = (uint8_t)(s > 255 ? 255 : s);
      } else if (wide) {
        for (u = 0; u < nuse; u++) ((uint16_t *)px)[at + u] = sixteen(v[u], ch[use[u]].bits);
      } else if (o->eight) {
        for (u = 0; u < nuse; u++) px[at + u] = eight_shift(v[u], ch[use[u]].bits, false);
      } else {
        for (u = 0; u < nuse; u++) px[at + u] = eight(v[u], ch[use[u]].bits);
      }
    }
  }
  free(cols);

made:
  /* the opacity apart: the colours drawn together (un-premultiplied where
     the JP2 says premultiplied, to the nearest), the last sample of
     each pixel to its own plane */
  if (o->opacity == IM_OPACITY_APART && opacity >= 0) {
    uint32_t top = wide ? 65535u : 255u;

    if (!(alpha = malloc(n * (wide ? 2u : 1u)))) {
      free(px);
      return false;
    }
    for (i = 0; i < n; i++) {
      if (wide) {
        uint16_t *p = (uint16_t *)(void *)px, al = p[i * step + ncol];

        for (u = 0; u < ncol; u++) {
          uint64_t c = p[i * step + u];

          if (premult) c = al ? (c > al ? top : (c * top + al / 2u) / al) : 0u;
          p[i * ncol + u] = (uint16_t)c;
        }
        ((uint16_t *)alpha)[i] = al;
      } else {
        uint8_t al = px[i * step + ncol];

        for (u = 0; u < ncol; u++) {
          uint32_t c = px[i * step + u];

          if (premult) c = al ? (c > al ? top : (c * top + al / 2u) / al) : 0u;
          px[i * ncol + u] = (uint8_t)c;
        }
        ((uint8_t *)alpha)[i] = al;
      }
    }
    step    = ncol;
    len     = n * step * (wide ? 2u : 1u);
    opacity = -1;
  }

  /* 16-bit samples in the order asked for; the host's if any */
  if (order != IM_BYTEORDER_BIG && order != IM_BYTEORDER_LITTLE) order = IM_BYTEORDER_HOST;
  if (wide && order != IM_BYTEORDER_HOST && (order == IM_BYTEORDER_BIG) != X_HOST_BIG) {
    uint16_t *p = (uint16_t *)px;

    for (i = 0; i < n * step; i++) p[i] = bswapu16(p[i]);
    for (i = 0; alpha && i < n; i++) ((uint16_t *)alpha)[i] = bswapu16(((uint16_t *)alpha)[i]);
  }

  /* RGB as RGB0: each pixel spread to four, from the last (in place) */
  if (o->pad && step == 3u) {
    uint8_t *q = realloc(px, n * 4u * (wide ? 2u : 1u));

    if (!q) {
      free(px);
      free(alpha);
      return false;
    }
    px = q;
    if (wide) {
      uint16_t *p = (uint16_t *)(void *)px;

      for (i = n; i-- > 0u;) {
        p[4u * i + 3u] = 0xFFFFu;
        p[4u * i + 2u] = p[3u * i + 2u];
        p[4u * i + 1u] = p[3u * i + 1u];
        p[4u * i]      = p[3u * i];
      }
    } else {
      for (i = n; i-- > 0u;) {
        px[4u * i + 3u] = 0xFFu;
        px[4u * i + 2u] = px[3u * i + 2u];
        px[4u * i + 1u] = px[3u * i + 1u];
        px[4u * i]      = px[3u * i];
      }
    }
    step = 4u;
    len  = n * 4u * (wide ? 2u : 1u);
  }

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

  /* the caller's memory: the rows there, at its stride */
  if (o->dest) {
    size_t row = (size_t)w * step * (wide ? 2u : 1u), y;

    if (o->dest_stride < row || o->dest_size < o->dest_stride * (h - 1u) + row
        || (wide && (((uintptr_t)o->dest | o->dest_stride) & 1u))) {
      free(px);
      free(alpha);
      free(icc);
      return false;
    }
    for (y = 0; y < h; y++) memcpy(o->dest + y * o->dest_stride, px + y * row, row);
    free(px);
    px                = NULL;
    len               = o->dest_stride * (h - 1u) + row;
    im->row_pad_last  = (uint32_t)(o->dest_stride - row);
  }

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
  bool           ok = false;

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

  for (c = 0; c < j->ncomp; c++) {
    j->px0[c] = (uint32_t)ceil_div_u(j->x0, j->dx[c]);
    j->py0[c] = (uint32_t)ceil_div_u(j->y0, j->dy[c]);
    j->pw[c]  = (uint32_t)ceil_div_u(j->x1, j->dx[c]) - j->px0[c];
    j->ph[c]  = (uint32_t)ceil_div_u(j->y1, j->dy[c]) - j->py0[c];
    if (!j->pw[c] || !j->ph[c] || !(j->plane[c] = calloc((size_t)j->pw[c] * j->ph[c], sizeof(uint16_t)))) goto done;
  }
  t1_tables(j);
  pool_start(j);
  for (tile = 0; tile < j->ntx * j->nty; tile++) {
    if (!tile_decode(j, tile)) goto done;
  }
  ok = compose(j, &x, im, o);

done:
  pool_stop(j);
  for (c = 0; c < X_COMPS; c++) free(j->plane[c]);
  for (c = 0; c < X_THREADS; c++) {
    free(j->scratch[c].flags);
    free(j->scratch[c].cdata);
    free(j->scratch[c].bytes);
    free(j->scratch[c].lift);
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
    uint32_t nch = x.nmap && x.pal ? x.nmap : j->ncomp, k;
    uint32_t ncol = x.enumcs == 17u ? 1u : x.enumcs == 12u ? 4u
                  : x.enumcs == 16u || x.enumcs == 18u || x.enumcs == 14u || x.enumcs == 24u ? 3u
                  : x.icc == 0x47524159u ? 1u : x.icc == 0x434D594Bu ? 4u : x.icc == 0x52474220u ? 3u
                  : nch <= 2u ? 1u : nch == 3u ? 3u : 4u;

    if (ncol > nch) ncol = nch >= 3u ? 3u : 1u;
    if (x.ndef) {
      for (k = 0; k < x.ndef; k++) info->opacity = info->opacity || (x.def[k].cn < nch && (x.def[k].typ == 1u || x.def[k].typ == 2u));
    } else {
      info->opacity = nch > ncol;
    }
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
    /* what isn't read here (HT code-blocks, JPX's extensions) Core Image
       may read */
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
