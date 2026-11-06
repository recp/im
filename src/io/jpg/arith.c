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
 * Arithmetic-coded scans (Annex D, F.2.4, G.2, H.1.2.3): the QM coder,
 * the statistics a table's, its conditioning (DAC), sequential and
 * progressive blocks, lossless differences, restarts. As libjpeg's
 * jdarith.c: past a segment's end zeros; a magnitude or a run past the
 * block (damage) and the MCUs after are left as they are, up to the next
 * restart; a DC refinement read regardless.
 */

#include "jpgint.h"

/* Table D.2: Qe, the next state after an LPS (with the MPS switch, bit 7)
   and after an MPS, packed as libjpeg's jpeg_aritab */
#define V(a, b, c, d) (((uint32_t)(a) << 16) | ((uint32_t)(c) << 8) | ((uint32_t)(d) << 7) | (uint32_t)(b))

static const uint32_t aritab[114] = {
  V(0x5a1d, 1, 1, 1),
  V(0x2586, 14, 2, 0),
  V(0x1114, 16, 3, 0),
  V(0x080b, 18, 4, 0),
  V(0x03d8, 20, 5, 0),
  V(0x01da, 23, 6, 0),
  V(0x00e5, 25, 7, 0),
  V(0x006f, 28, 8, 0),
  V(0x0036, 30, 9, 0),
  V(0x001a, 33, 10, 0),
  V(0x000d, 35, 11, 0),
  V(0x0006, 9, 12, 0),
  V(0x0003, 10, 13, 0),
  V(0x0001, 12, 13, 0),
  V(0x5a7f, 15, 15, 1),
  V(0x3f25, 36, 16, 0),
  V(0x2cf2, 38, 17, 0),
  V(0x207c, 39, 18, 0),
  V(0x17b9, 40, 19, 0),
  V(0x1182, 42, 20, 0),
  V(0x0cef, 43, 21, 0),
  V(0x09a1, 45, 22, 0),
  V(0x072f, 46, 23, 0),
  V(0x055c, 48, 24, 0),
  V(0x0406, 49, 25, 0),
  V(0x0303, 51, 26, 0),
  V(0x0240, 52, 27, 0),
  V(0x01b1, 54, 28, 0),
  V(0x0144, 56, 29, 0),
  V(0x00f5, 57, 30, 0),
  V(0x00b7, 59, 31, 0),
  V(0x008a, 60, 32, 0),
  V(0x0068, 62, 33, 0),
  V(0x004e, 63, 34, 0),
  V(0x003b, 32, 35, 0),
  V(0x002c, 33, 9, 0),
  V(0x5ae1, 37, 37, 1),
  V(0x484c, 64, 38, 0),
  V(0x3a0d, 65, 39, 0),
  V(0x2ef1, 67, 40, 0),
  V(0x261f, 68, 41, 0),
  V(0x1f33, 69, 42, 0),
  V(0x19a8, 70, 43, 0),
  V(0x1518, 72, 44, 0),
  V(0x1177, 73, 45, 0),
  V(0x0e74, 74, 46, 0),
  V(0x0bfb, 75, 47, 0),
  V(0x09f8, 77, 48, 0),
  V(0x0861, 78, 49, 0),
  V(0x0706, 79, 50, 0),
  V(0x05cd, 48, 51, 0),
  V(0x04de, 50, 52, 0),
  V(0x040f, 50, 53, 0),
  V(0x0363, 51, 54, 0),
  V(0x02d4, 52, 55, 0),
  V(0x025c, 53, 56, 0),
  V(0x01f8, 54, 57, 0),
  V(0x01a4, 55, 58, 0),
  V(0x0160, 56, 59, 0),
  V(0x0125, 57, 60, 0),
  V(0x00f6, 58, 61, 0),
  V(0x00cb, 59, 62, 0),
  V(0x00ab, 61, 63, 0),
  V(0x008f, 61, 32, 0),
  V(0x5b12, 65, 65, 1),
  V(0x4d04, 80, 66, 0),
  V(0x412c, 81, 67, 0),
  V(0x37d8, 82, 68, 0),
  V(0x2fe8, 83, 69, 0),
  V(0x293c, 84, 70, 0),
  V(0x2379, 86, 71, 0),
  V(0x1edf, 87, 72, 0),
  V(0x1aa9, 87, 73, 0),
  V(0x174e, 72, 74, 0),
  V(0x1424, 72, 75, 0),
  V(0x119c, 74, 76, 0),
  V(0x0f6b, 74, 77, 0),
  V(0x0d51, 75, 78, 0),
  V(0x0bb6, 77, 79, 0),
  V(0x0a40, 77, 48, 0),
  V(0x5832, 80, 81, 1),
  V(0x4d1c, 88, 82, 0),
  V(0x438e, 89, 83, 0),
  V(0x3bdd, 90, 84, 0),
  V(0x34ee, 91, 85, 0),
  V(0x2eae, 92, 86, 0),
  V(0x299a, 93, 87, 0),
  V(0x2516, 86, 71, 0),
  V(0x5570, 88, 89, 1),
  V(0x4ca9, 95, 90, 0),
  V(0x44d9, 96, 91, 0),
  V(0x3e22, 97, 92, 0),
  V(0x3824, 99, 93, 0),
  V(0x32b4, 99, 94, 0),
  V(0x2e17, 93, 86, 0),
  V(0x56a8, 95, 96, 1),
  V(0x4f46, 101, 97, 0),
  V(0x47e5, 102, 98, 0),
  V(0x41cf, 103, 99, 0),
  V(0x3c3d, 104, 100, 0),
  V(0x375e, 99, 93, 0),
  V(0x5231, 105, 102, 0),
  V(0x4c0f, 106, 103, 0),
  V(0x4639, 107, 104, 0),
  V(0x415e, 103, 99, 0),
  V(0x5627, 105, 106, 1),
  V(0x50e7, 108, 107, 0),
  V(0x4b85, 109, 103, 0),
  V(0x5597, 110, 109, 0),
  V(0x504f, 111, 107, 0),
  V(0x5a10, 110, 111, 1),
  V(0x5522, 112, 109, 0),
  V(0x59eb, 112, 111, 1),
  V(0x5a1d, 113, 113, 0),
};

