#include "app/screens/setup_ui.hpp"

#include <cctype>
#include <cmath>

#include "core/input/hints.hpp"

namespace marigold::app::setup_ui {
namespace {

using ui::Align;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;

char firstLetter(const std::string& s) {
  for (char c : s) {
    if (std::isalnum(static_cast<unsigned char>(c))) return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return 0;
}

}  // namespace

const char* title(Step s) {
  switch (s) {
    case Step::Provider: return "Provider";
    case Step::Area: return "Area";
    case Step::Languages: return "Languages";
    case Step::Channels: return "Channels";
    case Step::Sports: return "Sports";
    case Step::Teams: return "Teams";
    case Step::Done: return "Done";
  }
  return "";
}

std::string kicker(Step s) {
  std::string t = title(s);
  for (char& ch : t) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  return "STEP " + std::to_string(static_cast<int>(s) + 1) + " OF " + std::to_string(kSteps) + " \xc2\xb7 " + t;
}

void stepBar(ui::Ctx& c, Step at) {
  float x = kLeft, y = kStepsY;
  for (int i = 0; i < kSteps; ++i) {
    Step s = static_cast<Step>(i);
    bool now = s == at, done = i < static_cast<int>(at);
    Rect dot{x, y, 34, 34};
    if (done) {
      c.g.fill(dot, theme::hex(theme::kToggleOn), 17);
      ui::labelIn(c, "\xe2\x9c\x93", dot, Weight::Bold, 17, theme::white(), Align::Center);
    } else if (now) {
      c.g.fill(dot, theme::white(), 17);
      ui::labelIn(c, std::to_string(i + 1), dot, Weight::Bold, 17, theme::black(), Align::Center);
    } else {
      c.g.stroke(dot, theme::white(0.3f), 17, 2);
      ui::labelIn(c, std::to_string(i + 1), dot, Weight::Bold, 17, theme::white(0.45f), Align::Center);
    }
    x += 44;
    x += ui::labelIn(c, title(s), {x, y, 300, 34}, Weight::Bold, 20,
                     now ? theme::white() : done ? theme::white(0.75f) : theme::white(0.45f));
    if (i + 1 < kSteps) {
      c.g.fill({x + 16, y + 16, 56, 2}, theme::white(0.15f));
      x += 88;
    }
  }
}

float heading(ui::Ctx& c, const std::string& kick, const std::string& title, const std::string& lead, float width,
              float y) {
  ui::label(c, kick, kLeft, y, Weight::Bold, 18, theme::white(0.5f), width);
  y += 34;
  y += ui::paragraph(c, title, kLeft, y, width, Weight::Bold, 56, theme::white(), 2, 1.15f) + 14;
  if (!lead.empty()) y += ui::paragraph(c, lead, kLeft, y, width, Weight::Regular, 26, theme::white(0.7f), 4) + 30;
  return y;
}

float buttons(ui::Ctx& c, float x, float y, const std::vector<std::string>& titles, int focused, int hitBase) {
  float start = x;
  for (size_t i = 0; i < titles.size(); ++i) {
    float w = std::max(180.0f, ui::chipWidth(c, titles[i], 26) + 64);
    Rect r{x, y, w, 70};
    c.hit(r, hitBase + static_cast<int>(i));
    bool f = static_cast<int>(i) == focused;
    c.g.fill(r, f ? theme::white() : theme::white(0.12f), 35);
    ui::labelIn(c, titles[i], r, Weight::Bold, 26, f ? theme::black() : theme::white(), Align::Center);
    x += w + 16;
  }
  return x - start - 16;
}

void keys(ui::Ctx& c, const RemoteKeys& remote, bool letters) { ui::keyBar(c, hints::setup(remote, letters)); }

void qr(ui::Ctx& c, Rect r, const report::QrMatrix& m) {
  c.g.fill(r, theme::white(), 24);
  int n = m.size + 2 * report::kQuietZone;
  float module = std::floor(r.w / static_cast<float>(n));
  float ox = r.x + std::round((r.w - module * static_cast<float>(n)) * 0.5f) + module * report::kQuietZone;
  float oy = r.y + std::round((r.h - module * static_cast<float>(n)) * 0.5f) + module * report::kQuietZone;
  for (int yy = 0; yy < m.size; ++yy) {
    // Runs of dark modules as one rectangle.
    int run = -1;
    for (int xx = 0; xx <= m.size; ++xx) {
      bool dark = xx < m.size && m.at(xx, yy);
      if (dark && run < 0) run = xx;
      if (!dark && run >= 0) {
        c.g.fill({ox + module * static_cast<float>(run), oy + module * static_cast<float>(yy),
                  module * static_cast<float>(xx - run), module},
                 theme::black());
        run = -1;
      }
    }
  }
}

int letterStep(Key k) {
  switch (k) {
    case Key::ChannelDown:
    case Key::FastForward: return 1;
    case Key::ChannelUp:
    case Key::Rewind: return -1;
    default: return 0;
  }
}

int nextLetter(const std::vector<std::string>& names, int from, int step) {
  int n = static_cast<int>(names.size());
  if (from < 0 || from >= n || step == 0) return from;
  char at = firstLetter(names[static_cast<size_t>(from)]);
  for (int i = from + step; i >= 0 && i < n; i += step) {
    const std::string& s = names[static_cast<size_t>(i)];
    if (s.empty()) continue;
    char l = firstLetter(s);
    if (l == at) continue;
    if (step > 0) return i;
    // Back: the first entry of that letter.
    int j = i;
    for (int k = i - 1; k >= 0; --k) {
      const std::string& p = names[static_cast<size_t>(k)];
      if (p.empty() || firstLetter(p) != l) break;
      j = k;
    }
    return j;
  }
  return from;
}

}  // namespace marigold::app::setup_ui
