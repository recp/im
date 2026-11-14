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
 * im_load_mem and the decoders' options, each against what it changes:
 * from memory the file's pixels; a JPEG's colour transform turned off
 * gives its YCbCr, which libjpeg's integer conversion (here) turns into
 * the usual RGB, and turned on an RGB file's samples taken as YCbCr; an
 * Adobe CMYK left inverted is 255 minus the usual; RGB padded is the RGB
 * with 255 (65535) after each pixel; a JPEG 2000 palette's indexes, one
 * gray channel each pixel mapping to one colour of the usual decode; 8
 * bits OpenJPEG's rounding of the 16 (taken back to their depth); the
 * opacity apart (the colours un-premultiplied) or not decoded; one thread
 * what many give; im_info_mem the headers' word; decoded smaller, as
 * libjpeg's and OpenJPEG's (their pixels hashed; a JPEG 2000 palette's at
 * its own size).
 */

#include "../include/common.h"

#if !defined(_WIN32) && !defined(__wasi__)
#  include <pthread.h>
#  define TEST_THREADS 1
#endif

#ifndef IM_TEST_DATA
#  define IM_TEST_DATA "data"
#endif

static uint8_t*
read_all(const char *dir, const char *name, size_t *len) {
  char     path[1024];
  FILE    *f;
  long     size;
  uint8_t *d;

  snprintf(path, sizeof(path), "%s/%s/%s", IM_TEST_DATA, dir, name);
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

/* `dir/name` from memory, with up to two options (NULL none) */
static ImImage*
mem(const char *dir, const char *name, im_option_base_t *a, im_option_base_t *b) {
  ImImage          *im = NULL;
  size_t            len = 0;
  uint8_t          *d = read_all(dir, name, &len);
  im_option_base_t *opts[3] = { a, a ? b : NULL, NULL };

  if (!d) return NULL;
  if (im_load_mem(&im, d, len, opts, IM_OPEN_INTENT_READONLY) != IM_OK) im = NULL;
  memset(d, 0xA5, len); /* nothing of it may be kept */
  free(d);
  return im;
}

static ImImage*
file(const char *dir, const char *name) {
  char     path[1024];
  ImImage *im = NULL;

  snprintf(path, sizeof(path), "%s/%s/%s", IM_TEST_DATA, dir, name);
  if (test_load(&im, path, NULL) != IM_OK) return NULL;
  return im;
}

static bool
same(const ImImage *a, const ImImage *b) {
  return a && b && a->width == b->width && a->height == b->height && a->format == b->format
      && a->bitsPerComponent == b->bitsPerComponent && a->len == b->len && !memcmp(a->data.data, b->data.data, a->len)
      && a->iccProfileSize == b->iccProfileSize
      && (!a->iccProfileSize || !memcmp(a->iccProfile, b->iccProfile, a->iccProfileSize));
}

/* libjpeg's YCbCr to RGB (jdcolor.c's tables), one pixel */
static void
ycc_rgb(int y, int cb, int cr, uint8_t *o) {
  const int64_t half = (int64_t)1 << 15;
  int64_t       r, g, b;

  cb -= 128;
  cr -= 128;
  r = y + ((((int64_t)(1.40200 * 65536 + 0.5)) * cr + half) >> 16);
  g = y + ((-((int64_t)(0.34414 * 65536 + 0.5)) * cb - ((int64_t)(0.71414 * 65536 + 0.5)) * cr + half) >> 16);
  b = y + ((((int64_t)(1.77200 * 65536 + 0.5)) * cb + half) >> 16);
  o[0] = (uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r);
  o[1] = (uint8_t)(g < 0 ? 0 : g > 255 ? 255 : g);
  o[2] = (uint8_t)(b < 0 ? 0 : b > 255 ? 255 : b);
}

TEST_IMPL(load_mem) {
  static const struct { const char *dir, *name; } files[] = {
    { "jpg", "420.jpg" }, { "jpg", "prog.jpg" }, { "jpg", "ll-p12.jpg" }, { "jpg", "cmyk.jpg" },
    { "jpg", "threads.jpg" }, { "jp2", "rct.jp2" }, { "jp2", "ict-tiles.j2k" }, { "jp2", "imageio.jp2" },
    { "jp2", "alpha.jp2" }, { "jp2", "gray16.jp2" },
  };
  static const uint8_t junk[16] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
  ImImage *im = (ImImage *)(uintptr_t)1;
  size_t   i;

  for (i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
    ImImage *a = file(files[i].dir, files[i].name), *b = mem(files[i].dir, files[i].name, NULL, NULL);

    if (!same(a, b)) fprintf(stderr, "  %s differs from memory\n", files[i].name);
    ASSERT(same(a, b));
    ASSERT(a->fileFormatType == b->fileFormatType);
    im_free(a);
    im_free(b);
  }

  /* not one of them, or not enough: nothing */
  ASSERT(im_load_mem(&im, junk, sizeof(junk), NULL, IM_OPEN_INTENT_READONLY) == IM_EBADF && !im);
  im = (ImImage *)(uintptr_t)1;
  ASSERT(im_load_mem(&im, junk, 2, NULL, IM_OPEN_INTENT_READONLY) == IM_EBADF && !im);
  ASSERT(im_load_mem(NULL, junk, sizeof(junk), NULL, IM_OPEN_INTENT_READONLY) == IM_EBADF);

  TEST_SUCCESS
}

TEST_IMPL(jpg_options) {
  im_option_int_t  off = im_option_int(IM_OPTION_JPEG_COLOR_TRANSFORM, 0);
  im_option_int_t  on = im_option_int(IM_OPTION_JPEG_COLOR_TRANSFORM, 1);
  im_option_int_t  guess = im_option_int(IM_OPTION_JPEG_COLOR_TRANSFORM, -1);
  im_option_int_t  one = im_option_int(IM_OPTION_THREADS, 1);
  im_option_bool_t stored = im_option_bool(IM_OPTION_CMYK_AS_STORED, true);
  im_option_bool_t pad = im_option_bool(IM_OPTION_RGB_PADDED, true);
  ImImage         *a, *b;
  size_t           i, n;

  /* transform off: YCbCr as it is, libjpeg's conversion of it the usual */
  a = mem("jpg", "420.jpg", NULL, NULL);
  b = mem("jpg", "420.jpg", &off.base, NULL);
  ASSERT(a && b && a->len == b->len && memcmp(a->data.data, b->data.data, a->len));
  for (i = 0, n = (size_t)a->width * a->height; i < n; i++) {
    const uint8_t *y = (const uint8_t *)b->data.data + 3u * i;
    uint8_t        rgb[3];

    ycc_rgb(y[0], y[1], y[2], rgb);
    ASSERT(!memcmp(rgb, (const uint8_t *)a->data.data + 3u * i, 3));
  }
  im_free(b);
  /* as the file says, and on where it says so already: the same */
  ASSERT(same(a, b = mem("jpg", "420.jpg", &guess.base, NULL)));
  im_free(b);
  ASSERT(same(a, b = mem("jpg", "420.jpg", &on.base, NULL)));
  im_free(b);
  im_free(a);

  /* an RGB file taken as YCbCr */
  a = mem("jpg", "rgb.jpg", NULL, NULL);
  b = mem("jpg", "rgb.jpg", &on.base, NULL);
  ASSERT(a && b && a->len == b->len);
  for (i = 0, n = (size_t)a->width * a->height; i < n; i++) {
    const uint8_t *y = (const uint8_t *)a->data.data + 3u * i;
    uint8_t        rgb[3];

    ycc_rgb(y[0], y[1], y[2], rgb);
    ASSERT(!memcmp(rgb, (const uint8_t *)b->data.data + 3u * i, 3));
  }
  im_free(a);
  im_free(b);

  /* Adobe's CMYK (and YCCK's) as stored: inverted; plain CMYK as it is */
  {
    static const char *const names[] = { "cmyk.jpg", "ycck.jpg", "cmyk-plain.jpg" };

    for (i = 0; i < 3u; i++) {
      size_t k;

      a = mem("jpg", names[i], NULL, NULL);
      b = mem("jpg", names[i], &stored.base, NULL);
      ASSERT(a && b && a->len == b->len && b->format == IM_FORMAT_CMYK);
      for (k = 0; k < a->len; k++) {
        uint8_t u = ((const uint8_t *)a->data.data)[k], s = ((const uint8_t *)b->data.data)[k];

        ASSERT(i < 2u ? s == 255u - u : s == u);
      }
      im_free(a);
      im_free(b);
    }
  }

  /* RGB0: the RGB, then 255; 16-bit: 65535 */
  {
    static const char *const names[] = { "420.jpg", "p12.jpg", "ll-p2.jpg" };

    for (i = 0; i < 3u; i++) {
      size_t   k, w = i == 1u ? 2u : 1u;
      uint32_t c;

      a = mem("jpg", names[i], NULL, NULL);
      b = mem("jpg", names[i], &pad.base, NULL);
      ASSERT(a && b && b->format == IM_FORMAT_RGB0 && b->componentsPerPixel == 4u);
      ASSERT(b->alphaInfo == IM_ALPHA_NONE_SKIP_LAST && b->bitsPerComponent == a->bitsPerComponent);
      ASSERT(b->bytesPerPixel == 4u * w && b->len == (size_t)b->width * b->height * 4u * w);
      for (k = 0, n = (size_t)a->width * a->height; k < n; k++) {
        const uint8_t *p = (const uint8_t *)a->data.data + 3u * w * k, *q = (const uint8_t *)b->data.data + 4u * w * k;

        ASSERT(!memcmp(p, q, 3u * w));
        for (c = 0; c < w; c++) ASSERT(q[3u * w + c] == 0xFFu);
      }
      im_free(a);
      im_free(b);
    }
    /* gray stays gray */
    a = mem("jpg", "gray.jpg", &pad.base, NULL);
    ASSERT(a && a->format == IM_FORMAT_GRAY && a->componentsPerPixel == 1u);
    im_free(a);
  }

  /* one thread: what many give */
  a = mem("jpg", "threads.jpg", NULL, NULL);
  ASSERT(same(a, b = mem("jpg", "threads.jpg", &one.base, NULL)));
  im_free(a);
  im_free(b);

  /* 2 and 5 components (libjpeg-turbo's encoder, JCS_UNKNOWN): as they
     are, no colour; 5 in two scans (4 interleaved, then 1) with restarts,
     2 progressive. Each the samples its components cut out alone decode
     to (libjpeg-turbo's) */
  {
    static const struct { const char *name; uint32_t n, hash; } nc[] = {
      { "ncomp2.jpg", 2u, 0xC10FEC9Bu },
      { "ncomp5.jpg", 5u, 0xE49B8409u },
    };

    for (i = 0; i < sizeof(nc) / sizeof(nc[0]); i++) {
      uint32_t h = 2166136261u;

      a = mem("jpg", nc[i].name, NULL, NULL);
      ASSERT(a && a->width == 37u && a->height == 23u && a->componentsPerPixel == nc[i].n && a->bitsPerComponent == 8u);
      ASSERT(a->format == IM_FORMAT_NONE && a->colorSpace == IM_COLORSPACE_UNKNOWN && a->len == (size_t)37u * 23u * nc[i].n);
      for (n = 0; n < a->len; n++) h = (h ^ ((const uint8_t *)a->data.data)[n]) * 16777619u;
      if (h != nc[i].hash) fprintf(stderr, "  %s: 0x%08X\n", nc[i].name, h);
      ASSERT(h == nc[i].hash);
      ASSERT(same(a, b = mem("jpg", nc[i].name, &one.base, NULL)));
      im_free(a);
      im_free(b);
    }
  }

  TEST_SUCCESS
}

TEST_IMPL(jp2_options) {
  im_option_bool_t pad = im_option_bool(IM_OPTION_RGB_PADDED, true);
  im_option_bool_t idx = im_option_bool(IM_OPTION_PALETTE_INDEXES, true);
  im_option_int_t  one = im_option_int(IM_OPTION_THREADS, 1);
  ImImage         *a, *b;
  size_t           i, n;

  /* RGB0: the RGB, then the maximum (8 and 16 bits); RGBA as it is */
  {
    static const char *const names[] = { "rct.jp2", "rgb10.jp2", "sycc420.jp2" };

    for (i = 0; i < 3u; i++) {
      size_t   k, w;
      uint32_t c;

      a = mem("jp2", names[i], NULL, NULL);
      b = mem("jp2", names[i], &pad.base, NULL);
      ASSERT(a && b && a->format == IM_FORMAT_RGB);
      w = a->bitsPerComponent / 8u;
      ASSERT(b->format == IM_FORMAT_RGB0 && b->componentsPerPixel == 4u && b->alphaInfo == IM_ALPHA_NONE_SKIP_LAST);
      ASSERT(b->bytesPerPixel == 4u * w && b->len == (size_t)b->width * b->height * 4u * w);
      for (k = 0, n = (size_t)a->width * a->height; k < n; k++) {
        const uint8_t *p = (const uint8_t *)a->data.data + 3u * w * k, *q = (const uint8_t *)b->data.data + 4u * w * k;

        ASSERT(!memcmp(p, q, 3u * w));
        for (c = 0; c < w; c++) ASSERT(q[3u * w + c] == 0xFFu);
      }
      im_free(a);
      im_free(b);
    }
    a = mem("jp2", "alpha.jp2", NULL, NULL);
    ASSERT(same(a, b = mem("jp2", "alpha.jp2", &pad.base, NULL)) && b->format == IM_FORMAT_RGBA);
    im_free(a);
    im_free(b);
  }

  /* a palette's indexes: one gray channel, no profile; each index one of
     the usual decode's colours */
  {
    int32_t colour[256];

    a = mem("jp2", "palette.jp2", NULL, NULL);
    b = mem("jp2", "palette.jp2", &idx.base, NULL);
    ASSERT(a && b && a->format == IM_FORMAT_RGB && a->bitsPerComponent == 8u);
    ASSERT(b->format == IM_FORMAT_GRAY && b->componentsPerPixel == 1u && b->bitsPerComponent == 8u);
    ASSERT(b->colorSpace == IM_COLORSPACE_UNKNOWN && !b->iccProfile);
    ASSERT(b->width == a->width && b->height == a->height);
    for (i = 0; i < 256u; i++) colour[i] = -1;
    for (i = 0, n = (size_t)a->width * a->height; i < n; i++) {
      const uint8_t *p = (const uint8_t *)a->data.data + 3u * i;
      uint8_t        k = ((const uint8_t *)b->data.data)[i];
      int32_t        c = p[0] << 16 | p[1] << 8 | p[2];

      if (colour[k] < 0) colour[k] = c;
      ASSERT(colour[k] == c);
    }
    im_free(a);
    im_free(b);
    /* without a palette: the file's colours */
    a = mem("jp2", "rct.jp2", NULL, NULL);
    b = mem("jp2", "rct.jp2", &idx.base, NULL);
    ASSERT(a && b && b->format == IM_FORMAT_GRAY && b->len == (size_t)a->width * a->height);
    im_free(a);
    im_free(b);
  }

  /* 8 bits: OpenJPEG's (v >> d) + ((v >> (d - 1)) & 1), v the 16 taken
     back to the depth */
  {
    static const struct { const char *name; uint32_t depth; } deep[] = { { "rgb10.jp2", 10u }, { "gray16.jp2", 16u } };
    im_option_bool_t eight = im_option_bool(IM_OPTION_EIGHT_BITS, true);
    im_option_int_t  little = im_option_int(IM_OPTION_BYTE_ORDER, IM_BYTEORDER_LITTLE);

    for (i = 0; i < 2u; i++) {
      size_t   k;
      uint32_t max = (1u << deep[i].depth) - 1u, d = deep[i].depth - 8u;

      little.base.type = IM_OPTION_BYTE_ORDER;
      a = mem("jp2", deep[i].name, &little.base, NULL);
      b = mem("jp2", deep[i].name, &eight.base, NULL);
      ASSERT(a && b && a->bitsPerComponent == 16u && b->bitsPerComponent == 8u);
      ASSERT(b->len * 2u == a->len && b->componentsPerPixel == a->componentsPerPixel);
      for (k = 0; k < b->len; k++) {
        const uint8_t *q = (const uint8_t *)a->data.data + 2u * k;
        uint32_t       v16 = (uint32_t)q[0] | (uint32_t)q[1] << 8, v = (uint32_t)(((uint64_t)v16 * max + 32767u) / 65535u);
        uint32_t       e = (v >> d) + ((v >> (d - 1u)) & 1u);

        ASSERT(((const uint8_t *)b->data.data)[k] == (e > 255u ? 255u : e));
      }
      im_free(a);
      im_free(b);
    }
  }

  /* the opacity apart: the colours (un-premultiplied where the JP2 says
     premultiplied), the last sample to `alpha`; or not decoded at all */
  {
    static const char *const names[] = { "alpha.jp2", "premult.jp2", "gray-alpha.jp2" };
    im_option_int_t apart = im_option_int(IM_OPTION_OPACITY, IM_OPACITY_APART);
    im_option_int_t none = im_option_int(IM_OPTION_OPACITY, IM_OPACITY_NONE);

    for (i = 0; i < 3u; i++) {
      ImImage *c;
      uint32_t ncol, u;
      size_t   k;

      a = mem("jp2", names[i], NULL, NULL);
      b = mem("jp2", names[i], &apart.base, NULL);
      c = mem("jp2", names[i], &none.base, NULL);
      ASSERT(a && b && c && a->bitsPerComponent == 8u);
      ncol = a->componentsPerPixel - 1u;
      ASSERT(a->alphaInfo == (i == 1u ? IM_ALPHA_PREMUL_LAST : IM_ALPHA_LAST) && !a->alpha);
      ASSERT(b->componentsPerPixel == ncol && b->alphaInfo == IM_ALPHA_NONE && b->alpha);
      ASSERT(c->componentsPerPixel == ncol && c->alphaInfo == IM_ALPHA_NONE && !c->alpha);
      ASSERT(b->format == (ncol == 1u ? IM_FORMAT_GRAY : IM_FORMAT_RGB) && c->format == b->format);
      for (k = 0, n = (size_t)a->width * a->height; k < n; k++) {
        const uint8_t *p = (const uint8_t *)a->data.data + (ncol + 1u) * k;
        uint32_t       al = p[ncol];

        ASSERT(((const uint8_t *)b->alpha)[k] == al);
        for (u = 0; u < ncol; u++) {
          uint32_t o = p[u], want = i == 1u ? (al ? (o > al ? 255u : (o * 255u + al / 2u) / al) : 0u) : o;

          ASSERT(((const uint8_t *)b->data.data)[ncol * k + u] == want);
          ASSERT(((const uint8_t *)c->data.data)[ncol * k + u] == o);
        }
      }
      im_free(a);
      im_free(b);
      im_free(c);
    }
  }

  /* one thread: what many give */
  a = mem("jp2", "imageio.jp2", NULL, NULL);
  ASSERT(same(a, b = mem("jp2", "imageio.jp2", &one.base, NULL)));
  im_free(a);
  im_free(b);

  TEST_SUCCESS
}

/* the headers' word, nothing decoded */
TEST_IMPL(info_mem) {
  static const struct {
    const char *dir, *name;
    uint32_t    w, h, comps, bits, coding, levels;
    bool        arith, opacity;
  } cases[] = {
    { "jpg", "420.jpg",        37, 23, 3, 8,  IM_CODING_BASELINE,    0, false, false },
    { "jpg", "prog.jpg",       37, 23, 3, 8,  IM_CODING_PROGRESSIVE, 0, false, false },
    { "jpg", "p12.jpg",        37, 23, 3, 12, IM_CODING_EXTENDED,    0, false, false },
    { "jpg", "arith.jpg",      37, 23, 3, 8,  IM_CODING_EXTENDED,    0, true,  false },
    { "jpg", "arith-prog.jpg", 37, 23, 3, 8,  IM_CODING_PROGRESSIVE, 0, true,  false },
    { "jpg", "ll-p16.jpg",     37, 23, 3, 16, IM_CODING_LOSSLESS,    0, false, false },
    { "jpg", "gray.jpg",       37, 23, 1, 8,  IM_CODING_BASELINE,    0, false, false },
    { "jpg", "cmyk.jpg",       37, 23, 4, 8,  IM_CODING_BASELINE,    0, false, false },
    { "jpg", "dnl.jpg",        37, 23, 3, 8,  IM_CODING_BASELINE,    0, false, false }, /* the height a DNL's */
    { "jp2", "rct.jp2",        37, 23, 3, 8,  IM_CODING_JPEG2000,    7, false, false },
    { "jp2", "alpha.jp2",      37, 23, 4, 8,  IM_CODING_JPEG2000,    7, false, true  },
    { "jp2", "gray-alpha.jp2", 37, 23, 2, 8,  IM_CODING_JPEG2000,    7, false, true  },
    { "jp2", "gray16.jp2",     37, 23, 1, 16, IM_CODING_JPEG2000,    7, false, false },
    { "jp2", "imageio.jp2",    640, 480, 3, 8, IM_CODING_JPEG2000,   6, false, false },
    { "jp2", "cmyk.j2k",       37, 23, 4, 8,  IM_CODING_JPEG2000,    7, false, false },
  };
  static const uint8_t junk[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
  ImInfo info;
  size_t i;

  for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    size_t   len = 0;
    uint8_t *d = read_all(cases[i].dir, cases[i].name, &len);

    ASSERT(d && im_info_mem(&info, d, len) == IM_OK);
    if (info.width != cases[i].w || info.height != cases[i].h || info.components != cases[i].comps
        || info.bitsPerComponent != cases[i].bits || info.coding != (ImCoding)cases[i].coding
        || info.levels != cases[i].levels || info.arithmetic != cases[i].arith || info.opacity != cases[i].opacity) {
      fprintf(stderr, "  %s: %ux%u %u comps %u bits coding %d levels %u arith %d opacity %d\n", cases[i].name, info.width,
              info.height, info.components, info.bitsPerComponent, (int)info.coding, info.levels, info.arithmetic, info.opacity);
    }
    ASSERT(info.width == cases[i].w && info.height == cases[i].h && info.components == cases[i].comps);
    ASSERT(info.bitsPerComponent == cases[i].bits && info.coding == (ImCoding)cases[i].coding);
    ASSERT(info.levels == cases[i].levels && info.arithmetic == cases[i].arith && info.opacity == cases[i].opacity);
    ASSERT(info.fileFormatType == (!strcmp(cases[i].dir, "jpg") ? IM_FILEFORMATTYPE_JPEG
                                   : strstr(cases[i].name, ".j2k") ? IM_FILEFORMATTYPE_J2K : IM_FILEFORMATTYPE_JP2));
    ASSERT(info.pages == 1u);
    /* the bytes a decode read: a JPEG through its EOI, an inline image's
       end after it or not; a JPEG 2000 all of it */
    {
      size_t            got = 0;
      im_option_bytes_t br = im_option_bytes_read(&got);
      im_option_base_t *o[2] = { &br.base, NULL };
      uint8_t          *more = malloc(len + 6u);
      ImImage          *b;
      bool              eoi = len >= 2u && d[len - 2u] == 0xFFu && d[len - 1u] == 0xD9u;

      ASSERT(more);
      memcpy(more, d, len);
      memcpy(more + len, "\nEI Q\n", 6u);
      ASSERT(im_load_mem(&b, d, len, o, IM_OPEN_INTENT_READONLY) == IM_OK && b && got == len);
      im_free(b);
      ASSERT(im_load_mem(&b, more, len + 6u, o, IM_OPEN_INTENT_READONLY) == IM_OK && b);
      ASSERT(!strcmp(cases[i].dir, "jpg") && eoi ? got == len : got == len + 6u);
      im_free(b);
      free(more);
    }
    /* the first half alone (its headers whole) says the same */
    if (len > 8192u) {
      ImInfo head;

      ASSERT(im_info_mem(&head, d, len / 2u) == IM_OK && head.width == info.width && head.levels == info.levels);
    }
    free(d);
  }
  ASSERT(im_info_mem(&info, junk, sizeof(junk)) == IM_EBADF);

  TEST_SUCCESS
}

/* a caller's runner: in order on the caller's thread, or on threads of
   its own (items taken in order, each thread its worker number) */
typedef struct trun {
  uint32_t  calls, items;
  im_job_fn job;
  void     *arg;
  uint32_t  n, next;
  bool      ok;
#if TEST_THREADS
  pthread_mutex_t m;
#endif
} trun_t;

static bool
run_inline(void *ctx, im_job_fn job, void *arg, uint32_t n) {
  trun_t  *t  = ctx;
  bool     ok = true;
  uint32_t i;

  t->calls++;
  t->items += n;
  for (i = 0; i < n; i++) ok = job(arg, i % 4u, i) && ok;
  return ok;
}

#if TEST_THREADS
typedef struct tworker {
  trun_t  *t;
  uint32_t id;
} tworker_t;

static void*
run_main(void *p) {
  tworker_t *w = p;
  trun_t    *t = w->t;

  for (;;) {
    uint32_t i;

    pthread_mutex_lock(&t->m);
    i = t->next < t->n ? t->next++ : UINT32_MAX;
    pthread_mutex_unlock(&t->m);
    if (i == UINT32_MAX) break;
    if (!t->job(t->arg, w->id, i)) {
      pthread_mutex_lock(&t->m);
      t->ok = false;
      pthread_mutex_unlock(&t->m);
    }
  }
  return NULL;
}

static bool
run_threads(void *ctx, im_job_fn job, void *arg, uint32_t n) {
  trun_t   *t = ctx;
  pthread_t th[3];
  tworker_t w[4];
  uint32_t  i;

  t->calls++;
  t->items += n;
  t->job  = job;
  t->arg  = arg;
  t->n    = n;
  t->next = 0;
  t->ok   = true;
  for (i = 0; i < 4u; i++) {
    w[i].t  = t;
    w[i].id = i;
  }
  for (i = 0; i < 3u; i++) pthread_create(&th[i], NULL, run_main, &w[i + 1u]);
  run_main(&w[0]);
  for (i = 0; i < 3u; i++) pthread_join(th[i], NULL);
  return t->ok;
}
#endif

TEST_IMPL(runner) {
  static const struct { const char *dir, *name; } files[] = { { "jpg", "threads.jpg" }, { "jp2", "imageio.jp2" } };
  size_t i, k;

  for (i = 0; i < 2u; i++) {
    for (k = 0; k < 2u; k++) {
      trun_t             t;
      im_runner_t        r;
      im_option_runner_t opt;
      ImImage           *a, *b;

#if !TEST_THREADS
      if (k) continue;
#endif
      memset(&t, 0, sizeof(t));
#if TEST_THREADS
      pthread_mutex_init(&t.m, NULL);
      r.run = k ? run_threads : run_inline;
#else
      r.run = run_inline;
#endif
      r.ctx     = &t;
      r.workers = 4u;
      opt       = im_option_runner(r);
      a         = mem(files[i].dir, files[i].name, NULL, NULL);
      b         = mem(files[i].dir, files[i].name, &opt.base, NULL);
      ASSERT(a && b && a->len == b->len && !memcmp(a->data.data, b->data.data, a->len));
      ASSERT(t.calls > 0u && t.items >= t.calls);
      im_free(a);
      im_free(b);
#if TEST_THREADS
      pthread_mutex_destroy(&t.m);
#endif
    }
  }

  TEST_SUCCESS
}

/* into the caller's memory: each row there at its stride, the gaps left
   as they were, too little of it a failure */
TEST_IMPL(dest) {
  static const struct { const char *dir, *name; bool pad; } files[] = {
    { "jpg", "420.jpg", false }, { "jpg", "420.jpg", true }, { "jpg", "p12.jpg", false }, { "jpg", "threads.jpg", true },
    { "jp2", "rct.jp2", true }, { "jp2", "rgb10.jp2", false }, { "jp2", "alpha.jp2", false },
  };
  size_t i;

  for (i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
    im_option_bool_t pad = im_option_bool(IM_OPTION_RGB_PADDED, true);
    ImImage         *a = mem(files[i].dir, files[i].name, files[i].pad ? &pad.base : NULL, NULL), *b;
    size_t           row, stride, size, y, k;
    uint8_t         *buf;

    ASSERT(a && a->data.data);
    row    = (size_t)a->width * a->bytesPerPixel;
    stride = row + 14u;
    size   = stride * (a->height - 1u) + row;
    ASSERT((buf = malloc(size + 64u)));
    memset(buf, 0xAB, size + 64u);
    {
      im_option_dest_t d = im_option_dest(buf, stride, size);

      b = mem(files[i].dir, files[i].name, &d.base, files[i].pad ? &pad.base : NULL);
    }
    ASSERT(b && !b->data.data && b->row_pad_last == 14u && b->len == size);
    ASSERT(b->width == a->width && b->format == a->format && b->bitsPerComponent == a->bitsPerComponent);
    for (y = 0; y < a->height; y++) {
      ASSERT(!memcmp(buf + y * stride, (const uint8_t *)a->data.data + y * row, row));
      for (k = row; k < stride && y + 1u < a->height; k++) ASSERT(buf[y * stride + k] == 0xABu);
    }
    for (k = size; k < size + 64u; k++) ASSERT(buf[k] == 0xABu);
    im_free(b);
    /* a byte short: no image */
    {
      im_option_dest_t d = im_option_dest(buf, stride, size - 1u);

      b = mem(files[i].dir, files[i].name, &d.base, files[i].pad ? &pad.base : NULL);
      ASSERT(!b);
    }
    free(buf);
    im_free(a);
  }

  TEST_SUCCESS
}

static uint32_t
fnv1a(const uint8_t *p, size_t n) {
  uint32_t h = 2166136261u;
  size_t   i;

  for (i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

/* decoded smaller: a JPEG ½, ¼, ⅛ as libjpeg's scaled decode gives it (a
   lossless one at its size), a JPEG 2000 its finest levels left out as
   OpenJPEG's reduce; the size over the scale, rounded up; into the
   caller's memory at that size */
TEST_IMPL(scale) {
  static const struct {
    const char *dir, *name;
    uint32_t    scale, w, h, hash;
  } cases[] = {
    { "jpg", "420.jpg",        2,  19,  12, 0xF7664BE3u }, /* chroma's own block size: no upsampling */
    { "jpg", "420.jpg",        4,  10,   6, 0xE34090A3u },
    { "jpg", "420.jpg",        8,   5,   3, 0x2933CE47u },
    { "jpg", "422.jpg",        2,  19,  12, 0xD8D69540u },
    { "jpg", "440.jpg",        4,  10,   6, 0x2AE7CE1Au },
    { "jpg", "411.jpg",        2,  19,  12, 0xEB514F55u }, /* 2×2 chroma blocks still twice as wide */
    { "jpg", "411.jpg",        8,   5,   3, 0xA65B6D99u },
    { "jpg", "311.jpg",        4,  10,   6, 0x49484034u },
    { "jpg", "mixed.jpg",      2,  19,  12, 0xD0450488u },
    { "jpg", "mixed.jpg",      8,   5,   3, 0xC0F60D59u },
    { "jpg", "tiny.jpg",       8,   1,   1, 0x76595BC5u },
    { "jpg", "prog.jpg",       4,  10,   6, 0xE34090A3u }, /* 420.jpg's coefficients */
    { "jpg", "prog-cut.jpg",   2,  19,  12, 0x048ADAD2u }, /* smoothed, then smaller */
    { "jpg", "p12.jpg",        2,  19,  12, 0x1C8E4DD6u },
    { "jpg", "p12.jpg",        8,   5,   3, 0x9F1DE7BBu },
    { "jpg", "arith-prog.jpg", 8,   5,   3, 0x2933CE47u },
    { "jpg", "ycck.jpg",       4,  10,   6, 0xFB3DD00Eu },
    { "jpg", "gray.jpg",       2,  19,  12, 0x26851658u },
    { "jpg", "ll1.jpg",        8,  37,  23, 0x03A59D62u }, /* lossless: its size */
    { "jpg", "threads.jpg",    2, 389, 389, 0xBD5E6926u },
    { "jpg", "threads.jpg",    8,  98,  98, 0x4DBC78A0u },
    { "jp2", "rct.jp2",        2,  19,  12, 0x616860B0u },
    { "jp2", "rct.jp2",        8,   5,   3, 0x0644D8EAu },
    { "jp2", "ict-tiles.j2k",  4,  10,   6, 0x1272B1A3u },
    { "jp2", "sycc420.jp2",    2,  19,  12, 0xD7A5A5ABu },
    { "jp2", "palette.jp2",    4,  37,  23, 0x757C52B2u }, /* a palette's: its own size */
    { "jp2", "alpha.jp2",      8,   5,   3, 0x3CBFFA9Bu },
    { "jp2", "gray16.jp2",     2,  19,  12, 0xB817F4B4u },
    { "jp2", "poc.j2k",        4,  10,   6, 0xA377A9A6u },
    { "jp2", "styles.jp2",     8,   5,   3, 0xDF7269EFu },
    { "jp2", "imageio.jp2",    2, 320, 240, 0x9BC2E154u },
  };
  im_option_byteorder_t le = im_option_row_byteorder(IM_BYTEORDER_LITTLE);
  size_t                i;

  for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    im_option_int_t sc = im_option_int(IM_OPTION_SCALE, (int32_t)cases[i].scale);
    ImImage        *a = mem(cases[i].dir, cases[i].name, &sc.base, &le.base), *b;
    uint32_t        h;

    ASSERT(a && a->data.data && a->width == cases[i].w && a->height == cases[i].h);
    ASSERT(a->len == (size_t)a->width * a->height * a->bytesPerPixel);
    h = fnv1a(a->data.data, a->len);
    if (h != cases[i].hash) fprintf(stderr, "  %s at 1/%u: 0x%08X\n", cases[i].name, cases[i].scale, h);
    ASSERT(h == cases[i].hash);
    /* the caller's memory, as big as the smaller image */
    {
      size_t           row = (size_t)a->width * a->bytesPerPixel, size = row * a->height;
      uint8_t         *buf = malloc(size);
      im_option_dest_t d   = im_option_dest(buf, row, size);
      im_option_int_t  one = im_option_int(IM_OPTION_SCALE, (int32_t)cases[i].scale);
      im_option_base_t *opts[4] = { &one.base, &le.base, &d.base, NULL };

      ASSERT(buf);
      b = NULL;
      {
        char     path[1024];
        FILE    *f;
        long     n;
        uint8_t *data;

        snprintf(path, sizeof(path), "%s/%s/%s", IM_TEST_DATA, cases[i].dir, cases[i].name);
        ASSERT((f = fopen(path, "rb")));
        fseek(f, 0, SEEK_END);
        n = ftell(f);
        fseek(f, 0, SEEK_SET);
        ASSERT((data = malloc((size_t)n)) && fread(data, 1, (size_t)n, f) == (size_t)n);
        fclose(f);
        ASSERT(im_load_mem(&b, data, (size_t)n, opts, IM_OPEN_INTENT_READONLY) == IM_OK && b);
        ASSERT(!b->data.data && b->width == a->width && b->height == a->height && !memcmp(buf, a->data.data, size));
        im_free(b);
        free(data);
      }
      free(buf);
    }
    im_free(a);
  }
  /* other scales: the full size */
  {
    im_option_int_t three = im_option_int(IM_OPTION_SCALE, 3);
    ImImage        *a = mem("jpg", "420.jpg", &three.base, NULL);

    ASSERT(a && a->width == 37u && a->height == 23u);
    im_free(a);
  }
  /* a palette's indexes too: their own size, the full decode's */
  {
    im_option_int_t  two = im_option_int(IM_OPTION_SCALE, 2);
    im_option_bool_t ix  = im_option_bool(IM_OPTION_PALETTE_INDEXES, true);
    ImImage         *a = mem("jp2", "palette.jp2", &ix.base, NULL), *b = mem("jp2", "palette.jp2", &ix.base, &two.base);

    ASSERT(a && b && b->width == 37u && b->height == 23u && a->len == b->len && !memcmp(a->data.data, b->data.data, a->len));
    im_free(a);
    im_free(b);
  }

  TEST_SUCCESS
}

/* `data` with the options given (NULL ended), the result */
static ImResult
load_with(const uint8_t *data, size_t len, im_option_base_t **opts, ImImage **im) {
  *im = NULL;
  return im_load_mem(im, data, len, opts, IM_OPEN_INTENT_READONLY);
}

/* restart-bounded regions keep the same pixels at each scale, on one and
 * several threads, including padded RGB and caller-owned output rows.
 */
TEST_IMPL(jpg_restart_regions) {
  static const char *names[] = {
    "rst.jpg", "threads.jpg", "prog-rst.jpg", "arith-rst.jpg", "p12.jpg", "dnl.jpg", "prog-cut.jpg"
  };
  static const uint8_t padding[12] = {
    0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5
  };
  im_option_int_t    scale, threads;
  im_option_bool_t   padded;
  im_option_region_t region;
  im_option_dest_t   dest;
  im_option_base_t  *options[6];
  ImImage           *full, *part;
  const uint8_t     *expected;
  uint8_t           *data, *buffer, *pixels;
  size_t             len, pitch, bytes, i;
  uint32_t           s, n, pad, k, x, y, w, h, row, bpp;
  ImResult           result;

  for (i = 0u; i < sizeof(names) / sizeof(*names); i++) {
    len  = 0u;
    data = read_all("jpg", names[i], &len);
    ASSERT(data);
    for (s = 1u; s <= 8u; s *= 2u) {
      for (n = 1u; n <= 4u; n *= 4u) {
        for (pad = 0u; pad < 2u; pad++) {
          scale       = im_option_int(IM_OPTION_SCALE, (int32_t)s);
          threads     = im_option_int(IM_OPTION_THREADS, (int32_t)n);
          padded      = im_option_bool(IM_OPTION_RGB_PADDED, pad != 0u);
          options[0]  = &scale.base;
          options[1]  = &threads.base;
          options[2]  = &padded.base;
          options[3]  = options[4] = options[5] = NULL;
          ASSERT(load_with(data, len, options, &full) == IM_OK && full);
          bpp = full->bytesPerPixel;
          for (k = 0u; k < 9u; k++) {
            x = k % 3u == 0u ? 0u : k % 3u == 1u ? full->width / 2u : full->width - 1u;
            y = k / 3u == 0u ? 0u : k / 3u == 1u ? full->height / 2u : full->height - 1u;
            w = k & 1u ? full->width - x : 1u;
            h = k & 1u ? 1u : full->height - y;

            pitch  = (size_t)w * bpp + sizeof(padding);
            bytes  = pitch * h;
            buffer = malloc(bytes);
            ASSERT(buffer);
            memset(buffer, 0xA5, bytes);
            region = im_option_region(x, y, w, h);
            dest   = im_option_dest(buffer, pitch, bytes);

            options[3] = &region.base;
            options[4] = k & 1u ? &dest.base : NULL;
            result = load_with(data, len, options, &part);
            if (result != IM_OK)
              fprintf(stderr, "region %s scale=%u threads=%u pad=%u case=%u pitch=%zu result=%d\n",
                      names[i], s, n, pad, k, pitch, (int)result);
            ASSERT(result == IM_OK && part);
            ASSERT(part->width == w && part->height == h && part->bytesPerPixel == bpp);
            for (row = 0u; row < h; row++) {
              pixels   = k & 1u ? buffer + (size_t)row * pitch : (uint8_t *)part->data.data + (size_t)row * w * bpp;
              expected = (const uint8_t *)full->data.data + ((size_t)(y + row) * full->width + x) * bpp;
              ASSERT(!memcmp(pixels, expected, (size_t)w * bpp));
              if (k & 1u) {
                ASSERT(!memcmp(pixels + (size_t)w * bpp, padding, sizeof(padding)));
              }
            }
            im_free(part);
            free(buffer);
          }
          im_free(full);
        }
      }
    }
    free(data);
  }
  TEST_SUCCESS
}

TEST_IMPL(region) {
  static const char *names[] = {
    "420.jpg", "422.jpg", "440.jpg", "411.jpg", "311.jpg", "mixed.jpg", "444.jpg", "gray.jpg", "prog.jpg",
    "prog-cut.jpg", "prog-rst.jpg", "rst.jpg", "p12.jpg", "arith.jpg", "arith-prog.jpg", "arith-rst.jpg",
    "ycck.jpg", "cmyk.jpg", "ll1.jpg", "ll-420.jpg", "hier.jpg", "hier-prog.jpg", "dnl.jpg", "threads.jpg",
    "rct.jp2", "ict-tiles.j2k", "sycc420.jp2", "palette.jp2", "alpha.jp2", "gray16.jp2", "poc.j2k",
    "styles.jp2", "ht-irv-tiles.j2c", "ht-rev.jph", "parts.j2k", "imageio.jp2", "premult.jp2", "cmyk.j2k"
  };
  size_t i;

  for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    size_t   len = 0, nl = strlen(names[i]);
    uint8_t *data = read_all(nl > 4u && !strcmp(names[i] + nl - 4u, ".jpg") ? "jpg" : "jp2", names[i], &len);
    int32_t  scale;

    ASSERT(data);
    for (scale = 1; scale <= 2; scale++) {
      im_option_int_t   sc = im_option_int(IM_OPTION_SCALE, scale);
      im_option_base_t *o[4] = { &sc.base, NULL, NULL, NULL };
      ImImage          *full, *r;
      uint32_t          W, H, bpp, k;

      ASSERT(load_with(data, len, o, &full) == IM_OK && full);
      W   = full->width;
      H   = full->height;
      bpp = full->bytesPerPixel;
      /* corners, a pixel, odd places, one past the edges (cut), one outside */
      for (k = 0; k < 7u; k++) {
        static const uint32_t f[7][4] = { { 0, 0, 1, 1 }, { 1, 1, 0, 0 }, { 3, 5, 7, 9 }, { 1, 0, 2, 3 },
                                          { 0, 1, 1, 0 }, { 2, 2, 100000, 100000 }, { 0, 0, 1, 1 } };
        uint32_t x = k == 1u ? W - 1u : k == 3u ? W / 3u : k == 4u ? 0u : f[k][0] % W;
        uint32_t y = k == 1u ? H - 1u : k == 3u ? H / 2u : f[k][1] % H;
        uint32_t w = k == 1u ? 1u : k == 3u ? W - W / 3u : k == 4u ? W : f[k][2], h = k == 1u ? 1u : k == 3u ? 1u : k == 4u ? H : f[k][3];
        uint32_t cw = w < W - x ? w : W - x, ch = h < H - y ? h : H - y, yy;
        size_t   pitch = (size_t)cw * bpp + 2u;
        uint8_t *buf = malloc(pitch * ch);
        im_option_region_t rg = im_option_region(x, y, w, h);
        im_option_dest_t   d  = im_option_dest(buf, pitch, pitch * ch);

        ASSERT(buf);
        o[1] = &rg.base;
        o[2] = k == 6u ? &d.base : NULL; /* the caller's memory, the region's size */
        ASSERT(load_with(data, len, o, &r) == IM_OK && r && r->width == cw && r->height == ch);
        for (yy = 0; yy < ch; yy++) {
          const uint8_t *got = k == 6u ? buf + yy * pitch : (const uint8_t *)r->data.data + (size_t)yy * cw * bpp;

          ASSERT(!memcmp(got, (const uint8_t *)full->data.data + ((size_t)(y + yy) * W + x) * bpp, (size_t)cw * bpp));
        }
        im_free(r);
        free(buf);
      }
      {
        im_option_region_t out = im_option_region(W, 0, 4, 4);

        o[1] = &out.base;
        o[2] = NULL;
        ASSERT(load_with(data, len, o, &r) != IM_OK && !r); /* none of it inside */
      }
      im_free(full);
    }
    free(data);
  }

  TEST_SUCCESS
}

TEST_IMPL(memory_limit) {
  static const char *names[] = { "420.jpg", "prog.jpg", "threads.jpg", "ll1.jpg", "hier.jpg",
                                 "rct.jp2", "ict-tiles.j2k", "palette.jp2", "ht-rev.jph", "imageio.jp2" };
  size_t i;

  for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    size_t           len = 0, nl = strlen(names[i]);
    uint8_t         *data = read_all(nl > 4u && !strcmp(names[i] + nl - 4u, ".jpg") ? "jpg" : "jp2", names[i], &len);
    im_option_size_t tiny = im_option_memory(1u), lots = im_option_memory((size_t)1 << 30);
    im_option_base_t *o[2] = { NULL, NULL };
    ImImage          *a, *b;

    ASSERT(data);
    ASSERT(load_with(data, len, o, &a) == IM_OK && a);
    o[0] = &tiny.base;
    ASSERT(load_with(data, len, o, &b) == IM_ENOMEM && !b); /* refused, nothing made */
    o[0] = &lots.base;
    ASSERT(load_with(data, len, o, &b) == IM_OK && same(a, b));
    im_free(a);
    im_free(b);
    free(data);
  }

  TEST_SUCCESS
}

