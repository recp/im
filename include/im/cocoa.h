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
