// The guide (LiveTVGuideView): hero with the focused show and the preview
// corner, the time ruler, the channel grid with neighborhood headers and the
// colour-key bar, right of the left rail (Build 112): the strip of sections,
// the neighborhood column that pushes the grid, and the menu.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app/screen.hpp"
#include "app/screens/launch_mark.hpp"
#include "app/screens/left_rail.hpp"
#include "app/screens/setup_ui.hpp"
#include "app/store/store.hpp"
#include "core/engine/views.hpp"
#include "core/input/hints.hpp"
#include "core/input/left_rail.hpp"

namespace marigold::app {

class GuideScreen : public Screen {
 public:
  bool holdsOk() const override { return true; }
  explicit GuideScreen(App& app);

  bool key(const KeyEvent& e) override;
  void draw(ui::Ctx& c) override;
  bool tick(uint32_t ms) override;
  void hover(int id) override;
  void shown() override;
  bool keepsPreview() const override { return true; }

  // Window presets from the time menu.
  void setWindow(time::Seconds start);
  void returnToNow();
  void backOut();
  // Focus a channel row (search, number keys, back from the player).
  bool focusChannel(const std::string& entryId);
  // The Live Events board, over the guide (also from the end of setup);
  // `sports` opens it on its SPORTS column.
  void openEvents(bool sports = false);
  // All settings from the strip's Settings, the gear and the status screen:
  // the one way in (openAllSettings). Blue: Quick settings.
  void openOptions();
  void openQuickSettings();

 private:
  using Tab = engine::views::Tab;
  using Filter = engine::views::Filter::Kind;
  // The column and the menu are the rail's (nav_.mode()); these are the
  // content's own places.
  enum class Area { Time, Filter, Grid };
  struct Line {
    enum class Kind { Header, Channel, Prompt } kind = Kind::Channel;
    int channel = -1;  // index into Channels::all
    std::string title;  // header text
    float y = 0;        // top, relative to the grid
  };
  struct Section {
    std::string title;
    int firstLine = 0;
    int count = 0;
  };
  // Where the focus was when the column opened (the return rule).
  struct Place {
    int line = -1;
    std::string id;
    time::Seconds window = 0, time = 0;
    float scroll = 0;
  };

  void refresh(bool force = false);
  // `samePlace`: the list itself changed (sorted again), so a row that's
  // gone leaves the focus where it was rather than at the top.
  void rebuildLines(bool samePlace = false);
  const std::vector<int>& tabRows() const;
  engine::views::Filter filter() const { return {filter_, filterSection_}; }
  const GuideChannel* focusedChannel() const;
  const Listing* focusedListing() const;
  int programIndex(const std::vector<Listing>& v, time::Seconds t) const;
  const std::vector<std::string>& flipList() const { return flip_; }

  void moveRow(int step);
  void moveProgram(int step);
  void ensureVisible();
  int lineOf(const std::string& id) const;
  // The left rail: the column on the neighborhood the focus is in, a step
  // of the key path, picking a strip item, the pointer's click, and
  // closing (cancelling) whatever is open.
  int originSection() const;
  void openColumn(bool fromMenu);
  void leftEdge();
  void apply(const rail::Step& s, bool fromMenu);
  void open(rail::Item item, bool fromMenu);
  void showSection(Tab t);
  void fromEvents(rail::Item item, bool column);
  bool click();
  void closeRail();
  void restorePlace();
  void syncSlides();
  void settleSlides(uint32_t ms);
  hints::Rail railHint() const;
  void select();
  void longPress();
  void watch(const std::string& entryId);
  void openTimeMenu();
  void openFilter();
  void openSources();
  void openSearch();
  void openLineupSetup();
  void digit(int d);
  void requestVisibleListings();

  void drawHero(ui::Ctx& c);
  void drawPreview(ui::Ctx& c);
  // The filter and "BACK returns to Now" chips, right-aligned on the hero's
  // top line (the tab row they sat in is gone); returns their left edge.
  float drawChips(ui::Ctx& c, float right);
  void drawRuler(ui::Ctx& c);
  void drawGrid(ui::Ctx& c);
  void drawRow(ui::Ctx& c, int lineIndex, float y);
  void drawChannelCell(ui::Ctx& c, const GuideChannel& g, ui::Rect r, bool focused);
  void drawColumn(ui::Ctx& c, float alpha);
  void drawGuide(ui::Ctx& c);
  void drawStatus(ui::Ctx& c);
  // The header's status line (updating, updated, couldn't), fading out when
  // a result's few seconds are up.
  void drawHeaderLine(ui::Ctx& c, float x, float y, float right);
  void drawDigits(ui::Ctx& c);

  time::Seconds nowWindow() const;
  time::Seconds windowEnd() const;
  // Three hours at rest, two with the column open (re-timed once its push
  // lands); while the grid slides it keeps the old scale and shows what
  // fits up to the right edge (drawEnd).
  int windowMinutes() const;
  float progX() const;
  float pxPerSec() const;
  time::Seconds drawEnd() const;
  float xAt(time::Seconds t) const;
  bool later() const { return windowStart_ > nowWindow(); }

  std::shared_ptr<const Channels> ch_;
  std::shared_ptr<const Listings> li_;
  uint64_t chVersion_ = ~0ull, liVersion_ = ~0ull;

  Tab tab_ = Tab::Lineup;
  Area area_ = Area::Grid;

  std::vector<Line> lines_;
  std::vector<Section> sections_;
  std::vector<std::string> flip_;  // the channel lines' entries, for CH+ / CH-
  float contentH_ = 0;
  int focusLine_ = -1;
  setup_ui::Building building_;
  // Opening the app: the flower covers loading, then blooms out (once).
  LaunchMark launch_;
  bool launchOff_ = false;
  bool skeletonShown_ = false;  // placeholders were drawn last frame
  uint32_t pulseStep_ = 0;
  std::string shownLine_;
  StoreStatus::Mark shownMark_ = StoreStatus::Mark::None;
  uint32_t fadeFrom_ = 0;
  float scroll_ = 0;
  std::string focusId_;  // keeps the focus across rebuilds

  time::Seconds windowStart_ = 0;
  time::Seconds focusTime_ = 0;
  uint32_t lastKeyMs_ = 0;

  Filter filter_ = Filter::None;
  std::string filterSection_;

  rail::Nav nav_;
  Place place_;
  left_rail::Slide push_, menu_;  // the column's push, the menu's slide
  float left_ = left_rail::kRestLeft;  // the timeline and grid's left edge
  bool narrow_ = false;               // the two-hour window (column open)
  int pointer_ = 0;  // what the pointer is on (a hit id) while it shows

  std::string digits_;
  uint32_t digitsAt_ = 0;

  uint32_t focusSince_ = 0, lastPreviewAt_ = 0;
  std::string previewCandidate_;
  uint32_t backAt_ = 0;
};

}  // namespace marigold::app
