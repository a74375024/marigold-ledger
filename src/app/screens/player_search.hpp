// Search in the player (Build 132), inside the in-player guide's card: the
// guide's lists open it (Search, their first item), and the card becomes
// search while the picture keeps playing beside it with its sound. The
// Search screen's keyboard on the left (app/ui/keyboard, core/input/
// search_keys), live results on the right from the same passes
// (core/engine/search_live) and the same cells (search_view): the top
// result, then Channels, Games and Shows, your lineup first, never a count.
//
// Nothing previews: the picture changes only when something is picked. OK
// on a channel or anything on now switches the picture in place (as a
// channel picked in the guide: through My Lineup, Recent) and closes the
// card; a game on now plays the game, in place; a later show or game opens
// its details (Remind Me) over the card, and Back there returns here.
// Where each key goes between the keyboard, the results and the lists is
// core/input/player_rail's (PlayerNav). The query stays for the player's
// session (Playback::playerQuery()); leaving the player clears it.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "app/screen.hpp"
#include "app/screens/search_view.hpp"
#include "core/engine/search_live.hpp"
#include "core/input/player_rail.hpp"
#include "core/input/search_keys.hpp"

namespace marigold::app {

class App;

class PlayerSearch {
 public:
  // Pointer ids, clear of the guide's own and left_rail's.
  static constexpr int kHitKeys = 50000;   // + the keyboard's ids
  static constexpr int kHitLists = 50100;  // "◀ Lists"
  static constexpr int kHitCells = 50200;  // + row

  PlayerSearch(App& app, rail::PlayerNav& nav);
  ~PlayerSearch();
  PlayerSearch(const PlayerSearch&) = delete;
  PlayerSearch& operator=(const PlayerSearch&) = delete;

  // The card became search (Search picked in the lists): the query typed
  // last while the player has been open, the focus on the keyboard. Again
  // while open: back on the keyboard.
  void open();
  // Search is over (another list, Back from the lists over it).
  void close();
  bool isOpen() const { return open_; }
  // PlayerNav moved the focus (Do::Results, Do::Keyboard).
  void focusResults();
  void focusKeyboard();

  // A key in search (not Back, not in the lists: PlayerNav's). What the
  // rail does next: Results, Keyboard, the lists opened (None).
  rail::PlayerStep key(const KeyEvent& e);
  void hover(int id);
  // A click: true when taken here (then `step` is the rail's); false on a
  // key or a result, which the hover focused and OK then acts on.
  bool click(int id, rail::PlayerStep& step);
  // The TV's keyboard (🎤 Speak) sent its text.
  void text(const std::string& value, bool closed);
  // A computer's keyboard typed `ch` ("\b": delete). `took` when it went
  // into the field; then what the rail does next.
  rail::PlayerStep typed(const std::string& ch, bool& took);
  // Back from a details card: a reminder may show on a row.
  void shown() { labelsDirty_ = true; }

  bool tick(uint32_t ms);
  uint32_t wakeAt() const { return wake_; }
  // The header, the keyboard and the results inside the guide's card
  // (`card`), and with `legend` its legend line at `legendY`.
  void draw(ui::Ctx& c, ui::Rect card, float legendY, bool legend);
  void pointerGone() { pointer_ = 0; }

 private:
  struct Row {
    std::string header;  // over it ("TOP RESULT", "CHANNELS"...)
    search_view::Cell cell;
    float y = 0, h = 0;
  };

  bool any() const { return !rows_.empty(); }
  bool inResults() const { return nav_.search() && nav_.results(); }
  void setQuery(const std::string& q);
  void search(bool shows, bool keep = false);
  void layout();
  void add(const std::vector<engine::search::Item>& items, const char* header, bool mark);
  void wakeFor(uint32_t at);
  std::array<uint64_t, 3> versions() const;
  void labelAll(const ui::Ctx& c);

  rail::PlayerStep keyboardKey(const KeyEvent& e);
  rail::PlayerStep resultsKey(const KeyEvent& e);
  // OK on a result, and whether OK switches the picture to it here.
  void select(const search_view::Cell& cell);
  bool watchesHere(const search_view::Cell& cell) const;
  void watchChannel(const std::string& entryId);
  void watchFamily(const std::string& familyId);
  void watchGame(const std::string& eventId);
  void selectGame(const std::string& eventId);
  void remember(const search_view::Cell& cell, const std::string& name);

  void drawResults(ui::Ctx& c, ui::Rect card);

  App& app_;
  rail::PlayerNav& nav_;
  bool open_ = false;
  search_keys::Grid grid_;
  bool appleTv_ = false;
  std::string query_;
  std::shared_ptr<const engine::search::Index> index_;
  engine::search::Live live_;
  uint64_t eventsVersion_ = 0, channelsVersion_ = 0, listingsVersion_ = 0;
  std::vector<Row> rows_;
  int row_ = 0;
  float scroll_ = 0;
  search_keys::GlideStop glide_;
  int pointer_ = 0;
  uint32_t refreshedAt_ = 0;
  uint32_t wake_ = 0;
  // The rows' text (label()), and what it was made from.
  bool labelsDirty_ = true;
  time::Seconds labelMinute_ = 0;
  std::array<uint64_t, 3> labeled_{};
  std::string nothingText_;
};

}  // namespace marigold::app
