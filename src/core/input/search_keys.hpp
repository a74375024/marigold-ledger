// Search's own keyboard (Build 115), the same on every TV: two pages over
// one bottom row, ⌫ Delete · the mode key · Clear (Clear kept away from ⌫),
// and 🎤 Speak above them where dictation works.
//
//   letters  a 6x5 alphabetical grid: a-z, then Space in the four cells
//            after z. The mode key says "123".
//   123      a phone keypad (1 2 3 / 4 5 6 / 7 8 9 / & 0 ') and Space
//            across its row. The mode key says "abc".
//
// OK on the mode key flips the page and keeps the focus on it (a second OK
// flips back); Apple TV's Play/Pause flips it too. The page stays on 123
// after a digit and goes back to letters on Clear and when Search closes
// (the screen's rules). Where a key press goes is decided here so it can
// be tested without a screen; the screen draws the page and does what a
// Move says.
//
// A swipe on the Siri Remote moves several keys in one gesture (KeyEvent::
// glide): it stops at the page's edge (on 123, the keypad's), and only a
// new gesture (another glide, or a ring press) crosses to the results or
// the menu. The results list stops a glide at its edges the same way
// (GlideStop).
#pragma once

#include <cstdint>
#include <string>

#include "core/input/keys.hpp"

namespace marigold::search_keys {

enum class Page { Letters, Digits };
// Both pages have five rows over the bottom row; the letters six columns,
// the keypad three.
constexpr int kRows = 5;
constexpr int kLetterCols = 6;
constexpr int kPadCols = 3;
inline int colsOf(Page p) { return p == Page::Letters ? kLetterCols : kPadCols; }
// The bottom row's three wide keys.
enum class Wide { Delete, Mode, Clear };

// A swipe that reached an edge stops there: its later moves are dropped.
// A new gesture, or a ring press (glide 0), crosses.
class GlideStop {
 public:
  // This move belongs to a gesture that already stopped.
  bool blocked(uint32_t glide) const { return glide != 0 && glide == stopped_; }
  // A move inside the area.
  void moved(uint32_t glide) { moving_ = glide; }
  // The move would leave the area. A swipe that got here by moving inside
  // it stops (true: drop the move).
  bool stop(uint32_t glide) {
    if (glide == 0 || glide != moving_) return false;
    stopped_ = glide;
    return true;
  }

 private:
  uint32_t moving_ = 0, stopped_ = 0;
};

class Grid {
 public:
  // Where the focus is: Speak (above the keys), a key, or a wide key.
  struct Spot {
    enum class Area { Speak, Key, Bottom } area = Area::Key;
    // Key: row 0-4 and the key's first column (Space starts at 2 on the
    // letters, 0 on 123); Bottom: col 0-2 (Wide).
    int row = 0, col = 0;
    bool operator==(const Spot& o) const { return area == o.area && row == o.row && col == o.col; }
  };
  // Where a move went: within the keys, or out of one side.
  enum class Edge { None, Left, Right, Up, Down };
  struct Move {
    bool moved = false;
    Edge edge = Edge::None;  // the focus leaves the keyboard this way
  };

  explicit Grid(bool speak = false) : speak_(speak) {}
  void setSpeak(bool on);
  bool speak() const { return speak_; }

  Page page() const { return page_; }
  // The other page (the mode key, Play/Pause). The focus stays on the mode
  // key, Space and the wide keys; on another key it goes to the one in its
  // place.
  void flip();
  // Back to the letters page (Clear, Search closing, an empty field).
  void letters() {
    if (page_ != Page::Letters) flip();
  }

  // An arrow. `glide`: KeyEvent::glide (0 for a press).
  Move move(Key k, uint32_t glide = 0);
  const Spot& at() const { return at_; }
  void set(const Spot& s);
  void home();  // "a" on the letters page
  bool onMode() const { return at_.area == Spot::Area::Bottom && at_.col == static_cast<int>(Wide::Mode); }

  // What OK types on a key: "a", "1", "&", " " on Space; "" on Speak and
  // the wide keys.
  std::string typed() const;
  // The key of a page at a cell (' ' for Space's cells), 0 outside it.
  static char keyAt(Page page, int row, int col);
  // The key that types `c` on a page, for the pointer and tests.
  static Spot keyFor(Page page, char c);

 private:
  Spot spotAt(int row, int col) const;
  bool speak_;
  Page page_ = Page::Letters;
  Spot at_;
  int col_ = 0;  // the column the focus is in (under a wide key or Space, the one it came from)
  GlideStop glide_;
};

// Pointer targets, for the screen's hit ids: each key of a page once
// (Space's cells are one), numbered from 0: the letters by cell (row * 6 +
// col), the keypad's from 30 (row * 3 + col), the wide keys from 50. A
// target of the other page is none.
constexpr int kTargetPad = 30, kTargetWide = 50, kTargets = 53;
int targetOf(Page page, const Grid::Spot& spot);  // -1 for Speak
bool spotOfTarget(Page page, int target, Grid::Spot& spot);

}  // namespace marigold::search_keys
