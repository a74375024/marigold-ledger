// Text through SDL_ttf: each (string, weight, size) is rasterized once,
// uploaded as a texture and kept in an LRU cache with a memory budget.
#pragma once

#include <cstddef>
#include <list>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "platform/gfx.hpp"

// Same spelling as the SDK's SDL_ttf.h (2.24); the TV's older library has
// the same ABI for every call used here.
typedef struct TTF_Font TTF_Font;

namespace marigold::text {

enum class Weight { Regular, Medium, Bold };
enum class Align { Left, Center, Right };

class TextRenderer {
 public:
  explicit TextRenderer(size_t budgetBytes = 24u << 20) : budget_(budgetBytes) {}
  ~TextRenderer();
  TextRenderer(const TextRenderer&) = delete;
  TextRenderer& operator=(const TextRenderer&) = delete;

  // `fontDir` holds InterDisplay-{Regular,Medium,Bold}.ttf. Falls back to the
  // TV's LG Display font when a file is missing.
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
  TTF_Font* font(Weight w, int px);
  const Line* line(const std::string& s, Weight w, int px);
  std::string fitted(const std::string& s, Weight w, int px, float maxWidth);
  void evictTo(size_t bytes);

  std::string files_[3];
  std::map<std::pair<int, int>, TTF_Font*> fonts_;
  std::unordered_map<std::string, Line> lines_;
  std::unordered_map<std::string, float> widths_;  // measure cache, cleared when full
  std::list<std::string> lru_;
  size_t budget_;
  size_t bytes_ = 0;
  bool ready_ = false;
};

}  // namespace marigold::text
