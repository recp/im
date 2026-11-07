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
 * CCITT fax, a raw stream with its parameters given (IM_OPTION_CCITT):
 * ITU-T T.4's Group 3 one-dimensional (K = 0) and two-dimensional (K > 0,
 * each row tagged), T.6's Group 4 (K < 0). A row is its changing elements
 * (where white turns black and back), decoded against the row before (all
 * white before the first), then laid out a bit a pixel, 1 white unless
 * black-is-1 (or 8 bits a pixel, 255 for a 1).
 *
 * Before each row: a byte boundary where the rows are byte-aligned; for
 * Group 3 an EOL (any fill before it), six of them (RTC) the end, then
 * K > 0's tag bit (1: one-dimensional); for Group 4 EOFB the end (both
 * unless the end of block is said to be absent). A code that isn't one,
 * or the uncompressed mode, is damage: the rows so far are the image
 * (none: refused), or, where DamagedRowsBeforeError allows, the damaged
 * row is the one before it and decoding goes on at the next EOL.
 *
 * Codes by two tables: the next 8 bits, then (codes past 8 bits, under a
 * few prefixes) 5 more; built a decode (a few hundred entries).
 */

#include "../../common.h"
#include "ccitt.h"

#include <stdlib.h>
#include <string.h>

enum {
  CC_MAX_COLS = 1u << 20,
  CC_PASS = 1, CC_HORIZONTAL, CC_V0, CC_VR1, CC_VR2, CC_VR3, CC_VL1, CC_VL2, CC_VL3
};

/* T.4's run codes (Tables 2 and 3), the makeup codes past 1728 either
   colour's (Table 3a) */
static const uint16_t cc_white[][3] = { /* code, its bits, the run */
  { 0x0035,  8,    0 }, { 0x0007,  6,    1 }, { 0x0007,  4,    2 }, { 0x0008,  4,    3 }, { 0x000B,  4,    4 },
  { 0x000C,  4,    5 }, { 0x000E,  4,    6 }, { 0x000F,  4,    7 }, { 0x0013,  5,    8 }, { 0x0014,  5,    9 },
  { 0x0007,  5,   10 }, { 0x0008,  5,   11 }, { 0x0008,  6,   12 }, { 0x0003,  6,   13 }, { 0x0034,  6,   14 },
  { 0x0035,  6,   15 }, { 0x002A,  6,   16 }, { 0x002B,  6,   17 }, { 0x0027,  7,   18 }, { 0x000C,  7,   19 },
  { 0x0008,  7,   20 }, { 0x0017,  7,   21 }, { 0x0003,  7,   22 }, { 0x0004,  7,   23 }, { 0x0028,  7,   24 },
  { 0x002B,  7,   25 }, { 0x0013,  7,   26 }, { 0x0024,  7,   27 }, { 0x0018,  7,   28 }, { 0x0002,  8,   29 },
  { 0x0003,  8,   30 }, { 0x001A,  8,   31 }, { 0x001B,  8,   32 }, { 0x0012,  8,   33 }, { 0x0013,  8,   34 },
  { 0x0014,  8,   35 }, { 0x0015,  8,   36 }, { 0x0016,  8,   37 }, { 0x0017,  8,   38 }, { 0x0028,  8,   39 },
  { 0x0029,  8,   40 }, { 0x002A,  8,   41 }, { 0x002B,  8,   42 }, { 0x002C,  8,   43 }, { 0x002D,  8,   44 },
  { 0x0004,  8,   45 }, { 0x0005,  8,   46 }, { 0x000A,  8,   47 }, { 0x000B,  8,   48 }, { 0x0052,  8,   49 },
  { 0x0053,  8,   50 }, { 0x0054,  8,   51 }, { 0x0055,  8,   52 }, { 0x0024,  8,   53 }, { 0x0025,  8,   54 },
  { 0x0058,  8,   55 }, { 0x0059,  8,   56 }, { 0x005A,  8,   57 }, { 0x005B,  8,   58 }, { 0x004A,  8,   59 },
  { 0x004B,  8,   60 }, { 0x0032,  8,   61 }, { 0x0033,  8,   62 }, { 0x0034,  8,   63 }, { 0x001B,  5,   64 },
  { 0x0012,  5,  128 }, { 0x0017,  6,  192 }, { 0x0037,  7,  256 }, { 0x0036,  8,  320 }, { 0x0037,  8,  384 },
  { 0x0064,  8,  448 }, { 0x0065,  8,  512 }, { 0x0068,  8,  576 }, { 0x0067,  8,  640 }, { 0x00CC,  9,  704 },
  { 0x00CD,  9,  768 }, { 0x00D2,  9,  832 }, { 0x00D3,  9,  896 }, { 0x00D4,  9,  960 }, { 0x00D5,  9, 1024 },
  { 0x00D6,  9, 1088 }, { 0x00D7,  9, 1152 }, { 0x00D8,  9, 1216 }, { 0x00D9,  9, 1280 }, { 0x00DA,  9, 1344 },
  { 0x00DB,  9, 1408 }, { 0x0098,  9, 1472 }, { 0x0099,  9, 1536 }, { 0x009A,  9, 1600 }, { 0x0018,  6, 1664 },
  { 0x009B,  9, 1728 },
};

