// My Lineup setup (LineupSetupView): step 1 countries and languages, step 2
// the kinds of channels, then the summary. Nothing is saved until Build.
#include <set>

#include "app/app.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"
#include "app/ui/panel.hpp"

namespace marigold::app {
namespace {

using ui::Align;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;

class LineupSetup : public Screen {
 public:
  explicit LineupSetup(App& app) : Screen(app) {
    for (const MarketSummary& m : app.store().markets())
      if (m.on) countries_.insert(m.code);
    if (countries_.empty()) countries_.insert("US");
    for (const CategorySummary& c : app.store().categories(countries_, requireGuide_))
      if (c.on) categories_.insert(c.key);
    build();
    list_.focusFirst();
  }

  bool key(const KeyEvent& e) override {
    if (e.key == Key::Back) {
      if (step_ == 2) {
        step_ = 1;
        build();
        list_.focusFirst();
        return true;
      }
      return false;
    }
    if (e.key == Key::Ok) {
      list_.key(e.key);
      build();
      return true;
    }
    return list_.key(e.key);
  }

  void hover(int id) override { list_.hover(id); }

  void draw(ui::Ctx& c) override {
    float x = 120, y = 80;
    const char* steps[] = {"", "STEP 1 OF 2", "STEP 2 OF 2", "DONE"};
    ui::label(c, steps[step_], x, y, Weight::Bold, 18, theme::white(0.55f));
    y += 40;
    const char* titles[] = {"", "Where do you watch from?", "What do you want in My Lineup?", "My Lineup is ready"};
    ui::label(c, titles[step_], x, y, Weight::Bold, 52, theme::white());
    y += 80;
    const char* text[] = {"",
                          "Pick the countries and languages whose channels you want. Channels from the "
                          "others stay in All Channels and search.",
                          "Turn on the kinds of channels to suggest. You can add or remove any channel "
                          "later with the green key.",
                          ""};
    if (step_ == 3) {
      auto ch = app_.store().channels();
      ui::paragraph(c, std::to_string(ch->lineup.size()) + " channels, down from " + std::to_string(ch->rawChannels) + ".",
                    x, y, 620, Weight::Regular, 26, theme::white(0.7f), 3);
    } else {
      ui::paragraph(c, text[step_], x, y, 620, Weight::Regular, 24, theme::white(0.65f), 5);
    }
    list_.draw(c, {820, 80, 980, gfx::kLayoutHeight - 140});
  }

 private:
  void build() {
    list_.items.clear();
    Store& s = app_.store();
    LineupSetup* self = this;
    if (step_ == 1) {
      list_.title.clear();
      for (const MarketSummary& m : s.markets()) {
        ui::PanelItem it{ui::PanelItem::Kind::Check, m.name};
        it.value = std::to_string(m.count);
        it.on = countries_.count(m.code) > 0;
        std::string code = m.code;
        it.select = [self, code] {
          // At least one stays on.
          if (self->countries_.count(code)) {
            if (self->countries_.size() > 1) self->countries_.erase(code);
          } else {
            self->countries_.insert(code);
          }
        };
        list_.items.push_back(it);
      }
      ui::PanelItem next{ui::PanelItem::Kind::Chevron, "Continue"};
      next.select = [self] {
        self->step_ = 2;
        self->pendingFocusFirst_ = true;
      };
      list_.items.push_back(next);
    } else if (step_ == 2) {
      for (const CategorySummary& cat : s.categories(countries_, requireGuide_)) {
        if (cat.total == 0) continue;
        ui::PanelItem it{ui::PanelItem::Kind::Toggle, cat.title};
        it.detail = std::to_string(cat.suggested) + " of " + std::to_string(cat.total) + " \xc2\xb7 " + cat.detail;
        it.on = categories_.count(cat.key) > 0;
        std::string key = cat.key;
        it.select = [self, key] {
          if (!self->categories_.erase(key)) self->categories_.insert(key);
        };
        list_.items.push_back(it);
      }
      ui::PanelItem guide{ui::PanelItem::Kind::Toggle, "Only channels with a guide"};
      guide.detail = "National channels join only when they have listings";
      guide.on = requireGuide_;
      guide.select = [self] { self->requireGuide_ = !self->requireGuide_; };
      list_.items.push_back(guide);
      ui::PanelItem build{ui::PanelItem::Kind::Action, "Build My Lineup"};
      App& app = app_;
      build.select = [self, &app] {
        app.store().buildLineup(self->countries_, self->categories_, self->requireGuide_);
        self->step_ = 3;
        self->pendingFocusFirst_ = true;
      };
      list_.items.push_back(build);
    } else {
      ui::PanelItem done{ui::PanelItem::Kind::Action, "Start Watching"};
      App& app = app_;
      done.select = [&app] { app.nav().pop(); };
      list_.items.push_back(done);
    }
    if (pendingFocusFirst_) {
      pendingFocusFirst_ = false;
      build();
      list_.focusFirst();
      return;
    }
    list_.settle();
  }

  int step_ = 1;
  bool pendingFocusFirst_ = false;
  std::set<std::string> countries_, categories_;
  bool requireGuide_ = true;
  ui::ListPanel list_;
};

}  // namespace

std::unique_ptr<Screen> makeLineupSetup(App& app) { return std::make_unique<LineupSetup>(app); }

}  // namespace marigold::app
