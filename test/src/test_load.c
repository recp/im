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
 * gray channel each pixel mapping to one colour of the usual decode; one
 * thread what many give.
 */

#include "../include/common.h"

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
  if (im_load(&im, path, NULL, IM_OPEN_INTENT_READONLY) != IM_OK) return NULL;
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

  /* one thread: what many give */
  a = mem("jp2", "imageio.jp2", NULL, NULL);
  ASSERT(same(a, b = mem("jp2", "imageio.jp2", &one.base, NULL)));
  im_free(a);
  im_free(b);

  TEST_SUCCESS
}
