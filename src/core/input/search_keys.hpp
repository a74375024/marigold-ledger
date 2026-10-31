// Search's own keyboard (Build 113), the same on every TV: a 6x6
// alphabetical grid (a-z, then 1-9 and 0), ⌫ Delete · Space · Clear under
// it (Clear kept away from ⌫), and 🎤 Speak above it where dictation
// works. Where a key press goes is decided here so it can be tested
// without a screen; the screen draws the grid and does what a Move says.
//
// A swipe on the Siri Remote moves several keys in one gesture (KeyEvent::
// glide): it stops at the grid's edge, and only a new gesture (another
// glide, or a ring press) crosses to the results or the menu. The results
// list stops a glide at its edges the same way (GlideStop).
#pragma once

#include <cstdint>
#include <string>

#include "core/input/keys.hpp"

namespace marigold::search_keys {

constexpr int kCols = 6;
constexpr int kRows = 6;
// The bottom row's three wide keys, two columns each.
enum class Wide { Delete, Space, Clear };

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
  // Where the focus is: Speak (above the grid), a key, or a wide key.
  struct Spot {
    enum class Area { Speak, Key, Bottom } area = Area::Key;
    int row = 0, col = 0;  // Key: 0-5 each; Bottom: col 0-2 (Wide)
    bool operator==(const Spot& o) const { return area == o.area && row == o.row && col == o.col; }
  };
  // Where a move went: within the grid, or out of one side.
  enum class Edge { None, Left, Right, Up, Down };
  struct Move {
    bool moved = false;
    Edge edge = Edge::None;  // the focus leaves the grid this way
  };

  explicit Grid(bool speak = false) : speak_(speak) {}
  void setSpeak(bool on);
  bool speak() const { return speak_; }

  // An arrow. `glide`: KeyEvent::glide (0 for a press).
  Move move(Key k, uint32_t glide = 0);
  const Spot& at() const { return at_; }
  void set(const Spot& s) { at_ = s; }
  void home() { at_ = Spot{}; }  // "a"

  // What OK types on a key: "a", "1"; "" on Speak and the wide keys.
  std::string typed() const;
  static char keyAt(int row, int col);
  // The key that types `c` (a letter or a digit), for the pointer and tests.
  static Spot keyFor(char c);

 private:
  bool speak_;
  Spot at_;
  int lastCol_ = 0;  // the column the focus came down from (the wide keys)
  GlideStop glide_;
};

}  // namespace marigold::search_keys