/* raw CCITT fax (data/ccitt, make.sh): each stream's bits as decoded,
   with its rows given and not; a byte a pixel; a region; refusals */
static ImResult
fax_load(const uint8_t *data, size_t len, int32_t k, uint32_t w, uint32_t h, const bool f[4], im_option_base_t *more,
         ImImage **im) {
  im_option_int_t   fm = im_option_int(IM_OPTION_FORMAT, IM_FILEFORMATTYPE_CCITT);
  im_option_ccitt_t cc = im_option_ccitt(k, w, h);
  im_option_base_t *o[4] = { &fm.base, &cc.base, more, NULL };

  cc.params.end_of_line  = f[0];
  cc.params.byte_align   = f[1];
  cc.params.end_of_block = f[2];
  cc.params.black_is_1   = f[3];
  return load_with(data, len, o, im);
}

static int
fax_bit(const ImImage *im, uint32_t x, uint32_t y) {
  const uint8_t *d = im->data.data;

  if (im->bitsPerComponent == 8u) return d[(size_t)y * im->width + x] != 0u;
  return d[(size_t)y * ((im->width + 7u) / 8u) + x / 8u] >> (7u - x % 8u) & 1;
}

TEST_IMPL(ccitt) {
  static const struct {
    const char *name;
    int32_t     k;
    uint32_t    w, h;
    bool        f[4]; /* EOLs, byte-aligned, an end of block, black is 1 */
    uint32_t    hash;
  } cases[] = {
    { "g4.fax",          -1,  257,  11, { 0, 0, 1, 0 }, 0x699138E1u },
    { "g4-align.fax",    -1,  100,  17, { 0, 1, 1, 1 }, 0x818840BEu },
    { "g3-1d-eol.fax",    0,   31,  17, { 1, 0, 1, 0 }, 0x01F50945u },
    { "g3-1d.fax",        0,   64,  17, { 0, 0, 0, 0 }, 0xB2CD96B3u },
    { "g3-2d.fax",        4,  100,  17, { 1, 1, 1, 0 }, 0xDA5962C1u },
    { "g3-2d-align.fax", 1000,  1, 116, { 1, 1, 0, 0 }, 0x5B0A8101u },
    { "black1.fax",       4, 1378,  17, { 0, 0, 0, 1 }, 0x636CBF8Fu },
    { "fax.fax",         -1, 1728,  17, { 0, 0, 1, 0 }, 0xB6CF6D97u },
    { "wide.fax",         2, 3000,   9, { 1, 0, 1, 0 }, 0x331F025Au },
  };
  size_t i;

  for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    size_t   len = 0;
    uint8_t *data = read_all("ccitt", cases[i].name, &len);
    ImImage *a, *b;
    uint32_t x, y, h;

    ASSERT(data);
    ASSERT(fax_load(data, len, cases[i].k, cases[i].w, cases[i].h, cases[i].f, NULL, &a) == IM_OK && a);
    ASSERT(a->width == cases[i].w && a->height == cases[i].h && a->bitsPerComponent == 1u);
    ASSERT(a->len == (size_t)(a->width + 7u) / 8u * a->height && a->fileFormatType == IM_FILEFORMATTYPE_CCITT);
    h = fnv1a(a->data.data, a->len);
    if (h != cases[i].hash) fprintf(stderr, "  %s: 0x%08X\n", cases[i].name, h);
    ASSERT(h == cases[i].hash);
    /* its rows not given: the same, as many as the data has */
    ASSERT(fax_load(data, len, cases[i].k, cases[i].w, 0u, cases[i].f, NULL, &b) == IM_OK && b);
    ASSERT(b->height == a->height && b->len == a->len && !memcmp(a->data.data, b->data.data, a->len));
    im_free(b);
    /* the bytes read with its rows given: the stream's own (its RTC or
       EOFB too), an inline image's end after it or not */
    {
      size_t            got = 0;
      im_option_bytes_t br = im_option_bytes_read(&got);
      uint8_t          *more = malloc(len + 6u);

      ASSERT(more);
      memcpy(more, data, len);
      memcpy(more + len, "\nEI Q\n", 6u);
      ASSERT(fax_load(data, len, cases[i].k, cases[i].w, cases[i].h, cases[i].f, &br.base, &b) == IM_OK && b && got == len);
      im_free(b);
      ASSERT(fax_load(more, len + 6u, cases[i].k, cases[i].w, cases[i].h, cases[i].f, &br.base, &b) == IM_OK && b && got == len);
      im_free(b);
      free(more);
    }
    /* a byte a pixel */
    {
      im_option_bool_t e8 = im_option_bool(IM_OPTION_EIGHT_BITS, true);

      ASSERT(fax_load(data, len, cases[i].k, cases[i].w, cases[i].h, cases[i].f, &e8.base, &b) == IM_OK && b);
      ASSERT(b->bitsPerComponent == 8u && b->len == (size_t)a->width * a->height);
      for (y = 0; y < a->height; y++) {
        for (x = 0; x < a->width; x++) ASSERT(fax_bit(b, x, y) == fax_bit(a, x, y));
      }
      im_free(b);
    }
    /* a region: the whole one's bits there */
    {
      uint32_t           rx = a->width / 3u, ry = a->height / 4u, rw = a->width / 2u + 1u, rh = a->height / 2u + 1u;
      im_option_region_t rg = im_option_region(rx, ry, rw, rh);

      if (rx + rw > a->width) rw = a->width - rx;
      ASSERT(fax_load(data, len, cases[i].k, cases[i].w, cases[i].h, cases[i].f, &rg.base, &b) == IM_OK && b);
      ASSERT(b->width == rw && b->height == rh);
      for (y = 0; y < rh; y++) {
        for (x = 0; x < rw; x++) ASSERT(fax_bit(b, x, y) == fax_bit(a, rx + x, ry + y));
      }
      im_free(b);
    }
    /* refused: no columns; a limit it doesn't fit */
    {
      im_option_size_t tiny = im_option_memory(1u);

      ASSERT(fax_load(data, len, cases[i].k, 0u, cases[i].h, cases[i].f, NULL, &b) != IM_OK && !b);
      ASSERT(fax_load(data, len, cases[i].k, cases[i].w, cases[i].h, cases[i].f, &tiny.base, &b) == IM_ENOMEM && !b);
    }
    im_free(a);
    free(data);
  }

  TEST_SUCCESS
}

