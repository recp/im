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
 * libjpeg's and OpenJPEG's (their pixels hashed).
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
    { "jp2", "palette.jp2",    4,  10,   6, 0xF4B0DCECu },
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

  TEST_SUCCESS
}
