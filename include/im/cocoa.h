/*
 * Copyright (c), Recep Aslantas.
 * MIT License (MIT), http://opensource.org/licenses/MIT
 */

#ifndef im_cocoa_h
#define im_cocoa_h
#ifdef __APPLE__

#include "common.h"
#include <AppKit/NSImage.h>

NSImage*
im_nsimage(ImImage * __restrict im, bool copydata) {
  NSData  *imageData;
  NSImage *nsImage;

  if (copydata) {
    imageData = [NSData dataWithBytes: im->data length: im->len];
  } else {
    imageData = [NSData dataWithBytesNoCopy: im->data length: im->len];
  }

  nsImage = [[NSImage alloc] initWithData: imageData];

  return nsImage;
}

#endif
#endif /* im_cocoa_h */
