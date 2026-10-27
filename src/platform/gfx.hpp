// The UI renderer, in 1920x1080 layout units: GLES2 on webOS and Android
// (gfx.cpp), Metal on tvOS (tvos/gfx_metal.mm), the same drawing on each.
//
// One quad, one program: filled rounded rectangles (signed distance with a
// one-pixel edge), alpha-tinted textures for text and icons, scissor clips and
// the "hole" that clears the surface to transparent so the hardware video
// plane behind it shows. Blending keeps the surface alpha premultiplied, which
// is what the webOS compositor uses to mix the UI over the video.
#pragma once

#include <cstdint>
#include <vector>

#include "core/util/pixels.hpp"

namespace marigold::gfx {

struct Rect {
  float x = 0, y = 0, w = 0, h = 0;
};

struct Color {
  float r = 0, g = 0, b = 0, a = 1;
  // 0xRRGGBB plus a separate alpha.
  static Color hex(uint32_t rgb, float alpha = 1.0f) {
    return {static_cast<float>((rgb >> 16) & 0xff) / 255.0f,
            static_cast<float>((rgb >> 8) & 0xff) / 255.0f,
            static_cast<float>(rgb & 0xff) / 255.0f, alpha};
  }
  Color withAlpha(float alpha) const { return {r, g, b, alpha}; }
};

constexpr float kLayoutWidth = 1920.0f;
constexpr float kLayoutHeight = 1080.0f;

class Renderer {
 public:
  bool init(int drawableWidth, int drawableHeight);
  void shutdown();

  void beginFrame(Color clear);
  void fill(Rect r, Color c, float radius = 0.0f);
  // A rounded outline `thickness` wide, drawn inside `r`.
  void stroke(Rect r, Color c, float radius, float thickness);
  // Clears `r` to transparent: the video plane shows through it.
  void hole(Rect r);
  // Draws a texture's alpha channel tinted with `c`.
  void textureAlpha(unsigned texture, Rect r, Color c);
  // Draws a colour (RGBA, straight alpha) texture with rounded corners.
  void image(unsigned texture, Rect r, float opacity = 1.0f, float radius = 0.0f);

  void pushClip(Rect r);
  void popClip();
  void clearClips();

  int drawableWidth() const { return drawableW_; }
  int drawableHeight() const { return drawableH_; }
  // Drawable pixels per layout unit (1 on every TV measured so far).
  float pixelScale() const { return static_cast<float>(drawableW_) / kLayoutWidth; }

 private:
  void draw(Rect r, Color c, float radius, float thickness, int mode, unsigned texture);
  void applyClip();

  struct Backend;  // the GPU side (GL program and buffer, or Metal state)
  Backend* gpu_ = nullptr;
  int drawableW_ = 1920, drawableH_ = 1080;
  std::vector<Rect> clips_;
};

// The renderer the vendored C code draws through (see gfx_c.cpp).
Renderer& renderer();

// Textures for text, icons and logos, by id (0: none): RGBA with straight
// alpha, linear filtering, clamped edges. Passing an existing id replaces
// its pixels. On the thread that draws.
unsigned uploadTexture(const px::Image& im, unsigned replace = 0);
// Frees the texture and sets `id` to 0.
void releaseTexture(unsigned& id);

}  // namespace marigold::gfx
