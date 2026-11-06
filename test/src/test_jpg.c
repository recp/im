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
 * JPEG (io/jpg) on data/jpg's fixtures (make.sh made them): each one's
 * pixels hashed as im gives them, each of which is libjpeg-turbo's to the
 * byte (deeper samples spread over 16 bits, shallower over 8, Adobe's CMYK
 * un-inverted): every sampling libjpeg upsamples, progressive, restarts,
 * 12-bit, arithmetic coding, lossless at 2 to 16 bits (and arithmetic
 * coded, as its Huffman twin), hierarchical sequences, CMYK and YCCK, a
 * height from a DNL (420.jpg's and prog.jpg's pixels),
 * streams cut short (the rest grey; a progressive one smoothed), a large
 * one on threads; 16-bit samples in either order; an ICC profile's chunks
 * joined in order; every seventh cut of five files decoding as far as it
 * goes, or not at all.
 */

#include "../include/common.h"

#ifndef IM_TEST_DATA
#  define IM_TEST_DATA "data"
#endif

#ifndef IM_TEST_TMP
#  define IM_TEST_TMP "."
#endif

static uint32_t
fnv(uint32_t h, const uint8_t *p, size_t n) {
  size_t i;

  for (i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

static ImImage*
load(const char *path, ImByteOrder order) {
  ImImage               *im = NULL;
  im_option_byteorder_t  bo = im_option_row_byteorder(order);

  if (test_load(&im, path, IM_OPTIONS { &bo.base, NULL }) != IM_OK) {
    if (im) im_free(im);
    return NULL;
  }
  return im;
}

static ImImage*
fixture(const char *name, ImByteOrder order) {
  char path[1024];

  snprintf(path, sizeof(path), "%s/jpg/%s", IM_TEST_DATA, name);
  return load(path, order);
}

static uint8_t*
read_all(const char *name, size_t *len) {
  char     path[1024];
  FILE    *f;
  long     size;
  uint8_t *d;

  snprintf(path, sizeof(path), "%s/jpg/%s", IM_TEST_DATA, name);
  if (!(f = fopen(path, "rb"))) return NULL;
  fseek(f, 0, SEEK_END);
  size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size <= 0 || !(d = malloc((size_t)size))) {
    fclose(f);
    return NULL;
  }
  *len = fread(d, 1, (size_t)size, f);
  fclose(f);
  return d;
}

static bool
write_all(const char *path, const uint8_t *d, size_t n) {
  FILE *f = fopen(path, "wb");
  bool  ok;

  if (!f) return false;
  ok = fwrite(d, 1, n, f) == n;
  fclose(f);
  return ok;
}

TEST_IMPL(jpg_fixtures) {
  enum { RGB = IM_FORMAT_RGB, GRAY = IM_FORMAT_GRAY, CMYK = IM_FORMAT_CMYK };
  enum { sRGB = IM_COLORSPACE_sRGB, GRAYCS = IM_COLORSPACE_GRAY, CMYKCS = IM_COLORSPACE_CMYK };
  static const struct {
    const char *name;
    uint32_t    format, bits, space, w, h, hash;
  } cases[] = {
    { "420.jpg",          RGB,  8,  sRGB,   37,  23,  0x82F94015u }, /* fancy h2v2 */
    { "444.jpg",          RGB,  8,  sRGB,   37,  23,  0xF8F67F35u },
    { "422.jpg",          RGB,  8,  sRGB,   37,  23,  0x72B0C4F5u }, /* fancy h2v1 */
    { "440.jpg",          RGB,  8,  sRGB,   37,  23,  0x485F8039u }, /* fancy h1v2 */
    { "411.jpg",          RGB,  8,  sRGB,   37,  23,  0xDD0E7CD7u }, /* each sample four times */
    { "311.jpg",          RGB,  8,  sRGB,   37,  23,  0x726867A6u }, /* three times */
    { "mixed.jpg",        RGB,  8,  sRGB,   37,  23,  0xB4DD8811u }, /* 2x2, 1x2, 2x1 */
    { "tiny.jpg",         RGB,  8,  sRGB,   7,   5,   0x0DEFE61Du }, /* chroma two wide: repeated */
    { "prog.jpg",         RGB,  8,  sRGB,   37,  23,  0x82F94015u }, /* 420.jpg's coefficients */
    { "prog-rst.jpg",     RGB,  8,  sRGB,   37,  23,  0x82F94015u },
    { "opt.jpg",          RGB,  8,  sRGB,   37,  23,  0x82F94015u },
    { "rst.jpg",          RGB,  8,  sRGB,   37,  23,  0xF8F67F35u }, /* a restart a block */
    { "gray.jpg",         GRAY, 8,  GRAYCS, 37,  23,  0xC7FB2BA7u },
    { "rgb.jpg",          RGB,  8,  sRGB,   37,  23,  0x5A75988Au }, /* RGB, no transform */
    { "p12.jpg",          RGB,  16, sRGB,   37,  23,  0x77EEB558u },
    { "p12-prog.jpg",     RGB,  16, sRGB,   37,  23,  0xAFD76F1Au },
    { "arith.jpg",        RGB,  8,  sRGB,   37,  23,  0x82F94015u },
    { "arith-prog.jpg",   RGB,  8,  sRGB,   37,  23,  0x82F94015u },
    { "arith-rst.jpg",    RGB,  8,  sRGB,   37,  23,  0x82F94015u },
    { "arith-p12.jpg",    RGB,  16, sRGB,   37,  23,  0x77EEB558u },
    { "ll1.jpg",          RGB,  8,  sRGB,   37,  23,  0x03A59D62u }, /* lossless */
    { "ll7-pt4.jpg",      RGB,  8,  sRGB,   37,  23,  0x015A688Fu },
    { "ll-p2.jpg",        RGB,  8,  sRGB,   37,  23,  0x36BC0E05u }, /* 2 bits, spread */
    { "ll-p12.jpg",       RGB,  16, sRGB,   37,  23,  0x984C6751u },
    { "ll-p16.jpg",       RGB,  16, sRGB,   37,  23,  0xF61675E3u },
    { "ll-420.jpg",       RGB,  8,  sRGB,   37,  23,  0x03A59D62u },
    { "ll-rst.jpg",       RGB,  8,  sRGB,   37,  23,  0x03A59D62u },
    { "ll-arith.jpg",     RGB,  8,  sRGB,   37,  23,  0x03A59D62u }, /* lossless, arithmetic: the picture */
    { "ll-arith-rst.jpg", RGB,  8,  sRGB,   37,  23,  0x03A59D62u },
    { "ll-arith-420.jpg", RGB,  8,  sRGB,   37,  23,  0x133624F4u }, /* its Huffman twin's */
    { "hier.jpg",         RGB,  8,  sRGB,   37,  23,  0x7413C595u }, /* hierarchical: half, then differential */
    { "hier-prog.jpg",    RGB,  8,  sRGB,   37,  23,  0x7413C595u },
    { "hier-arith.jpg",   RGB,  8,  sRGB,   37,  23,  0x7413C595u },
    { "hier-ll.jpg",      RGB,  8,  sRGB,   37,  23,  0x5D2E1AB9u },
    { "hier-dct-ll.jpg",  RGB,  8,  sRGB,   37,  23,  0xCC4F1DDDu },
    { "cmyk.jpg",         CMYK, 8,  CMYKCS, 37,  23,  0x9D19D764u }, /* Adobe's: un-inverted */
    { "ycck.jpg",         CMYK, 8,  CMYKCS, 37,  23,  0x1FEDD2ACu },
    { "cmyk-plain.jpg",   CMYK, 8,  CMYKCS, 37,  23,  0x9EAC2000u },
    { "420-cut.jpg",      RGB,  8,  sRGB,   37,  23,  0xE4B5AF76u }, /* the rest grey */
    { "prog-cut.jpg",     RGB,  8,  sRGB,   37,  23,  0x86D88A7Du }, /* smoothed */
    { "threads.jpg",      RGB,  8,  sRGB,   777, 777, 0x6AA49719u }, /* restarts on threads */
    { "dnl.jpg",          RGB,  8,  sRGB,   37,  23,  0x82F94015u }, /* the height a DNL's */
    { "dnl-prog.jpg",     RGB,  8,  sRGB,   37,  23,  0x82F94015u },
  };
  size_t i;

  for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    ImImage *im = fixture(cases[i].name, IM_BYTEORDER_LITTLE);
    uint32_t comps, h;

    ASSERT(im && im->data.data);
    comps = cases[i].format == RGB ? 3u : cases[i].format == CMYK ? 4u : 1u;
    ASSERT(im->width == cases[i].w && im->height == cases[i].h);
    ASSERT(im->format == (ImFormat)cases[i].format);
    ASSERT(im->colorSpace == (ImColorSpace)cases[i].space);
    ASSERT(im->fileFormatType == IM_FILEFORMATTYPE_JPEG);
    ASSERT(im->componentsPerPixel == comps && im->bitsPerComponent == cases[i].bits);
    ASSERT(im->bytesPerPixel == comps * cases[i].bits / 8u && im->bitsPerPixel == comps * cases[i].bits);
    ASSERT(im->len == (size_t)cases[i].w * cases[i].h * im->bytesPerPixel);
    ASSERT(im->alphaInfo == IM_ALPHA_NONE);

    h = fnv(2166136261u, im->data.data, im->len);
    if (h != cases[i].hash) fprintf(stderr, "  %s: 0x%08X\n", cases[i].name, h);
    ASSERT(h == cases[i].hash);
    im_free(im);
  }

  TEST_SUCCESS
}

