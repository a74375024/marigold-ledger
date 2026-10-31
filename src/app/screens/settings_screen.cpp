// Settings (Build 112). Quick settings: the Blue key, the player's Settings
// button and Blue on Live Events open a short side panel for where you are
// (the picture or the board stays in view): two to four rows, then All
// settings and Help & info. All settings is full screen, in two panes:
// the seven categories on the left with a summary of each, the focused
// category's rows in the middle (shown as you move, so it previews), and
// on the right what the focused row does. Right or OK goes in, Back goes
// back to the categories and Back again closes. With the Magic Remote every
// row is clickable and the wheel scrolls the pane under the pointer. The
// rows themselves are in settings_rows.cpp. Also the "Getting around" card.
#include <algorithm>
#include <cmath>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/panel_screen.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"
#include "app/ui/panel.hpp"
#include "core/captions/sentence_case.hpp"
#include "core/input/hints.hpp"
#include "core/sports/my_sports.hpp"
#include "core/util/strings.hpp"
#include "platform/icons.hpp"

namespace marigold::app {
namespace {

using ui::Align;
using ui::Color;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;

constexpr float kLeft = 80;
constexpr float kCatsY = 170, kCatW = 520, kCatH = 92, kCatGap = 8;
constexpr float kPaneX = 660, kPaneY = 62, kPaneW = 780, kPaneBottom = 992;
constexpr float kInfoX = 1490, kInfoY = 170, kInfoW = 350;
constexpr float kWheelStep = 90;
constexpr int kHitCategory = 100;  // + index; the rows use ListPanel's own ids
constexpr int kHitTips = 70;

const char* categoryTitle(SettingsCategory c) {
  switch (c) {
    case SettingsCategory::Watching: return "Watching";
    case SettingsCategory::Guide: return "Guide";
    case SettingsCategory::MyLineup: return "My Lineup";
    case SettingsCategory::LiveEvents: return "Live Events";
    case SettingsCategory::Providers: return "Providers";
    case SettingsCategory::Advanced: return "Advanced";
    case SettingsCategory::Help: return "Help & info";
  }
  return "";
}

// The right column while the categories have the focus.
struct About {
  std::string heading, text;
};
About categoryAbout(SettingsCategory c) {
  switch (c) {
    case SettingsCategory::Watching:
      return {"Subtitles and the remote",
              "Subtitles stay on for every channel of that language until you turn them off. Pick a language from Aa "
              "in the player.\nWhere the TV has its own captions switch, it turns them on and off too."};
    case SettingsCategory::Guide:
      return {"The guide", "The corner preview, the clock, the All Channels tab and where listings come from."};
    case SettingsCategory::MyLineup:
      return {"Your channels",
              "Your area, countries and kinds of channels decide what My Lineup suggests. Changes save as you go; "
              "channels you added or removed stay."};
    case SettingsCategory::LiveEvents:
      return {"Games and scores", "Your leagues and teams lead the board. Scores can be hidden; live and final still "
                                  "show."};
    case SettingsCategory::Providers:
      return {"Where channels come from", "Each provider keeps its own login on this TV. Passwords are never shown."};
    case SettingsCategory::Advanced:
      return {"Playback", "Only what applies to this TV and your providers."};
    case SettingsCategory::Help:
      return {"Help & info", "The remote's keys, reports to send to your phone, a test screen and the version."};
  }
  return {};
}

// Paragraphs split by '\n'. Returns the height used.
float paragraphs(ui::Ctx& c, const std::string& text, float x, float y, float w, Color col) {
  float top = y;
  for (std::string_view part : str::split(text, '\n')) {
    y += ui::paragraph(c, std::string(part), x, y, w, Weight::Regular, 21, col, 8, 1.4f) + 14;
  }
  return y - top;
}

// A small picture with a caption in the chosen size, look, letters and
// position (what the player draws, scaled down).
void captionPreview(ui::Ctx& c, Rect r, const Settings& set) {
  c.g.fill(r, theme::hex(0x2B3546), 14);
  c.g.fill({r.x, r.y + r.h * 0.45f, r.w, r.h * 0.55f}, theme::hex(0x2C6E31), 0);
  c.g.stroke(r, theme::white(0.12f), 14, 1);
  static const int kPx[] = {14, 17, 21, 26};
  int px = kPx[std::clamp(set.subtitleSize, 0, 3)];
  const char* lines[] = {"WE'RE BACK FOR THE", "SECOND HALF IN ANN ARBOR."};
  float lineH = std::round(static_cast<float>(px) * 1.36f);
  float pad = std::round(static_cast<float>(px) * 0.4f);
  float y = set.subtitleTop && !Playback::playerDrawsSubtitles() ? r.y + 14 : r.y + r.h - 14 - 2 * lineH;
  bool startsSentence = true;
  c.g.pushClip(r);
  for (const char* line : lines) {
    std::string text = set.subtitleSentenceCase && !Playback::playerDrawsSubtitles()
                           ? captions::sentenceCase(line, startsSentence)
                           : std::string(line);
    startsSentence = false;
    float w = ui::textWidth(c, text, Weight::Medium, px);
    float x = std::round(r.x + (r.w - w) * 0.5f);
    float ty = y + std::round((lineH - static_cast<float>(px) * 1.2f) * 0.5f);
    switch (std::clamp(set.subtitleLook, 0, 2)) {
      case 0: c.g.fill({x - pad, y, w + 2 * pad, lineH}, theme::black(0.78f)); break;
      case 1: {
        static const float kRing[][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
        for (const auto& o : kRing) ui::label(c, text, x + o[0], ty + o[1], Weight::Medium, px, theme::black(0.9f));
        break;
      }
      default: ui::label(c, text, x + 1, ty + 2, Weight::Medium, px, theme::black(0.85f)); break;
    }
    ui::label(c, text, x, ty, Weight::Medium, px, theme::white());
    y += lineH;
  }
  c.g.popClip();
}

// What a row is set to, for the right column's small heading.
std::string rowValue(const ui::PanelItem& it) {
  switch (it.kind) {
    case ui::PanelItem::Kind::Toggle: return it.on ? "On" : "Off";
    case ui::PanelItem::Kind::Choice:
      if (it.choice < 0 || it.choice >= static_cast<int>(it.choices.size())) return "";
      return it.choices[static_cast<size_t>(it.choice)];
    default: return it.value;
  }
}

class AllSettings : public Screen {
 public:
  AllSettings(App& app, std::optional<SettingsCategory> inside) : Screen(app) {
    listCategories();
    SettingsCategory start = inside.value_or(SettingsCategory::Watching);
    for (size_t i = 0; i < cats_.size(); ++i)
      if (cats_[i] == start) cat_ = static_cast<int>(i);
    buildRows(false);
    inRows_ = inside.has_value() && rows_.focused() >= 0;
  }

  void shown() override {
    listCategories();
    buildRows(true);
  }

  // Rows that show live state (a provider signing in, memory).
  bool tick(uint32_t ms) override {
    SettingsCategory c = category();
    if (c != SettingsCategory::Providers && c != SettingsCategory::Help) return false;
    if (ms - rebuiltAt_ < 1000) return false;
    rebuiltAt_ = ms;
    buildRows(true);
    return true;
  }

  bool key(const KeyEvent& e) override {
    if (e.wheel) return wheel(e.key == Key::Up ? -1 : 1);
    if (!inRows_) {
      switch (e.key) {
        case Key::Up:
        case Key::Down: moveCategory(e.key == Key::Up ? -1 : 1); return true;
        case Key::Right:
        case Key::Ok: enterRows(); return true;
        case Key::Back:
        case Key::Exit: return false;  // closes Settings
        default: return true;
      }
    }
    const ui::PanelItem* f = focusedRow();
    bool choice = f && f->kind == ui::PanelItem::Kind::Choice;
    switch (e.key) {
      case Key::Up:
      case Key::Down: rows_.key(e.key); return true;
      case Key::Left:
        // On a row of choices Left and Right change it; elsewhere Left goes
        // back to the categories.
        if (choice) changeRow(Key::Left);
        else inRows_ = false;
        return true;
      case Key::Right:
        if (choice) changeRow(Key::Right);
        return true;
      case Key::Ok: changeRow(Key::Ok); return true;
      case Key::Back:
        inRows_ = false;
        return true;
      case Key::Exit: return false;
      default: return true;
    }
  }

  void hover(int id) override {
    if (id >= kHitCategory && id < kHitCategory + static_cast<int>(cats_.size())) {
      pane_ = 0;
      inRows_ = false;
      if (id - kHitCategory != cat_) {
        cat_ = id - kHitCategory;
        buildRows(false);
      }
    } else if (id >= ui::ListPanel::kHitBase) {
      pane_ = 1;
      inRows_ = true;
      rows_.hover(id);
    }
  }

  void draw(ui::Ctx& c) override {
    c.g.fill({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, theme::hex(theme::kBackground));
    ui::label(c, "MARIGOLD", kLeft, 40, Weight::Bold, 16, theme::white(0.5f));
    ui::label(c, "Settings", kLeft, 62, Weight::Bold, 52, theme::white());
    drawCategories(c);
    drawRows(c);
    drawInfo(c);
    const ui::PanelItem* f = inRows_ ? focusedRow() : nullptr;
    ui::keyBar(c, hints::settings(app_.remoteKeys(), inRows_, f && f->kind == ui::PanelItem::Kind::Choice));
  }

 private:
  SettingsCategory category() const { return cats_[static_cast<size_t>(cat_)]; }

  void listCategories() {
    SettingsCategory was = cats_.empty() ? SettingsCategory::Watching : category();
    cats_.clear();
    for (SettingsCategory c : {SettingsCategory::Watching, SettingsCategory::Guide, SettingsCategory::MyLineup,
                               SettingsCategory::LiveEvents, SettingsCategory::Providers, SettingsCategory::Advanced,
                               SettingsCategory::Help})
      if (settingsCategoryShown(app_, c)) cats_.push_back(c);
    cat_ = 0;
    for (size_t i = 0; i < cats_.size(); ++i)
      if (cats_[i] == was) cat_ = static_cast<int>(i);
  }

  // The rows again: after every change (toggles show their new state), and
  // on another category (from its top).
  void buildRows(bool keepFocus) {
    int focus = rows_.focused();
    rows_.items.clear();
    settingsRows(app_, category(), rows_);
    if (keepFocus) rows_.focusIndex(focus < 0 ? 0 : focus);
    else rows_.focusFirst();
    // The categories' summaries change with the rows (not worked out
    // every frame).
    summaries_.clear();
    for (SettingsCategory c : cats_) summaries_.push_back(settingsSummary(app_, c));
  }

  const ui::PanelItem* focusedRow() const {
    int f = rows_.focused();
    return f >= 0 && f < static_cast<int>(rows_.items.size()) ? &rows_.items[static_cast<size_t>(f)] : nullptr;
  }

  void moveCategory(int step) {
    int next = std::clamp(cat_ + step, 0, static_cast<int>(cats_.size()) - 1);
    if (next == cat_) return;
    cat_ = next;
    buildRows(false);
  }

  void enterRows() {
    if (rows_.focused() >= 0) inRows_ = true;
  }

  void changeRow(Key k) {
    rows_.key(k);
    buildRows(true);
  }

  // The wheel moves the pane under the pointer: the categories a step, the
  // rows by scrolling.
  bool wheel(int step) {
    if (pane_ == 1) rows_.scrollBy(static_cast<float>(step) * kWheelStep);
    else moveCategory(step);
    return true;
  }

  void drawCategories(ui::Ctx& c) {
    float y = kCatsY;
    for (size_t i = 0; i < cats_.size(); ++i) {
      Rect r{kLeft, y, kCatW, kCatH};
      c.hit(r, kHitCategory + static_cast<int>(i));
      bool sel = static_cast<int>(i) == cat_;
      bool focused = sel && !inRows_;
      c.g.fill(r, focused ? theme::white() : sel ? theme::white(0.18f) : theme::white(0.07f), theme::kRowRadius);
      if (sel && !focused) c.g.fill({r.x, r.y + 18, 5, r.h - 36}, theme::white(), 2.5f);
      Color fg = focused ? theme::black() : theme::white();
      Color sub = focused ? theme::black(0.6f) : theme::white(0.6f);
      float tw = r.w - 48 - (sel ? 30 : 0);
      ui::label(c, categoryTitle(cats_[i]), r.x + 24, r.y + 15, Weight::Bold, 27, fg, tw);
      if (i < summaries_.size()) ui::label(c, summaries_[i], r.x + 24, r.y + 54, Weight::Medium, 19, sub, tw);
      if (sel)
        ui::labelIn(c, "\xe2\x80\xba", {r.x + r.w - 44, r.y, 20, r.h}, Weight::Bold, 30,
                    focused ? theme::black(0.45f) : theme::white(0.45f), Align::Right);
      y += kCatH + kCatGap;
    }
  }

  void drawRows(ui::Ctx& c) {
    std::string title = categoryTitle(category());
    std::string crumb = inRows_ ? "SETTINGS \xe2\x80\xba " + str::upperAscii(title) : std::string("SETTINGS");
    ui::label(c, crumb, kPaneX, kPaneY, Weight::Bold, 16, theme::white(0.5f), kPaneW);
    ui::label(c, title, kPaneX, kPaneY + 26, Weight::Bold, 44, theme::white(), kPaneW);
    Rect list{kPaneX, kPaneY + 110, kPaneW, kPaneBottom - kPaneY - 110};
    rows_.draw(c, list, ui::ListPanel::kHitBase, inRows_);
    // A preview while the categories have the focus: shown, but quieter.
    if (!inRows_) c.g.fill({list.x - 4, list.y, list.w + 8, list.h}, theme::hex(theme::kBackground, 0.38f));
  }

  void drawInfo(ui::Ctx& c) {
    float x = kInfoX, y = kInfoY, w = kInfoW;
    const ui::PanelItem* f = inRows_ ? focusedRow() : nullptr;
    if (!f) {
      SettingsCategory cat = category();
      About a = categoryAbout(cat);
      ui::label(c, str::upperAscii(categoryTitle(cat)), x, y, Weight::Bold, 19, theme::white(0.5f), w);
      y += 32;
      y += ui::paragraph(c, a.heading, x, y, w, Weight::Bold, 26, theme::white(), 2) + 12;
      paragraphs(c, a.text, x, y, w, theme::white(0.68f));
      return;
    }
    if (f->preview == "captions") {
      captionPreview(c, {x, y, w, 197}, app_.store().settings());
      y += 197 + 22;
    }
    std::string value = rowValue(*f);
    std::string head = str::upperAscii(f->title) + (value.empty() ? "" : " \xc2\xb7 " + str::upperAscii(value));
    y += ui::paragraph(c, head, x, y, w, Weight::Bold, 19, theme::white(0.5f), 2) + 12;
    if (!f->enabled && !f->why.empty())
      y += ui::paragraph(c, f->why, x, y, w, Weight::Medium, 21, theme::hex(theme::kWarning), 3) + 14;
    paragraphs(c, f->about.empty() ? f->detail : f->about, x, y, w, theme::white(0.68f));
  }

  std::vector<SettingsCategory> cats_;
  int cat_ = 0;
  bool inRows_ = false;
  int pane_ = 1;  // under the pointer: 0 the categories, 1 the rows
  ui::ListPanel rows_;
  std::vector<std::string> summaries_;  // one per category
  uint32_t rebuiltAt_ = 0;
};

// ------------------------------------------------------------ quick settings

ui::PanelItem quickToggle(App& app, std::string title, std::string detail, bool Settings::*field) {
  ui::PanelItem it{ui::PanelItem::Kind::Toggle, std::move(title)};
  it.detail = std::move(detail);
  it.on = app.store().settings().*field;
  it.select = [&app, field] {
    Settings n = app.store().settings();
    n.*field = !(n.*field);
    app.store().setSettings(n);
  };
  return it;
}

ui::PanelItem quickChoice(std::string title, std::vector<std::string> choices, int on, std::function<void(int)> pick) {
  ui::PanelItem it{ui::PanelItem::Kind::Choice, std::move(title)};
  it.choices = std::move(choices);
  it.choice = on;
  it.pick = std::move(pick);
  return it;
}

void quickCaptions(App& app, const std::function<void(Settings&)>& edit) {
  Settings s = app.store().settings();
  edit(s);
  app.store().setSettings(s);
  app.playback().subtitlesChanged();
}

void playerRows(App& app, ui::ListPanel& p) {
  Playback& pb = app.playback();
  const Settings& set = app.store().settings();
  SubtitleChoice now = pb.subtitleChoice();
  ui::PanelItem subs = quickChoice("Subtitles", {"Off", "On"}, now.on ? 1 : 0, [&app](int k) {
    SubtitleChoice c = app.playback().subtitleChoice();
    c.on = k == 1;
    app.playback().setSubtitleChoice(c);
  });
  subs.detail = pb.spanishChannel() ? "Spanish-language channels" : "English-language channels";
  p.items.push_back(subs);
  p.items.push_back(quickChoice("Caption size", {"S", "M", "L", "XL"}, std::clamp(set.subtitleSize, 0, 3),
                                [&app](int k) { quickCaptions(app, [k](Settings& s) { s.subtitleSize = k; }); }));
  p.items.push_back(quickChoice("Caption look", {"Box", "Outline", "Shadow"}, std::clamp(set.subtitleLook, 0, 2),
                                [&app](int k) { quickCaptions(app, [k](Settings& s) { s.subtitleLook = k; }); }));
  // The channel's other version, when the provider has both (as Sources'
  // Version): switched in place.
  auto ch = app.store().channels();
  const GuideChannel* g = ch->find(pb.entry());
  const GuideChannel* other = g && !g->partner.empty() ? ch->find(g->partner) : nullptr;
  if (g && other) {
    std::string otherId = other->id;
    bool west = g->west;
    ui::PanelItem version = quickChoice("Version", {"East", "West"}, west ? 1 : 0, [&app, otherId, west](int k) {
      if ((k == 1) == west) return;
      if (!app.playback().tune(otherId)) {
        const GuideChannel* v = app.store().channels()->find(otherId);
        app.toast((v ? v->name : std::string("That version")) + " has no working feed right now");
      }
    });
    version.detail = "Switches this channel in place";
    p.items.push_back(version);
  }
}

void guideQuickRows(App& app, ui::ListPanel& p) {
  p.items.push_back(quickToggle(app, "Preview channels", "Rest on a channel to hear it in the corner",
                                &Settings::previewChannels));
  p.items.push_back(quickToggle(app, "All Channels tab", "Every channel in your countries", &Settings::allChannelsTab));
  ui::PanelItem edit{ui::PanelItem::Kind::Chevron, "Edit My Lineup"};
  edit.detail = "Added by you, removed by you, the whole list";
  edit.select = [&app] { app.nav().push(makeEditLineup(app)); };
  if (!app.store().channels()->lineupConfigured) {
    edit.enabled = false;
    edit.why = "Set up My Lineup first";
  }
  p.items.push_back(edit);
}

void eventsQuickRows(App& app, ui::ListPanel& p) {
  bool schedule = app.store().settings().scheduleEnabled;
  auto needsSchedule = [schedule](ui::PanelItem& it) {
    if (schedule) return;
    it.enabled = false;
    it.why = "Turn on Game schedule in All settings";
  };
  ui::PanelItem scores =
      quickToggle(app, "Show scores", "Off keeps live and final, without numbers", &Settings::showScores);
  needsSchedule(scores);
  p.items.push_back(scores);
  ui::PanelItem unwatchable =
      quickToggle(app, "Games not on my channels", "Dimmed at the end of each row", &Settings::showUnwatchable);
  needsSchedule(unwatchable);
  p.items.push_back(unwatchable);
  const sports::MySportsPrefs& my = app.store().mySports();
  ui::PanelItem sports{ui::PanelItem::Kind::Chevron, "My Sports"};
  sports.detail = "Leagues and favorite teams";
  sports.value = !my.configured ? std::string("Not set")
                                : std::to_string(my.leagues.size()) + (my.leagues.size() == 1 ? " league" : " leagues");
  sports.select = [&app] { app.nav().push(makeMySports(app)); };
  p.items.push_back(sports);
  ui::PanelItem refresh{ui::PanelItem::Kind::Action, "Refresh schedule"};
  refresh.select = [&app] {
    app.store().refreshSchedule();
    app.toast("Refreshing the schedule");
  };
  needsSchedule(refresh);
  p.items.push_back(refresh);
}

class GettingAround : public Screen {
 public:
  explicit GettingAround(App& app) : Screen(app) {}
  bool opaque() const override { return false; }
  bool key(const KeyEvent& e) override {
    if (e.key == Key::Ok || e.key == Key::Back) app_.nav().pop();
    return true;
  }
  void draw(ui::Ctx& c) override { drawGettingAround(c, app_.remoteKeys(), true, kHitTips); }
};

}  // namespace

void openAllSettings(App& app, std::optional<SettingsCategory> inside) {
  app.nav().push(std::make_unique<AllSettings>(app, inside));
}

std::unique_ptr<Screen> makeQuickSettings(App& app, QuickPlace place) {
  auto panel = std::make_unique<PanelScreen>(app, PanelScreen::Side::Trailing, 760.0f, [&app, place](ui::ListPanel& p) {
    switch (place) {
      case QuickPlace::Player: {
        const GuideChannel* g = app.store().channels()->find(app.playback().entry());
        p.subtitle = "QUICK \xc2\xb7 " + (g ? g->name : std::string("WATCHING"));
        playerRows(app, p);
        break;
      }
      case QuickPlace::Guide:
        p.subtitle = "QUICK \xc2\xb7 GUIDE";
        guideQuickRows(app, p);
        break;
      case QuickPlace::LiveEvents:
        p.subtitle = "QUICK \xc2\xb7 LIVE EVENTS";
        eventsQuickRows(app, p);
        break;
    }
    p.title = "Quick settings";
    p.items.push_back(ui::PanelItem::header(""));
    // Both close this panel first: Back from All settings returns to the
    // picture or the board.
    ui::PanelItem all{ui::PanelItem::Kind::Chevron, "All settings"};
    all.detail = "Everything else, by category";
    all.select = [&app] {
      app.nav().pop();
      openAllSettings(app);
    };
    p.items.push_back(all);
    ui::PanelItem help{ui::PanelItem::Kind::Chevron, "Help & info"};
    help.detail = "Getting around, reports, version";
    help.select = [&app] {
      app.nav().pop();
      openAllSettings(app, SettingsCategory::Help);
    };
    p.items.push_back(help);
    p.hint = app.remoteKeys().colour ? "Blue key or Back closes" : "Back closes";
  });
  App* a = &app;
  panel->onKey = [a](const KeyEvent& e) {
    if (e.key != Key::Blue) return false;
    a->nav().pop();
    return true;
  };
  return panel;
}

std::unique_ptr<Screen> makeGettingAround(App& app) { return std::make_unique<GettingAround>(app); }

void drawGettingAround(ui::Ctx& c, const RemoteKeys& keys, bool again, int hitOk) {
  ui::scrim(c, 0.55f);
  Rect card{460, 150, 1000, 780};
  c.g.fill(card, theme::hex(0x1C1F25, 0.97f), 34);
  float x = card.x + 64, y = card.y + 56;
  ui::label(c, "WATCHING LIVE TV", x, y, Weight::Bold, 18, theme::white(0.5f));
  ui::label(c, "Getting around", x, y + 30, Weight::Bold, 44, theme::white());
  y += 120;
  std::vector<hints::Tip> tips = hints::gettingAround(keys);
  float colW = (card.w - 128 - 40) / 2;
  for (size_t i = 0; i < tips.size() && i < 6; ++i) {
    float tx = x + static_cast<float>(i % 2) * (colW + 40), ty = y + static_cast<float>(i / 2) * 96;
    Rect cap{tx, ty, 56, 56};
    c.g.fill(cap, theme::white(0.12f), 14);
    const hints::Hint& key = tips[i].key;
    unsigned icon = !key.icons.empty() && c.icons ? c.icons->get(key.icons.front()) : 0;
    if (key.colour != hints::Colour::None)
      c.g.fill({cap.x + 16, cap.y + 16, 24, 24}, theme::hex(ui::keyColour(key.colour)), 12);
    else if (icon) c.g.textureAlpha(icon, {cap.x + 14, cap.y + 14, 28, 28}, theme::white());
    else ui::labelIn(c, key.cap, cap, Weight::Bold, 18, theme::white(), Align::Center);
    ui::label(c, tips[i].title, tx + 74, ty + 4, Weight::Bold, 25, theme::white(), colW - 74);
    ui::label(c, tips[i].detail, tx + 74, ty + 36, Weight::Regular, 19, theme::white(0.65f), colW - 74);
  }
  y += 3 * 96 + 10;
  ui::paragraph(c,
                again ? "If a feed stops, a backup takes over by itself. The keys are also listed under the banner "
                        "whenever it's up."
                      : "If a feed stops, a backup takes over by itself. This shows once (Settings \xe2\x80\xba Help & "
                        "info shows it again); the keys are listed under the banner whenever it's up.",
                x, y, card.w - 128, Weight::Regular, 20, theme::white(0.6f), 2);
  Rect ok{x, card.y + card.h - 130, ui::chipWidth(c, "Got it", 24) + 60, 68};
  c.hit(ok, hitOk);
  c.g.fill(ok, theme::white(), 34);
  ui::labelIn(c, "Got it", ok, Weight::Bold, 24, theme::black(), Align::Center);
}

}  // namespace marigold::app
