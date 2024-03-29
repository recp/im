/*
 * Copyright (C) 2020 Recep Aslantas
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

#ifndef src_jpg_idct_h
#define src_jpg_idct_h

#include "../common.h"

IM_HIDE
void
jpg_idct(int16_t * __restrict blk);

IM_HIDE
void
jpg_idct2(int16_t blk[3][64]);

IM_HIDE
void
jpg_idct3(int16_t blk[64]);

#endif /* src_jpg_idct_h */
