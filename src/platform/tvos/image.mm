// tvOS: images decode with ImageIO (PNG, JPEG, GIF's first frame, WebP).
// Core Graphics draws premultiplied, so the pixels are unpremultiplied to the
// straight alpha the shared code expects.
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>

#include <algorithm>

#include "platform/image.hpp"

namespace marigold::image {
namespace {

long property(CFDictionaryRef props, CFStringRef key) {
  CFNumberRef n = props ? static_cast<CFNumberRef>(CFDictionaryGetValue(props, key)) : nullptr;
  long v = 0;
  if (n) CFNumberGetValue(n, kCFNumberLongType, &v);
  return v;
}

}  // namespace

std::optional<px::Image> decodeFull(std::string_view bytes) {
  if (bytes.empty()) return std::nullopt;
  CFDataRef data = CFDataCreateWithBytesNoCopy(nullptr, reinterpret_cast<const UInt8*>(bytes.data()),
                                               static_cast<CFIndex>(bytes.size()), kCFAllocatorNull);
  CGImageSourceRef source = data ? CGImageSourceCreateWithData(data, nullptr) : nullptr;
  if (data) CFRelease(data);
  if (!source) return std::nullopt;
  CFDictionaryRef props = CGImageSourceCopyPropertiesAtIndex(source, 0, nullptr);
  long w = property(props, kCGImagePropertyPixelWidth), h = property(props, kCGImagePropertyPixelHeight);
  if (props) CFRelease(props);
  CGImageRef image = w > 0 && h > 0 && w * h <= kMaxPixels ? CGImageSourceCreateImageAtIndex(source, 0, nullptr)
                                                            : nullptr;
  CFRelease(source);
  if (!image) return std::nullopt;

  px::Image im;
  im.w = static_cast<int>(CGImageGetWidth(image));
  im.h = static_cast<int>(CGImageGetHeight(image));
  im.rgba.assign(static_cast<size_t>(im.w) * static_cast<size_t>(im.h) * 4u, 0);
  CGColorSpaceRef rgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef ctx = CGBitmapContextCreate(im.rgba.data(), static_cast<size_t>(im.w), static_cast<size_t>(im.h), 8,
                                           static_cast<size_t>(im.w) * 4u, rgb,
                                           kCGImageAlphaPremultipliedLast | kCGBitmapByteOrderDefault);
  CGColorSpaceRelease(rgb);
  if (!ctx) {
    CGImageRelease(image);
    return std::nullopt;
  }
  CGContextSetBlendMode(ctx, kCGBlendModeCopy);
  CGContextDrawImage(ctx, CGRectMake(0, 0, im.w, im.h), image);
  CGContextRelease(ctx);
  CGImageRelease(image);
  for (size_t i = 0; i + 3 < im.rgba.size(); i += 4) {
    unsigned a = im.rgba[i + 3];
    if (a == 0 || a == 255) continue;
    for (size_t c = 0; c < 3; ++c) im.rgba[i + c] = static_cast<uint8_t>(std::min(255u, (im.rgba[i + c] * 255u + a / 2) / a));
  }
  return im;
}

}  // namespace marigold::image
