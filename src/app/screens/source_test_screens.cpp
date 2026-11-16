// Build 166: Test all sources' screens (core/engine/source_test).
// The start panel over Settings › Providers, the testing screen (the test
// carries on in the app when it's left, and dims after a minute of no keys),
// the providers ranked when it ends, and "If you kept one or two". Shares
// only: never a count of channels; never a link.
#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/app.hpp"
#include "app/screens/screens.hpp"
#include "app/screens/setup_ui.hpp"
#include "app/store/store.hpp"
#include "core/engine/source_test.hpp"

namespace marigold::app {
namespace {

namespace st = engine::sourcetest;
namespace scores = engine::scores;
using ui::Align;
using ui::Color;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;

constexpr float kPadX = 96;
constexpr uint32_t kCard = 0x1C1F25;
constexpr uint32_t kBox = 0x16191F;
constexpr uint32_t kGood = 0x7EE2A8;
constexpr uint32_t kBad = 0xFF8A8A;
constexpr uint32_t kDimAfterMs = 60000;
constexpr int kHitButton = 9100;  // + index
constexpr int kHitChoice = 9200;  // + index

std::string percent(double share) {
  return std::to_string(static_cast<int>(std::lround(std::clamp(share, 0.0, 1.0) * 100.0))) + "%";
}

std::string upper(std::string s) {
  for (char& ch : s)
    if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
  return s;
}

// A provider's name by its source id.
std::string nameOf(App& app, const std::string& id) {
  for (const auto& [sid, name] : app.store().providerNames())
    if (sid == id) return name.empty() ? std::string("A provider") : name;
  return "A provider";
}

// Pill buttons in a row from (x, y), right-aligned when `right`; focused
// white. Returns the row's width.
float pills(ui::Ctx& c, float x, float y, const std::vector<std::string>& titles, int focused, bool right,
            const std::vector<bool>& enabled = {}) {
  std::vector<float> widths;
  float total = 0;
  for (const std::string& t : titles) {
    widths.push_back(std::max(150.0f, ui::chipWidth(c, t, 25) + 60));
    total += widths.back();
  }
  total += 18 * static_cast<float>(titles.size() > 0 ? titles.size() - 1 : 0);
  float at = right ? x - total : x;
  for (size_t i = 0; i < titles.size(); ++i) {
    Rect r{at, y, widths[i], 66};
    bool on = enabled.empty() || enabled[i];
    bool f = static_cast<int>(i) == focused;
    c.hit(r, kHitButton + static_cast<int>(i));
    c.g.fill(r, f ? theme::white() : theme::white(0.12f), 33);
    ui::labelIn(c, titles[i], r, Weight::Bold, 25, f ? theme::black() : theme::white(on ? 1.0f : 0.4f), Align::Center);
    at += widths[i] + 18;
  }
  return total;
}

std::string testedLine(App& app, time::Seconds t) {
  return st::testedText(t, time::now(), app.zone(), app.store().settings().clock24);
}

// ------------------------------------------------------------ start panel

class TestStart : public Screen {
 public:
  explicit TestStart(App& app) : Screen(app), estimate_(app.sourceTest().estimateNow()) {}
  const char* name() const override { return "test start"; }
  bool opaque() const override { return false; }

  bool key(const KeyEvent& e) override {
    switch (e.key) {
      case Key::Left: button_ = 0; return true;
      case Key::Right: button_ = 1; return true;
      case Key::Ok:
        if (button_ == 0) {
          app_.sourceTest().start();
          app_.nav().pop();
          app_.nav().push(makeTesting(app_));
        } else {
          app_.nav().pop();
        }
        return true;
      default: return e.key != Key::Back;
    }
  }
  void hover(int id) override {
    if (id == kHitButton || id == kHitButton + 1) button_ = id - kHitButton;
  }

