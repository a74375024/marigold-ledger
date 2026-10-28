// My Sports setup (MySportsSetupView): 1. what do you watch (leagues and
// other sports, with how many events your channels carry), 2. favourite
// teams, a league at a time from the rail on the left, 3. review with
// "Remind me before my teams play". In first-run setup these are steps 5
// and 6 of 7 (the reminder switch is on the teams screen, and the summary
// follows). Nothing is saved until the last step.
#include <algorithm>
#include <map>

#include "app/app.hpp"
#include "app/screens/events_ui.hpp"
#include "app/screens/screens.hpp"
#include "app/screens/setup_ui.hpp"
#include "app/store/store.hpp"
#include "core/sports/leagues.hpp"
#include "core/sports/my_sports.hpp"

namespace marigold::app {
namespace {

using ui::Align;
using ui::Color;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;

constexpr float kLeft = 80, kTop = 50;
constexpr int kLeagueCols = 4;
constexpr float kLeagueW = 400, kLeagueH = 96, kLeagueGap = 24;
constexpr int kTeamCols = 5;
constexpr float kTeamX = 540, kTeamW = 238, kTeamH = 170, kTeamGap = 20;
constexpr float kRailW = 400;
const char* kSections[] = {"Football", "Basketball", "Baseball", "Hockey", "Soccer"};
constexpr int kHitTile = 100, kHitButton = 10, kHitTab = 20, kHitReminder = 30;
// Focus on the teams screen besides a tile (>= 0).
constexpr int kRail = -1, kButtons = -2, kReminder = -3;
// First run: this many of the busiest leagues are picked to start with.
constexpr size_t kPrePicks = 4;

struct Pick {
  std::string id, name, section;
};

class MySportsSetup : public Screen {
 public:
  MySportsSetup(App& app, bool onboarding) : Screen(app), onboarding_(onboarding), draft_(app.store().mySports()) {
    counts_ = app.store().sportSuggestions();
    // Leagues by section, then the other sports.
    for (const char* section : kSections)
      for (const sports::League& l : sports::leagues())
        if (l.sport == section) picks_.push_back({l.shortName, l.name, section});
    for (const sports::League& l : sports::leagues()) {
      bool known = std::any_of(std::begin(kSections), std::end(kSections), [&](const char* s) { return l.sport == s; });
      if (!known) picks_.push_back({l.shortName, l.name, "More sports"});
    }
    for (const sports::OtherSport& o : sports::otherSports()) picks_.push_back({o.id, o.name, "More sports"});
    if (onboarding_ && !draft_.configured && draft_.leagues.empty()) prePick();
    initialTeams_ = draft_.teams;
    button_ = nextIndex();
    layoutPicks();
  }
  ~MySportsSetup() override { app_.store().releaseRosters(); }

  bool tick(uint32_t) override {
    if (step_ == 2 && app_.store().teamsRevision() != teamsRevision_) {
      std::string keep = focus_ >= 0 && focus_ < static_cast<int>(teams_.size()) ? teams_[static_cast<size_t>(focus_)].id : "";
      loadTeams();
      if (!keep.empty())
        for (size_t i = 0; i < teams_.size(); ++i)
          if (teams_[i].id == keep) focus_ = static_cast<int>(i);
      return true;
    }
    return step_ == 2 && teams_.empty();
  }

  // Pointer: ids 100+ tiles, 10/11 the two buttons, 20+ the leagues, 30 the
  // reminders row.
  void hover(int id) override {
    if (id == kHitButton || id == kHitButton + 1) {
      button_ = id - kHitButton;
      if (step_ == 1) focus_ = static_cast<int>(picks_.size());
      else if (step_ == 2) focus_ = kButtons;
      else reviewRow_ = 1;
    } else if (step_ == 1 && id >= kHitTile && id - kHitTile < static_cast<int>(picks_.size())) {
      focus_ = id - kHitTile;
    } else if (step_ == 2 && id >= kHitTile && id - kHitTile < static_cast<int>(teams_.size())) {
      focus_ = id - kHitTile;
    } else if (step_ == 2 && id >= kHitTab && id < kHitTab + 10) {
      int tab = id - kHitTab;
      focus_ = kRail;
      if (tab != tab_ && tab < static_cast<int>(teamLeagues().size())) {
        tab_ = tab;
        loadTeams();
      }
    } else if (id == kHitReminder) {
      if (step_ == 2) focus_ = kReminder;
      else reviewRow_ = 0;
    }
  }

