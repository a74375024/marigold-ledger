#include "platform/text.hpp"

#include <cmath>
#include <cstring>
#include <vector>

#include "core/util/log.hpp"

namespace marigold::text {
namespace {

constexpr const char* kEllipsis = "\xe2\x80\xa6";

std::string cacheKey(const std::string& s, Weight w, int px) {
  std::string key = std::to_string(static_cast<int>(w)) + ":" + std::to_string(px) + ":";
  key += s;
  return key;
}

// "Word…" reads better than "Word …" or "Word,…".
std::string trimRight(std::string s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == ',' || s.back() == '-')) s.pop_back();
  return s;
}

}  // namespace

TextRenderer::~TextRenderer() { shutdown(); }

bool TextRenderer::init(const std::string& fontDir) {
  ready_ = fonts_ && fonts_->init(fontDir);
  return ready_;
}

void TextRenderer::shutdown() {
  for (auto& [key, l] : lines_) {
    gfx::releaseTexture(l.texture);
  }
  lines_.clear();
  widths_.clear();
  lru_.clear();
  bytes_ = 0;
  if (ready_) fonts_->shutdown();
  ready_ = false;
}

const TextRenderer::Line* TextRenderer::line(const std::string& s, Weight w, int px) {
  if (s.empty() || !ready_) return nullptr;
  std::string key = cacheKey(s, w, px);
  auto it = lines_.find(key);
  if (it != lines_.end()) {
    lru_.splice(lru_.begin(), lru_, it->second.lru);
    return &it->second;
  }
  px::Image im;
  if (!fonts_->render(s, w, px, im) || im.empty()) return nullptr;

  Line l;
  l.w = im.w;
  l.h = im.h;
  l.bytes = static_cast<size_t>(l.w) * static_cast<size_t>(l.h) * 4u;
  l.texture = gfx::uploadTexture(im);

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
      gfx::releaseTexture(it->second.texture);
      bytes_ -= it->second.bytes;
      lines_.erase(it);
    }
    lru_.pop_back();
  }
}

float TextRenderer::measure(const std::string& s, Weight w, int px) {
  if (s.empty()) return 0.0f;
  std::string key = cacheKey(s, w, px);
  auto it = widths_.find(key);
  if (it != widths_.end()) return it->second;
  int width = 0;
  if (!ready_ || !fonts_->measure(s, w, px, width)) return 0.0f;
  if (widths_.size() >= 8192) widths_.clear();
  widths_.emplace(std::move(key), static_cast<float>(width));
  return static_cast<float>(width);
}

float TextRenderer::lineHeight(Weight w, int px) {
  float h = ready_ ? fonts_->lineHeight(w, px) : 0.0f;
  return h > 0 ? h : static_cast<float>(px) * 1.2f;
}

std::string TextRenderer::fitted(const std::string& s, Weight w, int px, float maxWidth) {
  if (maxWidth <= 0 || measure(s, w, px) <= maxWidth) return s;
  // Binary search over code point boundaries for the longest prefix that
  // fits with the ellipsis (results are cached by measure()).
  std::vector<size_t> cuts;
  for (size_t i = 0; i < s.size(); ++i) {
    if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80 && i > 0) cuts.push_back(i);
  }
  size_t lo = 0, hi = cuts.size();
  std::string best = kEllipsis;
  while (lo < hi) {
    size_t mid = (lo + hi + 1) / 2;
    std::string candidate(trimRight(s.substr(0, cuts[mid - 1])));
    candidate += kEllipsis;
    if (measure(candidate, w, px) <= maxWidth) {
      best = std::move(candidate);
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }
  return best;
}

std::vector<std::string> TextRenderer::wrap(const std::string& s, Weight w, int px,
                                            float maxWidth, int maxLines) {
  std::vector<std::string> lines;
  if (s.empty() || maxLines <= 0) return lines;
  std::vector<std::string> words;
  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\t')) ++i;
    size_t j = i;
    while (j < s.size() && s[j] != ' ' && s[j] != '\n' && s[j] != '\t') ++j;
    if (j > i) words.push_back(s.substr(i, j - i));
    i = j;
  }
  std::string current;
  size_t k = 0;
  for (; k < words.size(); ++k) {
    std::string candidate = current.empty() ? words[k] : current + " " + words[k];
    if (current.empty() || measure(candidate, w, px) <= maxWidth) {
      current = std::move(candidate);
      continue;
    }
    if (static_cast<int>(lines.size()) + 1 == maxLines) break;
    lines.push_back(std::move(current));
    current = words[k];
  }
  if (k < words.size()) {
    // Out of lines: the rest goes on the last one, cut with an ellipsis.
    for (; k < words.size(); ++k) current += " " + words[k];
  }
  if (!current.empty()) lines.push_back(fitted(current, w, px, maxWidth));
  return lines;
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
