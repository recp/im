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

#ifndef src_io_ccitt_ccitt_h
#define src_io_ccitt_ccitt_h

#include "../../common.h"

/* a raw CCITT fax stream (IM_OPTION_CCITT's parameters) */
IM_HIDE ImResult
ccitt_dec_mem(ImImage **dest, const uint8_t *data, size_t len, const im_open_config_t *conf);

/* JBIG2's MMR: T.6 rows, 1 black (ccitt.c says how) */
IM_HIDE int64_t
ccitt_mmr(const uint8_t *data, size_t len, uint32_t w, uint32_t h, uint8_t *out, size_t stride, size_t *used);

#endif /* src_io_ccitt_ccitt_h */