#undef V

typedef struct jac {
  jpg_t         *j;
  const uint8_t *p, *end;   /* the segment's bytes; zeros past */
  int64_t        c, a;
  int32_t        ct;        /* -1: damage, nothing read to the next restart */
  uint8_t        dc_stats[16][64], ac_stats[16][256], fixed[4];
  uint8_t        ll_stats[4][160]; /* lossless: 158 bins a table (H.1.2.3.2) */
  int32_t        last_dc[J_COMPS], dc_ctx[J_COMPS];
} jac_t;

static int
decode(jac_t *e, uint8_t *st) {
  int64_t  qe, temp;
  uint32_t nl, nm;
  int      sv;

  while (e->a < 0x8000) {
    if (--e->ct < 0) {
      uint32_t data = e->p < e->end ? *e->p++ : 0u;

      e->c = (e->c << 8) | data;
      if ((e->ct += 8) < 0) {
        if (++e->ct == 0) e->a = 0x8000;
      }
    }
    e->a <<= 1;
  }
  sv   = *st;
  qe   = aritab[sv & 0x7F];
  nl   = (uint32_t)(qe & 0xFF);
  qe >>= 8;
  nm   = (uint32_t)(qe & 0xFF);
  qe >>= 8;
  temp = e->a - qe;
  e->a = temp;
  temp <<= e->ct;
  if (e->c >= temp) {
    e->c -= temp;
    if (e->a < qe) {
      e->a = qe;
      *st  = (uint8_t)((sv & 0x80) ^ nm);
    } else {
      e->a = qe;
      *st  = (uint8_t)((sv & 0x80) ^ nl);
      sv ^= 0x80;
    }
  } else if (e->a < 0x8000) {
    if (e->a < qe) {
      *st = (uint8_t)((sv & 0x80) ^ nl);
      sv ^= 0x80;
    } else {
      *st = (uint8_t)((sv & 0x80) ^ nm);
    }
  }
  return sv >> 7;
}