  void draw(ui::Ctx& c) override {
    ui::scrim(c, 0.72f);
    float w = 1080, x = (gfx::kLayoutWidth - w) * 0.5f;
    std::vector<std::string> lines = {
        st::estimateText(estimate_) + ". Leave the TV on; the screen dims.",
        "Start watching any time: the test pauses and carries on after.",
        "One connection per provider at a time, gently spaced.",
    };
    std::string lead =
        "Every source of every channel in your lineup is checked, on all your providers: how fast it starts, "
        "whether it plays, its real picture, and how far back replay really goes.";
    float leadH = static_cast<float>(c.t.wrap(lead, Weight::Regular, 27, w - 120, 5).size()) * 42;
    float h = 54 + 63 + 27 + leadH + 27 + 3 * 48 + 36 + 66 + 54;
    Rect r{x, (gfx::kLayoutHeight - h) * 0.5f, w, h};
    c.g.fill(r, theme::hex(kCard, 0.98f), 30);
    float y = r.y + 54;
    ui::label(c, "Test all sources", r.x + 60, y, Weight::Bold, 42, theme::white(), w - 120);
    y += 63 + 27;
    y += ui::paragraph(c, lead, r.x + 60, y, w - 120, Weight::Regular, 27, theme::white(0.8f), 5, 1.55f);
    y += 27;
    for (const std::string& l : lines) {
      c.g.fill({r.x + 60, y + 12, 12, 12}, theme::hex(theme::kStar), 6);
      ui::label(c, l, r.x + 96, y, Weight::Regular, 25, theme::white(0.8f), w - 156);
      y += 48;
    }
    y += 36;
    pills(c, r.x + 60, y, {"Start", "Not now"}, button_, false);
  }

 private:
  uint32_t estimate_ = 0;
  int button_ = 0;
};

// ---------------------------------------------------------- testing screen

class Testing : public Screen {
 public:
  explicit Testing(App& app) : Screen(app), lastKey_(app.ms()), ended_(app.sourceTest().endedSerial()) {}
  const char* name() const override { return "testing"; }

  bool tick(uint32_t ms) override {
    st::Runner& t = app_.sourceTest();
    if (!leaving_ && (t.endedSerial() != ended_ || !t.running())) {
      // It ended (or was stopped): the results in its place.
      leaving_ = true;
      app_.nav().pop();
      app_.nav().push(makeTestResults(app_));
      return true;
    }
    bool dim = ms - lastKey_ >= kDimAfterMs;
    if (dim != dimmed_) {
      dimmed_ = dim;
      return true;
    }
    return false;
  }
  uint32_t wakeAt() const override { return dimmed_ ? 0 : lastKey_ + kDimAfterMs; }

  bool key(const KeyEvent& e) override {
    bool was = dimmed_;
    lastKey_ = app_.ms();
    dimmed_ = false;
    if (was) return true;  // any key brings it back, and does only that
    if (e.key == Key::Ok) {
      // Stop: what was found so far is kept (the tick shows the results).
      app_.sourceTest().stop();
      return true;
    }
    return e.key != Key::Back;  // Back: the test carries on in the app
  }
  void hover(int) override {
    lastKey_ = app_.ms();
    dimmed_ = false;
  }