static const uint16_t cc_black[][3] = {
  { 0x0037, 10,    0 }, { 0x0002,  3,    1 }, { 0x0003,  2,    2 }, { 0x0002,  2,    3 }, { 0x0003,  3,    4 },
  { 0x0003,  4,    5 }, { 0x0002,  4,    6 }, { 0x0003,  5,    7 }, { 0x0005,  6,    8 }, { 0x0004,  6,    9 },
  { 0x0004,  7,   10 }, { 0x0005,  7,   11 }, { 0x0007,  7,   12 }, { 0x0004,  8,   13 }, { 0x0007,  8,   14 },
  { 0x0018,  9,   15 }, { 0x0017, 10,   16 }, { 0x0018, 10,   17 }, { 0x0008, 10,   18 }, { 0x0067, 11,   19 },
  { 0x0068, 11,   20 }, { 0x006C, 11,   21 }, { 0x0037, 11,   22 }, { 0x0028, 11,   23 }, { 0x0017, 11,   24 },
  { 0x0018, 11,   25 }, { 0x00CA, 12,   26 }, { 0x00CB, 12,   27 }, { 0x00CC, 12,   28 }, { 0x00CD, 12,   29 },
  { 0x0068, 12,   30 }, { 0x0069, 12,   31 }, { 0x006A, 12,   32 }, { 0x006B, 12,   33 }, { 0x00D2, 12,   34 },
  { 0x00D3, 12,   35 }, { 0x00D4, 12,   36 }, { 0x00D5, 12,   37 }, { 0x00D6, 12,   38 }, { 0x00D7, 12,   39 },
  { 0x006C, 12,   40 }, { 0x006D, 12,   41 }, { 0x00DA, 12,   42 }, { 0x00DB, 12,   43 }, { 0x0054, 12,   44 },
  { 0x0055, 12,   45 }, { 0x0056, 12,   46 }, { 0x0057, 12,   47 }, { 0x0064, 12,   48 }, { 0x0065, 12,   49 },
  { 0x0052, 12,   50 }, { 0x0053, 12,   51 }, { 0x0024, 12,   52 }, { 0x0037, 12,   53 }, { 0x0038, 12,   54 },
  { 0x0027, 12,   55 }, { 0x0028, 12,   56 }, { 0x0058, 12,   57 }, { 0x0059, 12,   58 }, { 0x002B, 12,   59 },
  { 0x002C, 12,   60 }, { 0x005A, 12,   61 }, { 0x0066, 12,   62 }, { 0x0067, 12,   63 }, { 0x000F, 10,   64 },
  { 0x00C8, 12,  128 }, { 0x00C9, 12,  192 }, { 0x005B, 12,  256 }, { 0x0033, 12,  320 }, { 0x0034, 12,  384 },
  { 0x0035, 12,  448 }, { 0x006C, 13,  512 }, { 0x006D, 13,  576 }, { 0x004A, 13,  640 }, { 0x004B, 13,  704 },
  { 0x004C, 13,  768 }, { 0x004D, 13,  832 }, { 0x0072, 13,  896 }, { 0x0073, 13,  960 }, { 0x0074, 13, 1024 },
  { 0x0075, 13, 1088 }, { 0x0076, 13, 1152 }, { 0x0077, 13, 1216 }, { 0x0052, 13, 1280 }, { 0x0053, 13, 1344 },
  { 0x0054, 13, 1408 }, { 0x0055, 13, 1472 }, { 0x005A, 13, 1536 }, { 0x005B, 13, 1600 }, { 0x0064, 13, 1664 },
  { 0x0065, 13, 1728 },
};

