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
 * The planes to pixels: each component brought to the image's size, then
 * its colours. libjpeg's choices (jdsample.c, jdcolor.c): "fancy"
 * upsampling, a triangle filter, where a component is half as wide (more
 * than two samples), half as tall, or both; each sample repeated for any
 * other whole ratio; the row above the first and below the last the edge
 * one. YCbCr to RGB and YCCK to CMYK in 16-bit fixed point by tables.
 */

#include "jpgint.h"

#if (defined(__ARM_NEON) || defined(_M_ARM64)) && defined(__aarch64__) && !defined(IM_NO_SIMD)
#  define J_NEON 1
#  include "sample_neon.h"
#elif (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__)) && !defined(IM_NO_SIMD)
#  define J_X86 1
#  include "sample_x86.h"
#elif defined(__wasm_simd128__) && !defined(IM_NO_SIMD)
#  define J_WASM 1
#  include "sample_wasm.h"
#endif

/* how a component comes to the image's size */
typedef enum jup {
  UP_FULL,
  UP_H2V1_FANCY,
  UP_H1V2_FANCY,
  UP_H2V2_FANCY,
  UP_INT,      /* each sample repeated: hx across, vx down */
  UP_ANY       /* a ratio libjpeg refuses: the nearest sample */
} jup_t;

typedef struct jupc {
  jup_t    how;
  uint32_t hx, vx;
} jupc_t;

static jupc_t
up_method(const jpg_t *j, const jcomp_t *c) {
  jupc_t   u = { UP_ANY, 1u, 1u };
  bool     fancy = j->process != J_LOSSLESS; /* libjpeg: no context rows where a block is a sample */
  uint32_t hi = c->h, vi = c->v, ho = j->hmax, vo = j->vmax;

  if (hi == ho && vi == vo) {
    u.how = UP_FULL;
  } else if (hi * 2u == ho && vi == vo) {
    if (fancy && c->cw > 2u) u.how = UP_H2V1_FANCY;
    else {
      u.how = UP_INT;
      u.hx  = 2u;
    }
  } else if (hi == ho && vi * 2u == vo && fancy) {
    u.how = UP_H1V2_FANCY;
  } else if (hi * 2u == ho && vi * 2u == vo) {
    if (fancy && c->cw > 2u) u.how = UP_H2V2_FANCY;
    else {
      u.how = UP_INT;
      u.hx  = 2u;
      u.vx  = 2u;
    }
  } else if (ho % hi == 0u && vo % vi == 0u) {
    u.how = UP_INT;
    u.hx  = ho / hi;
    u.vx  = vo / vi;
  }
  return u;
}