/* JBIG2 (data/jbig2): streams of every segment kind (generic regions of
   each template, MMR, typical prediction, unknown length; text regions
   and symbol dictionaries, arithmetic and Huffman, refinement and
   aggregation, contexts kept from one dictionary for the next, a custom
   table; patterns and halftones; refinement of the page and of an
   intermediate region; striped pages; globals; a whole file) and a book
   page in symbol mode. Each the page its encoder's own model gives, and
   the same in two other decoders where they read what it has */
static ImResult
jb2_load(const uint8_t *data, size_t len, const uint8_t *glob, size_t glen, bool embedded, im_option_base_t *more,
         ImImage **im) {
  im_option_int_t   fm = im_option_int(IM_OPTION_FORMAT, IM_FILEFORMATTYPE_JBIG2);
  im_option_data_t  gl = im_option_jbig2_globals(glob, glen);
  im_option_base_t *o[4] = { &gl.base, more, NULL, NULL };

  if (embedded) {
    o[0] = &fm.base;
    o[1] = &gl.base;
    o[2] = more;
  }
  return load_with(data, len, o, im);
}

TEST_IMPL(jbig2) {
  static const struct {
    const char *name;
    bool        globals;
    uint32_t    w, h;
    uint32_t    hash;
  } cases[] = {
    { "mix",          1,  190,   23, 0x51075FC5u },
    { "huffman",      0,    7,   77, 0xA9598342u },
    { "contexts",     0,   31,  103, 0x914F62D1u },
    { "file",         0,   64,   16, 0x67F63671u },
    { "intermediate", 0,   64,   16, 0x8ED8B345u },
    { "stripes",      0,   31,   29, 0xFECE813Cu },
    { "refine",       0,  304,   61, 0x23E63090u },
    { "page",         0, 2480, 3508, 0x3BEEE73Bu },
    { "ext",          0,   96,   48, 0x05DD094Fu }, /* the extended template (Amd. 2), typical prediction */
  };
  size_t i;

  for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    char     name[64], gname[64];
    size_t   len = 0, glen = 0;
    uint8_t *data, *glob = NULL;
    bool     file = !strcmp(cases[i].name, "file");
    ImImage *a, *b;
    uint32_t x, y, h;

    snprintf(name, sizeof(name), "%s.jb2", cases[i].name);
    data = read_all("jbig2", name, &len);
    ASSERT(data);
    if (cases[i].globals) {
      snprintf(gname, sizeof(gname), "%s.glob", cases[i].name);
      ASSERT((glob = read_all("jbig2", gname, &glen)));
    }
    ASSERT(jb2_load(data, len, glob, glen, !file, NULL, &a) == IM_OK && a);
    ASSERT(a->width == cases[i].w && a->height == cases[i].h && a->bitsPerComponent == 1u);
    ASSERT(a->len == (size_t)(a->width + 7u) / 8u * a->height && a->fileFormatType == IM_FILEFORMATTYPE_JBIG2);
    h = fnv1a(a->data.data, a->len);
    if (h != cases[i].hash) fprintf(stderr, "  %s: 0x%08X\n", cases[i].name, h);
    ASSERT(h == cases[i].hash);
    if (file) {
      /* a file: told by its header, or said; its size without decoding */
      ImInfo info;

      ASSERT(jb2_load(data, len, NULL, 0u, true, NULL, &b) == IM_OK && b);
      ASSERT(b->len == a->len && !memcmp(a->data.data, b->data.data, a->len));
      im_free(b);
      ASSERT(im_info_mem(&info, data, len) == IM_OK);
      ASSERT(info.width == a->width && info.height == a->height && info.fileFormatType == IM_FILEFORMATTYPE_JBIG2);
    }
    /* a byte a pixel */
    {
      im_option_bool_t e8 = im_option_bool(IM_OPTION_EIGHT_BITS, true);

      ASSERT(jb2_load(data, len, glob, glen, !file, &e8.base, &b) == IM_OK && b);
      ASSERT(b->bitsPerComponent == 8u && b->len == (size_t)a->width * a->height);
      for (y = 0; y < a->height; y++) {
        for (x = 0; x < a->width; x++) ASSERT(fax_bit(b, x, y) == fax_bit(a, x, y));
      }
      im_free(b);
    }
    /* a region: the whole page's pixels there */
    {
      uint32_t           rx = a->width / 3u, ry = a->height / 4u, rw = a->width / 2u + 1u, rh = a->height / 2u + 1u;
      im_option_region_t rg = im_option_region(rx, ry, rw, rh);

      if (rx + rw > a->width) rw = a->width - rx;
      if (ry + rh > a->height) rh = a->height - ry;
      ASSERT(jb2_load(data, len, glob, glen, !file, &rg.base, &b) == IM_OK && b);
      ASSERT(b->width == rw && b->height == rh);
      for (y = 0; y < rh; y++) {
        for (x = 0; x < rw; x++) ASSERT(fax_bit(b, x, y) == fax_bit(a, rx + x, ry + y));
      }
      im_free(b);
    }
    /* refused: a limit it doesn't fit; a dictionary it doesn't have */
    {
      im_option_size_t tiny = im_option_memory(64u);

      ASSERT(jb2_load(data, len, glob, glen, !file, &tiny.base, &b) == IM_ENOMEM && !b);
      if (cases[i].globals && jb2_load(data, len, NULL, 0u, true, NULL, &b) == IM_OK) {
        ASSERT(b->len != a->len || memcmp(a->data.data, b->data.data, a->len));
        im_free(b);
      }
    }
    im_free(a);
    free(data);
    free(glob);
  }
  /* a file of three pages (random access): counted, each by
     IM_OPTION_PAGE (none: the first), one past them refused */
  {
    static const uint32_t pw[3] = { 40u, 23u, 64u }, ph[3] = { 20u, 37u, 9u };
    static const uint32_t phash[3] = { 0x2EBA5AD1u, 0x248BBB50u, 0x23419779u };
    size_t                len = 0;
    uint8_t              *data = read_all("jbig2", "pages.jb2", &len);
    ImInfo                info;
    int32_t               pg;

    ASSERT(data && im_info_mem(&info, data, len) == IM_OK);
    ASSERT(info.pages == 3u && info.width == pw[0] && info.height == ph[0]);
    for (pg = 0; pg <= 4; pg++) {
      im_option_int_t po = im_option_int(IM_OPTION_PAGE, pg);
      uint32_t        k = pg ? (uint32_t)pg - 1u : 0u, h;
      ImImage        *b;
      ImResult        r = jb2_load(data, len, NULL, 0u, false, &po.base, &b);

      if (pg == 4) {
        ASSERT(r != IM_OK && !b);
        continue;
      }
      ASSERT(r == IM_OK && b && b->width == pw[k] && b->height == ph[k]);
      h = fnv1a(b->data.data, b->len);
      if (h != phash[k]) fprintf(stderr, "  pages.jb2 page %u: 0x%08X\n", k + 1u, h);
      ASSERT(h == phash[k]);
      im_free(b);
    }
    free(data);
  }
  /* not JBIG2 */
  {
    static const uint8_t junk[] = { 0, 0, 0, 1, 0x30, 0, 1, 0, 0, 0, 3, 1, 2, 3 };
    ImImage             *b;

    ASSERT(jb2_load(junk, sizeof(junk), NULL, 0u, true, NULL, &b) != IM_OK && !b);
  }

  TEST_SUCCESS
}