/* a DC difference's magnitude and sign (F.2.4.1), the context after;
   false: the magnitude past 15 bits */
static bool
dc_diff(jac_t *e, uint32_t ci, uint32_t tbl, int32_t *out) {
  uint8_t *st = e->dc_stats[tbl] + e->dc_ctx[ci];
  int      sign, m, v;

  if (decode(e, st) == 0) {
    e->dc_ctx[ci] = 0;
    *out          = 0;
    return true;
  }
  sign = decode(e, st + 1);
  st += 2 + sign;
  if ((m = decode(e, st)) != 0) {
    st = e->dc_stats[tbl] + 20;
    while (decode(e, st)) {
      if ((m <<= 1) == 0x8000) return false;
      st += 1;
    }
  }
  if (m < (int)((1L << e->j->dc_l[tbl]) >> 1)) e->dc_ctx[ci] = 0;
  else if (m > (int)((1L << e->j->dc_u[tbl]) >> 1)) e->dc_ctx[ci] = 12 + sign * 4;
  else e->dc_ctx[ci] = 4 + sign * 4;
  v = m;
  st += 14;
  while (m >>= 1) {
    if (decode(e, st)) v |= m;
  }
  v += 1;
  *out = sign ? -v : v;
  return true;
}

/* an AC coefficient's magnitude past its sign (st at the bin after it) */
static bool
ac_value(jac_t *e, uint8_t *st, uint32_t tbl, uint32_t k, int *out) {
  int m, v;

  if ((m = decode(e, st)) != 0) {
    if (decode(e, st)) {
      m <<= 1;
      st = e->ac_stats[tbl] + (k <= e->j->ac_k[tbl] ? 189 : 217);
      while (decode(e, st)) {
        if ((m <<= 1) == 0x8000) return false;
        st += 1;
      }
    }
  }
  v = m;
  st += 14;
  while (m >>= 1) {
    if (decode(e, st)) v |= m;
  }
  *out = v + 1;
  return true;
}

/* -- blocks (libjpeg's decode_mcu and its progressive four) ------------------- */

static bool
block_seq(jac_t *e, uint32_t ci, int16_t *blk) {
  const jcomp_t *c = &e->j->comp[ci];
  int32_t        diff;
  uint32_t       k;

  if (!dc_diff(e, ci, c->td, &diff)) return false;
  if (diff) e->last_dc[ci] = (e->last_dc[ci] + diff) & 0xffff;
  blk[0] = (int16_t)e->last_dc[ci];
  for (k = 1; k <= 63u; k++) {
    uint8_t *st = e->ac_stats[c->ta] + 3u * (k - 1u);
    int      sign, v;

    if (decode(e, st)) break; /* EOB */
    while (decode(e, st + 1) == 0) {
      st += 3;
      if (++k > 63u) return false;
    }
    sign = decode(e, e->fixed);
    if (!ac_value(e, st + 2, c->ta, k, &v)) return false;
    blk[jpg_natural[k]] = (int16_t)(sign ? -v : v);
  }
  return true;
}

static bool
block_dc_first(jac_t *e, uint32_t ci, int16_t *blk) {
  int32_t diff;

  if (!dc_diff(e, ci, e->j->comp[ci].td, &diff)) return false;
  if (diff) e->last_dc[ci] = (e->last_dc[ci] + diff) & 0xffff;
  blk[0] = (int16_t)((uint32_t)e->last_dc[ci] << e->j->al);
  return true;
}

static bool
block_ac_first(jac_t *e, uint32_t ci, int16_t *blk) {
  const jpg_t *j = e->j;
  uint32_t     tbl = j->comp[ci].ta, k;

  for (k = j->ss; k <= j->se; k++) {
    uint8_t *st = e->ac_stats[tbl] + 3u * (k - 1u);
    int      sign, v;

    if (decode(e, st)) break; /* EOB */
    while (decode(e, st + 1) == 0) {
      st += 3;
      if (++k > j->se) return false;
    }
    sign = decode(e, e->fixed);
    if (!ac_value(e, st + 2, tbl, k, &v)) return false;
    blk[jpg_natural[k]] = (int16_t)((uint32_t)(sign ? -v : v) << j->al);
  }
  return true;
}

