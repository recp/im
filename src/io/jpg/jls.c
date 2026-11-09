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
 * JPEG-LS (ITU-T T.87 | ISO/IEC 14495-1): lossless and near-lossless, 2 to
 * 16 bits a sample, up to 255 components, a scan's interleaved or not (by
 * lines, by samples), sampling factors, restart intervals, the preset
 * parameters, mapping tables and oversize dimensions (LSE), DNL, a point
 * transform; HP's colour transforms (APP8 "mrfx", as CharLS writes and
 * reads them), SPIFF's colour space and resolution, an ICC profile.
 *
 * The stream is read through first: its frame, each scan's parameters, its
 * data and the restart intervals in it. The intervals are decoded then,
 * each on its own (its contexts afresh, its first lines' neighbours zero:
 * D.4), on threads where there are several, a component's rows straight to
 * the pixels, or to its plane where it's subsampled or a colour transform
 * waits for the others; those planes to the pixels last.
 *
 * The pixels are CharLS's: a stream it decodes decodes to its samples (8
 * bits a sample to 8, deeper to 16 and those not a whole byte spread over
 * it as for JPEG), but where CharLS isn't T.87's: a preset MAXVAL below
 * 2^P - 1 sets RANGE (A.2.1; CharLS takes 2^P - 1), a RESET past 255 is
 * kept whole (CharLS keeps its low byte), as the JPEG XT reference
 * software reads them; a line-interleaved scan's last MCU has the lines B.4
 * adds. A code the stream can't have (past the limit, an error past the
 * range, a run past the line) fails the decode, as it fails in CharLS.
 */

#include "jpgint.h"
#include "../../endian.h"

enum {
  L_COMPS  = 255, /* components a frame */
  L_SCOMPS = 4,   /* a scan's (T.81's Ns) */
  L_CTX    = 365  /* the regular mode's contexts */
};

/* RUNindex's run lengths' bits (A.2.1) */
static const uint8_t L_J[32] = {
  0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
  4, 4, 5, 5, 6, 6, 7, 7, 8, 9, 10, 11, 12, 13, 14, 15
};

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#  define L_HOST_BIG true
#else
#  define L_HOST_BIG false
#endif

IM_INLINE uint32_t
lbe16(const uint8_t *p) {
  return (uint32_t)p[0] << 8 | p[1];
}

IM_INLINE uint32_t
lbe(const uint8_t *p, uint32_t n) {
  uint32_t v = 0u, i;

  for (i = 0; i < n; i++) v = v << 8 | p[i];
  return v;
}

IM_INLINE uint64_t
lbe64(const uint8_t *p) {
  uint64_t v;

  memcpy(&v, p, 8);
#if L_HOST_BIG
  return v;
#else
  return bswapu64(v);
#endif
}

IM_INLINE uint32_t
lclz32(uint32_t v) {
#if defined(_MSC_VER) && !defined(__clang__)
  unsigned long i;

  _BitScanReverse(&i, v);
  return 31u - (uint32_t)i;
#else
  return (uint32_t)__builtin_clz(v);
#endif
}

IM_INLINE uint32_t
lclz64(uint64_t v) {
#if defined(_MSC_VER) && !defined(__clang__)
  unsigned long i;

  return _BitScanReverse64(&i, v) ? 63u - (uint32_t)i : 64u;
#else
  return v ? (uint32_t)__builtin_clzll(v) : 64u;
#endif
}

/* -- the stream's parts ------------------------------------------------------------ */

/* a mapping table (C.2.4.1.2): `n` entries of `wt` bytes; each definition its
   own (a scan keeps the one in force at it) */
typedef struct ltab {
  struct ltab *next;   /* every one made, to be freed */
  uint32_t     wt, n;
  uint8_t     *e;
} ltab_t;

/* how a component's samples go out: as they are, through a table (8 or 16
   bits; the point transform, the spread to the bytes, the order), or a
   mapping table's three bytes */
enum {
  L_COPY8 = 0,
  L_COPY16,
  L_SWAP16,
  L_LUT8,
  L_LUT16,
  L_RGB
};

typedef struct lcomp {
  uint8_t   id, h, v;
  bool      coded;        /* a scan has had it */
  uint32_t  w, ht;        /* its samples across and down */
  uint32_t  scan;         /* the scan that has it */
  uint8_t   tm;           /* that scan's mapping table for it (0 none) */
  /* its rows wanted [y0, y1), its columns [x0, x1) */
  uint32_t  x0, x1, y0, y1;
  /* the pixels at `off` bytes into each (direct), or its plane */
  uint8_t  *plane;
  size_t    pstride;
  uint32_t  pes;          /* the plane's bytes a sample */
  size_t    off;
  uint32_t  how;          /* L_COPY8 … */
  void     *lut;          /* L_LUT8, L_LUT16's; L_RGB's table's entries */
  const ltab_t *tab;
} lcomp_t;

typedef struct lseg {
  const uint8_t *p, *end; /* its data, to its marker's 0xFF */
} lseg_t;

typedef struct lscan {
  uint32_t  n, comp[L_SCOMPS];
  uint32_t  ilv, near, pt;
  int32_t   maxval, t1, t2, t3, reset;
  uint32_t  ri;           /* the restart interval in MCUs (0 none) */
  uint32_t  nmcu;         /* its MCUs */
  uint32_t  mcu0, mcu1;   /* those wanted (the region's) */
  lseg_t   *seg;
  uint32_t  nseg, segcap;
  int8_t   *q;            /* the gradients' regions, q[d] for d in [-maxval, maxval] */
} lscan_t;

/* a work item: a scan's restart interval */
typedef struct litem {
  uint32_t scan, iv;
} litem_t;

typedef struct ljls {
  const uint8_t *data, *end;
  size_t         used;

  /* the frame */
  bool      frame;
  uint32_t  prec, width, height, ncomp, hmax, vmax;
  lcomp_t   comp[L_COMPS];

  /* in force: the presets (0 their defaults), the restart interval, the
     mapping tables by ID */
  int32_t   maxval, t1, t2, t3, reset;
  uint32_t  ri;
  ltab_t   *tab[256], *tabs;
  ltab_t   *open;         /* the one continuations go on */
  uint32_t  xe, ye;       /* LSE's oversize dimensions (0 none) */
  bool      dnl;          /* a DNL may come after the first scan */

  lscan_t  *scan;
  uint32_t  nscan, scancap;

  /* APP8's: HP's colour transform; SPIFF's colour space, resolution;
     APP14's (Adobe) word that three components are YCbCr */
  uint32_t  xform;
  bool      adobe_ycc;
  bool      spiff;
  uint32_t  spiff_cs, spiff_unit, spiff_vres, spiff_hres;
  /* APP2's ICC profile chunks (by sequence number, in the data) */
  const uint8_t *icc_part[256];
  uint32_t  icc_part_len[256], icc_count;
  bool      icc_bad;

  /* the caller's */
  ImByteOrder order;
  bool       swap, rgb_pad;
  uint32_t   threads;
  im_runner_t runner;
  uint8_t   *dest;
  size_t     dest_stride, dest_size, mem_limit;
  bool       over;
  bool       region;
  uint32_t   reg_x, reg_y, reg_w, reg_h;

  /* the pixels out */
  uint8_t   *px;
  size_t     pitch;
  uint32_t   on, oes;     /* samples a pixel, bytes a sample */
  bool       own, planes, pad;
  ImFormat   format;
  ImColorSpace space;

  /* the work */
  litem_t   *item;
  uint32_t   nitem;
  im_pool_t *pool;
  uint16_t  *lines[IM_POOL_MAX]; /* each worker's */
  size_t     lines_len;
} ljls_t;

/* -- bits (A.1) ------------------------------------------------------------------- */

/* where a segment's bytes come from: past a 0xFF the next byte's low seven
   bits (its top one the stuffed 0); a marker (0xFF, then a byte from 0x80)
   or the segment's end ends them, zeros after */
typedef struct lsrc {
  const uint8_t *p, *end;
  bool           ff;      /* the last byte taken 0xFF */
} lsrc_t;

/* the bits, MSB first: `buf` holds `n` of them (the rest zero) */
typedef struct lhot {
  uint64_t buf;
  uint32_t n;
} lhot_t;

static J_NOINLINE lhot_t
fill_slow(lsrc_t *s, lhot_t h) {
  while (h.n <= 56u) {
    uint32_t c;

    if (s->p >= s->end) {
      /* zeros from here (a stream cut short decodes to an error or not) */
      h.n    = 64u;
      break;
    }
    c = *s->p;
    if (s->ff) {
      if (c & 0x80u) {
        s->end = s->p; /* a marker: the data's end */
        continue;
      }
      h.buf |= (uint64_t)c << (57u - h.n);
      h.n += 7u;
    } else {
      h.buf |= (uint64_t)c << (56u - h.n);
      h.n += 8u;
    }
    s->ff = c == 0xFFu;
    s->p++;
  }
  return h;
}

