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
 * Huffman-coded scans (Annex F, G): the bits, the tables, sequential and
 * progressive blocks, restarts.
 *
 * A table's codes and, where they fit in J_HBITS bits with it, a value's
 * bits are one look (the symbol, the run, the coefficient); longer codes
 * the canonical search. Where data ends or a marker cuts it, zeros follow
 * and the MCUs after the one that ran into them are skipped up to the
 * next restart, as libjpeg does; a code that isn't one takes 17 bits and
 * is symbol 0, as libjpeg's.
 */

#include "jpgint.h"

IM_HIDE const uint8_t jpg_natural[64 + 16] = {
  0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
  12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
  58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
  63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63
};

/* -- the data, unstuffed ----------------------------------------------------- */

static bool
seg_push(jdata_t *d, size_t start, size_t end, uint32_t marker) {
  if (d->nseg == d->segcap) {
    uint32_t cap  = d->segcap ? d->segcap * 2u : 16u;
    jseg_t  *next = realloc(d->seg, cap * sizeof(*next));

    if (!next) return false;
    d->seg    = next;
    d->segcap = cap;
  }
  d->seg[d->nseg].start  = start;
  d->seg[d->nseg].end    = end;
  d->seg[d->nseg].marker = marker;
  d->nseg++;
  return true;
}

/* the scan's data from `p`: stuffed 0xFFs as data, fill 0xFFs dropped, a
   segment ended at each marker; RSTn (and the markers below SOF0, which a
   restart's resync skips) go on, any other ends the scan, as does the
   data's end (libjpeg's fake EOI) */
IM_HIDE bool
jpg_unstuff(jdata_t *d, const uint8_t *p, const uint8_t *end) {
  size_t   need = (size_t)(end - p) + J_TAIL, at = 0u, start = 0u;
  uint8_t *o;

  if (need > d->cap) {
    uint8_t *grown = realloc(d->buf, need);

    if (!grown) return false;
    d->buf = grown;
    d->cap = need;
  }
  o       = d->buf;
  d->nseg = 0u;
  for (;;) {
    const uint8_t *ff = memchr(p, 0xFF, (size_t)(end - p)), *q;

    if (!ff) {
      memcpy(o + at, p, (size_t)(end - p));
      at += (size_t)(end - p);
      if (!seg_push(d, start, at, 0xD9u)) return false;
      d->next = end;
      break;
    }
    memcpy(o + at, p, (size_t)(ff - p));
    at += (size_t)(ff - p);
    for (q = ff + 1; q < end && *q == 0xFFu; q++) {
    }
    if (q >= end) {
      if (!seg_push(d, start, at, 0xD9u)) return false;
      d->next = end;
      break;
    }
    if (*q == 0x00u) {
      o[at++] = 0xFFu;
      p       = q + 1;
      continue;
    }
    if (!seg_push(d, start, at, *q)) return false;
    if ((*q >= 0xD0u && *q <= 0xD7u) || *q < 0xC0u) {
      p     = q + 1;
      start = at;
      continue;
    }
    d->next = q - 1;
    break;
  }
  memset(o + at, 0, J_TAIL);
  return true;
}

/* -- the bits ------------------------------------------------------------------ */

IM_INLINE uint64_t
load_be64(const uint8_t *p) {
  uint64_t w;

  memcpy(&w, p, 8u);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  return w;
#elif defined(_MSC_VER)
  return _byteswap_uint64(w);
#else
  return __builtin_bswap64(w);
#endif
}

/* to 56 bits and more, without a branch: a word loaded, the whole bytes
   it adds taken (libdeflate's way, MSB first) */
IM_INLINE void
refill(jbits_t *b) {
  b->buf |= load_be64(b->p) >> b->n;
  b->p += (63u - b->n) >> 3;
  b->n |= 56u;
}

#define PEEK(b, k) ((uint32_t)((b)->buf >> (64u - (k))))
#define DROP(b, k) ((b)->buf <<= (k), (b)->n -= (uint32_t)(k))

IM_INLINE uint32_t
get_bits(jbits_t *b, uint32_t s) {
  uint32_t v;

  if (!s) return 0u;
  if (b->n < s) refill(b);
  v = PEEK(b, s);
  DROP(b, s);
  return v;
}

IM_INLINE int32_t
extend(uint32_t v, uint32_t s) {
  return s && v < (1u << (s - 1u)) ? (int32_t)v - (int32_t)(1u << s) + 1 : (int32_t)v;
}

/* a code longer than one look's: the canonical search, from the length
   past it; none (damage): 17 bits taken, symbol 0, as libjpeg */
static uint32_t
huff_long(jbits_t *b, const jhuff_t *h) {
  uint32_t code = PEEK(b, 16), l;

  for (l = J_HBITS + 1u; l <= 16u; l++) {
    int32_t c = (int32_t)(code >> (16u - l));

    if (c <= h->maxcode[l]) {
      DROP(b, l);
      return h->vals[(uint32_t)(c + h->valoff[l]) & 0xFFu];
    }
  }
  DROP(b, 17u);
  return 0u;
}

