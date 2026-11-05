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

#ifndef tests_common_h
#define tests_common_h

#ifndef _CRT_SECURE_NO_WARNINGS
#  define _CRT_SECURE_NO_WARNINGS /* for windows */
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <im/im.h>

/* a file loaded: by its path, or (a codec-only build, which has no
   im_load) read here and given to im_load_mem */
static inline ImResult
test_load(ImImage **dest, const char *path, im_option_base_t *options[]) {
#if IM_CODECS_ONLY
  FILE    *f = fopen(path, "rb");
  long     n;
  void    *d;
  ImResult r;

  *dest = NULL;
  if (!f) return IM_EBADF;
  fseek(f, 0, SEEK_END);
  n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n <= 0 || !(d = malloc((size_t)n)) || fread(d, 1, (size_t)n, f) != (size_t)n) {
    fclose(f);
    return IM_EBADF;
  }
  fclose(f);
  r = im_load_mem(dest, d, (size_t)n, options, IM_OPEN_INTENT_READONLY);
  free(d);
  return r;
#else
  return im_load(dest, path, options, IM_OPEN_INTENT_READONLY);
#endif
}

typedef struct test_status_t {
  const char *msg;
  int         status;
} test_status_t;

typedef test_status_t (*fntest)(void);

typedef struct test_entry_t {
  char  *name;
  fntest entry;
  int    ret;
  int    show_output;
} test_entry_t;

#ifndef IM_TESTS_NO_COLORFUL_OUTPUT

#define RESET       "\033[0m"
#define RED         "\033[31m"             /* Red */
#define GREEN       "\033[32m"             /* Green */
#define YELLOW      "\033[33m"             /* Yellow */
#define MAGENTA     "\033[35m"             /* Magenta */
#define CYAN        "\033[36m"             /* Cyan */
#define BOLDRED     "\033[1m\033[31m"      /* Bold Red */
#define BOLDGREEN   "\033[1m\033[32m"      /* Bold Green */
#define BOLDMAGENTA "\033[1m\033[35m"      /* Bold Magenta */
#define BOLDCYAN    "\033[1m\033[36m"      /* Bold Cyan */
#define BOLDWHITE   "\033[1m\033[37m"      /* Bold White */

#else

#define RESET
#define RED
#define GREEN
#define YELLOW
#define MAGENTA
#define CYAN
#define BOLDRED
#define BOLDGREEN
#define BOLDMAGENTA
#define BOLDCYAN
#define BOLDWHITE

#endif

#define TEST_DECLARE(FUN) test_status_t test_ ## FUN(void);
#define TEST_ENTRY(FUN)   { #FUN, test_ ## FUN, 0, 0 },
#define TEST_LIST         static test_entry_t tests[] =

#define TEST_OK 1
#define TEST_SUCCESS  return (test_status_t){NULL, TEST_OK};

#define TEST_IMPL(FUN)                                                        \
  test_status_t test_ ## FUN (void);                                          \
  test_status_t test_ ## FUN(void)

#define ASSERT_EXT(expr, msg)                                                 \
  if (!(expr)) {                                                              \
    fprintf(stderr,                                                           \
            RED "  assert fail" RESET                                         \
            " in " BOLDCYAN "%s " RESET                                       \
            "on " BOLDMAGENTA "line %d" RESET                                 \
            " : " BOLDWHITE " ASSERT(%s)\n" RESET,                            \
            __FILE__,                                                         \
            __LINE__,                                                         \
            #expr);                                                           \
    return (test_status_t){msg, 0};                                           \
  }

#define ASSERT(expr) do { ASSERT_EXT(expr, NULL) } while(0);

#if defined(_WIN32)
#  define OK_TEXT    "ok:"
#  define FAIL_TEXT  "fail:"
#  define FINAL_TEXT "^_^"
#else
#  define OK_TEXT    "✔︎"
#  define FAIL_TEXT  "𐄂"
#  define FINAL_TEXT "🎉"
#endif

#endif /* tests_common_h */
