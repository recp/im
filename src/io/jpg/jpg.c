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
 * JPEG (ITU-T T.81 | ISO/IEC 10918-1), im's own: the markers, the frame,
 * the scans in turn, then the pixels. A stream's colour space is guessed
 * as libjpeg guesses it (JFIF, Adobe's transform, the component IDs), its
 * ICC profile (APP2's chunks) and density (JFIF) kept.
 */

#include "jpg.h"
#include "jpgint.h"
#if !IM_CODECS_ONLY
#  include "../../file.h"
#  ifdef __APPLE__
#    include "../apple/coreimg.h"
#  endif
#endif

IM_INLINE uint32_t
be16(const uint8_t *p) {
  return (uint32_t)p[0] << 8 | p[1];
}

/* -- blocks to planes ------------------------------------------------------------ */

IM_HIDE void
jpg_put(jpg_t *j, jcomp_t *c, int16_t *blk, uint32_t bx, uint32_t by) {
  size_t at;

  /* an MCU's padding (never shown), or outside the region's blocks */
  if (bx < c->rbx0 || bx >= c->rbx1 || by < c->rby0 || by >= c->rby1) return;
  at = ((size_t)by * c->stride + bx) * c->ss;
  if (j->diff) c->idct(blk, c->q, (int32_t *)c->plane + at, c->stride);
  else if (j->prec <= 8u) c->idct(blk, c->q, (uint8_t *)c->plane + at, c->stride);
  else c->idct(blk, c->q, (uint16_t *)c->plane + at, c->stride);
}

/* -- block smoothing (libjpeg's, jdcoefct.c) ------------------------------------- */

/* where a progressive image's first coefficients never all came, libjpeg
   guesses them from the 5 × 5 blocks' DCs round each (and the DC too where
   none of them came); smoothing_ok's conditions */
static bool
smoothing_ok(const jpg_t *j, int8_t latch[J_COMPS][10], int8_t prev[J_COMPS][10]) {
  static const uint8_t qpos[10] = { 0, 1, 8, 16, 9, 2, 3, 10, 17, 24 };
  bool     useful = false;
  uint32_t i, k;

  if (j->process != J_PROGRESSIVE) return false;
  for (i = 0; i < j->ncomp; i++) {
    const jcomp_t *c = &j->comp[i];

    for (k = 0; k < 10u; k++) {
      if (!c->q[qpos[k]]) return false;
    }
    if (c->bits[0] < 0) return false;
    latch[i][0] = c->bits[0];
    for (k = 1; k < 10u; k++) {
      prev[i][k]  = j->nscans > 1u ? c->prev[k] : -1;
      latch[i][k] = c->bits[k];
      if (c->bits[k] != 0) useful = true;
    }
  }
  return useful;
}

/* one coefficient's guess: num / (Q << 8), rounded, held below 2^Al */
IM_INLINE int16_t
guess(int64_t num, int64_t q, int al) {
  int32_t pred;

  if (num >= 0) {
    pred = (int32_t)(((q << 7) + num) / (q << 8));
    if (al > 0 && pred >= (1 << al)) pred = (1 << al) - 1;
  } else {
    pred = (int32_t)(((q << 7) - num) / (q << 8));
    if (al > 0 && pred >= (1 << al)) pred = (1 << al) - 1;
    pred = -pred;
  }
  return (int16_t)pred;
}

static void
smooth_comp(jpg_t *j, uint32_t ci, const int8_t *latch, const int8_t *prev, uint32_t by0, uint32_t by1) {
  jcomp_t       *c = &j->comp[ci];
  const uint16_t *qt = c->q;
  int64_t        Q00 = qt[0], Q01 = qt[1], Q10 = qt[8], Q20 = qt[16], Q11 = qt[9], Q02 = qt[2];
  int64_t        Q03 = qt[3], Q12 = qt[10], Q21 = qt[17], Q30 = qt[24];
  uint32_t       v = c->v, rows = j->mcuy, by, bx, last = c->sw - 1u;

  for (by = by0; by < by1; by++) {
    uint32_t       row = by / v, br = by % v, block_rows, ibr, nbr;
    const int8_t  *cb  = row > j->last_good ? prev : latch;
    bool           change_dc = true;
    const int16_t *cur, *pr, *ppr, *nx, *nnx;
    int            DC01, DC02, DC03, DC04, DC05, DC06, DC07, DC08, DC09, DC10, DC11, DC12, DC13;
    int            DC14, DC15, DC16, DC17, DC18, DC19, DC20, DC21, DC22, DC23, DC24, DC25, al;
    uint32_t       k;

    for (k = 1; k < 10u; k++) change_dc = change_dc && cb[k] == -1;
    /* libjpeg's block-row count: the last iMCU row's own */
    block_rows = row + 1u < rows ? v : (c->sh % v ? c->sh % v : v);
    ibr        = row * block_rows + br;
    nbr        = block_rows * rows;
    cur        = c->coefs + (size_t)by * c->bw * 64u;
    pr         = ibr > 0u ? cur - (size_t)c->bw * 64u : cur;
    ppr        = ibr > 1u ? pr - (size_t)c->bw * 64u : pr;
    nx         = ibr + 1u < nbr ? cur + (size_t)c->bw * 64u : cur;
    nnx        = ibr + 2u < nbr ? nx + (size_t)c->bw * 64u : nx;

    DC01 = DC02 = DC03 = DC04 = DC05 = ppr[0];
    DC06 = DC07 = DC08 = DC09 = DC10 = pr[0];
    DC11 = DC12 = DC13 = DC14 = DC15 = cur[0];
    DC16 = DC17 = DC18 = DC19 = DC20 = nx[0];
    DC21 = DC22 = DC23 = DC24 = DC25 = nnx[0];
    for (bx = 0; bx <= last; bx++) {
      IM_ALIGN(16) int16_t ws[64];
      size_t               o = (size_t)bx * 64u;

      memcpy(ws, cur + o, sizeof(ws));
      if (bx == 0u && bx < last) {
        DC04 = DC05 = ppr[o + 64u];
        DC09 = DC10 = pr[o + 64u];
        DC14 = DC15 = cur[o + 64u];
        DC19 = DC20 = nx[o + 64u];
        DC24 = DC25 = nnx[o + 64u];
      }
      if (bx + 1u < last) {
        DC05 = ppr[o + 128u];
        DC10 = pr[o + 128u];
        DC15 = cur[o + 128u];
        DC20 = nx[o + 128u];
        DC25 = nnx[o + 128u];
      }
      if ((al = cb[1]) != 0 && ws[1] == 0) {
        ws[1] = guess(Q00 * (change_dc ? (-DC01 - DC02 + DC04 + DC05 - 3 * DC06 + 13 * DC07 - 13 * DC09 + 3 * DC10 - 3 * DC11
                                          + 38 * DC12 - 38 * DC14 + 3 * DC15 - 3 * DC16 + 13 * DC17 - 13 * DC19 + 3 * DC20
                                          - DC21 - DC22 + DC24 + DC25)
                                       : (-7 * DC11 + 50 * DC12 - 50 * DC14 + 7 * DC15)), Q01, al);
      }
      if ((al = cb[2]) != 0 && ws[8] == 0) {
        ws[8] = guess(Q00 * (change_dc ? (-DC01 - 3 * DC02 - 3 * DC03 - 3 * DC04 - DC05 - DC06 + 13 * DC07 + 38 * DC08
                                          + 13 * DC09 - DC10 + DC16 - 13 * DC17 - 38 * DC18 - 13 * DC19 + DC20 + DC21
                                          + 3 * DC22 + 3 * DC23 + 3 * DC24 + DC25)
                                       : (-7 * DC03 + 50 * DC08 - 50 * DC18 + 7 * DC23)), Q10, al);
      }
      if ((al = cb[3]) != 0 && ws[16] == 0) {
        ws[16] = guess(Q00 * (change_dc ? (DC03 + 2 * DC07 + 7 * DC08 + 2 * DC09 - 5 * DC12 - 14 * DC13 - 5 * DC14
                                           + 2 * DC17 + 7 * DC18 + 2 * DC19 + DC23)
                                        : (-DC03 + 13 * DC08 - 24 * DC13 + 13 * DC18 - DC23)), Q20, al);
      }
      if ((al = cb[4]) != 0 && ws[9] == 0) {
        ws[9] = guess(Q00 * (change_dc ? (-DC01 + DC05 + 9 * DC07 - 9 * DC09 - 9 * DC17 + 9 * DC19 + DC21 - DC25)
                                       : (DC10 + DC16 - 10 * DC17 + 10 * DC19 - DC02 - DC20 + DC22 - DC24 + DC04
                                          - DC06 + 10 * DC07 - 10 * DC09)), Q11, al);
      }
      if ((al = cb[5]) != 0 && ws[2] == 0) {
        ws[2] = guess(Q00 * (change_dc ? (2 * DC07 - 5 * DC08 + 2 * DC09 + DC11 + 7 * DC12 - 14 * DC13 + 7 * DC14
                                          + DC15 + 2 * DC17 - 5 * DC18 + 2 * DC19)
                                       : (-DC11 + 13 * DC12 - 24 * DC13 + 13 * DC14 - DC15)), Q02, al);
      }
      if (change_dc) {
        if ((al = cb[6]) != 0 && ws[3] == 0) ws[3] = guess(Q00 * (DC07 - DC09 + 2 * DC12 - 2 * DC14 + DC17 - DC19), Q03, al);
        if ((al = cb[7]) != 0 && ws[10] == 0) ws[10] = guess(Q00 * (DC07 - 3 * DC08 + DC09 - DC17 + 3 * DC18 - DC19), Q12, al);
        if ((al = cb[8]) != 0 && ws[17] == 0) ws[17] = guess(Q00 * (DC07 - DC09 - 3 * DC12 + 3 * DC14 + DC17 - DC19), Q21, al);
        if ((al = cb[9]) != 0 && ws[24] == 0) ws[24] = guess(Q00 * (DC07 + 2 * DC08 + DC09 - DC17 - 2 * DC18 - DC19), Q30, al);
        ws[0] = guess(Q00 * (-2 * DC01 - 6 * DC02 - 8 * DC03 - 6 * DC04 - 2 * DC05 - 6 * DC06 + 6 * DC07 + 42 * DC08
                             + 6 * DC09 - 6 * DC10 - 8 * DC11 + 42 * DC12 + 152 * DC13 + 42 * DC14 - 8 * DC15
                             - 6 * DC16 + 6 * DC17 + 42 * DC18 + 6 * DC19 - 6 * DC20 - 2 * DC21 - 6 * DC22
                             - 8 * DC23 - 6 * DC24 - 2 * DC25), Q00, 0);
      }
      jpg_put(j, c, ws, bx, by);
      DC01 = DC02;
      DC02 = DC03;
      DC03 = DC04;
      DC04 = DC05;
      DC06 = DC07;
      DC07 = DC08;
      DC08 = DC09;
      DC09 = DC10;
      DC11 = DC12;
      DC12 = DC13;
      DC13 = DC14;
      DC14 = DC15;
      DC16 = DC17;
      DC17 = DC18;
      DC18 = DC19;
      DC19 = DC20;
      DC21 = DC22;
      DC22 = DC23;
      DC23 = DC24;
      DC24 = DC25;
    }
  }
}

/* a multi-scan image's blocks, once its scans are read: bands of block
   rows (of each component) on the pool */
typedef struct jidct {
  jpg_t   *j;
  bool     smooth;
  int8_t   latch[J_COMPS][10], prev[J_COMPS][10];
  uint32_t band, items[J_COMPS + 1]; /* items before component i */
} jidct_t;

/* the coefficients of block rows by0 … by1 - 1 done with, given back (a
   big image's): two rows either side kept for the bands round them
   (smoothing reads two rows away); none of a hierarchical frame's */
static void
idct_release(const jpg_t *j, const jcomp_t *c, uint32_t by0, uint32_t by1) {
  size_t   row = (size_t)c->bw * 64u * sizeof(int16_t);
  uint32_t r0 = by0 ? by0 + 2u : 0u, r1 = by1 >= c->sh ? c->bh : (by1 > 2u ? by1 - 2u : 0u);

  if (r1 > r0) im_release(j->big, (uint8_t *)c->coefs + (size_t)r0 * row, (size_t)(r1 - r0) * row);
}

/* component ci's block rows by0 … by1 - 1 */
static void
idct_rows(const jidct_t *t, uint32_t ci, uint32_t by0, uint32_t by1) {
  jpg_t   *j = t->j;
  jcomp_t *c = &j->comp[ci];
  uint32_t bx, by;

  if (t->smooth) {
    smooth_comp(j, ci, t->latch[ci], t->prev[ci], by0, by1);
    idct_release(j, c, by0, by1);
    return;
  }
  for (by = by0 > c->rby0 ? by0 : c->rby0; by < by1 && by < c->rby1; by++) {
    for (bx = c->rbx0; bx < c->rbx1; bx++) jpg_put(j, c, c->coefs + ((size_t)by * c->bw + bx) * 64u, bx, by);
  }
  idct_release(j, c, by0, by1);
}

static bool
idct_item(void *arg, uint32_t worker, uint32_t item) {
  jidct_t *t = arg;
  uint32_t ci = 0, by0;

  (void)worker;
  while (item >= t->items[ci + 1u]) ci++;
  by0 = (item - t->items[ci]) * t->band;
  idct_rows(t, ci, by0, by0 + t->band < t->j->comp[ci].sh ? by0 + t->band : t->j->comp[ci].sh);
  return true;
}

/* the bands: each component's block rows in items, a few a worker */
static void
idct_plan(jpg_t *j, jidct_t *t) {
  uint32_t i, rows = 0u;

  t->j      = j;
  t->smooth = !j->hier && smoothing_ok(j, t->latch, t->prev); /* a hierarchical frame as its encoder made it */
  for (i = 0; i < j->ncomp; i++) rows = j->comp[i].sh > rows ? j->comp[i].sh : rows;
  /* on one thread bands too, so a big image's coefficients go as its
     planes come */
  t->band     = j->pool ? rows / (im_pool_size(j->pool) * 4u) : 32u;
  t->band     = t->band ? t->band : 1u;
  t->items[0] = 0u;
  for (i = 0; i < j->ncomp; i++) t->items[i + 1u] = t->items[i] + (j->comp[i].sh + t->band - 1u) / t->band;
}

static bool
idct_all(jpg_t *j) {
  jidct_t t;

  idct_plan(j, &t);
  return im_pool_run(j->pool, idct_item, &t, t.items[j->ncomp]);
}

/* -- markers ----------------------------------------------------------------------- */

static bool frame_size(jpg_t *j);
static bool frame_done(jpg_t *j);
static uint32_t hier_comp(const jpg_t *j, uint32_t id);

static bool
read_sof(jpg_t *j, uint32_t m, const uint8_t *s, uint32_t len) {
  uint32_t i;
  bool     diff = m == 0xC5u || m == 0xC6u || m == 0xC7u || m >= 0xCDu;

  /* one frame, but a hierarchical sequence's one after another (the one
     before finished first); a differential one only in such a sequence */
  if (len < 6u || (j->frame && (!j->hier || !frame_done(j))) || (diff && !j->hier)) return false;
  j->diff   = diff;
  j->grow   = j->expand;
  j->expand = 0u;
  j->prec   = s[0];
  j->height = be16(s + 1);
  j->width  = be16(s + 3);
  j->ncomp  = s[5];
  j->arith  = m >= 0xC9u;
  switch (m & 3u) {
  case 2:  j->process = J_PROGRESSIVE; break;
  case 3:  j->process = J_LOSSLESS;    break;
  default: j->process = J_SEQUENTIAL;  break;
  }
  if (!j->width || !j->ncomp || j->ncomp > J_COMPS || len != 6u + j->ncomp * 3u) return false;
  if (j->process == J_LOSSLESS ? j->prec < 2u || j->prec > 16u : j->prec != 8u && j->prec != 12u) return false;

  j->hmax = j->vmax = 1u;
  for (i = 0; i < j->ncomp; i++) {
    jcomp_t *c = &j->comp[i];

    c->id = s[6u + i * 3u];
    c->h  = s[7u + i * 3u] >> 4;
    c->v  = s[7u + i * 3u] & 15u;
    c->tq = s[8u + i * 3u];
    if (c->h < 1u || c->h > 4u || c->v < 1u || c->v > 4u || c->tq > 3u) return false;
    if (c->h > j->hmax) j->hmax = c->h;
    if (c->v > j->vmax) j->vmax = c->v;
  }
  if (j->hier) {
    /* DHP's precision, no bigger than it, its components (each one's
       samples there before a differential frame of it) */
    if (j->prec != j->hprec || j->width > j->hwidth || !j->height || j->height > j->hheight) return false;
    for (i = 0; i < j->ncomp; i++) {
      uint32_t k = hier_comp(j, j->comp[i].id);

      if (k == J_COMPS || (diff && !(j->hseen >> k & 1u))) return false;
    }
    for (i = 0; !diff && i < j->ncomp; i++) j->hseen |= 1u << hier_comp(j, j->comp[i].id);
    if (!j->nframes++) j->hdct = j->process != J_LOSSLESS;
  }
  j->frame = true;
  /* no height: a DNL after the first scan gives it (frame_size then) */
  return j->height ? frame_size(j) : true;
}

/* the region cut to the frame's pixels (without one: all of them, the
   frame's as it's shown: a cut hierarchical sequence's is its last
   reference, past the frame read last); false: none of it inside */
IM_HIDE bool
jpg_region_cut(jpg_t *j) {
  if (!j->region) {
    j->reg_x = j->reg_y = 0u;
    j->reg_w = j->ow;
    j->reg_h = j->oh;
    return true;
  }
  if (j->reg_x >= j->ow || j->reg_y >= j->oh || !j->reg_w || !j->reg_h) return false;
  if (j->reg_w > j->ow - j->reg_x) j->reg_w = j->ow - j->reg_x;
  if (j->reg_h > j->oh - j->reg_y) j->reg_h = j->oh - j->reg_y;
  return true;
}

/* IM_OPTION_REGION's rectangle cut to the frame (none of it inside: false),
   and each component's blocks it needs: those under its pixels and their
   upsampling's neighbours (a sample either side, a block more for safety
   at a block's edge). A hierarchical frame's blocks are all made (the next
   frame's reference); without a region, every block */
static bool
region_size(jpg_t *j) {
  uint32_t i;

  /* a hierarchical sequence's is cut at its last frame (jpg_out) */
  if ((!j->region || !j->hier) && !jpg_region_cut(j)) return false;
  for (i = 0; i < j->ncomp; i++) {
    jcomp_t *c = &j->comp[i];
    uint64_t s0, s1, t0, t1, ss = c->ss ? c->ss : 1u;

    c->rbx0 = c->rby0 = 0u;
    c->rbx1 = c->sw;
    c->rby1 = c->sh;
    if (!j->region || j->hier || j->process == J_LOSSLESS) continue;
    /* its samples under the region: x × uh / hmax (UP_ANY's map, every
       other's too), a sample's neighbours either side */
    s0 = (uint64_t)j->reg_x * c->uh / j->hmax;
    s1 = ((uint64_t)(j->reg_x + j->reg_w) * c->uh + j->hmax - 1u) / j->hmax;
    t0 = (uint64_t)j->reg_y * c->uv / j->vmax;
    t1 = ((uint64_t)(j->reg_y + j->reg_h) * c->uv + j->vmax - 1u) / j->vmax;
    s0 = s0 > 1u ? s0 - 2u : 0u;
    t0 = t0 > 1u ? t0 - 2u : 0u;
    s1 += 2u;
    t1 += 2u;
    c->rbx0 = (uint32_t)(s0 / ss);
    c->rby0 = (uint32_t)(t0 / ss);
    c->rbx1 = (uint32_t)((s1 + ss - 1u) / ss) < c->sw ? (uint32_t)((s1 + ss - 1u) / ss) : c->sw;
    c->rby1 = (uint32_t)((t1 + ss - 1u) / ss) < c->sh ? (uint32_t)((t1 + ss - 1u) / ss) : c->sh;
  }
  return true;
}

/* the frame's MCUs and planes' sizes, its size known; past J_SAMPLES a
   component, refused (nothing made: the planes at the first scan).

   Decoded smaller (libjpeg's jdmaster.c): the pixels the size over the
   scale, rounded up, a block 8 / scale samples across; a component
   sampled more sparsely takes bigger blocks (by twos) while that leaves
   its upsampling less to do. A lossless frame keeps its size. */
static bool
frame_size(jpg_t *j) {
  uint32_t i, unit;
  bool     lossless = j->process == J_LOSSLESS;

  /* a data unit: a block, or (lossless) a sample */
  unit    = lossless ? 1u : 8u;
  j->ds   = lossless ? 1u : 8u / (j->hier ? 1u : j->scale); /* a hierarchical sequence full size */
  j->ow   = lossless ? j->width : (uint32_t)(((uint64_t)j->width * j->ds + 7u) / 8u);
  j->oh   = lossless ? j->height : (uint32_t)(((uint64_t)j->height * j->ds + 7u) / 8u);
  j->mcux = (j->width + unit * j->hmax - 1u) / (unit * j->hmax);
  j->mcuy = (j->height + unit * j->vmax - 1u) / (unit * j->vmax);
  for (i = 0; i < j->ncomp; i++) {
    jcomp_t *c = &j->comp[i];
    uint64_t samples;

    c->bw     = j->mcux * c->h;
    c->bh     = j->mcuy * c->v;
    c->cw     = (uint32_t)(((uint64_t)j->width * c->h + j->hmax - 1u) / j->hmax);
    c->ch     = (uint32_t)(((uint64_t)j->height * c->v + j->vmax - 1u) / j->vmax);
    c->sw     = (c->cw + unit - 1u) / unit;
    c->sh     = (c->ch + unit - 1u) / unit;
    c->ss     = (uint8_t)j->ds;
    while (!lossless && c->ss < 8u && (j->hmax * j->ds) % (c->h * c->ss * 2u) == 0u
           && (j->vmax * j->ds) % (c->v * c->ss * 2u) == 0u) {
      c->ss = (uint8_t)(c->ss * 2u);
    }
    c->uh     = (uint8_t)(c->h * c->ss / j->ds);
    c->uv     = (uint8_t)(c->v * c->ss / j->ds);
    c->ow     = lossless ? c->cw : (uint32_t)(((uint64_t)j->width * c->h * c->ss + j->hmax * 8u - 1u) / (j->hmax * 8u));
    c->oh     = lossless ? c->ch : (uint32_t)(((uint64_t)j->height * c->v * c->ss + j->vmax * 8u - 1u) / (j->vmax * 8u));
    c->idct   = j->diff ? jpg_idct_diff(j->prec) : jpg_idct_for(c->ss, j->prec);
    c->stride = (size_t)c->bw * c->ss;
    samples   = (uint64_t)c->bw * unit * c->bh * unit;
    if (samples > J_SAMPLES) return false;
    memset(c->bits, -1, sizeof(c->bits));
  }
  return region_size(j);
}

/* a component's coefficients' bytes */
static size_t
coef_size(const jcomp_t *c) {
  return (size_t)c->bw * c->bh * 64u * sizeof(int16_t);
}

/* what decoding the frame touches (IM_OPTION_MEMORY_LIMIT's count): of
   each plane the rows and columns its region's blocks are written to
   (the rest, allocated, is never touched: no page of it made), their
   pages a row; a multi-scan image's coefficients whole (every block's
   are read); the pixels out where im makes them; the scans' data
   unstuffed (the stream's bytes at most), each worker's rows out and
   the tables (a megabyte for them all) */
IM_HIDE uint64_t
jpg_frame_bytes(const jpg_t *j) {
  uint64_t all = 0u, es = j->diff ? 4u : j->prec > 8u ? 2u : 1u;
  uint64_t rows = ((uint64_t)j->hmax * 8u * (j->mcux + 1u) + 16u) * (j->ncomp + 4u) * 2u;
  uint32_t i, n = j->ncomp == 3u && j->rgb_pad ? 4u : j->ncomp;

  all = (uint64_t)(j->end - j->data) + rows * (j->pool ? im_pool_size(j->pool) : 1u) + (4u << 20);

  for (i = 0; i < j->ncomp; i++) {
    const jcomp_t *c   = &j->comp[i];
    uint64_t       row = (uint64_t)c->stride * es, span = (uint64_t)(c->rbx1 - c->rbx0) * c->ss * es;

    if (j->region && !j->hier) span = (span + 2u * 16384u - 1u) / 16384u * 16384u; /* its pages (16K: arm64's), one more */
    else span = row;
    all += (span < row ? span : row) * (uint64_t)(c->rby1 - c->rby0) * c->ss;
    if (j->multiscan) all += coef_size(c);
  }
  if (!j->dest) all += (uint64_t)j->reg_w * j->reg_h * n * (j->prec > 8u ? 2u : 1u);
  return all;
}

/* the planes, and a multi-scan image's coefficients; J_BIG bytes of them
   or more (not a hierarchical frame's), given back as they're done with */
static bool
frame_alloc(jpg_t *j) {
  uint64_t all = 0u;
  uint32_t i;

  for (i = 0; i < j->ncomp; i++) {
    jcomp_t *c = &j->comp[i];

    c->plane_size = c->stride * c->bh * c->ss * (j->diff ? 4u : j->prec > 8u ? 2u : 1u);
    all += c->plane_size + (j->multiscan ? coef_size(c) : 0u);
  }
  if (j->mem_limit && jpg_frame_bytes(j) > j->mem_limit) {
    j->over = true; /* IM_OPTION_MEMORY_LIMIT: nothing made */
    return false;
  }
  j->big = !j->hier && all >= J_BIG;
  for (i = 0; i < j->ncomp; i++) {
    jcomp_t *c = &j->comp[i];

    if (!(c->plane = im_big_alloc(c->plane_size, false, j->big))) return false;
    if (j->multiscan && !(c->coefs = im_big_alloc(coef_size(c), true, j->big))) return false;
  }
  return true;
}

/* -- hierarchical (Annex J) --------------------------------------------------------- */

/* DHP's component of this id; J_COMPS none */
static uint32_t
hier_comp(const jpg_t *j, uint32_t id) {
  uint32_t k;

  for (k = 0; k < j->hncomp && j->hid[k] != id; k++) {
  }
  return k < j->hncomp ? k : J_COMPS;
}

/* DHP (B.3.2): a frame header for the whole sequence, before any frame */
static bool
read_dhp(jpg_t *j, const uint8_t *s, uint32_t len) {
  uint32_t i, k;

  if (j->frame || j->hier || len < 6u) return false;
  j->hprec   = s[0];
  j->hheight = be16(s + 1);
  j->hwidth  = be16(s + 3);
  j->hncomp  = s[5];
  if (!j->hwidth || !j->hheight || !j->hncomp || j->hncomp > J_COMPS || len != 6u + j->hncomp * 3u) return false;
  for (i = 0; i < j->hncomp; i++) {
    j->hid[i] = s[6u + i * 3u];
    j->hh[i]  = s[7u + i * 3u] >> 4;
    j->hv[i]  = s[7u + i * 3u] & 15u;
    if (j->hh[i] < 1u || j->hh[i] > 4u || j->hv[i] < 1u || j->hv[i] > 4u) return false;
    for (k = 0; k < i; k++) {
      if (j->hid[k] == j->hid[i]) return false;
    }
  }
  if ((uint64_t)j->hwidth * j->hheight > J_SAMPLES) return false;
  j->hier = true;
  return true;
}

/* a component's samples twice as wide (J.1.1.2): a new one between each
   two, their mean truncated; the last column repeated for the right edge */
static bool
grow_h(jpg_t *j, uint32_t k) {
  uint32_t  w = j->rw[k], h = j->rh[k], x, y;
  uint16_t *in = j->ref[k], *out = malloc((size_t)w * 2u * h * sizeof(uint16_t));

  if (!out) return false;
  for (y = 0; y < h; y++) {
    const uint16_t *r = in + (size_t)y * w;
    uint16_t       *o = out + (size_t)y * w * 2u;

    for (x = 0; x < w; x++) {
      o[2u * x]      = r[x];
      o[2u * x + 1u] = (uint16_t)(((uint32_t)r[x] + r[x + 1u < w ? x + 1u : x]) >> 1);
    }
  }
  free(in);
  j->ref[k] = out;
  j->rw[k]  = w * 2u;
  return true;
}

/* twice as tall: the bottom row repeated */
static bool
grow_v(jpg_t *j, uint32_t k) {
  uint32_t  w = j->rw[k], h = j->rh[k], x, y;
  uint16_t *in = j->ref[k], *out = malloc((size_t)w * h * 2u * sizeof(uint16_t));

  if (!out) return false;
  for (y = 0; y < h; y++) {
    const uint16_t *r = in + (size_t)y * w, *n = y + 1u < h ? r + w : r;
    uint16_t       *o = out + (size_t)y * 2u * w;

    memcpy(o, r, (size_t)w * sizeof(uint16_t));
    for (x = 0; x < w; x++) o[w + x] = (uint16_t)(((uint32_t)r[x] + n[x]) >> 1);
  }
  free(in);
  j->ref[k] = out;
  j->rh[k]  = h * 2u;
  return true;
}

/* a frame's component into DHP's: its samples (a differential frame's
   added, modulo 2^16, to those before, grown first as EXP said; both
   ways: across, then down) */
static bool
frame_comp(jpg_t *j, const jcomp_t *c) {
  uint32_t  k = hier_comp(j, c->id), w = c->cw, h = c->ch, x, y;
  uint16_t *out;

  if (j->diff) {
    if (!j->ref[k]) return false; /* its first frame never decoded */
    if (((j->grow & 1u) && !grow_h(j, k)) || ((j->grow & 2u) && !grow_v(j, k))) return false;
    if (j->rw[k] < w || j->rh[k] < h) return false;
  }
  if (!(out = malloc((size_t)w * h * sizeof(uint16_t)))) return false;
  for (y = 0; y < h; y++) {
    uint16_t *o = out + (size_t)y * w;

    if (j->diff) {
      const int32_t  *d = (const int32_t *)c->plane + (size_t)y * c->stride;
      const uint16_t *r = j->ref[k] + (size_t)y * j->rw[k];

      for (x = 0; x < w; x++) o[x] = (uint16_t)((uint32_t)r[x] + (uint32_t)d[x]);
    } else if (j->prec <= 8u) {
      const uint8_t *p = (const uint8_t *)c->plane + (size_t)y * c->stride;

      for (x = 0; x < w; x++) o[x] = p[x];
    } else {
      memcpy(o, (const uint16_t *)c->plane + (size_t)y * c->stride, (size_t)w * sizeof(uint16_t));
    }
  }
  free(j->ref[k]);
  j->ref[k] = out;
  j->rw[k]  = w;
  j->rh[k]  = h;
  return true;
}

/* the frame done: its multi-scan blocks to samples, its components into
   DHP's; ready for the next frame */
static bool
frame_done(jpg_t *j) {
  uint32_t i;
  bool     ok = true;

  if (j->comp[0].plane) { /* its first scan came: decoded */
    if (j->multiscan && !idct_all(j)) ok = false;
    for (i = 0; ok && i < j->ncomp; i++) ok = frame_comp(j, &j->comp[i]);
  }
  for (i = 0; i < J_COMPS; i++) {
    im_big_free(j->comp[i].plane, j->comp[i].plane_size, j->big);
    im_big_free(j->comp[i].coefs, coef_size(&j->comp[i]), j->big);
  }
  memset(j->comp, 0, sizeof(j->comp));
  j->frame     = false;
  j->multiscan = false;
  return ok;
}

/* DHP's components as the planes out: each one's last samples (grown to
   its size where the sequence stops short of it), held to the precision
   (past it below zero modulo 2^16: 0); a DCT sequence upsampled as a DCT
   image, a lossless one as a lossless one */
static bool
hier_planes(jpg_t *j) {
  uint32_t i, max = (1u << j->prec) - 1u;

  j->width   = j->hwidth;
  j->height  = j->hheight;
  j->ncomp   = j->hncomp;
  j->process = j->hdct ? J_SEQUENTIAL : J_LOSSLESS;
  j->hmax = j->vmax = 1u;
  for (i = 0; i < j->ncomp; i++) {
    j->comp[i].id = j->hid[i];
    j->comp[i].h  = j->hh[i];
    j->comp[i].v  = j->hv[i];
    if (j->hh[i] > j->hmax) j->hmax = j->hh[i];
    if (j->hv[i] > j->vmax) j->vmax = j->hv[i];
  }
  j->ds   = j->hdct ? 8u : 1u;
  j->ow   = j->width;
  j->oh   = j->height;
  j->mcux = (j->width + 8u * j->hmax - 1u) / (8u * j->hmax);
  for (i = 0; i < j->ncomp; i++) {
    jcomp_t *c = &j->comp[i];
    uint32_t w = (uint32_t)(((uint64_t)j->width * c->h + j->hmax - 1u) / j->hmax);
    uint32_t h = (uint32_t)(((uint64_t)j->height * c->v + j->vmax - 1u) / j->vmax), x, y;

    if (!j->ref[i]) return false;
    while (j->rw[i] < w) {
      if (!grow_h(j, i)) return false;
    }
    while (j->rh[i] < h) {
      if (!grow_v(j, i)) return false;
    }
    c->cw = c->ow = w;
    c->ch = c->oh = h;
    c->uh = c->h;
    c->uv = c->v;
    c->ss = 1u;
    c->stride = w;
    c->plane_size = (size_t)w * h * (j->prec > 8u ? 2u : 1u);
    if (!(c->plane = im_big_alloc(c->plane_size, false, j->big))) return false;
    for (y = 0; y < h; y++) {
      const uint16_t *r = j->ref[i] + (size_t)y * j->rw[i];

      for (x = 0; x < w; x++) {
        uint32_t v = r[x] > max ? (r[x] >= 0x8000u ? 0u : max) : r[x];

        if (j->prec <= 8u) ((uint8_t *)c->plane)[(size_t)y * w + x] = (uint8_t)v;
        else ((uint16_t *)c->plane)[(size_t)y * w + x] = (uint16_t)v;
      }
    }
  }
  return true;
}

static uint32_t marker_at(const uint8_t **pp, const uint8_t *end);

/* the height a DNL gives (B.2.5): the first marker past the first scan's
   data from `p` but its restarts; 0 none */
static uint32_t
dnl_height(const uint8_t *p, const uint8_t *end) {
  for (;;) {
    uint32_t m = marker_at(&p, end);

    if (m >= 0xD0u && m <= 0xD7u) continue;
    if (m != 0xDCu || end - p < 4 || be16(p) != 4u) return 0u;
    return be16(p + 2);
  }
}

/* DQT: 16-bit entries for any precision nibble but 0, as libjpeg */
static bool
read_dqt(jpg_t *j, const uint8_t *s, uint32_t len) {
  uint32_t at = 0u, i;

  while (at < len) {
    uint32_t pq = s[at] >> 4, tq = s[at] & 15u, size = pq ? 129u : 65u;

    if (tq > 3u || at + size > len) return false;
    for (i = 0; i < 64u; i++) j->quant[tq][jpg_natural[i]] = (uint16_t)(pq ? be16(s + at + 1u + i * 2u) : s[at + 1u + i]);
    j->qdef |= (uint8_t)(1u << tq);
    at += size;
  }
  return true;
}

/* DHT: a table that isn't one fails where a scan uses it, as libjpeg */
static bool
read_dht(jpg_t *j, const uint8_t *s, uint32_t len) {
  uint32_t at = 0u, i;

  while (len - at > 16u) {
    uint32_t   tc = s[at] >> 4, th = s[at] & 15u, total = 0u;
    jhuff_t  **slot;

    for (i = 0; i < 16u; i++) total += s[at + 1u + i];
    if (total > 256u || total > len - at - 17u || tc > 1u || th > 3u) return false;
    slot = tc ? &j->ac[th] : &j->dc[th];
    if (!*slot) {
      if (!(*slot = malloc(sizeof(**slot)))) return false;
      (*slot)->pair    = NULL; /* the rest the build sets */
      (*slot)->pair_ac = NULL;
    }
    jpg_huff_build(*slot, s + at + 1u, s + at + 17u, !tc, true);
    at += 17u + total;
  }
  return at == len;
}

static void
read_app(jpg_t *j, uint32_t m, const uint8_t *s, uint32_t len) {
  if (m == 0xE0u && len >= 14u && !memcmp(s, "JFIF", 5)) {
    j->jfif         = true;
    j->density_unit = s[7];
    j->density[0]   = (uint16_t)be16(s + 8);
    j->density[1]   = (uint16_t)be16(s + 10);
  } else if (m == 0xEEu && len >= 12u && !memcmp(s, "Adobe", 5)) {
    j->adobe     = true;
    j->transform = s[11];
  } else if (m == 0xE2u && len >= 14u && !memcmp(s, "ICC_PROFILE", 12)) {
    /* a chunk of the profile: kept by its sequence number, joined at the
       end (libjpeg's jpeg_read_icc_profile and its checks) */
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

/* the profile's chunks in order; none if one is missing */
static void
join_icc(jpg_t *j) {
  uint32_t k;
  size_t   total = 0u, at = 0u;

  if (j->icc_bad || !j->icc_count) return;
  for (k = 1; k <= j->icc_count; k++) {
    if (!j->icc_part[k]) return;
    total += j->icc_part_len[k];
  }
  if (!total || !(j->icc = malloc(total))) return;
  for (k = 1; k <= j->icc_count; k++) {
    memcpy(j->icc + at, j->icc_part[k], j->icc_part_len[k]);
    at += j->icc_part_len[k];
  }
  j->icc_len = total;
}

/* SOS, libjpeg's checks (get_sos, start_pass): the components there and
   once each, their tables defined, a progression that can be one */
static bool
read_sos(jpg_t *j, const uint8_t *s, uint32_t len) {
  uint32_t n, i, k;

  if (!j->frame || len < 1u) return false;
  n = s[0];
  if (len != n * 2u + 4u || n < 1u || n > 4u) return false;
  for (i = 0; i < n; i++) {
    for (k = 0; k < j->ncomp && j->comp[k].id != s[1u + i * 2u]; k++) {
    }
    if (k == j->ncomp) return false;
    for (uint32_t p = 0; p < i; p++) {
      if (j->scomp[p] == k) return false;
    }
    j->scomp[i]    = k;
    j->comp[k].td  = s[2u + i * 2u] >> 4;
    j->comp[k].ta  = s[2u + i * 2u] & 15u;
  }
  j->nscomp = n;
  for (i = 0; i < n; i++) {
    jcomp_t *c = &j->comp[j->scomp[i]];

    if (!c->latched) {
      /* its table there by the first scan it's in, as libjpeg wants
         (lossless has none) */
      if (j->process != J_LOSSLESS && !(j->qdef >> c->tq & 1u)) return false;
      memcpy(c->q, j->quant[c->tq], sizeof(j->quant[0]));
      jpg_qlim(c->q);
      c->latched = true;
    }
  }
  j->ss     = s[1u + n * 2u];
  j->se     = s[2u + n * 2u];
  j->ah     = s[3u + n * 2u] >> 4;
  j->al     = s[3u + n * 2u] & 15u;

  if (j->process == J_LOSSLESS) {
    /* Ss the predictor, Al the point transform (libjpeg's checks); a
       differential frame's no predictor (J.1.3.2: 0) */
    if ((j->diff ? j->ss != 0u : j->ss < 1u || j->ss > 7u) || j->se != 0u || j->ah != 0u || j->al >= j->prec) return false;
    for (i = 0; i < n; i++) {
      const jcomp_t *c = &j->comp[j->scomp[i]];

      if (c->td > 3u || (!j->arith && (!j->dc[c->td] || !j->dc[c->td]->ok))) return false;
    }
    return true;
  }
  if (j->arith) {
    bool dc = j->ss == 0u;

    if (j->process == J_PROGRESSIVE) {
      if ((dc && j->se != 0u) || (!dc && (j->ss > j->se || j->se > 63u || n != 1u)) || (j->ah && j->al != j->ah - 1u)
          || j->al > 13u) {
        return false;
      }
      for (i = 0; i < n; i++) {
        jcomp_t *c = &j->comp[j->scomp[i]];

        for (k = j->ss < 1u ? j->ss : 1u; k <= (j->se > 9u ? j->se : 9u) && k < 10u; k++) {
          if (k) c->prev[k] = j->nscans + 1u > 1u ? c->bits[k] : 0;
        }
        for (k = j->ss; k <= j->se; k++) c->bits[k] = (int8_t)j->al;
      }
    } else {
      j->ss = 0u;
    j->se = 63u;
    j->ah = 0u;
    j->al = 0u;
    }
    for (i = 0; i < n; i++) {
      const jcomp_t *c = &j->comp[j->scomp[i]];

      if (c->td > 15u || c->ta > 15u) return false;
    }
    return true;
  }
  if (j->process == J_PROGRESSIVE) {
    bool dc = j->ss == 0u;

    if ((dc && j->se != 0u) || (!dc && (j->ss > j->se || j->se > 63u || n != 1u)) || (j->ah && j->al != j->ah - 1u)
        || j->al > 13u) {
      return false;
    }
    for (i = 0; i < n; i++) {
      jcomp_t *c = &j->comp[j->scomp[i]];

      if (dc && !j->ah && (c->td > 3u || !j->dc[c->td] || !j->dc[c->td]->ok || j->dc[c->td]->maxsym > 15u)) return false;
      if (!dc && (c->ta > 3u || !j->ac[c->ta] || !j->ac[c->ta]->ok)) return false;
      for (k = j->ss < 1u ? j->ss : 1u; k <= (j->se > 9u ? j->se : 9u) && k < 10u; k++) {
        if (k) c->prev[k] = j->nscans + 1u > 1u ? c->bits[k] : 0;
      }
      for (k = j->ss; k <= j->se; k++) c->bits[k] = (int8_t)j->al;
    }
  } else {
    j->ss = 0u;
    j->se = 63u;
    j->ah = 0u;
    j->al = 0u;
    for (i = 0; i < n; i++) {
      const jcomp_t *c = &j->comp[j->scomp[i]];

      if (c->td > 3u || c->ta > 3u || !j->dc[c->td] || !j->dc[c->td]->ok || !j->ac[c->ta] || !j->ac[c->ta]->ok) return false;
      if (j->dc[c->td]->maxsym > 15u) return false;
    }
  }
  return true;
}

/* -- the stream -------------------------------------------------------------------- */

/* the marker at or after `p` (anything before it skipped), its code; at the
   data's end EOI (libjpeg's next_marker) */
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

static void free_deferred(jpg_t *j);

static void
jpg_free(jpg_t *j) {
  uint32_t i;

  for (i = 0; i < J_COMPS; i++) {
    im_big_free(j->comp[i].plane, j->comp[i].plane_size, j->big);
    im_big_free(j->comp[i].coefs, coef_size(&j->comp[i]), j->big);
    free(j->ref[i]);
  }
  for (i = 0; i < 4u; i++) {
    jpg_huff_free(j->dc[i]);
    jpg_huff_free(j->ac[i]);
  }
  free(j->icc);
  free(j->sd.buf);
  free(j->sd.seg);
  free_deferred(j);
}

/* the scan whose data is unstuffed in `j->sd`, as its process codes it */
static bool
scan(jpg_t *j) {
  if (j->process == J_LOSSLESS) return j->arith ? jpg_scan_arith_lossless(j, j->pool) : jpg_scan_lossless(j, j->pool);
  return j->arith ? jpg_scan_arith(j) : jpg_scan_huff(j, j->pool);
}

/* -- a multi-scan image's scans on threads -------------------------------------- */

/* Each scan kept with its own data and the tables it reads (a DHT may
   change them between scans), then all decoded at once, row by row: a
   scan waits, before each of its rows, for the earlier ones sharing a
   component and an overlapping band of coefficients (a refinement needs
   what came before; disjoint bands of a block are apart) to have done
   that row. Handed out in stream order, a scan only waits for one that's
   running. In order as far as anything shows: each scan its own state,
   the last one's last good row the image's. At most J_BATCH at a time
   (their dependencies a mask), more after those are done. */
/* the row-by-row waits need locks: without a thread layer (a caller's
   runner then) the scans go in order, each on the threads its own way */
#if IM_NO_THREADS
#  define J_FLOW 0
#else
#  define J_FLOW 1
#endif

enum { J_BATCH = 64 };

typedef struct jdeferred {
  jpg_t   *copy;    /* the scan as its SOS left the decoder */
  jhuff_t *own[8];  /* its tables' copies */
} jdeferred_t;

typedef struct jflow {
  th_thread_mutex m;
  th_thread_cond  cv;
  jpg_t          *j;
  uint32_t        waiting;
  uint32_t        done[J_BATCH]; /* iMCU rows each has finished */
  /* the last batch's: its blocks' IDCTs after its scans, as items too, a
     band of iMCU rows of each component once the slowest scan has passed
     them; woken (their own) only when it reaches the first one waiting */
  th_thread_cond  tail_cv;
  jidct_t        *idct;
  uint32_t        nscan;
  uint32_t        least;     /* the slowest scan's rows done */
  uint32_t        tail_need; /* the least a waiting band needs past */
  uint32_t        tail_band; /* iMCU rows a band */
  uint32_t        tail_rows; /* iMCU rows in all */
  uint32_t        nband;
  /* and the pixels out, a band's rows after its IDCTs and the next band's
     (their context): items in turn, a band's IDCTs then the one before's
     rows */
  jsout_t        *out;
  uint8_t        *made;      /* a band's components done */
  uint32_t        lead;      /* the bands done from the first */
  uint32_t        out_need;  /* the least iMCU row a waiting band's rows need */
} jflow_t;

static bool scan(jpg_t *j);
static bool run_deferred(jpg_t *j, bool last);

/* the coefficients a scan's blocks touch: its band; a Huffman AC first
   scan's up to 15 past it on damage (a run lands there, as libjpeg's); a
   refinement's whole block (the Huffman one reads it for its mask, the
   arithmetic one below its band for the last nonzero) */
static uint32_t
touch_lo(const jpg_t *s) {
  return s->ss && s->ah ? 0u : s->ss;
}

static uint32_t
touch_hi(const jpg_t *s) {
  if (s->ss && s->ah) return 63u;
  if (s->ss && !s->arith) return s->se + 15u < 63u ? s->se + 15u : 63u;
  return s->se;
}

static void
free_copy(jdeferred_t *d) {
  uint32_t k;

  for (k = 0; k < 8u; k++) jpg_huff_free(d->own[k]);
  if (d->copy) {
    free(d->copy->sd.buf);
    free(d->copy->sd.seg);
    free(d->copy);
  }
  memset(d, 0, sizeof(*d));
}

/* the scan whose SOS was just read, from `p`: kept (`next`: where the
   next marker is) */
static bool
defer_scan(jpg_t *j, const uint8_t *p, const uint8_t *end, const uint8_t **next) {
  jdeferred_t *d;
  jpg_t       *c;
  uint32_t     i, nown = 0u;

  if (!j->defer && !(j->defer = calloc(J_BATCH, sizeof(jdeferred_t)))) return false;
  d = &j->defer[j->ndefer];
  if (!(c = d->copy = malloc(sizeof(*c)))) return false;
  *c = *j;
  memset(&c->sd, 0, sizeof(c->sd));
  c->pool      = NULL;
  c->defer     = NULL;
  c->icc       = NULL;
  c->flow_deps = 0u;
  c->flow_self = j->ndefer;
  if (!jpg_unstuff(&c->sd, p, end)) goto fail;
  if (next) *next = c->sd.next;
  /* the tables it reads, copied */
  for (i = 0; i < c->nscomp && !c->arith; i++) {
    jcomp_t  *k    = &c->comp[c->scomp[i]];
    jhuff_t **t[2] = { &c->dc[k->td & 3u], &c->ac[k->ta & 3u] };
    uint32_t  w;

    for (w = 0; w < 2u; w++) {
      jhuff_t *orig = *t[w], *dup;
      uint32_t o;
      bool     have = false;

      if (!orig) continue;
      for (o = 0; o < nown; o++) have = have || d->own[o] == orig;
      if (have) continue;
      if (!(dup = malloc(sizeof(*dup)))) goto fail;
      *dup = *orig;
      dup->pair    = NULL; /* the original's own */
      dup->pair_ac = NULL;
      /* every slot of its kind the scan has pointing at it: at the copy */
      for (o = 0; o < 4u; o++) {
        if (w == 0u && c->dc[o] == orig) c->dc[o] = dup;
        if (w == 1u && c->ac[o] == orig) c->ac[o] = dup;
      }
      d->own[nown++] = dup;
    }
  }
  /* what it waits for */
  for (i = 0; i < j->ndefer; i++) {
    const jpg_t *e     = j->defer[i].copy;
    bool         share = false;
    uint32_t     a, b;

    for (a = 0; a < c->nscomp; a++) {
      for (b = 0; b < e->nscomp; b++) share = share || c->scomp[a] == e->scomp[b];
    }
    if (share && touch_lo(c) <= touch_hi(e) && touch_lo(e) <= touch_hi(c)) c->flow_deps |= 1ull << i;
  }
  if (++j->ndefer == J_BATCH) return run_deferred(j, false);
  return true;

fail:
  free_copy(d);
  return false;
}

/* (locked) the slowest scan's rows again; the bands waiting woken when it
   reaches the first one's */
static void
flow_least(jflow_t *f) {
  uint32_t i, least = UINT32_MAX;

  for (i = 0; i < f->nscan; i++) least = f->done[i] < least ? f->done[i] : least;
  f->least = least;
  if (least > f->tail_need) {
    f->tail_need = UINT32_MAX;
    thread_cond_broadcast(&f->tail_cv);
  }
}

/* before scan `j`'s MCU row `row`: the rows before it done, the earlier
   scans it needs past it */
IM_HIDE void
jpg_flow_row(jpg_t *j, uint32_t row) {
  jflow_t *f    = j->flow;
  uint32_t imcu = j->nscomp == 1u ? row / j->comp[j->scomp[0]].v : row, i;

  thread_lock(&f->m);
  if (f->done[j->flow_self] < imcu) {
    f->done[j->flow_self] = imcu;
    if (f->waiting) thread_cond_broadcast(&f->cv);
    if (f->idct) flow_least(f);
  }
  for (;;) {
    uint64_t deps  = j->flow_deps;
    bool     ready = true;

    for (i = 0; deps; i++, deps >>= 1) ready = ready && (!(deps & 1u) || f->done[i] > imcu);
    if (ready) break;
    f->waiting++;
    thread_cond_wait(&f->cv, &f->m);
    f->waiting--;
  }
  thread_unlock(&f->m);
}

/* an IDCT band past the scans (band b of component ci, item k): once the
   slowest has passed its iMCU rows (smoothing: all done, the last one's
   last good row known) */
static bool
flow_idct(jflow_t *f, uint32_t b, uint32_t ci) {
  jpg_t         *j  = f->j;
  uint32_t       r0 = b * f->tail_band, r1, need;
  const jcomp_t *c  = &j->comp[ci];

  r1   = r0 + f->tail_band < f->tail_rows ? r0 + f->tail_band : f->tail_rows;
  need = f->idct->smooth ? UINT32_MAX - 1u : r1 - 1u;
  thread_lock(&f->m);
  while (f->least <= need) {
    f->tail_need = need < f->tail_need ? need : f->tail_need;
    thread_cond_wait(&f->tail_cv, &f->m);
  }
  thread_unlock(&f->m);
  if (r0 * c->v < c->sh) idct_rows(f->idct, ci, r0 * c->v, r1 * c->v < c->sh ? r1 * c->v : c->sh);
  if (f->out) {
    /* the band done once all its components are: the rows out waiting
       for it woken */
    thread_lock(&f->m);
    if (++f->made[b] == j->ncomp) {
      while (f->lead < f->nband && f->made[f->lead] == j->ncomp) f->lead++;
      if ((f->lead == f->nband ? UINT32_MAX : f->lead * f->tail_band) > f->out_need) {
        f->out_need = UINT32_MAX;
        thread_cond_broadcast(&f->tail_cv);
      }
    }
    thread_unlock(&f->m);
  }
  return true;
}

/* band b's rows out, once the IDCTs of its iMCU rows and the next one
   (the context a row's upsampling reads) are done */
static void
flow_out(jflow_t *f, uint32_t worker, uint32_t b) {
  jpg_t   *j = f->j;
  uint32_t per = j->ds * j->vmax, y0 = b * f->tail_band * per, y1 = y0 + f->tail_band * per, need;

  if (b + 1u == f->nband || y1 > j->oh) y1 = j->oh;
  if (y0 >= y1) return;
  need = (y1 - 1u + j->vmax) / per;
  need = need < f->tail_rows ? need : f->tail_rows - 1u;
  thread_lock(&f->m);
  while ((f->lead == f->nband ? UINT32_MAX : f->lead * f->tail_band) <= need) {
    f->out_need = need < f->out_need ? need : f->out_need;
    thread_cond_wait(&f->tail_cv, &f->m);
  }
  thread_unlock(&f->m);
  jpg_out_rows(f->out, worker, y0, y1);
}

static bool
flow_item(void *arg, uint32_t worker, uint32_t item) {
  jflow_t *f = arg;
  bool     ok;

  if (item >= f->nscan) {
    uint32_t k = item - f->nscan, nc = f->j->ncomp, g, p;

    if (!f->out) return flow_idct(f, k / nc, k % nc);
    /* in turn: band 0's IDCTs; then each band's IDCTs and the one
       before's rows; the last band's rows */
    if (k < nc) return flow_idct(f, 0u, k);
    g = 1u + (k - nc) / (nc + 1u);
    p = (k - nc) % (nc + 1u);
    if (p) return flow_idct(f, g, p - 1u);
    flow_out(f, worker, g - 1u);
    return true;
  }
  ok = scan(f->j->defer[item].copy);
  /* done, or given up: nothing waits for it any more (the last one's last
     good row known to smoothing first) */
  thread_lock(&f->m);
  if (item + 1u == f->nscan) f->j->last_good = f->j->defer[item].copy->last_good;
  f->done[item] = UINT32_MAX;
  if (f->waiting) thread_cond_broadcast(&f->cv);
  if (f->idct) flow_least(f);
  thread_unlock(&f->m);
  return ok;
}

static bool
run_deferred(jpg_t *j, bool last) {
  uint32_t i, n = j->ndefer;
  bool     ok;

  if (!n) return true;
  if (n == 1u) {
    /* alone: the threads for it (its restart intervals) */
    jpg_t *c = j->defer[0].copy;

    c->pool = j->pool;
    ok      = scan(c);
  } else {
    jflow_t  f;
    jidct_t  t;
    uint32_t tail = 0u;

    memset(&f, 0, sizeof(f));
    f.j         = j;
    f.nscan     = n;
    f.tail_need = UINT32_MAX;
    /* the last batch's IDCTs behind its scans, by bands of iMCU rows, a
       few a worker, each component's in turn */
    if (last && j->multiscan && !j->hier) {
      idct_plan(j, &t);
      f.idct = &t;
      for (i = 0; i < j->ncomp; i++) {
        uint32_t r = (j->comp[i].sh + j->comp[i].v - 1u) / j->comp[i].v;

        f.tail_rows = r > f.tail_rows ? r : f.tail_rows;
      }
      f.tail_band  = f.tail_rows / (im_pool_size(j->pool) * 4u);
      f.tail_band  = f.tail_band ? f.tail_band : 1u;
      f.nband      = (f.tail_rows + f.tail_band - 1u) / f.tail_band;
      tail         = f.nband * j->ncomp;
      j->idct_done = true;
      /* the rows out with them, where they're made ready */
      if (j->out && (f.made = calloc(f.nband, 1u))) {
        f.out       = j->out;
        f.out_need  = UINT32_MAX;
        tail       += f.nband;
        j->out_done = true;
      }
    }
    thread_mutex_init(&f.m);
    thread_cond_init(&f.cv);
    thread_cond_init(&f.tail_cv);
    for (i = 0; i < n; i++) j->defer[i].copy->flow = &f;
    ok = im_pool_run(j->pool, flow_item, &f, n + tail);
    thread_cond_destroy(&f.tail_cv);
    thread_cond_destroy(&f.cv);
    thread_mutex_destroy(&f.m);
    free(f.made);
  }
  j->last_good = j->defer[n - 1u].copy->last_good;
  for (i = 0; i < n; i++) free_copy(&j->defer[i]);
  j->ndefer = 0u;
  return ok;
}

static void
free_deferred(jpg_t *j) {
  uint32_t i;

  if (!j->defer) return;
  for (i = 0; i < j->ndefer; i++) free_copy(&j->defer[i]);
  free(j->defer);
  j->defer  = NULL;
  j->ndefer = 0u;
}

/* the stream's byte `k` from `p` (`have` there): past its end libjpeg's
   fake EOIs, FF D9 again and again */
IM_INLINE uint8_t
virt(const uint8_t *p, size_t have, size_t k) {
  return k < have ? p[k] : ((k - have) & 1u) ? 0xD9u : 0xFFu;
}

/* the colour space as libjpeg guesses it (jdapimin.c) */
static jspace_t
guess_space(const jpg_t *j) {
  switch (j->ncomp) {
  case 1:
    return J_GRAY;
  case 3:
    if (j->jfif) return J_YCC;
    if (j->adobe) return j->transform == 0u ? J_RGB : J_YCC;
    if (j->comp[0].id == 1u && j->comp[1].id == 2u && j->comp[2].id == 3u) return j->process == J_LOSSLESS ? J_RGB : J_YCC;
    if (j->comp[0].id == 82u && j->comp[1].id == 71u && j->comp[2].id == 66u) return J_RGB;
    return j->process == J_LOSSLESS ? J_RGB : J_YCC;
  case 4:
    if (j->adobe) return j->transform == 0u ? J_CMYK : J_YCCK;
    return J_CMYK;
  default:
    return J_OTHER;
  }
}

/* the colours: as guessed, or the caller's word (a container's flag)
   on whether three or four components are transformed */
static jspace_t
space_of(const jpg_t *j) {
  if (j->want_transform >= 0 && j->ncomp == 3u) return j->want_transform ? J_YCC : J_RGB;
  if (j->want_transform >= 0 && j->ncomp == 4u) return j->want_transform ? J_YCCK : J_CMYK;
  return guess_space(j);
}

static bool decode_stream(jpg_t *j, ImImage *im, ImByteOrder order, uint8_t **pad);

/* `j`'s stream read through on its own, its scans' data passed over and
   nothing frame-sized made: whether its decode would be refused */
static bool
dry_run(const jpg_t *j) {
  jpg_t   *d   = calloc(1, sizeof(*d));
  uint8_t *pad = NULL;
  bool     ok;

  if (!d) return false;
  d->data           = j->data;
  d->end            = j->end;
  d->want_transform = j->want_transform;
  d->rgb_pad        = j->rgb_pad;
  d->dest           = j->dest;
  d->dest_stride    = j->dest_stride;
  d->dest_size      = j->dest_size;
  d->scale          = j->scale;
  d->region         = j->region; /* the caller's memory is the region's */
  d->reg_x          = j->reg_x;
  d->reg_y          = j->reg_y;
  d->reg_w          = j->reg_w;
  d->reg_h          = j->reg_h;
  d->dry            = true;
  ok                = decode_stream(d, NULL, IM_BYTEORDER_ANY, &pad);
  free(pad);
  jpg_free(d);
  free(d);
  return ok;
}

static bool
decode(jpg_t *j, ImImage *im, ImByteOrder order) {
  uint8_t *pad = NULL;
  bool     ok  = decode_stream(j, im, order, &pad);

  free(pad);
  return ok;
}

static bool
decode_stream(jpg_t *j, ImImage *im, ImByteOrder order, uint8_t **padp) {
  const uint8_t *p = j->data, *end = j->end;
  bool           first = true, done = false, tail = false, odd = false;

  if (end - p < 4 || p[0] != 0xFFu || p[1] != 0xD8u) return false;
  p += 2;
  memset(j->dc_l, 0, sizeof(j->dc_l));
  memset(j->dc_u, 1, sizeof(j->dc_u));
  memset(j->ac_k, 5, sizeof(j->ac_k));
  while (!done) {
    uint32_t       m = marker_at(&p, end), len;
    const uint8_t *s;

    if (m == 0xD9u) {
      j->used = (size_t)(p - j->data); /* past the EOI (or the data's end) */
      break;
    }
    if ((m >= 0xD0u && m <= 0xD7u) || m == 0x01u) continue; /* parameterless */
    {
      size_t have = (size_t)(end - p);

      len = (uint32_t)virt(p, have, 0u) << 8 | virt(p, have, 1u);
      if (len < 2u) break;
      if (have < len) {
        /* cut short: read on as libjpeg does, its fake EOIs' bytes (FF D9
           FF D9 ...) for the rest; an odd count of them leaves a D9 for
           what comes next (a scan's one byte of data) */
        size_t   k;
        uint8_t *pad;

        free(*padp);
        if (!(*padp = pad = malloc(len))) return false;
        for (k = 0; k < len; k++) pad[k] = virt(p, have, k);
        s    = pad + 2;
        odd  = (len - have) & 1u;
        p    = end;
        tail = true;
      } else {
        s = p + 2;
        p += len;
      }
    }
    len -= 2u;
    /* cut inside a segment after a scan: the end there, as if EOI came (as
       a viewer that stops at the error shows it; where libjpeg reads such a segment's fake bytes
       without failing, nothing after it decodes either) */
    if (tail && m != 0xDAu && j->nscans) break;

    switch (m) {
    case 0xC0: case 0xC1: case 0xC2: case 0xC3:
    case 0xC9: case 0xCA: case 0xCB:
    case 0xC5: case 0xC6: case 0xC7: case 0xCD: case 0xCE: case 0xCF:
      if (!read_sof(j, m, s, len)) return false;
      first = true; /* a hierarchical sequence's next frame */
      break;
    case 0xDE:
      /* DHP before any frame; one past it, or one that doesn't parse, is
         passed over as other markers are (a sequence's frames then refused
         past the first) */
      if (!j->frame && !j->hier) (void)read_dhp(j, s, len);
      break;
    case 0xDF: /* EXP (B.3.3): the next frame's references grown */
      if (!j->hier) break; /* none to grow: passed over */
      if ((j->frame && first) || len != 1u) return false; /* not inside a frame's header */
      j->expand = (uint8_t)((s[0] >> 4 ? 1u : 0u) | (s[0] & 15u ? 2u : 0u));
      break;
    case 0xC4:
      if (!read_dht(j, s, len)) return false;
      break;
    case 0xDB:
      if (!read_dqt(j, s, len)) return false;
      break;
    case 0xCC: { /* DAC */
      uint32_t k;

      if (len & 1u) return false;
      for (k = 0; k < len; k += 2u) {
        uint32_t index = s[k], val = s[k + 1u];

        if (index >= 32u) return false;
        if (index >= 16u) {
          j->ac_k[index - 16u] = (uint8_t)val;
        } else {
          j->dc_l[index] = (uint8_t)(val & 15u);
          j->dc_u[index] = (uint8_t)(val >> 4);
          if (j->dc_l[index] > j->dc_u[index]) return false;
        }
      }
      break;
    }
    case 0xDD:
      if (len != 2u) return false;
      j->restart = be16(s);
      break;
    case 0xDA:
      if (first) {
        jpg_huff_std(j);
        if (!j->frame) return false;
        /* the height a DNL gives, where the frame gave none */
        if (!j->height && (tail || !(j->height = dnl_height(p, end)) || !frame_size(j))) return false;
      }
      if (!read_sos(j, s, len) || (first && !j->hier && !jpg_out_ok(j, space_of(j)))) return false;
      if (j->dry) {
        /* the structure alone: the data passed over */
        if (!tail) p = jpg_scan_end(p, end);
        first = false;
        j->nscans++;
        break;
      }
      if (first) {
        /* little data for so many pixels: the stream read through first, so
           one refused further on is refused before anything frame-sized is
           made or decoded (as for a big frame cut or damaged early) */
        if ((uint64_t)j->width * j->height > (uint64_t)J_DRY * (uint64_t)(j->end - j->data) && !dry_run(j)) {
          return false;
        }
        /* threads: one for each J_POOL_MIN pixels, no more than asked (below
           that a thread's start and its share's edges cost more than it
           saves); the caller's runner from J_POOL_MIN on; a hierarchical
           sequence on one */
        if (!j->hier && !j->pool) {
          uint64_t px = (uint64_t)j->width * j->height, n = px / J_POOL_MIN;

          if (j->runner.run) {
            if (n) j->pool = im_pool_wrap(&j->runner);
          } else {
            if (n > j->threads) n = j->threads;
            if (n > 1u) j->pool = im_pool_new((uint32_t)n);
          }
        }
        j->multiscan = j->process != J_LOSSLESS && (j->process == J_PROGRESSIVE || j->nscomp < j->ncomp);
        if (!frame_alloc(j)) return false;
        first = false;
      } else if (!j->multiscan && j->process != J_LOSSLESS) {
        /* one pass: later scans aren't read */
        if (tail) break;
        if (!jpg_unstuff(&j->sd, p, end)) return false;
        p = j->sd.next;
        break;
      }
      if (tail) {
        static const uint8_t fake[3] = { 0xD9u, 0xFFu, 0xD9u };

        if (j->pool && j->multiscan && J_FLOW) {
          if (!defer_scan(j, odd ? fake : fake + 1, fake + 3, NULL)) return false;
        } else if (!jpg_unstuff(&j->sd, odd ? fake : fake + 1, fake + 3) || !scan(j)) {
          return false;
        }
        j->nscans++;
        break;
      }
      if (j->pool && j->multiscan && J_FLOW) {
        /* kept for the waves; where the next marker is */
        if (!defer_scan(j, p, end, &p)) return false;
        j->nscans++;
        break;
      }
      if (!jpg_unstuff(&j->sd, p, end) || !scan(j)) return false;
      j->nscans++;
      p = j->sd.next;
      break;
    default:
      if (m >= 0xE0u && m <= 0xEFu) read_app(j, m, s, len);
      break;
    }
  }
  if (!j->nscans) return false;
  if (j->dry) return true;
  if (j->hier) {
    if ((j->frame && !frame_done(j)) || !hier_planes(j)) return false;
  } else {
    bool ok;

    /* a multi-scan image's pixels out with its last scans where they're
       on threads (jpg_out's way split: made ready, the rows, im) */
    if (j->ndefer > 1u && !(j->out = jpg_out_begin(j, space_of(j), order))) return false;
    ok = !j->ndefer || run_deferred(j, true);
    ok = ok && (!j->multiscan || j->idct_done || idct_all(j));
    if (j->out) {
      jsout_t *o = j->out;

      j->out = NULL;
      if (!jpg_out_end(o, ok ? im : NULL, !j->out_done)) return false;
    } else if (!ok || !jpg_out(j, space_of(j), im, order)) {
      return false;
    }
  }
  if (j->hier && !jpg_out(j, space_of(j), im, order)) return false;

  /* the profile, the density (pixels a metre) */
  join_icc(j);
  if (j->icc && j->icc_len) {
    im->iccProfile     = j->icc;
    im->iccProfileSize = j->icc_len;
    j->icc             = NULL;
  }
  if (j->jfif && (j->density_unit == 1u || j->density_unit == 2u) && j->density[0] && j->density[1]
      && (im->physicalDim = calloc(1, sizeof(*im->physicalDim)))) {
    double per = j->density_unit == 1u ? 100.0 / 2.54 : 100.0;

    im->physicalDim->pixelsPerUnitX = (uint32_t)(j->density[0] * per + 0.5);
    im->physicalDim->pixelsPerUnitY = (uint32_t)(j->density[1] * per + 0.5);
    im->physicalDim->unit           = 1u;
  }
  return true;
}

/* the frame header's word, and the DNL's where it gives no height; a
   hierarchical sequence's DHP's (its first frame's coding) */
IM_HIDE bool
jpg_info(const uint8_t *data, size_t len, ImInfo *info) {
  const uint8_t *p = data, *end = data + len;
  bool           sof = false, dhp = false;

  memset(info, 0, sizeof(*info));
  if (len < 4u || p[0] != 0xFFu || p[1] != 0xD8u) return false;
  p += 2;
  for (;;) {
    uint32_t m = marker_at(&p, end), l;

    if (m == 0xD9u || end - p < 2) return false;
    if ((m >= 0xD0u && m <= 0xD7u) || m == 0x01u) continue;
    l = be16(p);
    if (l < 2u || (size_t)(end - p) < l) return false;
    if (m == 0xDEu && !sof && !dhp) {
      if (l < 8u) return false;
      info->bitsPerComponent = p[2];
      info->height           = be16(p + 3);
      info->width            = be16(p + 5);
      info->components       = p[7];
      dhp                    = true;
    } else if (m >= 0xC0u && m <= 0xCFu && m != 0xC4u && m != 0xC8u && m != 0xCCu && !sof) {
      const uint8_t *s = p + 2;
      uint32_t       kind = m & 3u;

      if (l < 8u) return false;
      info->fileFormatType   = IM_FILEFORMATTYPE_JPEG;
      info->arithmetic       = m >= 0xC8u;
      sof                    = true;
      if (dhp) {
        info->coding = IM_CODING_HIERARCHICAL;
        p += l;
        continue;
      }
      info->bitsPerComponent = s[0];
      info->height           = be16(s + 1);
      info->width            = be16(s + 3);
      info->components       = s[5];
      info->coding           = (m & 4u) ? IM_CODING_HIERARCHICAL
                             : kind == 3u ? IM_CODING_LOSSLESS
                             : kind == 2u ? IM_CODING_PROGRESSIVE
                             : m == 0xC0u ? IM_CODING_BASELINE : IM_CODING_EXTENDED;
    } else if (m == 0xDAu) {
      if (!sof) return false;
      /* no height: the DNL after the first scan */
      if (!info->height) info->height = dnl_height(p + l, end);
      return info->height != 0u && info->width != 0u;
    }
    p += l;
  }
}

/* `data` decoded into `im` */
IM_HIDE bool
jpg_decode_mem(const uint8_t *data, size_t len, ImImage *im, const jopts_t *o) {
  jpg_t *j;
  bool   ok;

  if (!(j = calloc(1, sizeof(*j)))) return false;
  j->data           = data;
  j->end            = data + len;
  j->threads        = o->threads ? o->threads : IM_POOL_MAX;
  j->want_transform = o->transform;
  j->cmyk_stored    = o->cmyk_stored;
  j->rgb_pad        = o->rgb_pad;
  j->runner         = o->runner;
  j->dest           = o->dest;
  j->dest_stride    = o->dest_stride;
  j->dest_size      = o->dest_size;
  j->scale          = o->scale == 2u || o->scale == 4u || o->scale == 8u ? o->scale : 1u;
  j->region         = o->region;
  j->reg_x            = o->rx;
  j->reg_y            = o->ry;
  j->reg_w            = o->rw;
  j->reg_h            = o->rh;
  j->mem_limit        = o->mem_limit;
  ok                = decode(j, im, o->order);
  if (o->over) *o->over = j->over;
  if (ok && o->used) *o->used = j->used ? j->used : len;
  im_pool_free(j->pool);
  jpg_free(j);
  free(j);
  return ok;
}

static jopts_t
opts_of(const im_open_config_t *c) {
  jopts_t o;

  o.order       = c->byteOrder;
  o.threads     = c->threads;
  o.transform   = c->jpegTransform < 0 ? -1 : c->jpegTransform != 0;
  o.cmyk_stored = c->cmykAsStored;
  o.rgb_pad     = c->rgbPadded;
  o.runner      = c->runner;
  o.dest        = c->dest;
  o.dest_stride = c->destStride;
  o.dest_size   = c->destSize;
  o.scale       = c->scale;
  o.region      = c->region;
  o.rx          = c->regionX;
  o.ry          = c->regionY;
  o.rw          = c->regionW;
  o.rh          = c->regionH;
  o.mem_limit   = c->memoryLimit;
  o.over        = NULL;
  o.used        = c->bytesRead;
  return o;
}

/* decoded into a new image; NULL where it won't */
static ImImage*
dec(const uint8_t *data, size_t len, const im_open_config_t *conf, ImResult *res) {
  jopts_t  o  = opts_of(conf);
  ImImage *im = calloc(1, sizeof(*im));
  bool     over = false;

  o.over = &over;
  *res   = IM_EBADF;
  if (im && !jpg_decode_mem(data, len, im, &o)) {
    *res = over ? IM_ENOMEM : IM_EBADF;
    free(im->data.data);
    free(im->iccProfile);
    free(im->physicalDim);
    free(im);
    return NULL;
  }
  if (im) {
    im->openIntent     = conf->openIntent;
    im->ori            = IM_ORIENTATION_UP;
    im->fileFormatType = IM_FILEFORMATTYPE_JPEG;
  }
  return im;
}

IM_HIDE
ImResult
jpg_dec_mem(ImImage               ** __restrict dest,
            const uint8_t          * __restrict data,
            size_t                              len,
            const im_open_config_t * __restrict open_config) {
  ImResult res;

  *dest = dec(data, len, open_config, &res);
  return *dest ? IM_OK : res;
}

#if !IM_CODECS_ONLY
IM_HIDE
ImResult
jpg_dec(ImImage         ** __restrict dest,
        const char       * __restrict path,
        im_open_config_t * __restrict open_config) {
  ImFileResult fres;

  *dest = NULL;
  fres  = im_readfile(path, open_config->openIntent != IM_OPEN_INTENT_READWRITE);

  if (fres.ret != IM_OK || !fres.raw) {
    return IM_ERR;
  }

  {
    ImResult res;

    *dest = dec(fres.raw, fres.size, open_config, &res);
  }

  /* the pixels don't point into the file: it goes now */
  if (fres.mmap) { im_unmap(fres.raw, fres.size); }
  else           { free(fres.raw);                }

  if (!*dest) {
#ifdef __APPLE__
    /* what isn't read here Core Image may read */
    {
      ImResult ret;

      ret = coreimg_dec(dest, path, open_config);
      if (*dest) {
        (*dest)->fileFormatType = IM_FILEFORMATTYPE_JPEG;
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
