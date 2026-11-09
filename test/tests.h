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

#ifndef tests_h
#define tests_h

#include "include/common.h"

/*
 * To register a test:
 *   1. use TEST_DECLARE() to forward declare test
 *   2. use TEST_ENTRY() to add test to list
 */

#if IM_WITH_JP2
/* jp2 */
TEST_DECLARE(jp2_fixtures)
TEST_DECLARE(jp2_16bit)
TEST_DECLARE(jp2_threads)
TEST_DECLARE(jp2_truncated)
#endif

#if IM_WITH_JPG
/* jpg */
TEST_DECLARE(jpg_fixtures)
TEST_DECLARE(jpg_16bit)
TEST_DECLARE(jpg_icc)
TEST_DECLARE(jpg_truncated)
TEST_DECLARE(jpg_cut_segment)
TEST_DECLARE(jpg_refused_early)
TEST_DECLARE(jpg_hierarchical)
#endif

#if IM_WITH_JPG && IM_WITH_JP2
/* load */
TEST_DECLARE(load_mem)
TEST_DECLARE(jpg_options)
TEST_DECLARE(jp2_options)
TEST_DECLARE(info_mem)
TEST_DECLARE(runner)
TEST_DECLARE(dest)
TEST_DECLARE(scale)
TEST_DECLARE(region)
TEST_DECLARE(memory_limit)
TEST_DECLARE(ccitt)
TEST_DECLARE(jbig2)
TEST_DECLARE(jls)
TEST_DECLARE(jpg_index)
#endif

/*****************************************************************************/

TEST_LIST {
#if IM_WITH_JP2
  /* jp2 */
  TEST_ENTRY(jp2_fixtures)
  TEST_ENTRY(jp2_16bit)
  TEST_ENTRY(jp2_threads)
  TEST_ENTRY(jp2_truncated)
#endif

#if IM_WITH_JPG
  /* jpg */
  TEST_ENTRY(jpg_fixtures)
  TEST_ENTRY(jpg_16bit)
  TEST_ENTRY(jpg_icc)
  TEST_ENTRY(jpg_truncated)
  TEST_ENTRY(jpg_cut_segment)
  TEST_ENTRY(jpg_refused_early)
  TEST_ENTRY(jpg_hierarchical)
#endif

#if IM_WITH_JPG && IM_WITH_JP2
  /* load */
  TEST_ENTRY(load_mem)
  TEST_ENTRY(jpg_options)
  TEST_ENTRY(jp2_options)
  TEST_ENTRY(info_mem)
  TEST_ENTRY(runner)
  TEST_ENTRY(dest)
  TEST_ENTRY(scale)
  TEST_ENTRY(region)
  TEST_ENTRY(memory_limit)
  TEST_ENTRY(ccitt)
  TEST_ENTRY(jbig2)
  TEST_ENTRY(jls)
  TEST_ENTRY(jpg_index)
#endif
};

#endif /* tests_h */
