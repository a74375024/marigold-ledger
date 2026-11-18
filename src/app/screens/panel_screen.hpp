// A side panel as a screen: a ListPanel over whatever is below. The items
// are rebuilt by `build` after every selection, so toggles show their new
// state; `build` may also push or pop screens.
#pragma once

#include <functional>
#include <string>

#include "app/screen.hpp"
#include "app/ui/panel.hpp"

namespace marigold::app {

class PanelScreen : public Screen {
 public:
  bool holdsOk() const override { return true; }
  // Beside (Build 181): a small menu card just left of a trailing panel
  // `besideOf` wide (a hold menu over Sources), as tall as its rows.
  enum class Side { Leading, Trailing, Center, Beside };
  PanelScreen(App& app, Side side, float width, std::function<void(ui::ListPanel&)> build);

  bool key(const KeyEvent& e) override;
  void draw(ui::Ctx& c) override;
  void hover(int id) override { panel_.hover(id); }
  bool opaque() const override { return false; }
  void shown() override;

  ui::ListPanel& panel() { return panel_; }
  void rebuild();
  // Extra keys before the panel's own (colour keys, Left to step out).
  std::function<bool(const KeyEvent&)> onKey;
  // Rebuild the rows once a second (rows that show live state).
  bool liveRows = false;
  // Drawn under the rows, `footerHeight` tall (side panels only): the
  // Subtitles panel's caption preview (Build 169).
  float footerHeight = 0;
  std::function<void(ui::Ctx&, ui::Rect)> footer;
  float besideOf = 760;
  // Build 181: a large notice at the top of the screen for `forMs` (the
  // player's: "Feed 3 set aside for KNTV" over "Playing Feed 4 · OK again
  // to undo").
  void notice(std::string title, std::string detail, uint32_t forMs);
  bool tick(uint32_t ms) override;
  uint32_t wakeAt() const override { return notice_.empty() ? 0 : noticeUntil_; }
  const char* name() const override { return "panel"; }

 private:
  Side side_;
  float width_;
  std::function<void(ui::ListPanel&)> build_;
  void drawPanel(ui::Ctx& c);
  ui::ListPanel panel_;
  uint32_t rebuiltAt_ = 0;
  std::string notice_, noticeDetail_;
  uint32_t noticeUntil_ = 0;
};

}  // namespace marigold::app
