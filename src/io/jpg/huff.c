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

#if (defined(__ARM_NEON) || defined(_M_ARM64)) && defined(__aarch64__) && !defined(IM_NO_SIMD)
#  define J_NZ_NEON 1
#  include <arm_neon.h>
#elif (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__)) && !defined(IM_NO_SIMD)
#  define J_NZ_X86 1
#  include <tmmintrin.h>
#elif defined(__wasm_simd128__) && !defined(IM_NO_SIMD)
#  define J_NZ_WASM 1
#  include <wasm_simd128.h>
#endif
#if defined(_MSC_VER) && !defined(__clang__)
#  include <intrin.h>
#endif

/* zigzag to natural order; past 63 all 63: a run past the block lands
   there, as libjpeg's, and so does a pair look-up's end of block (64 on:
   block_pairs) */
IM_HIDE const uint8_t jpg_natural[128] = {
  0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
  12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
  58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
  63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63,
  63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63,
  63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63,
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
  d->bytes = at;
  memset(o + at, 0, J_TAIL);
  return true;
}

/* where the scan's data from `p` ends, as jpg_unstuff finds it: the marker
   that ends it (its last fill 0xFF), or the data's end; nothing copied */
IM_HIDE const uint8_t*
jpg_scan_end(const uint8_t *p, const uint8_t *end) {
  for (;;) {
    const uint8_t *ff = memchr(p, 0xFF, (size_t)(end - p)), *q;

    if (!ff) return end;
    for (q = ff + 1; q < end && *q == 0xFFu; q++) {
    }
    if (q >= end) return end;
    if (*q == 0x00u || (*q >= 0xD0u && *q <= 0xD7u) || *q < 0xC0u) {
      p = q + 1;
      continue;
    }
    return q - 1;
  }
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
   past it; none (damage): 17 bits, symbol 0, as libjpeg. Its length << 8 |
   its symbol; the bits by value, so a caller's stay in its registers */
static uint32_t
huff_long(uint64_t buf, const jhuff_t *h) {
  uint32_t code = (uint32_t)(buf >> 48), l;

  for (l = J_HBITS + 1u; l <= 16u; l++) {
    int32_t c = (int32_t)(code >> (16u - l));

    if (c <= h->maxcode[l]) return l << 8 | h->vals[(uint32_t)(c + h->valoff[l]) & 0xFFu];
  }
  return 17u << 8;
}

IM_INLINE uint32_t
huff_slow(jbits_t *b, const jhuff_t *h) {
  uint32_t r = huff_long(b->buf, h);

  DROP(b, r >> 8);
  return r & 0xFFu;
}

/* the next symbol (32 bits at hand: a code and a value's bits) */
IM_INLINE uint32_t
huff_sym(jbits_t *b, const jhuff_t *h) {
  uint32_t e;

  if (b->n < 32u) refill(b);
  e = h->fast[PEEK(b, J_HBITS)];
  if (likely(e)) {
    uint32_t sym = (e >> 8) & 0xFFu, len = e & 63u;

    if (e & J_FUSED) len -= sym & 15u; /* the value's bits stay */
    DROP(b, len);
    return sym;
  }
  return huff_slow(b, h);
}

/* -- tables -------------------------------------------------------------------- */

/* libjpeg's checks (jpeg_make_d_derived_tbl): no more codes than a
   length holds; a DC symbol 15 at most (16 lossless) */
IM_HIDE bool
jpg_huff_build(jhuff_t *h, const uint8_t counts[16], const uint8_t *vals, bool dc, bool lossless) {
  uint32_t len, i, p = 0u, code = 0u, total = 0u, end = 0u;
  uint16_t codes[256];
  uint8_t  sizes[256];

  /* all but the look-ups (those past the short codes cleared below); the
     pairs, made for the old codes, go */
  free(h->pair);
  memset(&h->maxcode, 0, sizeof(*h) - offsetof(jhuff_t, maxcode));
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

  /* one look: the code, and its value where both fit; the short codes
     fill the look-ups from the first on, one after another (canonical),
     the rest none */
  for (i = 0; i < total; i++) {
    uint32_t l = sizes[i], sym = h->vals[i], s = sym & 15u, first, span, f;

    if (l > J_HBITS) break;
    first = (uint32_t)codes[i] << (J_HBITS - l);
    span  = 1u << (J_HBITS - l);
    if (l + s <= J_HBITS && !(dc && sym == 16u)) {
      /* its value's bits after the code: 1 << sh look-ups each value v,
         the value extend(v, s) in bits 16–31. In each half of the values
         that's linear in v (the lower half's v - 2^s + 1, the upper's v),
         so a look-up is its half's first plus (k >> sh) << 16: two plain
         loops (vectorized), no extend a value */
      uint32_t  sh = J_HBITS - l - s, base = (l + s) | J_FUSED | sym << 8, half = s ? span >> 1 : span;
      uint32_t  lo = s ? base + ((1u - (1u << s)) << 16) : base, k;
      uint32_t *q  = h->fast + first;

      for (k = 0; k < half; k++) q[k] = lo + ((k >> sh) << 16);
      for (k = half; k < span; k++) q[k] = base + ((k >> sh) << 16);
    } else {
      uint32_t e = l | sym << 8, *q = h->fast + first;

      for (f = 0; f < span; f++) q[f] = e;
    }
    end = first + span;
  }
  memset(h->fast + end, 0, (J_HSIZE - end) * sizeof(h->fast[0]));
  h->ok = true;
  return true;
}

IM_HIDE void
jpg_huff_free(jhuff_t *h) {
  if (h) free(h->pair);
  free(h);
}

/* An AC table's pairs: for each J_HBITS bits, the symbol they start with
   and, where its code and value leave room, the next one's too. A symbol
   is a step and a value: k moves by the step, the value goes at k, k
   moves on one. A coefficient's step is its run; ZRL's 15 with value 0
   (the 16th zero written: it was zero); an end of block (EOB, or any run
   with no size but 15, as libjpeg reads it) 64, value 0, which lands on
   jpg_natural's 63 — still zero, no block reaching there goes on — and
   ends the block. A single's second is a step of 0 that k doesn't move
   past (JP_TWO clear): the zero goes where the next coefficient would.
     bits 0–5    bits taken: both symbols', or the one's
     bits 6–11   the first's alone (where the block ends with it)
     bits 16–22  the first's step
     JP_OK       a look-up: the first is code and value in J_HBITS bits
                 (else the long way, one symbol)
     bits 24–30  the second's step
     JP_TWO      a second symbol
     bits 32–47  the first's value, 48–63 the second's (signed) */
#define JP_OK  ((uint64_t)1 << 23)
#define JP_TWO ((uint64_t)1 << 31)

static uint32_t
pair_step(uint32_t sym) {
  return (sym & 15u) ? sym >> 4 : sym == 0xF0u ? 15u : 64u;
}

static uint64_t*
huff_pairs(const jhuff_t *h) {
  uint64_t *t = malloc(J_HSIZE * sizeof(*t));
  uint32_t  x;

  if (!t) return NULL;
  for (x = 0; x < J_HSIZE; x++) {
    uint32_t e1 = h->fast[x], l1 = e1 & 63u, s1 = (e1 >> 8) & 0xFFu, p1 = pair_step(s1);
    uint64_t e;

    if (!(e1 & J_FUSED)) {
      t[x] = 0u;
      continue;
    }
    e = l1 | (uint64_t)l1 << 6 | (uint64_t)p1 << 16 | JP_OK | (uint64_t)(uint16_t)(e1 >> 16) << 32;
    if (p1 != 64u && l1 < J_HBITS) {
      /* the next symbol where it's whole in the bits left: its look-up
         then depends on those bits only */
      uint32_t e2 = h->fast[(x << l1) & (J_HSIZE - 1u)], l2 = e2 & 63u;

      if ((e2 & J_FUSED) && l1 + l2 <= J_HBITS) {
        e = (e & ~(uint64_t)63u) | (l1 + l2);
        e |= (uint64_t)pair_step((e2 >> 8) & 0xFFu) << 24 | JP_TWO | (uint64_t)(uint16_t)(e2 >> 16) << 48;
      }
    }
    t[x] = e;
  }
  return t;
}

/* a DC table's look-ups with the first AC symbol of `ac`'s after them,
   in huff_pairs' form: the first symbol the DC difference (its step 0,
   unused), the second the AC one where it fits */
static uint64_t*
huff_dcac(const jhuff_t *dc, const jhuff_t *ac) {
  uint64_t *t = malloc(J_HSIZE * sizeof(*t));
  uint32_t  x;

  if (!t) return NULL;
  for (x = 0; x < J_HSIZE; x++) {
    uint32_t e1 = dc->fast[x], l1 = e1 & 63u;
    uint64_t e;

    if (!(e1 & J_FUSED)) {
      t[x] = 0u;
      continue;
    }
    e = l1 | (uint64_t)l1 << 6 | JP_OK | (uint64_t)(uint16_t)(e1 >> 16) << 32;
    if (l1 < J_HBITS) {
      uint32_t e2 = ac->fast[(x << l1) & (J_HSIZE - 1u)], l2 = e2 & 63u;

      if ((e2 & J_FUSED) && l1 + l2 <= J_HBITS) {
        e = (e & ~(uint64_t)63u) | (l1 + l2);
        e |= (uint64_t)pair_step((e2 >> 8) & 0xFFu) << 24 | JP_TWO | (uint64_t)(uint16_t)(e2 >> 16) << 48;
      }
    }
    t[x] = e;
  }
  return t;
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
    (*slot)->pair = NULL; /* the rest the build sets */
    (*slot)->pair_ac = NULL;
    jpg_huff_build(*slot, std[i].bits, std[i].vals, std[i].dc, false);
  }
}

/* -- blocks -------------------------------------------------------------------- */

/* the long way, one symbol, for the pair loops: its run and size (rs), the
   value read; ZRL and the end of block as huffman gives them */
IM_INLINE uint32_t
pair_long(jbits_t *b, const jhuff_t *act, int32_t *v) {
  uint32_t f = act->fast[PEEK(b, J_HBITS)], rs, s;

  if (f) {
    DROP(b, f & 63u);
    rs = (f >> 8) & 0xFFu;
  } else {
    rs = huff_slow(b, act);
  }
  s  = rs & 15u;
  *v = s ? extend(get_bits(b, s), s) : 0;
  return rs;
}

/* a block's AC coefficients through its table's pairs (huff_pairs): two
   symbols a look-up where they fit, the same bits taken as one by one */
IM_INLINE void
block_pairs(jbits_t *b, const jhuff_t *act, int16_t *blk, uint32_t k) {
  const uint64_t *pt = act->pair;

  while (k < 64u) {
    uint64_t e;

    if (b->n < 32) refill(b);
    e = pt[PEEK(b, J_HBITS)];
    if (likely(e & JP_OK)) {
      k += (uint32_t)(e >> 16) & 127u;
      blk[jpg_natural[k]] = (int16_t)(e >> 32);
      k++;
      if (unlikely(k >= 64u)) {
        DROP(b, (e >> 6) & 63u);
        return;
      }
      DROP(b, e & 63u);
      k += (uint32_t)(e >> 24) & 127u;
      blk[jpg_natural[k]] = (int16_t)(e >> 48);
      k += (uint32_t)(e >> 31) & 1u;
    } else {
      /* a long code, or a value past the look-up: one symbol */
      int32_t  v;
      uint32_t rs = pair_long(b, act, &v);

      if (rs & 15u) {
        k += rs >> 4;
        blk[jpg_natural[k]] = (int16_t)v;
        k++;
      } else if (rs == 0xF0u) {
        k += 16u;
      } else {
        return;
      }
    }
  }
}

