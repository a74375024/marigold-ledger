// My Lineup setup (LineupSetupView in tvOS), one screen per step:
//   1. Area: "Where do you watch?": TV markets, so their local stations join;
//   2. Languages: the countries and languages channels are kept from;
//   3. Channels: kinds of channels, "Only channels with a guide";
// then "My Lineup is ready" with its kinds of channels (never how many). In
// first-run setup these are steps 2-4 of 7 and Channels carries on to My
// Sports instead. Nothing is saved until the last step. Settings › My
// Lineup opens one step on its own (makeLineupStep), which saves as you go.
#include <algorithm>
#include <map>
#include <set>

#include "app/app.hpp"
#include "app/screens/screens.hpp"
#include "app/screens/setup_ui.hpp"
#include "app/store/store.hpp"
#include "app/ui/panel.hpp"
#include "core/catalog/lineup.hpp"
#include "core/util/strings.hpp"

namespace marigold::app {
namespace {

using ui::Align;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;
using setup_ui::kLeft;

constexpr float kLeftW = 760;
constexpr float kListX = 960;
constexpr int kHitButton = 10;  // + index

class LineupSetup : public Screen {
 public:
  enum class Step { Market, Languages, Channels, Summary };

  LineupSetup(App& app, bool onboarding, std::optional<Step> single = std::nullopt)
      : Screen(app), onboarding_(onboarding), single_(single.has_value()), draft_(app.store().lineupPrefs()) {
    for (const catalog::MarketChoice& m : draft_.markets) selected_.push_back(m.name);
    initial_ = draft_;
    if (single) step_ = *single;
    std::weak_ptr<bool> alive = alive_;
    app_.store().loadMarkets([this, alive](TvMarkets markets) {
      if (alive.expired()) return;
      markets_ = std::move(markets);
      loading_ = false;
      // First run: the area the channel names point to most, picked. With
      // no names yet (the list still downloading), the suggestion's place
      // is kept and it comes in later.
      suggestionPending_ = markets_.suggested.empty() && app_.store().channels()->all.empty() &&
                           app_.store().status().received == 0;
      if (onboarding_ && selected_.empty() && !markets_.suggested.empty()) selected_.push_back(markets_.suggested.front());
      build();
    });
    build();
    list_.focusFirst();
  }

  bool key(const KeyEvent& e) override {
    if (busy_) return true;
    int n = static_cast<int>(buttonTitles().size());
    if (int dir = setup_ui::letterStep(e.key); dir != 0 && step_ == Step::Market) {
      jumpLetter(dir);
      return true;
    }
    switch (e.key) {
      case Key::Left:
        if (focusList_) {
          focusList_ = false;
          button_ = 0;
        } else if (button_ > 0) {
          --button_;
        }
        return true;
      case Key::Right:
        if (!focusList_) {
          if (button_ + 1 < n) ++button_;
          else focusList_ = true;
        }
        return true;
      case Key::Up:
      case Key::Down:
        if (focusList_) return list_.key(e.key);
        return true;
      case Key::Ok:
        if (step_ == Step::Summary) {
          press(0);
        } else if (focusList_) {
          list_.key(Key::Ok);
          if (single_) save();
          build();
        } else {
          press(button_);
        }
        return true;
      case Key::Back:
        if (app_.keyboardOpen()) {
          app_.closeKeyboard();
          return true;
        }
        if (!single_ && (step_ == Step::Channels || step_ == Step::Languages)) {
          go(step_ == Step::Channels ? Step::Languages : Step::Market);
          return true;
        }
        return false;
      default:
        return true;
    }
  }

  void text(const std::string& value, bool, bool) override {
    if (!searching_) return;
    query_ = value;
    build();
  }

  void hover(int id) override {
    if (id >= kHitButton && id < kHitButton + 4) {
      focusList_ = false;
      button_ = id - kHitButton;
    } else {
      focusList_ = true;
      list_.hover(id);
    }
  }

