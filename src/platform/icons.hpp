// The sport icons shipped with the app (icons/<name>.png, white on
// transparent; see tools/build-icons.mjs) as GL textures, drawn tinted with
// Renderer::textureAlpha. Each loads the first time it's asked for; there
// are a couple of dozen, about 14 KB of texture each.
#pragma once

#include <string>
#include <unordered_map>

namespace marigold::icons {

class IconSet {
 public:
  IconSet() = default;
  ~IconSet();  // GL thread: deletes the textures
  IconSet(const IconSet&) = delete;
  IconSet& operator=(const IconSet&) = delete;

  // `dir` holds the PNGs.
  void init(const std::string& dir) { dir_ = dir; }
  // UI thread. The texture for `name` ("golf"), or 0 when there is no such
  // icon (the caller draws its glyph instead).
  unsigned get(const std::string& name);

 private:
  std::string dir_;
  std::unordered_map<std::string, unsigned> textures_;  // 0: missing or unreadable
};

}  // namespace marigold::icons
