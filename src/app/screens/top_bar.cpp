#include "app/screens/top_bar.hpp"

#include "app/playback.hpp"
#include "platform/icons.hpp"

namespace marigold::app::top_bar {
namespace {

using ui::Color;
namespace theme = ui::theme;

// Search and filter icons from rectangles (no vector paths here); the gear
// is app/icons/ui-settings.png.
void iconSearch(gfx::Renderer& g, float cx, float cy, Color c) {
  g.stroke({cx - 11, cy - 11, 18, 18}, c, 9, 2.4f);
  for (int i = 0; i < 4; ++i) g.fill({cx + 4 + i * 1.8f, cy + 4 + i * 1.8f, 3.2f, 3.2f}, c, 1.4f);
}
void iconFilter(gfx::Renderer& g, float cx, float cy, Color c) {
  g.fill({cx - 12, cy - 8, 24, 2.6f}, c, 1.3f);
  g.fill({cx - 7, cy - 1, 14, 2.6f}, c, 1.3f);
  g.fill({cx - 3, cy + 6, 6, 2.6f}, c, 1.3f);
}
void iconSettings(ui::Ctx& c, float cx, float cy, Color col) {
  if (unsigned icon = c.icons ? c.icons->get("ui-settings") : 0) {
    c.g.textureAlpha(icon, {cx - 15, cy - 15, 30, 30}, col);
    return;
  }
  // Until the icon loads: a ring with four teeth.
  c.g.stroke({cx - 9, cy - 9, 18, 18}, col, 9, 2.6f);
  c.g.fill({cx - 2.5f, cy - 13, 5, 6}, col, 1);
  c.g.fill({cx - 2.5f, cy + 7, 5, 6}, col, 1);
  c.g.fill({cx - 13, cy - 2.5f, 6, 5}, col, 1);
  c.g.fill({cx + 7, cy - 2.5f, 6, 5}, col, 1);
}

}  // namespace

void clock(ui::Ctx& c) {
  ui::label(c, ui::clock(c, c.now), kPreview.x, 40, ui::Weight::Medium, 26, theme::white(0.7f), kPreview.w,
            ui::Align::Right);
}

void preview(ui::Ctx& c, const Playback& p, const std::string& logoUrl, const std::string& name) {
  bool showing = p.active() && !p.full() && p.ready();
  if (showing) {
    c.g.hole(kPreview);
    ui::chip(c, "LIVE", kPreview.x + 12, kPreview.y + 12, 16, theme::hex(theme::kLive), theme::white());
  } else {
    c.g.fill(kPreview, theme::white(0.05f), 16);
    if (!logoUrl.empty() || !name.empty()) ui::logo(c, logoUrl, name, {kPreview.x + 100, kPreview.y + 77, 200, 70});
    if (p.active() && !p.full() && !p.failed())
      ui::spinner(c, kPreview.x + kPreview.w - 30, kPreview.y + kPreview.h - 30, 10);
  }
  c.g.stroke(kPreview, theme::white(0.15f), 16, 1);
}

void buttons(ui::Ctx& c, int focused, bool filtered, int hitBase) {
  for (int i = 0; i < 3; ++i) {
    ui::Rect r{gfx::kLayoutWidth - 80 - 60 - static_cast<float>(2 - i) * 76, kTabsY, 60, 60};
    bool f = focused == i;
    c.hit(r, hitBase + i);
    c.g.fill(r, f ? theme::white() : theme::white(0.1f), 30);
    Color ic = f ? theme::black() : theme::white();
    float cx = r.x + 30, cy = r.y + 30;
    if (i == 0) {
      iconSearch(c.g, cx, cy, ic);
    } else if (i == 1) {
      iconFilter(c.g, cx, cy, ic);
      if (filtered) c.g.fill({r.x + 44, r.y + 6, 12, 12}, theme::hex(theme::kNowLine), 6);
    } else {
      iconSettings(c, cx, cy, ic);
    }
  }
}

}  // namespace marigold::app::top_bar
