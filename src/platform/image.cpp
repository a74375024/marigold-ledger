#include "platform/image.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <dlfcn.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "core/util/log.hpp"

namespace marigold::image {
namespace {

// Larger sources are refused before decoding (a logo never needs this).
constexpr long kMaxPixels = 4096L * 4096L;

struct WebP {
  using Info = int (*)(const uint8_t*, size_t, int*, int*);
  using Rgba = uint8_t* (*)(const uint8_t*, size_t, int*, int*);
  using Free = void (*)(void*);
  Info info = nullptr;
  Rgba rgba = nullptr;
  Free free = nullptr;
};

const WebP& webp() {
  static WebP lib;
  static std::once_flag once;
  std::call_once(once, [] {
    const char* names[] = {"libwebp.so.7", "libwebp.so.6", "libwebp.so", nullptr};
    void* h = nullptr;
    for (int i = 0; names[i] && !h; ++i) h = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
    if (!h) {
      MLOG_I("image", "libwebp not found; WebP logos are skipped");
      return;
    }
    // dlsym returns object pointers; the copy is the portable way to get a
    // function pointer out of one.
    auto sym = [h](const char* name, auto& fn) {
      void* p = dlsym(h, name);
      static_assert(sizeof(fn) == sizeof(p), "function pointer size");
      if (p) std::memcpy(&fn, &p, sizeof p);
    };
    sym("WebPGetInfo", lib.info);
    sym("WebPDecodeRGBA", lib.rgba);
    sym("WebPFree", lib.free);  // missing in old libwebp: free() is right there
    if (!lib.info || !lib.rgba) lib = WebP{};
  });
  return lib;
}

bool isWebP(std::string_view b) {
  return b.size() >= 12 && b.substr(0, 4) == "RIFF" && b.substr(8, 4) == "WEBP";
}

std::optional<px::Image> decodeWebP(std::string_view bytes) {
  const WebP& lib = webp();
  if (!lib.rgba) return std::nullopt;
  const auto* data = reinterpret_cast<const uint8_t*>(bytes.data());
  int w = 0, h = 0;
  if (!lib.info(data, bytes.size(), &w, &h) || w <= 0 || h <= 0 ||
      static_cast<long>(w) * h > kMaxPixels)
    return std::nullopt;
  uint8_t* out = lib.rgba(data, bytes.size(), &w, &h);
  if (!out) return std::nullopt;  // animated WebP: not worth a demuxer for logos
  px::Image im;
  im.w = w;
  im.h = h;
  im.rgba.assign(out, out + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
  if (lib.free) lib.free(out);
  else std::free(out);
  return im;
}

// The TV's SDL2_image loads libpng/libjpeg lazily with unguarded counters;
// one decode at a time keeps that safe. Logos are small, so it costs little.
std::mutex gSdlDecode;

std::optional<px::Image> decodeSdl(std::string_view bytes) {
  std::lock_guard<std::mutex> lock(gSdlDecode);
  SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
  if (!rw) return std::nullopt;
  SDL_Surface* s = IMG_Load_RW(rw, 1);
  if (!s) return std::nullopt;
  if (static_cast<long>(s->w) * s->h > kMaxPixels) {
    SDL_FreeSurface(s);
    return std::nullopt;
  }
  // ABGR8888 is R,G,B,A in memory on little-endian ARM.
  SDL_Surface* c = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ABGR8888, 0);
  SDL_FreeSurface(s);
  if (!c) return std::nullopt;
  px::Image im;
  im.w = c->w;
  im.h = c->h;
  im.rgba.resize(static_cast<size_t>(c->w) * static_cast<size_t>(c->h) * 4);
  SDL_LockSurface(c);
  const auto* src = static_cast<const uint8_t*>(c->pixels);
  for (int y = 0; y < c->h; ++y)
    std::memcpy(&im.rgba[static_cast<size_t>(y) * static_cast<size_t>(c->w) * 4],
                src + static_cast<size_t>(y) * static_cast<size_t>(c->pitch),
                static_cast<size_t>(c->w) * 4);
  SDL_UnlockSurface(c);
  SDL_FreeSurface(c);
  return im;
}

}  // namespace

std::optional<px::Image> decode(std::string_view bytes, int maxW, int maxH) {
  if (bytes.size() < 8 || bytes.size() > (16u << 20)) return std::nullopt;
  std::optional<px::Image> im = isWebP(bytes) ? decodeWebP(bytes) : decodeSdl(bytes);
  if (!im) return std::nullopt;
  px::Image out = px::shrinkToFit(px::trimTransparent(std::move(*im)), maxW, maxH);
  if (out.empty()) return std::nullopt;
  return out;
}

}  // namespace marigold::image
