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
#include "../../file.h"

#ifdef __APPLE__
#  include "../apple/coreimg.h"
#endif

IM_INLINE uint32_t
be16(const uint8_t *p) {
  return (uint32_t)p[0] << 8 | p[1];
}

/* -- blocks to planes ------------------------------------------------------------ */

IM_HIDE void
jpg_put(jpg_t *j, jcomp_t *c, int16_t *blk, uint32_t bx, uint32_t by) {
  size_t at;

  if (bx >= c->sw || by >= c->sh) return; /* an MCU's padding: never shown */
  at = (size_t)by * 8u * c->stride + (size_t)bx * 8u;
  if (j->prec <= 8u) j->idct(blk, j->quant[c->tq], (uint8_t *)c->plane + at, c->stride);
  else j->idct(blk, j->quant[c->tq], (uint16_t *)c->plane + at, c->stride);
}

/* a multi-scan image's blocks, once its scans are read */
static void
idct_all(jpg_t *j) {
  uint32_t i, bx, by;

  for (i = 0; i < j->ncomp; i++) {
    jcomp_t *c = &j->comp[i];

    for (by = 0; by < c->sh; by++) {
      for (bx = 0; bx < c->sw; bx++) jpg_put(j, c, c->coefs + ((size_t)by * c->bw + bx) * 64u, bx, by);
    }
  }
}

/* -- markers ----------------------------------------------------------------------- */

static bool
read_sof(jpg_t *j, uint32_t m, const uint8_t *s, uint32_t len) {
  uint32_t i;

  if (j->frame || len < 6u) return false;
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
  if (j->process == J_LOSSLESS || j->arith) return false; /* not yet */
  if (!j->width || !j->height || !j->ncomp || j->ncomp > J_COMPS || len != 6u + j->ncomp * 3u) return false;
  if (j->prec != 8u && j->prec != 12u) return false;
  j->idct = j->prec == 8u ? jpg_idct8 : jpg_idct12;

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
  j->mcux = (j->width + 8u * j->hmax - 1u) / (8u * j->hmax);
  j->mcuy = (j->height + 8u * j->vmax - 1u) / (8u * j->vmax);
  for (i = 0; i < j->ncomp; i++) {
    jcomp_t *c = &j->comp[i];
    uint64_t samples;

    c->bw     = j->mcux * c->h;
    c->bh     = j->mcuy * c->v;
    c->cw     = (uint32_t)(((uint64_t)j->width * c->h + j->hmax - 1u) / j->hmax);
    c->ch     = (uint32_t)(((uint64_t)j->height * c->v + j->vmax - 1u) / j->vmax);
    c->sw     = (c->cw + 7u) / 8u;
    c->sh     = (c->ch + 7u) / 8u;
    c->stride = (size_t)c->bw * 8u;
    samples   = (uint64_t)c->bw * 8u * c->bh * 8u;
    if (samples > J_SAMPLES || !(c->plane = malloc((size_t)samples * (j->prec > 8u ? 2u : 1u)))) return false;
    memset(c->bits, -1, sizeof(c->bits));
  }
  j->frame = true;
  return true;
}

