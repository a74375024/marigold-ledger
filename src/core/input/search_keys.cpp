#include "core/input/search_keys.hpp"

namespace marigold::search_keys {

namespace {
constexpr const char kKeys[] = "abcdefghijklmnopqrstuvwxyz1234567890";
}  // namespace

char Grid::keyAt(int row, int col) {
  if (row < 0 || row >= kRows || col < 0 || col >= kCols) return 0;
  return kKeys[row * kCols + col];
}

Grid::Spot Grid::keyFor(char c) {
  for (int i = 0; i < kRows * kCols; ++i)
    if (kKeys[i] == c) return {Spot::Area::Key, i / kCols, i % kCols};
  return {};
}

void Grid::setSpeak(bool on) {
  speak_ = on;
  if (!on && at_.area == Spot::Area::Speak) at_ = Spot{};
}

std::string Grid::typed() const {
  if (at_.area != Spot::Area::Key) return {};
  char c = keyAt(at_.row, at_.col);
  return c ? std::string(1, c) : std::string();
}

Grid::Move Grid::move(Key k, uint32_t glide) {
  if (glide_.blocked(glide)) return {};
  Spot next = at_;
  Edge edge = Edge::None;
  using A = Spot::Area;
  switch (at_.area) {
    case A::Speak:
      // Speak sits right of the field: Down to the grid's last column,
      // Right to the results.
      if (k == Key::Down) next = {A::Key, 0, kCols - 1};
      else if (k == Key::Right) edge = Edge::Right;
      else if (k == Key::Up) edge = Edge::Up;
      else if (k == Key::Left) edge = Edge::Left;
      break;
    case A::Key:
      if (k == Key::Left) {
        if (at_.col == 0) edge = Edge::Left;
        else --next.col;
      } else if (k == Key::Right) {
        if (at_.col == kCols - 1) edge = Edge::Right;
        else ++next.col;
      } else if (k == Key::Up) {
        if (at_.row > 0) --next.row;
        else if (speak_) next = {A::Speak, 0, 0};
        else edge = Edge::Up;
      } else if (k == Key::Down) {
        if (at_.row < kRows - 1) ++next.row;
        else next = {A::Bottom, 0, at_.col / 2};
      }
      break;
    case A::Bottom:
      if (k == Key::Left) {
        if (at_.col == 0) edge = Edge::Left;
        else --next.col;
      } else if (k == Key::Right) {
        if (at_.col == 2) edge = Edge::Right;
        else ++next.col;
      } else if (k == Key::Up) {
        // Back to the column it came down from when it is over this key.
        int col = lastCol_ / 2 == at_.col ? lastCol_ : at_.col * 2;
        next = {A::Key, kRows - 1, col};
      } else if (k == Key::Down) {
        edge = Edge::Down;
      }
      break;
  }
  if (edge != Edge::None) {
    // A swipe stops at the edge; the next gesture crosses.
    if (glide_.stop(glide)) return {};
    return {false, edge};
  }
  if (next == at_) return {};
  glide_.moved(glide);
  if (at_.area == A::Key) lastCol_ = at_.col;
  at_ = next;
  return {true, Edge::None};
}

}  // namespace marigold::search_keys
