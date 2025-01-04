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
#ifdef __cplusplus
extern "C" {
#endif

#import <TargetConditionals.h>
#include "common.h"

#if !TARGET_OS_IOS
#  include <AppKit/NSImage.h>
#else
#  include <UIKit/UIKit.h>
#endif

#import <CoreGraphics/CoreGraphics.h>
#import <CoreImage/CoreImage.h>
#import <Foundation/Foundation.h>

/*
 https://docs.opencv.org/master/d3/def/tutorial_image_manipulation.html
 */
CF_RETURNS_RETAINED
CGImageRef
im_cgimage(ImImage *im, bool copydata) {
  NSData           *data;
  im_pal_t         *pal;
  CGColorSpaceRef   baseColorSpace, colorSpace;
  CGDataProviderRef provider;
  CGImageRef        imageRef;
  CGBitmapInfo      bitmapInfo;
  size_t            width, height, bytesPerRow;
  uint32_t          ncomp, bitsPerComponent;

  if (!im)
    return NULL;

  /* TODO: read from ImImage */
  
  switch (im->format) {
    case IM_FORMAT_RGB:
    case IM_FORMAT_BGR:
    case IM_FORMAT_YCbCr:      ncomp = 3; break;
    case IM_FORMAT_RGBA:
    case IM_FORMAT_ARGB:
    case IM_FORMAT_RGB0:
    case IM_FORMAT_BGRA:
    case IM_FORMAT_ABGR:
    case IM_FORMAT_BGR0:
    case IM_FORMAT_CMYK:       ncomp = 4; break;
    case IM_FORMAT_GRAY:
    case IM_FORMAT_GRAY_ALPHA:
    case IM_FORMAT_BLACKWHITE: ncomp = 1; break; /* TODO: ? */
    default:
      return NULL;
  }
  
  if (im->bitsPerComponent != 0) {
    bitsPerComponent = im->bitsPerComponent;
  } else {
    bitsPerComponent = 8;
  }
  
  width       = im->width;
  height      = im->height;
  // bytesPerRow = (uint32_t)(im->bytesPerPixel * width * ((float)MIN(bitsPerComponent, 8) / 8.0f) + im->row_pad_last);
  bytesPerRow = ((im->bitsPerPixel * width) + 7) / 8 + im->row_pad_last;

  if (!copydata) {
    data = [NSData dataWithBytesNoCopy: im->data.data length: bytesPerRow * height];
  } else {
    data = [NSData dataWithBytes:       im->data.data length: bytesPerRow * height];
  }

  /* base color space */
  if (ncomp == 1) {
    /* CGColorSpaceCreateWithName(kCGColorSpaceLinearGray) */
    baseColorSpace = CGColorSpaceCreateDeviceGray();
  } else {
    if (im->format != IM_FORMAT_CMYK) {
      baseColorSpace = CGColorSpaceCreateDeviceRGB();
    } else {
      baseColorSpace = CGColorSpaceCreateDeviceCMYK();
    }
  }

  /* palette */
  if (!((pal = im->pal) && pal->pal)) {
    colorSpace = baseColorSpace;
  } else {
    /* palette is used */
    colorSpace = CGColorSpaceCreateIndexed(baseColorSpace, pal->count, pal->pal);
  }

#if __has_feature(objc_arc)
  provider = CGDataProviderCreateWithCFData((__bridge CFDataRef)data);
#else
  provider = CGDataProviderCreateWithCFData(data);
#endif
  
  bitmapInfo = kCGBitmapByteOrderDefault;
  if (im->format != IM_FORMAT_BGRA) {
    if (bitsPerComponent > 8) {
      switch (im->byteOrder) {
        case IM_BYTEORDER_BIG:
          switch (bitsPerComponent) {
            case 16: bitmapInfo = kCGBitmapByteOrder16Big;   break;
            case 32: bitmapInfo = kCGBitmapByteOrder32Big;   break;
            default: break;
          }
          break;
        case IM_BYTEORDER_LITTLE:
          switch (bitsPerComponent) {
            case 16: bitmapInfo = kCGBitmapByteOrder16Little; break;
            case 32: bitmapInfo = kCGBitmapByteOrder32Little; break;
            default: break;
          }
          break;
        case IM_BYTEORDER_HOST:
#if __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
          switch (bitsPerComponent) {
            case 16: bitmapInfo = kCGBitmapByteOrder16Little; break;
            case 32: bitmapInfo = kCGBitmapByteOrder32Little; break;
            default: break;
          }
#else
          switch (bitsPerComponent) {
            case 16: bitmapInfo = kCGBitmapByteOrder16Big;   break;
            case 32: bitmapInfo = kCGBitmapByteOrder32Big;   break;
            default: break;
          }
#endif
          break;
        default:
          break;
      }
    }
  } else {
    switch (im->byteOrder) {
      case IM_BYTEORDER_BIG:    bitmapInfo = kCGBitmapByteOrder32Big;    break;
      case IM_BYTEORDER_LITTLE: bitmapInfo = kCGBitmapByteOrder32Little; break;
      case IM_BYTEORDER_HOST:
#if __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
        bitmapInfo = kCGBitmapByteOrder32Little;
#else
        bitmapInfo = kCGBitmapByteOrder32Big;
#endif
        break;
      default: break;
    }
    bitmapInfo |= kCGImageAlphaFirst;
  }
  
  bitmapInfo |= im->alphaInfo;

  imageRef   = CGImageCreate(width,                    /* width              */
                             height,                   /* height             */
                             bitsPerComponent,         /* bits per component */
                             im->bitsPerPixel,         /* bits per pixel     */
                             bytesPerRow,              /* bytesPerRow        */
                             colorSpace,               /* colorspace         */
                             bitmapInfo,               /* bitmap info        */
                             provider,                 /* CGDataProviderRef  */
                             NULL,                     /* decode             */
                             false,                    /* should interpolate */
                             kCGRenderingIntentDefault /* intent             */
                             );

  CGDataProviderRelease(provider);
  CGColorSpaceRelease(baseColorSpace);

  if (colorSpace != baseColorSpace) {
    CGColorSpaceRelease(colorSpace);
  }

  return imageRef;
}

#if !TARGET_OS_IOS

/* AppKit */

@interface NSImage(ImImage)
- (instancetype)initWithImImage:(NSString *)path copydata:(BOOL)copydata;
- (instancetype)initWithImImage:(NSString *)path;
@end

@implementation NSImage(ImImage)

- (instancetype)initWithImImage:(NSString *)path copydata:(BOOL)copydata {
  ImImage   *im;
  CGImageRef cgImage;
  
  im_load(&im,
          [path cStringUsingEncoding: NSUTF8StringEncoding],
          NULL,
          IM_OPEN_INTENT_READONLY);

  if (!(cgImage = im_cgimage(im, copydata)))
    return nil;
  
  return [self initWithCGImage: cgImage
                          size: CGSizeMake(im->width, im->height)];
}

- (instancetype)initWithImImage:(NSString *)path {
  return [self initWithImImage: path copydata: NO];
}

@end

CF_RETURNS_RETAINED
NSImage*
im_nsimage(ImImage * __restrict im, bool copydata) {
  CGImageRef cgImage;

  if (!(cgImage = im_cgimage(im, copydata)))
    return nil;

  return [[NSImage alloc] initWithCGImage: cgImage
                                     size: CGSizeMake(im->width, im->height)];
}

#else

/* UIKit */

CF_RETURNS_RETAINED
UIImage*
im_uiimage(ImImage * __restrict im, bool copydata) {
  CGImageRef cgImage;

  if (!(cgImage = im_cgimage(im, copydata)))
    return nil;
  
  return [[UIImage alloc] initWithCGImage: cgImage];
}

#endif

#ifdef __cplusplus
}
#endif
#endif
#endif /* im_cocoa_h */