/* `T` a sample: uint8_t or uint16_t; `W` the int its sums need */
#define UPSAMPLE_FN(NAME, T, W)                                                \
static const T*                                                                \
NAME(const jpg_t *j, const jcomp_t *c, const jupc_t *u, uint32_t y, T *out) {  \
  const T *plane = c->plane, *near, *far;                                      \
  size_t   st    = c->stride;                                                  \
  uint32_t n     = c->cw, x, cy;                                               \
                                                                               \
  switch (u->how) {                                                            \
  case UP_FULL:                                                                \
    return plane + (size_t)y * st;                                             \
  case UP_H2V1_FANCY: {                                                        \
    const T *in = plane + (size_t)y * st;                                      \
                                                                               \
    out[0] = in[0];                                                            \
    out[1] = (T)((in[0] * 3 + in[1] + 2) >> 2);                                \
    for (x = 1; x + 1u < n; x++) {                                             \
      W v = (W)in[x] * 3;                                                      \
                                                                               \
      out[2u * x]      = (T)((v + in[x - 1u] + 1) >> 2);                       \
      out[2u * x + 1u] = (T)((v + in[x + 1u] + 2) >> 2);                       \
    }                                                                          \
    out[2u * x]      = (T)((in[x] * 3 + in[x - 1u] + 1) >> 2);                 \
    out[2u * x + 1u] = in[x];                                                  \
    return out;                                                                \
  }                                                                            \
  case UP_H1V2_FANCY:                                                          \
  case UP_H2V2_FANCY: {                                                        \
    W bias = (y & 1u) ? 2 : 1;                                                 \
                                                                               \
    cy   = y >> 1;                                                             \
    near = plane + (size_t)cy * st;                                            \
    if (!(y & 1u)) far = cy ? near - st : near;                                \
    else far = cy + 1u < c->ch ? near + st : near;                             \
    if (u->how == UP_H1V2_FANCY) {                                             \
      for (x = 0; x < n; x++) out[x] = (T)(((W)near[x] * 3 + far[x] + bias) >> 2); \
      return out;                                                              \
    } else {                                                                   \
      W this = (W)near[0] * 3 + far[0], next = (W)near[1] * 3 + far[1], last;  \
                                                                               \
      out[0] = (T)((this * 4 + 8) >> 4);                                       \
      out[1] = (T)((this * 3 + next + 7) >> 4);                                \
      for (x = 1; x + 1u < n; x++) {                                           \
        last = this;                                                           \
        this = next;                                                           \
        next = (W)near[x + 1u] * 3 + far[x + 1u];                              \
        out[2u * x]      = (T)((this * 3 + last + 8) >> 4);                    \
        out[2u * x + 1u] = (T)((this * 3 + next + 7) >> 4);                    \
      }                                                                        \
      out[2u * x]      = (T)((next * 3 + this + 8) >> 4);                      \
      out[2u * x + 1u] = (T)((next * 4 + 7) >> 4);                             \
      return out;                                                              \
    }                                                                          \
  }                                                                            \
  case UP_INT: {                                                               \
    const T *in = plane + (size_t)(y / u->vx) * st;                            \
                                                                               \
    if (u->hx == 1u) return in;                                                \
    for (x = 0; x < j->width; x++) out[x] = in[x / u->hx];                     \
    return out;                                                                \
  }                                                                            \
  default: {                                                                   \
    const T *in = plane + (size_t)(((uint64_t)y * c->v) / j->vmax) * st;       \
                                                                               \
    for (x = 0; x < j->width; x++) out[x] = in[((uint64_t)x * c->h) / j->hmax]; \
    return out;                                                                \
  }                                                                            \
  }                                                                            \
}

UPSAMPLE_FN(up_row8_plain, uint8_t, int32_t)
UPSAMPLE_FN(up_row16, uint16_t, int64_t)

/* scratch a row's upsampling takes: a copy with room each side, sums */
typedef struct jrow {
  uint8_t  *pad;
  uint16_t *sums;
  bool      x86;  /* the CPU has SSSE3 and SSE4.1 */
} jrow_t;

