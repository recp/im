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

#include "com.h"
#include <stdio.h>

IM_HIDE
ImByte*
jpg_com(ImByte * __restrict pRaw, ImJpeg * __restrict jpg) {
  ImComment *com;
  uint32_t   len;

  len = jpg_get_ui16(pRaw);
  if (len == 0)
    return NULL;

  com = calloc(1, sizeof(*com) + len + 1);
  memcpy(com->buff, (pRaw + 2), len);
  com->buff[len] = '\0';
  
  com->len = len;

  com->next = jpg->comments;
  jpg->comments = com;

#ifdef DEBUG
  printf("\nComment:\n%s\n", com->buff);
#endif

  return pRaw + len;
}