  void draw(ui::Ctx& c) override {
    c.g.fill({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, theme::hex(theme::kBackground));
    st::Runner::Status s = app_.sourceTest().status();
    float right = gfx::kLayoutWidth - kPadX;
    ui::label(c, "Testing your sources", kPadX, 72, Weight::Bold, 45, theme::white(), 1200);
    ui::label(c, st::headerLine(s), kPadX, 136, Weight::Regular, 27, theme::white(0.65f), 1200);
    pills(c, right, 84, {"Stop"}, 0, true);
    ui::progress(c, {kPadX, 210, right - kPadX, 15}, s.fraction);

    // One card per provider, three to a row.
    float y = 258, gap = 27;
    float w = (right - kPadX - 2 * gap) / 3, h = 186;
    time::Seconds now = time::now();
    size_t shown = std::min<size_t>(s.providers.size(), 6);
    for (size_t i = 0; i < shown; ++i) {
      const st::Runner::Provider& p = s.providers[i];
      Rect r{kPadX + static_cast<float>(i % 3) * (w + gap), y + static_cast<float>(i / 3) * (h + gap), w, h};
      c.g.fill(r, theme::hex(kCard), 24);
      bool held = p.state == st::Runner::Provider::State::Paused || p.state == st::Runner::Provider::State::Stopped;
      ui::label(c, p.name, r.x + 36, r.y + 30, Weight::Bold, 30, theme::white(), r.w - 200);
      ui::label(c, st::providerCorner(p), r.x + 36, r.y + 36, Weight::Medium, 24,
                held ? theme::hex(theme::kWarning) : theme::white(0.65f), r.w - 72, Align::Right);
      double share = p.total ? static_cast<double>(p.done) / static_cast<double>(p.total) : 0.0;
      ui::progress(c, {r.x + 36, r.y + 88, r.w - 72, 9}, share, theme::white(0.14f), theme::white(0.55f));
      ui::paragraph(c, st::providerLine(p, now), r.x + 36, r.y + 114, r.w - 72, Weight::Regular, 24,
                    theme::white(0.8f), 2, 1.3f);
    }
    if (shown > 3) y += h + gap;
    y += h + 36;

    // What it found so far.
    std::vector<std::string> found = s.found;
    if (found.empty()) found.emplace_back("Nothing to report yet; the first results come in a few minutes.");
    float boxH = 33 + 33 + static_cast<float>(found.size()) * 40 + 30;
    Rect box{kPadX, y, right - kPadX, boxH};
    c.g.fill(box, theme::hex(kBox), 24);
    ui::label(c, "FOUND SO FAR", box.x + 36, box.y + 30, Weight::Bold, 22, theme::white(0.5f));
    float ly = box.y + 72;
    for (const std::string& f : found) {
      ui::label(c, f, box.x + 36, ly, Weight::Regular, 25, theme::white(0.8f), box.w - 72);
      ly += 40;
    }
    ui::paragraph(c,
                  "Press Back to watch TV: the test pauses and picks up where it left off. The screen dims after a "
                  "minute; any key brings it back.",
                  kPadX, std::max(y + boxH + 36, 960.0f), right - kPadX, Weight::Regular, 24, theme::white(0.55f), 2);
    if (dimmed_) ui::scrim(c, 0.88f);
  }

 private:
  uint32_t lastKey_ = 0;
  int ended_ = 0;
  bool dimmed_ = false;
  bool leaving_ = false;
};

// -------------------------------------------------------------- results

class Results : public Screen {
 public:
  explicit Results(App& app) : Screen(app) { load(); }
  const char* name() const override { return "test results"; }
  void shown() override { load(); }

  bool key(const KeyEvent& e) override {
    switch (e.key) {
      case Key::Left:
        button_ = std::max(0, button_ - 1);
        return true;
      case Key::Right:
        button_ = std::min(2, button_ + 1);
        return true;
      case Key::Ok: act(); return true;
      default: return e.key != Key::Back;
    }
  }
  void hover(int id) override {
    if (id >= kHitButton && id < kHitButton + 3) button_ = id - kHitButton;
  }