/* JPEG-LS: T.87's example; one 37×23 crop coded by CharLS (by components,
   lines, samples; HP's colour transforms) and by the JPEG XT reference
   software (restart intervals, a DNL), each to its same pixels; the rest
   hashed (NEAR 3; 4, 12 and 16 bits; 2 and 5 components as they are;
   chroma subsampled 2×2, by components and by lines; a preset MAXVAL of
   200, a RESET of 1000; checked against the crop where lossless); a
   mapping table; the options */
TEST_IMPL(jls) {
  /* H.3's image, H.4.1's stream */
  static const uint8_t h4[] = {
    0xFF, 0xD8, 0xFF, 0xF7, 0x00, 0x0B, 0x08, 0x00, 0x04, 0x00, 0x04, 0x01, 0x01, 0x11, 0x00, 0xFF,
    0xDA, 0x00, 0x08, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x6C, 0x80, 0x20, 0x8E,
    0x01, 0xC0, 0x00, 0x00, 0x57, 0x40, 0x00, 0x00, 0x6E, 0xE6, 0x00, 0x00, 0x01, 0xBC, 0x18, 0x00,
    0x00, 0x05, 0xD8, 0x00, 0x00, 0x91, 0x60, 0xFF, 0xD9
  };
  static const uint8_t h4px[16] = { 0, 0, 90, 74, 68, 50, 43, 205, 64, 145, 145, 145, 100, 145, 145, 145 };
  static const char   *rgb[] = { "rgb0", "rgb1", "rgb2", "rst0", "rst1", "rst2", "dnl", "hp1", "hp2", "hp3" };
  static const struct {
    const char *name;
    uint32_t    w, h, comps, bits;
    ImFormat    format;
    uint32_t    hash;
  } cases[] = {
    { "gray8",  37, 23, 1,  8, IM_FORMAT_GRAY, 0x52365CE9u },
    { "near",   37, 23, 3,  8, IM_FORMAT_RGB,  0x0AA86516u },
    { "p4",     37, 23, 1,  8, IM_FORMAT_GRAY, 0x557A23ECu },
    { "p12",    37, 23, 1, 16, IM_FORMAT_GRAY, 0x1C768B5Du },
    { "p16",    37, 23, 3, 16, IM_FORMAT_RGB,  0x6ED128FCu },
    { "c2",     37, 23, 2,  8, IM_FORMAT_NONE, 0x275C2D6Bu },
    { "c5",     37, 23, 5,  8, IM_FORMAT_NONE, 0xA9CDA332u },
    { "sub0",   37, 24, 3,  8, IM_FORMAT_RGB,  0x37F24AF2u }, /* by lines the same */
    { "sub1",   37, 24, 3,  8, IM_FORMAT_RGB,  0x37F24AF2u },
    { "maxval", 37, 23, 1,  8, IM_FORMAT_GRAY, 0x56684E23u },
    { "reset",  37, 23, 1, 16, IM_FORMAT_GRAY, 0x5980D223u },
  };
  im_option_int_t one = im_option_int(IM_OPTION_THREADS, 1);
  ImImage        *a, *b;
  ImInfo          info;
  size_t          i, k;

  ASSERT(im_load_mem(&a, h4, sizeof(h4), NULL, IM_OPEN_INTENT_READONLY) == IM_OK && a);
  ASSERT(a->width == 4u && a->height == 4u && a->format == IM_FORMAT_GRAY && a->bitsPerComponent == 8u);
  ASSERT(a->fileFormatType == IM_FILEFORMATTYPE_JLS && a->len == 16u && !memcmp(a->data.data, h4px, 16));
  im_free(a);
  ASSERT(im_info_mem(&info, h4, sizeof(h4)) == IM_OK && info.fileFormatType == IM_FILEFORMATTYPE_JLS);
  ASSERT(info.coding == IM_CODING_JPEGLS && info.width == 4u && info.height == 4u && info.components == 1u);
  ASSERT(info.bitsPerComponent == 8u && info.pages == 1u);

  /* one image, however coded */
  a = mem("jls", "rgb0.jls", NULL, NULL);
  ASSERT(a && a->width == 37u && a->height == 23u && a->format == IM_FORMAT_RGB && a->bitsPerComponent == 8u);
  for (i = 0; i < sizeof(rgb) / sizeof(rgb[0]); i++) {
    char name[32];

    snprintf(name, sizeof(name), "%s.jls", rgb[i]);
    ASSERT(same(a, b = mem("jls", name, NULL, NULL)));
    im_free(b);
    ASSERT(same(a, b = mem("jls", name, &one.base, NULL)));
    im_free(b);
  }

  for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    char     name[32];
    uint32_t h;

    snprintf(name, sizeof(name), "%s.jls", cases[i].name);
    b = mem("jls", name, NULL, NULL);
    ASSERT(b && b->width == cases[i].w && b->height == cases[i].h && b->componentsPerPixel == cases[i].comps);
    ASSERT(b->bitsPerComponent == cases[i].bits && b->format == cases[i].format);
    ASSERT(b->len == (size_t)b->width * b->height * b->componentsPerPixel * (b->bitsPerComponent / 8u));
    h = fnv1a(b->data.data, b->len);
    if (h != cases[i].hash) fprintf(stderr, "  %s: 0x%08X\n", name, h);
    ASSERT(h == cases[i].hash);
    im_free(b);
  }

  /* a mapping table (255 - v, a byte an entry): each sample through it */
  {
    ImImage *g = mem("jls", "gray8.jls", NULL, NULL), *m = mem("jls", "map.jls", NULL, NULL);

    ASSERT(g && m && m->len == g->len && m->format == IM_FORMAT_GRAY);
    for (k = 0; k < g->len; k++) ASSERT(((uint8_t *)m->data.data)[k] == 255u - ((uint8_t *)g->data.data)[k]);
    im_free(g);
    im_free(m);
  }

  /* a region: the whole image's pixels there */
  {
    im_option_region_t rg = im_option_region(5u, 7u, 20u, 9u);
    uint32_t           y;

    ASSERT((b = mem("jls", "rst2.jls", &rg.base, NULL)) && b->width == 20u && b->height == 9u);
    for (y = 0; y < 9u; y++) {
      ASSERT(!memcmp((uint8_t *)b->data.data + (size_t)y * 60u, (uint8_t *)a->data.data + ((size_t)(y + 7u) * 37u + 5u) * 3u, 60u));
    }
    im_free(b);
  }
  /* RGB padded; the caller's memory */
  {
    im_option_bool_t pad = im_option_bool(IM_OPTION_RGB_PADDED, true);
    size_t           stride = 37u * 3u + 5u, size = stride * 22u + 37u * 3u, y;
    uint8_t         *buf = malloc(size);

    ASSERT((b = mem("jls", "rgb1.jls", &pad.base, NULL)) && b->format == IM_FORMAT_RGB0 && b->componentsPerPixel == 4u);
    for (k = 0; k < 37u * 23u; k++) {
      ASSERT(!memcmp((uint8_t *)b->data.data + k * 4u, (uint8_t *)a->data.data + k * 3u, 3u));
      ASSERT(((uint8_t *)b->data.data)[k * 4u + 3u] == 0xFFu);
    }
    im_free(b);
    ASSERT(buf);
    {
      im_option_dest_t d = im_option_dest(buf, stride, size);

      ASSERT((b = mem("jls", "rgb0.jls", &d.base, NULL)) && !b->data.data && b->row_pad_last == 5u);
      for (y = 0; y < 23u; y++) ASSERT(!memcmp(buf + y * stride, (uint8_t *)a->data.data + y * 111u, 111u));
      im_free(b);
    }
    {
      im_option_dest_t d = im_option_dest(buf, stride, size - 1u);

      ASSERT(!mem("jls", "rgb0.jls", &d.base, NULL));
    }
    free(buf);
  }
  /* 16 bits in the order asked */
  {
    im_option_byteorder_t be = im_option_row_byteorder(IM_BYTEORDER_BIG), le = im_option_row_byteorder(IM_BYTEORDER_LITTLE);
    ImImage              *x = mem("jls", "p16.jls", &be.base, NULL), *y = mem("jls", "p16.jls", &le.base, NULL);

    ASSERT(x && y && x->len == y->len && x->byteOrder == IM_BYTEORDER_BIG && y->byteOrder == IM_BYTEORDER_LITTLE);
    for (k = 0; k < x->len; k += 2u) {
      ASSERT(((uint8_t *)x->data.data)[k] == ((uint8_t *)y->data.data)[k + 1u]);
      ASSERT(((uint8_t *)x->data.data)[k + 1u] == ((uint8_t *)y->data.data)[k]);
    }
    im_free(x);
    im_free(y);
  }
  /* a limit it doesn't fit; the bytes read (past EOI, not what follows);
     cut short anywhere: an image or none, nothing worse */
  {
    im_option_size_t  tiny = im_option_memory(256u);
    size_t            len = 0, used = 0;
    uint8_t          *d = read_all("jls", "rst1.jls", &len), *more;
    im_option_bytes_t br = im_option_bytes_read(&used);

    ASSERT(d && im_load_mem(&b, d, len, (im_option_base_t *[]){ &tiny.base, NULL }, IM_OPEN_INTENT_READONLY) == IM_ENOMEM);
    ASSERT((more = malloc(len + 7u)));
    memcpy(more, d, len);
    memset(more + len, 0x5A, 7u);
    ASSERT(im_load_mem(&b, more, len + 7u, (im_option_base_t *[]){ &br.base, NULL }, IM_OPEN_INTENT_READONLY) == IM_OK);
    ASSERT(used == len && same(a, b));
    im_free(b);
    for (k = 2; k < len; k++) {
      if (im_load_mem(&b, d, k, NULL, IM_OPEN_INTENT_READONLY) == IM_OK) im_free(b);
    }
    ASSERT(im_info_mem(&info, d, len) == IM_OK && info.width == 37u && info.height == 23u && info.components == 3u);
    free(more);
    free(d);
  }
  /* the DNL's height */
  {
    size_t   len = 0;
    uint8_t *d = read_all("jls", "dnl.jls", &len);

    ASSERT(d && im_info_mem(&info, d, len) == IM_OK && info.height == 23u && info.coding == IM_CODING_JPEGLS);
    free(d);
  }
  im_free(a);
  TEST_SUCCESS
}