/* the next symbol (32 bits at hand: a code and a value's bits) */
IM_INLINE uint32_t
huff_sym(jbits_t *b, const jhuff_t *h) {
  uint32_t e;

  if (b->n < 32u) refill(b);
  e = h->fast[PEEK(b, J_HBITS)];
  if (likely(e)) {
    uint32_t sym = (e >> 8) & 0xFFu, len = e & 31u;

    if (e & J_FUSED) len -= sym & 15u; /* the value's bits stay */
    DROP(b, len);
    return sym;
  }
  return huff_long(b, h);
}

/* -- tables -------------------------------------------------------------------- */

/* libjpeg's checks (jpeg_make_d_derived_tbl): no more codes than a
   length holds; a DC symbol 15 at most (16 lossless) */
IM_HIDE bool
jpg_huff_build(jhuff_t *h, const uint8_t counts[16], const uint8_t *vals, bool dc, bool lossless) {
  uint32_t len, i, p = 0u, code = 0u, total = 0u;
  uint16_t codes[256];
  uint8_t  sizes[256];

  memset(h, 0, sizeof(*h));
  for (len = 1; len <= 16u; len++) total += counts[len - 1u];
  if (total > 256u) return false;
  memcpy(h->vals, vals, total);

  for (len = 1; len <= 16u; len++) {
    uint32_t n = counts[len - 1u];

    if (n) {
      h->valoff[len] = (int32_t)p - (int32_t)code;
      for (i = 0; i < n; i++, p++, code++) {
        codes[p] = (uint16_t)code;
        sizes[p] = (uint8_t)len;
      }
      h->maxcode[len] = (int32_t)code - 1;
      if (code >= (1u << len)) return false; /* libjpeg: no code all ones */
    } else {
      h->maxcode[len] = -1;
    }
    code <<= 1;
  }
  h->valoff[17]  = 0;
  h->maxcode[17] = 0xFFFFF;

  for (i = 0; i < total; i++) {
    if (h->vals[i] > h->maxsym) h->maxsym = h->vals[i];
  }
  if (dc && h->maxsym > (lossless ? 16u : 15u)) return false;

  /* one look: the code, and its value where both fit */
  for (i = 0; i < total; i++) {
    uint32_t l = sizes[i], sym = h->vals[i], s = sym & 15u, first, span, f;

    if (l > J_HBITS) break;
    first = (uint32_t)codes[i] << (J_HBITS - l);
    span  = 1u << (J_HBITS - l);
    for (f = 0; f < span; f++) {
      uint32_t e = l | sym << 8;

      if (l + s <= J_HBITS && !(dc && sym == 16u)) {
        uint32_t v = s ? ((first + f) >> (J_HBITS - l - s)) & ((1u << s) - 1u) : 0u;

        e = (l + s) | J_FUSED | sym << 8 | (uint32_t)(uint16_t)extend(v, s) << 16;
      }
      h->fast[first + f] = e;
    }
  }
  h->ok = true;
  return true;
}

/* the tables Annex K suggests, where a stream defines none (Motion-JPEG):
   libjpeg-turbo's std_huff_tables, at the first scan */
IM_HIDE void
jpg_huff_std(jpg_t *j) {
  static const uint8_t dc0_bits[16] = { 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };
  static const uint8_t dc1_bits[16] = { 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 };
  static const uint8_t dc_vals[12]  = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
  static const uint8_t ac0_bits[16] = { 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d };
  static const uint8_t ac0_vals[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
    0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa
  };
  static const uint8_t ac1_bits[16] = { 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 };
  static const uint8_t ac1_vals[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
    0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
    0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa
  };
  static const struct { const uint8_t *bits, *vals; uint32_t slot; bool dc; } std[4] = {
    { dc0_bits, dc_vals, 0, true }, { ac0_bits, ac0_vals, 0, false },
    { dc1_bits, dc_vals, 1, true }, { ac1_bits, ac1_vals, 1, false }
  };
  uint32_t i;

  for (i = 0; i < 4u; i++) {
    jhuff_t **slot = std[i].dc ? &j->dc[std[i].slot] : &j->ac[std[i].slot];

    if (*slot || !(*slot = malloc(sizeof(**slot)))) continue;
    jpg_huff_build(*slot, std[i].bits, std[i].vals, std[i].dc, false);
  }
}

/* -- blocks -------------------------------------------------------------------- */

