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

/*
 https://docs.opencv.org/master/d3/def/tutorial_image_manipulation.html
 */
CGImageRef
im_cgimage(ImImage *im, bool copydata) {
  NSData           *data;
  CGColorSpaceRef   colorSpace;
  CGDataProviderRef provider;
  CGImageRef        imageRef;
  size_t            width, height, bytesPerRow;
  int               elemSize, bitsPerPixel;
  
  /* TODO: read from ImImage */
  elemSize     = 3;
  bitsPerPixel = 8;
  
  width       = im->width;
  height      = im->height;
  bytesPerRow = elemSize * width;
  
  if (!copydata) {
    data = [NSData dataWithBytesNoCopy: im->data length: bytesPerRow * height];
  } else {
    data = [NSData dataWithBytes:       im->data length: bytesPerRow * height];
  }

  if (elemSize == 1) {
    colorSpace = CGColorSpaceCreateDeviceGray();
  } else {
    colorSpace = CGColorSpaceCreateDeviceRGB();
  }

#if __has_feature(objc_arc)
  provider = CGDataProviderCreateWithCFData((__bridge CFDataRef)data);
#else
  provider = CGDataProviderCreateWithCFData(data);
#endif

  imageRef = CGImageCreate(width,                                       /* width              */
                           height,                                      /* height             */
                           bitsPerPixel,                                /* bits per component */
                           bitsPerPixel * elemSize,                     /* bits per pixel     */
                           bytesPerRow,                                 /* bytesPerRow        */
                           colorSpace,                                  /* colorspace         */
                           kCGImageAlphaNone|kCGBitmapByteOrderDefault, /* bitmap info        */
                           provider,                                    /* CGDataProviderRef  */
                           NULL,                                        /* decode             */
                           false,                                       /* should interpolate */
                           kCGRenderingIntentDefault                    /* intent             */
                           );

  CGDataProviderRelease(provider);
  CGColorSpaceRelease(colorSpace);

  return imageRef;
}

NSImage*
im_nsimage(ImImage * __restrict im, bool copydata) {
  CGImageRef cgImage;
  
  cgImage = im_cgimage(im, copydata);
  
  if (!cgImage)
    return nil;

  return [[NSImage alloc] initWithCGImage: cgImage
                                     size: CGSizeMake(im->width, im->height)];
}

#endif
#endif /* im_cocoa_h */
