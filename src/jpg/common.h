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

#ifndef jpg_common_h
#define jpg_common_h

#include "../common.h"
#include "markers.h"
#include <netinet/in.h>

#include "../color.h"

#if DEBUG
IM_HIDE
void
print_byte(uint8_t byte);
#endif

IM_INLINE
bool
jpg_marker_eq(ImByte *buff, JPGMarker marker) {
  return memcmp(buff, &marker, 2) == 0;
}

IM_INLINE
JPGMarker
jpg_marker(ImByte *buff) {
  uint16_t val;
  memcpy(&val, buff, 2);
  return val;
}

IM_INLINE
uint16_t
jpg_get_ui16(ImByte *buff) {
  uint16_t val;
  /* use memcpy instead of dereference of ushort,
     because of alignment warnings, errors */
  memcpy(&val, buff, 2);
  return ntohs(val);
}

IM_INLINE
bool
jpg_is_app_marker(JPGMarker mrk) {
  return JPG_APPn(F) == (JPG_APPn(F) & mrk);
}

IM_INLINE
bool
jpg_is_sof_marker(JPGMarker mrk) {
  return JPG_SOFn(F) == (JPG_SOFn(F) & mrk);
}

IM_INLINE
ImByte*
jfif_dec_skip_ext(ImByte *raw) {
  return raw + jpg_get_ui16(raw);
}

#endif /* jpg_common_h */
