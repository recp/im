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
#import <ImageIO/ImageIO.h>
#import "coreimg.h"

/*
 * Images ImageIO decodes (HEIC, JPEG XL, and what im's own decoders leave):
 * the first image of a source, from a file or from memory (the bytes taken
 * where they lie, not copied). Its pixels as ImageIO lays them out (its
 * depth, alpha, colour space and byte order told), drawn straight into the
 * memory they go to (im's, or the caller's: IM_OPTION_DEST) where a bitmap
 * context of that very layout can be made, so they're written once, the
 * same values (one colour space, one format: nothing converted); otherwise
 * copied from the image's data once. A region (IM_OPTION_REGION) is the
 * image's rectangle alone; IM_OPTION_SCALE asks ImageIO for a subsampled
 * image (formats that can't give one come at full size: the image says)
 */

/* `cg`'s pixels at (rx, ry), w × h of them, into `out` (`stride` a row):
   an RGB or gray image's drawn where a context of its own layout can be
   made, else copied from its data (CMYK's too: drawing matches its colours
   even into its own space, so its values move) */
static bool
cg_pixels(CGImageRef cg, uint32_t rx, uint32_t ry, uint32_t w, uint32_t h, uint8_t *out, size_t stride) {
  size_t          bpc = CGImageGetBitsPerComponent(cg), bpp = CGImageGetBitsPerPixel(cg), iw = CGImageGetWidth(cg);
  size_t          ih = CGImageGetHeight(cg), row = (size_t)w * (bpp / 8u), y;
  CGColorSpaceRef cs = CGImageGetColorSpace(cg);
  CGContextRef    ctx;
  CFDataRef       px;
  const uint8_t  *p;
  size_t          sbpr;

  if (cs && bpp % 8u == 0u
      && (CGColorSpaceGetModel(cs) == kCGColorSpaceModelRGB || CGColorSpaceGetModel(cs) == kCGColorSpaceModelMonochrome)) {
    ctx = CGBitmapContextCreate(out, w, h, bpc, stride, cs, CGImageGetBitmapInfo(cg));
    if (ctx) {
      /* the image placed so its (rx, ry) is the context's top-left (a
         context's origin is its bottom-left) */
      CGContextSetBlendMode(ctx, kCGBlendModeCopy);
      CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);
      CGContextSetShouldAntialias(ctx, false);
      CGContextDrawImage(ctx, CGRectMake(-(CGFloat)rx, (CGFloat)h + (CGFloat)ry - (CGFloat)ih, (CGFloat)iw, (CGFloat)ih), cg);
      CGContextRelease(ctx);
      return true;
    }
  }
  if (!(px = CGDataProviderCopyData(CGImageGetDataProvider(cg)))) return false;
  p    = CFDataGetBytePtr(px);
  sbpr = CGImageGetBytesPerRow(cg);
  if ((size_t)CFDataGetLength(px) < sbpr * (ry + h - 1u) + (size_t)(rx + w) * (bpp / 8u)) {
    CFRelease(px);
    return false;
  }
  for (y = 0; y < h; y++) memcpy(out + y * stride, p + (ry + y) * sbpr + (size_t)rx * (bpp / 8u), row);
  CFRelease(px);
  return true;
}