/* `n` to 57 or more: eight bytes at once where none is 0xFF */
IM_INLINE void
fill(lsrc_t *s, uint64_t *buf, uint32_t *n) {
  const uint8_t *p = s->p;

  if (likely(s->end - p >= 8 && !s->ff)) {
    uint64_t v = lbe64(p), x = ~v;

    if (!((x - 0x0101010101010101ull) & ~x & 0x8080808080808080ull)) {
      uint32_t k = (64u - *n) >> 3; /* whole bytes that fit */

      *buf |= (v & (~0ull << (64u - 8u * k))) >> *n;
      *n += 8u * k;
      s->p = p + k;
      return;
    }
  }
  {
    lhot_t h = { *buf, *n };

    h    = fill_slow(s, h);
    *buf = h.buf;
    *n   = h.n;
  }
}

/* `k` bits (1 to 32) */
IM_INLINE uint32_t
take(lsrc_t *s, uint64_t *buf, uint32_t *n, uint32_t k) {
  uint32_t v;

  if (*n < k) fill(s, buf, n);
  v = (uint32_t)(*buf >> (64u - k));
  *buf <<= k;
  *n -= k;
  return v;
}

IM_INLINE uint32_t
take1(lsrc_t *s, uint64_t *buf, uint32_t *n) {
  uint32_t v;

  if (!*n) fill(s, buf, n);
  v = (uint32_t)(*buf >> 63);
  *buf <<= 1;
  *n -= 1u;
  return v;
}

/* LG(k, limit) (A.5.3): the mapped error; past `range` where the code
   can't be one (its unary part past the limit). Bits topped up only where
   fewer than a whole unary part's are there: zeros past them then mean a
   code past the limit */
IM_INLINE uint64_t
golomb(lsrc_t *s, uint64_t *buf, uint32_t *n, uint32_t k, uint32_t limit, uint32_t qbpp) {
  uint32_t top = limit - qbpp - 1u, z;

  if (*n <= top) fill(s, buf, n);
  z = lclz64(*buf);
  if (likely(z < top)) {
    uint64_t b;

    if (likely(z + 1u + k <= *n)) {
      b = *buf << (z + 1u);
      *buf = b << k;
      *n -= z + 1u + k;
      return (uint64_t)z << k | ((b >> 1) >> (63u - k));
    }
    *buf <<= z + 1u;
    *n -= z + 1u;
    return (uint64_t)z << k | (k ? take(s, buf, n, k) : 0u);
  }
  if (z == top) {
    *buf <<= z + 1u;
    *n -= z + 1u;
    return (uint64_t)take(s, buf, n, qbpp) + 1u;
  }
  return ~0ull;
}

/* -- the decoding (Annex A, F) ---------------------------------------------------- */

/* a regular mode context: A, B, C, N (16 bytes: its address a shift) */
typedef struct lctx {
  int32_t  a, b;
  int16_t  c;
  uint16_t n;
  uint32_t pad;
} lctx_t;

/* a restart interval's state: its contexts (the regular mode's; the run
   interruptions', RItype 0 and 1), RUNindex (by lines, the components'),
   the bits */
typedef struct ldec {
  lctx_t    k[L_CTX];
  int32_t   ra[2];
  uint32_t  rn[2], rnn[2];
  uint32_t  run[L_SCOMPS];
  lsrc_t    src;
  uint64_t  buf;
  uint32_t  n;
  /* the scan's */
  const int8_t *q;
  int32_t   maxval, near, range, reset;
  uint32_t  qbpp, limit;
} ldec_t;

/* the smallest k with n·2^k ≥ a (A.10) */
IM_INLINE uint32_t
kof(uint32_t n, uint32_t a) {
  uint32_t k = 0u;

  /* a loop, not arithmetic on the leading zeros: its branches predicted, the
     bits read on before A and N are there */
  while (((uint64_t)n << k) < a) k++;
  return k;
}

/* A.12, A.13 */
IM_INLINE void
update(lctx_t *c, int32_t e, int32_t d2, int32_t reset) {
  int32_t b = c->b + e * d2, n = c->n;

  c->a += e < 0 ? -e : e;
  if (unlikely(n == reset)) {
    c->a >>= 1;
    b = b >= 0 ? b >> 1 : -((1 - b) >> 1);
    n >>= 1;
  }
  n++;
  if (b + n <= 0) {
    b += n;
    if (b <= -n) b = -n + 1;
    if (c->c > -128) c->c--;
  } else if (b > 0) {
    b -= n;
    if (b > 0) b = 0;
    if (c->c < 127) c->c++;
  }
  c->b = b;
  c->n = (uint16_t)n;
}

/* a line's parameters: the scan's, or constants a specialization fixes
   (lossless with MAXVAL 2^P - 1: the modulo a mask) */
typedef struct lpar {
  int32_t  near, maxval, range, reset;
  uint32_t qbpp, limit;
  bool     pow2;
} lpar_t;

/* Rx from the prediction and the error (F.1, 14): modulo the range, near-
   lossless's into [-NEAR, RANGE·(2·NEAR+1) - 1 - NEAR] first, then clamped */
IM_INLINE int32_t
recon(int32_t px, int32_t e, const lpar_t P) {
  int32_t rx = px + e;

  if (!P.near) {
    if (P.pow2) return rx & P.maxval;
    rx += rx < 0 ? P.range : 0;
    rx -= rx > P.maxval ? P.range : 0;
    return rx;
  }
  if (rx < -P.near) rx += P.range * (2 * P.near + 1);
  else if (rx > P.maxval + P.near) rx -= P.range * (2 * P.near + 1);
  return rx < 0 ? 0 : rx > P.maxval ? P.maxval : rx;
}

/* a sample in the regular mode, `qs` its gradients' regions (A.3–A.6) */
IM_INLINE bool
regular(lctx_t *K, lsrc_t *s, uint64_t *buf, uint32_t *n, int32_t qs, int32_t ra, int32_t rb, int32_t rc,
        const lpar_t P, int32_t *out) {
  int32_t  sg = qs >> 31, mx, mn, px, e;
  lctx_t  *c = &K[(qs ^ sg) - sg];
  uint32_t k;
  uint64_t m;

  mx = ra > rb ? ra : rb;
  mn = ra > rb ? rb : ra;
  px = rc >= mx ? mn : rc <= mn ? mx : ra + rb - rc;
  px += (c->c ^ sg) - sg;
  px = px < 0 ? 0 : px > P.maxval ? P.maxval : px;
  k  = kof(c->n, (uint32_t)c->a);
  m  = golomb(s, buf, n, k, P.limit, P.qbpp);
  if (unlikely(m > (uint64_t)P.range)) return false;
  e = (int32_t)(m >> 1) ^ -(int32_t)(m & 1u);
  if (!P.near && !k && 2 * c->b <= -(int32_t)c->n) e = ~e;
  update(c, e, 2 * P.near + 1, P.reset);
  e *= 2 * P.near + 1;
  *out = recon(px, (e ^ sg) - sg, P);
  return true;
}

/* a run interruption sample (A.7.2), `ritype` its context, `glimit` its
   limit */
IM_INLINE bool
interrupt(ldec_t *d, uint64_t *buf, uint32_t *nb, int32_t ra, int32_t rb, uint32_t ritype, uint32_t glimit,
          const lpar_t P, int32_t *out) {
  uint32_t n = d->rn[ritype], nn = d->rnn[ritype], temp, k, t, map, ab;
  int32_t  e, px;
  uint64_t m;

  temp = (uint32_t)d->ra[ritype] + (ritype ? n >> 1 : 0u);
  k    = kof(n, temp);
  m    = golomb(&d->src, buf, nb, k, glimit, P.qbpp);
  if (unlikely(m > (uint64_t)P.range)) return false;
  t   = (uint32_t)m + ritype;
  map = t & 1u;
  ab  = (t + map) >> 1;
  e   = (k != 0u || 2u * nn >= n) == (map != 0u) ? -(int32_t)ab : (int32_t)ab;
  /* A.23 */
  if (e < 0) nn++;
  d->ra[ritype] += (int32_t)(((uint32_t)m + 1u - ritype) >> 1);
  if (n == (uint32_t)P.reset) {
    d->ra[ritype] >>= 1;
    n >>= 1;
    nn >>= 1;
  }
  d->rn[ritype]  = n + 1u;
  d->rnn[ritype] = nn;
  px = ritype ? ra : rb;
  e *= 2 * P.near + 1;
  if (!ritype && ra > rb) e = -e;
  *out = recon(px, e, P);
  return true;
}

/* a component's line (lines -1 and w its neighbours' room): `pr` the one
   above, `ri` its RUNindex; the bits in registers through it */