  bool tick(uint32_t) override {
    const StoreStatus& status = app_.store().status();
    // The area suggestion, once the downloaded names are in.
    if (suggestionPending_ && !suggestionAsked_ &&
        (status.received > 0 || !app_.store().channels()->all.empty())) {
      suggestionAsked_ = true;
      std::weak_ptr<bool> alive = alive_;
      app_.store().loadMarkets([this, alive](TvMarkets markets) {
        if (alive.expired()) return;
        suggestionPending_ = false;
        markets_.suggested = std::move(markets.suggested);
        // Picked for the viewer only if they haven't picked or changed one.
        if (onboarding_ && selected_.empty() && !touched_ && !markets_.suggested.empty())
          selected_.push_back(markets_.suggested.front());
        if (step_ == Step::Market) rebuildKeepingFocus();
        app_.invalidate();
      });
    }
    // Countries and channels need the sorted list: they fill in when it's in.
    if (waitingForList_ && !waiting()) {
      waitingForList_ = false;
      rebuildKeepingFocus();
      app_.invalidate();
    }
    if (waiting()) {
      waitingForList_ = true;
      return true;
    }
    if (step_ == Step::Summary && !summaryReady_) {
      // The lineup is worked out on a worker; the summary shows meanwhile,
      // its kinds as placeholders, and waits for it to start watching.
      auto ch = app_.store().channels();
      if (ch->version != builtFrom_ && ch->lineupConfigured) {
        summaryReady_ = true;
        app_.invalidate();
        return true;
      }
      return ui::skeletonMoved(app_.ms(), pulseStep_);
    }
    return busy_ || loading_;
  }