static const uint8_t*
up_row8(const jpg_t *j, const jcomp_t *c, const jupc_t *u, uint32_t y, uint8_t *out, const jrow_t *r) {
#if J_NEON
  const uint8_t *plane = c->plane, *near, *far;
  size_t         st = c->stride;
  uint32_t       n = c->cw, cy;

  switch (u->how) {
  case UP_H2V1_FANCY: {
    const uint8_t *in = plane + (size_t)y * st;

    memcpy(r->pad + 1, in, n);
    r->pad[0]      = in[0];
    r->pad[n + 1u] = in[n - 1u];
    jn_h2v1(r->pad + 1, n, out);
    return out;
  }
  case UP_H1V2_FANCY:
  case UP_H2V2_FANCY:
    cy   = y >> 1;
    near = plane + (size_t)cy * st;
    if (!(y & 1u)) far = cy ? near - st : near;
    else far = cy + 1u < c->ch ? near + st : near;
    jn_colsum(near, far, n, r->sums + 1);
    if (u->how == UP_H2V2_FANCY) jn_h2v2(r->sums + 1, n, out);
    else jn_h1v2(r->sums + 1, n, (y & 1u) ? 2u : 1u, out);
    return out;
  default:
    break;
  }
#elif J_WASM
  const uint8_t *plane = c->plane, *near, *far;
  size_t         st = c->stride;
  uint32_t       n = c->cw, cy;

  switch (u->how) {
  case UP_H2V1_FANCY: {
    const uint8_t *in = plane + (size_t)y * st;

    memcpy(r->pad + 1, in, n);
    r->pad[0]      = in[0];
    r->pad[n + 1u] = in[n - 1u];
    jw_h2v1(r->pad + 1, n, out);
    return out;
  }
  case UP_H1V2_FANCY:
  case UP_H2V2_FANCY:
    cy   = y >> 1;
    near = plane + (size_t)cy * st;
    if (!(y & 1u)) far = cy ? near - st : near;
    else far = cy + 1u < c->ch ? near + st : near;
    jw_colsum(near, far, n, r->sums + 1);
    if (u->how == UP_H2V2_FANCY) jw_h2v2(r->sums + 1, n, out);
    else jw_h1v2(r->sums + 1, n, (y & 1u) ? 2u : 1u, out);
    return out;
  default:
    break;
  }
#elif J_X86
  const uint8_t *plane = c->plane, *near, *far;
  size_t         st = c->stride;
  uint32_t       n = c->cw, cy;

  if (r->x86) {
    switch (u->how) {
    case UP_H2V1_FANCY: {
      const uint8_t *in = plane + (size_t)y * st;

      memcpy(r->pad + 1, in, n);
      r->pad[0]      = in[0];
      r->pad[n + 1u] = in[n - 1u];
      jx_h2v1(r->pad + 1, n, out);
      return out;
    }
    case UP_H1V2_FANCY:
    case UP_H2V2_FANCY:
      cy   = y >> 1;
      near = plane + (size_t)cy * st;
      if (!(y & 1u)) far = cy ? near - st : near;
      else far = cy + 1u < c->ch ? near + st : near;
      jx_colsum(near, far, n, r->sums + 1);
      if (u->how == UP_H2V2_FANCY) jx_h2v2(r->sums + 1, n, out);
      else jx_h1v2(r->sums + 1, n, (y & 1u) ? 2u : 1u, out);
      return out;
    default:
      break;
    }
  }
#else
  (void)r;
#endif
  return up_row8_plain(j, c, u, y, out);
}

/* libjpeg's YCbCr tables (jdcolor.c build_ycc_rgb_table), for samples up
   to `max` */
typedef struct jycc {
  int32_t *cr_r, *cb_b;
  int64_t *cr_g, *cb_g;
} jycc_t;

#define SCALEBITS 16
#define ONE_HALF  ((int64_t)1 << (SCALEBITS - 1))
#define FIX(x)    ((int64_t)((x) * (1L << SCALEBITS) + 0.5))

/* `n` entries: every value a sample's type holds (damage can put a
   sample past its depth), centred on the depth's */
static bool
ycc_tables(jycc_t *t, uint32_t max, uint32_t n) {
  uint32_t i, centre = (max + 1u) / 2u;

  t->cr_r = malloc(n * sizeof(*t->cr_r));
  t->cb_b = malloc(n * sizeof(*t->cb_b));
  t->cr_g = malloc(n * sizeof(*t->cr_g));
  t->cb_g = malloc(n * sizeof(*t->cb_g));
  if (!t->cr_r || !t->cb_b || !t->cr_g || !t->cb_g) return false;
  for (i = 0; i < n; i++) {
    int64_t x = (int64_t)i - centre;

    t->cr_r[i] = (int32_t)((FIX(1.40200) * x + ONE_HALF) >> SCALEBITS);
    t->cb_b[i] = (int32_t)((FIX(1.77200) * x + ONE_HALF) >> SCALEBITS);
    t->cr_g[i] = (-FIX(0.71414)) * x;
    t->cb_g[i] = (-FIX(0.34414)) * x + ONE_HALF;
  }
  return true;
}

static void
ycc_free(jycc_t *t) {
  free(t->cr_r);
  free(t->cb_b);
  free(t->cr_g);
  free(t->cb_g);
}

IM_INLINE int32_t
clampi(int32_t v, int32_t max) {
  return v < 0 ? 0 : v > max ? max : v;
}

/* a row of pixels from the components' rows: `n` components to `ncol`
   out (a 4th after colour conversion), `invert` Adobe's CMYK */