static const uint16_t cc_long[][3] = {
  { 0x0008, 11, 1792 }, { 0x000C, 11, 1856 }, { 0x000D, 11, 1920 }, { 0x0012, 12, 1984 }, { 0x0013, 12, 2048 },
  { 0x0014, 12, 2112 }, { 0x0015, 12, 2176 }, { 0x0016, 12, 2240 }, { 0x0017, 12, 2304 }, { 0x001C, 12, 2368 },
  { 0x001D, 12, 2432 }, { 0x001E, 12, 2496 }, { 0x001F, 12, 2560 },
};

/* T.4's 2-D modes (Table 4), 7 bits at most; 0000001 the uncompressed
   mode's extension (none here) */
static const uint8_t cc_modes[][3] = {
  { 0x01, 4, CC_PASS }, { 0x01, 3, CC_HORIZONTAL }, { 0x01, 1, CC_V0 }, { 0x03, 3, CC_VR1 }, { 0x03, 6, CC_VR2 },
  { 0x03, 7, CC_VR3 }, { 0x02, 3, CC_VL1 }, { 0x02, 6, CC_VL2 }, { 0x02, 7, CC_VL3 },
};

/* a look-up: its value and bits (0 none; 255 a second-level block, v) */
typedef struct cce {
  int16_t v;
  uint8_t n;
} cce_t;

typedef struct cctab {
  cce_t    l1[256];
  cce_t    l2[32u * 24u]; /* 5 bits past 8: the long codes' prefixes */
  uint32_t nl2;
} cctab_t;

/* where the reader is: apart from the tables, so a row's loop keeps it in
   registers (a copy, written back) */
typedef struct ccs {
  const uint8_t *p;
  size_t         len;
  uint64_t       bit; /* the next bit's place */
  uint64_t       end; /* len · 8 */
} ccs_t;

typedef struct ccr {
  ccs_t          s;
  cctab_t        white, black;
  uint8_t        modes[128]; /* 7 bits: the mode << 3 | its bits; 0 none */
} ccr_t;

static void
tab_add(cctab_t *t, uint32_t code, uint32_t len, int v) {
  uint32_t i;

  if (len <= 8u) {
    for (i = code << (8u - len); i < (code + 1u) << (8u - len); i++) {
      t->l1[i].v = (int16_t)v;
      t->l1[i].n = (uint8_t)len;
    }
    return;
  }
  {
    cce_t   *e = &t->l1[code >> (len - 8u)];
    uint32_t rest = code & ((1u << (len - 8u)) - 1u), rl = len - 8u;

    if (e->n != 255u) {
      if (t->nl2 >= 24u) return; /* never: the tables' prefixes are fewer */
      e->n = 255u;
      e->v = (int16_t)t->nl2++;
      memset(&t->l2[(uint32_t)e->v * 32u], 0, 32u * sizeof(cce_t));
    }
    for (i = rest << (5u - rl); i < (rest + 1u) << (5u - rl); i++) {
      t->l2[(uint32_t)e->v * 32u + i].v = (int16_t)v;
      t->l2[(uint32_t)e->v * 32u + i].n = (uint8_t)len;
    }
  }
}

static void
tab_build(cctab_t *t, const uint16_t (*codes)[3], size_t n) {
  size_t i;

  memset(t->l1, 0, sizeof(t->l1));
  t->nl2 = 0u;
  for (i = 0; i < n; i++) tab_add(t, codes[i][0], codes[i][1], codes[i][2]);
  for (i = 0; i < sizeof(cc_long) / sizeof(cc_long[0]); i++) tab_add(t, cc_long[i][0], cc_long[i][1], cc_long[i][2]);
}