/* a JPEG's index (IM_OPTION_INDEX, im_index_mem): with it every scale's
   regions and the whole are the pixels without it, on one thread and on
   many; one a decode makes as it reads is the skim's; one a region decode
   made (its rows to the region's) serves below them too; another file's,
   or one whose headers changed, is passed over; streams it isn't for have
   none */
static ImImage*
ix_dec(const uint8_t *d, size_t len, uint32_t th, uint32_t sc, uint32_t x, uint32_t y, uint32_t w, uint32_t h, ImIndex **ix) {
  im_option_int_t    t = im_option_int(IM_OPTION_THREADS, (int32_t)th), s = im_option_int(IM_OPTION_SCALE, (int32_t)sc);
  im_option_region_t g = im_option_region(x, y, w, h);
  im_option_index_t  o = im_option_index(ix);
  im_option_base_t  *opts[5] = { &t.base, &s.base, NULL, NULL, NULL };
  ImImage           *im = NULL;
  size_t             k = 2;

  if (w) opts[k++] = &g.base;
  if (ix) opts[k++] = &o.base;
  return im_load_mem(&im, d, len, opts, IM_OPEN_INTENT_READONLY) == IM_OK ? im : NULL;
}

TEST_IMPL(jpg_index) {
  static const char *with[] = { "420.jpg", "444.jpg", "411.jpg", "gray.jpg", "p12.jpg", "cmyk.jpg", "dnl.jpg",
                                "420-cut.jpg", "tiny.jpg", "mixed.jpg", "rst.jpg", "threads.jpg" };
  static const char *none[] = { "prog.jpg", "arith.jpg", "ll1.jpg", "hier.jpg" };
  ImIndex           *other = NULL;
  size_t             i;

  for (i = 0; i < sizeof(with) / sizeof(with[0]); i++) {
    size_t   len = 0;
    uint8_t *d = read_all("jpg", with[i], &len);
    ImIndex *ix = NULL, *made = NULL, *part = NULL;
    ImImage *a, *b;
    uint32_t sc, th, k, W, H;

    ASSERT(d && im_index_mem(&ix, d, len, NULL) == IM_OK && ix && im_index_size(ix) > 0u);
    a = mem("jpg", with[i], NULL, NULL);
    ASSERT(a);
    for (sc = 1; sc <= 8u; sc *= 2u) {
      W = (a->width + sc - 1u) / sc;
      H = (a->height + sc - 1u) / sc;
      {
        /* the last two narrow (threads.jpg's: each row from its index's
           places, the second from the one at MCU 32) */
        const uint32_t reg[8][4] = { { 0, 0, 0, 0 }, { 0, H - 1u, W, 1 }, { W / 2u, H / 2u, W, H }, { W - 1u, 0, 1, H },
                                     { 1u < W ? 1u : 0u, H / 3u, W / 3u + 1u, 2 }, { W / 4u, H > 2u ? H - 2u : 0u, 3, 7 },
                                     { 2u < W ? 2u : 0u, 1u < H ? 1u : 0u, W / 8u + 1u, H }, { W * 2u / 3u, H / 5u, 3, H / 2u + 1u } };

        for (k = 0; k < 8u; k++) {
          for (th = 0; th < 2u; th++) {
            ImImage *x = ix_dec(d, len, th ? 0u : 1u, sc, reg[k][0], reg[k][1], reg[k][2], reg[k][3], NULL);

            ASSERT(x && same(x, b = ix_dec(d, len, th ? 0u : 1u, sc, reg[k][0], reg[k][1], reg[k][2], reg[k][3], &ix)));
            im_free(b);
            if (other) {
              ASSERT(same(x, b = ix_dec(d, len, th ? 0u : 1u, sc, reg[k][0], reg[k][1], reg[k][2], reg[k][3], &other)));
              im_free(b);
            }
            im_free(x);
          }
        }
      }
    }
    /* made as a decode reads: the skim's */
    ASSERT((b = ix_dec(d, len, 0u, 8u, 0, 0, 0, 0, &made)) && made && im_index_size(made) == im_index_size(ix));
    im_free(b);
    ASSERT(same(a, b = ix_dec(d, len, 0u, 1u, 0, 0, 0, 0, &made)));
    im_free(b);
    /* a region's (rows to its bottom), used for the last row */
    ASSERT((b = ix_dec(d, len, 1u, 1u, 0, a->height / 3u, 5, 1, &part)) && part);
    im_free(b);
    ASSERT(im_index_size(part) <= im_index_size(ix));
    {
      ImImage *x = ix_dec(d, len, 1u, 1u, 0, a->height - 1u, a->width, 1, NULL);

      ASSERT(x && same(x, b = ix_dec(d, len, 0u, 1u, 0, a->height - 1u, a->width, 1, &part)));
      im_free(x);
      im_free(b);
      /* a narrow region within its rows */
      x = ix_dec(d, len, 1u, 1u, a->width * 2u / 3u, 0, 3, a->height / 4u + 1u, NULL);
      ASSERT(x && same(x, b = ix_dec(d, len, 0u, 1u, a->width * 2u / 3u, 0, 3, a->height / 4u + 1u, &part)));
      im_free(x);
      im_free(b);
    }
    /* headers changed (a COM's byte, past its marker): the index passed over */
    {
      uint8_t *e = malloc(len + 6u);

      ASSERT(e);
      memcpy(e, d, 2u);
      memcpy(e + 2u, "\xFF\xFE\x00\x04hi", 6u);
      memcpy(e + 8u, d + 2u, len - 2u);
      ASSERT(same(a, b = ix_dec(e, len + 6u, 1u, 1u, 0, 0, 0, 0, &ix)));
      im_free(b);
      free(e);
    }
    im_free(a);
    im_index_free(made);
    im_index_free(part);
    im_index_free(other);
    other = ix;
    free(d);
  }
  im_index_free(other);
  for (i = 0; i < sizeof(none) / sizeof(none[0]); i++) {
    size_t   len = 0;
    uint8_t *d = read_all("jpg", none[i], &len);
    ImIndex *ix = NULL;
    ImImage *b;

    ASSERT(d && im_index_mem(&ix, d, len, NULL) == IM_EBADF && !ix);
    ASSERT((b = ix_dec(d, len, 1u, 1u, 0, 0, 0, 0, &ix)) && !ix);
    im_free(b);
    free(d);
  }
  im_index_free(NULL);
  TEST_SUCCESS
}