/* 16-bit samples in the order asked */
TEST_IMPL(jpg_16bit) {
  ImImage *le, *be, *host, *any;
  size_t   i;
  uint16_t one = 1u;

  le   = fixture("ll-p16.jpg", IM_BYTEORDER_LITTLE);
  be   = fixture("ll-p16.jpg", IM_BYTEORDER_BIG);
  host = fixture("ll-p16.jpg", IM_BYTEORDER_HOST);
  any  = fixture("ll-p16.jpg", IM_BYTEORDER_ANY);
  ASSERT(le && be && host && any);
  ASSERT(le->byteOrder == IM_BYTEORDER_LITTLE && be->byteOrder == IM_BYTEORDER_BIG);
  ASSERT(host->byteOrder == IM_BYTEORDER_HOST && any->byteOrder == IM_BYTEORDER_HOST);
  ASSERT(le->len == be->len && le->len == host->len && le->len == any->len);
  for (i = 0; i < le->len; i += 2u) {
    const uint8_t *l = (const uint8_t *)le->data.data + i, *b = (const uint8_t *)be->data.data + i;

    ASSERT(l[0] == b[1] && l[1] == b[0]);
  }
  ASSERT(!memcmp(host->data.data, *(uint8_t *)&one ? le->data.data : be->data.data, le->len));
  ASSERT(!memcmp(any->data.data, host->data.data, le->len));
  im_free(le);
  im_free(be);
  im_free(host);
  im_free(any);

  TEST_SUCCESS
}

