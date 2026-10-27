// Android: the font engine is the system's text drawing (Bridge.textMeasure,
// textHeight, textRender: android.graphics.Paint with the same Inter
// Display files as webOS). A line comes back as an alpha-only bitmap.
#include <android/bitmap.h>

#include <cstring>

#include "platform/android/jni.hpp"
#include "platform/text.hpp"

namespace marigold::text {
namespace {

class AndroidFonts final : public Fonts {
 public:
  bool init(const std::string& fontDir) override {
    JNIEnv* e = android::env();
    static jmethodID init = android::method("textInit", "([B)Z");
    android::Local<jbyteArray> dir(e, android::bytes(e, fontDir));
    bool ok = e->CallStaticBooleanMethod(android::bridge(), init, dir.get());
    return !android::caught(e, "textInit") && ok;
  }

  void shutdown() override {}

  bool measure(const std::string& s, Weight w, int px, int& width) override {
    JNIEnv* e = android::env();
    static jmethodID measure = android::method("textMeasure", "([BII)F");
    android::Local<jbyteArray> b(e, android::bytes(e, s));
    float f = e->CallStaticFloatMethod(android::bridge(), measure, b.get(), static_cast<jint>(w), px);
    if (android::caught(e, "textMeasure")) return false;
    width = static_cast<int>(f + 0.5f);
    return true;
  }

  float lineHeight(Weight w, int px) override {
    JNIEnv* e = android::env();
    static jmethodID height = android::method("textHeight", "(II)F");
    float h = e->CallStaticFloatMethod(android::bridge(), height, static_cast<jint>(w), px);
    return android::caught(e, "textHeight") ? 0.0f : h;
  }

  bool render(const std::string& s, Weight w, int px, px::Image& out) override {
    JNIEnv* e = android::env();
    static jmethodID render = android::method("textRender", "([BII)Landroid/graphics/Bitmap;");
    android::Local<jbyteArray> b(e, android::bytes(e, s));
    android::Local<jobject> bitmap(e, e->CallStaticObjectMethod(android::bridge(), render, b.get(), static_cast<jint>(w), px));
    if (android::caught(e, "textRender") || !bitmap) return false;
    AndroidBitmapInfo info;
    void* pixels = nullptr;
    if (AndroidBitmap_getInfo(e, bitmap.get(), &info) != ANDROID_BITMAP_RESULT_SUCCESS ||
        info.format != ANDROID_BITMAP_FORMAT_A_8 ||
        AndroidBitmap_lockPixels(e, bitmap.get(), &pixels) != ANDROID_BITMAP_RESULT_SUCCESS)
      return false;
    out.w = static_cast<int>(info.width);
    out.h = static_cast<int>(info.height);
    out.rgba.resize(static_cast<size_t>(out.w) * static_cast<size_t>(out.h) * 4u);
    // White, with the glyphs' coverage as alpha (the renderer reads alpha).
    for (int y = 0; y < out.h; ++y) {
      const auto* row = static_cast<const uint8_t*>(pixels) + static_cast<size_t>(y) * info.stride;
      uint8_t* dst = &out.rgba[static_cast<size_t>(y) * static_cast<size_t>(out.w) * 4u];
      for (int x = 0; x < out.w; ++x) {
        dst[0] = dst[1] = dst[2] = 255;
        dst[3] = row[x];
        dst += 4;
      }
    }
    AndroidBitmap_unlockPixels(e, bitmap.get());
    android::recycle(e, bitmap.get());
    return true;
  }
};

}  // namespace

std::unique_ptr<Fonts> makeFonts() { return std::make_unique<AndroidFonts>(); }

}  // namespace marigold::text