/* the first image of `src` into a new ImImage */
static ImResult
from_source(ImImage **dest, CGImageSourceRef src, const im_open_config_t *conf) {
  ImImage          *im;
  CFDictionaryRef   props, opts = NULL;
  CGImageRef        cg;
  CGColorSpaceRef   cs;
  CGImageAlphaInfo  alpha;
  CGBitmapInfo      info;
  CFNumberRef       nw, nh, no;
  int32_t           width = 0, height = 0, orient = 1;
  uint32_t          rx = 0, ry = 0, rw, rh;
  size_t            bpp, bpc, row, stride, len;
  bool              has_alpha = false;
  uint8_t          *out;

  *dest = NULL;
  if (!(props = CGImageSourceCopyPropertiesAtIndex(src, 0, NULL))) return IM_ERR;
  if ((nw = CFDictionaryGetValue(props, kCGImagePropertyPixelWidth)) && (nh = CFDictionaryGetValue(props, kCGImagePropertyPixelHeight))) {
    CFNumberGetValue(nw, kCFNumberSInt32Type, &width);
    CFNumberGetValue(nh, kCFNumberSInt32Type, &height);
  }
  if ((no = CFDictionaryGetValue(props, kCGImagePropertyOrientation))) CFNumberGetValue(no, kCFNumberSInt32Type, &orient);
  CFRelease(props);
  if (width <= 0 || height <= 0) return IM_ERR;

  /* smaller where asked: ImageIO's subsampling (2, 4, 8) */
  if (conf->scale == 2u || conf->scale == 4u || conf->scale == 8u) {
    int32_t     f = (int32_t)conf->scale;
    CFNumberRef n = CFNumberCreate(NULL, kCFNumberSInt32Type, &f);
    const void *k[1] = { kCGImageSourceSubsampleFactor }, *v[1] = { n };

    opts = CFDictionaryCreate(NULL, k, v, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFRelease(n);
  }
  cg = CGImageSourceCreateImageAtIndex(src, 0, opts);
  if (opts) CFRelease(opts);
  if (!cg) return IM_ERR;

  /* the rectangle wanted: the region's, cut to the image */
  rw = (uint32_t)CGImageGetWidth(cg);
  rh = (uint32_t)CGImageGetHeight(cg);
  if (conf->region) {
    if (conf->regionX >= rw || conf->regionY >= rh || !conf->regionW || !conf->regionH) {
      CGImageRelease(cg);
      return IM_ERR;
    }
    rx = conf->regionX;
    ry = conf->regionY;
    rw = conf->regionW < rw - rx ? conf->regionW : rw - rx;
    rh = conf->regionH < rh - ry ? conf->regionH : rh - ry;
  }

  bpp   = CGImageGetBitsPerPixel(cg);
  bpc   = CGImageGetBitsPerComponent(cg);
  alpha = CGImageGetAlphaInfo(cg);
  cs    = CGImageGetColorSpace(cg);
  info  = CGImageGetBitmapInfo(cg);
  if (!bpc || bpp % 8u || bpp < bpc || !(im = calloc(1, sizeof(*im)))) {
    CGImageRelease(cg);
    return IM_ERR;
  }

  switch (alpha) {
    case kCGImageAlphaPremultipliedLast:  has_alpha = true; im->alphaInfo = IM_ALPHA_PREMUL_LAST;     break;
    case kCGImageAlphaPremultipliedFirst: has_alpha = true; im->alphaInfo = IM_ALPHA_PREMUL_FIRST;    break;
    case kCGImageAlphaLast:               has_alpha = true; im->alphaInfo = IM_ALPHA_LAST;            break;
    case kCGImageAlphaFirst:              has_alpha = true; im->alphaInfo = IM_ALPHA_FIRST;           break;
    case kCGImageAlphaNoneSkipLast:                         im->alphaInfo = IM_ALPHA_NONE_SKIP_LAST;  break;
    case kCGImageAlphaNoneSkipFirst:                        im->alphaInfo = IM_ALPHA_NONE_SKIP_FIRST; break;
    default:                                                im->alphaInfo = IM_ALPHA_NONE;            break;
  }
  im->format     = has_alpha ? IM_FORMAT_RGBA : IM_FORMAT_RGB;
  im->colorSpace = IM_COLORSPACE_sRGB;
  if (cs) {
    switch (CGColorSpaceGetModel(cs)) {
      case kCGColorSpaceModelRGB: {
        CFStringRef name = CGColorSpaceCopyName(cs);

        im->colorSpace = IM_COLORSPACE_UNKNOWN;
        if (name) {
          if (CFStringCompare(name, kCGColorSpaceDisplayP3, 0) == kCFCompareEqualTo) im->colorSpace = IM_COLORSPACE_P3;
          else if (CFStringCompare(name, kCGColorSpaceSRGB, 0) == kCFCompareEqualTo
                   || CFStringCompare(name, kCGColorSpaceGenericRGB, 0) == kCFCompareEqualTo) im->colorSpace = IM_COLORSPACE_sRGB;
          CFRelease(name);
        }
        break;
      }
      case kCGColorSpaceModelMonochrome: im->colorSpace = IM_COLORSPACE_GRAY; im->format = IM_FORMAT_GRAY; break;
      case kCGColorSpaceModelCMYK:       im->colorSpace = IM_COLORSPACE_CMYK; im->format = IM_FORMAT_CMYK; break;
      default:                           im->colorSpace = IM_COLORSPACE_UNKNOWN; im->format = IM_FORMAT_NONE; break;
    }
  }
  im->byteOrder = IM_BYTEORDER_ANY;
  if ((info & kCGBitmapByteOrderMask) == kCGBitmapByteOrder32Big || (info & kCGBitmapByteOrderMask) == kCGBitmapByteOrder16Big) {
    im->byteOrder = IM_BYTEORDER_BIG;
  } else if ((info & kCGBitmapByteOrderMask) == kCGBitmapByteOrder32Little
             || (info & kCGBitmapByteOrderMask) == kCGBitmapByteOrder16Little) {
    im->byteOrder = IM_BYTEORDER_LITTLE;
  }
  switch (orient) {
    case 3:  im->ori = IM_ORIENTATION_DOWN;  break;
    case 6:  im->ori = IM_ORIENTATION_RIGHT; break;
    case 8:  im->ori = IM_ORIENTATION_LEFT;  break;
    default: im->ori = IM_ORIENTATION_UP;    break;
  }
  im->width              = rw;
  im->height             = rh;
  im->bitsPerPixel       = (uint32_t)bpp;
  im->bitsPerComponent   = (uint32_t)bpc;
  im->bytesPerPixel      = (uint32_t)(bpp / 8u);
  im->componentsPerPixel = (uint32_t)(bpp / bpc);

  /* where they go: the caller's memory (its stride, enough of it), or a
     tight block of im's */
  row = (size_t)rw * (bpp / 8u);
  if (conf->dest) {
    stride = conf->destStride;
    if (stride < row || conf->destSize < stride * (rh - 1u) + row) {
      free(im);
      CGImageRelease(cg);
      return IM_ERR;
    }
    out              = conf->dest;
    im->len          = stride * (rh - 1u) + row;
    im->row_pad_last = (uint32_t)(stride - row);
  } else {
    stride = row;
    len    = row * rh;
    if (!(out = malloc(len ? len : 1u))) {
      free(im);
      CGImageRelease(cg);
      return IM_ERR;
    }
    im->data.data = out;
    im->len       = len;
  }
  if (!cg_pixels(cg, rx, ry, rw, rh, out, stride)) {
    free(im->data.data);
    free(im);
    CGImageRelease(cg);
    return IM_ERR;
  }
  CGImageRelease(cg);
  im->openIntent = conf->openIntent;
  *dest          = im;
  return IM_OK;
}

IM_HIDE
ImResult
coreimg_dec(ImImage         ** __restrict dest,
            const char       * __restrict path,
            im_open_config_t * __restrict open_config) {
  CFURLRef         url;
  CGImageSourceRef src;
  ImResult         ret;

  *dest = NULL;
  if (!(url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)path, (CFIndex)strlen(path), false))) return IM_ERR;
  src = CGImageSourceCreateWithURL(url, NULL);
  CFRelease(url);
  if (!src) return IM_ERR;
  ret = from_source(dest, src, open_config);
  CFRelease(src);
  return ret;
}