/* a sequential block's coefficients into `blk` (zeros where none) */
IM_INLINE void
block_seq(jbits_t *b, const jhuff_t *dct, const jhuff_t *act, int32_t *dc, int16_t *blk) {
  uint32_t e, k;
  int32_t  diff;

  if (b->n < 32) refill(b);
  e = dct->fast[PEEK(b, J_HBITS)];
  if (likely(e & J_FUSED)) {
    DROP(b, e & 31u);
    diff = (int32_t)e >> 16;
  } else {
    uint32_t s = e ? (DROP(b, e & 31u), (e >> 8) & 15u) : huff_long(b, dct) & 15u;

    diff = extend(get_bits(b, s), s);
  }
  *dc    = (int32_t)((uint32_t)*dc + (uint32_t)diff);
  blk[0] = (int16_t)*dc;

  for (k = 1; k < 64u; k++) {
    uint32_t rs, s;

    if (b->n < 32) refill(b);
    e = act->fast[PEEK(b, J_HBITS)];
    if (likely(e & J_FUSED)) {
      DROP(b, e & 31u);
      rs = (e >> 8) & 0xFFu;
      if (rs & 15u) {
        k += rs >> 4;
        blk[jpg_natural[k]] = (int16_t)((int32_t)e >> 16);
        continue;
      }
    } else {
      rs = e ? (DROP(b, e & 31u), (e >> 8) & 0xFFu) : huff_long(b, act);
      if ((s = rs & 15u)) {
        k += rs >> 4;
        blk[jpg_natural[k]] = (int16_t)extend(get_bits(b, s), s);
        continue;
      }
    }
    if (rs != 0xF0u) break;
    k += 15u;
  }
}

/* a scan's state, one a thread decoding it: the DC predictions (by
   component), the end-of-band run, its reader, whether data ran out */
typedef struct jrd {
  jbits_t        b;
  const uint8_t *org;   /* where its bytes are counted from */
  size_t         pre;   /* bytes taken before `org` */
  size_t         len;   /* the segment's bytes */
  const uint8_t *lim;   /* past it an MCU may run out: the tail */
  bool           tail;
} jrd_t;

typedef struct jctx {
  jpg_t   *j;
  jrd_t    r;
  uint8_t *tail;
  size_t   margin;
  int32_t  dc[J_COMPS];
  uint32_t eobrun;
  uint32_t last_good; /* the iMCU row last decoded with data */
  bool     lacking;
} jctx_t;

/* a progressive scan's part of a block (G.1.2, as libjpeg's jdphuff.c) */
static void
block_dc_first(jctx_t *x, jbits_t *b, uint32_t ci, int16_t *blk) {
  const jpg_t *j = x->j;
  uint32_t     s = huff_sym(b, j->dc[j->comp[ci].td]) & 15u;
  int32_t      diff = extend(get_bits(b, s), s);

  x->dc[ci] = (int32_t)((uint32_t)x->dc[ci] + (uint32_t)diff);
  blk[0]    = (int16_t)((uint32_t)x->dc[ci] << j->al);
}

static void
block_dc_refine(const jpg_t *j, jbits_t *b, int16_t *blk) {
  if (get_bits(b, 1u)) blk[0] = (int16_t)(blk[0] | (1 << j->al));
}

static void
block_ac_first(jctx_t *x, jbits_t *b, const jcomp_t *c, int16_t *blk) {
  const jpg_t   *j = x->j;
  const jhuff_t *h = j->ac[c->ta];
  uint32_t       k;

  if (x->eobrun) {
    x->eobrun--;
    return;
  }
  for (k = j->ss; k <= j->se; k++) {
    uint32_t rs = huff_sym(b, h), r = rs >> 4, s = rs & 15u;

    if (s) {
      k += r;
      blk[jpg_natural[k]] = (int16_t)((uint32_t)extend(get_bits(b, s), s) << j->al);
    } else if (r == 15u) {
      k += 15u;
    } else {
      x->eobrun = 1u << r;
      if (r) x->eobrun += get_bits(b, r);
      x->eobrun--;
      break;
    }
  }
}

static void
block_ac_refine(jctx_t *x, jbits_t *b, const jcomp_t *c, int16_t *blk) {
  const jpg_t   *j  = x->j;
  const jhuff_t *h  = j->ac[c->ta];
  int32_t        p1 = 1 << j->al, m1 = -1 * (1 << j->al);
  uint32_t       k  = j->ss, se = j->se;

  if (!x->eobrun) {
    for (; k <= se; k++) {
      uint32_t rs = huff_sym(b, h), s = rs & 15u;
      int32_t  r = (int32_t)(rs >> 4), v = 0;

      if (s) {
        v = get_bits(b, 1u) ? p1 : m1;
      } else if (r != 15) {
        x->eobrun = 1u << r;
        if (r) x->eobrun += get_bits(b, (uint32_t)r);
        break;
      }
      do {
        int16_t *coef = &blk[jpg_natural[k]];

        if (*coef) {
          if (get_bits(b, 1u) && !(*coef & p1)) *coef = (int16_t)(*coef >= 0 ? *coef + p1 : *coef + m1);
        } else if (--r < 0) {
          break;
        }
        k++;
      } while (k <= se);
      if (v) blk[jpg_natural[k]] = (int16_t)v;
    }
  }
  if (x->eobrun) {
    for (; k <= se; k++) {
      int16_t *coef = &blk[jpg_natural[k]];

      if (*coef && get_bits(b, 1u) && !(*coef & p1)) *coef = (int16_t)(*coef >= 0 ? *coef + p1 : *coef + m1);
    }
    x->eobrun--;
  }
}

