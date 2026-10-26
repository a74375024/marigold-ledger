// The guide (LiveTVGuideView): hero with the focused show and the preview
// corner, tabs, the time ruler, the channel grid with neighborhood headers,
// the neighborhood rail and the colour-key bar.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app/screen.hpp"
#include "app/store/model.hpp"

namespace marigold::app {

class GuideScreen : public Screen {
 public:
  explicit GuideScreen(App& app);

  bool key(const KeyEvent& e) override;
  void draw(ui::Ctx& c) override;
  bool tick(uint32_t ms) override;
  void hover(int id) override;
  void shown() override;

  // Window presets from the time menu.
  void setWindow(time::Seconds start);
  void returnToNow();
  // Focus a channel row (search, number keys, back from the player).
  bool focusChannel(const std::string& entryId);

 private:
  enum class Tab { Lineup, Favorites, Recent, Events, All };
  enum class Area { Tabs, Buttons, Time, Grid, Rail };
  enum class Filter { None, SportsNow, NewsNow, MoviesNow, KidsNow, Section };
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
  void rebuildLines();
  const std::vector<int>& tabRows() const;
  bool passesFilter(const GuideChannel& g) const;
  const GuideChannel* focusedChannel() const;
  const Listing* focusedListing() const;
  int programIndex(const std::vector<Listing>& v, time::Seconds t) const;
  std::vector<std::string> flipList() const;

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
  float contentH_ = 0;
  int focusLine_ = -1;
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
