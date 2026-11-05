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
 * JPEG 2000 (io/jp2) on data/jp2's fixtures (make.sh made them): each one's
 * pixels hashed as the PDF engine's test_jpx hashes its decode, which is
 * PDFium's (OpenJPEG's) to the byte, with im's own layouts (CMYK, gray and
 * opacity, deeper samples in 16 bits, a premultiplied opacity kept); 16-bit
 * samples as ffmpeg decodes them; ImageIO's file, large enough for the
 * threads, and its ICC profile; every fifth cut of two files decoding as
 * far as it goes, or not at all.
 */

#include "../include/common.h"

#ifndef IM_TEST_DATA
#  define IM_TEST_DATA "data"
#endif

#ifndef IM_TEST_TMP
#  define IM_TEST_TMP "."
#endif

#define FNV_BASIS 2166136261u

static uint32_t
fnv(uint32_t h, uint32_t byte) {
  return (h ^ (byte & 0xFFu)) * 16777619u;
}

static ImImage*
load(const char *path, ImByteOrder order) {
  ImImage               *im;
  im_option_byteorder_t  bo;

  bo = im_option_row_byteorder(order);
  im = NULL;
  if (im_load(&im, path, IM_OPTIONS { &bo.base, NULL }, IM_OPEN_INTENT_READONLY) != IM_OK) {
    if (im) im_free(im);
    return NULL;
  }

  return im;
}

static ImImage*
fixture(const char *name, ImByteOrder order) {
  char path[1024];

  snprintf(path, sizeof(path), "%s/jp2/%s", IM_TEST_DATA, name);

  return load(path, order);
}

/* the PDF engine's test_jpx hash of `im`: its colours (an 0xFF after an
   RGB pixel's three, the engine's RGBX), 16-bit samples taken back to their
   `depth` and brought to 8 as the engine does (their top 8 bits and the
   next one); its opacity apart. False: a 16-bit sample isn't one of the
   depth's spread over 0–65535 */
static bool
pdf_hash(const ImImage *im, uint32_t depth, uint32_t *colour, uint32_t *alpha) {
  uint32_t ncol, comps, max, i, u;
  size_t   n;
  bool     wide;

  comps   = im->componentsPerPixel;
  ncol    = im->alphaInfo == IM_ALPHA_NONE ? comps : comps - 1u;
  wide    = im->bitsPerComponent == 16u;
  max     = (1u << depth) - 1u;
  n       = (size_t)im->width * im->height;
  *colour = *alpha = FNV_BASIS;

  for (i = 0; i < n; i++) {
    for (u = 0; u < comps; u++) {
      uint32_t v, s;

      if (wide) {
        s = ((const uint16_t *)im->data.data)[i * comps + u];
        v = depth == 16u ? s : (uint32_t)(((uint64_t)s * max + 32767u) / 65535u);
        if (depth < 16u && (uint32_t)(((uint64_t)v * 65535u + max / 2u) / max) != s)
          return false;
        v = (v >> (depth - 8u)) + ((v >> (depth - 9u)) & 1u);
        v = v > 255u ? 255u : v;
      } else {
        v = ((const uint8_t *)im->data.data)[i * comps + u];
      }

      if (u < ncol) *colour = fnv(*colour, v);
      else          *alpha  = fnv(*alpha, v);
    }

    if (ncol == 3u) *colour = fnv(*colour, 0xFFu);
  }

  return true;
}