/* -- a scan -------------------------------------------------------------------- */

/* one MCU's blocks (or the one block of a single-component scan's): where
   a multi-scan image keeps them, or (one pass) decoded in scratch zeros
   and put in the plane */
static void
mcu(jctx_t *x, uint32_t mx, uint32_t my, bool skip) {
  IM_ALIGN(16) int16_t scratch[64];
  jpg_t   *j = x->j;
  jbits_t *b = &x->r.b;
  uint32_t i, h, v;

  for (i = 0; i < j->nscomp; i++) {
    uint32_t ci = j->scomp[i];
    jcomp_t *c  = &j->comp[ci];
    uint32_t nh = j->nscomp == 1u ? 1u : c->h, nv = j->nscomp == 1u ? 1u : c->v;

    for (v = 0; v < nv; v++) {
      for (h = 0; h < nh; h++) {
        uint32_t bx = mx * nh + h, by = my * nv + v;
        int16_t *blk;

        if (j->multiscan) {
          blk = c->coefs + ((size_t)by * c->bw + bx) * 64u;
        } else {
          blk = scratch;
          memset(scratch, 0, sizeof(scratch));
        }
        if (!skip) {
          if (j->process == J_PROGRESSIVE) {
            if (j->ss == 0u) {
              if (j->ah == 0u) block_dc_first(x, b, ci, blk);
              else block_dc_refine(j, b, blk);
            } else if (j->ah == 0u) {
              block_ac_first(x, b, c, blk);
            } else {
              block_ac_refine(x, b, c, blk);
            }
          } else {
            block_seq(b, j->dc[c->td], j->ac[c->ta], &x->dc[ci], blk);
          }
        }
        if (!j->multiscan) jpg_put(j, c, blk, bx, by);
      }
    }
  }
}

/* a reader over segment `s` (or none: at a marker left unread, zeros) */
static void
rd_start(jctx_t *x, uint32_t s) {
  const jdata_t *d = &x->j->sd;
  jrd_t         *r = &x->r;

  r->b.buf = 0u;
  r->b.n   = 0u;
  r->pre   = 0u;
  if (s < d->nseg) {
    r->org  = d->buf + d->seg[s].start;
    r->len  = d->seg[s].end - d->seg[s].start;
    r->tail = false;
  } else {
    memset(x->tail, 0, x->margin + 16u);
    r->org  = x->tail;
    r->len  = 0u;
    r->tail = true;
  }
  r->b.p = r->org;
  r->lim = r->len > x->margin ? r->org + (r->len - x->margin) : r->org;
}

/* near the segment's end: what's left copied, zeros after, so an MCU
   reads past it as libjpeg does (zeros) */
static void
rd_tail(jctx_t *x) {
  jrd_t *r    = &x->r;
  size_t left = r->len - (size_t)(r->b.p - r->org);

  memcpy(x->tail, r->b.p, left);
  memset(x->tail + left, 0, x->margin + 16u);
  r->pre += (size_t)(r->b.p - r->org);
  r->org  = x->tail;
  r->b.p  = x->tail;
  r->len  = left;
  r->tail = true;
}

/* bits taken past the segment's data */
IM_INLINE bool
rd_past(const jrd_t *r) {
  return ((size_t)(r->b.p - r->org) + r->pre) * 8u - r->b.n > (r->len + r->pre) * 8u;
}

/* MCU `n` of the scan's, decoded (or skipped where data ran out) */
IM_INLINE void
scan_mcu(jctx_t *x, uint32_t n, uint32_t w) {
  uint32_t mx = n % w, my = n / w;

  if (!x->lacking) {
    const jpg_t *j = x->j;

    x->last_good = j->nscomp == 1u ? my / j->comp[j->scomp[0]].v : my;
    if (!x->r.tail && x->r.b.p >= x->r.lim) rd_tail(x);
    mcu(x, mx, my, false);
    if (rd_past(&x->r)) x->lacking = true;
  } else {
    mcu(x, mx, my, true);
  }
}

/* libjpeg's read_restart_marker and its resync, on the segments: the
   expected RSTn taken; a marker before it skipped, its segment with it;
   one of the next two RSTs (or any other marker) left unread (true), the
   interval reading zeros; any other RST taken for it. `cur` the segment
   the next interval reads */
