#include "core/input/search_keys.hpp"

namespace marigold::search_keys {

namespace {
// Row by row; ' ' is Space (the four cells after z, the keypad's last row).
constexpr const char kLetters[] = "abcdefghijklmnopqrstuvwxyz    ";
constexpr const char kPad[] = "123456789&0'   ";
}  // namespace

char Grid::keyAt(Page page, int row, int col) {
  int cols = colsOf(page);
  if (row < 0 || row >= kRows || col < 0 || col >= cols) return 0;
  return (page == Page::Letters ? kLetters : kPad)[row * cols + col];
}

Grid::Spot Grid::keyFor(Page page, char c) {
  int cols = colsOf(page);
  for (int i = 0; i < kRows * cols; ++i) {
    if (keyAt(page, i / cols, i % cols) != c) continue;
    Grid g;
    g.page_ = page;
    return g.spotAt(i / cols, i % cols);
  }
  return {};
}

// The key over a cell: Space's cells are one key, named by its first.
Grid::Spot Grid::spotAt(int row, int col) const {
  if (keyAt(page_, row, col) == ' ') {
    int c = col;
    while (c > 0 && keyAt(page_, row, c - 1) == ' ') --c;
    return {Spot::Area::Key, row, c};
  }
  return {Spot::Area::Key, row, col};
}

void Grid::setSpeak(bool on) {
  speak_ = on;
  if (!on && at_.area == Spot::Area::Speak) home();
}

void Grid::set(const Spot& s) {
  at_ = s;
  if (s.area == Spot::Area::Key) col_ = s.col;
  else if (s.area == Spot::Area::Bottom) col_ = page_ == Page::Letters ? s.col * 2 : s.col;
}

void Grid::home() {
  page_ = Page::Letters;
  at_ = Spot{};
  col_ = 0;
}

void Grid::flip() {
  // The keypad's columns sit under the letters' pairs of columns.
  bool toPad = page_ == Page::Letters;
  page_ = toPad ? Page::Digits : Page::Letters;
  col_ = toPad ? col_ / 2 : col_ * 2;
  if (at_.area == Spot::Area::Key) at_ = spotAt(at_.row, col_);
}

std::string Grid::typed() const {
  if (at_.area != Spot::Area::Key) return {};
  char c = keyAt(page_, at_.row, at_.col);
  return c ? std::string(1, c) : std::string();
}

Grid::Move Grid::move(Key k, uint32_t glide) {
  if (glide_.blocked(glide)) return {};
  Spot next = at_;
  int col = col_;
  int cols = colsOf(page_);
  Edge edge = Edge::None;
  using A = Spot::Area;
  switch (at_.area) {
    case A::Speak:
      // Speak sits right of the field: Down to the page's last column,
      // Right to the results.
      if (k == Key::Down) {
        col = cols - 1;
        next = spotAt(0, col);
      } else if (k == Key::Right) {
        edge = Edge::Right;
      } else if (k == Key::Up) {
        edge = Edge::Up;
      } else if (k == Key::Left) {
        edge = Edge::Left;
      }
      break;
    case A::Key: {
      // The key's last column (Space spans several).
      int end = at_.col;
      while (end + 1 < cols && keyAt(page_, at_.row, end + 1) == ' ' && keyAt(page_, at_.row, at_.col) == ' ') ++end;
      if (k == Key::Left) {
        if (at_.col == 0) edge = Edge::Left;
        else next = spotAt(at_.row, col = at_.col - 1);
      } else if (k == Key::Right) {
        if (end == cols - 1) edge = Edge::Right;
        else next = spotAt(at_.row, col = end + 1);
      } else if (k == Key::Up) {
        if (at_.row > 0) next = spotAt(at_.row - 1, col);
        else if (speak_) next = {A::Speak, 0, 0};
        else edge = Edge::Up;
      } else if (k == Key::Down) {
        if (at_.row < kRows - 1) next = spotAt(at_.row + 1, col);
        else next = {A::Bottom, 0, page_ == Page::Letters ? col / 2 : col};
      }
      break;
    }
    case A::Bottom:
      if (k == Key::Left) {
        if (at_.col == 0) edge = Edge::Left;
        else --next.col;
      } else if (k == Key::Right) {
        if (at_.col == 2) edge = Edge::Right;
        else ++next.col;
      } else if (k == Key::Up) {
        // Back to the column it came down from when it is under this key.
        if (page_ == Page::Letters) col = col / 2 == at_.col ? col : at_.col * 2;
        else col = at_.col;
        next = spotAt(kRows - 1, col);
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
  at_ = next;
  col_ = col;
  return {true, Edge::None};
}

int targetOf(Page page, const Grid::Spot& spot) {
  switch (spot.area) {
    case Grid::Spot::Area::Speak: return -1;
    case Grid::Spot::Area::Bottom: return kTargetWide + spot.col;
    case Grid::Spot::Area::Key: break;
  }
  int cols = colsOf(page);
  return (page == Page::Letters ? 0 : kTargetPad) + spot.row * cols + spot.col;
}

bool spotOfTarget(Page page, int target, Grid::Spot& spot) {
  if (target >= kTargetWide && target < kTargets) {
    spot = {Grid::Spot::Area::Bottom, 0, target - kTargetWide};
    return true;
  }
  int cols = colsOf(page);
  int k = target - (page == Page::Letters ? 0 : kTargetPad);
  if (k < 0 || k >= kRows * cols || target >= kTargetWide) return false;
  int row = k / cols, col = k % cols;
  // Space's cells are one key, named by its first.
  char c = Grid::keyAt(page, row, col);
  if (c == ' ' && col > 0 && Grid::keyAt(page, row, col - 1) == ' ') return false;
  spot = {Grid::Spot::Area::Key, row, col};
  return true;
}

}  // namespace marigold::search_keys