static bool
block_ac_refine(jac_t *e, uint32_t ci, int16_t *blk) {
  const jpg_t *j = e->j;
  uint32_t     tbl = j->comp[ci].ta, k, kex;
  int32_t      p1 = 1 << j->al, m1 = -1 * (1 << j->al);

  for (kex = j->se; kex > 0u; kex--) {
    if (blk[jpg_natural[kex]]) break;
  }
  for (k = j->ss; k <= j->se; k++) {
    uint8_t *st = e->ac_stats[tbl] + 3u * (k - 1u);

    if (k > kex && decode(e, st)) break; /* EOB */
    for (;;) {
      int16_t *coef = &blk[jpg_natural[k]];

      if (*coef) {
        if (decode(e, st + 2)) *coef = (int16_t)(*coef < 0 ? *coef + m1 : *coef + p1);
        break;
      }
      if (decode(e, st + 1)) {
        *coef = (int16_t)(decode(e, e->fixed) ? m1 : p1);
        break;
      }
      st += 3;
      if (++k > j->se) return false;
    }
  }
  return true;
}

/* -- a scan -------------------------------------------------------------------- */

/* the statistics, predictions and coder as a scan (or a restart) starts */
static void
reset(jac_t *e) {
  const jpg_t *j = e->j;
  uint32_t     i;

  for (i = 0; i < j->nscomp; i++) {
    uint32_t       ci = j->scomp[i];
    const jcomp_t *c  = &j->comp[ci];

    if (j->process != J_PROGRESSIVE || (j->ss == 0u && j->ah == 0u)) {
      memset(e->dc_stats[c->td], 0, sizeof(e->dc_stats[0]));
      e->last_dc[ci] = 0;
      e->dc_ctx[ci]  = 0;
    }
    if (j->process != J_PROGRESSIVE || j->ss) memset(e->ac_stats[c->ta], 0, sizeof(e->ac_stats[0]));
  }
  e->c  = 0;
  e->a  = 0;
  e->ct = -16;
}

static void
seg_at(jac_t *e, uint32_t s) {
  const jdata_t *d = &e->j->sd;

  if (s < d->nseg) {
    e->p   = d->buf + d->seg[s].start;
    e->end = d->buf + d->seg[s].end;
  } else {
    e->p = e->end = d->buf; /* at a marker: zeros */
  }
}

