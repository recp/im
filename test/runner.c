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

#include "include/common.h"
#include "tests.h"

#include <stdlib.h>
#include <time.h>
#include <string.h>

int
main(int argc, const char * argv[]) {
  test_entry_t *entry;
  test_status_t st;
  int32_t       i, count, passed, failed, maxlen;
  double        elapsed, total;
  clock_t       start, end;
  (void)argc;
  (void)argv;

  passed = failed = maxlen = 0;
  total  = 0.0;
  count  = sizeof(tests) / sizeof(tests[0]);

  fprintf(stderr, CYAN "\nWelcome to im tests\n\n" RESET);

  for (i = 0; i < count; i++) {
    int32_t len;

    entry = tests + i;
    len   = (int32_t)strlen(entry->name);

    maxlen = maxlen > len ? maxlen : len;
  }

  maxlen += 5;

  fprintf(stderr,
          BOLDWHITE  "  %-*s    %-*s\n" RESET,
          maxlen, "Test Name", maxlen, "Elapsed Time");

  for (i = 0; i < count; i++) {
    entry   = tests + i;
    start   = clock();
    st      = entry->entry();
    end     = clock();
    elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    total  += elapsed;

    if (!st.status) {
      fprintf(stderr,
              BOLDRED  "  " FAIL_TEXT BOLDWHITE " %s " RESET, entry->name);
      if (st.msg) {
        fprintf(stderr,
                YELLOW "- %s" RESET,
                st.msg);
      }

      fprintf(stderr, "\n");

      failed++;
    } else {
      fprintf(stderr, GREEN  "  " OK_TEXT RESET " %-*s  ", maxlen, entry->name);

      if (elapsed > 0.01)
        fprintf(stderr, YELLOW "%.2fs", elapsed);
      else
        fprintf(stderr, "0");

      fprintf(stderr, "\n" RESET);
      passed++;
    }
  }

  if (failed == 0) {
    fprintf(stderr,
            BOLDGREEN "\n  All tests passed " FINAL_TEXT "\n" RESET);
  }

  fprintf(stderr,
          CYAN "\nim test results (%0.2fs):\n" RESET
          "--------------------------\n"

          MAGENTA "%d" RESET " tests ran, "
          GREEN   "%d" RESET " passed, "
          RED     "%d" RESET " failed\n\n" RESET,
          total,
          count,
          passed,
          failed);

  return failed;
}