/* an APP2 ICC chunk: "ICC_PROFILE\0", its number, the count, then data */
static size_t
icc_chunk(uint8_t *o, uint8_t seq, uint8_t count, uint8_t fill, size_t n) {
  size_t len = 2u + 14u + n;

  o[0] = 0xFFu, o[1] = 0xE2u, o[2] = (uint8_t)(len >> 8), o[3] = (uint8_t)len;
  memcpy(o + 4, "ICC_PROFILE", 12);
  o[16] = seq;
  o[17] = count;
  memset(o + 18, fill, n);
  return 2u + len;
}

/* a profile's chunks joined by their numbers, not their order; none
   where one is missing */
TEST_IMPL(jpg_icc) {
  char     path[1024];
  uint8_t *d, *o;
  size_t   n, at, k;
  ImImage *im;

  ASSERT((d = read_all("gray.jpg", &n)));
  ASSERT((o = malloc(n + 1024u)));
  snprintf(path, sizeof(path), "%s/icc.jpg", IM_TEST_TMP);

  /* chunk 2 (100 × 0xBB) before chunk 1 (200 × 0xAA) */
  memcpy(o, d, 2u);
  at = 2u;
  at += icc_chunk(o + at, 2u, 2u, 0xBBu, 100u);
  at += icc_chunk(o + at, 1u, 2u, 0xAAu, 200u);
  memcpy(o + at, d + 2, n - 2u);
  ASSERT(write_all(path, o, at + n - 2u));
  ASSERT((im = load(path, IM_BYTEORDER_ANY)));
  ASSERT(im->iccProfile && im->iccProfileSize == 300u);
  for (k = 0; k < 300u; k++) ASSERT(im->iccProfile[k] == (k < 200u ? 0xAAu : 0xBBu));
  im_free(im);

  /* chunk 1 of 2 alone */
  at = 2u;
  at += icc_chunk(o + at, 1u, 2u, 0xAAu, 200u);
  memcpy(o + at, d + 2, n - 2u);
  ASSERT(write_all(path, o, at + n - 2u));
  ASSERT((im = load(path, IM_BYTEORDER_ANY)));
  ASSERT(!im->iccProfile && !im->iccProfileSize);
  im_free(im);

  remove(path);
  free(o);
  free(d);

  TEST_SUCCESS
}