/* a JPEG 2000's index (its tiles' packet headers as read, IM_OPTION_INDEX,
   im_index_mem): with it every scale's regions and the whole are the
   pixels without it, on one thread and on many, by windows too (win.j2k,
   win97.j2k); one a region decode makes holds the tiles it read and the
   whole is right with it; another file's, a JPEG's, or one whose bytes
   changed is passed over */
TEST_IMPL(jp2_index) {
  static const char *names[] = { "win.j2k",     "win97.j2k",   "ict-tiles.j2k",   "ht-irv-tiles.j2c", "ht-rev.jph",
                                 "ht-gray16.j2c", "poc.j2k",   "ppm.j2k",         "ppt.j2k",          "parts.j2k",
                                 "rct.jp2",     "alpha.jp2",   "sycc420.jp2",     "gray16.jp2",       "styles.jp2",
                                 "gray-layers.jp2", "palette.jp2", "cmyk.j2k",     "rgb10.jp2",        "premult.jp2" };
  ImIndex           *other = NULL, *jpeg = NULL;
  size_t             i, len = 0;
  uint8_t           *d = read_all("jpg", "420.jpg", &len);

  ASSERT(d && im_index_mem(&jpeg, d, len, NULL) == IM_OK && jpeg);
  free(d);
  for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    ImIndex *ix = NULL, *made = NULL;
    ImImage *a, *b;
    uint32_t sc, th, k;

    d = read_all("jp2", names[i], &len);
    ASSERT(d && im_index_mem(&ix, d, len, NULL) == IM_OK && ix && im_index_size(ix) > 0u);
    ASSERT((a = ix_dec(d, len, 1u, 1u, 0, 0, 0, 0, NULL)));
    for (sc = 1; sc <= 8u; sc *= 2u) {
      ImImage *s = ix_dec(d, len, 1u, sc, 0, 0, 0, 0, NULL);
      uint32_t W, H;

      ASSERT(s);
      W = s->width;
      H = s->height;
      im_free(s);
      {
        const uint32_t reg[7][4] = { { 0, 0, 0, 0 },           { 0, H - 1u, W, 1 },  { W / 2u, H / 2u, W, H }, { W - 1u, 0, 1, H },
                                     { 1u < W ? 1u : 0u, H / 3u, W / 3u + 1u, 2 }, { W / 4u, H > 2u ? H - 2u : 0u, 3, 7 },
                                     { W / 3u, H / 3u, 5, 4 } };

        for (k = 0; k < 7u; k++) {
          for (th = 0; th < 2u; th++) {
            ImImage *x = ix_dec(d, len, th ? 0u : 1u, sc, reg[k][0], reg[k][1], reg[k][2], reg[k][3], NULL);

            ASSERT(x && same(x, b = ix_dec(d, len, th ? 0u : 1u, sc, reg[k][0], reg[k][1], reg[k][2], reg[k][3], &ix)));
            im_free(b);
            ASSERT(same(x, b = ix_dec(d, len, th ? 0u : 1u, sc, reg[k][0], reg[k][1], reg[k][2], reg[k][3], &jpeg)));
            im_free(b);
            if (other) {
              ASSERT(same(x, b = ix_dec(d, len, th ? 0u : 1u, sc, reg[k][0], reg[k][1], reg[k][2], reg[k][3], &other)));
              im_free(b);
            }
            im_free(x);
          }
        }
      }
    }
    /* made as a region decode reads: its tiles' */
    ASSERT((b = ix_dec(d, len, 1u, 1u, 0, 0, 3, 3, &made)) && made && im_index_size(made) <= im_index_size(ix));
    im_free(b);
    ASSERT(same(a, b = ix_dec(d, len, 0u, 1u, 0, 0, 0, 0, &made)));
    im_free(b);
    /* a byte near the end changed (a packet's, perhaps its header's) */
    {
      uint8_t *e = malloc(len);
      ImImage *x;

      ASSERT(e);
      memcpy(e, d, len);
      e[len - 3u] ^= 0x41u;
      x = ix_dec(e, len, 1u, 1u, 0, 0, 0, 0, NULL);
      b = ix_dec(e, len, 1u, 1u, 0, 0, 0, 0, &ix);
      ASSERT(same(x, b));
      im_free(x);
      im_free(b);
      free(e);
    }
    im_free(a);
    im_index_free(made);
    im_index_free(other);
    other = ix;
    free(d);
  }
  im_index_free(other);
  /* a JPEG's decode with a JPEG 2000's index: passed over */
  {
    ImIndex *ix = NULL;
    ImImage *x, *b;

    d = read_all("jp2", "win.j2k", &len);
    ASSERT(d && im_index_mem(&ix, d, len, NULL) == IM_OK);
    free(d);
    d = read_all("jpg", "420.jpg", &len);
    ASSERT(d && (x = ix_dec(d, len, 1u, 1u, 0, 0, 0, 0, NULL)));
    ASSERT(same(x, b = ix_dec(d, len, 1u, 1u, 0, 0, 0, 0, &ix)));
    im_free(x);
    im_free(b);
    im_index_free(ix);
    free(d);
  }
  im_index_free(jpeg);
  TEST_SUCCESS
}

