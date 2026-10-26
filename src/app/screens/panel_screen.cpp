#include "app/screens/panel_screen.hpp"

#include "app/app.hpp"

namespace marigold::app {

PanelScreen::PanelScreen(App& app, Side side, float width, std::function<void(ui::ListPanel&)> build)
    : Screen(app), side_(side), width_(width), build_(std::move(build)) {
  rebuild();
  panel_.focusFirst();
}

void PanelScreen::rebuild() {
  int focus = panel_.focused();
  panel_.items.clear();
  if (build_) build_(panel_);
  panel_.focusIndex(focus < 0 ? 0 : focus);
}

void PanelScreen::shown() { rebuild(); }

bool PanelScreen::key(const KeyEvent& e) {
  if (onKey && onKey(e)) return true;
  if (e.key == Key::Ok && e.longPress) {
    int f = panel_.focused();
    if (f >= 0 && f < static_cast<int>(panel_.items.size()) && panel_.items[static_cast<size_t>(f)].hold) {
      auto hold = panel_.items[static_cast<size_t>(f)].hold;
      hold();
      rebuild();
      return true;
    }
  }
  if (e.key == Key::Ok) {
    bool used = panel_.key(e.key);
    rebuild();
    return used;
  }
  return panel_.key(e.key);
}

void PanelScreen::draw(ui::Ctx& c) {
  float h = gfx::kLayoutHeight;
  if (side_ == Side::Center) {
    ui::scrim(c, 0.6f);
    float ph = 760;
    ui::Rect r{(gfx::kLayoutWidth - width_) * 0.5f, (h - ph) * 0.5f, width_, ph};
    c.g.fill(r, ui::theme::hex(0x1C1F25, 0.98f), 34);
    panel_.draw(c, {r.x + 50, r.y + 44, r.w - 100, r.h - 80});
    return;
  }
  bool leading = side_ == Side::Leading;
  ui::Rect r{leading ? 0.0f : gfx::kLayoutWidth - width_, 0, width_, h};
  ui::sidePanel(c, r, leading);
  float inset = width_ * 0.12f;
  float contentW = width_ - inset - 70;
  float x = leading ? 70.0f : r.x + inset;
  panel_.draw(c, {x, 70, contentW, h - 120});
}

}  // namespace marigold::app
