/*
 * Copyright (C) 2024 Recep Aslantas
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

#ifndef src_jxl_h
#define src_jxl_h

#include "../common.h"

IM_HIDE
ImResult
jxl_dec(ImImage         ** __restrict dest,
        const char       * __restrict path,
        im_open_config_t * __restrict open_config);

/* from memory, through ImageIO: a JPEG XL (a bare codestream, or its
   ISO BMFF container) */
IM_HIDE
ImResult
jxl_dec_mem(ImImage               ** __restrict dest,
            const uint8_t          * __restrict data,
            size_t                              len,
            const im_open_config_t * __restrict open_config);

IM_HIDE
bool
jxl_info(const uint8_t *data, size_t len, ImInfo *info);

/* whether `data` starts as a JPEG XL does: a codestream's FF 0A, or the
   container's JXL box */
IM_INLINE
bool
jxl_is(const uint8_t *data, size_t len) {
  static const uint8_t box[12] = { 0, 0, 0, 12, 'J', 'X', 'L', ' ', 13, 10, 0x87, 10 };

  return (len >= 2u && data[0] == 0xFFu && data[1] == 0x0Au) || (len >= 12u && !memcmp(data, box, 12u));
}

#endif /* src_jxl_h */
