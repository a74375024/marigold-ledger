// My Lineup setup (LineupSetupView in tvOS):
//   1. "Where do you watch?": TV markets, so their local stations join;
//   2. "What do you want in My Lineup?": countries and languages, kinds of
//      channels, "Only channels with a guide";
//   3. "My Lineup is ready": the counts per kind.
// Nothing is saved until Build My Lineup.
#include <algorithm>
#include <map>
#include <set>

#include "app/app.hpp"
#include "app/screens/screens.hpp"
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

constexpr float kLeftX = 80, kLeftW = 620;
constexpr float kListX = 760;
constexpr int kHitButton = 10;  // + index

class LineupSetup : public Screen {
 public:
  explicit LineupSetup(App& app) : Screen(app), draft_(app.store().lineupPrefs()) {
    for (const catalog::MarketChoice& m : draft_.markets) selected_.push_back(m.name);
    std::weak_ptr<bool> alive = alive_;
    app_.store().loadMarkets([this, alive](TvMarkets markets) {
      if (alive.expired()) return;
      markets_ = std::move(markets);
      loading_ = false;
      build();
    });
    build();
    list_.focusFirst();
  }

  bool key(const KeyEvent& e) override {
    if (busy_ || step_ == Step::Building) return true;
    switch (e.key) {
      case Key::Left:
        if (focusList_) {
          focusList_ = false;
          button_ = 0;
        }
        return true;
      case Key::Right:
        if (!focusList_) focusList_ = true;
        return true;
      case Key::Up:
      case Key::Down:
        if (focusList_) return list_.key(e.key);
        button_ = std::clamp(button_ + (e.key == Key::Up ? -1 : 1), 0, buttonCount() - 1);
        return true;
      case Key::Ok:
        if (focusList_) {
          list_.key(Key::Ok);
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
        if (step_ == Step::Categories) {
          step_ = Step::Market;
          focusList_ = true;
          build();
          list_.focusFirst();
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
    if (step_ == Step::Building) {
      // The lineup is worked out on a worker; the summary waits for it.
      auto ch = app_.store().channels();
      if (ch->version != builtFrom_ && ch->lineupConfigured) {
        step_ = Step::Summary;
        focusList_ = false;
        button_ = 0;
        app_.invalidate();
      }
      return true;
    }
    return busy_ || loading_;
  }

  void draw(ui::Ctx& c) override {
    c.g.fill({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, theme::black());
    if (step_ == Step::Building) {
      ui::spinner(c, gfx::kLayoutWidth * 0.5f, 430, 30);
      ui::label(c, "Building My Lineup\xe2\x80\xa6", 0, 500, Weight::Bold, 44, theme::white(), gfx::kLayoutWidth,
                Align::Center);
      return;
    }
    if (step_ == Step::Summary) {
      drawSummary(c);
      return;
    }
    float y = 80;
    ui::label(c, step_ == Step::Market ? "STEP 1 OF 2" : "STEP 2 OF 2", kLeftX, y, Weight::Bold, 18, theme::white(0.5f));
    y += 40;
    y += ui::paragraph(c, step_ == Step::Market ? "Where do you watch?" : "What do you want in My Lineup?", kLeftX, y,
                       kLeftW, Weight::Bold, 52, theme::white(), 2, 1.2f) + 20;
    const char* detail = step_ == Step::Market
                             ? "Pick your TV market to keep its local stations (ABC, CBS, NBC, FOX, PBS\xe2\x80\xa6) in My "
                               "Lineup. Other cities' locals stay out of the way. You can pick more than one."
                             : "Each channel appears once, however many copies your providers have; the extra copies "
                               "become automatic backups. Everything you leave out stays available under All Channels "
                               "and Search.";
    y += ui::paragraph(c, detail, kLeftX, y, kLeftW, Weight::Regular, 24, theme::white(0.7f), 6) + 24;
    if (step_ == Step::Market) {
      for (const std::string& name : selected_) {
        ui::label(c, "\xe2\x9c\x93  " + name, kLeftX, y, Weight::Medium, 22, theme::white(), kLeftW);
        y += 36;
        if (y > 760) break;
      }
    }
    // Buttons at the bottom of the left column.
    float by = gfx::kLayoutHeight - 80 - static_cast<float>(buttonCount()) * 86;
    for (int i = 0; i < buttonCount(); ++i) {
      std::string title = buttonTitle(i);
      bool f = !focusList_ && button_ == i;
      Rect r{kLeftX, by, std::max(260.0f, ui::chipWidth(c, title, 26) + 60), 70};
      c.hit(r, kHitButton + i);
      c.g.fill(r, f ? theme::white() : theme::white(0.12f), 35);
      ui::labelIn(c, title, r, Weight::Bold, 26, f ? theme::black() : theme::white(), Align::Center);
      if (i == 0 && busy_) ui::spinner(c, r.x + r.w + 36, r.y + 35, 14);
      by += 86;
    }
    if (!problem_.empty()) ui::label(c, problem_, kLeftX, gfx::kLayoutHeight - 60, Weight::Medium, 20, theme::hex(theme::kWarning), kLeftW);

    list_.draw(c, {kListX, 70, gfx::kLayoutWidth - kListX - 80, gfx::kLayoutHeight - 130}, ui::ListPanel::kHitBase,
               focusList_);
  }

 private:
  enum class Step { Market, Categories, Building, Summary };

  int buttonCount() const { return step_ == Step::Summary ? 1 : 2; }
  std::string buttonTitle(int i) const {
    if (step_ == Step::Market) return i == 0 ? (selected_.empty() ? "Skip Local Stations" : "Continue") : "Not Now";
    return i == 0 ? "Build My Lineup" : "Back";
  }

  void press(int i) {
    if (step_ == Step::Summary) {
      app_.nav().pop();
      return;
    }
    if (step_ == Step::Market) {
      if (i == 1) {
        app_.nav().pop();
        return;
      }
      // Continue: look up each picked market's stations.
      busy_ = true;
      problem_.clear();
      std::weak_ptr<bool> alive = alive_;
      app_.store().resolveMarkets(selected_, draft_, [this, alive](std::vector<catalog::MarketChoice> choices) {
        if (alive.expired()) return;
        busy_ = false;
        draft_.markets = std::move(choices);
        step_ = Step::Categories;
        focusList_ = true;
        build();
        list_.focusFirst();
        app_.invalidate();
      });
      return;
    }
    if (i == 1) {
      step_ = Step::Market;
      focusList_ = true;
      build();
      list_.focusFirst();
      return;
    }
    builtFrom_ = app_.store().channels()->version;
    app_.store().buildLineup(draft_);
    step_ = Step::Building;
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
      }
      std::vector<std::string> shown;
      for (const std::string& name : markets_.all)
        if (q.empty() || str::lowerAscii(name).find(q) != std::string::npos) shown.push_back(name);
      if (!markets_.all.empty()) {
        list_.items.push_back(ui::PanelItem::header("ALL MARKETS (" + std::to_string(shown.size()) + ")"));
        for (const std::string& name : shown) row(name);
      }
    } else if (step_ == Step::Categories) {
      Store& s = app_.store();
      list_.items.push_back(ui::PanelItem::header("COUNTRIES AND LANGUAGES"));
      for (const MarketSummary& m : s.countries(draft_.countries)) {
        ui::PanelItem it{ui::PanelItem::Kind::Check, m.name};
        it.value = std::to_string(m.count);
        it.on = m.on;
        std::string code = m.code;
        it.select = [self, code] {
          // At least one stays on.
          auto& set = self->draft_.countries;
          if (set.count(code)) {
            if (set.size() > 1) set.erase(code);
          } else {
            set.insert(code);
          }
        };
        list_.items.push_back(it);
      }
      list_.items.push_back(ui::PanelItem::header("KINDS OF CHANNELS"));
      for (const CategorySummary& cat : s.categories(draft_)) {
        ui::PanelItem it{ui::PanelItem::Kind::Toggle, cat.title};
        it.detail = cat.detail;
        it.value = cat.key == "local" ? std::to_string(cat.suggested) + " stations"
                                      : std::to_string(cat.suggested) + " of " + std::to_string(cat.total);
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
    auto ch = app_.store().channels();
    float x = 110, y = 140;
    ui::label(c, "My Lineup is ready", x, y, Weight::Bold, 56, theme::white());
    y += 90;
    ui::paragraph(c,
                  std::to_string(ch->lineup.size()) + " channels, down from " + std::to_string(ch->rawChannels) +
                      ". Press the green key on any channel to add or remove it; Options \xe2\x80\xba Set Up My Lineup "
                      "changes the rest.",
                  x, y, 1500, Weight::Regular, 26, theme::white(0.75f), 3);
    y += 120;
    std::vector<std::pair<std::string, int>> bySection;
    for (int i : ch->lineup) {
      const std::string& s = ch->all[static_cast<size_t>(i)].section;
      if (bySection.empty() || bySection.back().first != s) bySection.push_back({s, 0});
      ++bySection.back().second;
    }
    float tx = x;
    for (const auto& [title, count] : bySection) {
      Rect r{tx, y, 150, 150};
      c.g.fill(r, theme::white(0.08f), 18);
      ui::label(c, std::to_string(count), r.x, r.y + 34, Weight::Bold, 34, theme::white(), r.w, Align::Center);
      ui::paragraph(c, title, r.x + 10, r.y + 92, r.w - 20, Weight::Medium, 16, theme::white(0.8f), 2);
      tx += 168;
      if (tx + 150 > gfx::kLayoutWidth - 80) break;
    }
    Rect b{x, y + 210, 320, 70};
    c.hit(b, kHitButton);
    c.g.fill(b, theme::white(), 35);
    ui::labelIn(c, "Start Watching", b, Weight::Bold, 26, theme::black(), Align::Center);
  }

  catalog::LineupPrefs draft_;
  Step step_ = Step::Market;
  TvMarkets markets_;
  bool loading_ = true;
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

std::unique_ptr<Screen> makeLineupSetup(App& app) { return std::make_unique<LineupSetup>(app); }

}  // namespace marigold::app
