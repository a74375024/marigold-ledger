// The guide in the player (Build 116): the left rail's key path inside its
// card, decided here for every platform so it can be tested without a
// screen. It is the main guide's path (rail::Nav and its return rule) with
// the lists in place of the strip's menu, and the grid's own Left and Back.
// The in-player guide and the games rail draw it and do what a Step says.
//
//   grid --Left at the first show at Now--> column --Left--> lists
//   a list without neighborhoods (Recent): grid --Left there--> lists
//   grid: Left at a later show or a later time goes earlier first; Back
//   clears a typed number, then goes back to Now, then closes the guide.
//   column: Up/Down walk the neighborhoods (the grid scrolls behind,
//   nothing tunes); OK or Right confirm (the return rule); Back cancels.
//   lists: Up/Down; OK or Right open one; Back goes back to the column
//   (to the grid on a list without one).
//
// A Siri Remote swipe (KeyEvent::glide) moves one step in the column or
// the lists, and a swipe that walked the grid to its left edge stops
// there: the next one opens the column.
//
// Search (Build 132) is the first of the lists, as in the main guide's
// rail. OK on it turns the card into search: the keyboard, then the
// results (Right at the keyboard's right edge). Back goes results ->
// keyboard -> the lists; Left at the keyboard's left edge (or "◀ Lists")
// opens the lists too. The lists' Back over search goes to the grid of the
// list that was on before it, as it was (Back there closes the guide), and
// OK on Search there comes back to the keyboard.
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "core/input/keys.hpp"
#include "core/input/left_rail.hpp"

namespace marigold::rail {

// The lists, top to bottom: Search, My Lineup, Favorites, Recent, Live
// Events (while the game schedule is on in Settings) and All Channels
// (while its tab is on).
std::vector<Item> playerLists(bool allChannels, bool liveEvents);

// The grid when a key comes: the screen's own state.
struct Grid {
  bool firstShow = false;  // the focus is on the first show in view
  bool later = false;      // the window starts after Now
  bool digits = false;     // a channel number is being typed
  int origin = 0;          // the focused row's neighborhood
  int count = 0;           // the list's neighborhoods (0: none, as Recent)
};

// What the screen does after a key or a click.
struct PlayerStep {
  enum class Do {
    Grid,     // the grid's own key: moves, OK, Left going earlier
    None,     // nothing more than a redraw
    Column,   // the column opened: keep the place for the return rule
    Scroll,   // the column moved to `index`: the grid scrolls to it behind
    Restore,  // back exactly where the focus was when the column opened
    Top,      // the top of neighborhood `index`, at Now
    Open,     // `item` was picked in the lists
    Digits,   // Back: the typed number goes
    Now,      // Back at a later time: Now again
    Close,    // Back with nothing left: the guide closes
    // Search (Build 132).
    Search,    // the card is search, the focus on its keyboard (Search picked)
    Results,   // the focus went to the results
    Keyboard,  // the focus went back to the keyboard
    Leave,     // search closed: the grid of `item` (the list before it), as it was
  } what = Do::Grid;
  int index = 0;
  Item item = Item::Lineup;
};

class PlayerNav {
 public:
  explicit PlayerNav(Item list = Item::Lineup);

  void setLists(std::vector<Item> lists) { nav_.setItems(std::move(lists)); }
  const std::vector<Item>& lists() const { return nav_.items(); }
  // The list on screen (the lists' bar).
  void setList(Item i) { nav_.setSection(i); }
  Item list() const { return nav_.section(); }
  Mode mode() const { return nav_.mode(); }

  PlayerStep key(Key k, const Grid& g, uint32_t glide = 0);

  // Left at the edge without the key: the pointer's list name.
  PlayerStep edge(int origin, int count);
  // A list opened from the lists: its column on `origin` (the playing
  // channel's neighborhood); Back there goes to the grid.
  PlayerStep openColumn(int origin, int count);
  // The column's "◀ Lists" clicked: as Left in the column. In search,
  // the keyboard's left edge (`glide`: the swipe that got there stops).
  PlayerStep openLists(uint32_t glide = 0);
  // The pointer: a click on a neighborhood (the return rule, as OK), a
  // list (as OK), the dimmed card beside the lists (as Back).
  PlayerStep clickColumn(int i);
  PlayerStep pickList(int i);
  PlayerStep closeLists();
  // Back to the grid as it is (a click on it, a number, a page key).
  void close() { nav_.close(); }

  // Search (Build 132). The card is search (the lists may be open over it)
  // and the focus is in its results (else on its keyboard).
  bool search() const { return search_; }
  bool results() const { return results_; }
  // Search opened without the lists (the games rail's lists picked it).
  PlayerStep openSearch();
  // In search, Right at the keyboard's right edge (or a result clicked):
  // the results, when there are any.
  PlayerStep toResults(bool any);
  // In search, Left at the results' left edge (or a key clicked).
  PlayerStep toKeyboard();
  // The list the grid shows under search (Leave goes back to it).
  Item before() const { return before_; }

  int origin() const { return nav_.origin(); }
  int index() const { return nav_.index(); }
  int count() const { return nav_.count(); }
  // The lists' focus, an index into lists().
  int listFocus() const { return nav_.menuFocus(); }
  // The lists opened from the column (Back there goes back to it).
  bool fromColumn() const { return fromColumn_; }

 private:
  static PlayerStep of(const Step& s);
  // A list picked in the lists: Search opens search; another leaves it.
  PlayerStep picked(PlayerStep s);

  Nav nav_;
  bool fromColumn_ = false;
  bool search_ = false, results_ = false;
  Item before_ = Item::Lineup;
  uint32_t glide_ = 0;  // the last swipe that moved something
};

}  // namespace marigold::rail