#if IM_WITH_JXL
/* JPEG XL from memory (the platform's decoder: ImageIO on Apple's): a bare
   codestream and one in the container, the size and the pixels make.sh
   wrote (lossless); the colours where opaque, the opacity everywhere */
TEST_IMPL(jxl_mem) {
  static const char *names[] = { "rgb.jxl", "rgba.jxl" };
  size_t             i;

  for (i = 0; i < 2u; i++) {
    size_t   len = 0;
    uint8_t *d = read_all("jxl", names[i], &len);
    ImImage *im = NULL;
    ImInfo   info;
    uint32_t x, y, bpp;

    ASSERT(d && im_info_mem(&info, d, len) == IM_OK && info.width == 37u && info.height == 23u);
    ASSERT(info.fileFormatType == IM_FILEFORMATTYPE_JXL);
    ASSERT(im_load_mem(&im, d, len, NULL, IM_OPEN_INTENT_READONLY) == IM_OK && im);
    ASSERT(im->width == 37u && im->height == 23u && im->fileFormatType == IM_FILEFORMATTYPE_JXL);
    bpp = im->bytesPerPixel;
    ASSERT(bpp == 3u || bpp == 4u);
    ASSERT(!i || bpp == 4u);
    for (y = 0; y < 23u; y++) {
      const uint8_t *row = (const uint8_t *)im->data.data + (size_t)y * (37u * bpp + im->row_pad_last);

      for (x = 0; x < 37u; x++) {
        const uint8_t *p = row + (size_t)x * bpp;
        uint32_t       a = i && x >= 20u ? ((x * 9u + y) & 255u) : 255u;

        if (i) ASSERT(p[3] == a);
        if (a == 255u) ASSERT(p[0] == ((x * 7u) & 255u) && p[1] == ((y * 11u) & 255u) && p[2] == (((x + y) * 3u) & 255u));
      }
    }
    im_free(im);
    free(d);
  }
  TEST_SUCCESS
}
#endif