IM_INLINE bool
line_one(ldec_t *d, uint16_t *pr, uint16_t *cu, uint32_t w, uint32_t *rip, const lpar_t P) {
  const int8_t *q = d->q;
  lctx_t       *K = d->k;
  lsrc_t       *s = &d->src;
  uint64_t      buf = d->buf;
  uint32_t      nb = d->n, x = 0u, ri = *rip;

  int32_t       rb, rd, ra;

  pr[w]  = pr[w - 1u];
  cu[-1] = pr[0];
  rb     = pr[-1];
  rd     = pr[0];
  ra     = cu[-1];
  while (x < w) {
    int32_t rc = rb, qs, rx;

    rb = rd;
    rd = pr[x + 1u];
    qs = (q[rd - rb] * 9 + q[rb - rc]) * 9 + q[rc - ra];
    if (likely(qs)) {
      if (unlikely(!regular(K, s, &buf, &nb, qs, ra, rb, rc, P, &rx))) return false;
      cu[x++] = (uint16_t)rx;
      ra = rx;
      continue;
    }
    /* the run mode (A.7, F.1 18) */
    {
      uint32_t left = w - x, cnt, i;

      for (;;) {
        if (take1(s, &buf, &nb)) {
          uint32_t r = 1u << L_J[ri];

          cnt = r < left ? r : left;
          for (i = 0; i < cnt; i++) cu[x + i] = (uint16_t)ra;
          x += cnt;
          left -= cnt;
          if (cnt == r && ri < 31u) ri++;
          if (!left) break;
          continue;
        }
        cnt = L_J[ri] ? take(s, &buf, &nb, L_J[ri]) : 0u;
        if (unlikely(cnt > left)) return false;
        for (i = 0; i < cnt; i++) cu[x + i] = (uint16_t)ra;
        x += cnt;
        left -= cnt;
        if (!left) break; /* to the line's end: no interruption (CharLS's) */
        rb = pr[x];
        if (unlikely(!interrupt(d, &buf, &nb, ra, rb, (rb - ra <= P.near && ra - rb <= P.near) ? 1u : 0u,
                                P.limit - L_J[ri] - 1u, P, &rx))) {
          return false;
        }
        cu[x++] = (uint16_t)rx;
        if (ri) ri--;
        break;
      }
      /* the neighbours where the run left off */
      if (x < w) {
        rb = pr[(int32_t)x - 1];
        rd = pr[x];
        ra = cu[(int32_t)x - 1];
      }
    }
  }
  d->buf = buf;
  d->n   = nb;
  *rip   = ri;
  return true;
}

/* the scan's parameters */
IM_INLINE lpar_t
par_of(const ldec_t *d, bool pow2) {
  lpar_t P;

  P.near   = d->near;
  P.maxval = d->maxval;
  P.range  = d->range;
  P.reset  = d->reset;
  P.qbpp   = d->qbpp;
  P.limit  = d->limit;
  P.pow2   = pow2;
  return P;
}

typedef bool (*lline_fn)(ldec_t *, uint16_t *, uint16_t *, uint32_t, uint32_t *);

/* lossless, 8 bits (MAXVAL 255) and 16 (65535): the constants fixed */
static bool
line_l8(ldec_t *d, uint16_t *pr, uint16_t *cu, uint32_t w, uint32_t *ri) {
  const lpar_t P = { 0, 255, 256, d->reset, 8u, 32u, true };

  return line_one(d, pr, cu, w, ri, P);
}

static bool
line_l16(ldec_t *d, uint16_t *pr, uint16_t *cu, uint32_t w, uint32_t *ri) {
  const lpar_t P = { 0, 65535, 65536, d->reset, 16u, 64u, true };

  return line_one(d, pr, cu, w, ri, P);
}

/* lossless, MAXVAL 2^P - 1 */
static bool
line_lp(ldec_t *d, uint16_t *pr, uint16_t *cu, uint32_t w, uint32_t *ri) {
  lpar_t P = par_of(d, true);

  P.near = 0;
  return line_one(d, pr, cu, w, ri, P);
}

/* lossless, any MAXVAL */
static bool
line_ll(ldec_t *d, uint16_t *pr, uint16_t *cu, uint32_t w, uint32_t *ri) {
  lpar_t P = par_of(d, false);

  P.near = 0;
  return line_one(d, pr, cu, w, ri, P);
}

static bool
line_nl(ldec_t *d, uint16_t *pr, uint16_t *cu, uint32_t w, uint32_t *ri) {
  return line_one(d, pr, cu, w, ri, par_of(d, false));
}

/* the line kind for the scan's parameters */
static lline_fn
line_for(const ldec_t *d) {
  if (d->near) return line_nl;
  if (d->maxval == 255) return line_l8;
  if (d->maxval == 65535) return line_l16;
  return (d->maxval & (d->maxval + 1)) == 0 ? line_lp : line_ll;
}

/* a line of `nc` components a sample each in turn (B.3): one run for all,
   RItype 0 */
IM_INLINE bool
line_px_one(ldec_t *d, uint16_t **pr, uint16_t **cu, const uint32_t nc, uint32_t w, const lpar_t P) {
  const int8_t *q = d->q;
  lctx_t       *K = d->k;
  lsrc_t       *s = &d->src;
  uint64_t      buf = d->buf;
  uint32_t      nb = d->n, x = 0u, ri = d->run[0], c;
  int32_t       ra[L_SCOMPS], rb[L_SCOMPS], rd[L_SCOMPS];

  /* the neighbours in registers along the line (a sample's Ra the one just
     made, not read back) */
  for (c = 0; c < nc; c++) {
    pr[c][w]  = pr[c][w - 1u];
    cu[c][-1] = pr[c][0];
    rb[c]     = pr[c][-1];
    rd[c]     = pr[c][0];
    ra[c]     = cu[c][-1];
  }
  while (x < w) {
    int32_t qs[L_SCOMPS], rc[L_SCOMPS], any = 0, rx;

    for (c = 0; c < nc; c++) {
      rc[c] = rb[c];
      rb[c] = rd[c];
      rd[c] = pr[c][x + 1u];
      qs[c] = (q[rd[c] - rb[c]] * 9 + q[rb[c] - rc[c]]) * 9 + q[rc[c] - ra[c]];
      any |= qs[c];
    }
    if (any) {
      for (c = 0; c < nc; c++) {
        if (unlikely(!regular(K, s, &buf, &nb, qs[c], ra[c], rb[c], rc[c], P, &rx))) return false;
        cu[c][x] = (uint16_t)rx;
        ra[c]    = rx;
      }
      x++;
      continue;
    }
    {
      uint32_t left = w - x, cnt, i;

      for (;;) {
        if (take1(s, &buf, &nb)) {
          uint32_t r = 1u << L_J[ri];

          cnt = r < left ? r : left;
          for (c = 0; c < nc; c++) {
            uint16_t v = (uint16_t)ra[c];

            for (i = 0; i < cnt; i++) cu[c][x + i] = v;
          }
          x += cnt;
          left -= cnt;
          if (cnt == r && ri < 31u) ri++;
          if (!left) break;
          continue;
        }
        cnt = L_J[ri] ? take(s, &buf, &nb, L_J[ri]) : 0u;
        if (unlikely(cnt > left)) return false;
        for (c = 0; c < nc; c++) {
          uint16_t v = (uint16_t)ra[c];

          for (i = 0; i < cnt; i++) cu[c][x + i] = v;
        }
        x += cnt;
        left -= cnt;
        if (!left) break;
        for (c = 0; c < nc; c++) {
          if (unlikely(!interrupt(d, &buf, &nb, ra[c], pr[c][x], 0u, P.limit - L_J[ri] - 1u, P, &rx))) return false;
          cu[c][x] = (uint16_t)rx;
        }
        x++;
        if (ri) ri--;
        break;
      }
      /* the neighbours where the run left off */
      if (x < w) {
        for (c = 0; c < nc; c++) {
          rb[c] = pr[c][(int32_t)x - 1];
          rd[c] = pr[c][x];
          ra[c] = cu[c][(int32_t)x - 1];
        }
      }
    }
  }
  d->buf    = buf;
  d->n      = nb;
  d->run[0] = ri;
  return true;
}

/* by the components' count (its loops unrolled), lossless 8 bits apart */
#define L_PX(name, NC)                                                                          \
  static bool name(ldec_t *d, uint16_t **pr, uint16_t **cu, uint32_t w) {                       \
    if (!d->near && d->maxval == 255) {                                                         \
      const lpar_t P = { 0, 255, 256, d->reset, 8u, 32u, true };                                \
                                                                                                \
      return line_px_one(d, pr, cu, NC, w, P);                                                  \
    }                                                                                           \
    return line_px_one(d, pr, cu, NC, w, par_of(d, !d->near && (d->maxval & (d->maxval + 1)) == 0)); \
  }
L_PX(line_px2, 2u)
L_PX(line_px3, 3u)
L_PX(line_px4, 4u)
#undef L_PX

static bool
line_px(ldec_t *d, uint16_t **pr, uint16_t **cu, uint32_t nc, uint32_t w) {
  return nc == 3u ? line_px3(d, pr, cu, w) : nc == 4u ? line_px4(d, pr, cu, w) : line_px2(d, pr, cu, w);
}

/* -- rows out --------------------------------------------------------------------- */

/* `cnt` samples of component `c` (its out kind) into a pixel row at `o`,
   `on` samples a pixel apart */
