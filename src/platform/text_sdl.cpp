// webOS: the font engine is the TV's SDL_ttf.
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <unistd.h>

#include <cstring>
#include <map>
#include <utility>

#include "core/util/log.hpp"
#include "platform/text.hpp"

namespace marigold::text {
namespace {

constexpr const char* kSystemFallback = "/usr/share/fonts/LG_Display-Regular.ttf";

class SdlFonts final : public Fonts {
 public:
  bool init(const std::string& fontDir) override {
    if (TTF_Init() != 0) {
      MLOG_E("text", "TTF_Init: %s", TTF_GetError());
      return false;
    }
    const char* names[3] = {"InterDisplay-Regular.ttf", "InterDisplay-Medium.ttf", "InterDisplay-Bold.ttf"};
    for (int i = 0; i < 3; ++i) {
      std::string path = fontDir + "/" + names[i];
      files_[i] = access(path.c_str(), R_OK) == 0 ? path : kSystemFallback;
      if (files_[i] == kSystemFallback) MLOG_W("text", "%s missing, using the TV font", names[i]);
    }
    return true;
  }

  void shutdown() override {
    for (auto& [key, f] : fonts_) {
      if (f) TTF_CloseFont(f);
    }
    fonts_.clear();
    TTF_Quit();
  }

  bool measure(const std::string& s, Weight w, int px, int& width) override {
    TTF_Font* f = font(w, px);
    int height = 0;
    return f && TTF_SizeUTF8(f, s.c_str(), &width, &height) == 0;
  }

  float lineHeight(Weight w, int px) override {
    TTF_Font* f = font(w, px);
    return f ? static_cast<float>(TTF_FontHeight(f)) : 0.0f;
  }

  bool render(const std::string& s, Weight w, int px, px::Image& out) override {
    TTF_Font* f = font(w, px);
    if (!f) return false;
    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface* rendered = TTF_RenderUTF8_Blended(f, s.c_str(), white);
    if (!rendered) return false;
    // ABGR8888 is R,G,B,A in memory on little-endian ARM: GL_RGBA as is.
    SDL_Surface* rgba = SDL_ConvertSurfaceFormat(rendered, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(rendered);
    if (!rgba) return false;
    out.w = rgba->w;
    out.h = rgba->h;
    size_t row = static_cast<size_t>(out.w) * 4u;
    out.rgba.resize(row * static_cast<size_t>(out.h));
    for (int y = 0; y < out.h; ++y) {
      std::memcpy(&out.rgba[static_cast<size_t>(y) * row],
                  static_cast<const unsigned char*>(rgba->pixels) + y * rgba->pitch, row);
    }
    SDL_FreeSurface(rgba);
    return true;
  }

 private:
  TTF_Font* font(Weight w, int px) {
    auto key = std::make_pair(static_cast<int>(w), px);
    auto it = fonts_.find(key);
    if (it != fonts_.end()) return it->second;
    const std::string& path = files_[static_cast<int>(w)];
    TTF_Font* f = TTF_OpenFont(path.c_str(), px);
    if (!f && path != kSystemFallback) f = TTF_OpenFont(kSystemFallback, px);
    if (!f) MLOG_E("text", "TTF_OpenFont %d px: %s", px, TTF_GetError());
    fonts_[key] = f;
    return f;
  }

  std::string files_[3];
  std::map<std::pair<int, int>, TTF_Font*> fonts_;
};

}  // namespace

std::unique_ptr<Fonts> makeFonts() { return std::make_unique<SdlFonts>(); }

}  // namespace marigold::text