IM_HIDE bool
jpg_resync(const jdata_t *d, uint32_t *next_rst, uint32_t *cur) {
  uint32_t want = *next_rst, m;
  bool     at_marker = false;

  for (;;) {
    int action;

    m = *cur < d->nseg ? d->seg[*cur].marker : 0xD9u;
    if (m == 0xD0u + want) {
      action = 1;
    } else if (m < 0xC0u) {
      action = 2;
    } else if (m < 0xD0u || m > 0xD7u) {
      action = 3;
    } else if (m == 0xD0u + ((want + 1u) & 7u) || m == 0xD0u + ((want + 2u) & 7u)) {
      action = 3;
    } else if (m == 0xD0u + ((want - 1u) & 7u) || m == 0xD0u + ((want - 2u) & 7u)) {
      action = 2;
    } else {
      action = 1;
    }
    if (action == 3) {
      at_marker = true;
      break;
    }
    (*cur)++;
    if (action == 1 || *cur >= d->nseg) break;
  }
  *next_rst = (*next_rst + 1u) & 7u;
  return at_marker;
}

/* a restart (libjpeg's process_restart): predictions and runs reset, data
   on from the segment the resync finds */
static void
restart(jctx_t *x, uint32_t *next_rst, uint32_t *cur, bool *at_marker) {
  uint32_t i;

  *at_marker = jpg_resync(&x->j->sd, next_rst, cur);
  for (i = 0; i < J_COMPS; i++) x->dc[i] = 0;
  x->eobrun = 0u;
  if (!*at_marker) x->lacking = false;
  rd_start(x, *at_marker ? UINT32_MAX : *cur);
}

static bool
ctx_init(jctx_t *x, jpg_t *j, size_t margin) {
  memset(x, 0, sizeof(*x));
  x->j      = j;
  x->margin = margin;
  return (x->tail = malloc(margin * 2u + 32u)) != NULL;
}

/* restart intervals on the pool: each its own segment, the RSTs in order
   (a valid stream's; any other is read in order, as libjpeg's resync goes) */
typedef struct jpar {
  jpg_t    *j;
  jctx_t   *ctx;       /* a worker's */
  uint32_t  w, total, per, step;
  uint32_t  last_good; /* the last interval's */
} jpar_t;

static bool
par_item(void *arg, uint32_t worker, uint32_t item) {
  jpar_t  *pa = arg;
  jctx_t  *x  = &pa->ctx[worker];
  uint32_t k  = item * pa->step, end = k + pa->step, n, i;
  uint32_t ints = (pa->total + pa->per - 1u) / pa->per;

  for (; k < end && k < ints; k++) {
    uint32_t last = (k + 1u) * pa->per < pa->total ? (k + 1u) * pa->per : pa->total;

    for (i = 0; i < J_COMPS; i++) x->dc[i] = 0;
    x->eobrun  = 0u;
    x->lacking = false;
    rd_start(x, k);
    for (n = k * pa->per; n < last; n++) scan_mcu(x, n, pa->w);
    if (k + 1u == ints) pa->last_good = x->last_good;
  }
  return true;
}

/* -- one segment on threads: speculative, self-synchronising ------------------- */

/* A sequential scan without restarts is one segment; its bits can still be
   split. A Huffman decoder started at any byte soon falls into step with
   one started at the beginning (Klein and Wiseman): once both are at a
   block boundary at the same bit and the same block of the MCU they decode
   alike. So (1) each thread decodes its chunk from its first byte, keeping
   the boundaries it passes early on and the first past its end; (2) in
   order, from the true start, each chunk's first true boundary is walked
   on, block by block, until it's one the chunk's decoder passed: its
   start, known; (3) each thread decodes its part for real. A chunk that
   never falls into step is walked through in (2): the pixels are the same
   either way, only the time differs. (parajpeg does this too.) */

typedef struct jsync {
  uint64_t bit;            /* where in the segment */
  uint32_t blk;            /* blocks since the path's start */
  uint32_t slot;           /* the block's place in its MCU */
  int32_t  dc[J_COMPS];    /* DC differences summed since the start */
} jsync_t;

typedef struct jchunk {
  size_t   start, end;     /* bytes */
  uint64_t window;         /* boundaries kept below this bit */
  jsync_t *rec;
  uint32_t nrec, cap;
  jsync_t  cross;          /* the first boundary at or past `end` */
  bool     crossed;
  jsync_t  s;              /* (2): its true start, absolute */
} jchunk_t;

typedef struct jspec {
  jpg_t    *j;
  jctx_t   *ctx;
  jchunk_t *ch;
  uint32_t  n, w, total;   /* chunks, MCUs across, blocks */
  uint32_t  nblk;          /* blocks an MCU */
  uint8_t   slot_ci[64], slot_h[64], slot_v[64];
} jspec_t;

