// tvOS: the font engine is Core Text with the same Inter Display files as
// webOS and Android. A line is drawn white into an alpha-only bitmap, as tall
// as the font's ascent plus descent.
#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

#include "core/util/log.hpp"
#include "platform/text.hpp"

namespace marigold::text {
namespace {

class CoreTextFonts final : public Fonts {
 public:
  ~CoreTextFonts() override { shutdown(); }

  bool init(const std::string& fontDir) override {
    const char* names[] = {"InterDisplay-Regular.ttf", "InterDisplay-Medium.ttf", "InterDisplay-Bold.ttf"};
    bool all = true;
    for (int i = 0; i < 3; ++i) {
      std::string path = fontDir + "/" + names[i];
      CGDataProviderRef data = CGDataProviderCreateWithFilename(path.c_str());
      faces_[i] = data ? CGFontCreateWithDataProvider(data) : nullptr;
      if (data) CGDataProviderRelease(data);
      if (!faces_[i]) {
        MLOG_W("text", "no font %s", names[i]);
        all = false;
      }
    }
    return all;
  }

  void shutdown() override {
    for (auto& entry : fonts_) CFRelease(entry.second);
    fonts_.clear();
    for (CGFontRef& f : faces_) {
      if (f) CGFontRelease(f);
      f = nullptr;
    }
  }

  bool measure(const std::string& s, Weight w, int px, int& width) override {
    CTLineRef line = makeLine(s, w, px);
    if (!line) return false;
    width = static_cast<int>(std::ceil(CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr)));
    CFRelease(line);
    return true;
  }

  float lineHeight(Weight w, int px) override {
    CTFontRef f = font(w, px);
    return f ? static_cast<float>(std::ceil(CTFontGetAscent(f) + CTFontGetDescent(f))) : 0.0f;
  }

  bool render(const std::string& s, Weight w, int px, px::Image& out) override {
    CTFontRef f = font(w, px);
    CTLineRef line = makeLine(s, w, px);
    if (!f || !line) {
      if (line) CFRelease(line);
      return false;
    }
    double width = CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr);
    int iw = std::max(1, static_cast<int>(std::ceil(width)));
    int ih = std::max(1, static_cast<int>(std::ceil(CTFontGetAscent(f) + CTFontGetDescent(f))));
    std::vector<uint8_t> alpha(static_cast<size_t>(iw) * static_cast<size_t>(ih), 0);
    CGContextRef ctx = CGBitmapContextCreate(alpha.data(), static_cast<size_t>(iw), static_cast<size_t>(ih), 8,
                                             static_cast<size_t>(iw), nullptr, kCGImageAlphaOnly);
    if (!ctx) {
      CFRelease(line);
      return false;
    }
    CGContextSetAllowsAntialiasing(ctx, true);
    CGContextSetShouldAntialias(ctx, true);
    CGContextSetGrayFillColor(ctx, 1.0, 1.0);
    // The baseline sits the descent above the bottom (Core Graphics counts up).
    CGContextSetTextPosition(ctx, 0, CTFontGetDescent(f));
    CTLineDraw(line, ctx);
    CGContextRelease(ctx);
    CFRelease(line);
    // The bitmap's first row is the line's top.
    out.w = iw;
    out.h = ih;
    out.rgba.resize(alpha.size() * 4u);
    for (size_t i = 0; i < alpha.size(); ++i) {
      out.rgba[i * 4] = out.rgba[i * 4 + 1] = out.rgba[i * 4 + 2] = 255;
      out.rgba[i * 4 + 3] = alpha[i];
    }
    return true;
  }

 private:
  CTFontRef font(Weight w, int px) {
    int i = static_cast<int>(w);
    if (i < 0 || i > 2 || px <= 0) return nullptr;
    auto key = std::make_pair(i, px);
    auto it = fonts_.find(key);
    if (it != fonts_.end()) return it->second;
    if (!faces_[i]) return nullptr;
    CTFontRef f = CTFontCreateWithGraphicsFont(faces_[i], static_cast<CGFloat>(px), nullptr, nullptr);
    if (!f) return nullptr;
    fonts_.emplace(key, f);
    return f;
  }

  // The caller releases it.
  CTLineRef makeLine(const std::string& s, Weight w, int px) {
    CTFontRef f = font(w, px);
    if (!f) return nullptr;
    CFStringRef text = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(s.data()),
                                               static_cast<CFIndex>(s.size()), kCFStringEncodingUTF8, false);
    if (!text) return nullptr;
    const void* keys[] = {kCTFontAttributeName, kCTForegroundColorFromContextAttributeName};
    const void* values[] = {f, kCFBooleanTrue};
    CFDictionaryRef attrs = CFDictionaryCreate(nullptr, keys, values, 2, &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef as = CFAttributedStringCreate(nullptr, text, attrs);
    CTLineRef line = as ? CTLineCreateWithAttributedString(as) : nullptr;
    if (as) CFRelease(as);
    CFRelease(attrs);
    CFRelease(text);
    return line;
  }

  CGFontRef faces_[3] = {nullptr, nullptr, nullptr};
  std::map<std::pair<int, int>, CTFontRef> fonts_;
};

}  // namespace

std::unique_ptr<Fonts> makeFonts() { return std::make_unique<CoreTextFonts>(); }

}  // namespace marigold::text
