/*
 * Copyright (C) 2024 Recep Aslantas
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

#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreImage/CoreImage.h>
#import "heic.h"

IM_HIDE
ImResult
coreimg_dec(ImImage         ** __restrict dest,
            const char       * __restrict path,
            im_open_config_t * __restrict open_config) {
  ImImage          *im;
  NSURL            *fileURL;
  CGImageSourceRef  imageSource;
  CGImageRef        cgImage;
  CFDictionaryRef   props;

  CGImageAlphaInfo  alphaInfo;
  CGColorSpaceRef   cgColorSpace;
  CGBitmapInfo      bitmapInfo;
  CGDataProviderRef dataProvider;
  CFDataRef         imageData;

  CFNumberRef       numref, numref2;
  int32_t           width = 0, height = 0;
  size_t            dataLength, bytesPerRow, bitsPerPixel, bitsPerComponent, sourceLength;

  ImOrientationType ori;
  ImAlphaInfo       imAlphaInfo;
  ImFormat          imFormat;
  ImColorSpace      imColorSpace;
  ImByteOrder       byteOrder;
  bool              hasAlpha;

  im          = NULL;
  imageSource = NULL;
  cgImage     = NULL;
  props       = NULL;
  width       = height = 0;

  fileURL = [[NSURL alloc] initFileURLWithPath: [NSString stringWithUTF8String: path]];
  if (!fileURL) {
    return IM_ERR;
  }

  imageSource = CGImageSourceCreateWithURL((CFURLRef)fileURL, NULL);
  [fileURL release];

  if (!imageSource) {
    return IM_ERR;
  }

  props = CGImageSourceCopyPropertiesAtIndex(imageSource, 0, NULL);
  if (!props) {
    CFRelease(imageSource);
    return IM_ERR;
  }

  if ((numref = CFDictionaryGetValue(props, kCGImagePropertyPixelWidth))
      && (numref2 = CFDictionaryGetValue(props, kCGImagePropertyPixelHeight))) {
    CFNumberGetValue(numref,  kCFNumberSInt32Type, &width);
    CFNumberGetValue(numref2, kCFNumberSInt32Type, &height);
  } else {
    CFRelease(props);
    CFRelease(imageSource);
    return IM_ERR;
  }

  if (width <= 0 || height <= 0) {
    CFRelease(props);
    CFRelease(imageSource);
    return IM_ERR;
  }

  ori = IM_ORIENTATION_UP;
  if ((numref = CFDictionaryGetValue(props, kCGImagePropertyOrientation))) {
    int cgOrientation;
    CFNumberGetValue(numref, kCFNumberIntType, &cgOrientation);
    switch (cgOrientation) {
      case 1:  ori = IM_ORIENTATION_UP;    break;
      case 3:  ori = IM_ORIENTATION_DOWN;  break;
      case 6:  ori = IM_ORIENTATION_RIGHT; break;
      case 8:  ori = IM_ORIENTATION_LEFT;  break;
      default: ori = IM_ORIENTATION_UP;    break;
    }
  }

  cgImage = CGImageSourceCreateImageAtIndex(imageSource, 0, NULL);
  CFRelease(props);
  CFRelease(imageSource);

  if (!cgImage) {
    return IM_ERR;
  }

  bytesPerRow      = CGImageGetBytesPerRow(cgImage);
  bitsPerPixel     = CGImageGetBitsPerPixel(cgImage);
  bitsPerComponent = CGImageGetBitsPerComponent(cgImage);
  alphaInfo        = CGImageGetAlphaInfo(cgImage);
  cgColorSpace     = CGImageGetColorSpace(cgImage);
  bitmapInfo       = CGImageGetBitmapInfo(cgImage);

  imAlphaInfo      = IM_ALPHA_NONE;
  imColorSpace     = IM_COLORSPACE_UNKNOWN;
  byteOrder        = IM_BYTEORDER_ANY;
  hasAlpha         = false;

  switch (alphaInfo) {
    case kCGImageAlphaNone:                                imAlphaInfo = IM_ALPHA_NONE;            break;
    case kCGImageAlphaPremultipliedLast:  hasAlpha = true; imAlphaInfo = IM_ALPHA_PREMUL_LAST;     break;
    case kCGImageAlphaPremultipliedFirst: hasAlpha = true; imAlphaInfo = IM_ALPHA_PREMUL_FIRST;    break;
    case kCGImageAlphaLast:               hasAlpha = true; imAlphaInfo = IM_ALPHA_LAST;            break;
    case kCGImageAlphaFirst:              hasAlpha = true; imAlphaInfo = IM_ALPHA_FIRST;           break;
    case kCGImageAlphaNoneSkipLast:                        imAlphaInfo = IM_ALPHA_NONE_SKIP_LAST;  break;
    case kCGImageAlphaNoneSkipFirst:                       imAlphaInfo = IM_ALPHA_NONE_SKIP_FIRST; break;
    default: break;
  }

  if (cgColorSpace) {
    switch (CGColorSpaceGetModel(cgColorSpace)) {
      case kCGColorSpaceModelRGB: {
        CFStringRef colorSpaceName = CGColorSpaceCopyName(cgColorSpace);
        if (colorSpaceName) {
          if (CFStringCompare(colorSpaceName, kCGColorSpaceDisplayP3, 0) == kCFCompareEqualTo) {
            imColorSpace = IM_COLORSPACE_P3;
          } else if (CFStringCompare(colorSpaceName, kCGColorSpaceSRGB, 0) == kCFCompareEqualTo ||
                     CFStringCompare(colorSpaceName, kCGColorSpaceGenericRGB, 0) == kCFCompareEqualTo) {
            imColorSpace = IM_COLORSPACE_sRGB;
          } else {
            imColorSpace = IM_COLORSPACE_UNKNOWN;
          }
          CFRelease(colorSpaceName);
        }
        imFormat = hasAlpha ? IM_FORMAT_RGBA : IM_FORMAT_RGB;
        break;
      }
      case kCGColorSpaceModelMonochrome: {
        imColorSpace = IM_COLORSPACE_GRAY;
        imFormat     = IM_FORMAT_GRAY;
        break;
      }
      case kCGColorSpaceModelCMYK: {
        imColorSpace = IM_COLORSPACE_CMYK;
        imFormat     = IM_FORMAT_CMYK;
        break;
      }
      default: {
        imColorSpace = IM_COLORSPACE_UNKNOWN;
        imFormat     = IM_FORMAT_NONE;
        break;
      }
    }
  } else {
    imColorSpace = IM_COLORSPACE_sRGB;
    imFormat     = hasAlpha ? IM_FORMAT_RGBA : IM_FORMAT_RGB;
  }

  if (bitmapInfo & kCGBitmapByteOrderMask) {
    if ((bitmapInfo & kCGBitmapByteOrder32Big) == kCGBitmapByteOrder32Big
        || (bitmapInfo & kCGBitmapByteOrder16Big) == kCGBitmapByteOrder16Big) {
      byteOrder = IM_BYTEORDER_BIG;
    } else if ((bitmapInfo & kCGBitmapByteOrder32Little) == kCGBitmapByteOrder32Little
               || (bitmapInfo & kCGBitmapByteOrder16Little) == kCGBitmapByteOrder16Little) {
      byteOrder = IM_BYTEORDER_LITTLE;
    }
  }

  im = calloc(1, sizeof(*im));
  if (!im) {
    CGImageRelease(cgImage);
    return IM_ERR;
  }

  im->width              = (uint32_t)width;
  im->height             = (uint32_t)height;
  im->bitsPerPixel       = (int)bitsPerPixel;
  im->bitsPerComponent   = (int)bitsPerComponent;
  im->bytesPerPixel      = (uint32_t)(bitsPerPixel / 8);
  im->componentsPerPixel = (int)(bitsPerPixel / bitsPerComponent);
  im->format             = imFormat;
  im->alphaInfo          = imAlphaInfo;
  im->colorSpace         = imColorSpace;
  im->ori                = ori;
  im->byteOrder          = byteOrder;
  im->row_pad_last       = (uint32_t)(bytesPerRow - (width * (bitsPerPixel / 8)));

  dataLength    = height * bytesPerRow;
  im->len       = dataLength;
  im->data.data = malloc(dataLength);

  if (!im->data.data) {
    free(im);
    CGImageRelease(cgImage);
    return IM_ERR;
  }

  dataProvider = CGImageGetDataProvider(cgImage);
  imageData    = CGDataProviderCopyData(dataProvider);
  if (!imageData) {
    free(im->data.data);
    free(im);
    CGImageRelease(cgImage);
    return IM_ERR;
  }

  sourceLength = CFDataGetLength(imageData);
  if (sourceLength > dataLength) {
    CFRelease(imageData);
    free(im->data.data);
    free(im);
    CGImageRelease(cgImage);
    return IM_ERR;
  }

  memcpy(im->data.data, CFDataGetBytePtr(imageData), sourceLength);
  CFRelease(imageData);

  CGImageRelease(cgImage);
  *dest = im;
  return IM_OK;
}
