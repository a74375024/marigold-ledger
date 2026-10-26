#include "platform/text.hpp"

#include <GLES2/gl2.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <unistd.h>

#include <cmath>
#include <cstring>
#include <vector>

#include "core/util/log.hpp"

namespace marigold::text {
namespace {

constexpr const char* kSystemFallback = "/usr/share/fonts/LG_Display-Regular.ttf";
constexpr const char* kEllipsis = "\xe2\x80\xa6";

std::string cacheKey(const std::string& s, Weight w, int px) {
  std::string key = std::to_string(static_cast<int>(w)) + ":" + std::to_string(px) + ":";
  key += s;
  return key;
}

// Drops the last UTF-8 code point.
void popCodePoint(std::string& s) {
  while (!s.empty()) {
    unsigned char c = static_cast<unsigned char>(s.back());
    s.pop_back();
    if ((c & 0xC0) != 0x80) break;
  }
}

}  // namespace

TextRenderer::~TextRenderer() { shutdown(); }

bool TextRenderer::init(const std::string& fontDir) {
  if (TTF_Init() != 0) {
    MLOG_E("text", "TTF_Init: %s", TTF_GetError());
    return false;
  }
  const char* names[3] = {"InterDisplay-Regular.ttf", "InterDisplay-Medium.ttf",
                          "InterDisplay-Bold.ttf"};
  for (int i = 0; i < 3; ++i) {
    std::string path = fontDir + "/" + names[i];
    files_[i] = access(path.c_str(), R_OK) == 0 ? path : kSystemFallback;
    if (files_[i] == kSystemFallback) MLOG_W("text", "%s missing, using the TV font", names[i]);
  }
  ready_ = true;
  return true;
}

void TextRenderer::shutdown() {
  for (auto& [key, l] : lines_) {
    if (l.texture) glDeleteTextures(1, &l.texture);
  }
  lines_.clear();
  lru_.clear();
  bytes_ = 0;
  for (auto& [key, f] : fonts_) {
    if (f) TTF_CloseFont(f);
  }
  fonts_.clear();
  if (ready_) TTF_Quit();
  ready_ = false;
}

TTF_Font* TextRenderer::font(Weight w, int px) {
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

const TextRenderer::Line* TextRenderer::line(const std::string& s, Weight w, int px) {
  if (s.empty() || !ready_) return nullptr;
  std::string key = cacheKey(s, w, px);
  auto it = lines_.find(key);
  if (it != lines_.end()) {
    lru_.splice(lru_.begin(), lru_, it->second.lru);
    return &it->second;
  }
  TTF_Font* f = font(w, px);
  if (!f) return nullptr;
  SDL_Color white = {255, 255, 255, 255};
  SDL_Surface* rendered = TTF_RenderUTF8_Blended(f, s.c_str(), white);
  if (!rendered) return nullptr;
  // ABGR8888 is R,G,B,A in memory on little-endian ARM: GL_RGBA as is.
  SDL_Surface* rgba = SDL_ConvertSurfaceFormat(rendered, SDL_PIXELFORMAT_ABGR8888, 0);
  SDL_FreeSurface(rendered);
  if (!rgba) return nullptr;

  Line l;
  l.w = rgba->w;
  l.h = rgba->h;
  l.bytes = static_cast<size_t>(l.w) * static_cast<size_t>(l.h) * 4u;
  std::vector<unsigned char> packed;
  const void* pixels = rgba->pixels;
  if (rgba->pitch != l.w * 4) {
    packed.resize(l.bytes);
    for (int y = 0; y < l.h; ++y) {
      std::memcpy(&packed[static_cast<size_t>(y) * static_cast<size_t>(l.w) * 4u],
                  static_cast<const unsigned char*>(rgba->pixels) + y * rgba->pitch,
                  static_cast<size_t>(l.w) * 4u);
    }
    pixels = packed.data();
  }
  glGenTextures(1, &l.texture);
  glBindTexture(GL_TEXTURE_2D, l.texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, l.w, l.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  SDL_FreeSurface(rgba);

  evictTo(budget_ > l.bytes ? budget_ - l.bytes : 0);
  lru_.push_front(key);
  l.lru = lru_.begin();
  bytes_ += l.bytes;
  auto [pos, inserted] = lines_.emplace(std::move(key), l);
  (void)inserted;
  return &pos->second;
}

void TextRenderer::evictTo(size_t limit) {
  while (bytes_ > limit && !lru_.empty()) {
    auto it = lines_.find(lru_.back());
    if (it != lines_.end()) {
      if (it->second.texture) glDeleteTextures(1, &it->second.texture);
      bytes_ -= it->second.bytes;
      lines_.erase(it);
    }
    lru_.pop_back();
  }
}

float TextRenderer::measure(const std::string& s, Weight w, int px) {
  TTF_Font* f = s.empty() ? nullptr : font(w, px);
  int width = 0, height = 0;
  if (!f || TTF_SizeUTF8(f, s.c_str(), &width, &height) != 0) return 0.0f;
  return static_cast<float>(width);
}

std::string TextRenderer::fitted(const std::string& s, Weight w, int px, float maxWidth) {
  if (maxWidth <= 0 || measure(s, w, px) <= maxWidth) return s;
  std::string cut = s;
  while (!cut.empty()) {
    popCodePoint(cut);
    std::string candidate = cut + kEllipsis;
    if (measure(candidate, w, px) <= maxWidth) return candidate;
  }
  return kEllipsis;
}

float TextRenderer::draw(gfx::Renderer& g, const std::string& s, float x, float y, Weight w,
                         int px, gfx::Color c, float maxWidth, Align align) {
  std::string shown = fitted(s, w, px, maxWidth);
  const Line* l = line(shown, w, px);
  if (!l) return 0.0f;
  float width = static_cast<float>(l->w);
  if (align == Align::Center && maxWidth > 0) x += (maxWidth - width) * 0.5f;
  if (align == Align::Right && maxWidth > 0) x += maxWidth - width;
  // Whole pixels: a texture drawn at a fractional offset is resampled and blurs.
  x = std::round(x);
  y = std::round(y);
  g.textureAlpha(l->texture, {x, y, width, static_cast<float>(l->h)}, c);
  return width;
}

}  // namespace marigold::text
