/*
 * Copyright (c), Recep Aslantas.
 * MIT License (MIT), http://opensource.org/licenses/MIT
 */

#ifndef jpg_common_h
#define jpg_common_h

#include "../common.h"
#include "markers.h"
#include <netinet/in.h>

IM_INLINE
bool
jpg_marker_eq(ImByte *buff, JPGMarker marker) {
  return memcmp(buff, &marker, 2) == 0;
}

IM_INLINE
JPGMarker
jpg_marker(ImByte *buff) {
  return *(JPGMarker *)buff;
}

IM_INLINE
uint16_t
jpg_read_uint16(ImByte *buff) {
  uint16_t val;
  val = *(uint16_t *)buff;
  return ntohs(val);
}

IM_INLINE
bool
jpg_is_app_marker(JPGMarker mrk) {
  return JPG_APPn(0) == (JPG_APPn(0) & mrk);
}

IM_INLINE
bool
jpg_is_sof_marker(JPGMarker mrk) {
  return JPG_SOFn(0) == (JPG_SOFn(0) & mrk);
}

#endif /* jpg_common_h */
