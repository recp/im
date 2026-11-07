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

#ifndef src_io_jbig2_jbig2_h
#define src_io_jbig2_jbig2_h

#include "../../common.h"

/* a JBIG2 file or a stream's embedded segments (the globals' first) */
IM_HIDE ImResult
jbig2_dec_mem(ImImage **dest, const uint8_t *data, size_t len, const im_open_config_t *conf);

/* a file's first page's size */
IM_HIDE bool
jbig2_info(const uint8_t *p, size_t len, ImInfo *info);

#endif /* src_io_jbig2_jbig2_h */