/* DQT: 16-bit entries for any precision nibble but 0, as libjpeg */
static bool
read_dqt(jpg_t *j, const uint8_t *s, uint32_t len) {
  uint32_t at = 0u, i;

  while (at < len) {
    uint32_t pq = s[at] >> 4, tq = s[at] & 15u, size = pq ? 129u : 65u;

    if (tq > 3u || at + size > len) return false;
    for (i = 0; i < 64u; i++) j->quant[tq][jpg_natural[i]] = (uint16_t)(pq ? be16(s + at + 1u + i * 2u) : s[at + 1u + i]);
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
    if (!*slot && !(*slot = malloc(sizeof(**slot)))) return false;
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
    /* its chunks in order: seq_no (1-based) of count; joined as they come */
    uint8_t *grown;

    if (!s[12] || s[12] > s[13] || !(grown = realloc(j->icc, j->icc_len + len - 14u))) return;
    memcpy(grown + j->icc_len, s + 14, len - 14u);
    j->icc = grown;
    j->icc_len += len - 14u;
  }
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
  j->ss     = s[1u + n * 2u];
  j->se     = s[2u + n * 2u];
  j->ah     = s[3u + n * 2u] >> 4;
  j->al     = s[3u + n * 2u] & 15u;

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
      for (k = j->ss; k <= j->se; k++) c->bits[k] = (int8_t)j->al;
    }
  } else {
    j->ss = 0u, j->se = 63u, j->ah = 0u, j->al = 0u;
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

static void
jpg_free(jpg_t *j) {
  uint32_t i;

  for (i = 0; i < J_COMPS; i++) {
    free(j->comp[i].plane);
    free(j->comp[i].coefs);
  }
  for (i = 0; i < 4u; i++) {
    free(j->dc[i]);
    free(j->ac[i]);
  }
  free(j->icc);
  free(j->sd.buf);
  free(j->sd.seg);
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

static bool
decode(jpg_t *j, ImImage *im, ImByteOrder order) {
  const uint8_t *p = j->data, *end = j->end;
  bool           first = true, done = false;

  if (end - p < 4 || p[0] != 0xFFu || p[1] != 0xD8u) return false;
  p += 2;
  while (!done) {
    uint32_t       m = marker_at(&p, end), len;
    const uint8_t *s;

    if (m == 0xD9u) break;
    if ((m >= 0xD0u && m <= 0xD7u) || m == 0x01u) continue; /* parameterless */
    if (end - p < 2) break;
    len = be16(p);
    if (len < 2u || (size_t)(end - p) < len) break; /* cut short: as far as it went */
    s = p + 2;
    len -= 2u;
    p += len + 2u;

    switch (m) {
    case 0xC0: case 0xC1: case 0xC2: case 0xC3:
    case 0xC9: case 0xCA: case 0xCB:
      if (!read_sof(j, m, s, len)) return false;
      break;
    case 0xC5: case 0xC6: case 0xC7: case 0xCD: case 0xCE: case 0xCF:
      return false; /* hierarchical: not yet */
    case 0xC4:
      if (!read_dht(j, s, len)) return false;
      break;
    case 0xDB:
      if (!read_dqt(j, s, len)) return false;
      break;
    case 0xDD:
      if (len != 2u) return false;
      j->restart = be16(s);
      break;
    case 0xDA:
      if (first) {
        jpg_huff_std(j);
        if (!j->frame) return false;
      }
      if (!read_sos(j, s, len)) return false;
      if (first) {
        uint32_t i;

        j->multiscan = j->process == J_PROGRESSIVE || j->nscomp < j->ncomp;
        for (i = 0; i < j->ncomp && j->multiscan; i++) {
          jcomp_t *c = &j->comp[i];

          if (!(c->coefs = calloc((size_t)c->bw * c->bh * 64u, sizeof(int16_t)))) return false;
        }
        first = false;
      } else if (!j->multiscan) {
        /* one pass: later scans aren't read */
        if (!jpg_unstuff(&j->sd, p, end)) return false;
        p = j->sd.next;
        break;
      }
      if (!jpg_unstuff(&j->sd, p, end) || !jpg_scan_huff(j)) return false;
      j->nscans++;
      p = j->sd.next;
      break;
    default:
      if (m >= 0xE0u && m <= 0xEFu) read_app(j, m, s, len);
      break;
    }
  }
  if (!j->nscans) return false;
  if (j->multiscan) idct_all(j);
  if (!jpg_out(j, guess_space(j), im, order)) return false;

  /* the profile, the density (pixels a metre) */
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

IM_HIDE
ImResult
jpg_dec(ImImage         ** __restrict dest,
        const char       * __restrict path,
        im_open_config_t * __restrict open_config) {
  ImImage     *im;
  ImFileResult fres;
  jpg_t       *j;
  bool         ok = false;

  *dest = NULL;
  fres  = im_readfile(path, open_config->openIntent != IM_OPEN_INTENT_READWRITE);

  if (fres.ret != IM_OK || !fres.raw) {
    return IM_ERR;
  }

  im = calloc(1, sizeof(*im));
  j  = calloc(1, sizeof(*j));
  if (im && j) {
    j->data = fres.raw;
    j->end  = (const uint8_t *)fres.raw + fres.size;
    ok      = decode(j, im, open_config->byteOrder);
  }
  if (j) {
    jpg_free(j);
    free(j);
  }

  /* the pixels don't point into the file: it goes now */
  if (fres.mmap) { im_unmap(fres.raw, fres.size); }
  else           { free(fres.raw);                }

  if (!ok) {
    if (im) {
      free(im->data.data);
      free(im->iccProfile);
      free(im->physicalDim);
      free(im);
    }

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

  im->openIntent     = open_config->openIntent;
  im->ori            = IM_ORIENTATION_UP;
  im->fileFormatType = IM_FILEFORMATTYPE_JPEG;
  *dest              = im;

  return IM_OK;
}