TEST_IMPL(jp2_fixtures) {
  enum { RGB = IM_COLORSPACE_sRGB, GRAY = IM_COLORSPACE_GRAY, CMYK = IM_COLORSPACE_CMYK };
  enum { JP2 = IM_FILEFORMATTYPE_JP2, J2K = IM_FILEFORMATTYPE_J2K };
  enum { NONE = IM_ALPHA_NONE, LAST = IM_ALPHA_LAST, PREMUL = IM_ALPHA_PREMUL_LAST };
  static const struct {
    const char *name;
    ImFormat    format;
    uint32_t    bits, depth, type, space, alpha_info;
    uint32_t    hash, alpha;
  } cases[] = {
    /* 5/3, the reversible transform */
    { "rct.jp2",         IM_FORMAT_RGB,        8,  8,  JP2, RGB,  NONE,   0x6047FBEDu, 0 },
    /* a bare codestream: 9/7, tiles, RPCL, SOP and EPH */
    { "ict-tiles.j2k",   IM_FORMAT_RGB,        8,  8,  J2K, RGB,  NONE,   0x868E41C2u, 0 },
    /* layers, PCRL */
    { "gray-layers.jp2", IM_FORMAT_GRAY,       8,  8,  JP2, GRAY, NONE,   0x937121C4u, 0 },
    /* sYCC, chroma subsampled */
    { "sycc420.jp2",     IM_FORMAT_RGB,        8,  8,  JP2, RGB,  NONE,   0x5CE39A76u, 0 },
    /* pclr and cmap, expanded */
    { "palette.jp2",     IM_FORMAT_RGB,        8,  8,  JP2, RGB,  NONE,   0xE1C2F643u, 0 },
    /* an opacity channel, CPRL */
    { "alpha.jp2",       IM_FORMAT_RGBA,       8,  8,  JP2, RGB,  LAST,   0x6047FBEDu, 0x4BCBF784u },
    /* 16-bit samples, kept */
    { "gray16.jp2",      IM_FORMAT_GRAY,       16, 16, JP2, GRAY, NONE,   0xC1EBF56Eu, 0 },
    /* every code-block style (rct.jp2's bits read with them) */
    { "styles.jp2",      IM_FORMAT_RGB,        8,  8,  JP2, RGB,  NONE,   0xE0E906C5u, 0 },
    /* ict-tiles.j2k's packet headers in PPT, */
    { "ppt.j2k",         IM_FORMAT_RGB,        8,  8,  J2K, RGB,  NONE,   0x868E41C2u, 0 },
    /* in PPM, */
    { "ppm.j2k",         IM_FORMAT_RGB,        8,  8,  J2K, RGB,  NONE,   0x868E41C2u, 0 },
    /* its tiles in two tile-parts each */
    { "parts.j2k",       IM_FORMAT_RGB,        8,  8,  J2K, RGB,  NONE,   0x868E41C2u, 0 },
    /* gray-layers.jp2's packets under a POC */
    { "poc.j2k",         IM_FORMAT_GRAY,       8,  8,  J2K, GRAY, NONE,   0x937121C4u, 0 },
    /* four components bare: CMYK (its opacity, as the engine has it, none) */
    { "cmyk.j2k",        IM_FORMAT_CMYK,       8,  8,  J2K, CMYK, NONE,   0x6047FBEDu, 0 },
    /* gray and opacity, 9/7 */
    { "gray-alpha.jp2",  IM_FORMAT_GRAY_ALPHA, 8,  8,  JP2, GRAY, LAST,   0x8BC82C1Eu, 0x4BCBF784u },
    /* 10-bit samples, spread over 16 */
    { "rgb10.jp2",       IM_FORMAT_RGB,        16, 10, JP2, RGB,  NONE,   0x6047FBEDu, 0 },
    /* alpha.jp2's opacity premultiplied, by cdef: kept so */
    { "premult.jp2",     IM_FORMAT_RGBA,       8,  8,  JP2, RGB,  PREMUL, 0x6047FBEDu, 0x4BCBF784u },
  };
  size_t i;

  for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    ImImage *im;
    uint32_t comps, hash, alpha;

    im = fixture(cases[i].name, IM_BYTEORDER_ANY);
    ASSERT(im && im->data.data);

    comps = cases[i].format == IM_FORMAT_RGBA || cases[i].format == IM_FORMAT_CMYK ? 4u
          : cases[i].format == IM_FORMAT_RGB        ? 3u
          : cases[i].format == IM_FORMAT_GRAY_ALPHA ? 2u : 1u;

    ASSERT(im->width == 37u && im->height == 23u);
    ASSERT(im->format == cases[i].format);
    ASSERT(im->fileFormatType == (ImFileFormatType)cases[i].type);
    ASSERT(im->colorSpace == (ImColorSpace)cases[i].space);
    ASSERT(im->componentsPerPixel == comps);
    ASSERT(im->bitsPerComponent == cases[i].bits);
    ASSERT(im->bitsPerPixel == comps * cases[i].bits);
    ASSERT(im->bytesPerPixel == comps * cases[i].bits / 8u);
    ASSERT(im->len == (size_t)37u * 23u * im->bytesPerPixel);
    ASSERT(im->alphaInfo == (ImAlphaInfo)cases[i].alpha_info);
    ASSERT(!im->iccProfile && !im->pal);

    ASSERT(pdf_hash(im, cases[i].depth, &hash, &alpha));
    if (hash != cases[i].hash || (cases[i].alpha && alpha != cases[i].alpha)) {
      fprintf(stderr, "  %s: 0x%08X 0x%08X\n", cases[i].name, hash, alpha);
    }

    ASSERT(hash == cases[i].hash);
    ASSERT(!cases[i].alpha || alpha == cases[i].alpha);

    im_free(im);
  }

  TEST_SUCCESS
}