/* `data` as ImageIO reads it: its bytes not copied (they outlive the
   decode) */
static CGImageSourceRef
mem_source(const uint8_t *data, size_t len) {
  CFDataRef        d;
  CGImageSourceRef src;

  if (!data || !len || len > (size_t)LONG_MAX) return NULL;
  if (!(d = CFDataCreateWithBytesNoCopy(NULL, data, (CFIndex)len, kCFAllocatorNull))) return NULL;
  src = CGImageSourceCreateWithData(d, NULL);
  CFRelease(d);
  return src;
}

IM_HIDE
ImResult
coreimg_dec_mem(ImImage               ** __restrict dest,
                const uint8_t          * __restrict data,
                size_t                              len,
                const im_open_config_t * __restrict open_config) {
  CGImageSourceRef src = mem_source(data, len);
  ImResult         ret;

  *dest = NULL;
  if (!src) return IM_EBADF;
  ret = from_source(dest, src, open_config);
  CFRelease(src);
  return ret == IM_OK ? IM_OK : IM_EBADF;
}

IM_HIDE
bool
coreimg_info_mem(const uint8_t *data, size_t len, ImInfo *info) {
  CGImageSourceRef src = mem_source(data, len);
  CFDictionaryRef  props;
  CFNumberRef      n;
  int32_t          v = 0;
  bool             ok = false;

  memset(info, 0, sizeof(*info));
  if (!src) return false;
  if ((props = CGImageSourceCopyPropertiesAtIndex(src, 0, NULL))) {
    if ((n = CFDictionaryGetValue(props, kCGImagePropertyPixelWidth)) && CFNumberGetValue(n, kCFNumberSInt32Type, &v) && v > 0) {
      info->width = (uint32_t)v;
    }
    if ((n = CFDictionaryGetValue(props, kCGImagePropertyPixelHeight)) && CFNumberGetValue(n, kCFNumberSInt32Type, &v) && v > 0) {
      info->height = (uint32_t)v;
    }
    if ((n = CFDictionaryGetValue(props, kCGImagePropertyDepth)) && CFNumberGetValue(n, kCFNumberSInt32Type, &v) && v > 0) {
      info->bitsPerComponent = (uint32_t)v;
    }
    info->opacity = CFDictionaryGetValue(props, kCGImagePropertyHasAlpha) == kCFBooleanTrue;
    info->pages   = (uint32_t)CGImageSourceGetCount(src);
    ok            = info->width && info->height;
    CFRelease(props);
  }
  CFRelease(src);
  return ok;
}
