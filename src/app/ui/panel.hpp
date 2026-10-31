// A list of panel rows (LiveSidePanel + panel rows in tvOS): section
// headers, actions, chevrons with a value, toggles, checkmarks and counts.
// Used by Settings, Filter, the time menu, Sources and the long-press menus.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "app/ui/ui.hpp"
#include "core/input/keys.hpp"

namespace marigold::ui {

struct PanelItem {
  // Choice: a segmented control on the right ("East | West | Both"); Left /
  // Right (or OK, to the next) picks.
  enum class Kind { Header, Action, Chevron, Toggle, Check, Count, Note, Choice };
  Kind kind = Kind::Action;
  std::string title;
  std::string detail;  // second line, smaller
  std::string value;   // right side text (Chevron, Count)
  bool on = false;     // Toggle and Check state
  bool enabled = true;
  bool dot = false;    // red dot before the title (active filter, origin)
  std::function<void()> select;
  std::function<void()> hold;  // OK held (optional)
  std::vector<std::string> choices;  // Choice
  int choice = -1;                   // -1: none of them yet
  std::function<void(int)> pick;
  // A Choice with too many choices to show at once: only the one picked,
  // between ‹ and › (Left / Right step, no wrapping).
  bool stepper = false;
  int coast = -1;  // an EAST (0) or WEST (1) tag after the title
  std::string tag;  // a small blue tag after the title ("NEW")
  // A row that can't be used is dimmed (never hidden, so rows stay put)
  // with why on its second line ("Turn on All Channels tab").
  std::string why;
  // All settings' right column: what the row does (paragraphs split by
  // '\n'), and a picture over it ("captions": a caption in the chosen style).
  std::string about;
  std::string preview;

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
  // Pointer: id from the hit regions registered by draw(). A Choice row's
  // choices are targets of their own, so a click picks the one under it.
  void hover(int id);
  // The pointer's wheel: scrolls without moving the focus (until a key).
  void scrollBy(float dy);

  // Draws inside `r` (the panel's own background is drawn by the caller).
  // `hitBase` offsets the pointer ids this panel registers.
  // `showFocus` false draws the focused row like the others (focus is elsewhere).
  void draw(Ctx& c, Rect r, int hitBase = 1000, bool showFocus = true);

  static constexpr int kHitBase = 1000;
  // Choice k of row i: kHitBase + kChoiceHits + i * 8 + k.
  static constexpr int kChoiceHits = 100000;

 private:
  float rowHeight(Ctx& c, const PanelItem& it, float width) const;
  int focus_ = -1;
  float scroll_ = 0;
  bool followFocus_ = true;  // scrolled to keep the focus in view
  int hoveredChoice_ = -1;   // the choice under the pointer on the focused row
  float maxScroll_ = 0;
};

}  // namespace marigold::ui
