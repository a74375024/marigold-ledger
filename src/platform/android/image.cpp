// Android: images decode with the system's BitmapFactory (PNG, JPEG, GIF's
// first frame, WebP) in Bridge.imageDecode, unpremultiplied RGBA.
#include <android/bitmap.h>

#include <cstring>

#include "platform/android/jni.hpp"
#include "platform/image.hpp"

namespace marigold::image {

std::optional<px::Image> decodeFull(std::string_view bytes) {
  JNIEnv* e = android::env();
  if (!e) return std::nullopt;
  static jmethodID decode = android::method("imageDecode", "([BI)Landroid/graphics/Bitmap;");
  android::Local<jbyteArray> b(e, android::bytes(e, bytes));
  android::Local<jobject> bitmap(
      e, e->CallStaticObjectMethod(android::bridge(), decode, b.get(), static_cast<jint>(kMaxPixels)));
  if (android::caught(e, "imageDecode") || !bitmap) return std::nullopt;
  AndroidBitmapInfo info;
  void* pixels = nullptr;
  if (AndroidBitmap_getInfo(e, bitmap.get(), &info) != ANDROID_BITMAP_RESULT_SUCCESS ||
      info.format != ANDROID_BITMAP_FORMAT_RGBA_8888 ||
      AndroidBitmap_lockPixels(e, bitmap.get(), &pixels) != ANDROID_BITMAP_RESULT_SUCCESS)
    return std::nullopt;
  px::Image im;
  im.w = static_cast<int>(info.width);
  im.h = static_cast<int>(info.height);
  size_t row = static_cast<size_t>(im.w) * 4u;
  im.rgba.resize(row * static_cast<size_t>(im.h));
  // RGBA_8888 is R, G, B, A in memory; decoded unpremultiplied.
  for (int y = 0; y < im.h; ++y)
    std::memcpy(&im.rgba[static_cast<size_t>(y) * row], static_cast<const uint8_t*>(pixels) + y * info.stride, row);
  AndroidBitmap_unlockPixels(e, bitmap.get());
  android::recycle(e, bitmap.get());
  return im;
}

}  // namespace marigold::image
