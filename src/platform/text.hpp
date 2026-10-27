// Text: each (string, weight, size) is rasterized once by the platform's font
// engine (Fonts: SDL_ttf on webOS, the system's text drawing on Android),
// uploaded as a texture and kept in an LRU cache with a memory budget. The
// cache, measuring, fitting and wrapping are the same everywhere.
#pragma once

#include <cstddef>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "core/util/pixels.hpp"
#include "platform/gfx.hpp"

namespace marigold::text {

enum class Weight { Regular, Medium, Bold };
enum class Align { Left, Center, Right };

// The font engine underneath: Inter Display in three weights, any size.
class Fonts {
 public:
  virtual ~Fonts() = default;
  // `fontDir` holds InterDisplay-{Regular,Medium,Bold}.ttf.
  virtual bool init(const std::string& fontDir) = 0;
  virtual void shutdown() = 0;
  // Width in pixels of one line; false when it can't be measured.
  virtual bool measure(const std::string& s, Weight w, int px, int& width) = 0;
  // A drawn line's height (ascent plus descent); 0 when unknown.
  virtual float lineHeight(Weight w, int px) = 0;
  // One line, white on transparent, as tall as lineHeight().
  virtual bool render(const std::string& s, Weight w, int px, px::Image& out) = 0;
};
// The platform's engine.
std::unique_ptr<Fonts> makeFonts();

class TextRenderer {
 public:
  explicit TextRenderer(size_t budgetBytes = 24u << 20) : fonts_(makeFonts()), budget_(budgetBytes) {}
  ~TextRenderer();
  TextRenderer(const TextRenderer&) = delete;
  TextRenderer& operator=(const TextRenderer&) = delete;

  // `fontDir` holds InterDisplay-{Regular,Medium,Bold}.ttf. On webOS a
  // missing file falls back to the TV's LG Display font.
  bool init(const std::string& fontDir);
  void shutdown();

  // Width in layout units (cached).
  float measure(const std::string& s, Weight w, int px);
  // Height of a drawn line (the font's ascent plus descent).
  float lineHeight(Weight w, int px);
  // Word-wraps into at most `maxLines` lines; the last one ends with an
  // ellipsis when text is left over.
  std::vector<std::string> wrap(const std::string& s, Weight w, int px, float maxWidth,
                                int maxLines);
  // Draws one line with its top at `y`. If `maxWidth` > 0 the text is cut
  // with an ellipsis to fit. Returns the drawn width.
  float draw(gfx::Renderer& g, const std::string& s, float x, float y, Weight w, int px,
             gfx::Color c, float maxWidth = 0, Align align = Align::Left);

  size_t cachedBytes() const { return bytes_; }

 private:
  struct Line {
    unsigned texture = 0;
    int w = 0, h = 0;
    size_t bytes = 0;
    std::list<std::string>::iterator lru;
  };
  const Line* line(const std::string& s, Weight w, int px);
  std::string fitted(const std::string& s, Weight w, int px, float maxWidth);
  void evictTo(size_t bytes);

  std::unique_ptr<Fonts> fonts_;
  std::unordered_map<std::string, Line> lines_;
  std::unordered_map<std::string, float> widths_;  // measure cache, cleared when full
  std::list<std::string> lru_;
  size_t budget_;
  size_t bytes_ = 0;
  bool ready_ = false;
};

}  // namespace marigold::text
