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
 * JBIG2 (ITU-T T.88): a file (its header, sequential or random-access,
 * its first page) or a stream's embedded segments (IM_OPTION_FORMAT),
 * with the segments of its globals (IM_OPTION_JBIG2_GLOBALS) first. The
 * segments read: page information, end of stripe, page and file; generic
 * regions (arithmetic: the four templates and Amendment 2's extended one,
 * their adaptive pixels, typical prediction; MMR through ccitt.c),
 * refinement regions, symbol
 * dictionaries and text regions (arithmetic or Huffman, refinement and
 * aggregation, contexts kept from one dictionary for the next), pattern
 * dictionaries and halftone regions, custom Huffman tables.
 *
 * Bitmaps are a bit a pixel, 1 black, the first pixel a byte's top bit,
 * bits past the width 0. Generic regions take a pixel's context from the
 * two rows above a byte at a time and the coder's state stays in locals
 * through a row; a refinement's reference rows are first laid on the
 * region's columns, so its context too shifts along. Contexts are
 * numbered as the standard numbers them (typical prediction's depend on
 * it). The page goes out a bit a pixel, 0 black (gray's way), or a byte
 * a pixel with IM_OPTION_EIGHT_BITS.
 *
 * Damage: a region the data runs out under (the coder far past its end)
 * stops there; segments that make no sense are left out. Bitmaps may take
 * sixteen times the page's pixels in all and drawing sixty-four times
 * them, so garbage decoded on and on stops. Memory is an arena of the
 * decode's own, freed before return; IM_OPTION_MEMORY_LIMIT bounds it.
 */

#include "../../common.h"
#include "jbig2.h"
#include "../ccitt/ccitt.h"

#include <stdlib.h>
#include <string.h>

enum {
  JB_MAX_SIDE  = 1u << 20,
  JB_MAX_SEGS  = 1u << 16,
  JB_MAX_SYMS  = 1u << 20,
  JB_MAX_REFS  = 1u << 12,
  JB_BLOCK     = 1u << 16,
  JB_STEP_COST = 64u  /* each placing or symbol over its pixels: tiny ones add up too */
};

#define JB_MAX_AREA ((uint64_t)1 << 32)

/* -- memory: an arena, freed at the end ------------------------------------- */

typedef struct jblk {
  struct jblk *next;
  size_t       size;
  size_t       used;
  size_t       pad_; /* the block after it 16-aligned */
} jblk_t;

typedef struct jbm {
  uint32_t w;
  uint32_t h;
  uint32_t stride; /* (w + 7) / 8 */
  uint8_t *p;
} jbm_t;

typedef struct huff_line {
  int32_t  low;
  uint8_t  preflen;
  uint8_t  rangelen;
  uint8_t  kind; /* 0 a range, 1 the lower range, 2 the upper, 3 OOB */
  uint32_t code;
} huff_line_t;

typedef struct huff {
  huff_line_t *lines;     /* by prefix length, then code */
  uint32_t     count;
  uint32_t     first[33]; /* each length's first line */
  uint32_t     number[33];
  uint32_t     code[33];  /* each length's first code */
  uint32_t     maxlen;
} huff_t;

typedef struct seg {
  uint32_t       number;
  uint8_t        type;
  bool           retain_cx; /* a symbol dictionary keeping its contexts */
  uint32_t       page;
  uint32_t      *refs;
  uint32_t       nrefs;
  const uint8_t *data;
  size_t         len;
  jbm_t        **syms;      /* a symbol dictionary's exported ones */
  uint32_t       nsyms;
  uint8_t       *gb_cx;     /* its contexts, where kept */
  uint8_t       *gr_cx;
  jbm_t        **pats;      /* a pattern dictionary's */
  uint32_t       npats;
  jbm_t         *region;    /* an intermediate region's result */
  huff_t        *table;     /* a tables segment's */
  bool           done;
} seg_t;

typedef struct jb {
  jblk_t  *blk;
  size_t   total;  /* bytes taken, the input's counted */
  size_t   limit;  /* 0: none */
  bool     nomem;
  seg_t   *segs;
  uint32_t nsegs;
  uint32_t capsegs;
  jbm_t   *page;
  bool     page_grows;  /* its height unknown: end of stripe says */
  bool     page_fresh;  /* nothing drawn on it yet: all its default pixel */
  uint8_t  page_default;
  uint32_t page_number; /* a file's page; 0: any */
  bool     page_seen;
  uint64_t budget;      /* pixels the bitmaps may still have */
  uint64_t effort;      /* pixels drawn and symbols read still allowed */
  uint8_t *scratch;     /* a refinement's lines */
  size_t   nscratch;
  /* the part wanted (IM_OPTION_REGION): regions off it left out where none
     comes after that reads the page */
  bool     cut;
  int64_t  cx0, cy0, cx1, cy1;
} jb_t;

static void*
jb_alloc(jb_t *j, size_t n) {
  jblk_t *b;
  size_t  sz;

  n = (n + 15u) & ~(size_t)15u;
  if (j->blk && j->blk->size - j->blk->used >= n) {
    void *p = (uint8_t *)(j->blk + 1) + j->blk->used;

    j->blk->used += n;
    return p;
  }
  sz = n > JB_BLOCK / 4u ? n : (size_t)JB_BLOCK;
  if ((j->limit && (j->total + sz + sizeof(jblk_t) > j->limit || j->total + sz < j->total))
      || !(b = calloc(1, sizeof(jblk_t) + sz))) {
    j->nomem = true;
    return NULL;
  }
  j->total += sizeof(jblk_t) + sz;
  b->size = sz;
  b->used = n;
  if (sz == n && j->blk) {
    /* a big one alone, behind the current block (its room kept) */
    b->next      = j->blk->next;
    j->blk->next = b;
  } else {
    b->next = j->blk;
    j->blk  = b;
  }
  return b + 1;
}

static void
jb_free_all(jb_t *j) {
  jblk_t *b = j->blk;

  while (b) {
    jblk_t *n = b->next;

    free(b);
    b = n;
  }
  j->blk = NULL;
}

static bool
spend(jb_t *j, uint64_t n) {
  if (n > j->effort) {
    j->effort = 0u;
    return false;
  }
  j->effort -= n;
  return true;
}

/* -- bitmaps ------------------------------------------------------------------ */

static jbm_t*
bm_new(jb_t *j, uint32_t w, uint32_t h, uint32_t fill) {
  jbm_t   *b;
  uint64_t area = (uint64_t)w * h;

  if (w > JB_MAX_SIDE || h > JB_MAX_SIDE || area > JB_MAX_AREA || area > j->budget) return NULL;
  j->budget -= area;
  if (!(b = jb_alloc(j, sizeof(*b)))) return NULL;
  b->w      = w;
  b->h      = h;
  b->stride = (w + 7u) / 8u;
  if (!(b->p = jb_alloc(j, (size_t)b->stride * h + 8u))) return NULL;
  if (fill && b->stride) {
    uint32_t y;

    memset(b->p, 0xFF, (size_t)b->stride * h);
    if (w & 7u) {
      for (y = 0; y < h; y++) b->p[(size_t)y * b->stride + b->stride - 1u] = (uint8_t)(0xFF00u >> (w & 7u));
    }
  }
  return b;
}

static inline uint32_t
bm_px(const jbm_t *b, int64_t x, int64_t y) {
  if (x < 0 || y < 0 || x >= (int64_t)b->w || y >= (int64_t)b->h) return 0u;
  return (b->p[(size_t)y * b->stride + (size_t)(x >> 3)] >> (7u - (uint32_t)(x & 7))) & 1u;
}

/* 8 pixels of a row from column col (the first the top bit), 0 off it */
static inline uint32_t
get8(const jbm_t *b, const uint8_t *row, int64_t col) {
  int64_t  q;
  uint32_t sh, v0, v1;

  if (!row) return 0u;
  q  = col >= 0 ? col / 8 : -((-col + 7) / 8);
  sh = (uint32_t)(col - q * 8);
  v0 = q >= 0 && q < (int64_t)b->stride ? row[q] : 0u;
  v1 = q + 1 >= 0 && q + 1 < (int64_t)b->stride ? row[q + 1] : 0u;
  return ((v0 << 8 | v1) << sh >> 8) & 0xFFu;
}

/* a row's destination bytes d0 … d1 from the source's bytes shifted
   right by sh (byte d from source bytes q - 1 and q, q = d - xq), the
   first and last masked: each operator its own loop */
#define DRAW_ROW(EXPR)                                                                                                  \
  for (d = d0; d <= d1; d++) {                                                                                          \
    int64_t  q  = d - xq;                                                                                               \
    uint32_t lo = q >= 0 && q < ns ? s[q] : 0u, hi = q >= 1 && q - 1 < ns ? s[q - 1] : 0u;                             \
    uint32_t v = ((hi << 8 | lo) >> sh) & 0xFFu, o = row[d], m = d == d0 ? m0 : 0xFFu, r;                              \
                                                                                                                        \
    if (d == d1) m &= m1;                                                                                               \
    r      = (EXPR);                                                                                                    \
    row[d] = (uint8_t)((o & ~m) | (r & m));                                                                             \
  }

/* src drawn on dst at (x, y): OR, AND, XOR, XNOR, REPLACE; false once the
   effort allowed is spent */
static bool
bm_draw(jb_t *j, jbm_t *dst, const jbm_t *src, int64_t x, int64_t y, uint32_t op) {
  int64_t x0 = x < 0 ? -x : 0, y0 = y < 0 ? -y : 0, x1 = src->w, y1 = src->h, k;

  if (x + x1 > (int64_t)dst->w) x1 = (int64_t)dst->w - x;
  if (y + y1 > (int64_t)dst->h) y1 = (int64_t)dst->h - y;
  if (x0 >= x1 || y0 >= y1) return spend(j, JB_STEP_COST);
  if (!spend(j, JB_STEP_COST + (uint64_t)(x1 - x0) * (uint64_t)(y1 - y0))) return false;
  if (op == 0u && !x0 && x1 == (int64_t)src->w && src->stride) {
    /* OR with every column inside (symbols, patterns): each source byte
       shifted into two, no masks (the source's bits past its width are 0);
       the byte it spills into only where a pixel of it lands */
    uint32_t sh = (uint32_t)(x & 7), ns = src->stride, q;
    size_t   xb = (size_t)(x >> 3);
    bool     spill = sh && xb + ns <= (size_t)((x + src->w - 1) >> 3);

    for (k = y0; k < y1; k++) {
      const uint8_t *sr = src->p + (size_t)k * ns;
      uint8_t       *dr = dst->p + (size_t)(y + k) * dst->stride + xb;

      if (!sh) {
        for (q = 0; q < ns; q++) dr[q] |= sr[q];
      } else {
        uint32_t c = 0u;

        for (q = 0; q < ns; q++) {
          uint32_t v = sr[q];

          dr[q] |= (uint8_t)((c << (8u - sh)) | (v >> sh));
          c = v;
        }
        if (spill) dr[ns] |= (uint8_t)(c << (8u - sh));
      }
    }
    return true;
  }
  {
    /* the destination's columns [c0, c1), its bytes d0 … d1; dst byte d
       takes the source's bits from column 8d - x */
    int64_t  c0 = x + x0, c1 = x + x1, d0 = c0 >> 3, d1 = (c1 - 1) >> 3, d, ns = src->stride;
    int64_t  xq = x >= 0 ? x / 8 : -((-x + 7) / 8);
    uint32_t m0 = 0xFFu >> (c0 & 7), m1 = (0xFF00u >> (((c1 - 1) & 7) + 1)) & 0xFFu, sh = (uint32_t)(x - xq * 8);

    for (k = y0; k < y1; k++) {
      const uint8_t *s   = src->p + (size_t)k * src->stride;
      uint8_t       *row = dst->p + (size_t)(y + k) * dst->stride;

      switch (op) {
        case 0:  DRAW_ROW(o | v) break;
        case 1:  DRAW_ROW(o & v) break;
        case 2:  DRAW_ROW(o ^ v) break;
        case 3:  DRAW_ROW(~(o ^ v)) break;
        default: DRAW_ROW(v) break;
      }
    }
  }
  return true;
}
#undef DRAW_ROW

/* -- reading ------------------------------------------------------------------ */

typedef struct jrd {
  const uint8_t *p;
  size_t         len;
  size_t         at;  /* a byte */
  uint32_t       bit; /* bits of it read, for Huffman */
  bool           bad;
} jrd_t;

static uint32_t
rd_u(jrd_t *r, uint32_t n) {
  uint32_t v = 0u;

  if (r->at > r->len || n > r->len - r->at) {
    r->bad = true;
    r->at  = r->len;
    return 0u;
  }
  while (n--) v = v << 8 | r->p[r->at++];
  return v;
}

static int32_t
rd_s8(jrd_t *r) {
  return (int8_t)rd_u(r, 1u);
}

/* the next 32 bits, 0s past the end */
static inline uint32_t
rd_peek(const jrd_t *r) {
  uint64_t w = 0u;
  size_t   k;

  for (k = 0; k < 5u; k++) w = w << 8 | (r->at + k < r->len ? r->p[r->at + k] : 0u);
  return (uint32_t)(w >> (8u - r->bit));
}

static inline void
rd_skip(jrd_t *r, uint32_t n) {
  uint64_t pos = (uint64_t)r->at * 8u + r->bit + n;

  if (pos > (uint64_t)r->len * 8u) {
    r->bad = true;
    r->at  = r->len;
    r->bit = 0u;
    return;
  }
  r->at  = (size_t)(pos >> 3);
  r->bit = (uint32_t)(pos & 7u);
}

static uint32_t
rd_bits(jrd_t *r, uint32_t n) {
  uint32_t v;

  if (!n) return 0u;
  v = rd_peek(r) >> (32u - n);
  rd_skip(r, n);
  return r->bad ? 0u : v;
}

static void
rd_align(jrd_t *r) {
  if (r->bit) {
    r->bit = 0u;
    r->at++;
  }
}

/* -- the arithmetic decoder (Annex E) ------------------------------------------- */

/* a context is a byte: its state's index << 1 | its more probable symbol;
   the table by that byte, the switch folded into the LPS's next */
typedef struct mqe {
  uint16_t qe;
  uint8_t  nmps;
  uint8_t  nlps;
} mqe_t;

#define Q(qe, nm, nl, sw) { qe, (nm) * 2, (nl) * 2 + (sw) }, { qe, (nm) * 2 + 1, (nl) * 2 + 1 - (sw) }
static const mqe_t mqt[94] = {
  Q(0x5601, 1, 1, 1),   Q(0x3401, 2, 6, 0),   Q(0x1801, 3, 9, 0),   Q(0x0AC1, 4, 12, 0),  Q(0x0521, 5, 29, 0),
  Q(0x0221, 38, 33, 0), Q(0x5601, 7, 6, 1),   Q(0x5401, 8, 14, 0),  Q(0x4801, 9, 14, 0),  Q(0x3801, 10, 14, 0),
  Q(0x3001, 11, 17, 0), Q(0x2401, 12, 18, 0), Q(0x1C01, 13, 20, 0), Q(0x1601, 29, 21, 0), Q(0x5601, 15, 14, 1),
  Q(0x5401, 16, 14, 0), Q(0x5101, 17, 15, 0), Q(0x4801, 18, 16, 0), Q(0x3801, 19, 17, 0), Q(0x3401, 20, 18, 0),
  Q(0x3001, 21, 19, 0), Q(0x2801, 22, 19, 0), Q(0x2401, 23, 20, 0), Q(0x2201, 24, 21, 0), Q(0x1C01, 25, 22, 0),
  Q(0x1801, 26, 23, 0), Q(0x1601, 27, 24, 0), Q(0x1401, 28, 25, 0), Q(0x1201, 29, 26, 0), Q(0x1101, 30, 27, 0),
  Q(0x0AC1, 31, 28, 0), Q(0x09C1, 32, 29, 0), Q(0x08A1, 33, 30, 0), Q(0x0521, 34, 31, 0), Q(0x0441, 35, 32, 0),
  Q(0x02A1, 36, 33, 0), Q(0x0221, 37, 34, 0), Q(0x0141, 38, 35, 0), Q(0x0111, 39, 36, 0), Q(0x0085, 40, 37, 0),
  Q(0x0049, 41, 38, 0), Q(0x0025, 42, 39, 0), Q(0x0015, 43, 40, 0), Q(0x0009, 44, 41, 0), Q(0x0005, 45, 42, 0),
  Q(0x0001, 45, 43, 0), Q(0x5601, 46, 46, 0)
};
#undef Q

typedef struct mq {
  const uint8_t *p;
  size_t         len;
  size_t         bp;
  uint32_t       c;
  uint32_t       a;
  int32_t        ct;
  uint32_t       past; /* bytes asked for past the data: the coder's end is a few, a damaged stream's many */
} mq_t;

static inline bool
mq_spent(const mq_t *m) {
  return m->past > 64u;
}

static inline uint32_t
mq_byte(const mq_t *m, size_t i) {
  return i < m->len ? m->p[i] : 0xFFu;
}

static void
mq_in(mq_t *m) {
  if (m->bp + 1u >= m->len) m->past++;
  if (mq_byte(m, m->bp) == 0xFFu) {
    if (mq_byte(m, m->bp + 1u) > 0x8Fu) {
      m->c += 0xFF00u;
      m->ct = 8;
    } else {
      m->bp++;
      m->c += mq_byte(m, m->bp) << 9;
      m->ct = 7;
    }
  } else {
    m->bp++;
    m->c += mq_byte(m, m->bp) << 8;
    m->ct = 8;
  }
}

static void
mq_init(mq_t *m, const uint8_t *p, size_t len) {
  memset(m, 0, sizeof(*m));
  m->p  = p;
  m->len = len;
  m->c  = mq_byte(m, 0u) << 16;
  mq_in(m);
  m->c <<= 7;
  m->ct -= 7;
  m->a  = 0x8000u;
}

static inline uint32_t
jb_clz32(uint32_t v) {
#if defined(_MSC_VER) && !defined(__clang__)
  unsigned long i;

  _BitScanReverse(&i, v);
  return 31u - (uint32_t)i;
#else
  return (uint32_t)__builtin_clz(v);
#endif
}

/* the coder's state in locals (mq_a, mq_c, mq_ct) through a loop */
#define MQ_LOCALS(m) uint32_t mq_a = (m)->a, mq_c = (m)->c; int32_t mq_ct = (m)->ct
#define MQ_SAVE(m)   ((m)->a = mq_a, (m)->c = mq_c, (m)->ct = mq_ct)

#define MQ_RENORM(m)                                                                                                    \
  do {                                                                                                                  \
    uint32_t mq_n = jb_clz32(mq_a) - 16u;                                                                               \
                                                                                                                        \
    if ((int32_t)mq_n <= mq_ct) {                                                                                       \
      mq_a <<= mq_n;                                                                                                    \
      mq_c <<= mq_n;                                                                                                    \
      mq_ct -= (int32_t)mq_n;                                                                                           \
    } else {                                                                                                            \
      do {                                                                                                              \
        if (!mq_ct) {                                                                                                   \
          (m)->c = mq_c;                                                                                                \
          mq_in(m);                                                                                                     \
          mq_c  = (m)->c;                                                                                               \
          mq_ct = (m)->ct;                                                                                              \
        }                                                                                                               \
        mq_a <<= 1;                                                                                                     \
        mq_c <<= 1;                                                                                                     \
        mq_ct--;                                                                                                        \
      } while (!(mq_a & 0x8000u));                                                                                      \
    }                                                                                                                   \
  } while (0)

#define MQ_DEC(m, d, cxp)                                                                                               \
  do {                                                                                                                  \
    uint8_t     *mq_x = (cxp);                                                                                          \
    uint32_t     mq_s = *mq_x;                                                                                          \
    const mqe_t *mq_e = &mqt[mq_s];                                                                                     \
    uint32_t     mq_q = mq_e->qe;                                                                                       \
                                                                                                                        \
    mq_a -= mq_q;                                                                                                       \
    if ((mq_c >> 16) < mq_q) {                                                                                          \
      if (mq_a < mq_q) {                                                                                                \
        (d)   = mq_s & 1u;                                                                                              \
        *mq_x = mq_e->nmps;                                                                                             \
      } else {                                                                                                          \
        (d)   = (mq_s & 1u) ^ 1u;                                                                                       \
        *mq_x = mq_e->nlps;                                                                                             \
      }                                                                                                                 \
      mq_a = mq_q;                                                                                                      \
      MQ_RENORM(m);                                                                                                     \
    } else {                                                                                                            \
      mq_c -= mq_q << 16;                                                                                               \
      if (mq_a & 0x8000u) {                                                                                             \
        (d) = mq_s & 1u;                                                                                                \
      } else {                                                                                                          \
        if (mq_a < mq_q) {                                                                                              \
          (d)   = (mq_s & 1u) ^ 1u;                                                                                     \
          *mq_x = mq_e->nlps;                                                                                           \
        } else {                                                                                                        \
          (d)   = mq_s & 1u;                                                                                            \
          *mq_x = mq_e->nmps;                                                                                           \
        }                                                                                                               \
        MQ_RENORM(m);                                                                                                   \
      }                                                                                                                 \
    }                                                                                                                   \
  } while (0)

static inline uint32_t
mq_bit(mq_t *m, uint8_t *cx) {
  uint32_t d;
  MQ_LOCALS(m);

  MQ_DEC(m, d, cx);
  MQ_SAVE(m);
  return d;
}

/* the integer decoders (A.2): false for OOB */
typedef struct iax {
  uint8_t cx[512];
} iax_t;

/* the coder's state in locals through the value's bits */
#define IA_BIT(b)                                                                                                       \
  do {                                                                                                                  \
    MQ_DEC(m, b, &c->cx[prev & 511u]);                                                                                  \
    prev = prev < 256u ? (prev << 1 | (b)) : (((prev << 1 | (b)) & 511u) | 256u);                                       \
  } while (0)

static uint32_t
ia_value(mq_t *m, iax_t *c, uint32_t *prev_io, uint32_t n) {
  uint32_t v = 0u, prev = *prev_io, b;
  MQ_LOCALS(m);

  while (n--) {
    IA_BIT(b);
    v = v << 1 | b;
  }
  MQ_SAVE(m);
  *prev_io = prev;
  return v;
}

static bool
ia_int(mq_t *m, iax_t *c, int32_t *out) {
  static const uint32_t bits[6] = { 2, 4, 6, 8, 12, 32 }, base[6] = { 0, 4, 20, 84, 340, 4436 };
  uint32_t prev = 1u, s, v, b, k = 0u;

  {
    MQ_LOCALS(m);

    IA_BIT(s);
    /* the prefix: 1s, a 0 ends it (five 1s: the last range) */
    for (k = 0; k < 5u; k++) {
      IA_BIT(b);
      if (!b) break;
    }
    MQ_SAVE(m);
  }
  v = ia_value(m, c, &prev, bits[k]) + base[k];
  if (s && !v) return false;
  *out = s ? (int32_t)(0u - v) : (int32_t)v;
  return true;
}
#undef IA_BIT

static uint32_t
ia_id(mq_t *m, uint8_t *cx, uint32_t len) {
  uint32_t prev = 1u, i;

  for (i = 0; i < len; i++) prev = prev << 1 | mq_bit(m, &cx[prev]);
  return prev - (1u << len);
}

/* -- Huffman tables (Annex B) ------------------------------------------------- */

typedef struct huff_spec {
  int32_t low;
  uint8_t preflen;
  uint8_t rangelen;
  uint8_t kind;
} huff_spec_t;

/* the standard tables, B.1 to B.15: their lines as printed (the lower and
   upper ranges, then OOB, last), codes assigned as B.3 assigns them */
static const huff_spec_t std_1[] = { { 0, 1, 4, 0 }, { 16, 2, 8, 0 }, { 272, 3, 16, 0 }, { 65808, 3, 32, 2 } };
static const huff_spec_t std_2[] = { { 0, 1, 0, 0 }, { 1, 2, 0, 0 }, { 2, 3, 0, 0 }, { 3, 4, 3, 0 }, { 11, 5, 6, 0 },
                                     { 75, 6, 32, 2 }, { 0, 6, 0, 3 } };
static const huff_spec_t std_3[] = { { -256, 8, 8, 0 }, { 0, 1, 0, 0 }, { 1, 2, 0, 0 }, { 2, 3, 0, 0 }, { 3, 4, 3, 0 },
                                     { 11, 5, 6, 0 }, { -257, 8, 32, 1 }, { 75, 7, 32, 2 }, { 0, 6, 0, 3 } };
static const huff_spec_t std_4[] = { { 1, 1, 0, 0 }, { 2, 2, 0, 0 }, { 3, 3, 0, 0 }, { 4, 4, 3, 0 }, { 12, 5, 6, 0 },
                                     { 76, 5, 32, 2 } };
static const huff_spec_t std_5[] = { { -255, 7, 8, 0 }, { 1, 1, 0, 0 }, { 2, 2, 0, 0 }, { 3, 3, 0, 0 }, { 4, 4, 3, 0 },
                                     { 12, 5, 6, 0 }, { -256, 7, 32, 1 }, { 76, 6, 32, 2 } };
static const huff_spec_t std_6[] = { { -2048, 5, 10, 0 }, { -1024, 4, 9, 0 }, { -512, 4, 8, 0 }, { -256, 4, 7, 0 },
                                     { -128, 5, 6, 0 }, { -64, 5, 5, 0 }, { -32, 4, 5, 0 }, { 0, 2, 7, 0 },
                                     { 128, 3, 7, 0 }, { 256, 3, 8, 0 }, { 512, 4, 9, 0 }, { 1024, 4, 10, 0 },
                                     { -2049, 6, 32, 1 }, { 2048, 6, 32, 2 } };
static const huff_spec_t std_7[] = { { -1024, 4, 9, 0 }, { -512, 3, 8, 0 }, { -256, 4, 7, 0 }, { -128, 5, 6, 0 },
                                     { -64, 5, 5, 0 }, { -32, 4, 5, 0 }, { 0, 4, 5, 0 }, { 32, 5, 5, 0 },
                                     { 64, 5, 6, 0 }, { 128, 4, 7, 0 }, { 256, 3, 8, 0 }, { 512, 3, 9, 0 },
                                     { 1024, 3, 10, 0 }, { -1025, 5, 32, 1 }, { 2048, 5, 32, 2 } };
static const huff_spec_t std_8[] = { { -15, 8, 3, 0 }, { -7, 9, 1, 0 }, { -5, 8, 1, 0 }, { -3, 9, 0, 0 }, { -2, 7, 0, 0 },
                                     { -1, 4, 0, 0 }, { 0, 2, 1, 0 }, { 2, 5, 0, 0 }, { 3, 6, 0, 0 }, { 4, 3, 4, 0 },
                                     { 20, 6, 1, 0 }, { 22, 4, 4, 0 }, { 38, 4, 5, 0 }, { 70, 5, 6, 0 },
                                     { 134, 5, 7, 0 }, { 262, 6, 7, 0 }, { 390, 7, 8, 0 }, { 646, 6, 10, 0 },
                                     { -16, 9, 32, 1 }, { 1670, 9, 32, 2 }, { 0, 2, 0, 3 } };
static const huff_spec_t std_9[] = { { -31, 8, 4, 0 }, { -15, 9, 2, 0 }, { -11, 8, 2, 0 }, { -7, 9, 1, 0 }, { -5, 7, 1, 0 },
                                     { -3, 4, 1, 0 }, { -1, 3, 1, 0 }, { 1, 3, 1, 0 }, { 3, 5, 1, 0 }, { 5, 6, 1, 0 },
                                     { 7, 3, 5, 0 }, { 39, 6, 2, 0 }, { 43, 4, 5, 0 }, { 75, 4, 6, 0 },
                                     { 139, 5, 7, 0 }, { 267, 5, 8, 0 }, { 523, 6, 8, 0 }, { 779, 7, 9, 0 },
                                     { 1291, 6, 11, 0 }, { -32, 9, 32, 1 }, { 3339, 9, 32, 2 }, { 0, 2, 0, 3 } };
static const huff_spec_t std_10[] = { { -21, 7, 4, 0 }, { -5, 8, 0, 0 }, { -4, 7, 0, 0 }, { -3, 5, 0, 0 }, { -2, 2, 2, 0 },
                                      { 2, 5, 0, 0 }, { 3, 6, 0, 0 }, { 4, 7, 0, 0 }, { 5, 8, 0, 0 }, { 6, 2, 6, 0 },
                                      { 70, 5, 5, 0 }, { 102, 6, 5, 0 }, { 134, 6, 6, 0 }, { 198, 6, 7, 0 },
                                      { 326, 6, 8, 0 }, { 582, 6, 9, 0 }, { 1094, 6, 10, 0 }, { 2118, 7, 11, 0 },
                                      { -22, 8, 32, 1 }, { 4166, 8, 32, 2 }, { 0, 2, 0, 3 } };
static const huff_spec_t std_11[] = { { 1, 1, 0, 0 }, { 2, 2, 1, 0 }, { 4, 4, 0, 0 }, { 5, 4, 1, 0 }, { 7, 5, 1, 0 },
                                      { 9, 5, 2, 0 }, { 13, 6, 2, 0 }, { 17, 7, 2, 0 }, { 21, 7, 3, 0 }, { 29, 7, 4, 0 },
                                      { 45, 7, 5, 0 }, { 77, 7, 6, 0 }, { 141, 7, 32, 2 } };
static const huff_spec_t std_12[] = { { 1, 1, 0, 0 }, { 2, 2, 0, 0 }, { 3, 3, 1, 0 }, { 5, 5, 0, 0 }, { 6, 5, 1, 0 },
                                      { 8, 6, 1, 0 }, { 10, 7, 0, 0 }, { 11, 7, 1, 0 }, { 13, 7, 2, 0 }, { 17, 7, 3, 0 },
                                      { 25, 7, 4, 0 }, { 41, 8, 5, 0 }, { 73, 8, 32, 2 } };
static const huff_spec_t std_13[] = { { 1, 1, 0, 0 }, { 2, 3, 0, 0 }, { 3, 4, 0, 0 }, { 4, 5, 0, 0 }, { 5, 4, 1, 0 },
                                      { 7, 3, 3, 0 }, { 15, 6, 1, 0 }, { 17, 6, 2, 0 }, { 21, 6, 3, 0 }, { 29, 6, 4, 0 },
                                      { 45, 6, 5, 0 }, { 77, 7, 6, 0 }, { 141, 7, 32, 2 } };
static const huff_spec_t std_14[] = { { -2, 3, 0, 0 }, { -1, 3, 0, 0 }, { 0, 1, 0, 0 }, { 1, 3, 0, 0 }, { 2, 3, 0, 0 } };
static const huff_spec_t std_15[] = { { -24, 7, 4, 0 }, { -8, 6, 2, 0 }, { -4, 5, 1, 0 }, { -2, 4, 0, 0 }, { -1, 3, 0, 0 },
                                      { 0, 1, 0, 0 }, { 1, 3, 0, 0 }, { 2, 4, 0, 0 }, { 3, 5, 1, 0 }, { 5, 6, 2, 0 },
                                      { 9, 7, 4, 0 }, { -25, 7, 32, 1 }, { 25, 7, 32, 2 } };

static const struct { const huff_spec_t *lines; uint32_t count; } std_tables[16] = {
  { NULL, 0 },
#define T(n) { std_##n, sizeof(std_##n) / sizeof(std_##n[0]) }
  T(1), T(2), T(3), T(4), T(5), T(6), T(7), T(8), T(9), T(10), T(11), T(12), T(13), T(14), T(15)
#undef T
};

/* the codes assigned (B.3): by length, in the lines' order */
static huff_t*
huff_build(jb_t *j, const huff_spec_t *spec, uint32_t count) {
  huff_t  *h;
  uint32_t lencount[33] = {0}, firstcode[33] = {0}, next[33], i, len, k;

  if (!count || count > 65536u || !(h = jb_alloc(j, sizeof(*h))) || !(h->lines = jb_alloc(j, count * sizeof(*h->lines)))) {
    return NULL;
  }
  for (i = 0; i < count; i++) {
    if (spec[i].preflen > 32u) return NULL;
    lencount[spec[i].preflen]++;
  }
  lencount[0] = 0u;
  for (len = 1; len <= 32u; len++) {
    firstcode[len] = (firstcode[len - 1u] + lencount[len - 1u]) << 1;
    next[len]      = firstcode[len];
  }
  /* by length then code: each length's lines in their order, codes counting up */
  for (k = 0, len = 1; len <= 32u; len++) {
    if (!lencount[len]) continue;
    h->first[len]  = k;
    h->number[len] = lencount[len];
    h->code[len]   = firstcode[len];
    h->maxlen      = len;
    for (i = 0; i < count; i++) {
      if (spec[i].preflen != len) continue;
      h->lines[k].low      = spec[i].low;
      h->lines[k].preflen  = spec[i].preflen;
      h->lines[k].rangelen = spec[i].rangelen;
      h->lines[k].kind     = spec[i].kind;
      h->lines[k].code     = next[len]++;
      k++;
    }
  }
  h->count = k;
  return h;
}

static huff_t*
huff_std(jb_t *j, uint32_t n) {
  return n >= 1u && n <= 15u ? huff_build(j, std_tables[n].lines, std_tables[n].count) : NULL;
}

/* a value: false for OOB, or where nothing matches */
static bool
huff_decode(jrd_t *r, const huff_t *h, int32_t *out) {
  uint32_t w, len;

  if (!h) {
    r->bad = true;
    return false;
  }
  if (r->bad) return false;
  w = rd_peek(r);
  for (len = 1; len <= h->maxlen; len++) {
    uint32_t code = w >> (32u - len);

    if (h->number[len] && code - h->code[len] < h->number[len]) {
      const huff_line_t *l = &h->lines[h->first[len] + (code - h->code[len])];
      uint32_t           v;

      rd_skip(r, len);
      if (r->bad || l->kind == 3) return false;
      v = l->rangelen ? rd_bits(r, l->rangelen) : 0u;
      if (r->bad) return false;
      *out = l->kind == 1 ? (int32_t)((int64_t)l->low - v) : (int32_t)((int64_t)l->low + v);
      return true;
    }
  }
  r->bad = true;
  return false;
}

/* a tables segment (7.4.13) */
static huff_t*
huff_custom(jb_t *j, const uint8_t *p, size_t len) {
  jrd_t        r = { p, len, 0, 0, false };
  uint32_t     flags = rd_u(&r, 1u), htps = ((flags >> 1) & 7u) + 1u, htrs = ((flags >> 4) & 7u) + 1u, n = 0u;
  int32_t      low = (int32_t)rd_u(&r, 4u), high = (int32_t)rd_u(&r, 4u);
  int64_t      cur = low;
  huff_spec_t *spec;

  /* the lower range is low - 1 and below: none under INT32_MIN */
  if (r.bad || high < low || low == INT32_MIN || !(spec = jb_alloc(j, 65540u * sizeof(*spec)))) return NULL;
  while (cur < high && n < 65536u && !r.bad) {
    spec[n].preflen  = (uint8_t)rd_bits(&r, htps);
    spec[n].rangelen = (uint8_t)rd_bits(&r, htrs);
    spec[n].low      = (int32_t)cur;
    spec[n].kind     = 0;
    if (spec[n].rangelen > 32u) return NULL;
    cur += (int64_t)1 << spec[n].rangelen;
    n++;
  }
  spec[n++] = (huff_spec_t){ low - 1, (uint8_t)rd_bits(&r, htps), 32, 1 };
  spec[n++] = (huff_spec_t){ high, (uint8_t)rd_bits(&r, htps), 32, 2 };
  if (flags & 1u) spec[n++] = (huff_spec_t){ 0, (uint8_t)rd_bits(&r, htps), 0, 3 };
  return r.bad ? NULL : huff_build(j, spec, n);
}

/* -- generic regions (6.2) ---------------------------------------------------- */

typedef struct gen {
  uint32_t     w;
  uint32_t     h;
  uint32_t     rows;  /* decoded: the rest not needed */
  uint8_t      templ;
  bool         tpgdon;
  bool         mmr;
  bool         ext;    /* template 0's extended one: twelve adaptive pixels
                          (EXTTEMPLATE, T.88 Amd. 2) */
  int8_t       at[24]; /* (x, y) pairs */
  const jbm_t *skip;   /* pixels not decoded, 0 (halftones) */
} gen_t;

static const int8_t at_nominal[4][8] = {
  { 3, -1, -3, -1, 2, -2, -2, -2 }, { 3, -1 }, { 2, -1 }, { 2, -1 }
};

/* the extended template's (Table 5): its sixteen places are template 0's,
   four fixed, (x-1, y), (x+1 … x-1, y-1), and twelve adaptive */
static const int8_t at_nominal_ext[24] = {
  -2, 0, 0, -2, -2, -1, -1, -2, 1, -2, 2, -1, -3, 0, -4, 0, 2, -2, 3, -1, -2, -2, -3, -1
};
static const uint32_t sltp_cx[4] = { 0x9B25u, 0x0795u, 0x00E5u, 0x0195u };

/* each template with its nominal adaptive pixels: the context taken from
   the rows above a byte at a time (l1 row y-1, l2 row y-2, each pixel's
   bits shifting through), as the standard numbers it. The rows' pixels:
   template 0, y-2 x-2 … x+2 (bits 15 … 11), y-1 x-3 … x+3 (10 … 4), y x-4 … x-1
   (3 … 0); 1, y-2 x-1 … x+2 (12 … 9), y-1 x-2 … x+3 (8 … 3), y x-3 … x-1;
   2, y-2 x-1 … x+1 (9 … 7), y-1 x-2 … x+2 (6 … 2), y x-2, x-1; 3, y-1
   x-3 … x+2 (9 … 4), y x-4 … x-1 */
#define GEN_FAST(name, l2shift, c_init, keep, sh1, b1, sh2, b2)                                                         \
  static void                                                                                                           \
  name(mq_t *m, uint8_t *cx, jbm_t *b, uint32_t rows, bool tpgdon, uint32_t sltp) {                                      \
    const uint32_t w = b->w, stride = b->stride;                                                                        \
    uint32_t       y, ltp = 0u;                                                                                         \
    MQ_LOCALS(m);                                                                                                       \
                                                                                                                        \
    for (y = 0; y < rows && !mq_spent(m); y++) {                                                                        \
      uint8_t       *row = b->p + (size_t)y * stride;                                                                   \
      const uint8_t *r1 = y >= 1u ? row - stride : NULL, *r2 = y >= 2u ? row - 2u * (size_t)stride : NULL;              \
      uint32_t       l1, l2, ctx, k;                                                                                    \
                                                                                                                        \
      if (tpgdon) {                                                                                                     \
        uint32_t bit;                                                                                                   \
                                                                                                                        \
        MQ_DEC(m, bit, &cx[sltp]);                                                                                      \
        ltp ^= bit;                                                                                                     \
        if (ltp) {                                                                                                      \
          if (r1) memcpy(row, r1, stride);                                                                              \
          continue;                                                                                                     \
        }                                                                                                               \
      }                                                                                                                 \
      l1  = r1 ? r1[0] : 0u;                                                                                            \
      l2  = r2 ? (uint32_t)r2[0] << (l2shift) : 0u;                                                                     \
      ctx = c_init;                                                                                                     \
      for (k = 0; k < stride; k++) {                                                                                    \
        uint32_t res = 0u, n = w - 8u * k < 8u ? w - 8u * k : 8u, xm;                                                   \
                                                                                                                        \
        if (r1) l1 = l1 << 8 | (k + 1u < stride ? r1[k + 1u] : 0u);                                                     \
        if (r2) l2 = l2 << 8 | (k + 1u < stride ? (uint32_t)r2[k + 1u] << (l2shift) : 0u);                              \
        for (xm = 0; xm < n; xm++) {                                                                                    \
          uint32_t bit;                                                                                                 \
                                                                                                                        \
          MQ_DEC(m, bit, &cx[ctx]);                                                                                     \
          res |= bit << (7u - xm);                                                                                      \
          ctx  = ((ctx & (keep)) << 1) | bit | ((l1 >> ((sh1) - xm)) & (b1)) | ((l2 >> ((sh2) - xm)) & (b2));           \
        }                                                                                                               \
        row[k] = (uint8_t)res;                                                                                          \
      }                                                                                                                 \
    }                                                                                                                   \
    MQ_SAVE(m);                                                                                                         \
  }

/* template 0, nominal: as GEN_FAST, and white space taken a byte at a
   time. Where the context is 0 and white its more probable symbol, every
   whole byte ahead the rows above leave white (and the 3 and 2 pixels
   past, the template's reach) decodes as MPS steps that renormalize
   nothing while A stays ≥ 0x8000 and C's high half ≥ Qe: k of them are
   A - kQe and C - kQe, the context's state unchanged */
static void
gen_t0(mq_t *m, uint8_t *cx, jbm_t *b, uint32_t rows, bool tpgdon, uint32_t sltp) {
  const uint32_t w = b->w, stride = b->stride, full = w / 8u;
  uint32_t       y, ltp = 0u;
  MQ_LOCALS(m);

  for (y = 0; y < rows && !mq_spent(m); y++) {
    uint8_t       *row = b->p + (size_t)y * stride;
    const uint8_t *r1 = y >= 1u ? row - stride : NULL, *r2 = y >= 2u ? row - 2u * (size_t)stride : NULL;
    uint32_t       l1, l2, ctx, k;

    if (tpgdon) {
      uint32_t bit;

      MQ_DEC(m, bit, &cx[sltp]);
      ltp ^= bit;
      if (ltp) {
        if (r1) memcpy(row, r1, stride);
        continue;
      }
    }
    l1  = r1 ? r1[0] : 0u;
    l2  = r2 ? (uint32_t)r2[0] << 6 : 0u;
    ctx = (l1 & 0x7F0u) | (l2 & 0xF800u);
    for (k = 0; k < stride; k++) {
      uint32_t res = 0u, n = w - 8u * k < 8u ? w - 8u * k : 8u, xm;

      if (!ctx && !(cx[0] & 1u) && k < full) {
        uint32_t qe = mqt[cx[0]].qe;

        if (mq_a - 0x8000u >= 8u * qe && (mq_c >> 16) >= 8u * qe) {
          uint32_t run = 0u, most = (mq_a - 0x8000u) / qe, c2 = (mq_c >> 16) / qe;

          if (c2 < most) most = c2;
          most /= 8u;
          while (run < most && k + run < full
                 && (!r1 || (!r1[k + run] && (k + run + 1u >= stride || !(r1[k + run + 1u] & 0xE0u))))
                 && (!r2 || (!r2[k + run] && (k + run + 1u >= stride || !(r2[k + run + 1u] & 0xC0u))))) {
            run++;
          }
          if (run) {
            /* the row's bytes there stay 0 (a new bitmap's) */
            mq_a -= 8u * run * qe;
            mq_c -= (8u * run * qe) << 16;
            k += run;
            if (k >= stride) break;
            n   = w - 8u * k < 8u ? w - 8u * k : 8u;
            l1  = r1 ? r1[k] : 0u;
            l2  = r2 ? (uint32_t)r2[k] << 6 : 0u;
            ctx = (l1 & 0x7F0u) | (l2 & 0xF800u);
          }
        }
      }
      if (r1) l1 = l1 << 8 | (k + 1u < stride ? r1[k + 1u] : 0u);
      if (r2) l2 = l2 << 8 | (k + 1u < stride ? (uint32_t)r2[k + 1u] << 6 : 0u);
      for (xm = 0; xm < n; xm++) {
        uint32_t bit;

        MQ_DEC(m, bit, &cx[ctx]);
        res |= bit << (7u - xm);
        ctx  = ((ctx & 0x7BF7u) << 1) | bit | ((l1 >> (7u - xm)) & 0x10u) | ((l2 >> (7u - xm)) & 0x800u);
      }
      row[k] = (uint8_t)res;
    }
  }
  MQ_SAVE(m);
}
GEN_FAST(gen_t1, 5, ((l1 >> 1) & 0x1F8u) | ((l2 >> 1) & 0x1E00u), 0xEFBu, 8u, 0x8u, 8u, 0x200u)
GEN_FAST(gen_t2, 4, ((l1 >> 3) & 0x7Cu) | ((l2 >> 3) & 0x380u), 0x1BDu, 10u, 0x4u, 10u, 0x80u)
GEN_FAST(gen_t3, 0, (l1 >> 1) & 0x3F0u, 0x1F7u, 8u, 0x10u, 31u, 0u)
#undef GEN_FAST

/* any adaptive pixels, a skip mask: the nominal context's rows as above
   with the adaptive pixels' places (their nominal ones) taken from where
   they are; each pixel stored as it's decoded (an adaptive pixel may be
   on the row's left). The extended template's pixels numbered as template
   0's places they nominally take (Amd. 2 gives no order of its own: so
   its nominal context is template 0's, and the typical prediction's
   context the same pattern) */
static void
gen_any(mq_t *m, uint8_t *cx, jbm_t *b, const gen_t *g) {
  static const uint32_t at_bit[4][4] = { { 4, 10, 11, 15 }, { 3 }, { 2 }, { 4 } };
  static const uint32_t at_ext[12]   = { 1, 13, 9, 14, 12, 5, 2, 3, 11, 4, 15, 10 };
  static const uint32_t nat[4]       = { 4, 1, 1, 1 };
  const uint32_t        w = b->w, stride = b->stride, k3 = g->templ & 3u, na = g->ext ? 12u : nat[k3];
  const uint32_t       *ab = g->ext ? at_ext : at_bit[k3];
  const jbm_t          *skip = g->skip && g->skip->w == w && g->skip->h >= g->rows ? g->skip : NULL;
  uint32_t              y, ltp = 0u, atmask = 0u, i;

  for (i = 0; i < na; i++) atmask |= 1u << ab[i];
  for (y = 0; y < g->rows && !mq_spent(m); y++) {
    uint8_t       *row = b->p + (size_t)y * stride;
    const uint8_t *r1 = y >= 1u ? row - stride : NULL, *r2 = y >= 2u ? row - 2u * (size_t)stride : NULL;
    uint32_t       l1, l2, ctx = 0u, k;

    if (g->tpgdon) {
      ltp ^= mq_bit(m, &cx[sltp_cx[k3]]);
      if (ltp) {
        if (r1) memcpy(row, r1, stride);
        continue;
      }
    }
    l1 = r1 ? r1[0] : 0u;
    switch (k3) {
      case 0:  l2 = r2 ? (uint32_t)r2[0] << 6 : 0u; ctx = (l1 & 0x7F0u) | (l2 & 0xF800u); break;
      case 1:  l2 = r2 ? (uint32_t)r2[0] << 5 : 0u; ctx = ((l1 >> 1) & 0x1F8u) | ((l2 >> 1) & 0x1E00u); break;
      case 2:  l2 = r2 ? (uint32_t)r2[0] << 4 : 0u; ctx = ((l1 >> 3) & 0x7Cu) | ((l2 >> 3) & 0x380u); break;
      default: l2 = 0u; ctx = (l1 >> 1) & 0x3F0u; break;
    }
    for (k = 0; k < stride; k++) {
      uint32_t n = w - 8u * k < 8u ? w - 8u * k : 8u, xm;

      if (r1) l1 = l1 << 8 | (k + 1u < stride ? r1[k + 1u] : 0u);
      if (r2) {
        uint32_t sh = k3 == 0 ? 6u : k3 == 1 ? 5u : 4u;

        l2 = l2 << 8 | (k + 1u < stride ? (uint32_t)r2[k + 1u] << sh : 0u);
      }
      row[k] = 0u;
      for (xm = 0; xm < n; xm++) {
        int64_t  x = (int64_t)8u * k + xm;
        uint32_t c = ctx & ~atmask, bit = 0u;

        for (i = 0; i < na; i++) c |= bm_px(b, x + g->at[2u * i], (int64_t)y + g->at[2u * i + 1u]) << ab[i];
        if (!skip || !bm_px(skip, x, y)) bit = mq_bit(m, &cx[c]);
        row[k] |= (uint8_t)(bit << (7u - xm));
        switch (k3) {
          case 0:  ctx = ((ctx & 0x7BF7u) << 1) | bit | ((l1 >> (7u - xm)) & 0x10u) | ((l2 >> (7u - xm)) & 0x800u); break;
          case 1:  ctx = ((ctx & 0xEFBu) << 1) | bit | ((l1 >> (8u - xm)) & 0x8u) | ((l2 >> (8u - xm)) & 0x200u); break;
          case 2:  ctx = ((ctx & 0x1BDu) << 1) | bit | ((l1 >> (10u - xm)) & 0x4u) | ((l2 >> (10u - xm)) & 0x80u); break;
          default: ctx = ((ctx & 0x1F7u) << 1) | bit | ((l1 >> (8u - xm)) & 0x10u); break;
        }
      }
    }
  }
}

/* a generic region's bitmap: `data` MMR's (its bytes taken to *used), or
   the coder's */
static jbm_t*
gen_decode(jb_t *j, const gen_t *g, mq_t *m, uint8_t *cx, const uint8_t *data, size_t len, size_t *used) {
  jbm_t   *b = bm_new(j, g->w, g->h, 0u);
  uint32_t k3 = g->templ & 3u, n = k3 == 0 ? 8u : 2u;

  if (!b) return NULL;
  if (!g->w || !g->h || !g->rows) return b;
  if (g->mmr) {
    if (ccitt_mmr(data, len, g->w, g->h, b->p, b->stride, used) < 0) return NULL;
    return b;
  }
  if (!g->skip && (g->ext ? !memcmp(g->at, at_nominal_ext, 24u) : !memcmp(g->at, at_nominal[k3], n))) {
    switch (k3) {
      case 0:  gen_t0(m, cx, b, g->rows, g->tpgdon, sltp_cx[0]); break;
      case 1:  gen_t1(m, cx, b, g->rows, g->tpgdon, sltp_cx[1]); break;
      case 2:  gen_t2(m, cx, b, g->rows, g->tpgdon, sltp_cx[2]); break;
      default: gen_t3(m, cx, b, g->rows, g->tpgdon, sltp_cx[3]); break;
    }
  } else {
    gen_any(m, cx, b, g);
  }
  return b;
}

/* -- refinement (6.3) ------------------------------------------------------------ */

typedef struct refine {
  uint32_t     w;
  uint32_t     h;
  uint8_t      templ;
  bool         tpgron;
  int8_t       at[4];
  const jbm_t *ref;
  int32_t      dx;
  int32_t      dy;
} refine_t;

/* the reference's row rr laid on the region's columns: line[i] the pixels
   of columns 8 (i - 1) … 8 (i - 1) + 7 (the reference's, dx to the left) */
static void
ref_line(uint8_t *line, uint32_t n, const jbm_t *ref, int64_t rr, int64_t dx) {
  const uint8_t *row = rr >= 0 && rr < (int64_t)ref->h ? ref->p + (size_t)rr * ref->stride : NULL;
  uint32_t       i;

  for (i = 0; i < n; i++) line[i] = (uint8_t)get8(ref, row, (int64_t)8 * ((int64_t)i - 1) - dx);
}

/* contexts as the standard numbers them; template 0: 0 (x-1, y), 1 (x+1,
   y-1), 2 (x, y-1), 3 the first adaptive pixel, 4 … 6 the reference's
   (u+1 … u-1, v+1), 7 … 9 (u+1 … u-1, v), 10, 11 (u+1, u, v-1), 12 the
   second; template 1: 0 (x-1, y), 1 … 3 (x+1 … x-1, y-1), 4, 5 (u+1, u,
   v+1), 6 … 8 (u+1 … u-1, v), 9 (u, v-1). Each line's pixels shift along
   through the context, the next ones from 24-bit windows of the lines */
static jbm_t*
refine_decode(jb_t *j, const refine_t *r, mq_t *m, uint8_t *cx) {
  jbm_t   *b = bm_new(j, r->w, r->h, 0u);
  uint32_t y, ltp = 0u, nl, stride;
  uint8_t *lm, *l0, *lp;
  bool     t1 = r->templ & 1u, nominal = t1 || (r->at[0] == -1 && r->at[1] == -1 && r->at[2] == -1 && r->at[3] == -1);
  uint32_t keep = t1 ? 0x1ACu : nominal ? 0x1B6Cu : 0xB64u;

  if (!b || !r->ref) return NULL;
  if (!r->w || !r->h) return b;
  stride = b->stride;
  nl     = stride + 3u;
  if (j->nscratch < 3u * (size_t)nl) {
    size_t want = 3u * (size_t)nl > 4096u ? 3u * (size_t)nl : 4096u;

    if (!(j->scratch = jb_alloc(j, want))) return NULL;
    j->nscratch = want;
  }
  lm = j->scratch;
  l0 = lm + nl;
  lp = l0 + nl;
  ref_line(lm, nl, r->ref, -(int64_t)r->dy - 1, r->dx);
  ref_line(l0, nl, r->ref, -(int64_t)r->dy, r->dx);
  ref_line(lp, nl, r->ref, -(int64_t)r->dy + 1, r->dx);
  {
    MQ_LOCALS(m);

    for (y = 0; y < r->h && !mq_spent(m); y++) {
      uint8_t       *row = b->p + (size_t)y * stride, *t;
      const uint8_t *r1  = y ? row - stride : NULL;
      uint32_t       ctx = 0u, k;

      if (r->tpgron) {
        uint32_t bit;

        MQ_DEC(m, bit, &cx[t1 ? 0x0040u : 0x0100u]);
        ltp ^= bit;
      }
      for (k = 0; k < stride; k++) {
        uint32_t n = r->w - 8u * k < 8u ? r->w - 8u * k : 8u, xm;
        uint32_t cw = r1 ? ((uint32_t)r1[k] << 8 | (k + 1u < stride ? r1[k + 1u] : 0u)) : 0u;
        uint32_t wm = (uint32_t)lm[k] << 16 | (uint32_t)lm[k + 1u] << 8 | lm[k + 2u];
        uint32_t w0 = (uint32_t)l0[k] << 16 | (uint32_t)l0[k + 1u] << 8 | l0[k + 2u];
        uint32_t wp = (uint32_t)lp[k] << 16 | (uint32_t)lp[k + 1u] << 8 | lp[k + 2u];
        uint32_t res = 0u;

        /* a window's column x + d at bit 15 - d (shifted left by xm) */
        if (!k) {
          if (t1) {
            ctx = ((cw >> 13) & 0x6u) | ((wp >> 10) & 0x30u) | ((w0 >> 8) & 0x1C0u) | ((wm >> 6) & 0x200u);
          } else {
            ctx = ((cw >> 13) & 0x6u) | ((wp >> 10) & 0x70u) | ((w0 >> 7) & 0x380u)
                | ((wm >> 4) & (nominal ? 0x1C00u : 0xC00u));
          }
        }
        for (xm = 0; xm < n; xm++) {
          int64_t  x = (int64_t)8u * k + xm;
          uint32_t c = ctx, bit = 0u, cws = cw << xm, wms = wm << xm, w0s = w0 << xm, wps = wp << xm;
          bool     done = false;

          if (!nominal) {
            c |= bm_px(b, x + r->at[0], (int64_t)y + r->at[1]) << 3;
            c |= bm_px(r->ref, x - r->dx + r->at[2], (int64_t)y - r->dy + r->at[3]) << 12;
          }
          if (ltp) {
            uint32_t gm = (wms >> 14) & 7u, g0 = (w0s >> 14) & 7u, gp = (wps >> 14) & 7u;

            if (!(gm | g0 | gp)) {
              bit  = 0u;
              done = true;
            } else if ((gm & g0 & gp) == 7u) {
              bit  = 1u;
              done = true;
            }
          }
          if (!done) MQ_DEC(m, bit, &cx[c]);
          res |= bit << (7u - xm);
          row[k] = (uint8_t)res;
          if (t1) {
            ctx = ((ctx << 1) & keep) | bit | ((cws >> 12) & 0x2u) | ((wps >> 9) & 0x10u) | ((w0s >> 7) & 0x40u)
                | ((wms >> 5) & 0x200u);
          } else {
            ctx = ((ctx << 1) & keep) | bit | ((cws >> 12) & 0x2u) | ((wps >> 9) & 0x10u) | ((w0s >> 6) & 0x80u)
                | ((wms >> 3) & 0x400u);
          }
        }
      }
      t  = lm;
      lm = l0;
      l0 = lp;
      lp = t;
      ref_line(lp, nl, r->ref, (int64_t)y + 2 - r->dy, r->dx);
    }
    MQ_SAVE(m);
  }
  return b;
}

/* -- text regions (6.4) ------------------------------------------------------------ */

typedef struct text {
  uint32_t      w;
  uint32_t      h;
  bool          huff;
  bool          refine;
  uint32_t      strips;     /* SBSTRIPS */
  uint32_t      corner;     /* 0 bottom left, 1 top left, 2 bottom right, 3 top right */
  bool          transposed;
  uint32_t      op;
  uint32_t      defpixel;
  int32_t       dsoffset;
  uint8_t       rtempl;
  int8_t        rat[4];
  uint32_t      instances;
  jbm_t       **syms;
  uint32_t      nsyms;
  uint32_t      codelen;
  const huff_t *fs, *ds, *dt, *rdw, *rdh, *rdx, *rdy, *rsize;
  const huff_t *ids;        /* the symbol IDs' own table */
} text_t;

typedef struct text_cx {
  iax_t    iadt, iafs, iads, iait, iari, iardw, iardh, iardx, iardy;
  uint8_t *iaid; /* 2 ^ codelen */
  uint8_t *gr;   /* refinement's 2 ^ 13 */
} text_cx_t;

static bool
text_cx_init(jb_t *j, text_cx_t *c, uint32_t codelen) {
  memset(c, 0, sizeof(*c));
  return codelen <= 24u && (c->iaid = jb_alloc(j, ((size_t)1 << codelen) + 2u)) && (c->gr = jb_alloc(j, 1u << 13));
}

/* the most instances or symbols data can hold: far past what streams
   have (an instance takes a few bits; arithmetic coding of the same one
   again and again, a few hundredths of one), so a damaged count's
   decoding on past the data stops */
static uint64_t
data_holds(bool huff, size_t len) {
  return huff ? (uint64_t)len * 8u + 64u : (uint64_t)len * 256u + 1024u;
}

static jbm_t*
text_decode(jb_t *j, const text_t *t, mq_t *m, text_cx_t *c, jrd_t *hr) {
  jbm_t   *b = bm_new(j, t->w, t->h, t->defpixel);
  int64_t  stript, firsts = 0;
  uint32_t n = 0u, sbits = 0u, instances = t->instances;
  int32_t  v;
  uint64_t most = data_holds(t->huff, t->huff ? hr->len - hr->at : m->len);

  if (!b) return NULL;
  if (instances > most) instances = (uint32_t)most;
  while ((1u << sbits) < t->strips) sbits++;
  /* the first strip's T, negated */
  if (t->huff ? !huff_decode(hr, t->dt, &v) : !ia_int(m, &c->iadt, &v)) return b;
  stript = -(int64_t)v * t->strips;
  while (n < instances && !(!t->huff && mq_spent(m)) && !(t->huff && hr->bad)) {
    int64_t curs = 0;
    bool    first = true;

    if (t->huff ? !huff_decode(hr, t->dt, &v) : !ia_int(m, &c->iadt, &v)) break;
    stript += (int64_t)v * t->strips;
    for (;;) {
      int64_t      curt, x, y, tt;
      uint32_t     id, ri = 0u;
      const jbm_t *ib;

      if (first) {
        if (t->huff ? !huff_decode(hr, t->fs, &v) : !ia_int(m, &c->iafs, &v)) return b;
        firsts += v;
        curs    = firsts;
        first   = false;
      } else {
        /* the strip's end is an OOB, the last instance's too: read, or what follows is read out of step */
        if (t->huff ? !huff_decode(hr, t->ds, &v) : !ia_int(m, &c->iads, &v)) break;
        if (n >= instances || (!t->huff && mq_spent(m))) break;
        curs += v + t->dsoffset;
      }
      if (t->strips == 1u) {
        curt = 0;
      } else if (t->huff) {
        curt = rd_bits(hr, sbits);
      } else {
        if (!ia_int(m, &c->iait, &v)) return b;
        curt = v;
      }
      tt = stript + curt;
      if (t->huff) {
        if (t->ids) {
          if (!huff_decode(hr, t->ids, &v) || v < 0) return b;
          id = (uint32_t)v;
        } else {
          id = rd_bits(hr, t->codelen);
        }
      } else {
        id = ia_id(m, c->iaid, t->codelen);
      }
      if (id >= t->nsyms || !t->syms[id]) return b;
      ib = t->syms[id];
      if (t->refine) {
        if (t->huff) ri = rd_bits(hr, 1u);
        else if (!ia_int(m, &c->iari, &v)) return b;
        else ri = v != 0;
      }
      if (ri) {
        int32_t  rdw, rdh, rdx, rdy, size = 0;
        refine_t rf;

        if (t->huff) {
          if (!huff_decode(hr, t->rdw, &rdw) || !huff_decode(hr, t->rdh, &rdh) || !huff_decode(hr, t->rdx, &rdx)
              || !huff_decode(hr, t->rdy, &rdy) || !huff_decode(hr, t->rsize, &size)) {
            return b;
          }
          rd_align(hr);
        } else if (!ia_int(m, &c->iardw, &rdw) || !ia_int(m, &c->iardh, &rdh) || !ia_int(m, &c->iardx, &rdx)
                   || !ia_int(m, &c->iardy, &rdy)) {
          return b;
        }
        if ((int64_t)ib->w + rdw < 0 || (int64_t)ib->h + rdh < 0) return b;
        rf.w      = (uint32_t)((int64_t)ib->w + rdw);
        rf.h      = (uint32_t)((int64_t)ib->h + rdh);
        rf.templ  = t->rtempl;
        rf.tpgron = false;
        memcpy(rf.at, t->rat, 4);
        rf.ref = ib;
        rf.dx  = (int32_t)(((int64_t)rdw >> 1) + rdx); /* ⌊RDW / 2⌋ + RDX: the shift floors */
        rf.dy  = (int32_t)(((int64_t)rdh >> 1) + rdy);
        if (t->huff) {
          /* its own arithmetic data, `size` bytes, then Huffman again */
          mq_t rm;

          if (size < 0 || hr->at > hr->len || (size_t)size > hr->len - hr->at) return b;
          mq_init(&rm, hr->p + hr->at, (size_t)size);
          ib = refine_decode(j, &rf, &rm, c->gr); /* the region's contexts go on */
          hr->at += (size_t)size;
        } else {
          ib = refine_decode(j, &rf, m, c->gr);
        }
        if (!ib) return b;
      }
      /* placed by its corner (S along the strip, T across it; transposed, S down) */
      if (!t->transposed && t->corner > 1u) curs += (int64_t)ib->w - 1;
      else if (t->transposed && !(t->corner & 1u)) curs += (int64_t)ib->h - 1;
      x = t->transposed ? tt : curs;
      y = t->transposed ? curs : tt;
      if (t->corner > 1u) x -= (int64_t)ib->w - 1;    /* the right corners */
      if (!(t->corner & 1u)) y -= (int64_t)ib->h - 1; /* the bottom ones */
      if (!bm_draw(j, b, ib, x, y, t->op)) return b;
      if (!t->transposed && t->corner < 2u) curs += (int64_t)ib->w - 1;
      else if (t->transposed && (t->corner & 1u)) curs += (int64_t)ib->h - 1;
      n++;
    }
  }
  return b;
}

/* -- segments' parts ------------------------------------------------------------- */

typedef struct region_info {
  uint32_t w;
  uint32_t h;
  int32_t  x;
  int32_t  y;
  uint32_t op;
} region_info_t;

static bool
read_region_info(jrd_t *r, region_info_t *ri) {
  ri->w  = rd_u(r, 4u);
  ri->h  = rd_u(r, 4u);
  ri->x  = (int32_t)rd_u(r, 4u);
  ri->y  = (int32_t)rd_u(r, 4u);
  ri->op = rd_u(r, 1u) & 7u;
  return !r->bad && ri->w <= JB_MAX_SIDE && (ri->h <= JB_MAX_SIDE || ri->h == 0xFFFFFFFFu) && ri->op <= 4u;
}

static seg_t*
find_seg(jb_t *j, uint32_t number) {
  uint32_t i;

  for (i = 0; i < j->nsegs; i++) {
    if (j->segs[i].number == number) return &j->segs[i];
  }
  return NULL;
}

/* the symbols of the dictionaries a segment refers to, in order */
static jbm_t**
referred_symbols(jb_t *j, const seg_t *s, uint32_t extra, uint32_t *count) {
  uint32_t i, k = 0u;
  uint64_t n = 0u;
  jbm_t  **all;

  for (i = 0; i < s->nrefs; i++) {
    seg_t *r = find_seg(j, s->refs[i]);

    if (r && r->type == 0u) n += r->nsyms;
  }
  if (n + extra > JB_MAX_SYMS || !(all = jb_alloc(j, ((size_t)n + extra + 1u) * sizeof(*all)))) return NULL;
  for (i = 0; i < s->nrefs; i++) {
    seg_t *r = find_seg(j, s->refs[i]);

    if (r && r->type == 0u && r->nsyms) {
      memcpy(all + k, r->syms, r->nsyms * sizeof(*all));
      k += r->nsyms;
    }
  }
  *count = (uint32_t)n;
  return all;
}

/* the custom tables a segment refers to, the next one each time asked */
typedef struct tables {
  jb_t        *j;
  const seg_t *s;
  uint32_t     next;
} tables_t;

static const huff_t*
next_custom(tables_t *t) {
  for (; t->next < t->s->nrefs; t->next++) {
    seg_t *r = find_seg(t->j, t->s->refs[t->next]);

    if (r && r->type == 53u) {
      t->next++;
      return r->table;
    }
  }
  return NULL;
}

static uint32_t
log2_up(uint32_t n) {
  uint32_t k = 0u;

  while (k < 32u && ((uint64_t)1 << k) < n) k++;
  return k;
}

/* -- symbol dictionaries (6.5, 7.4.2) ---------------------------------------------- */

static bool
symbol_dictionary(jb_t *j, seg_t *s) {
  jrd_t      r = { s->data, s->len, 0, 0, false };
  uint32_t   flags = rd_u(&r, 2u), i;
  bool       huff = flags & 1u, refagg = (flags >> 1) & 1u, cx_used = (flags >> 8) & 1u;
  uint8_t    templ = (flags >> 10) & 3u, rtempl = (flags >> 12) & 1u;
  int8_t     at[8] = { 3, -1, -3, -1, 2, -2, -2, -2 }, rat[4] = { -1, -1, -1, -1 };
  uint32_t   exported, fresh, in_count = 0u, total, codelen;
  jbm_t    **syms;
  uint32_t  *widths = NULL;
  huff_t    *hdh = NULL, *hdw = NULL, *hbmsize = NULL, *haggin = NULL, *t1 = NULL, *t15 = NULL;
  tables_t   custom = { j, s, 0u };
  mq_t       m;
  uint8_t   *gb = NULL, *exflags;
  iax_t     *iadh = NULL, *iadw = NULL, *iaex = NULL, *iaai = NULL;
  text_cx_t  tcx;
  uint32_t   n = 0u, hcheight = 0u;

  s->retain_cx = (flags >> 9) & 1u;
  if (!huff) {
    for (i = 0; i < (templ == 0u ? 8u : 2u); i++) at[i] = (int8_t)rd_s8(&r);
  }
  if (refagg && rtempl == 0u) {
    for (i = 0; i < 4u; i++) rat[i] = (int8_t)rd_s8(&r);
  }
  exported = rd_u(&r, 4u);
  fresh    = rd_u(&r, 4u);
  if (r.bad || fresh > JB_MAX_SYMS || exported > JB_MAX_SYMS || fresh > data_holds(huff, r.len - r.at)) return false;
  if (!(syms = referred_symbols(j, s, fresh, &in_count))) return false; /* the input symbols, then the new ones */
  total   = in_count + fresh;
  codelen = log2_up(total);

  if (huff) {
    uint32_t dh = (flags >> 2) & 3u, dw = (flags >> 4) & 3u;

    hdh     = dh == 0u ? huff_std(j, 4) : dh == 1u ? huff_std(j, 5) : (huff_t *)next_custom(&custom);
    hdw     = dw == 0u ? huff_std(j, 2) : dw == 1u ? huff_std(j, 3) : (huff_t *)next_custom(&custom);
    hbmsize = (flags >> 6) & 1u ? (huff_t *)next_custom(&custom) : huff_std(j, 1);
    haggin  = (flags >> 7) & 1u ? (huff_t *)next_custom(&custom) : huff_std(j, 1);
    if (!hdh || !hdw || !hbmsize || !haggin) return false;
    if (!(widths = jb_alloc(j, ((size_t)fresh + 1u) * sizeof(*widths)))) return false;
  } else {
    if (!(gb = jb_alloc(j, 1u << 16)) || !(iadh = jb_alloc(j, 4u * sizeof(iax_t)))) return false;
    iadw = iadh + 1;
    iaex = iadh + 2;
    iaai = iadh + 3;
    mq_init(&m, r.p + r.at, r.len - r.at);
  }
  if (refagg && !text_cx_init(j, &tcx, codelen)) return false;
  /* the contexts the last dictionary referred to that kept them left them in */
  if (!huff && cx_used) {
    for (i = s->nrefs; i-- > 0u;) {
      seg_t *p = find_seg(j, s->refs[i]);

      if (p && p->type == 0u && p->gb_cx) {
        memcpy(gb, p->gb_cx, 1u << 16);
        if (refagg && p->gr_cx) memcpy(tcx.gr, p->gr_cx, 1u << 13);
        break;
      }
    }
  }

  while (n < fresh && !(!huff && mq_spent(&m)) && !r.bad) {
    int32_t  dh, dw;
    uint32_t symwidth = 0u, totwidth = 0u, hcfirst = n;

    if (huff ? !huff_decode(&r, hdh, &dh) : !ia_int(&m, iadh, &dh)) return false;
    if ((int64_t)hcheight + dh < 0 || (int64_t)hcheight + dh > JB_MAX_SIDE) return false;
    hcheight = (uint32_t)((int64_t)hcheight + dh);
    for (;;) {
      jbm_t *b = NULL;

      /* past the data or the effort allowed: a damaged dictionary */
      if ((huff ? r.bad : mq_spent(&m)) || !spend(j, JB_STEP_COST)) return false;
      if (huff ? !huff_decode(&r, hdw, &dw) : !ia_int(&m, iadw, &dw)) break; /* OOB: the class's end */
      if (n >= fresh || (int64_t)symwidth + dw < 0 || (int64_t)symwidth + dw > JB_MAX_SIDE) return false;
      symwidth = (uint32_t)((int64_t)symwidth + dw);
      if ((uint64_t)totwidth + symwidth > JB_MAX_SIDE) return false;
      totwidth += symwidth;
      if (huff && !refagg) {
        widths[n++] = symwidth;
        continue;
      }
      if (!refagg) {
        gen_t g;

        memset(&g, 0, sizeof(g));
        g.w     = symwidth;
        g.h     = hcheight;
        g.rows  = hcheight;
        g.templ = templ;
        memcpy(g.at, at, 8);
        b = gen_decode(j, &g, &m, gb, NULL, 0u, NULL);
      } else {
        int32_t ninst;

        if (huff ? !huff_decode(&r, haggin, &ninst) : !ia_int(&m, iaai, &ninst)) return false;
        if (ninst == 1) {
          /* one symbol refined */
          uint32_t id;
          int32_t  rdx, rdy, size = 0;
          refine_t rf;

          if (huff) {
            if (!t15 && !(t15 = huff_std(j, 15))) return false;
            if (!t1 && !(t1 = huff_std(j, 1))) return false;
            id = rd_bits(&r, codelen);
            if (!huff_decode(&r, t15, &rdx) || !huff_decode(&r, t15, &rdy) || !huff_decode(&r, t1, &size)) return false;
            rd_align(&r);
          } else {
            id = ia_id(&m, tcx.iaid, codelen);
            if (!ia_int(&m, &tcx.iardx, &rdx) || !ia_int(&m, &tcx.iardy, &rdy)) return false;
          }
          if (id >= in_count + n || !syms[id]) return false;
          rf.w      = symwidth;
          rf.h      = hcheight;
          rf.templ  = rtempl;
          rf.tpgron = false;
          memcpy(rf.at, rat, 4);
          rf.ref = syms[id];
          rf.dx  = rdx;
          rf.dy  = rdy;
          if (huff) {
            mq_t rm;

            if (size < 0 || r.at > r.len || (size_t)size > r.len - r.at) return false;
            mq_init(&rm, r.p + r.at, (size_t)size);
            b = refine_decode(j, &rf, &rm, tcx.gr);
            r.at += (size_t)size;
          } else {
            b = refine_decode(j, &rf, &m, tcx.gr);
          }
        } else if (ninst > 1) {
          /* an aggregate: a text region of the symbols so far */
          text_t t;

          memset(&t, 0, sizeof(t));
          t.w         = symwidth;
          t.h         = hcheight;
          t.huff      = huff;
          t.refine    = true;
          t.strips    = 1u;
          t.corner    = 1u;
          t.op        = 0u;
          t.rtempl    = rtempl;
          memcpy(t.rat, rat, 4);
          t.instances = (uint32_t)ninst;
          t.syms      = syms;
          t.nsyms     = in_count + n;
          t.codelen   = codelen;
          if (huff) {
            t.fs    = huff_std(j, 6);
            t.ds    = huff_std(j, 8);
            t.dt    = huff_std(j, 11);
            t.rdw   = t.rdh = t.rdx = t.rdy = huff_std(j, 15);
            t.rsize = huff_std(j, 1);
          }
          b = text_decode(j, &t, &m, &tcx, &r);
        } else {
          return false;
        }
      }
      if (!b) return false;
      syms[in_count + n++] = b;
    }
    if (huff && !refagg) {
      /* the class's symbols side by side: raw, or MMR */
      int32_t  bmsize;
      jbm_t   *all;
      uint32_t x, k;

      if (!huff_decode(&r, hbmsize, &bmsize) || bmsize < 0) return false;
      rd_align(&r);
      if (bmsize == 0) {
        uint32_t stride = (totwidth + 7u) / 8u, y;

        if (!(all = bm_new(j, totwidth, hcheight, 0u)) || r.at > r.len || (uint64_t)stride * hcheight > r.len - r.at) {
          return false;
        }
        for (y = 0; y < hcheight; y++) {
          memcpy(all->p + (size_t)y * all->stride, r.p + r.at + (size_t)y * stride, stride);
          if (totwidth & 7u) all->p[(size_t)y * all->stride + stride - 1u] &= (uint8_t)(0xFF00u >> (totwidth & 7u));
        }
        r.at += (size_t)stride * hcheight;
      } else {
        gen_t g;

        memset(&g, 0, sizeof(g));
        g.w    = totwidth;
        g.h    = hcheight;
        g.rows = hcheight;
        g.mmr  = true;
        if (r.at > r.len || (size_t)bmsize > r.len - r.at || !(all = gen_decode(j, &g, NULL, NULL, r.p + r.at, (size_t)bmsize, NULL))) {
          return false;
        }
        r.at += (size_t)bmsize;
      }
      for (x = 0, k = hcfirst; k < n; k++) {
        jbm_t   *b = bm_new(j, widths[k], hcheight, 0u);
        uint32_t y, q;

        if (!b) return false;
        for (y = 0; y < hcheight; y++) {
          const uint8_t *src = all->p + (size_t)y * all->stride;
          uint8_t       *dst = b->p + (size_t)y * b->stride;

          for (q = 0; q < b->stride; q++) dst[q] = (uint8_t)get8(all, src, (int64_t)x + 8 * (int64_t)q);
          if (b->w & 7u) dst[b->stride - 1u] &= (uint8_t)(0xFF00u >> (b->w & 7u));
        }
        syms[in_count + k] = b;
        x += widths[k];
      }
    }
  }

  /* which are exported: runs, alternately not and so (runs of 0 don't
     move it on: no more than two a symbol) */
  if (!(exflags = jb_alloc(j, (size_t)total + 1u))) return false;
  for (i = 0, n = 0u; i < total;) {
    int32_t  run;
    uint32_t k;

    if (n > 2u * (uint64_t)total + 2u || (!huff && mq_spent(&m))) return false;

    if (huff) {
      if (!t1 && !(t1 = huff_std(j, 1))) return false;
      if (!huff_decode(&r, t1, &run)) return false;
    } else if (!ia_int(&m, iaex, &run)) {
      return false;
    }
    if (run < 0 || (uint32_t)run > total - i) return false;
    for (k = 0; k < (uint32_t)run; k++) exflags[i++] = (uint8_t)(n & 1u);
    n++;
  }
  if (!(s->syms = jb_alloc(j, ((size_t)exported + 1u) * sizeof(*s->syms)))) return false;
  for (i = 0, n = 0u; i < total && n < exported; i++) {
    if (exflags[i]) s->syms[n++] = syms[i];
  }
  s->nsyms = n;
  if (s->retain_cx && !huff) {
    s->gb_cx = gb;
    s->gr_cx = refagg ? tcx.gr : NULL;
  }
  return true;
}

/* -- text region segments (7.4.3) ------------------------------------------------- */

/* the symbol IDs' own Huffman table (7.4.3.1.7) */
static huff_t*
symbol_id_table(jb_t *j, jrd_t *r, uint32_t count) {
  huff_spec_t  runs[35], *spec;
  huff_t      *rt;
  uint32_t     i, n = 0u;
  int32_t      prev = 0;

  for (i = 0; i < 35u; i++) runs[i] = (huff_spec_t){ (int32_t)i, (uint8_t)rd_bits(r, 4u), 0, 0 };
  if (r->bad || !(rt = huff_build(j, runs, 35u)) || !(spec = jb_alloc(j, ((size_t)count + 1u) * sizeof(*spec)))) {
    return NULL;
  }
  while (n < count) {
    int32_t  code, times = 1, len = 0;
    uint32_t k;

    if (!huff_decode(r, rt, &code)) return NULL;
    if (code < 32) {
      len = code;
    } else if (code == 32) {
      if (!n) return NULL;
      len   = prev;
      times = 3 + (int32_t)rd_bits(r, 2u);
    } else if (code == 33) {
      times = 3 + (int32_t)rd_bits(r, 3u);
    } else {
      times = 11 + (int32_t)rd_bits(r, 7u);
    }
    for (k = 0; k < (uint32_t)times && n < count; k++, n++) spec[n] = (huff_spec_t){ (int32_t)n, (uint8_t)len, 0, 0 };
    prev = len;
  }
  rd_align(r);
  return huff_build(j, spec, count);
}

static jbm_t*
text_region(jb_t *j, seg_t *s, region_info_t *ri) {
  jrd_t     r = { s->data, s->len, 0, 0, false };
  text_t    t;
  text_cx_t c;
  uint32_t  flags, hflags = 0u, i;
  tables_t  custom = { j, s, 0u };
  mq_t      m;

  memset(&t, 0, sizeof(t));
  memset(&m, 0, sizeof(m));
  if (!read_region_info(&r, ri) || ri->h == 0xFFFFFFFFu) return NULL;
  flags        = rd_u(&r, 2u);
  t.w          = ri->w;
  t.h          = ri->h;
  t.huff       = flags & 1u;
  t.refine     = (flags >> 1) & 1u;
  t.strips     = 1u << ((flags >> 2) & 3u);
  t.corner     = (flags >> 4) & 3u;
  t.transposed = (flags >> 6) & 1u;
  t.op         = (flags >> 7) & 3u;
  t.defpixel   = (flags >> 9) & 1u;
  t.dsoffset   = (int32_t)((flags >> 10) & 0x1Fu);
  if (t.dsoffset > 15) t.dsoffset -= 32;
  t.rtempl     = (flags >> 15) & 1u;
  t.rat[0] = t.rat[1] = t.rat[2] = t.rat[3] = -1;
  if (t.huff) hflags = rd_u(&r, 2u);
  if (t.refine && !t.rtempl) {
    for (i = 0; i < 4u; i++) t.rat[i] = (int8_t)rd_s8(&r);
  }
  t.instances = rd_u(&r, 4u);
  if (r.bad || !(t.syms = referred_symbols(j, s, 0u, &t.nsyms))) return NULL;
  t.codelen = log2_up(t.nsyms);
  if (t.huff) {
    uint32_t v;

#define PICK(field, shift, a, b, c3)                                                                                    \
    v       = (hflags >> (shift)) & 3u;                                                                                 \
    t.field = v == 0u ? huff_std(j, a) : v == 1u ? huff_std(j, b) : v == 2u && (c3) ? huff_std(j, c3) : next_custom(&custom);
    PICK(fs, 0, 6, 7, 0)
    PICK(ds, 2, 8, 9, 10)
    PICK(dt, 4, 11, 12, 13)
    PICK(rdw, 6, 14, 15, 0)
    PICK(rdh, 8, 14, 15, 0)
    PICK(rdx, 10, 14, 15, 0)
    PICK(rdy, 12, 14, 15, 0)
#undef PICK
    t.rsize = (hflags >> 14) & 1u ? next_custom(&custom) : huff_std(j, 1);
    if (!(t.ids = symbol_id_table(j, &r, t.nsyms))) return NULL;
  } else {
    mq_init(&m, r.p + r.at, r.len - r.at);
  }
  if (!text_cx_init(j, &c, t.codelen)) return NULL;
  return text_decode(j, &t, &m, &c, &r);
}

/* -- pattern dictionaries and halftones (6.6, 6.7) --------------------------------- */

static bool
pattern_dictionary(jb_t *j, seg_t *s) {
  jrd_t    r = { s->data, s->len, 0, 0, false };
  uint32_t flags = rd_u(&r, 1u), pw = rd_u(&r, 1u), ph = rd_u(&r, 1u), graymax = rd_u(&r, 4u), i;
  gen_t    g;
  mq_t     m;
  uint8_t *cx;
  jbm_t   *all;

  if (r.bad || !pw || !ph || graymax >= (1u << 16) || (uint64_t)pw * (graymax + 1u) > JB_MAX_SIDE) return false;
  memset(&g, 0, sizeof(g));
  memset(&m, 0, sizeof(m));
  g.w     = pw * (graymax + 1u);
  g.h     = ph;
  g.rows  = ph;
  g.mmr   = flags & 1u;
  g.templ = (flags >> 1) & 3u;
  g.at[0] = (int8_t)-(int32_t)pw;
  g.at[1] = 0;
  g.at[2] = -3, g.at[3] = -1, g.at[4] = 2, g.at[5] = -2, g.at[6] = -2, g.at[7] = -2;
  if (!(cx = jb_alloc(j, 1u << 16))) return false;
  if (!g.mmr) mq_init(&m, r.p + r.at, r.len - r.at);
  if (!(all = gen_decode(j, &g, &m, cx, r.p + r.at, r.len - r.at, NULL))) return false;
  if (!(s->pats = jb_alloc(j, (graymax + 1u) * sizeof(*s->pats)))) return false;
  for (i = 0; i <= graymax; i++) {
    jbm_t   *p = bm_new(j, pw, ph, 0u);
    uint32_t y, q;

    if (!p) return false;
    for (y = 0; y < ph; y++) {
      const uint8_t *src = all->p + (size_t)y * all->stride;
      uint8_t       *dst = p->p + (size_t)y * p->stride;

      for (q = 0; q < p->stride; q++) dst[q] = (uint8_t)get8(all, src, (int64_t)i * pw + 8 * (int64_t)q);
      if (pw & 7u) dst[p->stride - 1u] &= (uint8_t)(0xFF00u >> (pw & 7u));
    }
    s->pats[i] = p;
  }
  s->npats = graymax + 1u;
  return true;
}

static jbm_t*
halftone_region(jb_t *j, seg_t *s, region_info_t *ri) {
  jrd_t     r = { s->data, s->len, 0, 0, false };
  uint32_t  flags, gw, gh, rx, ry, bpp, i, mm, nn, npats = 0u, op, *gray;
  int32_t   gx, gy;
  jbm_t    *b, *skip = NULL, **planes, **pats = NULL;
  uint8_t  *cx;
  bool      mmr;
  gen_t     g;
  mq_t      m;

  memset(&m, 0, sizeof(m));
  if (!read_region_info(&r, ri) || ri->h == 0xFFFFFFFFu) return NULL;
  flags = rd_u(&r, 1u);
  gw    = rd_u(&r, 4u);
  gh    = rd_u(&r, 4u);
  gx    = (int32_t)rd_u(&r, 4u);
  gy    = (int32_t)rd_u(&r, 4u);
  rx    = rd_u(&r, 2u);
  ry    = rd_u(&r, 2u);
  mmr   = flags & 1u;
  op    = (flags >> 4) & 7u;
  if (r.bad || gw > JB_MAX_SIDE || gh > JB_MAX_SIDE || (uint64_t)gw * gh > ((uint64_t)1 << 28) || op > 4u) return NULL;
  for (i = 0; i < s->nrefs && !pats; i++) {
    seg_t *p = find_seg(j, s->refs[i]);

    if (p && p->type == 16u) {
      pats  = p->pats;
      npats = p->npats;
    }
  }
  if (!pats || !npats || !(b = bm_new(j, ri->w, ri->h, (flags >> 7) & 1u))) return NULL;
  bpp = log2_up(npats);

  /* the grid's cells off the region skipped */
  if ((flags >> 3) & 1u) {
    if (!(skip = bm_new(j, gw, gh, 0u))) return NULL;
    for (mm = 0; mm < gh; mm++) {
      for (nn = 0; nn < gw; nn++) {
        int64_t x = ((int64_t)gx + (int64_t)mm * ry + (int64_t)nn * rx) >> 8;
        int64_t y = ((int64_t)gy + (int64_t)mm * rx - (int64_t)nn * ry) >> 8;

        if (x + pats[0]->w <= 0 || x >= (int64_t)ri->w || y + pats[0]->h <= 0 || y >= (int64_t)ri->h) {
          skip->p[(size_t)mm * skip->stride + nn / 8u] |= (uint8_t)(0x80u >> (nn & 7u));
        }
      }
    }
  }

  /* the gray values: bit planes, Gray-coded, most significant first (C.5) */
  memset(&g, 0, sizeof(g));
  g.w     = gw;
  g.h     = gh;
  g.rows  = gh;
  g.mmr   = mmr;
  g.templ = (flags >> 1) & 3u;
  g.skip  = skip;
  g.at[0] = g.templ <= 1u ? 3 : 2, g.at[1] = -1, g.at[2] = -3, g.at[3] = -1, g.at[4] = 2, g.at[5] = -2, g.at[6] = -2;
  g.at[7] = -2;
  if (!(planes = jb_alloc(j, ((size_t)bpp + 1u) * sizeof(*planes))) || !(cx = jb_alloc(j, 1u << 16))
      || !(gray = jb_alloc(j, ((size_t)gw * gh + 1u) * sizeof(*gray)))) {
    return NULL;
  }
  if (!mmr) mq_init(&m, r.p + r.at, r.len - r.at);
  for (i = bpp; i-- > 0u;) {
    if (mmr) {
      /* each plane its own MMR data, to its EOFB */
      size_t used = 0u;

      if (!(planes[i] = gen_decode(j, &g, NULL, NULL, r.p + r.at, r.len - r.at, &used))) return NULL;
      r.at += used;
    } else if (!(planes[i] = gen_decode(j, &g, &m, cx, NULL, 0u, NULL))) {
      return NULL;
    }
    if (i + 1u < bpp) {
      size_t k, nbytes = (size_t)planes[i]->stride * gh;

      for (k = 0; k < nbytes; k++) planes[i]->p[k] ^= planes[i + 1u]->p[k];
    }
  }
  for (i = 0; i < bpp; i++) {
    for (mm = 0; mm < gh; mm++) {
      const uint8_t *pr = planes[i]->p + (size_t)mm * planes[i]->stride;

      for (nn = 0; nn < gw; nn++) gray[(size_t)mm * gw + nn] |= (uint32_t)((pr[nn >> 3] >> (7u - (nn & 7u))) & 1u) << i;
    }
  }
  for (mm = 0; mm < gh; mm++) {
    for (nn = 0; nn < gw; nn++) {
      int64_t  x = ((int64_t)gx + (int64_t)mm * ry + (int64_t)nn * rx) >> 8;
      int64_t  y = ((int64_t)gy + (int64_t)mm * rx - (int64_t)nn * ry) >> 8;
      uint32_t v = gray[(size_t)mm * gw + nn];

      if (skip && bm_px(skip, nn, mm)) continue;
      if (!bm_draw(j, b, pats[v < npats ? v : npats - 1u], x, y, op)) return b;
    }
  }
  return b;
}

/* -- regions to the page ------------------------------------------------------------- */

/* a page of unknown height grows as regions reach down */
static void
page_reach(jb_t *j, int64_t bottom) {
  jbm_t *grown;

  if (!j->page || !j->page_grows || bottom <= (int64_t)j->page->h || bottom > JB_MAX_SIDE) return;
  if ((grown = bm_new(j, j->page->w, (uint32_t)bottom, j->page_default))) {
    memcpy(grown->p, j->page->p, (size_t)j->page->stride * j->page->h);
    j->page = grown;
  }
}

static void
page_place(jb_t *j, jbm_t *b, const region_info_t *ri) {
  if (!j->page || !b) return;
  page_reach(j, (int64_t)ri->y + b->h);
  /* the whole page, nothing on it yet, an operator that gives the region
     there: the region is the page (a page coded as one region) */
  if (j->page_fresh && !ri->x && !ri->y && b->w == j->page->w && b->h == j->page->h
      && (ri->op == 4u || (!j->page_default && (ri->op == 0u || ri->op == 2u)) || (j->page_default && (ri->op == 1u || ri->op == 3u)))) {
    j->page       = b;
    j->page_fresh = false;
    (void)spend(j, JB_STEP_COST + (uint64_t)b->w * b->h);
    return;
  }
  j->page_fresh = false;
  (void)bm_draw(j, j->page, b, ri->x, ri->y, ri->op);
}

/* a region off the part wanted, where nothing after reads the page */
static bool
off_cut(const jb_t *j, int64_t x, int64_t y, int64_t w, int64_t h) {
  return j->cut && (x >= j->cx1 || y >= j->cy1 || x + w <= j->cx0 || y + h <= j->cy0);
}

static jbm_t*
generic_region(jb_t *j, seg_t *s, region_info_t *ri, bool immediate) {
  jrd_t    r = { s->data, s->len, 0, 0, false };
  gen_t    g;
  uint32_t flags, i;
  uint8_t *cx;
  mq_t     m;

  memset(&g, 0, sizeof(g));
  memset(&m, 0, sizeof(m));
  if (!read_region_info(&r, ri)) return NULL;
  flags    = rd_u(&r, 1u);
  g.mmr    = flags & 1u;
  g.templ  = (flags >> 1) & 3u;
  g.tpgdon = (flags >> 3) & 1u;
  g.ext    = (flags >> 4) & 1u && !g.mmr && g.templ == 0u; /* else unused (Table 2) */
  if (!g.mmr) {
    for (i = 0; i < (g.ext ? 24u : g.templ == 0u ? 8u : 2u); i++) g.at[i] = (int8_t)rd_s8(&r);
  }
  if (r.bad) return NULL;
  g.w = ri->w;
  g.h = ri->h;
  if (g.h == 0xFFFFFFFFu) {
    /* unknown: the row count ends the data (7.4.6.4) */
    if (s->len < r.at + 4u) return NULL;
    g.h = (uint32_t)s->data[s->len - 4u] << 24 | (uint32_t)s->data[s->len - 3u] << 16
        | (uint32_t)s->data[s->len - 2u] << 8 | s->data[s->len - 1u];
    ri->h = g.h;
    if (g.h > JB_MAX_SIDE) return NULL;
  }
  g.rows = g.h;
  if (immediate && off_cut(j, ri->x, ri->y, g.w, g.h)) {
    page_reach(j, (int64_t)ri->y + g.h); /* left out, the page as tall as with it */
    return NULL;
  }
  if (immediate && j->cut && !g.mmr && (int64_t)ri->y + g.h > j->cy1) {
    /* the rows below the part wanted: not needed */
    g.rows = j->cy1 - ri->y > 0 ? (uint32_t)(j->cy1 - ri->y) : 0u;
  }
  if (!(cx = jb_alloc(j, 1u << 16))) return NULL;
  if (!g.mmr) mq_init(&m, r.p + r.at, r.len - r.at);
  return gen_decode(j, &g, &m, cx, r.p + r.at, r.len - r.at, NULL);
}

static jbm_t*
refinement_region(jb_t *j, seg_t *s, region_info_t *ri) {
  jrd_t    r = { s->data, s->len, 0, 0, false };
  refine_t f;
  uint32_t flags, i;
  uint8_t *cx;
  mq_t     m;
  jbm_t   *ref = NULL;

  memset(&f, 0, sizeof(f));
  if (!read_region_info(&r, ri) || ri->h == 0xFFFFFFFFu) return NULL;
  flags    = rd_u(&r, 1u);
  f.templ  = flags & 1u;
  f.tpgron = (flags >> 1) & 1u;
  if (!f.templ) {
    for (i = 0; i < 4u; i++) f.at[i] = (int8_t)rd_s8(&r);
  }
  if (r.bad) return NULL;
  /* its reference: the intermediate region it refers to, or the page there */
  for (i = 0; i < s->nrefs && !ref; i++) {
    seg_t *p = find_seg(j, s->refs[i]);

    if (p && p->region) ref = p->region;
  }
  if (!ref && j->page) {
    uint32_t y, q;

    if (!(ref = bm_new(j, ri->w, ri->h, 0u))) return NULL;
    for (y = 0; y < ri->h; y++) {
      int64_t        py  = (int64_t)ri->y + y;
      const uint8_t *src = py >= 0 && py < (int64_t)j->page->h ? j->page->p + (size_t)py * j->page->stride : NULL;
      uint8_t       *dst = ref->p + (size_t)y * ref->stride;

      for (q = 0; q < ref->stride; q++) dst[q] = (uint8_t)get8(j->page, src, (int64_t)ri->x + 8 * (int64_t)q);
      if (ri->w & 7u) dst[ref->stride - 1u] &= (uint8_t)(0xFF00u >> (ri->w & 7u));
    }
  }
  if (!ref || !(cx = jb_alloc(j, 1u << 13))) return NULL;
  f.w   = ri->w;
  f.h   = ri->h;
  f.ref = ref;
  mq_init(&m, r.p + r.at, r.len - r.at);
  return refine_decode(j, &f, &m, cx);
}

/* -- the stream ------------------------------------------------------------------------ */

/* the segments' headers and their data (7.2): sequential, or (a file's
   random-access organisation) all headers first */
static bool
read_segments(jb_t *j, const uint8_t *p, size_t len, bool headers_first) {
  jrd_t    r = { p, len, 0, 0, false };
  uint32_t first = j->nsegs, i;
  size_t   data_at;

  while (r.at < r.len && !r.bad) {
    seg_t   *s;
    uint32_t flags, count, sz, k;

    if (j->nsegs == j->capsegs) {
      seg_t   *grown;
      uint32_t cap = j->capsegs ? j->capsegs * 2u : 64u;

      if (cap > JB_MAX_SEGS || !(grown = jb_alloc(j, cap * sizeof(*grown)))) return false;
      if (j->nsegs) memcpy(grown, j->segs, j->nsegs * sizeof(*grown));
      j->segs    = grown;
      j->capsegs = cap;
    }
    s = &j->segs[j->nsegs];
    memset(s, 0, sizeof(*s));
    s->number = rd_u(&r, 4u);
    flags     = rd_u(&r, 1u);
    s->type   = flags & 0x3Fu;
    count     = rd_u(&r, 1u);
    if ((count >> 5) == 7u) {
      r.at--;
      count = rd_u(&r, 4u) & 0x1FFFFFFFu;
      if (count > JB_MAX_REFS) return false;
      rd_u(&r, (count + 8u) / 8u); /* the retain bits */
    } else {
      count >>= 5;
      if (count > 4u) return false;
    }
    sz = s->number <= 256u ? 1u : s->number <= 65536u ? 2u : 4u;
    if (!(s->refs = jb_alloc(j, ((size_t)count + 1u) * sizeof(*s->refs)))) return false;
    for (k = 0; k < count; k++) s->refs[k] = rd_u(&r, sz);
    s->nrefs = count;
    s->page  = rd_u(&r, flags & 0x40u ? 4u : 1u);
    s->len   = rd_u(&r, 4u);
    if (r.bad) break;
    j->nsegs++;
    if (s->type == 51u) break; /* end of file */
    if (!headers_first) {
      if (s->len == 0xFFFFFFFFu) {
        /* an immediate generic region of unknown length: to its end marker and row count */
        size_t e;
        bool   mmr;

        if (s->type != 38u && s->type != 39u) return false;
        /* the marker its coding ends with: MMR 00 00, the coder's FF AC */
        mmr = r.at + 17u < r.len && (r.p[r.at + 17u] & 1u);
        for (e = r.at + 18u; e + 6u <= r.len; e++) {
          if (mmr ? (r.p[e] == 0x00 && r.p[e + 1u] == 0x00) : (r.p[e] == 0xFF && r.p[e + 1u] == 0xAC)) {
            s->len = e + 6u - r.at;
            break;
          }
        }
        if (s->len == 0xFFFFFFFFu) s->len = r.len - r.at;
      }
      if (s->len > r.len - r.at) s->len = r.len - r.at; /* cut: what there is */
      s->data = r.p + r.at;
      r.at   += s->len;
    }
  }
  if (headers_first) {
    for (data_at = r.at, i = first; i < j->nsegs; i++) {
      seg_t *s = &j->segs[i];

      if (s->len > len - data_at) s->len = len - data_at;
      s->data  = p + data_at;
      data_at += s->len;
    }
  }
  return true;
}

static void
budget_for(jb_t *j, uint64_t area) {
  uint64_t b = area * 16u > ((uint64_t)1 << 18) ? area * 16u : (uint64_t)1 << 18;
  uint64_t e = area * 64u > ((uint64_t)1 << 20) ? area * 64u : (uint64_t)1 << 20;

  j->budget = b;
  j->effort = e;
}

static void
run_segment(jb_t *j, seg_t *s) {
  region_info_t ri;
  jbm_t        *b = NULL;
  uint8_t       t = s->type;

  if (s->done) return;
  s->done = true;
  /* a file's other pages' segments (0: every page's) */
  if (j->page_number && s->page && s->page != j->page_number) return;
  switch (t) {
    case 48: { /* page information */
      jrd_t    r = { s->data, s->len, 0, 0, false };
      uint32_t w = rd_u(&r, 4u), h = rd_u(&r, 4u), flags;

      (void)rd_u(&r, 8u);
      flags = rd_u(&r, 1u);
      (void)rd_u(&r, 2u);
      if (r.bad || w > JB_MAX_SIDE || j->page) return;
      j->page_default = (flags >> 2) & 1u;
      j->page_grows   = h == 0xFFFFFFFFu;
      if (!j->page_grows && h > JB_MAX_SIDE) return;
      j->page       = bm_new(j, w, j->page_grows ? 0u : h, j->page_default);
      j->page_fresh = true;
      return;
    }
    case 50: { /* end of stripe: a page of unknown height reaches its row */
      jrd_t    r = { s->data, s->len, 0, 0, false };
      uint32_t row = rd_u(&r, 4u);

      if (!r.bad) page_reach(j, (int64_t)row + 1);
      return;
    }
    case 0:
      (void)symbol_dictionary(j, s);
      return;
    case 16:
      (void)pattern_dictionary(j, s);
      return;
    case 53:
      s->table = huff_custom(j, s->data, s->len);
      return;
    case 6: case 7: case 22: case 23: case 42: case 43: {
      /* immediate ones off the part wanted: not decoded */
      jrd_t r = { s->data, s->len, 0, 0, false };

      if (j->cut && read_region_info(&r, &ri) && ri.h != 0xFFFFFFFFu && off_cut(j, ri.x, ri.y, ri.w, ri.h)) {
        page_reach(j, (int64_t)ri.y + ri.h);
        return;
      }
      if (t <= 7u) b = text_region(j, s, &ri);
      else if (t <= 23u) b = halftone_region(j, s, &ri);
      else b = refinement_region(j, s, &ri);
      break;
    }
    case 4:
      b = text_region(j, s, &ri);
      break;
    case 20:
      b = halftone_region(j, s, &ri);
      break;
    case 36: case 38: case 39:
      b = generic_region(j, s, &ri, t != 36u);
      break;
    case 40:
      b = refinement_region(j, s, &ri);
      break;
    default:
      return; /* end of page or file, profiles, extensions */
  }
  if (!b) return;
  if (t == 4u || t == 20u || t == 36u || t == 40u) {
    s->region = b; /* intermediate: for a refinement to come */
  } else {
    page_place(j, b, &ri);
  }
}

/* -- out ------------------------------------------------------------------------------- */

static const uint8_t jb_magic[8] = { 0x97, 'J', 'B', '2', 0x0D, 0x0A, 0x1A, 0x0A };

/* a file's header: its organisation and where its segments start */
static bool
file_header(const uint8_t *p, size_t len, size_t *skip, bool *headers_first) {
  uint8_t flags;

  if (len < 9u || memcmp(p, jb_magic, 8u)) return false;
  flags          = p[8];
  *skip          = 9u + (flags & 2u ? 0u : 4u);
  *headers_first = !(flags & 1u);
  if (*skip > len) *skip = len;
  return true;
}

IM_HIDE bool
jbig2_info(const uint8_t *p, size_t len, ImInfo *info) {
  jb_t     j;
  size_t   skip;
  bool     first, ok = false;
  uint32_t i;

  memset(&j, 0, sizeof(j));
  if (!file_header(p, len, &skip, &first)) return false;
  if (read_segments(&j, p + skip, len - skip, first)) {
    for (i = 0; i < j.nsegs; i++) {
      seg_t *s = &j.segs[i];

      if (s->type == 48u && s->len >= 8u && s->data) {
        uint32_t k;
        bool     seen = false;

        /* the pages: one a page number its information segments give */
        for (k = 0; k < i && !seen; k++) seen = j.segs[k].type == 48u && j.segs[k].page == s->page;
        if (!seen) info->pages++;
        if (ok || info->pages > 1u) continue;
        info->width  = (uint32_t)s->data[0] << 24 | (uint32_t)s->data[1] << 16 | (uint32_t)s->data[2] << 8 | s->data[3];
        info->height = (uint32_t)s->data[4] << 24 | (uint32_t)s->data[5] << 16 | (uint32_t)s->data[6] << 8 | s->data[7];
        if (info->height == 0xFFFFFFFFu) info->height = 0u;
        ok = info->width && info->width <= JB_MAX_SIDE && info->height <= JB_MAX_SIDE;
      }
    }
  }
  jb_free_all(&j);
  if (!ok) return false;
  info->fileFormatType   = IM_FILEFORMATTYPE_JBIG2;
  info->components       = 1u;
  info->bitsPerComponent = 1u;
  return true;
}

IM_HIDE ImResult
jbig2_dec_mem(ImImage **dest, const uint8_t *data, size_t len, const im_open_config_t *conf) {
  jb_t       j;
  ImImage   *im = NULL;
  uint32_t   i, x0 = 0u, y0 = 0u, w, h, y;
  size_t     skip = 0u, stride, need;
  bool       first = false, eight = conf->eightBits, file;
  uint8_t   *out;
  ImResult   res = IM_OK;

  *dest = NULL;
  memset(&j, 0, sizeof(j));
  j.limit = conf->memoryLimit;
  j.total = len + conf->jbig2GlobalsLen;
  if (j.limit && j.total > j.limit) return IM_ENOMEM;
  file = file_header(data, len, &skip, &first);
  if (file) {
    data += skip;
    len  -= skip;
  }
  if (conf->jbig2Globals && conf->jbig2GlobalsLen && !read_segments(&j, conf->jbig2Globals, conf->jbig2GlobalsLen, false)) {
    res = j.nomem ? IM_ENOMEM : IM_EBADF;
  }
  if (res == IM_OK && !read_segments(&j, data, len, first)) res = j.nomem ? IM_ENOMEM : IM_EBADF;
  if (res == IM_OK && conf->page) {
    /* the page asked for (IM_OPTION_PAGE), as its segments number it */
    j.page_number = conf->page;
  } else if (res == IM_OK && file) {
    /* a file: its first page */
    for (i = 0; i < j.nsegs; i++) {
      if (j.segs[i].type == 48u && j.segs[i].page) {
        j.page_number = j.segs[i].page;
        break;
      }
    }
  }
  if (res == IM_OK) {
    /* what bitmaps and drawing may take, by the page's size (its segment
       read before any runs: the globals' dictionaries come first); no page
       that can be: the least */
    uint64_t area = 0u;

    for (i = 0; i < j.nsegs; i++) {
      seg_t *s = &j.segs[i];

      if (s->type == 48u && s->len >= 8u && s->data && (!j.page_number || s->page == j.page_number)) {
        uint64_t w = (uint32_t)s->data[0] << 24 | (uint32_t)s->data[1] << 16 | (uint32_t)s->data[2] << 8 | s->data[3];
        uint64_t h = (uint32_t)s->data[4] << 24 | (uint32_t)s->data[5] << 16 | (uint32_t)s->data[6] << 8 | s->data[7];

        if (h == 0xFFFFFFFFu) h = 16384u; /* striped, its height to come: as tall as this */
        if (w <= JB_MAX_SIDE && h <= JB_MAX_SIDE) area = w * h;
        break;
      }
    }
    budget_for(&j, area);
  }
  if (res == IM_OK && conf->region) {
    /* left out: immediate regions off the part, unless a refinement of the
       page itself comes (it reads what's there) */
    bool reads_page = false;

    for (i = 0; i < j.nsegs; i++) {
      seg_t   *s = &j.segs[i];
      uint32_t k;
      bool     intermediate = false;

      if (s->type != 42u && s->type != 43u) continue;
      for (k = 0; k < s->nrefs; k++) {
        seg_t *p = find_seg(&j, s->refs[k]);

        if (p && (p->type == 4u || p->type == 20u || p->type == 36u || p->type == 40u)) intermediate = true;
      }
      if (!intermediate) reads_page = true;
    }
    j.cut = !reads_page;
    j.cx0 = conf->regionX;
    j.cy0 = conf->regionY;
    j.cx1 = (int64_t)conf->regionX + conf->regionW;
    j.cy1 = (int64_t)conf->regionY + conf->regionH;
  }
  for (i = 0; res == IM_OK && i < j.nsegs && !j.nomem; i++) {
    seg_t *s = &j.segs[i];

    run_segment(&j, s);
    /* a file's page done: the rest are others' */
    if (j.page_number && s->type == 49u && s->page == j.page_number) break;
  }
  if (res == IM_OK && j.nomem) res = IM_ENOMEM;
  if (res == IM_OK && (!j.page || !j.page->w || !j.page->h)) res = IM_EBADF;
  if (res != IM_OK) {
    jb_free_all(&j);
    return res;
  }

  w = j.page->w;
  h = j.page->h;
  if (conf->region) {
    if (conf->regionX >= w || conf->regionY >= h || !conf->regionW || !conf->regionH) {
      jb_free_all(&j);
      return IM_EBADF;
    }
    x0 = conf->regionX;
    y0 = conf->regionY;
    w  = conf->regionW < w - x0 ? conf->regionW : w - x0;
    h  = conf->regionH < h - y0 ? conf->regionH : h - y0;
  }
  stride = eight ? (size_t)w : ((size_t)w + 7u) / 8u;
  need   = stride * h;
  if (conf->dest) {
    if (conf->destStride < stride || conf->destSize < conf->destStride * (h - 1u) + stride) {
      jb_free_all(&j);
      return IM_EBADF;
    }
    out = conf->dest;
  } else if ((j.limit && j.total + need > j.limit) || !(out = malloc(need))) {
    jb_free_all(&j);
    return IM_ENOMEM;
  }
  /* the page as gray has it: 0 black (or a byte a pixel); a row's padding white */
  for (y = 0; y < h; y++) {
    const uint8_t *src = j.page->p + (size_t)(y0 + y) * j.page->stride;
    uint8_t       *dst = out + (size_t)y * (conf->dest ? conf->destStride : stride);

    if (eight) {
      uint32_t x;

      for (x = 0; x < w; x++) dst[x] = ((src[(x0 + x) >> 3] >> (7u - ((x0 + x) & 7u))) & 1u) ? 0u : 255u;
    } else {
      size_t q, nb = ((size_t)w + 7u) / 8u;

      if (!(x0 & 7u)) {
        for (q = 0; q < nb; q++) dst[q] = (uint8_t)~src[(x0 >> 3) + q];
      } else {
        for (q = 0; q < nb; q++) dst[q] = (uint8_t)~get8(j.page, src, (int64_t)x0 + 8 * (int64_t)q);
      }
      if (w & 7u) dst[nb - 1u] |= (uint8_t)(0xFFu >> (w & 7u));
    }
  }
  jb_free_all(&j);
  if (!(im = calloc(1, sizeof(*im)))) {
    if (!conf->dest) free(out);
    return IM_ENOMEM;
  }
  im->width              = w;
  im->height             = h;
  im->data.data          = conf->dest ? NULL : out;
  im->len                = conf->dest ? conf->destStride * ((size_t)h - 1u) + stride : need;
  im->row_pad_last       = conf->dest ? (uint32_t)(conf->destStride - stride) : 0u;
  im->componentsPerPixel = 1u;
  im->bitsPerComponent   = eight ? 8u : 1u;
  im->bitsPerPixel       = eight ? 8u : 1u;
  im->bytesPerPixel      = eight ? 1u : 0u;
  im->format             = IM_FORMAT_GRAY;
  im->colorSpace         = IM_COLORSPACE_GRAY;
  im->alphaInfo          = IM_ALPHA_NONE;
  im->byteOrder          = IM_BYTEORDER_HOST;
  im->fileFormatType     = IM_FILEFORMATTYPE_JBIG2;
  im->openIntent         = conf->openIntent;
  im->ori                = IM_ORIENTATION_UP;
  *dest = im;
  return IM_OK;
}
