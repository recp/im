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

#ifndef file_h
#define file_h

#include "common.h"

typedef struct ImFileResult {
  void    *raw;
  size_t   size;
  ImResult ret;
  bool     mmap;
} ImFileResult;

ImFileResult
im_readfile(const char * __restrict file);

#endif /* file_h */