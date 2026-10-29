// The guide (LiveTVGuideView): hero with the focused show and the preview
// corner, tabs, the time ruler, the channel grid with neighborhood headers,
// the neighborhood rail and the colour-key bar.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app/screen.hpp"
#include "app/screens/setup_ui.hpp"
#include "app/store/store.hpp"
#include "core/engine/views.hpp"

namespace marigold::app {

class GuideScreen : public Screen {
 public:
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
  // The Live Events board, over the guide (also from the end of setup).
  void openEvents();

 private:
  using Tab = engine::views::Tab;
  using Filter = engine::views::Filter::Kind;
  enum class Area { Tabs, Buttons, Time, Grid, Rail };
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
  void openRail();
  void select();
  void longPress();
  void watch(const std::string& entryId);
  void openTimeMenu();
  void openFilter();
  void openOptions();
  void openSources();
  void openSearch();
  void openLineupSetup();
  void digit(int d);
  void requestVisibleListings();

  void drawHero(ui::Ctx& c);
  void drawPreview(ui::Ctx& c);
  void drawTabs(ui::Ctx& c);
  void drawRuler(ui::Ctx& c);
  void drawGrid(ui::Ctx& c);
  void drawRow(ui::Ctx& c, int lineIndex, float y);
  void drawChannelCell(ui::Ctx& c, const GuideChannel& g, ui::Rect r, bool focused);
  void drawRail(ui::Ctx& c);
  void drawStatus(ui::Ctx& c);
  // The header's status line (updating, updated, couldn't), fading out when
  // a result's few seconds are up.
  void drawHeaderLine(ui::Ctx& c, float x, float y, float right);
  void drawDigits(ui::Ctx& c);

  time::Seconds nowWindow() const;
  time::Seconds windowEnd() const;
  float xAt(time::Seconds t) const;
  bool later() const { return windowStart_ > nowWindow(); }

  std::shared_ptr<const Channels> ch_;
  std::shared_ptr<const Listings> li_;
  uint64_t chVersion_ = ~0ull, liVersion_ = ~0ull;

  std::vector<Tab> tabs_;
  Tab tab_ = Tab::Lineup;
  int tabFocus_ = 0;
  Area area_ = Area::Grid;
  int button_ = 0;  // 0 search, 1 filter, 2 options

  std::vector<Line> lines_;
  std::vector<Section> sections_;
  std::vector<std::string> flip_;  // the channel lines' entries, for CH+ / CH-
  float contentH_ = 0;
  int focusLine_ = -1;
  setup_ui::Building building_;
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

  int railIndex_ = 0, railOrigin_ = 0, railReturnLine_ = -1;

  std::string digits_;
  uint32_t digitsAt_ = 0;

  uint32_t focusSince_ = 0, lastPreviewAt_ = 0;
  std::string previewCandidate_;
  uint32_t backAt_ = 0;
};

}  // namespace marigold::app