#define PIXELS_FN(NAME, T)                                                     \
static void                                                                    \
NAME(const T *const *row, uint32_t ncomp, jspace_t space, const jycc_t *t,     \
     int32_t max, bool invert, uint32_t w, T *o) {                             \
  uint32_t x;                                                                  \
                                                                               \
  if (ncomp == 1u) {                                                           \
    memcpy(o, row[0], (size_t)w * sizeof(T));                                  \
  } else if (space == J_YCC || space == J_YCCK) {                              \
    const T *y0 = row[0], *cb = row[1], *cr = row[2], *k = row[3];             \
    uint32_t step = ncomp;                                                     \
                                                                               \
    for (x = 0; x < w; x++, o += step) {                                       \
      int32_t Y = y0[x], r, g, b;                                              \
                                                                               \
      r = Y + t->cr_r[cr[x]];                                                  \
      g = Y + (int32_t)((t->cb_g[cb[x]] + t->cr_g[cr[x]]) >> SCALEBITS);       \
      b = Y + t->cb_b[cb[x]];                                                  \
      if (space == J_YCC) {                                                    \
        o[0] = (T)clampi(r, max);                                              \
        o[1] = (T)clampi(g, max);                                              \
        o[2] = (T)clampi(b, max);                                              \
      } else {                                                                 \
        o[0] = (T)clampi(max - r, max);                                        \
        o[1] = (T)clampi(max - g, max);                                        \
        o[2] = (T)clampi(max - b, max);                                        \
        o[3] = k[x];                                                           \
      }                                                                        \
    }                                                                          \
    o -= (size_t)w * step;                                                     \
  } else {                                                                     \
    uint32_t c;                                                                \
                                                                               \
    for (x = 0; x < w; x++, o += ncomp) {                                      \
      for (c = 0; c < ncomp; c++) o[c] = row[c][x];                            \
    }                                                                          \
    o -= (size_t)w * ncomp;                                                    \
  }                                                                            \
  if (invert) {                                                                \
    for (x = 0; x < w * ncomp; x++) o[x] = (T)(max - o[x]);                    \
  }                                                                            \
}

PIXELS_FN(pixels8_plain, uint8_t)
PIXELS_FN(pixels16, uint16_t)

static void
pixels8(const uint8_t *const *row, uint32_t ncomp, jspace_t space, const jycc_t *t, int32_t max, bool invert,
        uint32_t w, uint8_t *o, bool x86) {
#if J_NEON || J_X86 || J_WASM
  uint32_t done = 0, i;

  /* YCbCr's SIMD is 8-bit samples' (lossless shallower ones: the plain loop) */
#  if J_NEON
  (void)x86;
  if (ncomp == 3u && space == J_YCC && max == 255) done = jn_ycc(row[0], row[1], row[2], NULL, o, w);
  else if (ncomp == 4u && space == J_YCCK && max == 255) done = jn_ycc(row[0], row[1], row[2], row[3], o, w);
  else if (ncomp > 1u) done = jn_interleave(row, ncomp, o, w);
#  elif J_WASM
  (void)x86;
  if (ncomp == 3u && space == J_YCC && max == 255) done = jw_ycc(row[0], row[1], row[2], NULL, o, w);
  else if (ncomp == 4u && space == J_YCCK && max == 255) done = jw_ycc(row[0], row[1], row[2], row[3], o, w);
  else if (ncomp > 1u) done = jw_interleave(row, ncomp, o, w);
#  else
  if (!x86) done = 0;
  else if (ncomp == 3u && space == J_YCC && max == 255) done = jx_ycc(row[0], row[1], row[2], NULL, o, w);
  else if (ncomp == 4u && space == J_YCCK && max == 255) done = jx_ycc(row[0], row[1], row[2], row[3], o, w);
  else if (ncomp > 1u) done = jx_interleave(row, ncomp, o, w);
#  endif
  if (done) {
    const uint8_t *rest[J_COMPS];

    for (i = 0; i < ncomp; i++) rest[i] = row[i] + done;
    if (done < w) pixels8_plain(rest, ncomp, space, t, max, false, w - done, o + (size_t)done * ncomp);
    if (invert) {
      for (i = 0; i < w * ncomp; i++) o[i] = (uint8_t)(255u - o[i]);
    }
    return;
  }
#else
  (void)x86;
#endif
  pixels8_plain(row, ncomp, space, t, max, invert, w, o);
}

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#  define J_HOST_BIG true
#else
#  define J_HOST_BIG false
#endif