static void
put(const ljls_t *j, const lcomp_t *c, const uint16_t *s, uint32_t cnt, uint8_t *o) {
  uint32_t on = j->on, i;

  switch (c->how) {
  case L_COPY8:
    if (on == 1u) {
      for (i = 0; i < cnt; i++) o[i] = (uint8_t)s[i];
    } else {
      for (i = 0; i < cnt; i++) o[(size_t)i * on] = (uint8_t)s[i];
    }
    break;
  case L_COPY16: {
    uint16_t *d = (uint16_t *)(void *)o;

    if (on == 1u) memcpy(d, s, (size_t)cnt * 2u);
    else for (i = 0; i < cnt; i++) d[(size_t)i * on] = s[i];
    break;
  }
  case L_SWAP16: {
    uint16_t *d = (uint16_t *)(void *)o;

    for (i = 0; i < cnt; i++) d[(size_t)i * on] = bswapu16(s[i]);
    break;
  }
  case L_LUT8: {
    const uint8_t *t = c->lut;

    for (i = 0; i < cnt; i++) o[(size_t)i * on] = t[s[i]];
    break;
  }
  case L_LUT16: {
    const uint16_t *t = c->lut;
    uint16_t       *d = (uint16_t *)(void *)o;

    for (i = 0; i < cnt; i++) d[(size_t)i * on] = t[s[i]];
    break;
  }
  default: { /* L_RGB: one component, three bytes an entry */
    const uint8_t *t = c->lut;

    for (i = 0; i < cnt; i++) memcpy(o + (size_t)i * on, t + (size_t)s[i] * 3u, 3u);
    break;
  }
  }
}

/* RGB0's fourth samples */
static void
put_pad(const ljls_t *j, uint32_t cnt, uint8_t *o) {
  uint32_t i;

  if (j->oes == 1u) {
    for (i = 0; i < cnt; i++) o[(size_t)i * 4u + 3u] = 0xFFu;
  } else {
    uint16_t *d = (uint16_t *)(void *)o;

    for (i = 0; i < cnt; i++) d[(size_t)i * 4u + 3u] = 0xFFFFu;
  }
}

/* row `y` of component `ci` decoded: to the pixels or its plane, where
   wanted */
static void
store(const ljls_t *j, uint32_t ci, uint32_t y, const uint16_t *line) {
  const lcomp_t *c = &j->comp[ci];
  uint32_t       i, cnt;

  if (y < c->y0 || y >= c->y1) return;
  cnt = c->x1 - c->x0;
  line += c->x0;
  if (!j->planes) {
    uint8_t *o = j->px + (size_t)(y - c->y0) * j->pitch;

    put(j, c, line, cnt, o + c->off);
    if (j->pad && ci == j->ncomp - 1u) put_pad(j, cnt, o);
    return;
  }
  if (c->pes == 1u) {
    uint8_t *o = c->plane + (size_t)(y - c->y0) * c->pstride;

    for (i = 0; i < cnt; i++) o[i] = (uint8_t)line[i];
  } else {
    memcpy(c->plane + (size_t)(y - c->y0) * c->pstride, line, (size_t)cnt * 2u);
  }
}

/* a row of every component (a scan of them all by samples, each out as it
   is, three or four of them): interleaved at once; false where it isn't
   one */
static bool
store_all(const ljls_t *j, const lscan_t *s, uint32_t y, uint16_t *const *cu) {
  const uint16_t *a, *b, *c, *e;
  uint32_t        n = j->ncomp, i, k, cnt, how = j->comp[0].how;
  uint8_t        *o;

  if (j->planes || s->n != n || (n != 3u && n != 4u) || (how != L_COPY8 && how != L_COPY16)) return false;
  for (k = 0; k < n; k++) {
    if (s->comp[k] != k || j->comp[k].how != how) return false;
  }
  if (y < j->comp[0].y0 || y >= j->comp[0].y1) return true;
  cnt = j->comp[0].x1 - j->comp[0].x0;
  k   = j->comp[0].x0;
  a   = cu[0] + k;
  b   = cu[1] + k;
  c   = cu[2] + k;
  e   = n == 4u ? cu[3] + k : NULL;
  o   = j->px + (size_t)(y - j->comp[0].y0) * j->pitch;
  if (how == L_COPY8) {
    if (n == 3u && j->on == 3u) {
      for (i = 0; i < cnt; i++) {
        o[3u * i]      = (uint8_t)a[i];
        o[3u * i + 1u] = (uint8_t)b[i];
        o[3u * i + 2u] = (uint8_t)c[i];
      }
    } else {
      uint8_t pad = 0xFFu;

      for (i = 0; i < cnt; i++) {
        o[4u * i]      = (uint8_t)a[i];
        o[4u * i + 1u] = (uint8_t)b[i];
        o[4u * i + 2u] = (uint8_t)c[i];
        o[4u * i + 3u] = e ? (uint8_t)e[i] : pad;
      }
    }
  } else {
    uint16_t *d = (uint16_t *)(void *)o;

    if (n == 3u && j->on == 3u) {
      for (i = 0; i < cnt; i++) {
        d[3u * i]      = a[i];
        d[3u * i + 1u] = b[i];
        d[3u * i + 2u] = c[i];
      }
    } else {
      for (i = 0; i < cnt; i++) {
        d[4u * i]      = a[i];
        d[4u * i + 1u] = b[i];
        d[4u * i + 2u] = c[i];
        d[4u * i + 3u] = e ? e[i] : 0xFFFFu;
      }
    }
  }
  return true;
}

/* -- a restart interval ----------------------------------------------------------- */

static bool
interval(void *arg, uint32_t worker, uint32_t item) {
  ljls_t        *j = arg;
  const litem_t *it = &j->item[item];
  const lscan_t *s = &j->scan[it->scan];
  ldec_t         d;
  uint16_t      *pr[L_SCOMPS], *cu[L_SCOMPS], *lines = j->lines[worker];
  uint32_t       m0 = it->iv * (s->ri ? s->ri : s->nmcu), m1, m, c, ainit, i;
  lline_fn       line;

  m1 = s->ri ? m0 + s->ri : s->nmcu;
  if (m1 > s->nmcu) m1 = s->nmcu;
  if (m1 > s->mcu1) m1 = s->mcu1; /* past the region: not wanted */
  if (it->iv >= s->nseg) return false;

  /* afresh (A.2.1, D.4) */
  d.q      = s->q + s->maxval;
  d.maxval = s->maxval;
  d.near   = (int32_t)s->near;
  d.range  = (s->maxval + 2 * (int32_t)s->near) / (2 * (int32_t)s->near + 1) + 1;
  d.reset  = s->reset;
  {
    uint32_t r = (uint32_t)d.range - 1u, bpp, mx = (uint32_t)s->maxval;

    d.qbpp = r ? 32u - lclz32(r) : 0u;
    bpp    = mx ? 32u - lclz32(mx) : 0u;
    bpp    = bpp < 2u ? 2u : bpp;
    d.limit = 2u * (bpp + (bpp > 8u ? bpp : 8u));
  }
  line  = line_for(&d);
  ainit = (uint32_t)(d.range + 32) / 64u;
  ainit = ainit < 2u ? 2u : ainit;
  for (i = 0; i < L_CTX; i++) {
    d.k[i].a = (int32_t)ainit;
    d.k[i].b = 0;
    d.k[i].c = 0;
    d.k[i].n = 1u;
  }
  d.ra[0] = d.ra[1] = (int32_t)ainit;
  d.rn[0] = d.rn[1] = 1u;
  d.rnn[0] = d.rnn[1] = 0u;
  for (c = 0; c < L_SCOMPS; c++) d.run[c] = 0u;
  d.src.p   = s->seg[it->iv].p;
  d.src.end = s->seg[it->iv].end;
  d.src.ff  = false;
  d.buf     = 0u;
  d.n       = 0u;

  /* each component's two lines, zero: the first's neighbours */
  {
    uint16_t *at = lines;

    for (c = 0; c < s->n; c++) {
      size_t len = (size_t)j->comp[s->comp[c]].w + 2u;

      memset(at, 0, len * 2u * sizeof(uint16_t));
      pr[c] = at + 1;
      cu[c] = at + len + 1;
      at += len * 2u;
    }
  }

  for (m = m0; m < m1; m++) {
    if (s->ilv == 0u) {
      const lcomp_t *k = &j->comp[s->comp[0]];
      uint16_t      *t;

      if (!line(&d, pr[0], cu[0], k->w, &d.run[0])) return false;
      store(j, s->comp[0], m, cu[0]);
      t = pr[0], pr[0] = cu[0], cu[0] = t;
    } else if (s->ilv == 1u && j->vmax == 1u && !j->planes) {
      bool all;

      /* a line each: the row out at once */
      for (c = 0; c < s->n; c++) {
        if (!line(&d, pr[c], cu[c], j->comp[s->comp[c]].w, &d.run[c])) return false;
      }
      all = store_all(j, s, m, cu);
      for (c = 0; c < s->n; c++) {
        uint16_t *t;

        if (!all) store(j, s->comp[c], m, cu[c]);
        t = pr[c], pr[c] = cu[c], cu[c] = t;
      }
    } else if (s->ilv == 1u) {
      for (c = 0; c < s->n; c++) {
        const lcomp_t *k = &j->comp[s->comp[c]];
        uint32_t       v;

        for (v = 0; v < k->v; v++) {
          uint32_t  y = m * k->v + v;
          uint16_t *t;

          if (!line(&d, pr[c], cu[c], k->w, &d.run[c])) return false;
          if (y < k->ht) store(j, s->comp[c], y, cu[c]);
          t = pr[c], pr[c] = cu[c], cu[c] = t;
        }
      }
    } else {
      bool all;

      if (!line_px(&d, pr, cu, s->n, j->comp[s->comp[0]].w)) return false;
      all = store_all(j, s, m, cu);
      for (c = 0; c < s->n; c++) {
        uint16_t *t;

        if (!all) store(j, s->comp[c], m, cu[c]);
        t = pr[c], pr[c] = cu[c], cu[c] = t;
      }
    }
  }
  return true;
}