/* a JPEG 2000 region much smaller than its tile, by windows (its levels'
   reach of it alone, jp2.c's tile_window): 640×480, two levels, 5/3 and
   9/7; each scale's regions, at the corners, edges and inside, against the
   whole decode cut there, on one thread and many */
TEST_IMPL(jp2_window) {
  static const char *names[] = { "win.j2k", "win97.j2k" };
  size_t             i;

  for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    size_t   len = 0;
    uint8_t *d = read_all("jp2", names[i], &len);
    uint32_t sc, th, k;

    ASSERT(d);
    for (sc = 1; sc <= 8u; sc *= 2u) {
      ImImage *a = ix_dec(d, len, 1u, sc, 0, 0, 0, 0, NULL);
      uint32_t W, H, bpp;

      ASSERT(a);
      W   = a->width;
      H   = a->height;
      bpp = a->bytesPerPixel;
      {
        const uint32_t reg[8][4] = { { 0, 0, 9, 7 }, { W - 5u, H - 3u, 5, 3 }, { W / 2u, H / 2u, 17, 13 }, { 0, H / 3u, 3, 40 },
                                     { W - 1u, 0, 1, 1 }, { W / 3u, H - 1u, 30, 1 }, { 1, 1, 1, 1 }, { W / 4u + 1u, H / 5u, 11, 11 } };

        for (k = 0; k < 8u; k++) {
          for (th = 0; th < 2u; th++) {
            ImImage *b = ix_dec(d, len, th ? 0u : 1u, sc, reg[k][0], reg[k][1], reg[k][2], reg[k][3], NULL);
            uint32_t y, w, h;

            ASSERT(b);
            w = reg[k][2] < W - reg[k][0] ? reg[k][2] : W - reg[k][0];
            h = reg[k][3] < H - reg[k][1] ? reg[k][3] : H - reg[k][1];
            ASSERT(b->width == w && b->height == h);
            for (y = 0; y < h; y++) {
              ASSERT(!memcmp((uint8_t *)b->data.data + (size_t)y * w * bpp,
                             (uint8_t *)a->data.data + ((size_t)(reg[k][1] + y) * W + reg[k][0]) * bpp, (size_t)w * bpp));
            }
            im_free(b);
          }
        }
      }
      im_free(a);
    }
    free(d);
  }
  TEST_SUCCESS
}