  bool key(const KeyEvent& e) override {
    if (step_ == 1) return leaguesKey(e);
    if (step_ == 2) return teamsKey(e);
    return reviewKey(e);
  }

  void draw(ui::Ctx& c) override {
    c.g.fill({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, theme::hex(theme::kBackground));
    const char* label[] = {"", "MY SPORTS \xc2\xb7 STEP 1 OF 3", "MY SPORTS \xc2\xb7 STEP 2 OF 3", "MY SPORTS \xc2\xb7 STEP 3 OF 3"};
    std::string kick = onboarding_ ? setup_ui::kicker(step_ == 1 ? setup_ui::Step::Sports : setup_ui::Step::Teams)
                                   : std::string(label[step_]);
    ui::label(c, kick, kLeft, kTop, Weight::Bold, 18, theme::white(0.5f));
    if (step_ == 1) drawLeagues(c);
    else if (step_ == 2) drawTeams(c);
    else drawReview(c);
    if (onboarding_) setup_ui::stepBar(c, step_ == 1 ? setup_ui::Step::Sports : setup_ui::Step::Teams);
    setup_ui::keys(c, app_.remoteKeys(), step_ == 2 && focus_ >= 0);
  }

 private:
  // Continue is the first button in first-run setup (as on its other
  // screens); Next is the second one here.
  int nextIndex() const { return onboarding_ ? 0 : 1; }
  float bottom() const { return onboarding_ ? setup_ui::kContentBottom : 990; }

  bool picked(const std::string& id) const {
    return std::find(draft_.leagues.begin(), draft_.leagues.end(), id) != draft_.leagues.end();
  }
  void togglePick(const std::string& id) {
    auto it = std::find(draft_.leagues.begin(), draft_.leagues.end(), id);
    if (it != draft_.leagues.end()) draft_.leagues.erase(it);
    else draft_.leagues.push_back(id);
  }
  // Leagues with teams to pick (real leagues, not "S:" sports).
  std::vector<std::string> teamLeagues() const {
    std::vector<std::string> out;
    for (const std::string& id : draft_.leagues) {
      const sports::League* l = sports::leagueByShort(id);
      if (l && !l->fights && !l->racing && !l->tennis) out.push_back(id);
    }
    return out;
  }

  // The busiest leagues on the user's channels, in the screen's order.
  void prePick() {
    std::vector<std::pair<int, size_t>> busy;
    for (size_t i = 0; i < picks_.size(); ++i) {
      auto n = counts_.find(picks_[i].id);
      // Never picked for you: pro wrestling is opt-in.
      if (picks_[i].id == sports::kProWrestlingPick) continue;
      if (n != counts_.end() && n->second > 0) busy.push_back({n->second, i});
    }
    std::stable_sort(busy.begin(), busy.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    if (busy.size() > kPrePicks) busy.resize(kPrePicks);
    std::sort(busy.begin(), busy.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    for (const auto& [n, i] : busy) draft_.leagues.push_back(picks_[i].id);
  }

  // The end of first-run setup's sports: saved, then the summary.
  void finishOnboarding() {
    draft_.configured = true;
    draft_.prompted = true;
    app_.store().setMySports(draft_);
    app_.nav().push(makeSetupDone(app_));
  }

  // ---------------------------------------------------------- step 1
  // Rows as drawn: kLeagueCols per row, a new row (under a heading) for
  // each section.
  void layoutPicks() {
    rows_.clear();
    rowOf_.assign(picks_.size(), 0);
    colOf_.assign(picks_.size(), 0);
    ys_.assign(picks_.size(), 0);
    std::string section;
    float cy = 0;
    for (size_t i = 0; i < picks_.size(); ++i) {
      bool newSection = i == 0 || picks_[i].section != section;
      if (newSection || rows_.back().size() == static_cast<size_t>(kLeagueCols)) {
        if (i > 0) cy += kLeagueH + kLeagueGap;
        if (newSection) cy += 44;
        section = picks_[i].section;
        rows_.emplace_back();
      }
      rowOf_[i] = static_cast<int>(rows_.size()) - 1;
      colOf_[i] = static_cast<int>(rows_.back().size());
      ys_[i] = cy;
      rows_.back().push_back(static_cast<int>(i));
    }
  }

  bool leaguesKey(const KeyEvent& e) {
    int n = static_cast<int>(picks_.size());
    if (focus_ >= n) {  // the buttons
      switch (e.key) {
        case Key::Left: button_ = 0; return true;
        case Key::Right: button_ = 1; return true;
        case Key::Up:
          if (!rows_.empty()) focus_ = rows_.back().front();
          return true;
        case Key::Ok:
          if (button_ == nextIndex()) {
            goTeams();
          } else if (onboarding_) {
            // Skip: no sports picked; Live Events won't ask again.
            app_.store().markMySportsPrompted();
            app_.nav().push(makeSetupDone(app_));
          } else {
            app_.nav().pop();
          }
          return true;
        default: return e.key != Key::Back;
      }
    }
    // Arrows follow the grid as drawn: each section starts a new row.
    size_t f = static_cast<size_t>(focus_);
    int row = rowOf_[f], col = colOf_[f];
    switch (e.key) {
      case Key::Left: if (col > 0) --focus_; return true;
      case Key::Right:
        if (col + 1 < static_cast<int>(rows_[static_cast<size_t>(row)].size())) ++focus_;
        return true;
      case Key::Up:
        if (row > 0) {
          const std::vector<int>& above = rows_[static_cast<size_t>(row - 1)];
          focus_ = above[static_cast<size_t>(std::min(col, static_cast<int>(above.size()) - 1))];
        }
        return true;
      case Key::Down:
        if (row + 1 < static_cast<int>(rows_.size())) {
          const std::vector<int>& below = rows_[static_cast<size_t>(row + 1)];
          focus_ = below[static_cast<size_t>(std::min(col, static_cast<int>(below.size()) - 1))];
        } else {
          focus_ = n;
          button_ = nextIndex();
        }
        return true;
      case Key::Ok:
        togglePick(picks_[static_cast<size_t>(focus_)].id);
        return true;
      default: return e.key != Key::Back;
    }
  }

  void goTeams() {
    std::vector<std::string> leagues = teamLeagues();
    if (leagues.empty()) {
      if (onboarding_) {
        finishOnboarding();
        return;
      }
      step_ = 3;
      button_ = 1;
      return;
    }
    app_.store().requestRosters(std::set<std::string>(leagues.begin(), leagues.end()));
    step_ = 2;
    tab_ = 0;
    focus_ = kRail;
    loadTeams();
  }

  void drawLeagues(ui::Ctx& c) {
    ui::label(c, "What do you watch?", kLeft, kTop + 34, Weight::Bold, 52, theme::white());
    ui::label(c,
              onboarding_ ? "Each pick gets its own row in Live Events, your teams lead, and the schedule is only looked "
                            "up for these. The busiest on your channels are picked for you."
                          : "Your teams lead Live Events, each pick gets a row, and the schedule is only looked up for these.",
              kLeft, kTop + 108, Weight::Regular, 22, theme::white(0.65f), 1760);
    // Grid with section headings; scrolled only when the focus would leave
    // the view (so tiles stay put under the pointer).
    float y = kTop + 160;
    float visible = bottom() - 90 - y;
    if (focus_ >= 0 && focus_ < static_cast<int>(picks_.size())) {
      float fy = ys_[static_cast<size_t>(focus_)];
      if (fy - 50 < scroll_) scroll_ = std::max(0.0f, fy - 50);
      if (fy + kLeagueH + 20 > scroll_ + visible) scroll_ = fy + kLeagueH + 20 - visible;
    }
    c.g.pushClip({0, y - 4, gfx::kLayoutWidth, visible + 8});
    std::string section;
    for (size_t i = 0; i < picks_.size(); ++i) {
      const Pick& p = picks_[i];
      if (p.section != section) {
        section = p.section;
        ui::label(c, section, kLeft, y + ys_[i] - 40 - scroll_, Weight::Bold, 20, theme::white(0.6f));
      }
      Rect r{kLeft + static_cast<float>(colOf_[i]) * (kLeagueW + kLeagueGap), y + ys_[i] - scroll_, kLeagueW, kLeagueH};
      if (r.y + r.h < y - 4 || r.y > y + visible + 4) continue;
      c.hit(r, kHitTile + static_cast<int>(i));
      bool f = static_cast<int>(i) == focus_;
      bool on = picked(p.id);
      c.g.fill(r, f ? theme::white() : theme::white(0.08f), 18);
      if (on) c.g.stroke(r, theme::hex(theme::kToggleOn), 18, 3);
      Color ink = f ? theme::black() : theme::white();
      ui::label(c, p.name, r.x + 24, r.y + 18, Weight::Bold, 25, ink, r.w - 90);
      auto n = counts_.find(p.id);
      if (n != counts_.end() && n->second > 0)
        ui::label(c, std::to_string(n->second) + " events in your channels", r.x + 24, r.y + 56, Weight::Medium, 18,
                  theme::hex(theme::kNew), r.w - 90);
      Rect check{r.x + r.w - 56, r.y + 32, 32, 32};
      if (on) {
        c.g.fill(check, theme::hex(theme::kToggleOn), 16);
        ui::labelIn(c, "\xe2\x9c\x93", check, Weight::Bold, 20, theme::white(), Align::Center);
      } else {
        c.g.stroke(check, ink.withAlpha(0.4f), 16, 2);
      }
    }
    c.g.popClip();
    std::vector<std::string> titles;
    if (onboarding_) titles = {"Continue", "Skip"};
    else titles = {"Cancel", teamLeagues().empty() ? "Next: review" : "Next: favorite teams"};
    drawButtons(c, titles, focus_ >= static_cast<int>(picks_.size()));
  }

  void drawButtons(ui::Ctx& c, const std::vector<std::string>& titles, bool focusedRow) {
    setup_ui::buttons(c, kLeft, bottom() - 70, titles, focusedRow ? button_ : -1, kHitButton);
  }

  // ---------------------------------------------------------- step 2
  void loadTeams() {
    std::vector<std::string> leagues = teamLeagues();
    teamsRevision_ = app_.store().teamsRevision();
    teams_.clear();
    if (tab_ < static_cast<int>(leagues.size())) teams_ = app_.store().teamsOf(leagues[static_cast<size_t>(tab_)]);
    std::sort(teams_.begin(), teams_.end(), [](const sports::SportsTeam& a, const sports::SportsTeam& b) { return a.name < b.name; });
  }

  void setTab(int tab) {
    if (tab == tab_) return;
    tab_ = tab;
    teamScroll_ = 0;
    loadTeams();
  }

  void leaveTeams(bool next) {
    if (next) {
      if (onboarding_) finishOnboarding();
      else {
        step_ = 3;
        button_ = 1;
      }
      return;
    }
    step_ = 1;
    focus_ = 0;
    button_ = nextIndex();
  }

  bool teamsKey(const KeyEvent& e) {
    std::vector<std::string> leagues = teamLeagues();
    int nLeagues = static_cast<int>(leagues.size());
    int n = static_cast<int>(teams_.size());
    if (teams_.empty()) loadTeams();
    if (e.key == Key::Back) {
      step_ = 1;
      focus_ = 0;
      button_ = nextIndex();
      return true;
    }
    if (focus_ == kRail) {
      switch (e.key) {
        case Key::Up: if (tab_ > 0) setTab(tab_ - 1); return true;
        case Key::Down:
          if (tab_ + 1 < nLeagues) setTab(tab_ + 1);
          else {
            focus_ = onboarding_ ? kReminder : kButtons;
            button_ = nextIndex();
          }
          return true;
        case Key::Right:
        case Key::Ok: if (n > 0) focus_ = 0; return true;
        default: return true;
      }
    }
    if (focus_ == kReminder) {
      switch (e.key) {
        case Key::Up: focus_ = kRail; return true;
        case Key::Down: focus_ = kButtons; button_ = nextIndex(); return true;
        case Key::Right: if (n > 0) focus_ = 0; return true;
        case Key::Ok: draft_.reminders = !draft_.reminders; return true;
        default: return true;
      }
    }
    if (focus_ == kButtons) {
      switch (e.key) {
        case Key::Left: button_ = 0; return true;
        case Key::Right: button_ = 1; return true;
        case Key::Up: focus_ = onboarding_ ? kReminder : kRail; return true;
        case Key::Ok:
          // First run: Continue or Skip both move on (Skip leaves the teams
          // as they were);
          // otherwise Back or Next.
          if (onboarding_ && button_ != nextIndex()) draft_.teams = initialTeams_;
          leaveTeams(onboarding_ || button_ == nextIndex());
          return true;
        default: return true;
      }
    }
    // The grid of teams.
    if (int dir = setup_ui::letterStep(e.key); dir != 0) {
      std::vector<std::string> names;
      for (const sports::SportsTeam& t : teams_) names.push_back(t.name);
      focus_ = setup_ui::nextLetter(names, focus_, dir);
      return true;
    }
    switch (e.key) {
      case Key::Left:
        if (focus_ % kTeamCols > 0) --focus_;
        else focus_ = kRail;
        return true;
      case Key::Right: if (focus_ + 1 < n && (focus_ + 1) % kTeamCols != 0) ++focus_; return true;
      case Key::Up: if (focus_ >= kTeamCols) focus_ -= kTeamCols; return true;
      case Key::Down:
        if (focus_ + kTeamCols < n) focus_ += kTeamCols;
        else {
          focus_ = kButtons;
          button_ = nextIndex();
        }
        return true;
      case Key::Ok: {
        if (focus_ < 0 || focus_ >= n || tab_ >= nLeagues) return true;
        std::string key = sports::teamKey(leagues[static_cast<size_t>(tab_)], teams_[static_cast<size_t>(focus_)].id);
        auto it = std::find(draft_.teams.begin(), draft_.teams.end(), key);
        if (it != draft_.teams.end()) draft_.teams.erase(it);
        else draft_.teams.push_back(key);
        return true;
      }
      default: return true;
    }
  }

  void drawTeams(ui::Ctx& c) {
    if (teams_.empty()) loadTeams();
    ui::label(c, onboarding_ ? "Your teams" : "Your favorite teams", kLeft, kTop + 34, Weight::Bold, 52, theme::white());
    ui::label(c, "Their games lead Live Events.", kLeft, kTop + 108, Weight::Regular, 22, theme::white(0.65f), kRailW);
    std::vector<std::string> leagues = teamLeagues();

    // The rail: the picked leagues, then (first run) the reminders switch.
    float y = kTop + 170;
    ui::label(c, "YOUR LEAGUES", kLeft + 6, y, Weight::Bold, 16, theme::white(0.5f));
    y += 32;
    for (size_t i = 0; i < leagues.size(); ++i) {
      const sports::League* l = sports::leagueByShort(leagues[i]);
      std::string t = l ? l->shortName : leagues[i];
      Rect r{kLeft, y, kRailW, 60};
      c.hit(r, kHitTab + static_cast<int>(std::min<size_t>(i, 9)));
      bool sel = static_cast<int>(i) == tab_;
      bool f = sel && focus_ == kRail;
      c.g.fill(r, f ? theme::white() : sel ? theme::white(0.2f) : theme::white(0.07f), 16);
      Color ink = f ? theme::black() : theme::white();
      ui::labelIn(c, t, {r.x + 22, r.y, r.w - 90, r.h}, Weight::Bold, 23, ink);
      int count = static_cast<int>(std::count_if(draft_.teams.begin(), draft_.teams.end(), [&](const std::string& k) {
        return k.rfind(leagues[i] + "|", 0) == 0;
      }));
      if (count > 0)
        ui::labelIn(c, std::to_string(count), {r.x, r.y, r.w - 22, r.h}, Weight::Medium, 19,
                    f ? theme::black(0.6f) : theme::white(0.6f), Align::Right);
      y += 70;
      if (y > bottom() - 250) break;
    }
    if (onboarding_) {
      y += 14;
      Rect row{kLeft, y, kRailW, 84};
      c.hit(row, kHitReminder);
      bool f = focus_ == kReminder;
      c.g.fill(row, f ? theme::white() : theme::white(0.07f), 16);
      Color ink = f ? theme::black() : theme::white();
      ui::label(c, "Remind me before", row.x + 22, row.y + 14, Weight::Bold, 21, ink);
      ui::label(c, "my teams play", row.x + 22, row.y + 44, Weight::Bold, 21, ink);
      Rect knob{row.x + row.w - 88, row.y + 23, 66, 38};
      bool on = draft_.reminders;
      c.g.fill(knob, on ? theme::hex(theme::kToggleOn) : (f ? theme::black(0.2f) : theme::white(0.25f)), 19);
      c.g.fill({on ? knob.x + knob.w - 34 : knob.x + 4, knob.y + 4, 30, 30}, theme::white(), 15);
    }

    // The grid.
    std::string league = tab_ < static_cast<int>(leagues.size()) ? leagues[static_cast<size_t>(tab_)] : "";
    const sports::League* l = sports::leagueByShort(league);
    float gy = kTop + 100;
    if (l) ui::label(c, l->name, kTeamX, gy, Weight::Bold, 30, theme::white(), 900);
    if (!teams_.empty())
      ui::label(c, std::to_string(teams_.size()) + " teams, A to Z", kTeamX, gy + 44, Weight::Regular, 20, theme::white(0.55f));
    gy += 90;
    if (teams_.empty()) {
      ui::spinner(c, kTeamX + 20, gy + 40, 16);
      ui::label(c, "Looking up the teams\xe2\x80\xa6", kTeamX + 50, gy + 26, Weight::Medium, 24, theme::white(0.6f));
    }
    float visible = bottom() - gy;
    if (focus_ >= 0) {
      float fy = static_cast<float>(focus_ / kTeamCols) * (kTeamH + kTeamGap);
      if (fy < teamScroll_) teamScroll_ = fy;
      if (fy + kTeamH > teamScroll_ + visible) teamScroll_ = fy + kTeamH - visible;
    }
    float scroll = teamScroll_;
    c.g.pushClip({kTeamX - 10, gy - 4, gfx::kLayoutWidth - kTeamX, visible + 8});
    for (size_t i = 0; i < teams_.size(); ++i) {
      const sports::SportsTeam& t = teams_[i];
      int row = static_cast<int>(i) / kTeamCols, col = static_cast<int>(i) % kTeamCols;
      Rect r{kTeamX + static_cast<float>(col) * (kTeamW + kTeamGap), gy + static_cast<float>(row) * (kTeamH + kTeamGap) - scroll,
             kTeamW, kTeamH};
      if (r.y > gy + visible || r.y + r.h < gy) continue;
      c.hit(r, kHitTile + static_cast<int>(i));
      bool f = static_cast<int>(i) == focus_;
      bool on = std::find(draft_.teams.begin(), draft_.teams.end(), sports::teamKey(league, t.id)) != draft_.teams.end();
      c.g.fill(r, f ? theme::white() : theme::white(0.08f), 18);
      if (on) c.g.stroke(r, theme::hex(theme::kToggleOn), 18, 3);
      EventSide side;
      side.abbr = t.abbreviation();
      side.logoUrl = t.badge.value_or("");
      auto hex = [](const std::optional<std::string>& h, uint32_t fb) {
        if (!h || h->size() < 6) return fb;
        return static_cast<uint32_t>(std::strtoul(h->c_str() + (h->front() == '#' ? 1 : 0), nullptr, 16));
      };
      side.color = hex(t.color, 0x3A3D44);
      side.alt = hex(t.altColor, 0xFFFFFF);
      events_ui::badge(c, side, r.x + (r.w - 78) * 0.5f, r.y + 18, 78, f);
      ui::label(c, t.name, r.x + 12, r.y + 116, Weight::Medium, 21, f ? theme::black() : theme::white(), r.w - 24, Align::Center);
      if (on) {
        Rect check{r.x + r.w - 40, r.y + 10, 30, 30};
        c.g.fill(check, theme::hex(theme::kToggleOn), 15);
        ui::labelIn(c, "\xe2\x9c\x93", check, Weight::Bold, 18, theme::white(), Align::Center);
      }
    }
    c.g.popClip();
    std::vector<std::string> titles;
    if (onboarding_) titles = {"Continue", "Skip"};
    else titles = {"Back", "Next: review"};
    drawButtons(c, titles, focus_ == kButtons);
  }

  // ---------------------------------------------------------- step 3
  bool reviewKey(const KeyEvent& e) {
    // Rows: 0 the reminders toggle, 1 the buttons.
    switch (e.key) {
      case Key::Up: reviewRow_ = 0; return true;
      case Key::Down: reviewRow_ = 1; return true;
      case Key::Left: if (reviewRow_ == 1) button_ = 0; return true;
      case Key::Right: if (reviewRow_ == 1) button_ = 1; return true;
      case Key::Ok:
        if (reviewRow_ == 0) {
          if (!draft_.teams.empty()) draft_.reminders = !draft_.reminders;
        } else if (button_ == 0) {
          step_ = teamLeagues().empty() ? 1 : 2;
          focus_ = step_ == 1 ? 0 : kRail;
        } else {
          draft_.configured = true;
          draft_.prompted = true;
          app_.store().setMySports(draft_);
          app_.toast("My Sports saved");
          app_.nav().pop();
        }
        return true;
      case Key::Back:
        step_ = teamLeagues().empty() ? 1 : 2;
        focus_ = step_ == 1 ? 0 : kRail;
        return true;
      default: return true;
    }
  }

  void drawReview(ui::Ctx& c) {
    ui::label(c, "Your sports", kLeft, kTop + 34, Weight::Bold, 52, theme::white());
    float y = kTop + 140;
    std::vector<std::string> names;
    for (const std::string& id : draft_.leagues) names.push_back(sports::nameOf(id).value_or(id));
    auto line = [&](const std::string& title, const std::string& value) {
      ui::label(c, title, kLeft, y, Weight::Bold, 26, theme::white());
      ui::paragraph(c, value, kLeft + 360, y + 2, 1300, Weight::Regular, 24, theme::white(0.75f), 2);
      y += 76;
    };
    line("Leagues", names.empty() ? "None picked: every sport shows in one row" : str_join(names));
    line("Favorite teams", draft_.teams.empty() ? "None" : std::to_string(draft_.teams.size()) + " picked");
    line("Everything else", "In \"More events\"");
    y += 20;
    Rect row{kLeft, y, 1200, 84};
    c.hit(row, kHitReminder);
    bool f = reviewRow_ == 0;
    bool enabled = !draft_.teams.empty();
    c.g.fill(row, f ? theme::white() : theme::white(0.07f), theme::kRowRadius);
    Color ink = f ? theme::black(enabled ? 1 : 0.4f) : theme::white(enabled ? 1 : 0.4f);
    ui::labelIn(c, "Remind me before my teams play", {row.x + 28, row.y, row.w - 140, row.h}, Weight::Bold, 25, ink);
    Rect knob{row.x + row.w - 94, row.y + 23, 66, 38};
    bool on = draft_.reminders && enabled;
    c.g.fill(knob, on ? theme::hex(theme::kToggleOn) : (f ? theme::black(0.2f) : theme::white(0.25f)), 19);
    c.g.fill({on ? knob.x + knob.w - 34 : knob.x + 4, knob.y + 4, 30, 30}, theme::white(), 15);
    drawButtons(c, {"Back", "Done"}, reviewRow_ == 1);
  }

  static std::string str_join(const std::vector<std::string>& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) out += (i ? ", " : "") + v[i];
    return out;
  }

  bool onboarding_;
  sports::MySportsPrefs draft_;
  std::map<std::string, int> counts_;
  std::vector<Pick> picks_;
  std::vector<sports::SportsTeam> teams_;
  std::vector<std::string> initialTeams_;
  int step_ = 1;
  int focus_ = 0;
  int button_ = 1;
  int tab_ = 0;
  int reviewRow_ = 1;
  int teamsRevision_ = -1;
  // Step 1 layout (see layoutPicks) and the scroll positions.
  std::vector<std::vector<int>> rows_;
  std::vector<int> rowOf_, colOf_;
  std::vector<float> ys_;
  float scroll_ = 0, teamScroll_ = 0;
};

}  // namespace

std::unique_ptr<Screen> makeMySports(App& app, bool onboarding) { return std::make_unique<MySportsSetup>(app, onboarding); }

}  // namespace marigold::app