/* -- the planes to the pixels (subsampled, or HP's transform) ---------------------- */

typedef struct lout {
  ljls_t   *j;
  uint32_t *col[L_COMPS]; /* a subsampled component's column for each out one */
  uint32_t  band;
} lout_t;

static bool
out_band(void *arg, uint32_t worker, uint32_t item) {
  lout_t   *o = arg;
  ljls_t   *j = o->j;
  uint32_t  y0 = item * o->band, y1 = y0 + o->band, y, c, x, w = j->reg_w;
  uint16_t *tmp = j->lines[worker];

  if (y1 > j->reg_h) y1 = j->reg_h;
  for (y = y0; y < y1; y++) {
    uint8_t *row = j->px + (size_t)y * j->pitch;

    for (c = 0; c < j->ncomp; c++) {
      const lcomp_t *k = &j->comp[c];
      uint32_t       sy = (uint32_t)(((uint64_t)(j->reg_y + y) * k->v) / j->vmax) - k->y0;
      uint16_t      *t = tmp + (size_t)w * c;
      const uint8_t *src = k->plane + (size_t)sy * k->pstride;

      if (o->col[c]) {
        if (k->pes == 1u) for (x = 0; x < w; x++) t[x] = src[o->col[c][x]];
        else for (x = 0; x < w; x++) t[x] = ((const uint16_t *)(const void *)src)[o->col[c][x]];
      } else if (k->pes == 1u) {
        for (x = 0; x < w; x++) t[x] = src[x];
      } else {
        memcpy(t, src, (size_t)w * 2u);
      }
    }
    if (j->xform) {
      /* HP's inverses (CharLS's color_transform.hpp), modulo 2^P */
      uint16_t *v1 = tmp, *v2 = tmp + w, *v3 = tmp + 2u * (size_t)w;
      int32_t   range = 1 << j->prec, bias = range >> 1, mask = range - 1;

      for (x = 0; x < w; x++) {
        int32_t a = v1[x], b = v2[x], cc = v3[x], r, g, bl;

        if (j->xform == 1u) {
          r  = (a + b - bias) & mask;
          g  = b;
          bl = (cc + b - bias) & mask;
        } else if (j->xform == 2u) {
          r  = (a + b - bias) & mask;
          g  = b;
          bl = (cc + ((r + b) >> 1) - bias) & mask;
        } else {
          int32_t gg = a - ((cc + b) >> 2) + range / 4;

          r  = (cc + gg - bias) & mask;
          g  = gg & mask;
          bl = (b + gg - bias) & mask;
        }
        v1[x] = (uint16_t)r;
        v2[x] = (uint16_t)g;
        v3[x] = (uint16_t)bl;
      }
    }
    for (c = 0; c < j->ncomp; c++) put(j, &j->comp[c], tmp + (size_t)w * c, w, row + j->comp[c].off);
    if (j->pad) put_pad(j, w, row);
  }
  return true;
}

/* -- the stream ------------------------------------------------------------------- */

/* the marker at or after `*pp` (fill bytes and anything else before it
   passed over), its code; EOI at the data's end */
static uint32_t
marker_at(const uint8_t **pp, const uint8_t *end) {
  const uint8_t *p = *pp;

  for (;;) {
    while (p < end && *p != 0xFFu) p++;
    while (p + 1 < end && p[1] == 0xFFu) p++;
    if (p + 1 >= end) {
      *pp = end;
      return 0xD9u;
    }
    if (p[1] != 0x00u) break;
    p += 2;
  }
  *pp = p + 2;
  return p[1];
}

/* a scan's data from `p`: its restart intervals' segments, to the marker
   that ends it (returned: its 0xFF; the data's end where none comes) */
static const uint8_t*
scan_data(lscan_t *s, const uint8_t *p, const uint8_t *end) {
  const uint8_t *start = p;

  for (;;) {
    const uint8_t *f = p < end ? memchr(p, 0xFF, (size_t)(end - p)) : NULL, *m;

    if (!f || f + 1 >= end) {
      f = end;
      m = end;
    } else if (f[1] < 0x80u) {
      p = f + 2; /* data: its next byte's top bit the stuffed 0 */
      continue;
    } else {
      m = f;
      while (m + 1 < end && m[1] == 0xFFu) m++; /* fill bytes */
      if (m + 1 >= end) {
        m = end;
      }
    }
    if (s->nseg == s->segcap) {
      uint32_t cap = s->segcap ? s->segcap * 2u : 8u;
      lseg_t  *g = realloc(s->seg, cap * sizeof(*g));

      if (!g) return NULL;
      s->seg    = g;
      s->segcap = cap;
    }
    s->seg[s->nseg].p   = start;
    s->seg[s->nseg].end = f;
    s->nseg++;
    if (m < end && m[1] >= 0xD0u && m[1] <= 0xD7u) {
      start = p = m + 2; /* RSTn: the next interval */
      continue;
    }
    return f;
  }
}

/* the default thresholds (C.2.4.1.1.1) */
static void
thresholds(int32_t maxval, int32_t near, int32_t *t1, int32_t *t2, int32_t *t3) {
#define L_CLAMP(i, lo) ((i) > maxval || (i) < (lo) ? (lo) : (i))
  if (maxval >= 128) {
    int32_t f = ((maxval < 4095 ? maxval : 4095) + 128) / 256;

    *t1 = L_CLAMP(f * (3 - 2) + 2 + 3 * near, near + 1);
    *t2 = L_CLAMP(f * (7 - 3) + 3 + 5 * near, *t1);
    *t3 = L_CLAMP(f * (21 - 4) + 4 + 7 * near, *t2);
  } else {
    int32_t f = 256 / (maxval + 1), v;

    v   = 3 / f + 3 * near;
    *t1 = L_CLAMP(v < 2 ? 2 : v, near + 1);
    v   = 7 / f + 5 * near;
    *t2 = L_CLAMP(v < 3 ? 3 : v, *t1);
    v   = 21 / f + 7 * near;
    *t3 = L_CLAMP(v < 4 ? 4 : v, *t2);
  }
#undef L_CLAMP
}

static bool
read_sof(ljls_t *j, const uint8_t *s, uint32_t len) {
  uint32_t n, i, k;

  if (j->frame || len < 6u) return false;
  j->prec   = s[0];
  j->height = lbe16(s + 1);
  j->width  = lbe16(s + 3);
  n         = s[5];
  if (j->prec < 2u || j->prec > 16u || !n || len != 6u + 3u * n) return false;
  j->ncomp = n;
  j->hmax = j->vmax = 1u;
  for (i = 0; i < n; i++) {
    lcomp_t *c = &j->comp[i];

    c->id = s[6u + 3u * i];
    c->h  = s[7u + 3u * i] >> 4;
    c->v  = s[7u + 3u * i] & 15u;
    if (c->h < 1u || c->h > 4u || c->v < 1u || c->v > 4u) return false;
    for (k = 0; k < i; k++) {
      if (j->comp[k].id == c->id) return false;
    }
    if (c->h > j->hmax) j->hmax = c->h;
    if (c->v > j->vmax) j->vmax = c->v;
  }
  j->frame = true;
  return true;
}

static bool
read_lse(ljls_t *j, const uint8_t *s, uint32_t len) {
  if (len < 1u) return false;
  switch (s[0]) {
  case 1: /* the preset coding parameters */
    if (len != 11u) return false;
    j->maxval = (int32_t)lbe16(s + 1);
    j->t1     = (int32_t)lbe16(s + 3);
    j->t2     = (int32_t)lbe16(s + 5);
    j->t3     = (int32_t)lbe16(s + 7);
    j->reset  = (int32_t)lbe16(s + 9);
    return true;
  case 2: { /* a mapping table */
    ltab_t  *t;
    uint32_t id, wt;

    if (len < 3u) return false;
    id = s[1];
    wt = s[2];
    if (!id || !wt || !(t = calloc(1, sizeof(*t)))) return false;
    t->next = j->tabs;
    j->tabs = t;
    t->wt   = wt;
    t->n    = (len - 3u) / wt;
    if (t->n * wt != len - 3u) return false;
    if (t->n && !(t->e = malloc((size_t)t->n * wt))) return false;
    if (t->n) memcpy(t->e, s + 3, (size_t)t->n * wt);
    j->tab[id] = t;
    j->open    = t;
    return true;
  }
  case 3: { /* its continuation */
    ltab_t  *t = j->open;
    uint32_t more;
    uint8_t *e;

    if (len < 3u || !t || j->tab[s[1]] != t || s[2] != t->wt || (len - 3u) % t->wt) return false;
    more = (len - 3u) / t->wt;
    if (!more) return true;
    if (!(e = realloc(t->e, ((size_t)t->n + more) * t->wt))) return false;
    memcpy(e + (size_t)t->n * t->wt, s + 3, (size_t)more * t->wt);
    t->e = e;
    t->n += more;
    return true;
  }
  case 4: { /* oversize dimensions */
    uint32_t wxy;

    if (len < 2u) return false;
    wxy = s[1];
    if (wxy < 2u || wxy > 4u || len != 2u + 2u * wxy) return false;
    j->ye = lbe(s + 2, wxy);
    j->xe = lbe(s + 2 + wxy, wxy);
    return true;
  }
  default:
    return false;
  }
}