  void draw(ui::Ctx& c) override {
    c.g.fill({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, theme::hex(theme::kBackground));
    float left = 84, right = gfx::kLayoutWidth - 84;
    ui::label(c, "Your providers, ranked", left, 60, Weight::Bold, 45, theme::white(), 900);
    std::string when = app_.sourceTest().lastTested() > 0 ? testedLine(app_, app_.sourceTest().lastTested()) : "";
    // "today, 1:12 AM"
    size_t space = when.find(' ');
    if (space != std::string::npos && (when.rfind("today", 0) == 0 || when.rfind("yesterday", 0) == 0))
      when.insert(space, ",");
    std::string sub = when.empty() ? std::string("Not tested yet")
                                   : "Tested " + when + " \xc2\xb7 every channel now starts with its best source";
    ui::label(c, sub, left, 124, Weight::Regular, 27, theme::white(0.65f), 1050);
    pills(c, right, 78, {"Keeping one or two", "Done", "Test again"}, button_, true,
          {roll_.providers.size() >= 2, true, true});

    // The table: the provider, then six columns.
    const char* heads[] = {"Provider", "Has your channels", "Only here", "Replay", "Starts in", "Didn't play",
                           "Real full HD"};
    float firstW = 300, colW = (right - left - firstW) / 6;
    float y = 210, headH = 66;
    size_t n = order_.size();
    float rowH = n > 4 ? 78 : 99;
    Rect table{left, y, right - left, headH + rowH * static_cast<float>(n)};
    c.g.fill(table, theme::hex(kCard), 21);
    c.g.fill({left, y, right - left, headH}, theme::hex(0x22262E), 21);
    c.g.fill({left, y + headH - 21, right - left, 21}, theme::hex(0x22262E));
    for (int k = 0; k < 7; ++k) {
      float x = k == 0 ? left + 27 : left + firstW + colW * static_cast<float>(k - 1) + 18;
      ui::labelIn(c, heads[k], {x, y, (k == 0 ? firstW : colW) - 24, headH}, Weight::Bold, 22, theme::white(0.55f));
    }
    // The best start and replay in green; many failures in red.
    std::optional<uint32_t> fastest;
    double mostReplay = 0;
    for (const scores::ProviderFigures& f : roll_.providers) {
      if (f.medianStartMs && (!fastest || *f.medianStartMs < *fastest)) fastest = f.medianStartMs;
      mostReplay = std::max(mostReplay, f.replayWorks);
    }
    for (size_t i = 0; i < n; ++i) {
      const scores::ProviderFigures& f = roll_.providers[order_[i]];
      float ry = y + headH + rowH * static_cast<float>(i);
      if (i > 0) c.g.fill({left, ry, right - left, 2}, theme::white(0.07f));
      float badge = 45;
      Rect b{left + 27, ry + (rowH - badge) * 0.5f, badge, badge};
      c.g.fill(b, i == 0 ? theme::white() : theme::white(0.18f), badge * 0.5f);
      ui::labelIn(c, std::to_string(i + 1), b, Weight::Bold, 24, i == 0 ? theme::black() : theme::white(),
                  Align::Center);
      ui::labelIn(c, names_[order_[i]], {b.x + badge + 18, ry, firstW - badge - 60, rowH}, Weight::Bold, 28,
                  theme::white());
      auto cell = [&](int k, const std::string& text, Color col) {
        float x = left + firstW + colW * static_cast<float>(k - 1) + 18;
        ui::labelIn(c, text, {x, ry, colW - 24, rowH}, Weight::Regular, 25, col);
      };
      Color plain = theme::white(0.9f);
      cell(1, percent(f.lineup), plain);
      cell(2, percent(f.only), plain);
      cell(3, st::replayCell(f),
           f.replayWorks <= 0 ? theme::white(0.5f) : f.replayWorks >= mostReplay ? theme::hex(kGood) : plain);
      cell(4, f.medianStartMs ? st::secondsText(*f.medianStartMs) : std::string("\xe2\x80\x93"),
           f.medianStartMs && fastest && *f.medianStartMs == *fastest ? theme::hex(kGood) : plain);
      cell(5, f.startFailures ? percent(*f.startFailures) : std::string("\xe2\x80\x93"),
           f.startFailures && *f.startFailures >= 0.1 ? theme::hex(kBad) : plain);
      cell(6, f.fullHdCopies ? percent(*f.fullHdCopies) : std::string("\xe2\x80\x93"), plain);
    }
    if (n == 0)
      ui::label(c, "Add a provider and set up My Lineup to compare providers.", left + 27, y + headH + 24,
                Weight::Regular, 25, theme::white(0.7f));

    // Two notes: how they're ranked, and the same service underneath.
    float ny = table.y + table.h + 30, nw = (right - left - 24) / 2;
    std::string how =
        "Ranked by what matters when you watch: whether it plays, how fast it starts and its real picture, then "
        "how much of your lineup it has and its replay.";
    float hh = std::max(static_cast<float>(c.t.wrap(how, Weight::Regular, 25, nw - 66, 4).size()),
                        static_cast<float>(c.t.wrap(sameNote_, Weight::Regular, 25, nw - 66, 4).size())) *
                   38 +
               54;
    for (int k = 0; k < 2; ++k) {
      Rect box{left + static_cast<float>(k) * (nw + 24), ny, nw, hh};
      c.g.fill(box, theme::hex(kBox), 21);
      ui::paragraph(c, k == 0 ? how : sameNote_, box.x + 33, box.y + 27, nw - 66, Weight::Regular, 25,
                    theme::white(0.8f), 4, 1.5f);
    }
    ui::label(c, "From the test and the channels you've watched. Pinning a source still wins.", left,
              std::min(ny + hh + 24, 1020.0f), Weight::Regular, 21, theme::white(0.45f));
  }

 private:
  void load() {
    roll_ = app_.store().providerRollup();
    order_ = st::rank(roll_);
    names_.clear();
    for (const scores::ProviderFigures& f : roll_.providers) names_.push_back(nameOf(app_, f.provider));
    const std::vector<st::SameService>& same = app_.store().sameServices();
    sameNote_.clear();
    if (roll_.providers.size() < 2) {
      sameNote_ = "With one provider, a channel's backups come from that provider too.";
    } else if (same.empty()) {
      sameNote_ = "Your providers look like separate services: when one stops, the others can carry on.";
    } else {
      const st::SameService& s = same.front();
      sameNote_ = nameOf(app_, s.a) + " and " + nameOf(app_, s.b) +
                  " look like the same service underneath: when one stops, the other may too.";
      for (size_t i : order_) {
        const std::string& p = roll_.providers[i].provider;
        if (st::isSame(same, p, s.a) || st::isSame(same, p, s.b) || p == s.a || p == s.b) continue;
        sameNote_ += " Backups come from " + nameOf(app_, p) + " first.";
        break;
      }
    }
  }
  void act() {
    switch (button_) {
      case 0:
        if (roll_.providers.size() >= 2) app_.nav().push(makeKeepingPairs(app_));
        break;
      case 1: app_.nav().pop(); break;
      default:
        app_.nav().pop();
        app_.nav().push(makeTestStart(app_));
        break;
    }
  }

  scores::Rollup roll_;
  std::vector<size_t> order_;
  std::vector<std::string> names_;
  std::string sameNote_;
  int button_ = 0;
};

// --------------------------------------------------- keeping one or two

class Pairs : public Screen {
 public:
  explicit Pairs(App& app) : Screen(app) {
    rows_ = app.store().lineupRows();
    scores::Rollup r = app.store().providerRollup();
    choices_ = st::choices(r, app.store().sameServices());
  }
  const char* name() const override { return "test pairs"; }

