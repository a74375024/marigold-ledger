// A programme description held compactly: one block (a count and the bytes)
// that every copy of the programme points at, so a West feed made from the
// East one, a merged list, the saved and live listings and the guide's
// programmes and listings made from them (Program and Listing::overview,
// Build 119) hold each description once. TextPool hands out one block per distinct text, so a
// rerun's description is also held once. The text never changes; copies may
// be made and dropped on any thread.
#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace marigold::guide {

class Text {
 public:
  Text() = default;
  Text(std::nullopt_t) {}  // NOLINT: none, as for an optional string
  Text(std::string_view text);  // NOLINT: implicit on purpose
  Text(const char* text) : Text(std::string_view(text)) {}
  Text(const std::string& text) : Text(std::string_view(text)) {}
  Text(const std::optional<std::string>& text) : Text(text ? std::string_view(*text) : std::string_view()) {}  // NOLINT
  Text(const Text& o) : rep_(o.rep_) { retain(); }
  Text(Text&& o) noexcept : rep_(o.rep_) { o.rep_ = nullptr; }
  Text& operator=(const Text& o);
  Text& operator=(Text&& o) noexcept;
  ~Text() { release(); }

  explicit operator bool() const { return rep_ != nullptr; }
  std::string_view view() const;
  size_t size() const { return rep_ ? rep_->size : 0; }
  bool empty() const { return rep_ == nullptr; }  // an empty text is none
  // For code that takes a description as a string, or an optional one.
  std::string string() const { return std::string(view()); }
  std::optional<std::string> str() const;
  operator std::optional<std::string>() const { return str(); }  // NOLINT

  bool operator==(const Text& o) const { return rep_ == o.rep_ || (rep_ && o.rep_ && view() == o.view()); }
  bool operator!=(const Text& o) const { return !(*this == o); }
  friend bool operator==(const Text& t, std::string_view s) { return t && t.view() == s; }
  friend bool operator==(const Text& t, const std::string& s) { return t == std::string_view(s); }
  friend bool operator==(const Text& t, const char* s) { return t == std::string_view(s); }
  template <typename S>
  friend bool operator!=(const Text& t, const S& s) { return !(t == s); }

 private:
  struct Rep {
    std::atomic<uint32_t> refs;
    uint32_t size;
  };
  void retain() const {
    if (rep_) rep_->refs.fetch_add(1, std::memory_order_relaxed);
  }
  void release();
  Rep* rep_ = nullptr;
};

// One Text per distinct text, while the pool lives (a guide file's read).
class TextPool {
 public:
  Text intern(std::string_view text);
  void clear() { texts_ = {}; }

 private:
  std::unordered_map<std::string_view, Text> texts_;  // keys view their Text
};

}  // namespace marigold::guide