/* a sequential block decoded and dropped: its DC difference */
IM_INLINE int32_t
block_drop(jbits_t *b, const jhuff_t *dct, const jhuff_t *act) {
  uint32_t e, k;
  int32_t  diff;

  if (b->n < 32) refill(b);
  e = dct->fast[PEEK(b, J_HBITS)];
  if (likely(e & J_FUSED)) {
    DROP(b, e & 31u);
    diff = (int32_t)e >> 16;
  } else {
    uint32_t s = e ? (DROP(b, e & 31u), (e >> 8) & 15u) : huff_long(b, dct) & 15u;

    diff = extend(get_bits(b, s), s);
  }
  for (k = 1; k < 64u; k++) {
    uint32_t rs;

    if (b->n < 32) refill(b);
    e = act->fast[PEEK(b, J_HBITS)];
    if (likely(e & J_FUSED)) {
      DROP(b, e & 31u);
      rs = (e >> 8) & 0xFFu;
    } else {
      rs = e ? (DROP(b, e & 31u), (e >> 8) & 0xFFu) : huff_long(b, act);
      if (rs & 15u) {
        if (b->n < 16) refill(b);
        DROP(b, rs & 15u);
      }
    }
    if (rs & 15u) {
      k += rs >> 4;
      continue;
    }
    if (rs != 0xF0u) break;
    k += 15u;
  }
  return diff;
}

IM_INLINE uint64_t
rd_bit(const jrd_t *r) {
  return ((uint64_t)(r->b.p - r->org) + r->pre) * 8u - r->b.n;
}

/* the reader at bit `bit` of segment 0 */
static void
rd_seek(jctx_t *x, uint64_t bit) {
  jrd_t *r = &x->r;

  rd_start(x, 0u);
  r->b.p = r->org + bit / 8u;
  if (r->b.p >= r->lim) rd_tail(x);
  refill(&r->b);
  DROP(&r->b, (uint32_t)(bit & 7u));
}

/* block `blk` of the scan, to its plane, decoded with `dc` */
static void
spec_block(jspec_t *sp, jctx_t *x, uint32_t blk, bool skip) {
  IM_ALIGN(16) int16_t scratch[64];
  jpg_t   *j = sp->j;
  uint32_t m = blk / sp->nblk, slot = blk % sp->nblk, ci = sp->slot_ci[slot];
  jcomp_t *c = &j->comp[ci];
  uint32_t mx = m % sp->w, my = m / sp->w, bx, by;

  if (j->nscomp == 1u) {
    bx = mx;
    by = my;
  } else {
    bx = mx * c->h + sp->slot_h[slot];
    by = my * c->v + sp->slot_v[slot];
  }
  memset(scratch, 0, sizeof(scratch));
  if (!skip) block_seq(&x->r.b, j->dc[c->td], j->ac[c->ta], &x->dc[ci], scratch);
  jpg_put(j, c, scratch, bx, by);
}

/* (1): chunk `item` from its first byte (the first chunk's from the true
   start): boundaries in its window kept, the first past its end */
static bool
spec_pass1(void *arg, uint32_t worker, uint32_t item) {
  jspec_t  *sp = arg;
  jpg_t    *j  = sp->j;
  jctx_t   *x  = &sp->ctx[worker];
  jchunk_t *ch = &sp->ch[item];
  jsync_t   st;
  uint64_t  end = (uint64_t)ch->end * 8u;
  bool      last = item + 1u == sp->n;

  memset(&st, 0, sizeof(st));
  rd_seek(x, (uint64_t)ch->start * 8u);
  for (;;) {
    uint32_t ci = sp->slot_ci[st.slot];

    st.bit = rd_bit(&x->r);
    if (item && st.bit < ch->window) {
      if (ch->nrec == ch->cap) {
        uint32_t cap  = ch->cap ? ch->cap * 2u : 1024u;
        jsync_t *next = realloc(ch->rec, cap * sizeof(*next));

        if (!next) return false;
        ch->rec = next;
        ch->cap = cap;
      }
      ch->rec[ch->nrec++] = st;
    }
    if (last ? st.bit >= ch->window : st.bit >= end) {
      ch->cross   = st;
      ch->crossed = !last;
      return true;
    }
    if (st.blk >= sp->total) return true; /* ran off the end */
    st.dc[ci] = (int32_t)((uint32_t)st.dc[ci]
                          + (uint32_t)block_drop(&x->r.b, j->dc[j->comp[ci].td], j->ac[j->comp[ci].ta]));
    st.blk++;
    st.slot = st.slot + 1u == sp->nblk ? 0u : st.slot + 1u;
  }
}