/* cut short anywhere: as far as it goes (its size whole), or nothing */
TEST_IMPL(jpg_truncated) {
  static const char *names[] = { "420.jpg", "prog.jpg", "arith-prog.jpg", "ll7-pt4.jpg", "ycck.jpg" };
  size_t             k;

  for (k = 0; k < sizeof(names) / sizeof(names[0]); k++) {
    char     part[1024];
    uint8_t *d;
    size_t   n, cut;

    ASSERT((d = read_all(names[k], &n)));
    snprintf(part, sizeof(part), "%s/cut-%s", IM_TEST_TMP, names[k]);
    for (cut = 0; cut < n; cut += 7u) {
      ImImage *im;

      ASSERT(write_all(part, d, cut));
      if ((im = load(part, IM_BYTEORDER_ANY))) {
        ASSERT(im->width == 37u && im->height == 23u && im->data.data);
        im_free(im);
      }
    }
    remove(part);
    free(d);
  }

  TEST_SUCCESS
}

/* cut inside a segment after a scan (here a DHT between progressive
   scans): the scans so far, as if EOI came where the segment begins */
/* hierarchical: the lossless one the picture (ll1.jpg's pixels) where
   its reference's samples are copied, one less at most where they're
   interpolated (its encoder rounds there, the spec truncates); DHP's size
   in the headers' word; cut in its last frame, the rest of that frame's
   differences none; without DHP, a second frame refused */
TEST_IMPL(jpg_hierarchical) {
  ImImage *a = fixture("hier-ll.jpg", IM_BYTEORDER_LITTLE), *b = fixture("ll1.jpg", IM_BYTEORDER_LITTLE);
  ImInfo   info;
  uint8_t *d;
  size_t   len, x, y, k, at;

  ASSERT(a && b && a->len == b->len && a->width == 37u);
  for (y = 0; y < a->height; y++) {
    for (x = 0; x < a->width; x++) {
      for (k = 0; k < 3u; k++) {
        int u = ((const uint8_t *)a->data.data)[(y * a->width + x) * 3u + k];
        int v = ((const uint8_t *)b->data.data)[(y * a->width + x) * 3u + k];

        ASSERT((x | y) & 1u ? v - u == 0 || v - u == 1 : u == v);
      }
    }
  }
  im_free(a);
  im_free(b);

  ASSERT((d = read_all("hier-arith.jpg", &len)));
  ASSERT(im_info_mem(&info, d, len) == IM_OK);
  ASSERT(info.width == 37u && info.height == 23u && info.components == 3u && info.coding == IM_CODING_HIERARCHICAL);
  ASSERT(info.arithmetic);
  free(d);

  ASSERT((d = read_all("hier.jpg", &len)));
  /* cut in the differential frame's data */
  {
    ImImage *cut = NULL;

    ASSERT(im_load_mem(&cut, d, len - 200u, NULL, IM_OPEN_INTENT_READONLY) == IM_OK);
    ASSERT(cut && cut->width == 37u && cut->height == 23u);
    im_free(cut);
  }
  /* DHP made an APP5: two frames, refused */
  for (at = 2; at + 1u < len && !(d[at] == 0xFFu && d[at + 1u] == 0xDEu); at++) {
  }
  ASSERT(at + 1u < len);
  d[at + 1u] = 0xE5u;
  {
    ImImage *two = NULL;

    ASSERT(im_load_mem(&two, d, len, NULL, IM_OPEN_INTENT_READONLY) != IM_OK && !two);
  }
  free(d);

  TEST_SUCCESS
}