IM_HIDE bool
jpg_scan_arith(jpg_t *j) {
  jac_t   *e;
  uint32_t w, h, total, n, cur = 0u, next_rst = 0u, togo, i;
  bool     refine_dc = j->process == J_PROGRESSIVE && j->ss == 0u && j->ah;

  if (!(e = calloc(1, sizeof(*e)))) return false;
  e->j        = j;
  e->fixed[0] = 113;
  if (j->nscomp == 1u) {
    w = j->comp[j->scomp[0]].sw;
    h = j->comp[j->scomp[0]].sh;
  } else {
    w = j->mcux;
    h = j->mcuy;
  }
  total = w * h;
  reset(e);
  seg_at(e, 0u);
  togo = j->restart;
  for (n = 0; n < total; n++) {
    uint32_t mx = n % w, my = n / w;

    if (j->flow && !mx) jpg_flow_row(j, my);
    if (j->restart) {
      if (!togo) {
        bool at_marker = jpg_resync(&j->sd, &next_rst, &cur);

        reset(e);
        seg_at(e, at_marker ? UINT32_MAX : cur);
        togo = j->restart;
      }
      togo--;
    }
    for (i = 0; i < j->nscomp; i++) {
      uint32_t ci = j->scomp[i];
      jcomp_t *c  = &j->comp[ci];
      uint32_t nh = j->nscomp == 1u ? 1u : c->h, nv = j->nscomp == 1u ? 1u : c->v, a, b;

      for (b = 0; b < nv; b++) {
        for (a = 0; a < nh; a++) {
          IM_ALIGN(16) int16_t scratch[64];
          uint32_t bx = mx * nh + a, by = my * nv + b;
          int16_t *blk;
          bool     ok = true;

          if (j->multiscan) {
            blk = c->coefs + ((size_t)by * c->bw + bx) * 64u;
          } else {
            blk = scratch;
            memset(scratch, 0, sizeof(scratch));
          }
          if (refine_dc) {
            if (decode(e, e->fixed)) blk[0] = (int16_t)(blk[0] | (1 << j->al));
          } else if (e->ct != -1) {
            if (j->process != J_PROGRESSIVE) ok = block_seq(e, ci, blk);
            else if (j->ss == 0u) ok = block_dc_first(e, ci, blk);
            else if (!j->ah) ok = block_ac_first(e, ci, blk);
            else ok = block_ac_refine(e, ci, blk);
          }
          if (!ok) e->ct = -1; /* the rest of the MCU and those after: left */
          if (!j->multiscan) jpg_put(j, c, blk, bx, by);
        }
      }
    }
  }
  /* no data runs out here: each MCU counts (libjpeg's last good row) */
  j->last_good = j->nscomp == 1u ? (h - 1u) / j->comp[j->scomp[0]].v : h - 1u;
  free(e);
  return true;
}

/* -- lossless (H.1.2.3) ----------------------------------------------------------- */

/* a difference's class for the contexts (F.1.4.4.1.2's bounds): 0 zero,
   1 small positive, 2 small negative, 3 large positive, 4 large negative */
static uint8_t
ll_class(const jpg_t *j, uint32_t tbl, int32_t d) {
  uint32_t a = d < 0 ? (uint32_t)-d : (uint32_t)d, l = j->dc_l[tbl];

  if (a <= (l ? 1u << (l - 1u) : 0u)) return 0;
  if (a > 1u << j->dc_u[tbl]) return d < 0 ? 4 : 3;
  return d < 0 ? 2 : 1;
}

/* a difference by its neighbours' classes, the left one's `ca` and the
   one above's `cb` (Table H.3: S0 from both, the magnitudes' from the one
   above alone); false: the magnitude past 15 bits */
static bool
ll_diff(jac_t *e, uint8_t *stats, uint32_t ca, uint32_t cb, int32_t *out) {
  uint8_t *st = stats + 20u * ca + 4u * cb;
  int      sign, m, v;

  if (decode(e, st) == 0) {
    *out = 0;
    return true;
  }
  sign = decode(e, st + 1);
  st += 2 + sign;
  if ((m = decode(e, st)) != 0) {
    st = stats + (cb >= 3u ? 129 : 100);
    while (decode(e, st)) {
      if ((m <<= 1) == 0x8000) return false;
      st += 1;
    }
  }
  v = m;
  st += 14;
  while (m >>= 1) {
    if (decode(e, st)) v |= m;
  }
  v += 1;
  *out = sign ? -v : v;
  return true;
}

/* Row by row as the Huffman one's (jpg_scan_lossless): each difference
   conditioned on those coded to its left and above in its component, the
   left zero at a row's start, the above zero on a scan's or a restart's
   first row; the statistics reset with the coder. Past the data zeros
   are read; a magnitude past 15 bits (damage) zeros to the next restart */
