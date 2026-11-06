// A list of panel rows (LiveSidePanel + panel rows in tvOS): section
// headers, actions, chevrons with a value, toggles, checkmarks and counts.
// Used by Settings, Filter, the time menu, Sources and the long-press menus.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "app/ui/ui.hpp"
#include "core/input/keys.hpp"
#include "core/input/settings_nav.hpp"

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
  // Build 144, Settings' pages in place. The right column's small heading
  // instead of the title ("BACKUP TV"), and a heading over `about`
  // ("Membership ends March 3, 2027").
  std::string aboutLabel, aboutHeading;
  // The second line (a note's title) in amber: a membership's last week.
  bool warn = false;
  // What OK does, for the key hints ("Remove…", "Stop sharing");
  // empty: by kind ("Open" on a chevron). `next`: a ▼ hint while focused
  // ("Next report").
  std::string okLabel, next;
  // Asks on the row itself before something that can't be undone: OK
  // turns the row red with `title` over `line`, OK again runs `select`;
  // Back, or moving to another row, keeps things as they are.
  struct Ask {
    std::string title;            // "Remove Backup TV?"
    std::string line;             // "OK to remove · Back to keep it"
    std::string ok, back;         // the key hints: "Remove", "Keep it"
    std::string heading, about;  // the right column: what happens
  };
  std::optional<Ask> ask;

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
  // Up/Down/Ok. Returns true when handled. OK on a row that asks first
  // (PanelItem::ask) asks; OK again acts.
  bool key(Key k);
  // The row asking now, if any; stopAsking() (Back) keeps things as they
  // are and says whether one was asking.
  const PanelItem* asking() const;
  bool stopAsking();
  // Pointer: id from the hit regions registered by draw(). A Choice row's
  // choices are targets of their own, so a click picks the one under it.
  void hover(int id);
  // The pointer's wheel: scrolls without moving the focus (until a key).
  void scrollBy(float dy);

  // Draws inside `r` (the panel's own background is drawn by the caller).
  // `hitBase` offsets the pointer ids this panel registers.
  // `showFocus` false draws the focused row like the others (focus is elsewhere).
  void draw(Ctx& c, Rect r, int hitBase = 1000, bool showFocus = true);
  // What the rows say (their words, values and states, not the focus): rows
  // rebuilt with live state are drawn again only when it changed.
  std::string look() const;

  static constexpr int kHitBase = 1000;
  // Choice k of row i: kHitBase + kChoiceHits + i * 8 + k.
  static constexpr int kChoiceHits = 100000;

 private:
  float rowHeight(Ctx& c, const PanelItem& it, float width) const;
  void settleFocus();
  settings_nav::Arm arm_;  // the row asking first
  int focus_ = -1;
  float scroll_ = 0;
  bool followFocus_ = true;  // scrolled to keep the focus in view
  int hoveredChoice_ = -1;   // the choice under the pointer on the focused row
  float maxScroll_ = 0;
};

}  // namespace marigold::ui