TEST_IMPL(jpg_cut_segment) {
  uint8_t *d;
  size_t   n, at = 2u, scans = 0u, dht = 0u;

  ASSERT((d = read_all("prog.jpg", &n)));
  /* the first DHT after a scan */
  while (at + 4u <= n && !dht) {
    uint32_t m = d[at + 1u], len = (uint32_t)d[at + 2u] << 8 | d[at + 3u];

    if (m == 0xC4u && scans) {
      dht = at;
      break;
    }
    at += 2u + len;
    if (m == 0xDAu) {
      /* past the scan's data: the next marker but RSTn and stuffing */
      while (at + 1u < n && !(d[at] == 0xFFu && d[at + 1u] && d[at + 1u] != 0xFFu && (d[at + 1u] < 0xD0u || d[at + 1u] > 0xD7u))) at++;
      scans++;
    }
  }
  ASSERT(dht && dht + 12u < n);
  {
    ImImage *a = NULL, *b = NULL;
    uint8_t *e = malloc(dht + 2u);

    /* the cut one, and the same with an EOI where the DHT began */
    ASSERT(e);
    memcpy(e, d, dht);
    e[dht]      = 0xFFu;
    e[dht + 1u] = 0xD9u;
    ASSERT(im_load_mem(&a, d, dht + 12u, NULL, IM_OPEN_INTENT_READONLY) == IM_OK && a);
    ASSERT(im_load_mem(&b, e, dht + 2u, NULL, IM_OPEN_INTENT_READONLY) == IM_OK && b);
    ASSERT(a->len == b->len && !memcmp(a->data.data, b->data.data, a->len));
    im_free(a);
    im_free(b);
    free(e);
  }
  free(d);

  TEST_SUCCESS
}

/* the first `m` segment's place (its FF) before the first scan, or 0 */
static size_t
segment_at(const uint8_t *d, size_t n, uint32_t m) {
  size_t at = 2u;

  while (at + 4u <= n && d[at] == 0xFFu) {
    if (d[at + 1u] == m) return at;
    if (d[at + 1u] == 0xDAu) break;
    at += 2u + ((size_t)d[at + 2u] << 8 | d[at + 3u]);
  }
  return 0u;
}

/* refused before anything is decoded where it will be anyway: a scan whose
   component's quantization table no DQT gave (as libjpeg); and a big frame
   from little data whose stream goes wrong after its scan (its structure
   read first), while the same frame whole decodes (the rest gray) */
TEST_IMPL(jpg_refused_early) {
  static const uint8_t stray[] = { 0xFFu, 0xDAu, 0x00u, 0x08u, 0x01u, 0x09u, 0x00u, 0x00u, 0x3Fu, 0x00u, 0xFFu, 0xD9u };
  static const char   *names[] = { "420.jpg", "prog.jpg" };
  size_t               k;

  for (k = 0; k < sizeof(names) / sizeof(names[0]); k++) {
    ImImage *im = NULL;
    uint8_t *d, *e;
    size_t   n, q, sof, len;

    ASSERT((d = read_all(names[k], &n)) && (e = malloc(n + sizeof(stray))));
    ASSERT((q = segment_at(d, n, 0xDBu)) && (sof = segment_at(d, n, k ? 0xC2u : 0xC0u)));

    /* its first DQT left out */
    len = 2u + ((size_t)d[q + 2u] << 8 | d[q + 3u]);
    memcpy(e, d, q);
    memcpy(e + q, d + q + len, n - q - len);
    ASSERT(im_load_mem(&im, e, n - len, NULL, IM_OPEN_INTENT_READONLY) != IM_OK && !im);

    /* 2048×2048 from the same data */
    memcpy(e, d, n);
    e[sof + 5u] = e[sof + 7u] = 0x08u;
    e[sof + 6u] = e[sof + 8u] = 0x00u;
    ASSERT(im_load_mem(&im, e, n, NULL, IM_OPEN_INTENT_READONLY) == IM_OK && im);
    ASSERT(im->width == 2048u && im->height == 2048u);
    im_free(im);
    im = NULL;

    /* and a scan after its end naming no component it has */
    ASSERT(e[n - 2u] == 0xFFu && e[n - 1u] == 0xD9u);
    memcpy(e + n - 2u, stray, sizeof(stray));
    ASSERT(im_load_mem(&im, e, n - 2u + sizeof(stray), NULL, IM_OPEN_INTENT_READONLY) != IM_OK && !im);
    free(e);
    free(d);
  }

  TEST_SUCCESS
}