static void
read_app(ljls_t *j, uint32_t m, const uint8_t *s, uint32_t len) {
  if (m == 0xE8u && len >= 5u && !memcmp(s, "mrfx", 4)) {
    j->xform = s[4] ? s[4] : 0u;
    if (s[4] > 3u) j->xform = 0xFFu; /* refused where a scan comes */
  } else if (m == 0xE8u && len >= 30u && !memcmp(s, "SPIFF", 6)) {
    j->spiff      = true;
    j->spiff_cs   = s[18];
    j->spiff_unit = s[21];
    j->spiff_vres = lbe(s + 22, 4);
    j->spiff_hres = lbe(s + 26, 4);
  } else if (m == 0xEEu && len >= 12u && !memcmp(s, "Adobe", 5)) {
    j->adobe_ycc = s[11] == 1u;
  } else if (m == 0xE2u && len >= 14u && !memcmp(s, "ICC_PROFILE", 12)) {
    uint32_t seq = s[12], count = s[13];

    if (j->icc_bad) return;
    if (!j->icc_count) j->icc_count = count;
    if (count != j->icc_count || !seq || seq > count || j->icc_part[seq]) {
      j->icc_bad = true;
      return;
    }
    j->icc_part[seq]     = s + 14;
    j->icc_part_len[seq] = len - 14u;
  }
}

/* SOS (C.2.3): its components (in the frame, once each), the parameters
   in force (C.2.4.1.1's checks), NEAR, ILV, the point transform */
static bool
read_sos(ljls_t *j, const uint8_t *s, uint32_t len) {
  lscan_t *sc;
  uint32_t n, i, k;
  int32_t  maxval, near;

  if (!j->frame || len < 1u) return false;
  n = s[0];
  if (n < 1u || n > L_SCOMPS || len != 4u + 2u * n) return false;
  if (j->nscan == j->scancap) {
    uint32_t cap = j->scancap ? j->scancap * 2u : 4u;
    lscan_t *g = realloc(j->scan, cap * sizeof(*g));

    if (!g) return false;
    j->scan    = g;
    j->scancap = cap;
  }
  sc = &j->scan[j->nscan];
  memset(sc, 0, sizeof(*sc));
  j->nscan++;
  sc->n = n;
  for (i = 0; i < n; i++) {
    lcomp_t *c;

    for (k = 0; k < j->ncomp && j->comp[k].id != s[1u + 2u * i]; k++) {
    }
    if (k == j->ncomp || j->comp[k].coded) return false;
    c        = &j->comp[k];
    c->coded = true;
    c->scan  = j->nscan - 1u;
    c->tm    = s[2u + 2u * i];
    c->tab   = c->tm ? j->tab[c->tm] : NULL;
    if (c->tm && !c->tab) return false;
    sc->comp[i] = k;
  }
  sc->near = s[1u + 2u * n];
  sc->ilv  = s[2u + 2u * n];
  sc->pt   = s[3u + 2u * n] & 15u;
  if (s[3u + 2u * n] >> 4 || sc->pt >= j->prec || sc->ilv > 2u || (sc->ilv == 0u && n != 1u)) return false;
  if (n == 1u) sc->ilv = 0u; /* one component: by lines or samples alike */
  for (i = 0; i < n; i++) {
    if (sc->pt && j->comp[sc->comp[i]].tm) return false;
  }

  /* the parameters (C.2.4.1.1): 0 their defaults */
  maxval = j->maxval ? j->maxval : (int32_t)((1u << j->prec) - 1u);
  near   = (int32_t)sc->near;
  if (maxval < 1 || maxval > (int32_t)((1u << j->prec) - 1u)) return false;
  if (near > (maxval / 2 < 255 ? maxval / 2 : 255)) return false;
  thresholds(maxval, near, &sc->t1, &sc->t2, &sc->t3);
  if (j->t1) sc->t1 = j->t1;
  if (j->t2) sc->t2 = j->t2;
  if (j->t3) sc->t3 = j->t3;
  if (sc->t1 < near + 1 || sc->t1 > maxval || sc->t2 < sc->t1 || sc->t2 > maxval || sc->t3 < sc->t2 || sc->t3 > maxval) {
    return false;
  }
  sc->reset = j->reset ? j->reset : 64;
  if (sc->reset < 3 || sc->reset > (maxval > 255 ? maxval : 255)) return false;
  sc->maxval = maxval;
  sc->ri     = j->ri;
  return true;
}

/* the frame's size from LSE or DNL where its header gave none; each
   component's (4.3.1) */
static bool
frame_size(ljls_t *j) {
  uint32_t i;

  if (!j->width) j->width = j->xe;
  if (!j->height) j->height = j->ye;
  if (!j->width || !j->height) return false;
  for (i = 0; i < j->ncomp; i++) {
    lcomp_t *c = &j->comp[i];

    c->w  = (uint32_t)(((uint64_t)j->width * c->h + j->hmax - 1u) / j->hmax);
    c->ht = (uint32_t)(((uint64_t)j->height * c->v + j->vmax - 1u) / j->vmax);
    if ((uint64_t)c->w * c->ht > J_SAMPLES || c->w > (1u << 30)) return false;
  }
  return true;
}

/* through the stream: its markers, each scan's data; `head`: to the first
   scan's alone where the size is known by then */
static bool
parse(ljls_t *j, bool head) {
  const uint8_t *p = j->data, *end = j->end;

  if (end - p < 4 || p[0] != 0xFFu || p[1] != 0xD8u) return false;
  p += 2;
  for (;;) {
    uint32_t       m = marker_at(&p, end), len;
    const uint8_t *s;

    if (m == 0xD9u) {
      j->used = (size_t)(p - j->data);
      break;
    }
    if ((m >= 0xD0u && m <= 0xD7u) || m == 0x01u || m == 0xD8u) continue;
    if (end - p < 2) return false;
    len = lbe16(p);
    if (len < 2u || (size_t)(end - p) < len) return false; /* cut inside a segment */
    s = p + 2;
    p += len;
    len -= 2u;
    switch (m) {
    case 0xF7:
      if (!read_sof(j, s, len)) return false;
      break;
    case 0xF8:
      if (!read_lse(j, s, len)) return false;
      break;
    case 0xDD:
      if (len < 2u || len > 4u) return false;
      j->ri = lbe(s, len);
      break;
    case 0xDC: /* DNL: the first scan's height where the frame gave none */
      if (len < 2u || len > 4u) return false;
      if (j->dnl && !j->height) j->height = lbe(s, len);
      break;
    case 0xDA:
      if (!read_sos(j, s, len)) return false;
      if (j->xform == 0xFFu) return false;
      j->dnl = j->nscan == 1u;
      if (head && (j->width || j->xe) && (j->height || j->ye)) return true;
      p = scan_data(&j->scan[j->nscan - 1u], p, end);
      if (!p) return false;
      break;
    case 0xFE:
      break;
    default:
      /* other frames, tables of other processes: not JPEG-LS's */
      if ((m >= 0xC0u && m <= 0xCFu) || m == 0xDBu || m == 0xDEu || m == 0xDFu) return false;
      if (m >= 0xE0u && m <= 0xEFu) read_app(j, m, s, len);
      break;
    }
    if (m != 0xDAu && m != 0xDCu) j->dnl = false;
    if (head && j->nscan && (j->width || j->xe) && (j->height || j->ye)) return true;
  }
  return j->frame && j->nscan;
}

/* -- the image -------------------------------------------------------------------- */

