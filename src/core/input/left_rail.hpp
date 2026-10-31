// The left rail (Build 112): the strip's items and the key path between a
// section's content, its neighborhood column and the full-height menu,
// decided here for every platform so it can be tested without a screen.
// The guide and Live Events draw the strip, the column and the menu, and do
// what a Step says (scroll behind the column, put the focus back, go to a
// neighborhood's top, switch section, open a screen).
//
//   content --Left at the first show--> column --Left--> menu
//   column: Up/Down walk the neighborhoods; OK or Right confirms (the return
//   rule: the neighborhood it opened on puts the focus back exactly where it
//   was, another one goes to its top); Back cancels.
//   menu: OK or Right picks; Back goes to My Lineup, or out on My Lineup.
#pragma once

#include <vector>

#include "core/input/keys.hpp"

namespace marigold::rail {

enum class Item { Search, Lineup, Favorites, Recent, Events, All, MySports, Options };

// The strip from top to bottom; All Channels only when that setting is on.
std::vector<Item> items(bool allChannels);
const char* title(Item i);
// The sections the strip's bar marks (the others open their own screens).
bool isSection(Item i);
// Sections with a neighborhood column: My Lineup, Favorites and All Channels
// (their neighborhoods) and Live Events (SPORTS). Recent has none.
bool hasColumn(Item i);
// The bottom group (My Sports, Options), below a rule.
bool atBottom(Item i);

enum class Mode { Content, Column, Menu };

// What the screen does after a key or a click.
struct Step {
  enum class Do {
    None,     // nothing more than a redraw
    Scroll,   // the column moved to `index`: the content scrolls to its top behind it
    Restore,  // back exactly where the focus was when the column opened
    Top,      // the top of neighborhood `index`, at Now
    Open,     // `item` was picked: a section to switch to, or a screen to open
    Home,     // Back in the menu on another section: My Lineup, at rest
    Exit,     // Back in the menu on My Lineup: the platform's way out
  } what = Do::None;
  int index = 0;
  Item item = Item::Lineup;
};

class Nav {
 public:
  explicit Nav(Item section = Item::Lineup);

  void setItems(std::vector<Item> items);
  const std::vector<Item>& items() const { return items_; }
  // The section on screen (the strip's bar).
  void setSection(Item s) { section_ = s; }
  Item section() const { return section_; }
  Mode mode() const { return mode_; }

  // The content: Left at its left edge (the first show at Now, a row's first
  // tile). Opens the column on the neighborhood the focus is in (`origin`
  // of `count`) when the section has one, else the menu.
  void leftEdge(int origin, int count);
  // The column, opened on `origin`. `fromMenu`: Back returns to the menu.
  void openColumn(int origin, int count, bool fromMenu = false);
  // The menu, focus on the current section (Back with nothing to undo).
  void openMenu();
  // Back to the content as it is (a page key, a number, a click on the grid).
  void close() { mode_ = Mode::Content; }

  // Keys while the column or the menu is open. Content keys are the screen's.
  Step key(Key k);
  // The pointer: a click on a neighborhood (the return rule, as OK), and an
  // item on the strip or in the menu (as the menu's OK).
  Step clickColumn(int i);
  Step pick(Item i);
  void hoverMenu(int i);

  // The column.
  int origin() const { return origin_; }
  int index() const { return index_; }
  int count() const { return count_; }
  bool fromMenu() const { return fromMenu_; }
  // The menu's focus, an index into items().
  int menuFocus() const { return menuFocus_; }

 private:
  Step confirm(int i);
  int indexOf(Item i) const;

  std::vector<Item> items_;
  Item section_;
  Mode mode_ = Mode::Content;
  int origin_ = 0, index_ = 0, count_ = 0;
  bool fromMenu_ = false;
  int menuFocus_ = 0;
};

}  // namespace marigold::rail