/* (3): chunk `item` for real, from its true start to the next chunk's */
static bool
spec_pass3(void *arg, uint32_t worker, uint32_t item) {
  jspec_t  *sp = arg;
  jctx_t   *x  = &sp->ctx[worker];
  jchunk_t *ch = &sp->ch[item];
  uint32_t  blk, end = item + 1u < sp->n ? sp->ch[item + 1u].s.blk : sp->total, i;

  for (i = 0; i < J_COMPS; i++) x->dc[i] = ch->s.dc[i];
  x->lacking = false;
  rd_seek(x, ch->s.bit);
  for (blk = ch->s.blk; blk < end; blk++) {
    if (x->lacking) {
      spec_block(sp, x, blk, true);
      continue;
    }
    if (!x->r.tail && x->r.b.p >= x->r.lim) rd_tail(x);
    spec_block(sp, x, blk, false);
    /* where data runs out: as one pass, the MCUs after are skipped */
    if ((blk + 1u) % sp->nblk == 0u && rd_past(&x->r)) x->lacking = true;
  }
  return true;
}

/* (2): each chunk's true start, in order; false: fall back to one thread */
static bool
spec_pass2(jspec_t *sp, jctx_t *x) {
  jpg_t   *j = sp->j;
  jsync_t  t = sp->ch[0].cross;
  uint32_t i;

  memset(&sp->ch[0].s, 0, sizeof(sp->ch[0].s));
  if (!sp->ch[0].crossed) return false;
  for (i = 1; i < sp->n; i++) {
    jchunk_t *ch = &sp->ch[i];
    uint32_t  r  = 0, k;

    /* walk t on until the chunk's decoder passed it */
    for (;;) {
      while (r < ch->nrec && ch->rec[r].bit < t.bit) r++;
      if (r < ch->nrec && ch->rec[r].bit == t.bit && ch->rec[r].slot == t.slot) break;
      if (r >= ch->nrec || t.blk >= sp->total) {
        r = UINT32_MAX; /* never in step */
        break;
      }
      rd_seek(x, t.bit);
      {
        uint32_t ci = sp->slot_ci[t.slot];

        t.dc[ci] = (int32_t)((uint32_t)t.dc[ci] + (uint32_t)block_drop(&x->r.b, j->dc[j->comp[ci].td], j->ac[j->comp[ci].ta]));
      }
      t.bit = rd_bit(&x->r);
      t.blk++;
      t.slot = t.slot + 1u == sp->nblk ? 0u : t.slot + 1u;
    }
    if (r == UINT32_MAX) {
      /* through the chunk in order, to the next one's start */
      ch->s = t;
      if (i + 1u < sp->n) {
        uint64_t end = (uint64_t)ch->end * 8u;

        rd_seek(x, t.bit);
        while (t.bit < end && t.blk < sp->total) {
          uint32_t ci = sp->slot_ci[t.slot];

          t.dc[ci] = (int32_t)((uint32_t)t.dc[ci] + (uint32_t)block_drop(&x->r.b, j->dc[j->comp[ci].td], j->ac[j->comp[ci].ta]));
          t.bit    = rd_bit(&x->r);
          t.blk++;
          t.slot = t.slot + 1u == sp->nblk ? 0u : t.slot + 1u;
        }
      }
      continue;
    }
    ch->s = t;
    if (i + 1u < sp->n) {
      const jsync_t *a = &ch->rec[r], *e = &ch->cross;

      if (!ch->crossed) return false;
      t.blk += e->blk - a->blk;
      for (k = 0; k < J_COMPS; k++) t.dc[k] = (int32_t)((uint32_t)t.dc[k] + (uint32_t)e->dc[k] - (uint32_t)a->dc[k]);
      t.bit  = e->bit;
      t.slot = e->slot;
    }
  }
  /* starts in order, none past the end */
  for (i = 1; i < sp->n; i++) {
    if (sp->ch[i].s.blk < sp->ch[i - 1u].s.blk || sp->ch[i].s.blk > sp->total) return false;
  }
  return true;
}

/* the one-pass sequential scan's single segment on `pool`; false with
   nothing written: decode it in order */