/* each component's out kind, its table; the pixels' layout */
static bool
plan_out(ljls_t *j) {
  uint32_t i, n = j->ncomp, pmax = (1u << j->prec) - 1u, oes = 0u;
  bool     rgb = false;

  for (i = 0; i < n; i++) {
    lcomp_t       *c = &j->comp[i];
    const lscan_t *s = &j->scan[c->scan];
    uint32_t       es, v, mx = (uint32_t)s->maxval;

    if (c->tab) {
      const ltab_t *t = c->tab;

      /* an entry for each sample value (C.2.4.1.2) */
      if (t->n < mx + 1u) return false;
      if (t->wt == 3u && n == 1u) {
        c->how = L_RGB;
        c->lut = t->e;
        rgb    = true;
        es     = 1u;
      } else if (t->wt == 1u) {
        uint8_t *l = malloc(mx + 1u);

        if (!(c->lut = l)) return false;
        for (v = 0; v <= mx; v++) l[v] = t->e[v];
        c->how = L_LUT8;
        es     = 1u;
      } else if (t->wt == 2u) {
        uint16_t *l = malloc((mx + 1u) * 2u);

        /* an entry a 16-bit value, MSB first; out in the order asked */
        if (!(c->lut = l)) return false;
        for (v = 0; v <= mx; v++) {
          uint16_t e = (uint16_t)lbe16(t->e + 2u * v);

          l[v] = j->swap ? bswapu16(e) : e;
        }
        c->how = L_LUT16;
        es     = 2u;
      } else {
        return false; /* entries of other widths: no meaning given */
      }
    } else {
      bool identity = !s->pt && (j->prec == 8u || j->prec == 16u);

      es = j->prec > 8u ? 2u : 1u;
      if (identity) {
        c->how = es == 1u ? L_COPY8 : j->swap ? L_SWAP16 : L_COPY16;
      } else {
        uint32_t top = es == 1u ? 255u : 65535u;

        if (!(c->lut = malloc((mx + 1u) * es))) return false;
        for (v = 0; v <= mx; v++) {
          uint32_t x = v << s->pt;

          x = x > pmax ? pmax : x;
          x = pmax == top ? x : (uint32_t)(((uint64_t)x * top + pmax / 2u) / pmax);
          if (es == 1u) ((uint8_t *)c->lut)[v] = (uint8_t)x;
          else ((uint16_t *)c->lut)[v] = j->swap ? bswapu16((uint16_t)x) : (uint16_t)x;
        }
        c->how = es == 1u ? L_LUT8 : L_LUT16;
      }
    }
    if (oes && es != oes) return false; /* samples out of one size */
    oes = es;
  }
  j->oes = oes;
  if (rgb) {
    j->on     = j->rgb_pad ? 4u : 3u;
    j->format = j->rgb_pad ? IM_FORMAT_RGB0 : IM_FORMAT_RGB;
    j->space  = IM_COLORSPACE_sRGB;
    j->pad    = j->rgb_pad;
  } else if (n == 1u) {
    j->on     = 1u;
    j->format = IM_FORMAT_GRAY;
    j->space  = IM_COLORSPACE_GRAY;
  } else if (n == 3u) {
    bool ycc = !j->xform && (j->adobe_ycc || (j->spiff && (j->spiff_cs == 1u || j->spiff_cs == 3u || j->spiff_cs == 4u)));

    j->pad    = j->rgb_pad && !ycc;
    j->on     = j->pad ? 4u : 3u;
    j->format = ycc ? IM_FORMAT_YCbCr : j->pad ? IM_FORMAT_RGB0 : IM_FORMAT_RGB;
    j->space  = ycc ? IM_COLORSPACE_UNKNOWN : IM_COLORSPACE_sRGB;
  } else if (n == 4u && j->spiff && j->spiff_cs == 12u) {
    j->on     = 4u;
    j->format = IM_FORMAT_CMYK;
    j->space  = IM_COLORSPACE_CMYK;
  } else {
    j->on     = n;
    j->format = IM_FORMAT_NONE; /* the components as they are */
    j->space  = IM_COLORSPACE_UNKNOWN;
  }
  for (i = 0; i < n; i++) j->comp[i].off = (size_t)i * j->oes;
  return true;
}

/* the scans' sizes, the region's rows and MCUs, the gradients' tables */
static bool
plan_scans(ljls_t *j) {
  uint32_t i, c;

  for (i = 0; i < j->nscan; i++) {
    lscan_t *s = &j->scan[i];
    int32_t  d, mx = s->maxval;

    for (c = 1; c < s->n; c++) {
      const lcomp_t *a = &j->comp[s->comp[0]], *b = &j->comp[s->comp[c]];

      if (s->ilv == 2u && (a->w != b->w || a->ht != b->ht)) return false; /* B.3.1 */
    }
    if (s->ilv == 1u) {
      uint32_t nm = 0u, y1 = 0u;

      for (c = 0; c < s->n; c++) {
        const lcomp_t *k = &j->comp[s->comp[c]];
        uint32_t       a = (k->ht + k->v - 1u) / k->v, b = (k->y1 + k->v - 1u) / k->v;

        nm = a > nm ? a : nm;
        y1 = b > y1 ? b : y1;
      }
      s->nmcu = nm;
      s->mcu1 = y1;
    } else {
      s->nmcu = j->comp[s->comp[0]].ht;
      s->mcu1 = j->comp[s->comp[0]].y1;
    }
    /* the intervals the region's rows need: from the one holding its first */
    if (s->ilv == 1u) {
      uint32_t y0 = ~0u;

      for (c = 0; c < s->n; c++) {
        const lcomp_t *k = &j->comp[s->comp[c]];

        y0 = k->y0 / k->v < y0 ? k->y0 / k->v : y0;
      }
      s->mcu0 = y0;
    } else {
      s->mcu0 = j->comp[s->comp[0]].y0;
    }
    if (!(s->q = malloc((size_t)mx * 2u + 1u))) return false;
    for (d = -mx; d <= mx; d++) {
      int32_t near = (int32_t)s->near, r;

      r = d <= -s->t3 ? -4 : d <= -s->t2 ? -3 : d <= -s->t1 ? -2 : d < -near ? -1 : d <= near ? 0
        : d < s->t1 ? 1 : d < s->t2 ? 2 : d < s->t3 ? 3 : 4;
      s->q[d + mx] = (int8_t)r;
    }
  }
  return true;
}

static void
jls_free(ljls_t *j) {
  uint32_t i;
  ltab_t  *t, *nx;

  for (i = 0; i < j->nscan; i++) {
    free(j->scan[i].seg);
    free(j->scan[i].q);
  }
  free(j->scan);
  for (t = j->tabs; t; t = nx) {
    nx = t->next;
    free(t->e);
    free(t);
  }
  for (i = 0; i < j->ncomp; i++) {
    if (j->comp[i].how != L_RGB) free(j->comp[i].lut);
    free(j->comp[i].plane);
  }
  for (i = 0; i < IM_POOL_MAX; i++) free(j->lines[i]);
  free(j->item);
  if (j->own) free(j->px);
}