  void draw(ui::Ctx& c) override {
    c.g.fill({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, theme::hex(theme::kBackground));
    if (step_ == Step::Summary) {
      drawSummary(c);
      return;
    }
    float y = setup_ui::heading(c, kicker(), heading(), lead(), kLeftW);
    setup_ui::buttons(c, kLeft, y, buttonTitles(), focusList_ ? -1 : button_, kHitButton);
    if (busy_) ui::spinner(c, kLeft + 420, y + 35, 14);
    y += 110;
    y += ui::paragraph(c, note(), kLeft, y, kLeftW, Weight::Regular, 22, theme::white(0.55f), 4) + 20;
    if (step_ == Step::Market) {
      for (const std::string& name : selected_) {
        if (y > bottom() - 40) break;
        ui::label(c, "\xe2\x9c\x93  " + name, kLeft, y, Weight::Medium, 22, theme::white(), kLeftW);
        y += 36;
      }
    }
    if (!problem_.empty())
      ui::label(c, problem_, kLeft, bottom() - 30, Weight::Medium, 20, theme::hex(theme::kWarning), kLeftW);

    Rect listArea{kListX, 70, gfx::kLayoutWidth - kListX - 80, bottom() - 70};
    if (waiting()) {
      // Still sorting (a slow TV): its progress where the list goes.
      c.g.fill(listArea, theme::white(0.04f), 24);
      setup_ui::building(c, listArea, app_.store().loadProgress(), building_, false);
    } else {
      list_.draw(c, listArea, ui::ListPanel::kHitBase, focusList_);
    }
    if (onboarding_) setup_ui::stepBar(c, stepOf());
    setup_ui::keys(c, app_.remoteKeys(), step_ == Step::Market && focusList_);
  }

 private:
  float bottom() const { return onboarding_ ? setup_ui::kContentBottom : 990; }

  setup_ui::Step stepOf() const {
    return step_ == Step::Market ? setup_ui::Step::Area
           : step_ == Step::Languages ? setup_ui::Step::Languages
                                      : setup_ui::Step::Channels;
  }

  std::string kicker() const {
    if (onboarding_) return setup_ui::kicker(stepOf());
    if (single_) return "SETTINGS \xc2\xb7 MY LINEUP";
    int n = step_ == Step::Market ? 1 : step_ == Step::Languages ? 2 : 3;
    return "MY LINEUP \xc2\xb7 STEP " + std::to_string(n) + " OF 3";
  }

  std::string heading() const {
    if (single_) return step_ == Step::Market ? "Your area" : step_ == Step::Languages ? "Countries & languages"
                                                                                      : "Kinds of channels";
    switch (step_) {
      case Step::Market: return "Where do you watch?";
      case Step::Languages: return "Countries & languages";
      default: return "What kinds of channels?";
    }
  }

  std::string lead() const {
    switch (step_) {
      case Step::Market:
        return "Your local stations (ABC, CBS, NBC, FOX, PBS\xe2\x80\xa6) and regional sports join My Lineup. East or West "
               "feeds follow the area's time zone.";
      case Step::Languages:
        return "Which channels My Lineup, All Channels and Live Events include. Everything else is still in Search.";
      default:
        return "My Lineup suggests these. Each channel appears once, however many copies your providers have; the extra "
               "copies become automatic backups.";
    }
  }

  std::string note() const {
    if (single_) return noteText() + " Changes save as you go.";
    return noteText();
  }

  std::string noteText() const {
    switch (step_) {
      case Step::Market:
        return "Pick more than one if you split your time between places; the first is home for Live Events. Other "
               "cities' locals stay out of the way.";
      case Step::Languages:
        return "Your area's country is picked for you. Each one you add brings its own channels and sports.";
      default: {
        catalog::Coast coast = catalog::resolveCoast(draft_);
        std::string feeds = coast == catalog::Coast::west ? "West" : coast == catalog::Coast::east ? "East" : "Both";
        return "East/West premium feeds: " + feeds + " here. Change it any time in Settings \xe2\x80\xba My Lineup.";
      }
    }
  }

  std::vector<std::string> buttonTitles() const {
    if (step_ == Step::Summary) return {"Start Watching"};
    if (single_) return {"Done"};
    if (onboarding_) return {"Continue", "Skip"};
    switch (step_) {
      case Step::Market: return {selected_.empty() ? "Skip Local Stations" : "Continue", "Not Now"};
      case Step::Languages: return {"Continue", "Back"};
      default: return {"Build My Lineup", "Back"};
    }
  }

  void go(Step s) {
    step_ = s;
    focusList_ = true;
    button_ = 0;
    build();
    list_.focusFirst();
  }

  void press(int i) {
    if (step_ == Step::Summary) {
      if (summaryReady_) app_.nav().pop();  // Start Watching waits for the lineup
      return;
    }
    if (single_) {
      app_.nav().pop();  // Done: every change is saved already
      return;
    }
    if (waiting()) return;  // Back still goes back
    bool skip = onboarding_ && i == 1;
    if (!onboarding_ && i == 1) {
      // Not Now / Back.
      if (step_ == Step::Market) app_.nav().pop();
      else go(step_ == Step::Channels ? Step::Languages : Step::Market);
      return;
    }
    switch (step_) {
      case Step::Market: {
        if (skip) selected_.clear();  // no local stations
        // Look up each picked market's stations.
        busy_ = true;
        problem_.clear();
        std::weak_ptr<bool> alive = alive_;
        app_.store().resolveMarkets(selected_, draft_, [this, alive](std::vector<catalog::MarketChoice> choices) {
          if (alive.expired()) return;
          busy_ = false;
          draft_.markets = std::move(choices);
          go(Step::Languages);
          app_.invalidate();
        });
        return;
      }
      case Step::Languages:
        if (skip) draft_.countries = initial_.countries;
        go(Step::Channels);
        return;
      case Step::Channels:
        if (skip) {
          draft_.categories = initial_.categories;
          draft_.requireGuide = initial_.requireGuide;
        }
        builtFrom_ = app_.store().channels()->version;
        app_.store().buildLineup(draft_);
        if (onboarding_) {
          // The lineup builds while the sports are picked.
          app_.nav().push(makeMySports(app_, true));
          return;
        }
        step_ = Step::Summary;
        summaryReady_ = false;
        focusList_ = false;
        button_ = 0;
        return;
      default:
        return;
    }
  }

  // A single page: the change saved at once. Areas are looked up (their
  // stations) and the suggestions built again; countries only change what
  // is included, as before; kinds of channels build the suggestions again.
  void save() {
    Store& s = app_.store();
    catalog::LineupPrefs now = s.lineupPrefs();
    switch (step_) {
      case Step::Market: {
        std::vector<std::string> names;
        for (const catalog::MarketChoice& m : now.markets) names.push_back(m.name);
        if (names == selected_) return;
        if (s.status().phase == StoreStatus::Phase::Loading) {
          // Suggestions are built from the sorted list: not while it sorts.
          app_.toast("Your channels are still loading. Try again in a moment");
          selected_ = names;
          return;
        }
        busy_ = true;
        std::weak_ptr<bool> alive = alive_;
        s.resolveMarkets(selected_, now, [this, alive](std::vector<catalog::MarketChoice> choices) {
          if (alive.expired()) return;
          busy_ = false;
          catalog::LineupPrefs p = app_.store().lineupPrefs();
          p.markets = std::move(choices);
          app_.store().buildLineup(p);
          draft_ = app_.store().lineupPrefs();
          app_.invalidate();
        });
        return;
      }
      case Step::Languages:
        if (draft_.countries == now.countries) return;
        s.setLineupCountries(draft_.countries);
        draft_ = s.lineupPrefs();
        return;
      case Step::Channels:
        if (draft_.categories == now.categories && draft_.requireGuide == now.requireGuide) return;
        now.categories = draft_.categories;
        now.requireGuide = draft_.requireGuide;
        s.buildLineup(now);
        draft_ = s.lineupPrefs();
        return;
      default:
        return;
    }
  }

  // CH + / CH - (or rewind / fast forward): the next letter of the A-to-Z
  // market list.
  void jumpLetter(int dir) {
    std::vector<std::string> names(list_.items.size());
    int all = -1;
    for (size_t i = 0; i < list_.items.size(); ++i) {
      const ui::PanelItem& it = list_.items[i];
      if (it.kind == ui::PanelItem::Kind::Header && str::startsWith(it.title, "ALL")) all = static_cast<int>(i);
      if (all >= 0 && it.kind == ui::PanelItem::Kind::Check) names[i] = it.title;
    }
    if (all < 0) return;
    focusList_ = true;
    int from = list_.focused();
    if (from < 0 || from >= static_cast<int>(names.size()) || names[static_cast<size_t>(from)].empty()) {
      list_.focusIndex(all + 1);
      return;
    }
    list_.focusIndex(setup_ui::nextLetter(names, from, dir));
  }

  // Countries and channels while the first list still sorts.
  bool waiting() const {
    return (step_ == Step::Languages || step_ == Step::Channels) &&
           app_.store().status().phase == StoreStatus::Phase::Loading;
  }

  // The list again, the focus staying on the same row (a suggestion coming
  // in above doesn't move it).
  void rebuildKeepingFocus() {
    int was = list_.focused();
    std::string title = was >= 0 && was < static_cast<int>(list_.items.size()) ? list_.items[static_cast<size_t>(was)].title
                                                                              : std::string();
    build();
    if (title.empty()) return;
    int n = static_cast<int>(list_.items.size());
    for (int i = std::max(0, was); i < n; ++i) {
      if (list_.items[static_cast<size_t>(i)].title == title) {
        list_.focusIndex(i);
        return;
      }
    }
    for (int i = std::min(was, n - 1); i >= 0; --i) {
      if (list_.items[static_cast<size_t>(i)].title == title) {
        list_.focusIndex(i);
        return;
      }
    }
  }

  void build() {
    list_.items.clear();
    list_.title.clear();
    LineupSetup* self = this;
    App& app = app_;
    if (step_ == Step::Market) {
      ui::PanelItem search{ui::PanelItem::Kind::Action, query_.empty() ? "Search: city or market" : "Search: " + query_};
      search.select = [self, &app] {
        self->searching_ = true;
        if (!app.openKeyboard(self->query_)) app.toast("This TV has no on-screen keyboard");
      };
      list_.items.push_back(search);
      if (loading_) {
        list_.items.push_back(ui::PanelItem::note("Loading TV markets\xe2\x80\xa6"));
      } else if (!markets_.problem.empty()) {
        list_.items.push_back(ui::PanelItem::note(markets_.problem));
      }
      auto row = [&](const std::string& name) {
        ui::PanelItem it{ui::PanelItem::Kind::Check, name};
        it.on = std::find(selected_.begin(), selected_.end(), name) != selected_.end();
        it.select = [self, name] {
          auto& sel = self->selected_;
          self->touched_ = true;
          auto f = std::find(sel.begin(), sel.end(), name);
          if (f != sel.end()) sel.erase(f);
          else sel.push_back(name);
        };
        list_.items.push_back(it);
      };
      std::string q = str::lowerAscii(std::string(str::trim(query_)));
      if (q.empty() && !markets_.suggested.empty()) {
        list_.items.push_back(ui::PanelItem::header("SUGGESTED FROM YOUR CHANNELS"));
        for (const std::string& name : markets_.suggested) row(name);
      } else if (q.empty() && suggestionPending_) {
        list_.items.push_back(ui::PanelItem::header("SUGGESTED FROM YOUR CHANNELS"));
        list_.items.push_back(ui::PanelItem::note("Looking at your channels\xe2\x80\xa6"));
      }
      std::vector<std::string> shown;
      for (const std::string& name : markets_.all)
        if (q.empty() || str::lowerAscii(name).find(q) != std::string::npos) shown.push_back(name);
      // A to Z, so the letter keys can jump.
      std::sort(shown.begin(), shown.end(),
                [](const std::string& a, const std::string& b) { return str::lowerAscii(a) < str::lowerAscii(b); });
      if (!markets_.all.empty()) {
        list_.items.push_back(ui::PanelItem::header("ALL AREAS, A TO Z (" + std::to_string(shown.size()) + ")"));
        for (const std::string& name : shown) row(name);
      }
    } else if (step_ == Step::Languages) {
      Store& s = app_.store();
      list_.items.push_back(ui::PanelItem::header("IN YOUR CHANNEL LIST"));
      for (const MarketSummary& m : s.countries(draft_.countries)) {
        ui::PanelItem it{ui::PanelItem::Kind::Check, m.name};
        it.on = m.on;
        std::string code = m.code;
        it.select = [self, code] {
          // At least one stays on.
          auto& set = self->draft_.countries;
          if (set.count(code)) {
            if (set.size() > 1) set.erase(code);
            else if (self->single_) self->app_.toast("Keep at least one");
          } else {
            set.insert(code);
          }
        };
        list_.items.push_back(it);
      }
    } else if (step_ == Step::Channels) {
      Store& s = app_.store();
      list_.items.push_back(ui::PanelItem::header("KINDS OF CHANNELS"));
      for (const CategorySummary& cat : s.categories(draft_)) {
        ui::PanelItem it{ui::PanelItem::Kind::Toggle, cat.title};
        it.detail = cat.detail;  // never how many channels
        it.on = cat.on;
        auto category = catalog::categoryFromRaw(cat.key);
        it.select = [self, category] {
          if (!category) return;
          auto& set = self->draft_.categories;
          if (!set.erase(*category)) set.insert(*category);
        };
        list_.items.push_back(it);
      }
      list_.items.push_back(ui::PanelItem::header(""));
      ui::PanelItem guide{ui::PanelItem::Kind::Toggle, "Only channels with a guide"};
      guide.detail = "Leaves out channels nobody publishes listings for (local stations and your area's sports channels are always included)";
      guide.on = draft_.requireGuide;
      guide.select = [self] { self->draft_.requireGuide = !self->draft_.requireGuide; };
      list_.items.push_back(guide);
    }
    list_.settle();
  }

  void drawSummary(ui::Ctx& c) {
    if (!summaryReady_) {
      drawSummaryLoading(c);
      return;
    }
    auto ch = app_.store().channels();
    float x = 110, y = 140;
    ui::label(c, "My Lineup is ready", x, y, Weight::Bold, 56, theme::white());
    y += 90;
    ui::paragraph(c,
                  "Your channels, in one short guide. Press the green key on any channel to add or remove it; "
                  "Settings \xe2\x80\xba My Lineup changes the rest.",
                  x, y, 1500, Weight::Regular, 26, theme::white(0.75f), 3);
    y += 120;
    // Its kinds of channels, in guide order (never how many).
    std::vector<std::string> sections;
    for (int i : ch->lineup) {
      const std::string& s = ch->all[static_cast<size_t>(i)].section;
      if (sections.empty() || sections.back() != s) sections.push_back(s);
    }
    float tx = x;
    for (const std::string& title : sections) {
      Rect r{tx, y, 150, 150};
      c.g.fill(r, theme::white(0.08f), 18);
      ui::label(c, "\xe2\x9c\x93", r.x, r.y + 30, Weight::Bold, 30, theme::hex(theme::kToggleOn), r.w, Align::Center);
      ui::paragraph(c, title, r.x + 10, r.y + 86, r.w - 20, Weight::Medium, 18, theme::white(0.85f), 2);
      tx += 168;
      if (tx + 150 > gfx::kLayoutWidth - 80) break;
    }
    setup_ui::buttons(c, x, y + 210, buttonTitles(), 0, kHitButton);
  }

  // The summary while the lineup is worked out: the same layout, its kinds
  // as pulsing placeholders, a quiet line, Start Watching dimmed.
  void drawSummaryLoading(ui::Ctx& c) {
    float x = 110, y = 140;
    ui::label(c, "Building My Lineup", x, y, Weight::Bold, 56, theme::white());
    y += 90;
    ui::Color block = ui::skeleton(c);
    c.g.fill({x, y + 6, 900, 24}, block, 12);
    c.g.fill({x, y + 46, 620, 24}, block, 12);
    y += 120;
    float tx = x;
    for (int i = 0; i < 6; ++i) {
      Rect r{tx, y, 150, 150};
      c.g.fill(r, theme::white(0.06f), 18);
      c.g.fill({r.x + 45, r.y + 38, 60, 34}, block, 10);
      c.g.fill({r.x + 25, r.y + 96, 100, 16}, block, 8);
      tx += 168;
    }
    float by = y + 210;
    float bw = ui::chipWidth(c, "Start Watching", 24) + 60;
    c.g.fill({x, by, bw, 70}, theme::white(0.12f), 35);
    ui::labelIn(c, "Start Watching", {x, by, bw, 70}, Weight::Bold, 24, theme::white(0.4f), Align::Center);
    ui::spinner(c, x + bw + 40, by + 35, 10);
    ui::label(c, "Working out your channels\xe2\x80\xa6", x + bw + 62, by + 23, Weight::Regular, 20, theme::white(0.6f));
  }

  bool onboarding_;
  bool single_;  // one step on its own, saved as you go
  catalog::LineupPrefs draft_, initial_;
  Step step_ = Step::Market;
  TvMarkets markets_;
  bool loading_ = true;
  bool suggestionPending_ = false, suggestionAsked_ = false;
  bool touched_ = false;         // the viewer picked or unpicked an area
  bool waitingForList_ = false;  // countries or channels wait for the sorted list
  setup_ui::Building building_;
  bool summaryReady_ = true;  // the summary's lineup is in
  uint32_t pulseStep_ = 0;
  bool busy_ = false;
  bool searching_ = false;
  std::string query_;
  std::string problem_;
  std::vector<std::string> selected_;
  bool focusList_ = true;
  int button_ = 0;
  uint64_t builtFrom_ = 0;
  ui::ListPanel list_;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace

std::unique_ptr<Screen> makeLineupSetup(App& app, bool onboarding) {
  return std::make_unique<LineupSetup>(app, onboarding);
}

std::unique_ptr<Screen> makeLineupStep(App& app, LineupStep step) {
  using Step = LineupSetup::Step;
  Step s = step == LineupStep::Area ? Step::Market : step == LineupStep::Countries ? Step::Languages : Step::Channels;
  return std::make_unique<LineupSetup>(app, false, s);
}

}  // namespace marigold::app