IM_HIDE bool
jpg_scan_arith_lossless(jpg_t *j) {
  jac_t   *e;
  jloss_t  L;
  uint8_t *above[J_COMPS] = { NULL };
  uint32_t i, per_row, nrows, nirows, irow, rows_to_go = 0u, cur = 0u, next_rst = 0u;
  bool     single = j->nscomp == 1u, ok = false;

  per_row = single ? j->comp[j->scomp[0]].cw : j->mcux;
  nrows   = single ? j->comp[j->scomp[0]].ch : j->mcuy;
  if (j->restart && j->restart % per_row) return false; /* as the Huffman one */
  if (!(e = calloc(1, sizeof(*e)))) return false;
  e->j = j;
  if (!jpg_loss_start(j, &L)) goto done;
  for (i = 0; i < j->nscomp; i++) {
    if (!(above[j->scomp[i]] = calloc(j->comp[j->scomp[i]].bw, 1))) goto done;
  }
  e->ct = -16;
  seg_at(e, 0u);
  rows_to_go = j->restart / per_row;

  nirows = single ? (nrows + j->comp[j->scomp[0]].v - 1u) / j->comp[j->scomp[0]].v : nrows;
  for (irow = 0; irow < nirows; irow++) {
    uint32_t mrows = 1u, yo;

    if (single) {
      uint32_t v = j->comp[j->scomp[0]].v;

      mrows = irow + 1u < nirows ? v : (nrows % v ? nrows % v : v);
    }
    for (yo = 0; yo < mrows; yo++) {
      uint32_t mx;

      if (j->restart && !rows_to_go) {
        bool at_marker = jpg_resync(&j->sd, &next_rst, &cur);

        memset(e->ll_stats, 0, sizeof(e->ll_stats));
        e->c  = 0;
        e->a  = 0;
        e->ct = -16;
        seg_at(e, at_marker ? UINT32_MAX : cur);
        for (i = 0; i < J_COMPS; i++) L.first[i] = true;
        for (i = 0; i < j->nscomp; i++) memset(above[j->scomp[i]], 0, j->comp[j->scomp[i]].bw);
        rows_to_go = j->restart / per_row;
      }
      for (mx = 0; mx < per_row; mx++) {
        for (i = 0; i < j->nscomp; i++) {
          uint32_t ci = j->scomp[i], tbl;
          jcomp_t *c  = &j->comp[ci];
          uint32_t nh = single ? 1u : c->h, nv = single ? 1u : c->v, a, b;

          tbl = c->td;
          for (b = 0; b < nv; b++) {
            uint32_t r = single ? yo : b;
            int32_t *row = L.diff[ci] + (size_t)r * c->bw;

            for (a = 0; a < nh; a++) {
              uint32_t x = mx * nh + a, ca, cb;
              int32_t  d = 0;

              ca = x ? ll_class(j, tbl, row[x - 1u]) : 0u;
              cb = single || !r ? above[ci][x] : ll_class(j, tbl, L.diff[ci][(size_t)(r - 1u) * c->bw + x]);
              if (e->ct != -1 && !ll_diff(e, e->ll_stats[tbl], ca, cb, &d)) {
                e->ct = -1;
                d     = 0;
              }
              row[x] = d;
            }
          }
        }
      }
      /* a lone component's row is the next one's above at once */
      if (single) {
        uint32_t ci = j->scomp[0], x;
        int32_t *row = L.diff[ci] + (size_t)yo * j->comp[ci].bw;

        for (x = 0; x < j->comp[ci].bw; x++) above[ci][x] = ll_class(j, j->comp[ci].td, row[x]);
      }
      if (j->restart) rows_to_go--;
    }
    for (i = 0; i < j->nscomp; i++) {
      uint32_t ci = j->scomp[i], rows = single ? mrows : j->comp[ci].v, r, x;
      jcomp_t *c  = &j->comp[ci];

      if (!single) {
        const int32_t *last = L.diff[ci] + (size_t)(c->v - 1u) * c->bw;

        for (x = 0; x < c->bw; x++) above[ci][x] = ll_class(j, c->td, last[x]);
        if (irow + 1u == nirows && c->ch % c->v) rows = c->ch % c->v;
      }
      for (r = 0; r < rows; r++) jpg_undiff(j, &L, ci, L.diff[ci] + (size_t)r * c->bw);
    }
  }
  j->last_good = nirows - 1u;
  ok = true;

done:
  for (i = 0; i < J_COMPS; i++) free(above[i]);
  jpg_loss_end(&L);
  free(e);
  return ok;
}