static bool
scan_spec(jpg_t *j, im_pool_t *pool, uint32_t w, uint32_t h, size_t margin, bool *done) {
  jspec_t  sp;
  jctx_t   ctx[IM_POOL_MAX];
  jchunk_t ch[IM_POOL_MAX];
  size_t   len = j->sd.seg[0].end - j->sd.seg[0].start, part;
  uint32_t nw = im_pool_size(pool), i, s = 0;
  bool     ok = false;

  *done = false;
  memset(&sp, 0, sizeof(sp));
  memset(ch, 0, sizeof(ch));
  sp.j = j;
  sp.w = w;
  /* the MCU's blocks, in order */
  for (i = 0; i < j->nscomp; i++) {
    const jcomp_t *c  = &j->comp[j->scomp[i]];
    uint32_t       nh = j->nscomp == 1u ? 1u : c->h, nv = j->nscomp == 1u ? 1u : c->v, a, b;

    for (b = 0; b < nv; b++) {
      for (a = 0; a < nh; a++) {
        if (s >= 64u) return false;
        sp.slot_ci[s] = (uint8_t)j->scomp[i];
        sp.slot_h[s]  = (uint8_t)a;
        sp.slot_v[s]  = (uint8_t)b;
        s++;
      }
    }
  }
  sp.nblk  = s;
  sp.total = w * h * s;
  sp.n     = nw;
  if (len / sp.n < (64u << 10)) sp.n = (uint32_t)(len / (64u << 10));
  if (sp.n < 3u) return false;
  part = len / sp.n;
  for (i = 0; i < sp.n; i++) {
    ch[i].start  = part * i;
    ch[i].end    = i + 1u < sp.n ? part * (i + 1u) : len;
    ch[i].window = ((uint64_t)ch[i].start + (part / 2u < (16u << 10) ? part / 2u : (16u << 10))) * 8u;
  }
  for (i = 0; i < nw; i++) {
    if (!ctx_init(&ctx[i], j, margin)) {
      while (i--) free(ctx[i].tail);
      return false;
    }
  }
  sp.ctx = ctx;
  sp.ch  = ch;
  if (im_pool_run(pool, spec_pass1, &sp, sp.n) && spec_pass2(&sp, &ctx[0])) {
    ok    = im_pool_run(pool, spec_pass3, &sp, sp.n);
    *done = true;
  }
  for (i = 0; i < nw; i++) free(ctx[i].tail);
  for (i = 0; i < sp.n; i++) free(ch[i].rec);
  return ok;
}

/* the scan whose data `j->sd` holds; `pool` its threads, if any */
IM_HIDE bool
jpg_scan_huff(jpg_t *j, im_pool_t *pool) {
  jctx_t   x;
  uint32_t w, h, i, blocks = 0u, n, total, cur = 0u, next_rst = 0u, togo;
  size_t   margin;
  bool     at_marker = false;

  for (i = 0; i < j->nscomp; i++) {
    const jcomp_t *c = &j->comp[j->scomp[i]];

    blocks += j->nscomp == 1u ? 1u : (uint32_t)c->h * c->v;
  }
  /* what an MCU can take at most, zeros and damage included: per block a
     DC (17 + 16 bits) and 63 codes with their values (17 + 15 bits), or
     refinement's correction bits besides */
  margin = (size_t)blocks * 280u + 16u;

  if (j->nscomp == 1u) {
    w = j->comp[j->scomp[0]].sw;
    h = j->comp[j->scomp[0]].sh;
  } else {
    w = j->mcux;
    h = j->mcuy;
  }
  total = w * h;

  /* in parallel: where every interval has its segment, ended by the RST
     the next restart expects */
  if (pool && j->restart && total > j->restart) {
    uint32_t ints = (total + j->restart - 1u) / j->restart, k;
    bool     valid = j->sd.nseg >= ints;

    for (k = 0; valid && k + 1u < ints; k++) valid = j->sd.seg[k].marker == 0xD0u + (k & 7u);
    if (valid) {
      jpar_t   pa;
      jctx_t   ctx[IM_POOL_MAX];
      uint32_t nw = im_pool_size(pool), items;
      bool     ok;

      for (i = 0; i < nw; i++) {
        if (!ctx_init(&ctx[i], j, margin)) {
          while (i--) free(ctx[i].tail);
          return false;
        }
      }
      pa.j     = j;
      pa.ctx   = ctx;
      pa.w     = w;
      pa.total = total;
      pa.per   = j->restart;
      items    = nw * 4u < ints ? nw * 4u : ints;
      pa.step  = (ints + items - 1u) / items;
      items    = (ints + pa.step - 1u) / pa.step;
      pa.last_good = 0u;
      ok       = im_pool_run(pool, par_item, &pa, items);
      for (i = 0; i < nw; i++) free(ctx[i].tail);
      /* as in order: each interval starts with data, so the last one's
         last MCU read with it is the row */
      j->last_good = pa.last_good;
      return ok;
    }
  }

  /* one segment, one pass, sequential: on threads, speculatively */
  if (pool && !j->restart && !j->multiscan && j->process == J_SEQUENTIAL && j->sd.nseg >= 1u
      && j->sd.seg[0].end - j->sd.seg[0].start >= (256u << 10)) {
    bool done;

    if (scan_spec(j, pool, w, h, margin, &done)) return true;
    if (done) return false;
  }

  /* in order */
  if (!ctx_init(&x, j, margin)) return false;
  togo = j->restart;
  rd_start(&x, 0u);
  for (n = 0; n < total; n++) {
    if (j->restart) {
      if (!togo) {
        restart(&x, &next_rst, &cur, &at_marker);
        togo = j->restart;
      }
      togo--;
    }
    scan_mcu(&x, n, w);
  }
  j->last_good = x.last_good;
  free(x.tail);
  return true;
}
