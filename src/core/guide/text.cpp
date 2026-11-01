#include "core/guide/text.hpp"

#include <cstring>
#include <new>

namespace marigold::guide {

// The bytes follow the count in the same block.
Text::Text(std::string_view text) {
  if (text.empty()) return;
  void* block = ::operator new(sizeof(Rep) + text.size());
  rep_ = new (block) Rep{{1}, static_cast<uint32_t>(text.size())};
  std::memcpy(reinterpret_cast<char*>(rep_ + 1), text.data(), text.size());
}

Text& Text::operator=(const Text& o) {
  if (rep_ != o.rep_) {
    o.retain();
    release();
    rep_ = o.rep_;
  }
  return *this;
}

Text& Text::operator=(Text&& o) noexcept {
  if (this != &o) {
    release();
    rep_ = o.rep_;
    o.rep_ = nullptr;
  }
  return *this;
}

void Text::release() {
  if (rep_ && rep_->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    rep_->~Rep();
    ::operator delete(rep_);
  }
  rep_ = nullptr;
}

std::string_view Text::view() const {
  return rep_ ? std::string_view(reinterpret_cast<const char*>(rep_ + 1), rep_->size) : std::string_view();
}

std::optional<std::string> Text::str() const {
  if (!rep_) return std::nullopt;
  return std::string(view());
}

Text TextPool::intern(std::string_view text) {
  if (text.empty()) return Text();
  auto found = texts_.find(text);
  if (found != texts_.end()) return found->second;
  Text made(text);
  texts_.emplace(made.view(), made);
  return made;
}

}  // namespace marigold::guide