/* gray16.jp2 (lossless) as ffmpeg's decoder gives it, in the order asked */
TEST_IMPL(jp2_16bit) {
  ImImage  *le, *be, *host, *any;
  uint32_t  h;
  size_t    i, n;
  uint16_t  one;

  le   = fixture("gray16.jp2", IM_BYTEORDER_LITTLE);
  be   = fixture("gray16.jp2", IM_BYTEORDER_BIG);
  host = fixture("gray16.jp2", IM_BYTEORDER_HOST);
  any  = fixture("gray16.jp2", IM_BYTEORDER_ANY);
  ASSERT(le && be && host && any);

  n = (size_t)37u * 23u * 2u;
  ASSERT(le->len == n && be->len == n && host->len == n && any->len == n);
  ASSERT(le->byteOrder == IM_BYTEORDER_LITTLE && be->byteOrder == IM_BYTEORDER_BIG);
  ASSERT(host->byteOrder == IM_BYTEORDER_HOST && any->byteOrder == IM_BYTEORDER_HOST);

  /* ffmpeg -i gray16.jp2 -f rawvideo -pix_fmt gray16le */
  h = FNV_BASIS;
  for (i = 0; i < n; i++) h = fnv(h, ((const uint8_t *)le->data.data)[i]);
  ASSERT(h == 0x2346478Au);

  for (i = 0; i < n; i += 2u) {
    const uint8_t *l = (const uint8_t *)le->data.data + i, *b = (const uint8_t *)be->data.data + i;

    ASSERT(l[0] == b[1] && l[1] == b[0]);
  }

  one = 1u;
  ASSERT(!memcmp(host->data.data, *(uint8_t *)&one ? le->data.data : be->data.data, n));
  ASSERT(!memcmp(any->data.data, host->data.data, n));

  im_free(le);
  im_free(be);
  im_free(host);
  im_free(any);

  TEST_SUCCESS
}

/* ImageIO's file: 307,200 pixels decode on the threads to the PDF engine's
   pixels; its sRGB profile comes with them */
TEST_IMPL(jp2_threads) {
  ImImage       *im;
  const uint8_t *icc;
  uint32_t       hash, alpha;

  im = fixture("imageio.jp2", IM_BYTEORDER_ANY);
  ASSERT(im && im->data.data);
  ASSERT(im->width == 640u && im->height == 480u);
  ASSERT(im->format == IM_FORMAT_RGB && im->bitsPerComponent == 8u);
  ASSERT(im->fileFormatType == IM_FILEFORMATTYPE_JP2);

  ASSERT(pdf_hash(im, 8u, &hash, &alpha));
  if (hash != 0xF1F2CB8Du) fprintf(stderr, "  imageio.jp2: 0x%08X\n", hash);
  ASSERT(hash == 0xF1F2CB8Du);

  icc = im->iccProfile;
  ASSERT(icc && im->iccProfileSize == 3144u);
  ASSERT(((uint32_t)icc[0] << 24 | (uint32_t)icc[1] << 16 | (uint32_t)icc[2] << 8 | icc[3]) == 3144u);
  ASSERT(!memcmp(icc + 16, "RGB ", 4) && !memcmp(icc + 36, "acsp", 4));
  ASSERT(im->colorSpace == IM_COLORSPACE_sRGB);

  im_free(im);

  TEST_SUCCESS
}

/* cut short anywhere: as far as it goes (its size whole), or nothing */
TEST_IMPL(jp2_truncated) {
  static const char *names[] = { "ict-tiles.j2k", "alpha.jp2" };
  uint8_t           *d;
  size_t             k, n, cut;

  for (k = 0; k < sizeof(names) / sizeof(names[0]); k++) {
    char  path[1024], part[1024];
    FILE *f;
    long  size;

    snprintf(path, sizeof(path), "%s/jp2/%s", IM_TEST_DATA, names[k]);
    snprintf(part, sizeof(part), "%s/cut-%s", IM_TEST_TMP, names[k]);

    ASSERT((f = fopen(path, "rb")));
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    ASSERT(size > 0 && (d = malloc((size_t)size)));
    n = fread(d, 1, (size_t)size, f);
    fclose(f);
    ASSERT(n == (size_t)size);

    for (cut = 0; cut < n; cut += 5u) {
      ImImage *im;

      ASSERT((f = fopen(part, "wb")));
      ASSERT(fwrite(d, 1, cut, f) == cut);
      fclose(f);

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