static void
reader_init(ccr_t *r, const uint8_t *p, size_t len) {
  size_t i;

  r->s.p   = p;
  r->s.len = len;
  r->s.bit = 0u;
  r->s.end = (uint64_t)len * 8u;
  tab_build(&r->white, cc_white, sizeof(cc_white) / sizeof(cc_white[0]));
  tab_build(&r->black, cc_black, sizeof(cc_black) / sizeof(cc_black[0]));
  memset(r->modes, 0, sizeof(r->modes));
  for (i = 0; i < sizeof(cc_modes) / sizeof(cc_modes[0]); i++) {
    uint32_t len7 = cc_modes[i][1], k;

    for (k = (uint32_t)cc_modes[i][0] << (7u - len7); k < ((uint32_t)cc_modes[i][0] + 1u) << (7u - len7); k++) {
      r->modes[k] = (uint8_t)(cc_modes[i][2] << 3 | len7);
    }
  }
}

/* the next 32 bits, zeros past the end */
IM_INLINE uint32_t
peek32(const ccs_t *r) {
  size_t   at = (size_t)(r->bit >> 3);
  uint64_t w  = 0u;

  if (at + 8u <= r->len) {
    memcpy(&w, r->p + at, 8u);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#elif defined(_MSC_VER)
    w = _byteswap_uint64(w);
#else
    w = __builtin_bswap64(w);
#endif
  } else {
    size_t k;

    for (k = 0; k < 8u; k++) w = w << 8 | (at + k < r->len ? r->p[at + k] : 0u);
  }
  return (uint32_t)((w << (r->bit & 7u)) >> 32);
}

IM_INLINE uint32_t
peek(const ccs_t *r, uint32_t n) {
  return peek32(r) >> (32u - n);
}

IM_INLINE bool
exhausted(const ccs_t *r) {
  return r->bit >= r->end;
}

/* a run code; -1 none (not a code, or past the end) */
IM_INLINE int
code(ccs_t *r, const cctab_t *t) {
  uint32_t w;
  cce_t    e;

  /* past the end only 0s, which no code is: none, as at the end */
  w = peek(r, 13u);
  e = t->l1[w >> 5];
  if (e.n == 255u) e = t->l2[(uint32_t)e.v * 32u + (w & 31u)];
  if (!e.n) return -1;
  r->bit += e.n;
  return e.v;
}

/* a run: makeup codes (64 and more) till a terminating one */
IM_INLINE int
run(ccs_t *s, const ccr_t *r, bool black) {
  int total = 0, v;

  do {
    if ((v = code(s, black ? &r->black : &r->white)) < 0) return -1;
    total += v;
    if (total > (int)CC_MAX_COLS) return -1;
  } while (v >= 64);
  return total;
}

/* an EOL (eleven 0s and a 1), any fill before it; true where there was one */
static bool
eol(ccs_t *r) {
  uint64_t at = r->bit;
  uint32_t zeros = 0u;

  while (!exhausted(r) && zeros < 64u && !(peek32(r) >> 31)) {
    r->bit++;
    zeros++;
  }
  if (zeros >= 11u && !exhausted(r) && (peek32(r) >> 31)) {
    r->bit++;
    return true;
  }
  r->bit = at;
  return false;
}

/* a change at `at`: where the last one was it cancels it (a run of
   nothing), so the count keeps the colour's parity */
IM_INLINE void
add(uint32_t *cur, uint32_t *count, uint32_t at) {
  if (*count && cur[*count - 1u] == at) (*count)--;
  else cur[(*count)++] = at;
}

/* the reference row's b1 and b2 for a0 of its colour: the first change
   past a0 to the other colour, and the next; a0 only moves right, so the
   search goes on from `from` */
IM_INLINE void
find_b(const uint32_t *ref, uint32_t columns, int32_t a0, bool black, uint32_t *from, uint32_t *b1, uint32_t *b2) {
  uint32_t i = *from;

  /* the line's end twice after the changes stops it: a0 < columns here */
  while ((int32_t)ref[i] <= a0) i++;
  *from = i;
  i += (i & 1u) != (uint32_t)black;
  *b1 = ref[i];
  *b2 = *b1 < columns ? ref[i + 1u] : columns;
}

/* a black run [from, to) into a row's mask: 64 pixels a word, the first
   its top bit (stored big-endian it's the row's bytes) */
