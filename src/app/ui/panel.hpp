// A list of panel rows (LiveSidePanel + panel rows in tvOS): section
// headers, actions, chevrons with a value, toggles, checkmarks and counts.
// Used by Options, Filter, the time menu, Sources and the long-press menus.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "app/ui/ui.hpp"
#include "core/input/keys.hpp"

namespace marigold::ui {

struct PanelItem {
  enum class Kind { Header, Action, Chevron, Toggle, Check, Count, Note };
  Kind kind = Kind::Action;
  std::string title;
  std::string detail;  // second line, smaller
  std::string value;   // right side text (Chevron, Count)
  bool on = false;     // Toggle and Check state
  bool enabled = true;
  bool dot = false;    // red dot before the title (active filter, origin)
  std::function<void()> select;

  PanelItem() = default;
  PanelItem(Kind k, std::string t) : kind(k), title(std::move(t)) {}
  static PanelItem header(std::string t) { return {Kind::Header, std::move(t)}; }
  static PanelItem note(std::string t) { return {Kind::Note, std::move(t)}; }
  bool focusable() const { return kind != Kind::Header && kind != Kind::Note && enabled; }
};

class ListPanel {
 public:
  std::string title;
  std::string subtitle;  // small caps line above the title ("FILTER · My Lineup")
  std::string hint;      // bottom line
  std::vector<PanelItem> items;

  // Keeps the focus on a focusable row (call after changing items).
  void settle();
  void focusFirst();
  void focusIndex(int i);
  int focused() const { return focus_; }
  // Up/Down/Ok. Returns true when handled.
  bool key(Key k);
  // Pointer: id from the hit regions registered by draw().
  void hover(int id);

  // Draws inside `r` (the panel's own background is drawn by the caller).
  // `hitBase` offsets the pointer ids this panel registers.
  void draw(Ctx& c, Rect r, int hitBase = 1000);

  static constexpr int kHitBase = 1000;

 private:
  float rowHeight(const PanelItem& it) const;
  int focus_ = -1;
  float scroll_ = 0;
};

}  // namespace marigold::ui