static bool
decode(ljls_t *j, ImImage *im) {
  uint32_t i, c, nw;
  size_t   lines = 0u, bytes;
  uint64_t px, mem = 0u;

  if (!parse(j, false) || !frame_size(j)) return false;
  for (i = 0; i < j->ncomp; i++) {
    if (!j->comp[i].coded) return false; /* a component no scan has */
  }
  for (i = 0; i < j->nscan; i++) {
    const lscan_t *s = &j->scan[i];

    /* HP's transforms: three components, 8 or 16 bits, lossless,
       interleaved (CharLS's) */
    if (j->xform && (j->ncomp != 3u || (j->prec != 8u && j->prec != 16u) || s->near || s->ilv == 0u)) return false;
  }

  /* the region, cut to the image; each component's part of it */
  if (!j->region) {
    j->reg_x = j->reg_y = 0u;
    j->reg_w = j->width;
    j->reg_h = j->height;
  } else {
    if (j->reg_x >= j->width || j->reg_y >= j->height || !j->reg_w || !j->reg_h) return false;
    if (j->reg_w > j->width - j->reg_x) j->reg_w = j->width - j->reg_x;
    if (j->reg_h > j->height - j->reg_y) j->reg_h = j->height - j->reg_y;
  }
  j->planes = j->xform != 0u;
  for (i = 0; i < j->ncomp; i++) {
    lcomp_t *k = &j->comp[i];

    if (k->h != j->hmax || k->v != j->vmax) j->planes = true;
    k->x0 = (uint32_t)(((uint64_t)j->reg_x * k->h) / j->hmax);
    k->x1 = (uint32_t)(((uint64_t)(j->reg_x + j->reg_w - 1u) * k->h) / j->hmax) + 1u;
    k->y0 = (uint32_t)(((uint64_t)j->reg_y * k->v) / j->vmax);
    k->y1 = (uint32_t)(((uint64_t)(j->reg_y + j->reg_h - 1u) * k->v) / j->vmax) + 1u;
  }
  if (j->xform && j->comp[0].tab) return false;
  if (!plan_out(j) || !plan_scans(j)) return false;

  /* the pixels' memory: the caller's, enough of it, or made */
  bytes    = (size_t)j->reg_w * j->on * j->oes;
  j->pitch = bytes;
  if (j->dest) {
    if (j->dest_stride < bytes || j->dest_size < j->dest_stride * (j->reg_h - 1u) + bytes
        || (j->oes == 2u && (((uintptr_t)j->dest | j->dest_stride) & 1u))) {
      return false;
    }
    j->px    = j->dest;
    j->pitch = j->dest_stride;
  } else {
    mem += (uint64_t)bytes * j->reg_h;
  }
  for (i = 0; i < j->ncomp; i++) {
    lcomp_t *k = &j->comp[i];

    k->pes     = j->scan[k->scan].maxval > 255 ? 2u : 1u;
    k->pstride = (size_t)(k->x1 - k->x0) * k->pes;
    if (j->planes) mem += (uint64_t)k->pstride * (k->y1 - k->y0);
  }
  /* each worker's lines: a scan's components' two each (or the out rows) */
  for (i = 0; i < j->nscan; i++) {
    size_t l = 0u;

    for (c = 0; c < j->scan[i].n; c++) l += ((size_t)j->comp[j->scan[i].comp[c]].w + 2u) * 2u;
    lines = l > lines ? l : lines;
  }
  if (j->planes && (size_t)j->reg_w * j->ncomp > lines) lines = (size_t)j->reg_w * j->ncomp;
  j->lines_len = lines;

  /* the work: each scan's intervals the region needs */
  for (i = 0; i < j->nscan; i++) {
    lscan_t *s = &j->scan[i];
    uint32_t per = s->ri ? s->ri : s->nmcu, a = s->mcu0 / per, b = (s->mcu1 + per - 1u) / per;

    j->nitem += b > a ? b - a : 0u;
  }
  if (!(j->item = malloc((size_t)(j->nitem ? j->nitem : 1u) * sizeof(*j->item)))) return false;
  j->nitem = 0u;
  for (i = 0; i < j->nscan; i++) {
    lscan_t *s = &j->scan[i];
    uint32_t per = s->ri ? s->ri : s->nmcu, a = s->mcu0 / per, b = (s->mcu1 + per - 1u) / per, v;

    for (v = a; v < b; v++) {
      j->item[j->nitem].scan = i;
      j->item[j->nitem].iv   = v;
      j->nitem++;
    }
  }

  /* threads: one for each J_POOL_MIN pixels, no more than the items */
  px = (uint64_t)j->width * j->height * j->ncomp;
  {
    uint64_t n = px / J_POOL_MIN;

    if (n > j->nitem) n = j->nitem;
    if (j->runner.run) {
      if (n > 1u) j->pool = im_pool_wrap(&j->runner);
    } else {
      if (n > j->threads) n = j->threads;
      if (n > 1u) j->pool = im_pool_new((uint32_t)n);
    }
  }
  nw = im_pool_size(j->pool);
  mem += (uint64_t)lines * sizeof(uint16_t) * nw;
  if (j->mem_limit && mem > j->mem_limit) {
    j->over = true;
    return false;
  }
  for (i = 0; i < nw; i++) {
    if (!(j->lines[i] = malloc(lines * sizeof(uint16_t)))) return false;
  }
  if (!j->dest) {
    if (!(j->px = malloc((size_t)bytes * j->reg_h))) return false;
    j->own = true;
  }
  if (j->planes) {
    for (i = 0; i < j->ncomp; i++) {
      lcomp_t *k = &j->comp[i];

      if (!(k->plane = malloc(k->pstride * (k->y1 - k->y0)))) return false;
    }
  }
  if (!im_pool_run(j->pool, interval, j, j->nitem)) return false;

  if (j->planes) {
    lout_t o;
    bool   ok = true;

    memset(&o, 0, sizeof(o));
    o.j = j;
    for (c = 0; c < j->ncomp && ok; c++) {
      const lcomp_t *k = &j->comp[c];
      uint32_t       x;

      if (k->h == j->hmax) continue;
      if (!(o.col[c] = malloc((size_t)j->reg_w * sizeof(uint32_t)))) {
        ok = false;
        break;
      }
      for (x = 0; x < j->reg_w; x++) o.col[c][x] = (uint32_t)(((uint64_t)(j->reg_x + x) * k->h) / j->hmax) - k->x0;
    }
    o.band = j->pool ? (j->reg_h + nw * 4u - 1u) / (nw * 4u) : j->reg_h;
    o.band = o.band ? o.band : 1u;
    ok     = ok && im_pool_run(j->pool, out_band, &o, (j->reg_h + o.band - 1u) / o.band);
    for (c = 0; c < j->ncomp; c++) free(o.col[c]);
    if (!ok) return false;
  }

  im->width              = j->reg_w;
  im->height             = j->reg_h;
  im->data.data          = j->own ? j->px : NULL;
  im->len                = j->own ? bytes * j->reg_h : j->pitch * (j->reg_h - 1u) + bytes;
  im->row_pad_last       = j->own ? 0u : (uint32_t)(j->pitch - bytes);
  im->componentsPerPixel = j->on;
  im->bitsPerComponent   = j->oes * 8u;
  im->bitsPerPixel       = j->on * im->bitsPerComponent;
  im->bytesPerPixel      = im->bitsPerPixel / 8u;
  im->byteOrder          = j->order;
  im->alphaInfo          = j->pad ? IM_ALPHA_NONE_SKIP_LAST : IM_ALPHA_NONE;
  im->format             = j->format;
  im->colorSpace         = j->space;
  j->own                 = false; /* im's now */

  /* the profile; SPIFF's resolution (pixels a metre) */
  if (!j->icc_bad && j->icc_count) {
    uint32_t k;
    size_t   total = 0u, at = 0u;

    for (k = 1; k <= j->icc_count && j->icc_part[k]; k++) total += j->icc_part_len[k];
    if (k > j->icc_count && total && (im->iccProfile = malloc(total))) {
      for (k = 1; k <= j->icc_count; k++) {
        memcpy(im->iccProfile + at, j->icc_part[k], j->icc_part_len[k]);
        at += j->icc_part_len[k];
      }
      im->iccProfileSize = total;
    }
  }
  if (j->spiff && (j->spiff_unit == 1u || j->spiff_unit == 2u) && j->spiff_vres && j->spiff_hres
      && (im->physicalDim = calloc(1, sizeof(*im->physicalDim)))) {
    double per = j->spiff_unit == 1u ? 100.0 / 2.54 : 100.0;

    im->physicalDim->pixelsPerUnitX = (uint32_t)(j->spiff_hres * per + 0.5);
    im->physicalDim->pixelsPerUnitY = (uint32_t)(j->spiff_vres * per + 0.5);
    im->physicalDim->unit           = 1u;
  }
  return true;
}

/* -- entry points ------------------------------------------------------------------ */

/* whether `data`'s first frame is JPEG-LS's (SOF55) */
IM_HIDE bool
jls_is(const uint8_t *data, size_t len) {
  const uint8_t *p = data, *end = data + len;

  if (len < 4u || p[0] != 0xFFu || p[1] != 0xD8u) return false;
  p += 2;
  for (;;) {
    uint32_t m = marker_at(&p, end), l;

    if (m == 0xF7u) return true;
    if (m == 0xD9u || m == 0xDAu || m == 0xDEu || (m >= 0xC0u && m <= 0xCFu && m != 0xC4u && m != 0xC8u && m != 0xCCu)) {
      return false;
    }
    if ((m >= 0xD0u && m <= 0xD7u) || m == 0x01u || m == 0xD8u) continue;
    if (end - p < 2) return false;
    l = lbe16(p);
    if (l < 2u || (size_t)(end - p) < l) return false;
    p += l;
  }
}

IM_HIDE bool
jls_info(const uint8_t *data, size_t len, ImInfo *info) {
  ljls_t *j;
  bool    ok;

  memset(info, 0, sizeof(*info));
  if (!(j = calloc(1, sizeof(*j)))) return false;
  j->data = data;
  j->end  = data + len;
  ok      = parse(j, true) && frame_size(j);
  if (ok) {
    info->fileFormatType   = IM_FILEFORMATTYPE_JLS;
    info->coding           = IM_CODING_JPEGLS;
    info->width            = j->width;
    info->height           = j->height;
    info->components       = j->ncomp;
    info->bitsPerComponent = j->prec;
    info->pages            = 1u;
  }
  jls_free(j);
  free(j);
  return ok;
}

IM_HIDE bool
jls_decode_mem(const uint8_t *data, size_t len, ImImage *im, const jopts_t *o) {
  ljls_t     *j;
  bool        ok;
  ImByteOrder order = o->order;

  if (!(j = calloc(1, sizeof(*j)))) return false;
  if (order != IM_BYTEORDER_BIG && order != IM_BYTEORDER_LITTLE) order = IM_BYTEORDER_HOST;
  j->data        = data;
  j->end         = data + len;
  j->order       = order;
  j->swap        = order != IM_BYTEORDER_HOST && (order == IM_BYTEORDER_BIG) != L_HOST_BIG;
  j->rgb_pad     = o->rgb_pad;
  j->threads     = o->threads ? o->threads : IM_POOL_MAX;
  j->runner      = o->runner;
  j->dest        = o->dest;
  j->dest_stride = o->dest_stride;
  j->dest_size   = o->dest_size;
  j->mem_limit   = o->mem_limit;
  j->region      = o->region;
  j->reg_x       = o->rx;
  j->reg_y       = o->ry;
  j->reg_w       = o->rw;
  j->reg_h       = o->rh;
  ok             = decode(j, im);
  if (o->over) *o->over = j->over;
  if (ok && o->used) *o->used = j->used ? j->used : len;
  im_pool_free(j->pool);
  jls_free(j);
  free(j);
  return ok;
}