/* the output's work: bands of rows on the pool, a worker's own rows */
typedef struct jsout {
  jpg_t   *j;
  jupc_t   up[J_COMPS];
  jycc_t   t;
  jspace_t space;
  uint8_t *px;
  size_t   rowlen;
  uint32_t band, max;
  bool     wide, invert, swap;
  bool     pad;      /* RGB as RGB0: a row made, then spread to four */
  uint32_t on;       /* samples a pixel out */
  uint8_t *rows[IM_POOL_MAX];
  uint8_t *rgb[IM_POOL_MAX];
  jrow_t   scratch[IM_POOL_MAX];
} jsout_t;

/* an RGB row as RGB0, the 4th sample `x` */
static void
pad8(const uint8_t *in, uint32_t w, uint8_t *out) {
  uint32_t i = 0;

#if J_NEON
  for (; i + 16u <= w; i += 16u) {
    uint8x16x3_t a = vld3q_u8(in + 3u * i);
    uint8x16x4_t b = { { a.val[0], a.val[1], a.val[2], vdupq_n_u8(255) } };

    vst4q_u8(out + 4u * i, b);
  }
#elif J_WASM
  i = jw_pad(in, w, out);
#endif
  for (; i < w; i++) {
    out[4u * i]      = in[3u * i];
    out[4u * i + 1u] = in[3u * i + 1u];
    out[4u * i + 2u] = in[3u * i + 2u];
    out[4u * i + 3u] = 255u;
  }
}

static void
pad16(const uint16_t *in, uint32_t w, uint16_t *out) {
  uint32_t i;

  for (i = 0; i < w; i++) {
    out[4u * i]      = in[3u * i];
    out[4u * i + 1u] = in[3u * i + 1u];
    out[4u * i + 2u] = in[3u * i + 2u];
    out[4u * i + 3u] = 65535u;
  }
}

static bool
out_band(void *arg, uint32_t worker, uint32_t item) {
  jsout_t *o = arg;
  jpg_t   *j = o->j;
  uint32_t n = j->ncomp, w = j->width, y0 = item * o->band, y1 = y0 + o->band, y, i;
  uint8_t *rows = o->rows[worker];

  if (y1 > j->height) y1 = j->height;
  for (y = y0; y < y1; y++) {
    if (!o->wide) {
      const uint8_t *row[J_COMPS];
      uint8_t       *dst = o->pad ? o->rgb[worker] : o->px + (size_t)y * w * n;

      for (i = 0; i < n; i++) row[i] = up_row8(j, &j->comp[i], &o->up[i], y, rows + o->rowlen * i, &o->scratch[worker]);
      pixels8(row, n, o->space, &o->t, (int32_t)o->max, o->invert, w, dst, o->scratch[worker].x86);
      if (j->prec < 8u) {
        /* shallower samples spread over 0–255 */
        uint8_t *p = dst;
        size_t   k;

        for (k = 0; k < (size_t)w * n; k++) {
          uint32_t v = p[k] > o->max ? o->max : p[k];

          p[k] = (uint8_t)((v * 255u + o->max / 2u) / o->max);
        }
      }
      if (o->pad) pad8(dst, w, o->px + (size_t)y * w * 4u);
    } else {
      const uint16_t *row[J_COMPS];
      uint16_t       *p = o->pad ? (uint16_t *)(void *)o->rgb[worker] : (uint16_t *)o->px + (size_t)y * w * n;
      size_t          k;

      for (i = 0; i < n; i++) row[i] = up_row16(j, &j->comp[i], &o->up[i], y, (uint16_t *)rows + o->rowlen * i);
      pixels16(row, n, o->space, &o->t, (int32_t)o->max, o->invert, w, p);
      /* deeper samples to 16 bits, in the order asked */
      for (k = 0; k < (size_t)w * n; k++) {
        uint32_t v = p[k] > o->max ? o->max : p[k]; /* damage past the depth: its top */

        v = j->prec == 16u ? v : (uint32_t)(((uint64_t)v * 65535u + o->max / 2u) / o->max);

        p[k] = o->swap ? bswapu16((uint16_t)v) : (uint16_t)v;
      }
      if (o->pad) pad16(p, w, (uint16_t *)o->px + (size_t)y * w * 4u);
    }
  }
  return true;
}

