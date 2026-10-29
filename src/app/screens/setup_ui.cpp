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

std::string grouped(size_t n) {
  std::string digits = std::to_string(n), out;
  for (size_t i = 0; i < digits.size(); ++i) {
    if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
    out += digits[i];
  }
  return out;
}

void building(ui::Ctx& c, Rect area, const engine::LoadProgress& p, Building& st, bool large) {
  using Stage = engine::LoadProgress::Stage;
  // The bar's share of each step: downloading, recognizing, local stations
  // and games, done.
  static const float kFrom[] = {0.0f, 0.18f, 0.86f, 1.0f};
  static const float kTo[] = {0.18f, 0.86f, 0.97f, 1.0f};
  int stage = static_cast<int>(p.stage);
  if (p.stage == Stage::Download && st.downloadFrom == 0) st.downloadFrom = c.ms ? c.ms : 1;
  float target;
  if (p.stage == Stage::Done) {
    target = 1.0f;
  } else if (p.stage == Stage::Download) {
    // No byte count: creeps towards the step's end and never reaches it.
    float t = static_cast<float>(c.ms - st.downloadFrom) / 12000.0f;
    target = kFrom[0] + (kTo[0] - kFrom[0]) * 0.92f * (1.0f - std::exp(-t));
  } else {
    float within = std::min(std::max(p.within, 0.0f), 0.97f);
    target = kFrom[stage] + (kTo[stage] - kFrom[stage]) * within;
  }
  float dt = st.lastMs ? static_cast<float>(c.ms - st.lastMs) : 16.0f;
  st.lastMs = c.ms;
  if (target > st.shown) st.shown += (target - st.shown) * std::min(1.0f, dt / 400.0f);

  float scale = large ? 1.0f : 0.8f;
  auto px = [scale](float v) { return static_cast<int>(std::lround(v * scale)); };
  float cx = area.x + area.w * 0.5f;
  float y = area.y + area.h * (large ? 0.26f : 0.18f);
  // Twelve petals, the brightest turning slowly, round an amber centre.
  ui::Color amber = theme::hex(theme::kStar);
  float ring = 58 * scale, petal = 20 * scale;
  int lead = static_cast<int>((c.ms / 150) % 12);
  for (int i = 0; i < 12; ++i) {
    float ang = static_cast<float>(i) * 0.5235988f;
    int age = (lead - i + 12) % 12;
    float a = p.stage == Stage::Done ? 0.9f : 0.95f - static_cast<float>(age) * 0.065f;
    c.g.fill({cx + std::cos(ang) * ring - petal * 0.5f, y + std::sin(ang) * ring - petal * 0.5f, petal, petal},
             amber.withAlpha(a), petal * 0.5f);
  }
  float core = 30 * scale;
  if (p.stage == Stage::Done) {
    c.g.fill({cx - core * 0.7f, y - core * 0.7f, core * 1.4f, core * 1.4f}, theme::hex(theme::kToggleOn), core * 0.7f);
    ui::labelIn(c, "\xe2\x9c\x93", {cx - core, y - core, core * 2, core * 2}, Weight::Bold, px(30), theme::white(),
                Align::Center);
  } else {
    c.g.fill({cx - core * 0.5f, y - core * 0.5f, core, core}, amber, core * 0.5f);
  }
  y += ring + 60 * scale;
  ui::label(c, p.stage == Stage::Done ? "Your lineup is ready" : "Building your lineup", area.x, y, Weight::Bold,
            px(44), theme::white(), area.w, Align::Center);
  y += 62 * scale;
  std::string doing = p.stage == Stage::Download  ? "Getting your channel list from your provider"
                      : p.stage == Stage::Games   ? "Finding your local stations and games"
                      : p.stage == Stage::Done    ? "Opening the guide"
                      : p.channels > 0            ? "Recognizing " + grouped(p.channels) + " channels"
                                                  : "Recognizing your channels";
  ui::label(c, doing, area.x, y, Weight::Regular, px(24), theme::white(0.65f), area.w, Align::Center);
  y += 56 * scale;
  float bw = 620 * scale;
  c.g.fill({cx - bw * 0.5f, y, bw, 8}, theme::white(0.12f), 4);
  c.g.fill({cx - bw * 0.5f, y, std::max(8.0f, bw * st.shown), 8}, amber, 4);
  y += 44 * scale;
  // The four steps: ticked when done, a spinner on the current one.
  static const char* const kStepNames[] = {"Download", "Recognize", "Locals & games", "Ready"};
  float gap = 36 * scale, total = 0;
  float widths[4];
  for (int i = 0; i < 4; ++i) {
    widths[i] = 34 * scale + ui::textWidth(c, kStepNames[i], Weight::Medium, px(20));
    total += widths[i] + (i ? gap : 0);
  }
  float x = cx - total * 0.5f;
  for (int i = 0; i < 4; ++i) {
    float d = 22 * scale;
    Rect dot{x, y, d, d};
    bool done = i < stage || p.stage == Stage::Done;
    if (done) {
      c.g.fill(dot, theme::hex(theme::kToggleOn), d * 0.5f);
      ui::labelIn(c, "\xe2\x9c\x93", dot, Weight::Bold, px(14), theme::white(), Align::Center);
    } else if (i == stage) {
      ui::spinner(c, x + d * 0.5f, y + d * 0.5f, d * 0.42f);
    } else {
      c.g.fill(dot, theme::white(0.12f), d * 0.5f);
    }
    ui::label(c, kStepNames[i], x + d + 10 * scale, y - 1, Weight::Medium, px(20),
              done || i == stage ? theme::white(0.85f) : theme::white(0.45f));
    x += widths[i] + gap;
  }
}

}  // namespace marigold::app::setup_ui