IM_INLINE void
black_run(uint64_t *m, uint32_t from, uint32_t to) {
  uint32_t wa, wb, k;
  uint64_t ha, tb;

  if (from >= to) return;
  wa = from >> 6;
  wb = (to - 1u) >> 6;
  ha = ~0ull >> (from & 63u);
  tb = ~0ull << (63u - ((to - 1u) & 63u));
  if (wa == wb) {
    m[wa] |= ha & tb;
    return;
  }
  m[wa] |= ha;
  for (k = wa + 1u; k < wb; k++) m[k] = ~0ull;
  m[wb] |= tb;
}

IM_INLINE uint64_t
be64(uint64_t v) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  return v;
#elif defined(_MSC_VER)
  return _byteswap_uint64(v);
#else
  return __builtin_bswap64(v);
#endif
}

/* one row's changes into cur (*count, the line's end twice after them);
   false: damage */
static bool
row_decode(ccr_t *r, bool two_d, const uint32_t *ref, uint32_t columns, uint32_t *cur, uint32_t *count_out) {
  ccs_t    s = r->s;
  uint32_t count = 0u;
  bool     black = false;

  if (!two_d) {
    uint32_t at = 0u;

    while (at < columns) {
      int len = run(&s, r, black);

      if (len < 0) {
        r->s = s;
        return false;
      }
      at = at + (uint32_t)len > columns ? columns : at + (uint32_t)len;
      add(cur, &count, at);
      black = !black;
      if (count > columns + 1u) break;
    }
  } else {
    int32_t  a0   = -1;
    uint32_t from = 0u;

    while (a0 < (int32_t)columns) {
      uint32_t b1, b2, m;
      int      mode = 0;

      if ((m = r->modes[peek(&s, 7u)]) != 0u) { /* 0s (past the end too): none */
        s.bit += m & 7u;
        mode = (int)(m >> 3);
      }
      find_b(ref, columns, a0, black, &from, &b1, &b2);
      if (mode == CC_PASS) {
        a0 = (int32_t)b2;
      } else if (mode == CC_HORIZONTAL) {
        int      r1 = run(&s, r, black), r2;
        uint32_t start = a0 < 0 ? 0u : (uint32_t)a0, a1, a2;

        r2 = r1 < 0 ? -1 : run(&s, r, !black);
        if (r1 < 0 || r2 < 0) {
          r->s = s;
          return false;
        }
        a1 = start + (uint32_t)r1 > columns ? columns : start + (uint32_t)r1;
        a2 = a1 + (uint32_t)r2 > columns ? columns : a1 + (uint32_t)r2;
        if (count + 2u > columns + 2u) break;
        add(cur, &count, a1);
        add(cur, &count, a2);
        a0 = (int32_t)a2;
      } else if (mode >= CC_V0 && mode <= CC_VL3) {
        static const int delta[] = { 0, 1, 2, 3, -1, -2, -3 };
        int32_t          a1 = (int32_t)b1 + delta[mode - CC_V0];

        if (a1 < 0) a1 = 0;
        if (a1 < a0) a1 = a0 < 0 ? 0 : a0; /* damage: never back past a0 */
        if (a1 > (int32_t)columns) a1 = (int32_t)columns;
        if (count + 1u > columns + 2u) break;
        add(cur, &count, (uint32_t)a1);
        a0    = a1;
        black = !black;
      } else {
        r->s = s;
        return false; /* the uncompressed mode, or not a code */
      }
    }
  }
  while (count && cur[count - 1u] >= columns) count--;
  cur[count] = cur[count + 1u] = columns;
  *count_out = count;
  r->s = s;
  return true;
}

/* the image: rows as decoded into `out` (`stride` a row, `maxrows` of
   them room), from row `y0` on (those before decoded, not kept) and the
   columns [x0, x0 + w) of each (shifted to the row's start); 8 bits a
   pixel where `eight`. The rows kept back */