/* the planes as `im`'s pixels: gray, RGB or CMYK; 8 bits a sample, or 16
   (deeper samples spread over 0–65535) in `order` (the host's if any) */
IM_HIDE bool
jpg_out(jpg_t *j, jspace_t space, ImImage *im, ImByteOrder order) {
  jsout_t *o;
  uint32_t n = j->ncomp, w = j->width, h = j->height, i, nw = im_pool_size(j->pool);
  size_t   len;
  bool     ok = false;

  if (n == 2u || (n == 3u && space != J_RGB && space != J_YCC) || (n == 4u && space != J_CMYK && space != J_YCCK)) {
    return false;
  }
  if (!(o = calloc(1, sizeof(*o)))) return false;
  if (order != IM_BYTEORDER_BIG && order != IM_BYTEORDER_LITTLE) order = IM_BYTEORDER_HOST;
  o->j      = j;
  o->space  = space;
  o->max    = (1u << j->prec) - 1u;
  o->wide   = j->prec > 8u;
  o->invert = n == 4u && j->adobe && !j->cmyk_stored;
  o->pad    = n == 3u && j->rgb_pad;
  o->on     = o->pad ? 4u : n;
  o->swap   = order != IM_BYTEORDER_HOST && (order == IM_BYTEORDER_BIG) != J_HOST_BIG;
  o->rowlen = (size_t)j->hmax * 8u * (j->mcux + 1u) + 16u;
  o->band   = j->pool ? h / (nw * 8u) : h;
  o->band   = o->band < 8u ? 8u : o->band;
  len       = (size_t)w * h * o->on * (o->wide ? 2u : 1u);
  for (i = 0; i < n; i++) o->up[i] = up_method(j, &j->comp[i]);
  if ((space == J_YCC || space == J_YCCK) && !ycc_tables(&o->t, o->max, o->wide ? 65536u : 256u)) goto done;
  if (!(o->px = malloc(len))) goto done;
  for (i = 0; i < nw; i++) {
#if J_X86
    o->scratch[i].x86 = __builtin_cpu_supports("ssse3") && __builtin_cpu_supports("sse4.1");
#endif
    if (!(o->rows[i] = malloc(o->rowlen * n * (o->wide ? 2u : 1u))) || !(o->scratch[i].pad = malloc(o->rowlen + 2u))
        || !(o->scratch[i].sums = malloc((o->rowlen + 2u) * sizeof(uint16_t)))
        || (o->pad && !(o->rgb[i] = malloc((size_t)w * 3u * (o->wide ? 2u : 1u))))) {
      goto done;
    }
  }
  if (!im_pool_run(j->pool, out_band, o, (h + o->band - 1u) / o->band)) goto done;

  im->width              = w;
  im->height             = h;
  im->data.data          = o->px;
  im->len                = len;
  im->componentsPerPixel = o->on;
  im->bitsPerComponent   = o->wide ? 16u : 8u;
  im->bitsPerPixel       = o->on * im->bitsPerComponent;
  im->bytesPerPixel      = im->bitsPerPixel / 8u;
  im->byteOrder          = order;
  im->alphaInfo          = o->pad ? IM_ALPHA_NONE_SKIP_LAST : IM_ALPHA_NONE;
  im->format             = n == 1u ? IM_FORMAT_GRAY : n == 3u ? (o->pad ? IM_FORMAT_RGB0 : IM_FORMAT_RGB) : IM_FORMAT_CMYK;
  im->colorSpace         = n == 1u ? IM_COLORSPACE_GRAY : n == 3u ? IM_COLORSPACE_sRGB : IM_COLORSPACE_CMYK;
  o->px                  = NULL;
  ok                     = true;

done:
  free(o->px);
  for (i = 0; i < IM_POOL_MAX; i++) {
    free(o->rows[i]);
    free(o->scratch[i].pad);
    free(o->scratch[i].sums);
    free(o->rgb[i]);
  }
  ycc_free(&o->t);
  free(o);
  return ok;
}
