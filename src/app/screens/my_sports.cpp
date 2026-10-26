// My Sports setup (MySportsSetupView): 1. what do you watch (leagues and
// other sports, with how many events your channels carry), 2. favourite
// teams per league, 3. review with "Remind me before my teams play".
// Nothing is saved until Done.
#include <algorithm>
#include <map>

#include "app/app.hpp"
#include "app/screens/events_ui.hpp"
#include "app/screens/screens.hpp"
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
constexpr int kTeamCols = 6;
constexpr float kTeamW = 250, kTeamH = 180, kTeamGap = 22;
const char* kSections[] = {"Football", "Basketball", "Baseball", "Hockey", "Soccer"};
constexpr int kHitTile = 100, kHitButton = 10, kHitTab = 20, kHitReminder = 30;

struct Pick {
  std::string id, name, section;
};

class MySportsSetup : public Screen {
 public:
  explicit MySportsSetup(App& app) : Screen(app), draft_(app.store().mySports()) {
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

  // Pointer: ids 100+ tiles, 10/11 the two buttons, 20+ league tabs, 30 the
  // reminders row.
  void hover(int id) override {
    if (id == kHitButton || id == kHitButton + 1) {
      button_ = id - kHitButton;
      if (step_ == 1) focus_ = static_cast<int>(picks_.size());
      else if (step_ == 2) focus_ = -2;
      else reviewRow_ = 1;
    } else if (step_ == 1 && id >= kHitTile && id - kHitTile < static_cast<int>(picks_.size())) {
      focus_ = id - kHitTile;
    } else if (step_ == 2 && id >= kHitTile && id - kHitTile < static_cast<int>(teams_.size())) {
      focus_ = id - kHitTile;
    } else if (step_ == 2 && id >= kHitTab && id < kHitTab + 10) {
      int tab = id - kHitTab;
      focus_ = -1;
      if (tab != tab_ && tab < static_cast<int>(teamLeagues().size())) {
        tab_ = tab;
        loadTeams();
      }
    } else if (step_ == 3 && id == kHitReminder) {
      reviewRow_ = 0;
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
    ui::label(c, label[step_], kLeft, kTop, Weight::Bold, 18, theme::white(0.5f));
    if (step_ == 1) drawLeagues(c);
    else if (step_ == 2) drawTeams(c);
    else drawReview(c);
  }

 private:
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

  int buttonsRow() const { return static_cast<int>(picks_.size()); }  // focus index of the buttons

  bool leaguesKey(const KeyEvent& e) {
    int n = static_cast<int>(picks_.size());
    if (focus_ >= n) {  // buttons: Cancel / Next
      switch (e.key) {
        case Key::Left: button_ = 0; return true;
        case Key::Right: button_ = 1; return true;
        case Key::Up:
          if (!rows_.empty()) focus_ = rows_.back().front();
          return true;
        case Key::Ok:
          if (button_ == 0) app_.nav().pop();
          else goTeams();
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
          button_ = 1;
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
      step_ = 3;
      button_ = 1;
      return;
    }
    app_.store().requestRosters(std::set<std::string>(leagues.begin(), leagues.end()));
    step_ = 2;
    tab_ = 0;
    focus_ = -1;  // the league tabs
    loadTeams();
  }

  void drawLeagues(ui::Ctx& c) {
    ui::label(c, "What do you watch?", kLeft, kTop + 34, Weight::Bold, 52, theme::white());
    ui::label(c, "Your teams lead Live Events, each pick gets a row, and the schedule is only looked up for these.",
              kLeft, kTop + 108, Weight::Regular, 22, theme::white(0.65f), 1760);
    // Grid with section headings; scrolled only when the focus would leave
    // the view (so tiles stay put under the pointer).
    float y = kTop + 160;
    float visible = gfx::kLayoutHeight - y - 140;
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
    drawButtons(c, "Cancel", teamLeagues().empty() ? "Next: review" : "Next: favorite teams",
                focus_ >= static_cast<int>(picks_.size()));
  }

  void drawButtons(ui::Ctx& c, const std::string& a, const std::string& b, bool focusedRow) {
    float x = kLeft, y = gfx::kLayoutHeight - 110;
    for (int i = 0; i < 2; ++i) {
      const std::string& t = i == 0 ? a : b;
      float w = ui::chipWidth(c, t, 24) + 50;
      Rect r{x, y, w, 64};
      c.hit(r, kHitButton + i);
      bool f = focusedRow && button_ == i;
      c.g.fill(r, f ? theme::white() : theme::white(0.12f), 32);
      ui::labelIn(c, t, r, Weight::Bold, 24, f ? theme::black() : theme::white(), Align::Center);
      x += w + 20;
    }
  }

  // ---------------------------------------------------------- step 2
  void loadTeams() {
    std::vector<std::string> leagues = teamLeagues();
    teamsRevision_ = app_.store().teamsRevision();
    teams_.clear();
    if (tab_ < static_cast<int>(leagues.size())) teams_ = app_.store().teamsOf(leagues[static_cast<size_t>(tab_)]);
    std::sort(teams_.begin(), teams_.end(), [](const sports::SportsTeam& a, const sports::SportsTeam& b) { return a.name < b.name; });
  }

  bool teamsKey(const KeyEvent& e) {
    std::vector<std::string> leagues = teamLeagues();
    int n = static_cast<int>(teams_.size());
    if (teams_.empty()) loadTeams();
    if (focus_ == -1) {  // league tabs
      switch (e.key) {
        case Key::Left: if (tab_ > 0) { --tab_; loadTeams(); } return true;
        case Key::Right: if (tab_ + 1 < static_cast<int>(leagues.size())) { ++tab_; loadTeams(); } return true;
        case Key::Down: focus_ = n > 0 ? 0 : -2; button_ = 1; return true;
        case Key::Back: step_ = 1; focus_ = 0; return true;
        default: return true;
      }
    }
    if (focus_ == -2) {  // buttons: Back / Next
      switch (e.key) {
        case Key::Left: button_ = 0; return true;
        case Key::Right: button_ = 1; return true;
        case Key::Up: focus_ = n > 0 ? n - 1 : -1; return true;
        case Key::Ok:
          if (button_ == 0) { step_ = 1; focus_ = 0; }
          else { step_ = 3; button_ = 1; }
          return true;
        case Key::Back: step_ = 1; focus_ = 0; return true;
        default: return true;
      }
    }
    switch (e.key) {
      case Key::Left: if (focus_ % kTeamCols > 0) --focus_; return true;
      case Key::Right: if (focus_ + 1 < n && (focus_ + 1) % kTeamCols != 0) ++focus_; return true;
      case Key::Up: focus_ = focus_ >= kTeamCols ? focus_ - kTeamCols : -1; return true;
      case Key::Down: if (focus_ + kTeamCols < n) focus_ += kTeamCols; else { focus_ = -2; button_ = 1; } return true;
      case Key::Ok: {
        if (focus_ < 0 || focus_ >= n || tab_ >= static_cast<int>(leagues.size())) return true;
        std::string key = sports::teamKey(leagues[static_cast<size_t>(tab_)], teams_[static_cast<size_t>(focus_)].id);
        auto it = std::find(draft_.teams.begin(), draft_.teams.end(), key);
        if (it != draft_.teams.end()) draft_.teams.erase(it);
        else draft_.teams.push_back(key);
        return true;
      }
      case Key::Back: step_ = 1; focus_ = 0; return true;
      default: return true;
    }
  }

  void drawTeams(ui::Ctx& c) {
    if (teams_.empty()) loadTeams();
    ui::label(c, "Your favorite teams", kLeft, kTop + 34, Weight::Bold, 52, theme::white());
    std::vector<std::string> leagues = teamLeagues();
    float x = kLeft, y = kTop + 120;
    for (size_t i = 0; i < leagues.size(); ++i) {
      const sports::League* l = sports::leagueByShort(leagues[i]);
      std::string t = l ? l->shortName : leagues[i];
      float w = ui::chipWidth(c, t, 22) + 30;
      Rect r{x, y, w, 52};
      c.hit(r, kHitTab + static_cast<int>(i));
      bool sel = static_cast<int>(i) == tab_;
      bool f = sel && focus_ == -1;
      c.g.fill(r, f ? theme::white() : sel ? theme::white(0.25f) : theme::white(0.1f), 26);
      ui::labelIn(c, t, r, Weight::Bold, 22, f ? theme::black() : theme::white(), Align::Center);
      x += w + 14;
    }
    y += 80;
    if (teams_.empty()) {
      ui::spinner(c, kLeft + 20, y + 40, 16);
      ui::label(c, "Looking up the teams\xe2\x80\xa6", kLeft + 50, y + 26, Weight::Medium, 24, theme::white(0.6f));
    }
    float visible = gfx::kLayoutHeight - y - 140;
    if (focus_ >= 0) {
      float fy = static_cast<float>(focus_ / kTeamCols) * (kTeamH + kTeamGap);
      if (fy < teamScroll_) teamScroll_ = fy;
      if (fy + kTeamH > teamScroll_ + visible) teamScroll_ = fy + kTeamH - visible;
    } else if (focus_ == -1) {
      teamScroll_ = 0;
    }
    float scroll = teamScroll_;
    c.g.pushClip({0, y - 4, gfx::kLayoutWidth, visible + 8});
    std::string league = tab_ < static_cast<int>(leagues.size()) ? leagues[static_cast<size_t>(tab_)] : "";
    for (size_t i = 0; i < teams_.size(); ++i) {
      const sports::SportsTeam& t = teams_[i];
      int row = static_cast<int>(i) / kTeamCols, col = static_cast<int>(i) % kTeamCols;
      Rect r{kLeft + static_cast<float>(col) * (kTeamW + kTeamGap), y + static_cast<float>(row) * (kTeamH + kTeamGap) - scroll, kTeamW, kTeamH};
      if (r.y > y + visible || r.y + r.h < y) continue;
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
      events_ui::badge(c, side, r.x + (r.w - 84) * 0.5f, r.y + 20, 84, f);
      ui::label(c, t.name, r.x + 12, r.y + 122, Weight::Medium, 21, f ? theme::black() : theme::white(), r.w - 24, Align::Center);
      if (on) {
        Rect check{r.x + r.w - 40, r.y + 10, 30, 30};
        c.g.fill(check, theme::hex(theme::kToggleOn), 15);
        ui::labelIn(c, "\xe2\x9c\x93", check, Weight::Bold, 18, theme::white(), Align::Center);
      }
    }
    c.g.popClip();
    drawButtons(c, "Back", "Next: review", focus_ == -2);
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
          focus_ = step_ == 1 ? 0 : -1;
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
        focus_ = step_ == 1 ? 0 : -1;
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
    drawButtons(c, "Back", "Done", reviewRow_ == 1);
  }

  static std::string str_join(const std::vector<std::string>& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) out += (i ? ", " : "") + v[i];
    return out;
  }

  sports::MySportsPrefs draft_;
  std::map<std::string, int> counts_;
  std::vector<Pick> picks_;
  std::vector<sports::SportsTeam> teams_;
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

std::unique_ptr<Screen> makeMySports(App& app) { return std::make_unique<MySportsSetup>(app); }

}  // namespace marigold::app