static int64_t
decode_rows(ccr_t *r, const im_ccitt_t *pa, uint8_t *out, size_t stride, uint32_t y0, uint32_t maxrows, uint32_t x0,
            uint32_t w, bool eight, uint8_t **grow, size_t *cap, size_t limit) {
  uint32_t columns = pa->columns, rows = pa->rows, row, count = 0u, kept = 0u, damaged = 0u, i;
  uint32_t *ref = malloc((columns + 4u) * sizeof(*ref)), *cur = malloc((columns + 4u) * sizeof(*cur));
  uint32_t  nw = (columns + 63u) / 64u + 2u; /* a row's mask's words, a zero one past */
  uint64_t *m = malloc(nw * sizeof(*m));
  bool      ok_any = false;
  /* a row's bits past its width white's, as a row starts; the mask's 1s
     (black) 1 bits only where black is 1 */
  uint8_t   keep = (uint8_t)(0xFF00u >> (w & 7u)), pad = pa->black_is_1 ? 0x00u : 0xFFu;
  uint64_t  flip = pa->black_is_1 ? 0u : ~0ull;

  if (!ref || !cur || !m) {
    free(ref);
    free(cur);
    free(m);
    return -1;
  }
  ref[0] = ref[1] = columns; /* the line before the first: all white */
  for (row = 0; !rows || row < rows; row++) {
    bool two_d = pa->k < 0, good;

    /* a row on a byte; not where the data's end starts right here (an RTC
       or EOFB straight after the last row, unaligned: aligning would cut
       into it). Two EOLs in turn (2-D: the first's tag 1 between) are
       nothing a row's fill and codes can make: no code ends with more
       than 3 0s or starts with more than 7 */
    if (pa->byte_align && (pa->k > 0 ? peek(&r->s, 25u) != 0x3001u : peek(&r->s, 24u) != 0x001001u)) {
      r->s.bit = (r->s.bit + 7u) / 8u * 8u;
    }
    if (pa->k >= 0) {
      if (eol(&r->s)) {
        uint32_t more = 0;

        /* RTC: six EOLs; two-dimensional ones each with its tag bit (a 1,
           T.4 4.2.4) between. Only all six end it; else the EOLs as they
           come (a 1 between two is damage, decoded as such) */
        if (pa->k > 0 && pa->end_of_block) {
          uint64_t after = r->s.bit;

          while (more < 5u && !exhausted(&r->s) && (peek32(&r->s) >> 31)) {
            r->s.bit++;
            if (!eol(&r->s)) break;
            more++;
          }
          if (more >= 5u) break;
          r->s.bit = after;
          more   = 0u;
        }
        while (more < 5u && eol(&r->s)) more++;
        if (more >= 5u && pa->end_of_block) break;
      }
      if (pa->k > 0) {
        if (exhausted(&r->s)) break;
        two_d = !(peek32(&r->s) >> 31);
        r->s.bit++;
      }
    } else if (pa->end_of_block && peek(&r->s, 24u) == 0x001001u) {
      r->s.bit += 24u; /* EOFB */
      break;
    }
    if (exhausted(&r->s)) break;

    good = row_decode(r, two_d, ref, columns, cur, &count);
    if (!good) {
      /* damage: the rows so far, or (DamagedRowsBeforeError, rows with
         EOLs) the one before again and on at the next EOL */
      if (pa->k < 0 || !pa->end_of_line || damaged >= pa->damaged_rows) break;
      damaged++;
      memcpy(cur, ref, (columns + 4u) * sizeof(*cur));
      for (count = 0; cur[count] < columns; count++) {}
      while (!exhausted(&r->s) && peek(&r->s, 12u) != 1u) r->s.bit++;
    }
    ok_any = true;

    if (row >= y0) {
      uint8_t *dst;

      if (kept >= maxrows) {
        /* rows not known: more room (the limit holding) */
        size_t want = *cap ? *cap * 2u : stride * 64u;

        if (!grow || (limit && want > limit)) break;
        {
          uint8_t *g = realloc(*grow, want);

          if (!g) break;
          *grow   = g;
          *cap    = want;
          out     = g;
          maxrows = (uint32_t)(want / stride);
        }
      }
      dst = out + (size_t)kept * stride;
      memset(m, 0, nw * sizeof(*m));
      for (i = 0; i < count; i += 2u) black_run(m, cur[i], cur[i + 1u] < columns ? cur[i + 1u] : columns);
      if (eight) {
        uint32_t x;

        for (x = 0; x < w; x++) dst[x] = (uint8_t)((((m[(x0 + x) >> 6] >> (63u - ((x0 + x) & 63u))) & 1u) ^ (flip & 1u)) ? 255u : 0u);
      } else {
        /* the columns from x0, 64 a word (shifted where x0 isn't on one),
           big-endian: the row's bytes */
        uint32_t q = x0 >> 6, sh = x0 & 63u, nb = (w + 7u) / 8u, k;

        for (k = 0; k * 8u < nb; k++) {
          uint64_t v = sh ? (m[q + k] << sh | m[q + k + 1u] >> (64u - sh)) : m[q + k];

          v = be64(v ^ flip);
          if (k * 8u + 8u <= nb) memcpy(dst + k * 8u, &v, 8u);
          else memcpy(dst + k * 8u, &v, nb - k * 8u);
        }
        if (w & 7u) dst[w / 8u] = (uint8_t)((dst[w / 8u] & keep) | (pad & ~keep));
      }
      kept++;
      if (kept >= maxrows && !grow) {
        row++;
        break; /* the rows wanted are made */
      }
    }
    {
      uint32_t *t = ref;

      ref = cur;
      cur = t;
    }
  }
  free(ref);
  free(cur);
  free(m);
  return ok_any || !rows ? (int64_t)kept : -1;
}

