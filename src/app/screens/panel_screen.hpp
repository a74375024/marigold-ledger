// A side panel as a screen: a ListPanel over whatever is below. The items
// are rebuilt by `build` after every selection, so toggles show their new
// state; `build` may also push or pop screens.
#pragma once

#include <functional>

#include "app/screen.hpp"
#include "app/ui/panel.hpp"

namespace marigold::app {

class PanelScreen : public Screen {
 public:
  enum class Side { Leading, Trailing, Center };
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
  bool tick(uint32_t ms) override;

 private:
  Side side_;
  float width_;
  std::function<void(ui::ListPanel&)> build_;
  ui::ListPanel panel_;
  uint32_t rebuiltAt_ = 0;
};

}  // namespace marigold::app