  bool key(const KeyEvent& e) override {
    int n = static_cast<int>(choices_.size());
    switch (e.key) {
      case Key::Up:
        if (focus_ > 0) --focus_;
        return true;
      case Key::Down:
        if (focus_ + 1 < n) ++focus_;
        return true;
      case Key::Ok:
      case Key::Left:
      case Key::Right: return true;
      default: return e.key != Key::Back;
    }
  }
  void hover(int id) override {
    int i = id - kHitChoice;
    if (i >= 0 && i < static_cast<int>(choices_.size())) focus_ = i;
  }

  void draw(ui::Ctx& c) override {
    c.g.fill({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, theme::hex(theme::kBackground));
    float left = 84, right = gfx::kLayoutWidth - 84;
    ui::label(c, "If you kept one or two", left, 60, Weight::Bold, 45, theme::white(), 1400);
    ui::label(c, "How much of your lineup and its replay each choice keeps, from the latest test", left, 124,
              Weight::Regular, 27, theme::white(0.65f), 1600);
    float top = 200, bottom = 930, gap = 30;
    float listW = (right - left - gap) * 1.25f / 2.25f, panelX = left + listW + gap, panelW = right - panelX;
    // The choices, the focused one kept in view.
    float itemH = 114, step = itemH + 15;
    float visible = bottom - top;
    scroll_ = setup_ui::keepRowInView(scroll_, focus_, itemH, 15, visible);
    c.g.pushClip({left - 6, top - 6, listW + 12, visible + 12});
    for (size_t i = 0; i < choices_.size(); ++i) {
      const st::Choice& ch = choices_[i];
      Rect r{left, top + step * static_cast<float>(i) - scroll_, listW, itemH};
      if (r.y + r.h < top - 6 || r.y > bottom + 6) continue;
      bool f = static_cast<int>(i) == focus_;
      c.hit(r, kHitChoice + static_cast<int>(i));
      c.g.fill(r, f ? theme::white() : theme::hex(kCard), 21);
      Color ink = f ? theme::black() : theme::white();
      Color soft = f ? theme::black(0.7f) : theme::white(0.75f);
      std::string title = nameOf(app_, ch.providers[0]);
      if (ch.providers.size() > 1) title += " + " + nameOf(app_, ch.providers[1]);
      else title += " alone";
      float tw = ui::label(c, title, r.x + 30, r.y + 18, Weight::Bold, 30, ink, r.w - 260);
      if (ch.best) ui::chip(c, "BEST PAIR", r.x + 30 + tw + 18, r.y + 22, 20, theme::hex(theme::kStar), theme::black());
      const char* kind = ch.kind == st::Choice::Kind::Separate ? "Two separate services"
                         : ch.kind == st::Choice::Kind::Same   ? "Likely the same service underneath"
                                                               : "No backup";
      std::string line = "Lineup " + percent(ch.lineup) + "     Replay " + percent(ch.replay) + "     " + kind;
      ui::label(c, line, r.x + 30, r.y + 68, Weight::Regular, 24, soft, r.w - 60);
    }
    c.g.popClip();

    // What the focused choice would lose, by name.
    Rect panel{panelX, top, panelW, visible};
    c.g.fill(panel, theme::hex(kCard), 24);
    if (focus_ >= 0 && focus_ < static_cast<int>(choices_.size())) {
      const st::Choice& ch = choices_[static_cast<size_t>(focus_)];
      std::string who = nameOf(app_, ch.providers[0]);
      if (ch.providers.size() > 1) who += " + " + nameOf(app_, ch.providers[1]);
      ui::label(c, "WITH " + upper(who) + " YOU'D LOSE", panel.x + 36, panel.y + 33, Weight::Bold, 22,
                theme::white(0.5f), panel.w - 72);
      st::Lost l = st::lost(rows_, ch.providers);
      float y = panel.y + 81;
      if (l.names.empty()) {
        ui::label(c, "Nothing: every channel in My Lineup stays.", panel.x + 36, y, Weight::Regular, 25,
                  theme::white(0.9f), panel.w - 72);
      }
      for (const std::string& name : l.names) {
        ui::label(c, name, panel.x + 36, y, Weight::Regular, 25, theme::white(0.9f), panel.w - 72);
        y += 39;
      }
      if (l.more) ui::label(c, "and a few more", panel.x + 36, y, Weight::Regular, 25, theme::white(0.55f));
      ui::paragraph(c,
                    "You'd keep everything else. Channels that only one provider has are listed by name, so you can "
                    "decide if they matter.",
                    panel.x + 36, panel.y + panel.h - 120, panel.w - 72, Weight::Regular, 22, theme::white(0.65f), 3,
                    1.5f);
    }
    ui::label(c, "Nothing changes until you remove a provider yourself.", left, 966, Weight::Regular, 21,
              theme::white(0.45f));
  }

 private:
  std::vector<scores::LineupRow> rows_;
  std::vector<st::Choice> choices_;
  int focus_ = 0;
  float scroll_ = 0;
};

}  // namespace

std::unique_ptr<Screen> makeTestStart(App& app) { return std::make_unique<TestStart>(app); }
std::unique_ptr<Screen> makeTesting(App& app) { return std::make_unique<Testing>(app); }
std::unique_ptr<Screen> makeTestResults(App& app) { return std::make_unique<Results>(app); }
std::unique_ptr<Screen> makeKeepingPairs(App& app) { return std::make_unique<Pairs>(app); }

void openTestAllSources(App& app) {
  st::Runner& t = app.sourceTest();
  if (t.running()) app.nav().push(makeTesting(app));
  else if (t.lastTested() > 0) app.nav().push(makeTestResults(app));
  else app.nav().push(makeTestStart(app));
}

std::string testAllSourcesLine(App& app) {
  st::Runner& t = app.sourceTest();
  if (t.running()) {
    st::Runner::Status s = t.status();
    return (s.waitingForPlay ? "Paused while you watch \xc2\xb7 " : "Testing now \xc2\xb7 ") + percent(s.fraction);
  }
  if (t.lastTested() > 0) return "Last tested: " + testedLine(app, t.lastTested());
  return "Last tested: never";
}

}  // namespace marigold::app
