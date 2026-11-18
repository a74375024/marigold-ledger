#include "app/screens/panel_screen.hpp"

#include <algorithm>

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

void PanelScreen::notice(std::string title, std::string detail, uint32_t forMs) {
  notice_ = std::move(title);
  noticeDetail_ = std::move(detail);
  noticeUntil_ = app_.ms() + forMs;
}

bool PanelScreen::tick(uint32_t ms) {
  if (!notice_.empty() && ms >= noticeUntil_) {
    // The notice's time is up: drawn once more, without it.
    notice_.clear();
    noticeDetail_.clear();
    return true;
  }
  if (!liveRows || ms - rebuiltAt_ < 1000) return false;
  rebuiltAt_ = ms;
  // Drawn again only when what the rows say changed.
  std::string before = panel_.look();
  rebuild();
  return panel_.look() != before;
}

bool PanelScreen::key(const KeyEvent& e) {
  if (onKey && onKey(e)) return true;
  // A row asking first: Back keeps things as they are.
  if (e.key == Key::Back && panel_.stopAsking()) return true;
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
  bool used = panel_.key(e.key);
  if (used && (e.key == Key::Left || e.key == Key::Right)) rebuild();  // a Choice row
  return used;
}

void PanelScreen::draw(ui::Ctx& c) {
  drawPanel(c);
  if (!notice_.empty() && app_.ms() < noticeUntil_) ui::noticeCard(c, notice_, noticeDetail_);
}

void PanelScreen::drawPanel(ui::Ctx& c) {
  float h = gfx::kLayoutHeight;
  if (side_ == Side::Beside) {
    // A small card left of the panel it belongs to, its rows' height.
    float inner = width_ - 56;
    float ph = std::min(h - 160, panel_.contentHeight(c, inner) + 40);
    ui::Rect r{gfx::kLayoutWidth - besideOf - 20 - width_, (h - ph) * 0.5f, width_, ph};
    c.g.fill(r, ui::theme::hex(0x1F2632, 0.98f), 26);
    c.g.stroke(r, ui::theme::white(0.08f), 26, 1);
    panel_.draw(c, {r.x + 28, r.y + 22, inner, r.h - 30});
    return;
  }
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
  float rows = h - 120;
  if (footer && footerHeight > 0) {
    rows -= footerHeight + 28;
    footer(c, {x, 70 + rows + 28, contentW, footerHeight});
  }
  panel_.draw(c, {x, 70, contentW, rows});
}

}  // namespace marigold::app