/* a sequential block's coefficients into `blk` (zeros where none) */
IM_INLINE void
block_seq(jbits_t *b, const jhuff_t *dct, const jhuff_t *act, int32_t *dc, int16_t *blk) {
  uint32_t e, k;
  int32_t  diff;

  if (b->n < 32) refill(b);
  if (dct->pair_ac == act) {
    /* the DC and the first AC symbol in one look-up (huff_dcac) */
    uint64_t p = dct->pair[PEEK(b, J_HBITS)];

    if (likely(p & JP_OK)) {
      DROP(b, p & 63u);
      *dc    = (int32_t)((uint32_t)*dc + (uint32_t)(int16_t)(p >> 32));
      blk[0] = (int16_t)*dc;
      k      = 1u + ((uint32_t)(p >> 24) & 127u);
      blk[jpg_natural[k]] = (int16_t)(p >> 48);
      block_pairs(b, act, blk, k + ((uint32_t)(p >> 31) & 1u));
      return;
    }
  }
  e = dct->fast[PEEK(b, J_HBITS)];
  if (likely(e & J_FUSED)) {
    DROP(b, e & 63u);
    diff = (int32_t)e >> 16;
  } else {
    uint32_t s;

    if (e) {
      DROP(b, e & 63u);
      s = (e >> 8) & 15u;
    } else {
      s = huff_slow(b, dct) & 15u;
    }

    diff = extend(get_bits(b, s), s);
  }
  *dc    = (int32_t)((uint32_t)*dc + (uint32_t)diff);
  blk[0] = (int16_t)*dc;
  if (act->pair) {
    block_pairs(b, act, blk, 1u);
    return;
  }

  for (k = 1; k < 64u; k++) {
    uint32_t rs, s;

    if (b->n < 32) refill(b);
    e = act->fast[PEEK(b, J_HBITS)];
    if (likely(e & J_FUSED)) {
      DROP(b, e & 63u);
      rs = (e >> 8) & 0xFFu;
      if (rs & 15u) {
        k += rs >> 4;
        blk[jpg_natural[k]] = (int16_t)((int32_t)e >> 16);
        continue;
      }
    } else {
      if (e) {
        DROP(b, e & 63u);
        rs = (e >> 8) & 0xFFu;
      } else {
        rs = huff_slow(b, act);
      }
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
  bool     ssse3;     /* x86: the masks by shuffles */
} jctx_t;

/* a progressive scan's part of a block (G.1.2, as libjpeg's jdphuff.c) */
IM_INLINE void
block_dc_first(jctx_t *x, jbits_t *b, uint32_t ci, int16_t *blk) {
  const jpg_t *j = x->j;
  uint32_t     s = huff_sym(b, j->dc[j->comp[ci].td]) & 15u;
  int32_t      diff = extend(get_bits(b, s), s);

  x->dc[ci] = j->diff ? diff : (int32_t)((uint32_t)x->dc[ci] + (uint32_t)diff); /* J.2.3.1: none predicted */
  blk[0]    = (int16_t)((uint32_t)x->dc[ci] << j->al);
}

IM_INLINE void
block_dc_refine(const jpg_t *j, jbits_t *b, int16_t *blk) {
  if (get_bits(b, 1u)) blk[0] = (int16_t)(blk[0] | (1 << j->al));
}

IM_INLINE void
block_ac_first(jctx_t *x, jbits_t *b, const jcomp_t *c, int16_t *blk) {
  const jpg_t   *j = x->j;
  const jhuff_t *h = j->ac[c->ta];
  uint32_t       k;

  if (x->eobrun) {
    x->eobrun--;
    return;
  }
  for (k = j->ss; k <= j->se; k++) {
    uint32_t rs, r, s, e;

    /* code and value in one look-up where they fit (block_seq's) */
    if (b->n < 32) refill(b);
    e = h->fast[PEEK(b, J_HBITS)];
    if (likely(e & J_FUSED)) {
      DROP(b, e & 63u);
      rs = (e >> 8) & 0xFFu;
      if (rs & 15u) {
        k += rs >> 4;
        blk[jpg_natural[k]] = (int16_t)((uint32_t)((int32_t)e >> 16) << j->al);
        continue;
      }
    } else if (e) {
      DROP(b, e & 63u);
      rs = (e >> 8) & 0xFFu;
    } else {
      rs = huff_slow(b, h);
    }
    r = rs >> 4;
    s = rs & 15u;
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

/* -- refinement by masks ---------------------------------------------------- */

/* An AC refinement walks the band: a correction bit for each coefficient
   already nonzero, a symbol's run counting the zeros. On a mask of the
   nonzero ones (bit k: zigzag position k) a run's end is the (r+1)th zero
   bit and the corrections on the way one read, so no branch on what each
   coefficient holds (in a detailed image, a coin's toss) */

IM_INLINE uint32_t
ctz64(uint64_t v) {
#if defined(__GNUC__) || defined(__clang__)
  return (uint32_t)__builtin_ctzll(v);
#elif defined(_MSC_VER) && defined(_WIN64)
  unsigned long i;

  _BitScanForward64(&i, v);
  return (uint32_t)i;
#else
  uint32_t n = 0u;

  while (!(v & 1u)) {
    v >>= 1;
    n++;
  }
  return n;
#endif
}

IM_INLINE uint32_t
popcount64(uint64_t v) {
#if defined(__GNUC__) || defined(__clang__)
  return (uint32_t)__builtin_popcountll(v);
#else
  v = v - ((v >> 1) & 0x5555555555555555ull);
  v = (v & 0x3333333333333333ull) + ((v >> 2) & 0x3333333333333333ull);
  v = (v + (v >> 4)) & 0x0F0F0F0F0F0F0F0Full;
  return (uint32_t)((v * 0x0101010101010101ull) >> 56);
#endif
}

/* the block's nonzero coefficients, bit k for zigzag position k */
#if J_NZ_NEON
/* 16 coefficients' "nonzero" bytes (0xFF) */
IM_INLINE uint8x16_t
jn_nz16(const int16_t *p) {
  int16x8_t a = vld1q_s16(p), b = vld1q_s16(p + 8);

  return vcombine_u8(vmovn_u16(vtstq_s16(a, a)), vmovn_u16(vtstq_s16(b, b)));
}

IM_INLINE uint64_t
nonzero_zz(const jctx_t *x, const int16_t *blk) {
  static const uint8_t wb[16] = { 1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128 };
  const uint8x16_t     w      = vld1q_u8(wb);
  uint8x16x4_t         t      = { { jn_nz16(blk), jn_nz16(blk + 16), jn_nz16(blk + 32), jn_nz16(blk + 48) } };
  uint8x16_t           z0, z1, z2, z3, sum;

  (void)x;
  z0  = vandq_u8(vqtbl4q_u8(t, vld1q_u8(jpg_natural)), w);
  z1  = vandq_u8(vqtbl4q_u8(t, vld1q_u8(jpg_natural + 16)), w);
  z2  = vandq_u8(vqtbl4q_u8(t, vld1q_u8(jpg_natural + 32)), w);
  z3  = vandq_u8(vqtbl4q_u8(t, vld1q_u8(jpg_natural + 48)), w);
  sum = vpaddq_u8(vpaddq_u8(z0, z1), vpaddq_u8(z2, z3));
  sum = vpaddq_u8(sum, sum);
  return vgetq_lane_u64(vreinterpretq_u64_u8(sum), 0);
}
#else
#  if !J_NZ_WASM
static uint64_t
nonzero_plain(const int16_t *blk) {
  uint64_t m = 0u;
  uint32_t k;

  for (k = 0; k < 64u; k++) m |= (uint64_t)(blk[jpg_natural[k]] != 0) << k;
  return m;
}
#  endif

#  if J_NZ_X86 || J_NZ_WASM
/* zigzag position 16c + i from the zero bytes of natural 16s…16s+15:
   jpg_natural's low four bits where its high ones are s, else nothing */
static const uint8_t zz_pick[4][4][16] = {
  { { 0x00, 0x01, 0x08, 0x80, 0x09, 0x02, 0x03, 0x0A, 0x80, 0x80, 0x80, 0x80, 0x80, 0x0B, 0x04, 0x05 },
    { 0x80, 0x80, 0x80, 0x00, 0x80, 0x80, 0x80, 0x80, 0x01, 0x08, 0x80, 0x09, 0x02, 0x80, 0x80, 0x80 },
    { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x80, 0x80, 0x80, 0x80, 0x80 },
    { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 } },
  { { 0x0C, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x0D, 0x06, 0x07, 0x0E, 0x80, 0x80 },
    { 0x80, 0x03, 0x0A, 0x80, 0x80, 0x80, 0x80, 0x80, 0x0B, 0x04, 0x80, 0x80, 0x80, 0x80, 0x05, 0x0C },
    { 0x80, 0x80, 0x80, 0x01, 0x08, 0x80, 0x09, 0x02, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 },
    { 0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 } },
  { { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x0F, 0x80, 0x80, 0x80, 0x80, 0x80 },
    { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x0D, 0x06, 0x80, 0x07, 0x0E, 0x80, 0x80, 0x80 },
    { 0x03, 0x0A, 0x80, 0x80, 0x80, 0x80, 0x0B, 0x04, 0x80, 0x80, 0x80, 0x80, 0x80, 0x05, 0x0C, 0x80 },
    { 0x80, 0x80, 0x01, 0x08, 0x09, 0x02, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x03 } },
  { { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 },
    { 0x80, 0x80, 0x80, 0x80, 0x80, 0x0F, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 },
    { 0x80, 0x80, 0x80, 0x0D, 0x06, 0x80, 0x07, 0x0E, 0x80, 0x80, 0x80, 0x80, 0x0F, 0x80, 0x80, 0x80 },
    { 0x0A, 0x0B, 0x04, 0x80, 0x80, 0x80, 0x80, 0x80, 0x05, 0x0C, 0x0D, 0x06, 0x80, 0x07, 0x0E, 0x0F } }
};

#  endif

#  if J_NZ_WASM
/* the tables' 0x80: past 16, swizzle's zero */
static uint64_t
nonzero_wasm(const int16_t *blk) {
  const v128_t zero = wasm_i16x8_splat(0);
  v128_t       src[4];
  uint64_t     m = 0u;
  uint32_t     c, k;

  for (k = 0; k < 4u; k++) {
    src[k] = wasm_i8x16_narrow_i16x8(wasm_i16x8_eq(wasm_v128_load(blk + 16u * k), zero),
                                     wasm_i16x8_eq(wasm_v128_load(blk + 16u * k + 8u), zero));
  }
  for (c = 0; c < 4u; c++) {
    v128_t acc = wasm_i8x16_splat(0);

    for (k = 0; k < 4u; k++) acc = wasm_v128_or(acc, wasm_i8x16_swizzle(src[k], wasm_v128_load(zz_pick[c][k])));
    m |= (uint64_t)wasm_i8x16_bitmask(acc) << (16u * c);
  }
  return ~m;
}
#  endif

#  if J_NZ_X86
__attribute__((target("ssse3"))) static uint64_t
nonzero_ssse3(const int16_t *blk) {
  const __m128i zero = _mm_setzero_si128();
  __m128i       src[4];
  uint64_t      m = 0u;
  uint32_t      c, k;

  for (k = 0; k < 4u; k++) {
    __m128i a = _mm_loadu_si128((const __m128i *)(const void *)(blk + 16u * k));
    __m128i b = _mm_loadu_si128((const __m128i *)(const void *)(blk + 16u * k + 8u));

    src[k] = _mm_packs_epi16(_mm_cmpeq_epi16(a, zero), _mm_cmpeq_epi16(b, zero));
  }
  for (c = 0; c < 4u; c++) {
    __m128i acc = zero;

    for (k = 0; k < 4u; k++) {
      acc = _mm_or_si128(acc, _mm_shuffle_epi8(src[k], _mm_loadu_si128((const __m128i *)(const void *)zz_pick[c][k])));
    }
    m |= (uint64_t)(uint32_t)_mm_movemask_epi8(acc) << (16u * c);
  }
  return ~m;
}
#  endif

IM_INLINE uint64_t
nonzero_zz(const jctx_t *x, const int16_t *blk) {
#  if J_NZ_X86
  if (x->ssse3) return nonzero_ssse3(blk);
#  endif
  (void)x;
#  if J_NZ_WASM
  return nonzero_wasm(blk);
#  else
  return nonzero_plain(blk);
#  endif
}
#endif

/* a byte's set bits' places, in order, a byte each (the rest 0) */
static const uint64_t jbitpos[256] = {
  0x0000000000000000ull, 0x0000000000000000ull, 0x0000000000000001ull, 0x0000000000000100ull,
  0x0000000000000002ull, 0x0000000000000200ull, 0x0000000000000201ull, 0x0000000000020100ull,
  0x0000000000000003ull, 0x0000000000000300ull, 0x0000000000000301ull, 0x0000000000030100ull,
  0x0000000000000302ull, 0x0000000000030200ull, 0x0000000000030201ull, 0x0000000003020100ull,
  0x0000000000000004ull, 0x0000000000000400ull, 0x0000000000000401ull, 0x0000000000040100ull,
  0x0000000000000402ull, 0x0000000000040200ull, 0x0000000000040201ull, 0x0000000004020100ull,
  0x0000000000000403ull, 0x0000000000040300ull, 0x0000000000040301ull, 0x0000000004030100ull,
  0x0000000000040302ull, 0x0000000004030200ull, 0x0000000004030201ull, 0x0000000403020100ull,
  0x0000000000000005ull, 0x0000000000000500ull, 0x0000000000000501ull, 0x0000000000050100ull,
  0x0000000000000502ull, 0x0000000000050200ull, 0x0000000000050201ull, 0x0000000005020100ull,
  0x0000000000000503ull, 0x0000000000050300ull, 0x0000000000050301ull, 0x0000000005030100ull,
  0x0000000000050302ull, 0x0000000005030200ull, 0x0000000005030201ull, 0x0000000503020100ull,
  0x0000000000000504ull, 0x0000000000050400ull, 0x0000000000050401ull, 0x0000000005040100ull,
  0x0000000000050402ull, 0x0000000005040200ull, 0x0000000005040201ull, 0x0000000504020100ull,
  0x0000000000050403ull, 0x0000000005040300ull, 0x0000000005040301ull, 0x0000000504030100ull,
  0x0000000005040302ull, 0x0000000504030200ull, 0x0000000504030201ull, 0x0000050403020100ull,
  0x0000000000000006ull, 0x0000000000000600ull, 0x0000000000000601ull, 0x0000000000060100ull,
  0x0000000000000602ull, 0x0000000000060200ull, 0x0000000000060201ull, 0x0000000006020100ull,
  0x0000000000000603ull, 0x0000000000060300ull, 0x0000000000060301ull, 0x0000000006030100ull,
  0x0000000000060302ull, 0x0000000006030200ull, 0x0000000006030201ull, 0x0000000603020100ull,
  0x0000000000000604ull, 0x0000000000060400ull, 0x0000000000060401ull, 0x0000000006040100ull,
  0x0000000000060402ull, 0x0000000006040200ull, 0x0000000006040201ull, 0x0000000604020100ull,
  0x0000000000060403ull, 0x0000000006040300ull, 0x0000000006040301ull, 0x0000000604030100ull,
  0x0000000006040302ull, 0x0000000604030200ull, 0x0000000604030201ull, 0x0000060403020100ull,
  0x0000000000000605ull, 0x0000000000060500ull, 0x0000000000060501ull, 0x0000000006050100ull,
  0x0000000000060502ull, 0x0000000006050200ull, 0x0000000006050201ull, 0x0000000605020100ull,
  0x0000000000060503ull, 0x0000000006050300ull, 0x0000000006050301ull, 0x0000000605030100ull,
  0x0000000006050302ull, 0x0000000605030200ull, 0x0000000605030201ull, 0x0000060503020100ull,
  0x0000000000060504ull, 0x0000000006050400ull, 0x0000000006050401ull, 0x0000000605040100ull,
  0x0000000006050402ull, 0x0000000605040200ull, 0x0000000605040201ull, 0x0000060504020100ull,
  0x0000000006050403ull, 0x0000000605040300ull, 0x0000000605040301ull, 0x0000060504030100ull,
  0x0000000605040302ull, 0x0000060504030200ull, 0x0000060504030201ull, 0x0006050403020100ull,
  0x0000000000000007ull, 0x0000000000000700ull, 0x0000000000000701ull, 0x0000000000070100ull,
  0x0000000000000702ull, 0x0000000000070200ull, 0x0000000000070201ull, 0x0000000007020100ull,
  0x0000000000000703ull, 0x0000000000070300ull, 0x0000000000070301ull, 0x0000000007030100ull,
  0x0000000000070302ull, 0x0000000007030200ull, 0x0000000007030201ull, 0x0000000703020100ull,
  0x0000000000000704ull, 0x0000000000070400ull, 0x0000000000070401ull, 0x0000000007040100ull,
  0x0000000000070402ull, 0x0000000007040200ull, 0x0000000007040201ull, 0x0000000704020100ull,
  0x0000000000070403ull, 0x0000000007040300ull, 0x0000000007040301ull, 0x0000000704030100ull,
  0x0000000007040302ull, 0x0000000704030200ull, 0x0000000704030201ull, 0x0000070403020100ull,
  0x0000000000000705ull, 0x0000000000070500ull, 0x0000000000070501ull, 0x0000000007050100ull,
  0x0000000000070502ull, 0x0000000007050200ull, 0x0000000007050201ull, 0x0000000705020100ull,
  0x0000000000070503ull, 0x0000000007050300ull, 0x0000000007050301ull, 0x0000000705030100ull,
  0x0000000007050302ull, 0x0000000705030200ull, 0x0000000705030201ull, 0x0000070503020100ull,
  0x0000000000070504ull, 0x0000000007050400ull, 0x0000000007050401ull, 0x0000000705040100ull,
  0x0000000007050402ull, 0x0000000705040200ull, 0x0000000705040201ull, 0x0000070504020100ull,
  0x0000000007050403ull, 0x0000000705040300ull, 0x0000000705040301ull, 0x0000070504030100ull,
  0x0000000705040302ull, 0x0000070504030200ull, 0x0000070504030201ull, 0x0007050403020100ull,
  0x0000000000000706ull, 0x0000000000070600ull, 0x0000000000070601ull, 0x0000000007060100ull,
  0x0000000000070602ull, 0x0000000007060200ull, 0x0000000007060201ull, 0x0000000706020100ull,
  0x0000000000070603ull, 0x0000000007060300ull, 0x0000000007060301ull, 0x0000000706030100ull,
  0x0000000007060302ull, 0x0000000706030200ull, 0x0000000706030201ull, 0x0000070603020100ull,
  0x0000000000070604ull, 0x0000000007060400ull, 0x0000000007060401ull, 0x0000000706040100ull,
  0x0000000007060402ull, 0x0000000706040200ull, 0x0000000706040201ull, 0x0000070604020100ull,
  0x0000000007060403ull, 0x0000000706040300ull, 0x0000000706040301ull, 0x0000070604030100ull,
  0x0000000706040302ull, 0x0000070604030200ull, 0x0000070604030201ull, 0x0007060403020100ull,
  0x0000000000070605ull, 0x0000000007060500ull, 0x0000000007060501ull, 0x0000000706050100ull,
  0x0000000007060502ull, 0x0000000706050200ull, 0x0000000706050201ull, 0x0000070605020100ull,
  0x0000000007060503ull, 0x0000000706050300ull, 0x0000000706050301ull, 0x0000070605030100ull,
  0x0000000706050302ull, 0x0000070605030200ull, 0x0000070605030201ull, 0x0007060503020100ull,
  0x0000000007060504ull, 0x0000000706050400ull, 0x0000000706050401ull, 0x0000070605040100ull,
  0x0000000706050402ull, 0x0000070605040200ull, 0x0000070605040201ull, 0x0007060504020100ull,
  0x0000000706050403ull, 0x0000070605040300ull, 0x0000070605040301ull, 0x0007060504030100ull,
  0x0000070605040302ull, 0x0007060504030200ull, 0x0007060504030201ull, 0x0706050403020100ull
};

/* a refinement's symbol (its run and size, rs) and the new coefficient's
   sign where it has one (*v: 1 or -1, else 0): with its sign bit in one
   look-up where the size is 1. libjpeg reads one sign bit whatever the
   size (another is damage), so only a size of 0 or 1 is taken fused */
IM_INLINE uint32_t
ref_sym(jbits_t *b, const jhuff_t *h, int32_t *v) {
  uint32_t e, rs;

  if (b->n < 32u) refill(b);
  e = h->fast[PEEK(b, J_HBITS)];
  if (likely((e & J_FUSED) && ((e >> 8) & 15u) <= 1u)) {
    DROP(b, e & 63u);
    *v = (int32_t)e >> 16;
    return (e >> 8) & 0xFFu;
  }
  if (e) {
    rs = (e >> 8) & 0xFFu;
    DROP(b, (e & J_FUSED) ? (e & 63u) - (rs & 15u) : e & 63u); /* the code's bits */
  } else {
    rs = huff_slow(b, h);
  }
  *v = (rs & 15u) ? (get_bits(b, 1u) ? 1 : -1) : 0;
  return rs;
}

/* `s` bits, none to 63, without a branch on none (the reader's next
   place shouldn't wait for one) */
IM_INLINE uint64_t
take_bits(jbits_t *b, uint32_t s) {
  uint64_t v;

  if (unlikely(s > 56u)) {
    v = (uint64_t)get_bits(b, 32u) << (s - 32u);
    return v | get_bits(b, s - 32u);
  }
  if (b->n < s) refill(b);
  v = (b->buf >> 1) >> (63u - s);
  DROP(b, s);
  return v;
}

/* the block's correction bits, `cb`'s low `ncb` in order, onto its first
   `ncb` nonzero coefficients in zigzag order (`nz`) */
IM_INLINE void
corr_apply(int16_t *blk, uint64_t nz, uint64_t cb, uint32_t ncb, int32_t p1) {
  for (; ncb; ncb--) {
    int16_t *q  = &blk[jpg_natural[ctz64(nz)]];
    int32_t  cv = *q;
    uint32_t on = (uint32_t)(cb >> (ncb - 1u)) & 1u & (uint32_t)((cv & p1) == 0);

    *q = (int16_t)(cv + ((cv >= 0 ? p1 : -p1) & -(int32_t)on));
    nz &= nz - 1u;
  }
}

/* Over a whole block, each coefficient nonzero before the scan has one
   correction bit, in zigzag order, whichever symbol passes it: so a symbol
   only counts its corrections (the reader's next place), and they're put
   on together at the block's end (a new coefficient is where there was
   none, never corrected) */
IM_INLINE void
block_ac_refine(jctx_t *x, jbits_t *b, const jcomp_t *c, int16_t *blk) {
  const jpg_t   *j    = x->j;
  const jhuff_t *h    = j->ac[c->ta];
  int32_t        p1   = 1 << j->al;
  uint32_t       k    = j->ss, se = j->se, ncb = 0u;
  uint64_t       band = (~0ull << k) & (~0ull >> (63u - se));
  uint64_t       nz   = nonzero_zz(x, blk) & band, cb = 0u;

  if (!nz) {
    /* none before (most of a smooth image's): nothing to correct, a run
       counts positions */
    if (x->eobrun) {
      x->eobrun--;
      return;
    }
    while (k <= se) {
      int32_t  sg;
      uint32_t rs = ref_sym(b, h, &sg), s = rs & 15u, r = rs >> 4, t;

      if (s) {
        t = k + r > se ? se + 1u : k + r;
        blk[jpg_natural[t]] = (int16_t)(sg > 0 ? p1 : -p1);
      } else if (r != 15u) {
        x->eobrun = 1u << r;
        if (r) x->eobrun += get_bits(b, r);
        x->eobrun--;
        return;
      } else {
        t = k + 15u > se ? se + 1u : k + 15u;
      }
      k = t + 1u;
    }
    return;
  }
  if (!x->eobrun) {
    /* the band's zeros' places in order (a run's end: the next r + 1th),
       eight at a time by their bytes */
    IM_ALIGN(16) uint8_t zp[64 + 8];
    uint64_t zm = ~nz & band, bc, pre;
    uint32_t nzero = popcount64(zm), z = 0u, g;

    bc  = zm - ((zm >> 1) & 0x5555555555555555ull);
    bc  = (bc & 0x3333333333333333ull) + ((bc >> 2) & 0x3333333333333333ull);
    bc  = (bc + (bc >> 4)) & 0x0F0F0F0F0F0F0F0Full; /* each byte's count */
    pre = (bc * 0x0101010101010101ull) << 8;         /* the bytes' before it */
    for (g = 0; g < 8u; g++) {
      uint64_t at = jbitpos[(zm >> (8u * g)) & 0xFFu] + 0x0808080808080808ull * g;

      memcpy(zp + ((pre >> (8u * g)) & 0xFFu), &at, 8u);
    }
    while (k <= se) {
      int32_t  sg, v = 0;
      uint32_t rs = ref_sym(b, h, &sg), s = rs & 15u, r = rs >> 4, t, nc;

      if (s) {
        v = sg > 0 ? p1 : -p1;
      } else if (r != 15u) {
        x->eobrun = 1u << r;
        if (r) x->eobrun += get_bits(b, r);
        break;
      }
      if (likely(z + r < nzero)) {
        t  = zp[z + r];
        nc = t - k - r;
        z += r + 1u;
      } else {
        t  = se + 1u;
        nc = popcount64(nz & (~0ull << k));
      }
      cb = cb << nc | take_bits(b, nc);
      ncb += nc;
      if (v) blk[jpg_natural[t]] = (int16_t)v;
      k = t + 1u;
    }
  }
  if (x->eobrun) {
    uint64_t m = k <= se ? nz & (~0ull << k) : 0u;

    x->eobrun--;
    if (!m) {
      /* in a run, nothing before: nothing more (most of a smooth image's) */
      if (ncb) corr_apply(blk, nz, cb, ncb, p1);
      return;
    }
    cb = cb << popcount64(m) | take_bits(b, popcount64(m));
    ncb += popcount64(m);
  }
  corr_apply(blk, nz, cb, ncb, p1);
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
            if (j->diff) x->dc[ci] = 0; /* a differential frame's DC: none predicted */
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

/* MCUs n0 … n1 - 1 of a progressive scan, no restart among them, from
   where the reader is: as scan_mcu one by one, but the reader in
   registers, the block's part chosen once, the blocks' places stepped
   along a row, the check for data run out only near the segment's end
   (where it can be); a first AC scan's blocks in an EOB run passed over
   together. A scan with a flow waits at each row's start as before */
enum { J_DC_FIRST, J_DC_REFINE, J_AC_FIRST, J_AC_REFINE };
enum { J_MCU_BLOCKS = 64 };

typedef struct jpblk {
  jcomp_t *c;
  uint32_t ci, dx, dy, nh, nv;
} jpblk_t;

static void
prog_range(jctx_t *x, uint32_t w, uint32_t n0, uint32_t n1) {
  jpg_t         *j    = x->j;
  jbits_t        bs   = x->r.b;
  const uint32_t kind = j->ss == 0u ? (j->ah ? J_DC_REFINE : J_DC_FIRST) : (j->ah ? J_AC_REFINE : J_AC_FIRST);
  uint32_t       n = n0, mx = n0 % w, my = n0 / w, nb = 0u, i;
  jpblk_t        list[J_MCU_BLOCKS];

  /* an MCU's blocks, where the scan has more than one component */
  for (i = 0; j->nscomp > 1u && i < j->nscomp; i++) {
    uint32_t ci = j->scomp[i], dx, dy;
    jcomp_t *c  = &j->comp[ci];

    for (dy = 0; dy < c->v; dy++) {
      for (dx = 0; dx < c->h && nb < J_MCU_BLOCKS; dx++) {
        jpblk_t *e = &list[nb++];

        e->c  = c;
        e->ci = ci;
        e->dx = dx;
        e->dy = dy;
        e->nh = c->h;
        e->nv = c->v;
      }
    }
  }
  while (n < n1 && !x->lacking) {
    uint32_t stop = n1 - n < w - mx ? n1 : n + (w - mx);

    if (!mx && j->flow) jpg_flow_row(j, my);
    x->last_good = j->nscomp == 1u ? my / j->comp[j->scomp[0]].v : my;
    if (j->nscomp == 1u) {
      /* one component: its blocks along the row */
      jcomp_t *c   = &j->comp[j->scomp[0]];
      int16_t *blk = c->coefs + ((size_t)my * c->bw + mx) * 64u;

      while (n < stop) {
        if (kind == J_AC_FIRST && x->eobrun) {
          uint32_t k = stop - n < x->eobrun ? stop - n : x->eobrun;

          x->eobrun -= k;
          n += k;
          blk += 64u * (size_t)k;
          continue;
        }
        if (!x->r.tail && bs.p >= x->r.lim) {
          x->r.b = bs;
          rd_tail(x);
          bs = x->r.b;
        }
        switch (kind) {
        case J_DC_FIRST:  block_dc_first(x, &bs, j->scomp[0], blk); break;
        case J_DC_REFINE: block_dc_refine(j, &bs, blk); break;
        case J_AC_FIRST:  block_ac_first(x, &bs, c, blk); break;
        default:          block_ac_refine(x, &bs, c, blk); break;
        }
        n++;
        blk += 64u;
        if (x->r.tail) {
          x->r.b = bs;
          if (rd_past(&x->r)) {
            x->lacking = true;
            break;
          }
        }
      }
    } else {
      /* DC scans of several: an MCU's blocks in turn */
      for (; n < stop; n++, mx++) {
        if (!x->r.tail && bs.p >= x->r.lim) {
          x->r.b = bs;
          rd_tail(x);
          bs = x->r.b;
        }
        for (i = 0; i < nb; i++) {
          const jpblk_t *e   = &list[i];
          int16_t       *blk = e->c->coefs + ((size_t)(my * e->nv + e->dy) * e->c->bw + mx * e->nh + e->dx) * 64u;

          if (kind == J_DC_FIRST) block_dc_first(x, &bs, e->ci, blk);
          else block_dc_refine(j, &bs, blk);
        }
        if (x->r.tail) {
          x->r.b = bs;
          if (rd_past(&x->r)) {
            x->lacking = true;
            n++;
            break;
          }
        }
      }
    }
    mx = n % w;
    my = n / w;
  }
  x->r.b = bs;
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
#if J_NZ_X86
  x->ssse3 = __builtin_cpu_supports("ssse3");
#endif
  return (x->tail = malloc(margin * 2u + 32u)) != NULL;
}

/* A one-pass sequential scan (most images), in order, by restart
   intervals or by the speculative chunks: its MCU's blocks listed once,
   positions counted, the bits in registers, no dispatch a block; the
   check for data run out only near a segment's end, where it can be */
typedef struct jsblk {
  jcomp_t       *c;
  const jhuff_t *dc, *ac;
  int32_t       *pred;
  uint32_t       dx, dy, nh, nv; /* the block in its MCU; the MCU in blocks */
} jsblk_t;

/* the scan's MCU, its blocks in order, predicted from `x`'s DCs */
static uint32_t
seq_list(jctx_t *x, jsblk_t *list) {
  jpg_t   *j  = x->j;
  uint32_t nb = 0u, i;

  for (i = 0; i < j->nscomp; i++) {
    uint32_t ci = j->scomp[i], dx, dy;
    jcomp_t *c  = &j->comp[ci];
    uint32_t nh = j->nscomp == 1u ? 1u : c->h, nv = j->nscomp == 1u ? 1u : c->v;

    for (dy = 0; dy < nv; dy++) {
      for (dx = 0; dx < nh && nb < J_MCU_BLOCKS; dx++) {
        jsblk_t *s = &list[nb++];

        s->c    = c;
        s->dc   = j->dc[c->td];
        s->ac   = j->ac[c->ta];
        s->pred = &x->dc[ci];
        s->dx   = dx;
        s->dy   = dy;
        s->nh   = nh;
        s->nv   = nv;
      }
    }
  }
  return nb;
}

/* blocks b0 … b1 - 1 (MCUs × their blocks, `w` MCUs a row) from where the
   reader is: decoded (zeros once data ran out, as an MCU's end finds) and
   put in their planes. Each goes to its plane a block late: the IDCT's
   vector loads of a block just written by 16-bit stores wait for every
   one of them to reach the cache, a block later they have */
static void
seq_range(jctx_t *x, const jsblk_t *list, uint32_t nb, uint32_t w, uint32_t b0, uint32_t b1) {
  IM_ALIGN(16) int16_t blks[2][64];
  jpg_t   *j  = x->j;
  jbits_t  bs = x->r.b; /* here, so the compiler keeps it in registers */
  uint32_t m = b0 / nb, slot = b0 % nb, mx = m % w, my = m / w, b, cur = 0u;
  uint32_t vdiv = j->nscomp == 1u ? j->comp[j->scomp[0]].v : 1u, row = my / vdiv;
  size_t   es = j->diff ? 4u : j->prec <= 8u ? 1u : 2u;
  bool     diff = j->diff;
  jcp_t   *rec = j->rec; /* an index made: each row's places kept */
  jcomp_t *held = NULL;  /* the block held back: its component, where it goes */
  uint8_t *held_at = NULL;

  for (b = b0; b < b1; b++) {
    const jsblk_t *s = &list[slot];
    jcomp_t       *c = s->c;
    uint32_t       bx = mx * s->nh + s->dx, by = my * s->nv + s->dy;
    int16_t       *blk = blks[cur];

    memset(blk, 0, sizeof(blks[0]));
    if (!x->lacking) {
      if (!slot) {
        x->last_good = row;
        if (rec && !(mx & (J_IXCOL - 1u)) && my < j->nrec) {
          jcp_t   *e = &rec[(size_t)my * j->reccols + mx / J_IXCOL];
          uint32_t i;

          e->at = ((uint64_t)(bs.p - x->r.org) + x->r.pre) * 8u - bs.n;
          for (i = 0; i < j->nscomp; i++) e->dc[i] = x->dc[j->scomp[i]];
        }
      }
      if (!x->r.tail && bs.p >= x->r.lim) {
        x->r.b = bs;
        rd_tail(x);
        bs = x->r.b;
      }
      if (diff) *s->pred = 0; /* a differential frame's DC: none predicted */
      block_seq(&bs, s->dc, s->ac, s->pred, blk);
    }
    if (held) held->idct(blks[cur ^ 1u], held->q, held_at, held->stride);
    held = NULL;
    if (bx >= c->rbx0 && bx < c->rbx1 && by >= c->rby0 && by < c->rby1) {
      held    = c;
      held_at = (uint8_t *)c->plane + ((size_t)by * c->stride + bx) * c->ss * es;
    }
    cur ^= 1u;
    if (++slot == nb) {
      slot = 0u;
      if (!x->lacking && x->r.tail) {
        x->r.b = bs;
        if (rd_past(&x->r)) x->lacking = true;
      }
      if (++mx == w) {
        mx  = 0u;
        row = ++my / vdiv;
      }
    }
  }
  if (held) held->idct(blks[cur ^ 1u], held->q, held_at, held->stride);
  x->r.b = bs;
}

/* A region's MCU rows (IM_OPTION_REGION): a scan's rows past the last its
   blocks need left undecoded (h cut: the rows wanted), and a one-pass
   sequential scan's first MCU wanted (*first); otherwise h and 0. A
   progressive one's two more (block smoothing reads the DCs two rows
   below); the scans after start at their own data */
IM_HIDE uint32_t
jpg_region_rows(const jpg_t *j, uint32_t w, uint32_t h, uint32_t *first) {
  uint32_t i, r0 = UINT32_MAX, r1 = 0u;

  *first = 0u;
  if (!j->region || j->hier || (j->process != J_SEQUENTIAL && j->process != J_PROGRESSIVE)) return h;
  for (i = 0; i < j->nscomp; i++) {
    const jcomp_t *c = &j->comp[j->scomp[i]];
    uint32_t       v = j->nscomp == 1u ? 1u : c->v, a = c->rby0 / v, b = (c->rby1 + v - 1u) / v;

    r0 = a < r0 ? a : r0;
    r1 = b > r1 ? b : r1;
  }
  if (j->process == J_PROGRESSIVE) r1 += 2u;
  if (r1 < h) h = r1;
  if (!j->multiscan) *first = r0 < h ? r0 * w : 0u;
  return h;
}

/* the first interval of k0 (a region's first) where each one before it is
   its own segment ended by the RST it should be: those passed over
   without decoding (their DCs restart anyway); else 0 */
static uint32_t
region_skip(const jpg_t *j, uint32_t first, uint32_t per) {
  uint32_t k0 = j->restart && per ? first / per : 0u, k;

  if (!k0 || j->sd.nseg <= k0) return 0u;
  for (k = 0; k < k0; k++) {
    if (j->sd.seg[k].marker != 0xD0u + (k & 7u)) return 0u;
  }
  return k0;
}

/* the whole scan in order, its restart intervals one by one (a region's
   first ones passed over where they can be) */
static void
scan_seq(jctx_t *x, uint32_t w, uint32_t h, uint32_t first) {
  jsblk_t  list[J_MCU_BLOCKS];
  jpg_t   *j = x->j;
  uint32_t nb = seq_list(x, list), total = w * h, per = j->restart ? j->restart : total, n;
  uint32_t k0 = region_skip(j, first, per), next_rst = k0 & 7u, cur = k0;
  bool     at_marker = false;

  rd_start(x, k0);
  for (n = k0 * per; n < total; n += per) {
    uint32_t last = total - n > per ? n + per : total;

    if (n != k0 * per) restart(x, &next_rst, &cur, &at_marker);
    seq_range(x, list, nb, w, n * nb, last * nb);
  }
}

/* restart intervals on the pool: each its own segment, the RSTs in order
   (a valid stream's; any other is read in order, as libjpeg's resync goes) */
typedef struct jpar {
  jpg_t    *j;
  jctx_t   *ctx;       /* a worker's */
  uint32_t  w, total, per, step;
  uint32_t  k0;        /* the first interval decoded (a region's) */
  uint32_t  last_good; /* the last interval's */
} jpar_t;

static bool
par_item(void *arg, uint32_t worker, uint32_t item) {
  jpar_t  *pa = arg;
  jctx_t  *x  = &pa->ctx[worker];
  uint32_t k  = pa->k0 + item * pa->step, end = k + pa->step, n, i, nb = 0u;
  uint32_t ints = (pa->total + pa->per - 1u) / pa->per;
  jsblk_t  list[J_MCU_BLOCKS];

  /* one pass, sequential: the quick loop */
  if (!x->j->multiscan && x->j->process == J_SEQUENTIAL) nb = seq_list(x, list);

  for (; k < end && k < ints; k++) {
    uint32_t last = (k + 1u) * pa->per < pa->total ? (k + 1u) * pa->per : pa->total;

    for (i = 0; i < J_COMPS; i++) x->dc[i] = 0;
    x->eobrun  = 0u;
    x->lacking = false;
    rd_start(x, k);
    if (nb) seq_range(x, list, nb, pa->w, k * pa->per * nb, last * nb);
    else for (n = k * pa->per; n < last; n++) scan_mcu(x, n, pa->w);
    if (k + 1u == ints) pa->last_good = x->last_good;
  }
  return true;
}

/* -- lossless (H.1, H.2): differences, then the predictors ------------------- */

/* (jpgint.h's jloss_t) the scan's components' rows, each row predicted
   as a first one */
IM_HIDE bool
jpg_loss_start(jpg_t *j, jloss_t *L) {
  uint32_t i;

  memset(L, 0, sizeof(*L));
  for (i = 0; i < j->nscomp; i++) {
    uint32_t ci = j->scomp[i];
    jcomp_t *c  = &j->comp[ci];

    if (!(L->diff[ci] = calloc((size_t)c->bw * c->v, sizeof(int32_t))) || !(L->prev[ci] = calloc(c->bw, sizeof(uint16_t)))) {
      return false;
    }
  }
  for (i = 0; i < J_COMPS; i++) L->first[i] = true;
  return true;
}

IM_HIDE void
jpg_loss_end(jloss_t *L) {
  uint32_t i;

  for (i = 0; i < J_COMPS; i++) {
    free(L->diff[i]);
    free(L->prev[i]);
  }
}

IM_HIDE void
jpg_undiff(jpg_t *j, jloss_t *L, uint32_t ci, const int32_t *d) {
  jcomp_t  *c = &j->comp[ci];
  uint16_t *prev = L->prev[ci];
  uint32_t  w = c->cw, x, psv = j->ss, al = j->al;
  int32_t   ra, rb, rc;
  uint16_t  out[1];

  (void)out;
  if (L->y[ci] >= c->bh) return;
  if (j->diff) {
    /* a differential frame's (J.2.3.2): no prediction, the point transform */
    int32_t *o = (int32_t *)c->plane + (size_t)L->y[ci] * c->stride;

    for (x = 0; x < w; x++) o[x] = (int32_t)((uint32_t)d[x] << al);
    L->y[ci]++;
    return;
  }
  if (L->first[ci]) {
    ra = (d[0] + (1 << (j->prec - al - 1u))) & 0xFFFF;
    prev[0] = (uint16_t)ra;
    for (x = 1; x < w; x++) {
      ra      = (d[x] + ra) & 0xFFFF;
      prev[x] = (uint16_t)ra;
    }
    L->first[ci] = false;
  } else {
    rb = prev[0];
    ra = (d[0] + rb) & 0xFFFF;
    prev[0] = (uint16_t)ra;
    /* a loop a predictor (the choice not made each sample) */
#define UNDIFF(P)                                                              \
    for (x = 1; x < w; x++) {                                                  \
      rc      = rb;                                                            \
      rb      = prev[x];                                                       \
      ra      = (d[x] + (P)) & 0xFFFF;                                         \
      prev[x] = (uint16_t)ra;                                                  \
    }
    switch (psv) {
    case 1:  UNDIFF(ra) break;
    case 2:  UNDIFF(rb) break;
    case 3:  UNDIFF(rc) break;
    case 4:  UNDIFF(ra + rb - rc) break;
    case 5:  UNDIFF(ra + ((rb - rc) >> 1)) break;
    case 6:  UNDIFF(rb + ((ra - rc) >> 1)) break;
    default: UNDIFF((ra + rb) >> 1) break;
    }
#undef UNDIFF
  }
  /* to the plane, shifted (and cut to the sample's type, as libjpeg) */
  if (j->prec <= 8u) {
    uint8_t *o = (uint8_t *)c->plane + (size_t)L->y[ci] * c->stride;

    for (x = 0; x < w; x++) o[x] = (uint8_t)((uint32_t)prev[x] << al);
  } else {
    uint16_t *o = (uint16_t *)c->plane + (size_t)L->y[ci] * c->stride;

    for (x = 0; x < w; x++) o[x] = (uint16_t)((uint32_t)prev[x] << al);
  }
  L->y[ci]++;
}

/* a difference (F.1.2.1 with H.1.2.2's 16: 32768, no bits): code and
   value in one look-up where they fit */
IM_INLINE int32_t
loss_diff(jbits_t *b, const jhuff_t *h) {
  uint32_t e, s;

  if (b->n < 32u) refill(b);
  e = h->fast[PEEK(b, J_HBITS)];
  if (likely(e & J_FUSED)) {
    DROP(b, e & 63u);
    return (int32_t)e >> 16;
  }
  if (e) {
    DROP(b, e & 63u);
    s = (e >> 8) & 0xFFu;
  } else {
    s = huff_slow(b, h);
  }
  if (!s) return 0;
  if (s == 16u) return 32768;
  return extend(get_bits(b, s), s);
}

/* libjpeg's jdlhuff.c: where data ran out the rows after are zeros */
static bool
loss_seq(jpg_t *j) {
  jctx_t   x;
  jloss_t  L;
  uint32_t i, nrows, per_row, rows_to_go = 0u, cur = 0u, next_rst = 0u, irow, nirows, blocks = 0u;
  bool     at_marker = false, ok = false, single = j->nscomp == 1u, full = true;
  size_t   margin;

  for (i = 0; i < j->nscomp; i++) {
    const jcomp_t *c = &j->comp[j->scomp[i]];

    blocks += single ? 1u : (uint32_t)c->h * c->v;
    full = full && c->h == 1u && c->v == 1u; /* no component subsampled */
  }
  per_row = single ? j->comp[j->scomp[0]].cw : j->mcux;
  nrows   = single ? j->comp[j->scomp[0]].ch : j->mcuy;
  if (j->restart && j->restart % per_row) return false; /* libjpeg: JERR_BAD_RESTART */
  margin = (size_t)blocks * per_row * 5u + 64u; /* an MCU row: 17 + 16 bits a sample at most */
  if (!ctx_init(&x, j, margin)) return false;
  if (!jpg_loss_start(j, &L)) goto done;
  rows_to_go = j->restart / per_row;
  rd_start(&x, 0u);

  /* iMCU rows: an MCU row each, interleaved; v sample rows of a lone
     component (its last fewer) */
  nirows = single ? (nrows + j->comp[j->scomp[0]].v - 1u) / j->comp[j->scomp[0]].v : nrows;
  for (irow = 0; irow < nirows; irow++) {
    uint32_t mrows = 1u, yo;

    if (single) {
      uint32_t v = j->comp[j->scomp[0]].v;

      mrows = irow + 1u < nirows ? v : (nrows % v ? nrows % v : v);
    }
    for (yo = 0; yo < mrows; yo++) {
      if (j->restart) {
        if (!rows_to_go) {
          restart(&x, &next_rst, &cur, &at_marker);
          for (i = 0; i < J_COMPS; i++) L.first[i] = true;
          rows_to_go = j->restart / per_row;
        }
      }
      if (x.lacking) {
        for (i = 0; i < j->nscomp; i++) {
          uint32_t ci = j->scomp[i];
          jcomp_t *c  = &j->comp[ci];

          if (single) memset(L.diff[ci] + (size_t)yo * c->bw, 0, (size_t)c->bw * sizeof(int32_t));
          else memset(L.diff[ci], 0, (size_t)c->bw * c->v * sizeof(int32_t));
        }
        for (i = 0; i < J_COMPS; i++) L.first[i] = true;
      } else {
        uint32_t mx;

        jbits_t bs;

        if (!x.r.tail && x.r.b.p >= x.r.lim) rd_tail(&x);
        bs = x.r.b; /* here, so the compiler keeps it in registers */
        if (single || full) {
          /* a sample a component an MCU: each one's table and row once */
          const jhuff_t *ht[J_COMPS];
          int32_t       *dp[J_COMPS];
          uint32_t       n = j->nscomp;

          for (i = 0; i < n; i++) {
            const jcomp_t *c = &j->comp[j->scomp[i]];

            ht[i] = j->dc[c->td];
            dp[i] = L.diff[j->scomp[i]] + (single ? (size_t)yo * c->bw : 0u);
          }
          if (n == 1u) {
            for (mx = 0; mx < per_row; mx++) dp[0][mx] = loss_diff(&bs, ht[0]);
          } else if (n == 3u) {
            for (mx = 0; mx < per_row; mx++) {
              dp[0][mx] = loss_diff(&bs, ht[0]);
              dp[1][mx] = loss_diff(&bs, ht[1]);
              dp[2][mx] = loss_diff(&bs, ht[2]);
            }
          } else {
            for (mx = 0; mx < per_row; mx++) {
              for (i = 0; i < n; i++) dp[i][mx] = loss_diff(&bs, ht[i]);
            }
          }
        } else {
          for (mx = 0; mx < per_row; mx++) {
            for (i = 0; i < j->nscomp; i++) {
              uint32_t ci = j->scomp[i];
              jcomp_t *c  = &j->comp[ci];
              uint32_t a, b;

              for (b = 0; b < c->v; b++) {
                for (a = 0; a < c->h; a++) L.diff[ci][(size_t)b * c->bw + mx * c->h + a] = loss_diff(&bs, j->dc[c->td]);
              }
            }
          }
        }
        x.r.b = bs;
        if (rd_past(&x.r)) x.lacking = true;
      }
      if (j->restart) rows_to_go--;
    }
    /* the iMCU row's rows, predicted in order */
    for (i = 0; i < j->nscomp; i++) {
      uint32_t ci = j->scomp[i];
      jcomp_t *c  = &j->comp[ci];
      uint32_t rows = single ? mrows : c->v, r;

      if (!single && irow + 1u == nirows && c->ch % c->v) rows = c->ch % c->v;
      for (r = 0; r < rows; r++) jpg_undiff(j, &L, ci, L.diff[ci] + (size_t)r * c->bw);
    }
  }
  ok = true;

done:
  jpg_loss_end(&L);
  free(x.tail);
  return ok;
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
  /* (1): its blocks as its decoder made them, 64 coefficients each, from
     its first byte (all but the last chunk's); from where the true path
     met its decoder (m, (2)) to its cross (xs, (2): the true state there)
     they are the true ones, their DCs off by the same amount a component */
  int16_t *coef;          /* block_keep's records, one after another */
  size_t   ncoef, coefcap; /* int16s used, room */
  uint32_t nkept;          /* records */
  jsync_t  m, xs;
  bool     kept;
} jchunk_t;

typedef struct jspec {
  jpg_t    *j;
  jctx_t   *ctx;
  jchunk_t *ch;
  uint32_t  n, w, total;   /* chunks, MCUs across, blocks */
  uint32_t  nblk;          /* blocks an MCU */
  size_t    room;          /* int16s a chunk's records may take (an
                              IM_OPTION_MEMORY_LIMIT's share; else all) */
  uint8_t   slot_ci[64], slot_h[64], slot_v[64];
} jspec_t;

/* block_pairs' walk, its coefficients as records at `out` (a natural
   position, a value): every symbol writes one where the next goes, a
   value (never 0) keeps it, so ZRL, the end of block and a single's
   second leave none but one past the last (room for it: 2 int16s past
   63 records). The records kept */
IM_INLINE uint32_t
keep_pairs(jbits_t *b, const jhuff_t *act, int16_t *out) {
  const uint64_t *pt = act->pair;
  uint32_t        k  = 1u, n = 0u;

  while (k < 64u) {
    uint64_t e;

    if (b->n < 32) refill(b);
    e = pt[PEEK(b, J_HBITS)];
    if (likely(e & JP_OK)) {
      int16_t v1 = (int16_t)(e >> 32), v2 = (int16_t)(e >> 48);

      k += (uint32_t)(e >> 16) & 127u;
      out[2u * n]      = (int16_t)jpg_natural[k];
      out[2u * n + 1u] = v1;
      n += v1 != 0;
      k++;
      if (unlikely(k >= 64u)) {
        DROP(b, (e >> 6) & 63u);
        break;
      }
      DROP(b, e & 63u);
      k += (uint32_t)(e >> 24) & 127u;
      out[2u * n]      = (int16_t)jpg_natural[k];
      out[2u * n + 1u] = v2;
      n += v2 != 0;
      k += (uint32_t)(e >> 31) & 1u;
    } else {
      int32_t  v;
      uint32_t rs = pair_long(b, act, &v);

      if (rs & 15u) {
        k += rs >> 4;
        out[2u * n]      = (int16_t)jpg_natural[k];
        out[2u * n + 1u] = (int16_t)v;
        n++;
        k++;
      } else if (rs == 0xF0u) {
        k += 16u;
      } else {
        break;
      }
    }
  }
  return n;
}

/* block_pairs' walk, only the bits taken */
IM_INLINE void
drop_pairs(jbits_t *b, const jhuff_t *act) {
  const uint64_t *pt = act->pair;
  uint32_t        k  = 1u;

  while (k < 64u) {
    uint64_t e;

    if (b->n < 32) refill(b);
    e = pt[PEEK(b, J_HBITS)];
    if (likely(e & JP_OK)) {
      k += ((uint32_t)(e >> 16) & 127u) + 1u;
      if (unlikely(k >= 64u)) {
        DROP(b, (e >> 6) & 63u);
        return;
      }
      DROP(b, e & 63u);
      k += ((uint32_t)(e >> 24) & 127u) + ((uint32_t)(e >> 31) & 1u);
    } else {
      int32_t  v;
      uint32_t rs = pair_long(b, act, &v);

      if (rs & 15u) k += (rs >> 4) + 1u;
      else if (rs == 0xF0u) k += 16u;
      else return;
    }
  }
}

/* a sequential block decoded into a compact record at `out`: its count n
   of coefficients past the DC, its DC (as predicted), then n pairs of a
   natural position and a value, in decode order (a damaged run's past the
   end at 63, as jpg_natural has it; a later one over an earlier); the
   int16s it took (with pairs, 2 more may be written past them) */
IM_INLINE uint32_t
block_keep(jbits_t *b, const jhuff_t *dct, const jhuff_t *act, int32_t *dc, int16_t *out) {
  uint32_t e, k, n = 0;
  int32_t  diff;

  if (b->n < 32) refill(b);
  e = dct->fast[PEEK(b, J_HBITS)];
  if (likely(e & J_FUSED)) {
    DROP(b, e & 63u);
    diff = (int32_t)e >> 16;
  } else {
    uint32_t s;

    if (e) {
      DROP(b, e & 63u);
      s = (e >> 8) & 15u;
    } else {
      s = huff_slow(b, dct) & 15u;
    }

    diff = extend(get_bits(b, s), s);
  }
  *dc    = (int32_t)((uint32_t)*dc + (uint32_t)diff);
  out[1] = (int16_t)*dc;
  if (act->pair) {
    out[0] = (int16_t)(n = keep_pairs(b, act, out + 2));
    return 2u + 2u * n;
  }

  for (k = 1; k < 64u; k++) {
    uint32_t rs, s;

    if (b->n < 32) refill(b);
    e = act->fast[PEEK(b, J_HBITS)];
    if (likely(e & J_FUSED)) {
      DROP(b, e & 63u);
      rs = (e >> 8) & 0xFFu;
      if (rs & 15u) {
        k += rs >> 4;
        out[2u + 2u * n]  = (int16_t)jpg_natural[k];
        out[3u + 2u * n++] = (int16_t)((int32_t)e >> 16);
        continue;
      }
    } else {
      if (e) {
        DROP(b, e & 63u);
        rs = (e >> 8) & 0xFFu;
      } else {
        rs = huff_slow(b, act);
      }
      if ((s = rs & 15u)) {
        k += rs >> 4;
        out[2u + 2u * n]  = (int16_t)jpg_natural[k];
        out[3u + 2u * n++] = (int16_t)extend(get_bits(b, s), s);
        continue;
      }
    }
    if (rs != 0xF0u) break;
    k += 15u;
  }
  out[0] = (int16_t)n;
  return 2u + 2u * n;
}

/* a sequential block decoded and dropped: its DC difference */
IM_INLINE int32_t
block_drop(jbits_t *b, const jhuff_t *dct, const jhuff_t *act) {
  uint32_t e, k;
  int32_t  diff;

  if (b->n < 32) refill(b);
  e = dct->fast[PEEK(b, J_HBITS)];
  if (likely(e & J_FUSED)) {
    DROP(b, e & 63u);
    diff = (int32_t)e >> 16;
  } else {
    uint32_t s;

    if (e) {
      DROP(b, e & 63u);
      s = (e >> 8) & 15u;
    } else {
      s = huff_slow(b, dct) & 15u;
    }

    diff = extend(get_bits(b, s), s);
  }
  if (act->pair) {
    drop_pairs(b, act);
    return diff;
  }
  for (k = 1; k < 64u; k++) {
    uint32_t rs;

    if (b->n < 32) refill(b);
    e = act->fast[PEEK(b, J_HBITS)];
    if (likely(e & J_FUSED)) {
      DROP(b, e & 63u);
      rs = (e >> 8) & 0xFFu;
    } else {
      if (e) {
        DROP(b, e & 63u);
        rs = (e >> 8) & 0xFFu;
      } else {
        rs = huff_slow(b, act);
      }
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

/* (1): chunk `item` from its first byte (the first chunk's from the true
   start): boundaries in its window kept, the first past its end */
static bool
spec_pass1(void *arg, uint32_t worker, uint32_t item) {
  jsync_t   st;
  jbits_t   bs;
  jspec_t  *sp = arg;
  jpg_t    *j  = sp->j;
  jctx_t   *x  = &sp->ctx[worker];
  jchunk_t *ch = &sp->ch[item];
  int16_t  *coef_next;
  size_t    coef_cap;
  uint64_t  end = (uint64_t)ch->end * 8u, at;
  bool      last = item + 1u == sp->n;

  memset(&st, 0, sizeof(st));
  rd_seek(x, (uint64_t)ch->start * 8u);
  bs = x->r.b; /* in registers */
  at = ((uint64_t)(bs.p - x->r.org) + x->r.pre) * 8u;
  for (;;) {
    uint32_t ci = sp->slot_ci[st.slot];

    st.bit = at + (uint64_t)(bs.p - x->r.b.p) * 8u - bs.n;
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
    if (!last && ch->coef) {
      /* room for the longest record (a DC and 63 pairs, and keep_pairs'
         one past them), else dropped as they come, decoded again in (3) */
      if (ch->ncoef + 130u > ch->coefcap) {
        coef_cap = ch->coefcap * 2u;
        if (coef_cap <= sp->room) {
          coef_next = im_big_grow(ch->coef,
                                  ch->coefcap * sizeof(*coef_next),
                                  coef_cap * sizeof(*coef_next),
                                  j->big);
        } else {
          im_big_free(ch->coef, ch->coefcap * sizeof(*coef_next), j->big);
          coef_next = NULL;
        }

        if (!coef_next) {
          ch->coef = NULL;
          st.dc[ci] = (int32_t)((uint32_t)st.dc[ci] + (uint32_t)block_drop(&bs, j->dc[j->comp[ci].td], j->ac[j->comp[ci].ta]));
          st.blk++;
          st.slot = st.slot + 1u == sp->nblk ? 0u : st.slot + 1u;
          continue;
        }
        ch->coef    = coef_next;
        ch->coefcap = coef_cap;
      }
      ch->ncoef += block_keep(&bs, j->dc[j->comp[ci].td], j->ac[j->comp[ci].ta], &st.dc[ci], ch->coef + ch->ncoef);
      ch->nkept++;
    } else {
      st.dc[ci] = (int32_t)((uint32_t)st.dc[ci] + (uint32_t)block_drop(&bs, j->dc[j->comp[ci].td], j->ac[j->comp[ci].ta]));
    }
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
  uint32_t  end = item + 1u < sp->n ? sp->ch[item + 1u].s.blk : sp->total, i;
  jsblk_t   list[J_MCU_BLOCKS];

  uint32_t  nb = seq_list(x, list);
  jsync_t   from = ch->s;

  /* kept: as many as the true path takes from where it met them (else, the
     chunk decoded again) */
  if (ch->kept && (ch->xs.blk < from.blk || ch->nkept < ch->m.blk || ch->nkept - ch->m.blk < ch->xs.blk - from.blk)) ch->kept = false;
  if (ch->kept) {
    /* the kept blocks to the cross: their DCs moved to the true ones, to
       the planes */
    IM_ALIGN(16) int16_t blks[2][64];
    jpg_t         *j  = sp->j;
    uint32_t       b, k, upto = ch->xs.blk < end ? ch->xs.blk : end, cur = 0u;
    uint32_t       slot = from.blk % nb, m = from.blk / nb, mx = m % sp->w, my = m / sp->w;
    size_t         es = j->prec <= 8u ? 1u : 2u;
    const int16_t *rec = ch->coef;
    uint16_t       off[J_COMPS];
    jcomp_t       *held = NULL; /* a block late, as seq_range's */
    uint8_t       *held_at = NULL;

    for (i = 0; i < J_COMPS; i++) off[i] = (uint16_t)((uint32_t)ch->s.dc[i] - (uint32_t)ch->m.dc[i]);
    for (k = 0; k < ch->m.blk; k++) rec += 2u + 2u * (uint16_t)rec[0]; /* to where the paths met */
    for (b = from.blk; b < upto; b++) {
      const jsblk_t *e  = &list[slot];
      jcomp_t       *c  = e->c;
      uint32_t       bx = mx * e->nh + e->dx, by = my * e->nv + e->dy, n = (uint16_t)rec[0], q;

      if (bx >= c->rbx0 && bx < c->rbx1 && by >= c->rby0 && by < c->rby1) {
        int16_t *blk = blks[cur];

        memset(blk, 0, sizeof(blks[0]));
        blk[0] = (int16_t)(uint16_t)((uint16_t)rec[1] + off[sp->slot_ci[slot]]);
        for (q = 0; q < n; q++) blk[(uint16_t)rec[2u + 2u * q]] = rec[3u + 2u * q];
        if (held) held->idct(blks[cur ^ 1u], held->q, held_at, held->stride);
        held    = c;
        held_at = (uint8_t *)c->plane + ((size_t)by * c->stride + bx) * c->ss * es;
        cur ^= 1u;
      }
      rec += 2u + 2u * n;
      if (++slot == nb) {
        slot = 0u;
        if (++mx == sp->w) {
          mx = 0u;
          my++;
        }
      }
    }
    if (held) held->idct(blks[cur ^ 1u], held->q, held_at, held->stride);
    from = ch->xs;
    if (from.blk >= end) return true;
  }
  for (i = 0; i < J_COMPS; i++) x->dc[i] = from.dc[i];
  x->lacking = false;
  rd_seek(x, from.bit);
  seq_range(x, list, nb, sp->w, from.blk, end);
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
  /* the first chunk's decoder on the true path from the start */
  memset(&sp->ch[0].m, 0, sizeof(sp->ch[0].m));
  sp->ch[0].xs   = t;
  sp->ch[0].kept = sp->ch[0].coef != NULL;
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
      ch->s    = t;
      ch->kept = false;
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
      ch->m = *a;
      t.blk += e->blk - a->blk;
      for (k = 0; k < J_COMPS; k++) t.dc[k] = (int32_t)((uint32_t)t.dc[k] + (uint32_t)e->dc[k] - (uint32_t)a->dc[k]);
      t.bit    = e->bit;
      t.slot   = e->slot;
      ch->xs   = t;
      ch->kept = ch->coef != NULL;
    }
  }
  /* starts in order, none past the end */
  for (i = 1; i < sp->n; i++) {
    if (sp->ch[i].s.blk < sp->ch[i - 1u].s.blk || sp->ch[i].s.blk > sp->total) return false;
  }
  return true;
}

/* whether a chunk's blocks are kept from (1) (a test build can say 0) */
#ifndef J_KEEP
#  define J_KEEP 1
#endif

/* chunks a thread at most (each at least 64K) */
#ifndef J_CHUNKS
#  define J_CHUNKS 1u
#endif

/* the one-pass sequential scan's single segment on `pool`; false with
   nothing written: decode it in order */
static bool
scan_spec(jpg_t *j, im_pool_t *pool, uint32_t w, uint32_t h, size_t margin, bool *done) {
  jspec_t  sp;
  jctx_t   ctx[IM_POOL_MAX];
  jchunk_t ch[J_CHUNKS * IM_POOL_MAX];
  size_t   len = j->sd.seg[0].end - j->sd.seg[0].start, part;
  uint32_t nw = im_pool_size(pool), i, s = 0, nctx = 0;
  bool     ok = false, keep;

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
  /* more chunks than threads, taken in turn: a slower core takes fewer */
  sp.n     = J_CHUNKS * nw;
  if (len / sp.n < (64u << 10)) sp.n = (uint32_t)(len / (64u << 10));
  if (sp.n < 3u) return false;
  part = len / sp.n;
  /* blocks kept where decoding them again would cost most: their data 4
     bytes a block or more (less: the pages they'd take cost more than the
     decoding they'd save, most on many threads) */
  keep = J_KEEP && !j->rec && len >= (size_t)sp.total * 4u;
  /* under a memory limit the records take what the frame leaves of it,
     shared (too little: none kept, all decoded again) */
  sp.room = SIZE_MAX;
  if (j->mem_limit) {
    uint64_t used = jpg_frame_bytes(j), left = used < j->mem_limit ? j->mem_limit - used : 0u;

    sp.room = (size_t)(left / (sp.n - 1u) / sizeof(int16_t));
    keep    = keep && sp.room >= (64u << 10);
  }
  for (i = 0; i < sp.n; i++) {
    ch[i].start  = part * i;
    ch[i].end    = i + 1u < sp.n ? part * (i + 1u) : len;
    ch[i].window = ((uint64_t)ch[i].start + (part / 2u < (16u << 10) ? part / 2u : (16u << 10))) * 8u;
    /* room to start with: three times its share of the blocks, and of
       its bytes (each a coefficient at most: 4 bytes a pair), whichever's
       less (pages never touched cost nothing) */
    if (keep && i + 1u < sp.n) {
      uint64_t nblk = (uint64_t)sp.total * 3u / sp.n, a = nblk * 2u + ((uint64_t)part * 3u) * 2u, b = nblk * 128u;

      ch[i].coefcap = (size_t)(a < b ? a : b) + 1024u;
      if (ch[i].coefcap > sp.room) ch[i].coefcap = sp.room;
      ch[i].coef    = im_big_alloc(ch[i].coefcap * sizeof(int16_t), false, j->big);
    }
  }
  for (i = 0; i < nw; i++) {
    if (!ctx_init(&ctx[i], j, margin))
      goto cleanup;
    nctx++;
  }
  sp.ctx = ctx;
  sp.ch  = ch;
  if (im_pool_run(pool, spec_pass1, &sp, sp.n) && spec_pass2(&sp, &ctx[0])) {
    ok    = im_pool_run(pool, spec_pass3, &sp, sp.n);
    *done = true;
  }
cleanup:
  for (i = 0; i < nctx; i++) {
    free(ctx[i].tail);
  }
  for (i = 0; i < sp.n; i++) {
    free(ch[i].rec);
    im_big_free(ch[i].coef, ch[i].coefcap * sizeof(int16_t), j->big);
  }
  return ok;
}

/* -- lossless on threads ------------------------------------------------------- */

/* A lossless row is predicted from the row above, its differences from
   nothing but the bits: those are what threads decode, the predictors run
   in order (an eighth of a decode). Restart intervals each start over (a
   first row's prediction): one whole on a thread. One segment: as
   scan_spec, speculatively (Huffman codes fall into step), the
   differences kept (16 bits each: the predictors add modulo 2^16), then
   each component's rows predicted on a thread of their own */

/* where threads can take the scan: one component, or several none
   subsampled (a sample each an MCU); its rows and MCUs a row */
static bool
loss_shape(const jpg_t *j, uint32_t *per_row, uint32_t *rows, uint32_t *nslot) {
  uint32_t i;

  if (j->diff || j->hier) return false;
  if (j->nscomp == 1u) {
    *per_row = j->comp[j->scomp[0]].cw;
    *rows    = j->comp[j->scomp[0]].ch;
    *nslot   = 1u;
    return true;
  }
  for (i = 0; i < j->nscomp; i++) {
    if (j->comp[j->scomp[i]].h != 1u || j->comp[j->scomp[i]].v != 1u) return false;
  }
  *per_row = j->mcux;
  *rows    = j->mcuy;
  *nslot   = j->nscomp;
  return true;
}

/* a row's differences, MCU by MCU, into the rows of `dp` */
IM_INLINE void
loss_row(jbits_t *bs, const jhuff_t *const *ht, int32_t *const *dp, uint32_t n, uint32_t per_row) {
  uint32_t mx, i;

  if (n == 1u) {
    for (mx = 0; mx < per_row; mx++) dp[0][mx] = loss_diff(bs, ht[0]);
  } else if (n == 3u) {
    for (mx = 0; mx < per_row; mx++) {
      dp[0][mx] = loss_diff(bs, ht[0]);
      dp[1][mx] = loss_diff(bs, ht[1]);
      dp[2][mx] = loss_diff(bs, ht[2]);
    }
  } else {
    for (mx = 0; mx < per_row; mx++) {
      for (i = 0; i < n; i++) dp[i][mx] = loss_diff(bs, ht[i]);
    }
  }
}

typedef struct jlpar {
  jpg_t   *j;
  jctx_t  *ctx;
  uint32_t per_row, rows, nslot, rpi, ints, step;
} jlpar_t;

/* restart intervals [item × step, …): each as loss_seq has it after its
   restart (its first row predicted as a first one, data run out: zeros) */
static bool
loss_par_item(void *arg, uint32_t worker, uint32_t item) {
  jlpar_t       *pa = arg;
  jpg_t         *j  = pa->j;
  jctx_t        *x  = &pa->ctx[worker];
  jloss_t        L;
  const jhuff_t *ht[J_COMPS];
  int32_t       *dp[J_COMPS];
  uint32_t       k = item * pa->step, end = k + pa->step, i, r;
  bool           ok = false;

  if (!jpg_loss_start(j, &L)) goto done;
  for (i = 0; i < pa->nslot; i++) {
    ht[i] = j->dc[j->comp[j->scomp[i]].td];
    dp[i] = L.diff[j->scomp[i]];
  }
  for (; k < end && k < pa->ints; k++) {
    uint32_t r1 = (k + 1u) * pa->rpi < pa->rows ? (k + 1u) * pa->rpi : pa->rows;

    rd_start(x, k);
    x->lacking = false;
    for (i = 0; i < J_COMPS; i++) {
      L.first[i] = true;
      L.y[i]     = k * pa->rpi;
    }
    for (r = k * pa->rpi; r < r1; r++) {
      if (x->lacking) {
        for (i = 0; i < pa->nslot; i++) memset(dp[i], 0, (size_t)pa->per_row * sizeof(int32_t));
        for (i = 0; i < J_COMPS; i++) L.first[i] = true;
      } else {
        jbits_t bs;

        if (!x->r.tail && x->r.b.p >= x->r.lim) rd_tail(x);
        bs = x->r.b;
        loss_row(&bs, ht, dp, pa->nslot, pa->per_row);
        x->r.b = bs;
        if (rd_past(&x->r)) x->lacking = true;
      }
      for (i = 0; i < pa->nslot; i++) jpg_undiff(j, &L, j->scomp[i], dp[i]);
    }
  }
  ok = true;

done:
  jpg_loss_end(&L);
  return ok;
}

/* a sample boundary: where, samples since the start, its place in the MCU */
typedef struct jlrec {
  uint64_t bit;
  uint32_t n, slot;
} jlrec_t;

typedef struct jlchunk {
  size_t   start, end;   /* bytes */
  uint64_t window;       /* boundaries kept below this bit */
  jlrec_t *rec;
  uint32_t nrec, cap;
  jlrec_t  cross;        /* the first boundary at or past `end` */
  bool     crossed;
  int16_t *d;            /* (1): its differences from its first byte */
  size_t   nd, dcap;
  size_t   within;       /* the last chunk's: those read inside the data */
  int16_t *head;         /* (2): the true ones from its true start on to */
  size_t   nhead, hcap;
  uint32_t from;         /* … its own `from`th, where the paths met */
  bool     met;
} jlchunk_t;

typedef struct jlspec {
  jpg_t         *j;
  jctx_t        *ctx;
  jlchunk_t     *ch;
  const jhuff_t *ht[J_COMPS];
  uint32_t       n, nslot, per_row, rows, total, run;
  uint64_t       bits;   /* the segment's */
  size_t         room;   /* int16s a chunk's may take */
  bool           alike;  /* every slot's table the same: a sample decodes
                            the same in any, in step wherever the bits are */
} jlspec_t;

/* room for k more */
static bool
loss_room(const jlspec_t *sp, int16_t **d, size_t nd, size_t *cap, size_t k) {
  if (nd + k > *cap) {
    size_t   c    = *cap ? *cap * 2u : 4096u;
    int16_t *next;

    while (c < nd + k) c *= 2u;
    next = c <= sp->room ? im_big_grow(*d, *cap * sizeof(int16_t), c * sizeof(int16_t), sp->j->big) : NULL;
    if (!next) return false;
    *d   = next;
    *cap = c;
  }
  return true;
}

static bool
loss_push(const jlspec_t *sp, int16_t **d, size_t *nd, size_t *cap, int32_t v) {
  if (*nd == *cap) {
    size_t   c    = *cap ? *cap * 2u : 4096u;
    int16_t *next = c <= sp->room ? im_big_grow(*d, *cap * sizeof(int16_t), c * sizeof(int16_t), sp->j->big) : NULL;

    if (!next) return false;
    *d   = next;
    *cap = c;
  }
  (*d)[(*nd)++] = (int16_t)(uint16_t)(uint32_t)v;
  return true;
}

IM_INLINE uint64_t
bs_bit(const jrd_t *r, const jbits_t *bs) {
  return ((uint64_t)(bs->p - r->org) + r->pre) * 8u - bs->n;
}

/* (1): chunk `item` from its first byte (the first chunk's the true
   start): boundaries in its window kept, its differences, the first
   boundary past its end (the last chunk: to the data's end) */
static bool
loss_pass1(void *arg, uint32_t worker, uint32_t item) {
  jlspec_t  *sp = arg;
  jctx_t    *x  = &sp->ctx[worker];
  jlchunk_t *ch = &sp->ch[item];
  jlrec_t    st;
  jbits_t    bs;
  uint64_t   end = (uint64_t)ch->end * 8u;
  bool       last = item + 1u == sp->n;

  memset(&st, 0, sizeof(st));
  rd_seek(x, (uint64_t)ch->start * 8u);
  bs = x->r.b;
  for (;;) {
    if (!x->r.tail && bs.p >= x->r.lim) {
      x->r.b = bs;
      rd_tail(x);
      bs = x->r.b;
    }
    st.bit = bs_bit(&x->r, &bs);
    if (item && st.bit < ch->window) {
      if (ch->nrec == ch->cap) {
        uint32_t cap  = ch->cap ? ch->cap * 2u : 1024u;
        jlrec_t *next = realloc(ch->rec, cap * sizeof(*next));

        if (!next) return false;
        ch->rec = next;
        ch->cap = cap;
      }
      ch->rec[ch->nrec++] = st;
    }
    if (last ? st.bit > sp->bits : st.bit >= end) {
      ch->cross   = st;
      ch->crossed = !last;
      return true;
    }
    if (last) ch->within = st.n;
    if (st.n >= sp->total) return true;
    {
      /* past the window and far from the end (32 bits a sample at most):
         a run with no boundary to look at */
      uint64_t to = last ? sp->bits : end;
      uint32_t k  = 1u, q;
      int16_t *o;

      if (!(item && st.bit < ch->window) && to - st.bit > 32u * (uint64_t)sp->run) k = sp->run;
      if (k > sp->total - st.n) k = sp->total - st.n;
      if (!loss_room(sp, &ch->d, ch->nd, &ch->dcap, k)) return false;
      o = ch->d + ch->nd;
      for (q = 0; q < k; q++) {
        o[q]    = (int16_t)(uint16_t)(uint32_t)loss_diff(&bs, sp->ht[st.slot]);
        st.slot = st.slot + 1u == sp->nslot ? 0u : st.slot + 1u;
      }
      ch->nd += k;
      st.n   += k;
    }
  }
}

/* (2)'s walk: the true path on from `t`, its differences to the chunk's
   head, to a boundary in `rec` (its index; UINT32_MAX none), `stop` bits
   or `nmax` samples; false past the data, or out of memory */
static bool
loss_walk(jlspec_t *sp, jctx_t *x, jlrec_t *t, jlchunk_t *ch, const jlrec_t *rec, uint32_t nrec, uint64_t stop, uint32_t nmax,
          uint32_t *hit) {
  jbits_t  bs;
  uint32_t r = 0;

  *hit = UINT32_MAX;
  rd_seek(x, t->bit);
  bs = x->r.b;
  for (;;) {
    if (!x->r.tail && bs.p >= x->r.lim) {
      x->r.b = bs;
      rd_tail(x);
      bs = x->r.b;
    }
    t->bit = bs_bit(&x->r, &bs);
    if (t->bit > sp->bits) return false;
    while (r < nrec && rec[r].bit < t->bit) r++;
    if (r < nrec && rec[r].bit == t->bit && (sp->alike || rec[r].slot == t->slot)) {
      *hit = r;
      return true;
    }
    if (t->bit >= stop || t->n >= nmax || (nrec && r >= nrec && stop == UINT64_MAX)) return true;
    if (!loss_push(sp, &ch->head, &ch->nhead, &ch->hcap, loss_diff(&bs, sp->ht[t->slot]))) return false;
    t->n++;
    t->slot = t->slot + 1u == sp->nslot ? 0u : t->slot + 1u;
  }
}

/* (2): each chunk's true start in order, the differences between it and
   where the paths met; false: in order on one thread */
static bool
loss_pass2(jlspec_t *sp, jctx_t *x) {
  jlrec_t  t;
  uint32_t i, hit;

  if (!sp->ch[0].crossed || sp->ch[0].nd != sp->ch[0].cross.n) return false;
  sp->ch[0].met  = true;
  sp->ch[0].from = 0u;
  t              = sp->ch[0].cross;
  for (i = 1; i < sp->n; i++) {
    jlchunk_t *ch   = &sp->ch[i];
    bool       last = i + 1u == sp->n;

    /* on to a boundary its decoder passed */
    if (!loss_walk(sp, x, &t, ch, ch->rec, ch->nrec, UINT64_MAX, sp->total, &hit)) return false;
    if (hit != UINT32_MAX) {
      const jlrec_t *a = &ch->rec[hit];
      uint32_t       need;

      ch->met  = true;
      ch->from = a->n;
      if (last) {
        need = sp->total - t.n;
        if ((size_t)a->n + need > ch->within || (size_t)a->n + need > ch->nd) return false;
        t.n = sp->total;
      } else {
        if (!ch->crossed || ch->nd != ch->cross.n) return false;
        /* its slots its own count's: the true one's from where they met */
        t.slot = (uint32_t)(((uint64_t)t.slot + (ch->cross.n - a->n)) % sp->nslot);
        t.n   += ch->cross.n - a->n;
        t.bit  = ch->cross.bit;
      }
      continue;
    }
    /* never in step: through it in order */
    ch->met = false;
    if (!loss_walk(sp, x, &t, ch, NULL, 0u, last ? UINT64_MAX : (uint64_t)ch->end * 8u, sp->total, &hit)) return false;
  }
  return t.n == sp->total;
}

/* the differences in order (pieces: chunk by chunk, its head, then its
   own from where the paths met) */
typedef struct jlund {
  jlspec_t      *sp;
  jloss_t       *L;
  const int16_t *pc[2u * IM_POOL_MAX];
  size_t         pn[2u * IM_POOL_MAX];
  uint32_t       npc;
} jlund_t;

static bool
loss_pieces(jlund_t *u) {
  const jlspec_t *sp = u->sp;
  uint64_t        sum = 0u;
  uint32_t        i;

  u->npc = 0u;
  for (i = 0; i < sp->n; i++) {
    const jlchunk_t *ch = &sp->ch[i];

    if (ch->nhead) {
      u->pc[u->npc]   = ch->head;
      u->pn[u->npc++] = ch->nhead;
      sum            += ch->nhead;
    }
    if (ch->met) {
      size_t n = i + 1u < sp->n ? (size_t)ch->cross.n - ch->from : (size_t)(sp->total - sum);

      if (sum + n > sp->total || ch->from + n > ch->nd) return false;
      u->pc[u->npc]   = ch->d + ch->from;
      u->pn[u->npc++] = n;
      sum            += n;
    }
  }
  return sum == sp->total;
}

/* (3): component `item`'s rows, predicted in order: its samples every
   nslot-th of the pieces' */
static bool
loss_pass3(void *arg, uint32_t worker, uint32_t item) {
  jlund_t        *u  = arg;
  const jlspec_t *sp = u->sp;
  jpg_t          *j  = sp->j;
  uint32_t        ci = j->scomp[item], mx = 0u, ns = sp->nslot, k;
  int32_t        *d  = u->L->diff[ci];
  uint64_t        g  = 0u; /* the stream's samples before the piece */

  (void)worker;
  for (k = 0; k < u->npc; k++) {
    const int16_t *p = u->pc[k];
    size_t         n = u->pn[k], q = (size_t)((item + ns - g % ns) % ns);

    for (; q < n; q += ns) {
      d[mx++] = p[q];
      if (mx == sp->per_row) {
        jpg_undiff(j, u->L, ci, d);
        mx = 0u;
      }
    }
    g += n;
  }
  return true;
}

/* the lossless scan's single Huffman segment on `pool`, speculatively;
   false with nothing written: in order */
static bool
loss_spec(jpg_t *j, im_pool_t *pool, uint32_t per_row, uint32_t rows, uint32_t nslot, size_t margin, bool *done) {
  jlspec_t  sp;
  jctx_t    ctx[IM_POOL_MAX];
  jlchunk_t ch[IM_POOL_MAX];
  jloss_t   L;
  jlund_t   u;
  size_t    len = j->sd.seg[0].end - j->sd.seg[0].start, part;
  uint32_t  nw = im_pool_size(pool), i;
  bool      ok = false;

  *done = false;
  memset(&sp, 0, sizeof(sp));
  memset(ch, 0, sizeof(ch));
  memset(&L, 0, sizeof(L));
  sp.j       = j;
  sp.nslot   = nslot;
  sp.per_row = per_row;
  sp.rows    = rows;
  sp.total   = per_row * rows * nslot;
  sp.bits    = (uint64_t)len * 8u;
  sp.n       = nw;
  sp.alike = true;
  for (i = 0; i < nslot; i++) {
    sp.ht[i]  = j->dc[j->comp[j->scomp[i]].td];
    sp.alike = sp.alike && sp.ht[i] == sp.ht[0];
  }
  /* a run's samples: what a quarter of the margin holds at 4 bytes each */
  sp.run = (uint32_t)(margin / 16u < 256u ? margin / 16u : 256u);
  sp.run = sp.run ? sp.run : 1u;
  if (len / sp.n < (64u << 10) || len / sp.n < margin * 4u) sp.n = (uint32_t)(len / ((64u << 10) > margin * 4u ? (64u << 10) : margin * 4u));
  if (sp.n < 2u) return false;
  part = len / sp.n;
  /* the differences, 2 bytes a sample: under a memory limit what the
     frame leaves of it (too little: one thread) */
  sp.room = SIZE_MAX;
  if (j->mem_limit) {
    uint64_t used = jpg_frame_bytes(j), left = used < j->mem_limit ? j->mem_limit - used : 0u;

    if (left < (uint64_t)sp.total * 3u) return false;
    sp.room = (size_t)(left / 2u / sp.n);
  }
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
  if (im_pool_run(pool, loss_pass1, &sp, sp.n) && loss_pass2(&sp, &ctx[0])) {
    *done = true;
    u.sp = &sp;
    u.L  = &L;
    if (loss_pieces(&u) && jpg_loss_start(j, &L)) ok = im_pool_run(pool, loss_pass3, &u, nslot);
    else *done = false;
    jpg_loss_end(&L);
  }
  for (i = 0; i < nw; i++) free(ctx[i].tail);
  for (i = 0; i < sp.n; i++) {
    free(ch[i].rec);
    im_big_free(ch[i].d, ch[i].dcap * sizeof(int16_t), j->big);
    im_big_free(ch[i].head, ch[i].hcap * sizeof(int16_t), j->big);
  }
  return ok;
}

/* a lossless Huffman scan: on threads where it can be, else in order */
IM_HIDE bool
jpg_scan_lossless(jpg_t *j, im_pool_t *pool) {
  uint32_t per_row, rows, nslot, i;
  size_t   margin;

  if (pool && loss_shape(j, &per_row, &rows, &nslot)) {
    margin = (size_t)nslot * per_row * 5u + 64u;
    if (j->restart && !(j->restart % per_row) && rows > j->restart / per_row) {
      jlpar_t  pa;
      jctx_t   ctx[IM_POOL_MAX];
      uint32_t nw = im_pool_size(pool), items, k;
      bool     valid, ok;

      pa.rpi  = j->restart / per_row;
      pa.ints = (rows + pa.rpi - 1u) / pa.rpi;
      valid   = j->sd.nseg >= pa.ints;
      for (k = 0; valid && k + 1u < pa.ints; k++) valid = j->sd.seg[k].marker == 0xD0u + (k & 7u);
      if (valid) {
        for (i = 0; i < nw; i++) {
          if (!ctx_init(&ctx[i], j, margin)) {
            while (i--) free(ctx[i].tail);
            return false;
          }
        }
        pa.j       = j;
        pa.ctx     = ctx;
        pa.per_row = per_row;
        pa.rows    = rows;
        pa.nslot   = nslot;
        items      = nw * 4u < pa.ints ? nw * 4u : pa.ints;
        pa.step    = (pa.ints + items - 1u) / items;
        items      = (pa.ints + pa.step - 1u) / pa.step;
        ok         = im_pool_run(pool, loss_par_item, &pa, items);
        for (i = 0; i < nw; i++) free(ctx[i].tail);
        return ok;
      }
    }
    if (!j->restart && j->sd.nseg >= 1u && j->sd.seg[0].end - j->sd.seg[0].start >= (256u << 10)) {
      bool done;

      if (loss_spec(j, pool, per_row, rows, nslot, margin, &done)) return true;
      if (done) return false;
    }
  }
  return loss_seq(j);
}

/* -- from an index (IM_OPTION_INDEX) -------------------------------------------- */

/* the rows wanted in parts, each from its row's start as the index has it:
   its bytes, to the next part's row and a little past (its last MCU's bits
   end there; the scan's end for the last), unstuffed on its own, its DCs
   and bit set, its rows decoded (a region's blocks alone transformed). A
   region much narrower than the image: each row a part of its own, from
   the place before the region's first column to its last */
typedef struct jix {
  jpg_t    *j;
  jctx_t   *ctx;              /* a worker's */
  jdata_t  *data;             /* a worker's bytes */
  uint32_t  w, items;
  uint32_t  row[IM_POOL_MAX * 4u + 1u];
  uint32_t  first, k0, k1, c1; /* by rows: the first; the places from and
                                  past; the MCU column past the region */
  uint32_t  last_good;        /* the last part's */
} jix_t;

/* a worker's reader on the bytes from `cp`'s to `to`, unstuffed, at its
   bit with its DCs */
static bool
ix_reader(jix_t *ix, jctx_t *x, jdata_t *d, const jcp_t *cp, const uint8_t *to) {
  jpg_t   *j = ix->j;
  jrd_t   *r = &x->r;
  uint32_t i;

  if (!jpg_unstuff(d, j->data + (cp->at >> 3), to)) return false;
  for (i = 0; i < J_COMPS; i++) x->dc[i] = 0;
  for (i = 0; i < j->nscomp; i++) x->dc[j->scomp[i]] = cp->dc[i];
  x->eobrun  = 0u;
  x->lacking = false;
  r->b.buf   = 0u;
  r->b.n     = 0u;
  r->pre     = 0u;
  r->org     = d->buf;
  r->len     = d->nseg ? d->seg[0].end - d->seg[0].start : 0u;
  r->tail    = false;
  r->b.p     = r->org;
  r->lim     = r->len > x->margin ? r->org + (r->len - x->margin) : r->org;
  if (r->b.p >= r->lim) rd_tail(x);
  refill(&r->b);
  DROP(&r->b, (uint32_t)(cp->at & 7u));
  return true;
}

static bool
ix_item(void *arg, uint32_t worker, uint32_t item) {
  jix_t         *ix = arg;
  jpg_t         *j  = ix->j;
  const ImIndex *in = j->idx_in;
  jctx_t        *x  = &ix->ctx[worker];
  uint32_t       r0 = ix->row[item], r1 = ix->row[item + 1u], nb;
  const jcp_t   *cp = &in->cp[(size_t)r0 * in->cols], *next = r1 < in->rows ? &in->cp[(size_t)r1 * in->cols] : NULL;
  const uint8_t *to = j->data + in->end;
  jsblk_t        list[J_MCU_BLOCKS];

  if (next && (next->at >> 3) + 16u < in->end) to = j->data + (next->at >> 3) + 16u;
  if (!ix_reader(ix, x, &ix->data[worker], cp, to)) return false;
  nb = seq_list(x, list);
  seq_range(x, list, nb, ix->w, r0 * ix->w * nb, r1 * ix->w * nb);
  if (item + 1u == ix->items) ix->last_good = x->last_good;
  return true;
}

/* a narrow region's row: from the place before its first column to its
   last, the bytes to the place past it and a little more */
static bool
ix_row(void *arg, uint32_t worker, uint32_t item) {
  jix_t         *ix = arg;
  jpg_t         *j  = ix->j;
  const ImIndex *in = j->idx_in;
  jctx_t        *x  = &ix->ctx[worker];
  uint32_t       row = ix->first + item, nb;
  const jcp_t   *cp = &in->cp[(size_t)row * in->cols + ix->k0], *next = NULL;
  const uint8_t *to = j->data + in->end;
  jsblk_t        list[J_MCU_BLOCKS];

  if (ix->k1 < in->cols) next = &in->cp[(size_t)row * in->cols + ix->k1];
  else if (row + 1u < in->rows) next = &in->cp[(size_t)(row + 1u) * in->cols];
  if (next && (next->at >> 3) + 16u < in->end) to = j->data + (next->at >> 3) + 16u;
  if (!ix_reader(ix, x, &ix->data[worker], cp, to)) return false;
  nb = seq_list(x, list);
  seq_range(x, list, nb, ix->w, (row * ix->w + ix->k0 * J_IXCOL) * nb, (row * ix->w + ix->c1) * nb);
  if (item + 1u == ix->items) ix->last_good = x->last_good;
  return true;
}

/* the MCU columns a region's blocks lie in, [c0, c1) */
static void
region_cols(const jpg_t *j, uint32_t w, uint32_t *c0, uint32_t *c1) {
  uint32_t i, a = w, b = 0u;

  for (i = 0; i < j->nscomp; i++) {
    const jcomp_t *c  = &j->comp[j->scomp[i]];
    uint32_t       nh = j->nscomp == 1u ? 1u : c->h;

    if (c->rbx1 <= c->rbx0) continue;
    if (c->rbx0 / nh < a) a = c->rbx0 / nh;
    if ((c->rbx1 + nh - 1u) / nh > b) b = (c->rbx1 + nh - 1u) / nh;
  }
  *c0 = a;
  *c1 = b < w ? b : w;
}

IM_HIDE bool
jpg_scan_index(jpg_t *j, im_pool_t *pool, uint32_t w, uint32_t h, uint32_t r0, size_t margin) {
  const ImIndex *in = j->idx_in;
  jix_t          ix;
  jctx_t         ctx[IM_POOL_MAX];
  jdata_t        data[IM_POOL_MAX];
  uint32_t       nw = im_pool_size(pool), a, top, k, i;
  bool           ok = true;
  im_job_fn      fn = ix_item;

  /* from the region's first row, or the index's last before it */
  a = r0 < in->rows ? r0 : in->rows - 1u;
  if (a >= h) return true;
  top = h < in->rows ? h : in->rows;
  memset(&ix, 0, sizeof(ix));
  ix.j     = j;
  ix.w     = w;
  ix.items = nw > 1u ? nw * 4u : 1u;
  if (ix.items > top - a) ix.items = top - a;
  for (k = 0; k < ix.items; k++) ix.row[k] = a + (uint32_t)((uint64_t)(top - a) * k / ix.items);
  ix.row[ix.items] = h;

  /* a region a quarter of the rows' width or less, every row of it in the
     index: each row from the place before it */
  if (j->region && a == r0 && h <= in->rows) {
    uint32_t c0, c1;

    region_cols(j, w, &c0, &c1);
    if (c0 < c1 && (c1 - c0 / J_IXCOL * J_IXCOL) * 4u <= w) {
      ix.first = a;
      ix.k0    = c0 / J_IXCOL;
      ix.k1    = (c1 + J_IXCOL - 1u) / J_IXCOL;
      ix.c1    = c1;
      ix.items = h - a;
      fn       = ix_row;
    }
  }
  memset(data, 0, sizeof(data));
  for (i = 0; i < nw; i++) {
    if (!ctx_init(&ctx[i], j, margin)) {
      while (i--) free(ctx[i].tail);
      return false;
    }
  }
  ix.ctx  = ctx;
  ix.data = data;
  ok      = im_pool_run(pool, fn, &ix, ix.items);
  j->last_good = ix.last_good;
  for (i = 0; i < nw; i++) {
    free(ctx[i].tail);
    free(data[i].buf);
    free(data[i].seg);
  }
  return ok;
}


/* the scan whose data `j->sd` holds; `pool` its threads, if any */
IM_HIDE bool
jpg_scan_huff(jpg_t *j, im_pool_t *pool) {
  jctx_t   x;
  uint32_t w, h, hfull, i, blocks = 0u, n, total, cur = 0u, next_rst = 0u, togo, first = 0u;
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
  /* a region's rows: those below it not decoded */
  hfull = h;
  h     = jpg_region_rows(j, w, h, &first);
  total = w * h;

  /* a big sequential scan's AC tables in pairs, here before any thread
     reads them (a table's look-ups cost more than a small scan's blocks
     save); without memory for them, one symbol a look-up */
  for (i = 0; i < j->nscomp && j->process != J_LOSSLESS; i++) {
    jhuff_t *d = j->dc[j->comp[j->scomp[i]].td & 3u];

    /* a DC table's DC+AC look-ups are this scan's or none: its AC table
       may have been built again since */
    if (d) {
      free(d->pair);
      d->pair    = NULL;
      d->pair_ac = NULL;
    }
  }
  if (j->process == J_SEQUENTIAL && (uint64_t)total * blocks >= J_PAIR_MIN
      && (j->via_index ? (uint64_t)(j->idx_in->end - j->idx_in->scan)
          : (uint64_t)j->sd.bytes)
         >= (uint64_t)J_PAIR_BYTES * hfull * w * blocks) {
    for (i = 0; i < j->nscomp; i++) {
      jhuff_t *a = j->ac[j->comp[j->scomp[i]].ta & 3u];

      if (a && a->ok && !a->pair) a->pair = huff_pairs(a);
    }
    for (i = 0; i < j->nscomp; i++) {
      const jcomp_t *c = &j->comp[j->scomp[i]];
      jhuff_t       *d = j->dc[c->td & 3u], *a = j->ac[c->ta & 3u];
      uint32_t       o;
      bool           one = true;

      /* where every component with this DC table has the same AC table */
      for (o = 0; o < j->nscomp; o++) {
        const jcomp_t *e = &j->comp[j->scomp[o]];

        one = one && ((e->td & 3u) != (c->td & 3u) || (e->ta & 3u) == (c->ta & 3u));
      }
      if (one && d && d->ok && a && a->pair && !d->pair && (d->pair = huff_dcac(d, a))) d->pair_ac = a;
    }
  }

  /* from the index: the rows wanted (and their parts on threads) alone */
  if (j->via_index) return jpg_scan_index(j, pool, w, h, w ? first / w : 0u, margin);

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
      pa.k0    = region_skip(j, first, j->restart);
      items    = nw * 4u < ints - pa.k0 ? nw * 4u : ints - pa.k0;
      pa.step  = (ints - pa.k0 + items - 1u) / items;
      items    = (ints - pa.k0 + pa.step - 1u) / pa.step;
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
      && j->sd.seg[0].end - j->sd.seg[0].start >= (256u << 10) && h * 2u > hfull) {
    bool done;

    /* the whole segment (a region's rows past it decoded too, their
       blocks not transformed): its chunks are the segment's bytes */
    if (scan_spec(j, pool, w, hfull, margin, &done)) return true;
    if (done) return false;
  }

  /* in order */
  if (!ctx_init(&x, j, margin)) return false;
  if (!j->multiscan && j->process == J_SEQUENTIAL && !j->flow) {
    scan_seq(&x, w, h, first);
    j->last_good = x.last_good;
    free(x.tail);
    return true;
  }
  if (j->process == J_PROGRESSIVE) {
    uint32_t per = j->restart ? j->restart : total;

    rd_start(&x, 0u);
    for (n = 0; n < total; n += per) {
      if (n) restart(&x, &next_rst, &cur, &at_marker);
      prog_range(&x, w, n, total - n > per ? n + per : total);
    }
    j->last_good = x.last_good;
    free(x.tail);
    return true;
  }
  togo = j->restart;
  rd_start(&x, 0u);
  for (n = 0; n < total; n++) {
    if (j->flow && !(n % w)) jpg_flow_row(j, n / w);
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