IM_HIDE ImResult
ccitt_dec_mem(ImImage **dest, const uint8_t *data, size_t len, const im_open_config_t *conf) {
  const im_ccitt_t *pa = &conf->ccitt;
  ImImage          *im;
  ccr_t            *r;
  uint32_t          x0 = 0u, y0 = 0u, w, h, maxrows;
  bool              eight = conf->eightBits;
  size_t            stride, cap = 0u;
  uint8_t          *out = NULL, *grown = NULL;
  int64_t           n;

  *dest = NULL;
  if (!pa->columns || pa->columns > CC_MAX_COLS) return IM_EBADF;
  w = pa->columns;
  h = pa->rows;
  if (conf->region) {
    if (conf->regionX >= w || (h && conf->regionY >= h) || !conf->regionW || !conf->regionH) return IM_EBADF;
    x0 = conf->regionX;
    y0 = conf->regionY;
    w  = conf->regionW < w - x0 ? conf->regionW : w - x0;
    h  = h ? (conf->regionH < h - y0 ? conf->regionH : h - y0) : conf->regionH;
  }
  stride = eight ? (size_t)w : ((size_t)w + 7u) / 8u;
  /* the rows' memory: the caller's, or im's (grown as they come where
     the count isn't known and no region bounds it) */
  maxrows = h;
  if (conf->memoryLimit && (uint64_t)stride * (h ? h : 1u) + ((uint64_t)pa->columns + 4u) * 8u + len > conf->memoryLimit) {
    return IM_ENOMEM;
  }
  if (conf->dest) {
    if (!h || conf->destStride < stride || conf->destSize < conf->destStride * (h - 1u) + stride) return IM_EBADF;
    out = conf->dest;
  } else if (h) {
    if (!(out = malloc(stride * h))) return IM_ENOMEM;
  }
  if (!(r = malloc(sizeof(*r)))) {
    if (!conf->dest) free(out);
    return IM_ENOMEM;
  }
  reader_init(r, data, len);
  n = decode_rows(r, pa, out, conf->dest ? conf->destStride : stride, y0, h ? maxrows : 0u, x0, w, eight,
                  h ? NULL : &grown, &cap, conf->memoryLimit);
  free(r);
  if (!h) out = grown;
  if (n <= 0 || !(im = calloc(1, sizeof(*im)))) {
    if (!conf->dest) free(out);
    return IM_EBADF;
  }
  im->width              = w;
  im->height             = (uint32_t)n;
  im->data.data          = conf->dest ? NULL : out;
  im->len                = conf->dest ? conf->destStride * ((size_t)n - 1u) + stride : stride * (size_t)n;
  im->row_pad_last       = conf->dest ? (uint32_t)(conf->destStride - stride) : 0u;
  im->componentsPerPixel = 1u;
  im->bitsPerComponent   = eight ? 8u : 1u;
  im->bitsPerPixel       = eight ? 8u : 1u;
  im->bytesPerPixel      = eight ? 1u : 0u;
  im->format             = IM_FORMAT_GRAY;
  im->colorSpace         = IM_COLORSPACE_GRAY;
  im->alphaInfo          = IM_ALPHA_NONE;
  im->byteOrder          = IM_BYTEORDER_HOST;
  im->fileFormatType     = IM_FILEFORMATTYPE_CCITT;
  im->openIntent         = conf->openIntent;
  im->ori                = IM_ORIENTATION_UP;
  *dest = im;
  return IM_OK;
}
