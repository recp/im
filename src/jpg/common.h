/*
 * Copyright (c), Recep Aslantas.
 * MIT License (MIT), http://opensource.org/licenses/MIT
 */

#ifndef jpg_common_h
#define jpg_common_h

#include "../common.h"
#include "markers.h"

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
JPGMarker
jpg_buff_advc_m(ImByte **buff) {
  return *(JPGMarker *)buff;
}

#endif /* jpg_common_h */
