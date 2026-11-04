// My Sports setup (MySportsSetupView): 1. what do you watch, 2. favourite
// teams, a league at a time from the rail on the left, 3. review with
// "Remind me before my teams play". In first-run setup these are steps 5
// and 6 of 7 (the reminder switch is on the teams screen, and the summary
// follows). Nothing is saved until the last step. Settings › Live Events ›
// My teams starts on step 2 (Back from there closes).
//
// Step 1 (Build 127): sixteen tiles, the big US leagues first, each with
// what it covers and, when it was ticked for your languages, a green line
// saying so. Until My Sports is set up it starts from the defaults
// (Store::defaultMySports()). OK ticks; Options (OK held, or ☰) on Soccer
// and the college tiles opens their page (which competitions; Top 25,
// conferences and schools). "More sports ›" opens the rest. "I don't watch
// sports" saves no leagues at all: games are then in More events.
#include <algorithm>
#include <cctype>
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
constexpr float kLeagueGap = 24, kLeagueRowGap = 20;
constexpr float kLeagueW = (1760 - kLeagueGap * (kLeagueCols - 1)) / kLeagueCols, kLeagueH = 132;
constexpr int kTeamCols = 5;
constexpr float kTeamX = 540, kTeamW = 238, kTeamH = 170, kTeamGap = 20;
constexpr float kRailW = 400;
constexpr int kHitTile = 100, kHitButton = 10, kHitTab = 20, kHitReminder = 30;
// Focus on the teams screen besides a tile (>= 0).
constexpr int kRail = -1, kButtons = -2, kReminder = -3;

// A tile of step 1: what it ticks (one pick, both college basketballs, or
// Soccer's competitions), or the page it opens.
struct Tile {
  enum class Kind { Pick, Soccer, More };
  std::string title, detail;
  std::vector<std::string> ids;
  Kind kind = Kind::Pick;
  std::string college;  // the college league its Options open ("" none)
};

// The grid, in the mock's order (the big US leagues, the WNBA and women's
// college sports first; the sports fewer people follow after).
std::vector<Tile> gridTiles() {
  using K = Tile::Kind;
  return {
      {"NFL", "Pro football", {"NFL"}, K::Pick, ""},
      {"College football", "Top 25 and your schools", {"NCAAF"}, K::Pick, "NCAAF"},
      {"NBA", "Pro basketball", {"NBA"}, K::Pick, ""},
      {"WNBA", "Women's pro basketball", {"WNBA"}, K::Pick, ""},
      {"College basketball", "Men's and women's \xc2\xb7 Top 25 and your schools", {"NCAAM", "NCAAW"}, K::Pick, "NCAAM"},
      {"MLB", "Pro baseball", {"MLB"}, K::Pick, ""},
      {"NHL", "Pro hockey", {"NHL"}, K::Pick, ""},
      {"Soccer", "", {}, K::Soccer, ""},
      {"College volleyball", "Top 25 and your schools", {"S:Volleyball"}, K::Pick, "NCAAWV"},
      {"Tennis", "Slams and tour events", {"S:Tennis"}, K::Pick, ""},
      {"Golf", "Majors and tours", {"S:Golf"}, K::Pick, ""},
      {"Motorsport", "F1, NASCAR, IndyCar", {"S:Motorsport"}, K::Pick, ""},
      {"Boxing and UFC", "Main cards", {"S:Fighting"}, K::Pick, ""},
      {"Cricket", "Internationals and leagues", {"S:Cricket"}, K::Pick, ""},
      {"Rugby", "Internationals and top clubs", {"S:Rugby"}, K::Pick, ""},
      {"More sports", "Lacrosse, softball, darts\xe2\x80\xa6", {}, K::More, ""},
  };
}

// "More sports ›": the rest of the sports, then any league the grid and
// Soccer's page leave out.
std::vector<Tile> moreTiles(const std::vector<Tile>& grid) {
  using K = Tile::Kind;
  std::vector<Tile> out = {
      {"Lacrosse", "College and pro", {"S:Lacrosse"}, K::Pick, "NCAAML"},
      {"Field hockey", "College", {"S:FieldHockey"}, K::Pick, "NCAAFH"},
      {"Softball", "College", {"S:Softball"}, K::Pick, "NCAASB"},
      {"Darts", "Tour events", {"S:Darts"}, K::Pick, ""},
      {"Pro wrestling", "WWE, AEW, NXT, TNA and ROH", {sports::kProWrestlingPick}, K::Pick, ""},
      {"High school", "High-school games", {sports::kHighSchoolPick}, K::Pick, ""},
  };
  auto listed = [&](const std::string& id) {
    auto in = [&](const std::vector<Tile>& v) {
      return std::any_of(v.begin(), v.end(), [&](const Tile& t) {
        return std::find(t.ids.begin(), t.ids.end(), id) != t.ids.end();
      });
    };
    if (in(grid) || in(out)) return true;
    const auto& soccer = sports::soccerCompetitions();
    return std::any_of(soccer.begin(), soccer.end(), [&](const sports::SoccerCompetition& s) { return s.id == id; });
  };
  for (const sports::OtherSport& o : sports::otherSports())
    if (!listed(o.id)) out.push_back({o.group, o.name, {o.id}, K::Pick, ""});
  for (const sports::League& l : sports::leagues())
    if (!listed(l.shortName)) out.push_back({l.name, l.soccer ? "Soccer" : l.sport, {l.shortName}, K::Pick, ""});
  return out;
}

// Soccer's picks: its page's competitions and every soccer league.
std::vector<std::string> soccerIds() {
  std::vector<std::string> ids;
  for (const sports::SoccerCompetition& s : sports::soccerCompetitions()) ids.push_back(s.id);
  for (const sports::League& l : sports::leagues())
    if (l.soccer && std::find(ids.begin(), ids.end(), l.shortName) == ids.end()) ids.push_back(l.shortName);
  return ids;
}

// A competition's name on the Soccer tile: short ("USA").
std::string briefName(const std::string& id) {
  if (id == "USA") return "USA";
  if (id == "S:SoccerAll") return "every other competition";
  return pickName(id);
}

// "Liga MX and Mexico", "EPL, UCL and MLS".
std::string andList(const std::vector<std::string>& names) {
  std::string out;
  for (size_t i = 0; i < names.size(); ++i) {
    if (i > 0) out += i + 1 == names.size() ? " and " : ", ";
    out += names[i];
  }
  return out;
}

std::string lowerFirst(std::string s) {
  if (!s.empty()) s[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[0])));
  return s;
}

class MySportsSetup : public Screen {
 public:
  MySportsSetup(App& app, bool onboarding, bool teams)
      : Screen(app), onboarding_(onboarding), startedOnTeams_(teams), draft_(boardPicks(app)) {
    grid_ = gridTiles();
    more_ = moreTiles(grid_);
    initialTeams_ = draft_.teams;
    button_ = nextIndex();
    if (startedOnTeams_) goTeams();
  }
  ~MySportsSetup() override { app_.store().releaseRosters(); }

  bool tick(uint32_t ms) override {
    wake_.clear();
    if (step_ == 2 && app_.store().teamsRevision() != teamsRevision_) {
      std::string keep = focus_ >= 0 && focus_ < static_cast<int>(teams_.size()) ? teams_[static_cast<size_t>(focus_)].id : "";
      loadTeams();
      if (!keep.empty())
        for (size_t i = 0; i < teams_.size(); ++i)
          if (teams_[i].id == keep) focus_ = static_cast<int>(i);
      // A tile focused that the new list doesn't have: the last one, or the
      // rail when the list is empty (focus is never on nothing).
      if (focus_ >= static_cast<int>(teams_.size())) focus_ = teams_.empty() ? kRail : static_cast<int>(teams_.size()) - 1;
      return true;
    }
    // Looking up the teams: the spinner moves every 125 ms, asleep in between.
    return step_ == 2 && teams_.empty() && spinner_.tick(ms, wake_);
  }
  uint32_t wakeAt() const override { return wake_.get(); }
  const char* name() const override { return "my sports"; }
  // Step 1: OK held is Options (a tile's page).
  bool holdsOk() const override { return step_ == 1; }

  // Pointer: ids 100+ tiles, 10-12 the buttons, 20+ the leagues, 30 the
  // reminders row.
  void hover(int id) override {
    if (id >= kHitButton && id < kHitButton + 3) {
      button_ = id - kHitButton;
      if (step_ == 1) focus_ = static_cast<int>(tiles().size());
      else if (step_ == 2) focus_ = kButtons;
      else reviewRow_ = 1;
    } else if (step_ == 1 && id >= kHitTile && id - kHitTile < static_cast<int>(tiles().size())) {
      focus_ = id - kHitTile;
    } else if (step_ == 2 && id >= kHitTile && id - kHitTile < static_cast<int>(teams_.size())) {
      focus_ = id - kHitTile;
    } else if (step_ == 2 && id >= kHitTab && id < kHitTab + 10) {
      int tab = railFirst_ + (id - kHitTab);
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
    if (step_ == 1) leaguesBar(c);
    else setup_ui::keys(c, app_.remoteKeys(), step_ == 2 && focus_ >= 0);
  }

 private:
  // Continue is the first button in first-run setup (as on its other
  // screens); Next is the second one here.
  int nextIndex() const { return onboarding_ ? 0 : 1; }
  float bottom() const { return onboarding_ ? setup_ui::kContentBottom : 990; }

  bool picked(const std::string& id) const {
    return std::find(draft_.leagues.begin(), draft_.leagues.end(), id) != draft_.leagues.end();
  }
  // Ticking brings back a pick that was hidden from the board.
  void setPick(const std::string& id, bool on) {
    draft_.leagues.erase(std::remove(draft_.leagues.begin(), draft_.leagues.end(), id), draft_.leagues.end());
    if (!on) return;
    draft_.leagues.push_back(id);
    draft_.hidden.erase(std::remove(draft_.hidden.begin(), draft_.hidden.end(), id), draft_.hidden.end());
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

  // The end of first-run setup's sports: saved, then the summary.
  void finishOnboarding() {
    draft_.configured = true;
    draft_.prompted = true;
    app_.store().setMySports(draft_);
    app_.nav().push(makeSetupDone(app_));
  }

  // "I don't watch sports": saved with no leagues (games go to More events).
  void noSports() {
    draft_.leagues.clear();
    draft_.configured = true;
    draft_.prompted = true;
    app_.store().setMySports(draft_);
    if (onboarding_) {
      app_.nav().push(makeSetupDone(app_));
    } else {
      app_.toast("My Sports saved");
      app_.nav().pop();
    }
  }

  // ---------------------------------------------------------- step 1
  const std::vector<Tile>& tiles() const { return morePage_ ? more_ : grid_; }

  std::vector<std::string> buttonTitles() const {
    if (morePage_) return {"Done"};
    if (onboarding_) return {"Continue", "I don't watch sports"};
    return {"Cancel", teamLeagues().empty() ? "Next: review" : "Next: favorite teams", "I don't watch sports"};
  }

  std::vector<std::string> soccerPicked() const {
    std::vector<std::string> out;
    for (const std::string& id : soccerIds())
      if (picked(id)) out.push_back(id);
    return out;
  }

  bool on(const Tile& t) const {
    switch (t.kind) {
      case Tile::Kind::Pick: return std::any_of(t.ids.begin(), t.ids.end(), [&](const std::string& id) { return picked(id); });
      case Tile::Kind::Soccer: return !soccerPicked().empty();
      case Tile::Kind::More: return false;
    }
    return false;
  }

  // What Soccer ticks when it is ticked again: what it had, or the
  // defaults' competitions.
  std::vector<std::string> soccerAgain() const {
    if (!soccerMemory_.empty()) return soccerMemory_;
    std::vector<std::string> out;
    sports::MySportsPrefs d = app_.store().defaultMySports();
    std::vector<std::string> all = soccerIds();
    for (const std::string& id : d.leagues)
      if (std::find(all.begin(), all.end(), id) != all.end()) out.push_back(id);
    return out;
  }

  void activate(const Tile& t) {
    switch (t.kind) {
      case Tile::Kind::Pick: {
        bool now = !on(t);
        for (const std::string& id : t.ids) setPick(id, now);
        break;
      }
      case Tile::Kind::Soccer: {
        std::vector<std::string> ticked = soccerPicked();
        if (ticked.empty()) {
          for (const std::string& id : soccerAgain()) setPick(id, true);
        } else {
          soccerMemory_ = ticked;
          for (const std::string& id : ticked) setPick(id, false);
        }
        break;
      }
      case Tile::Kind::More:
        morePage_ = true;
        focus_ = 0;
        scroll_ = 0;
        break;
    }
  }

  // Options: a tile's own page (Soccer's competitions, a college sport).
  void options(const Tile& t) {
    MySportsSetup* self = this;
    PicksDone back = [self](const sports::MySportsPrefs& p) { self->draft_ = p; };
    if (t.kind == Tile::Kind::Soccer) app_.nav().push(makeSoccerPicks(app_, draft_, back));
    else if (!t.college.empty()) app_.nav().push(makeCollegePicks(app_, draft_, t.college, back));
  }

  void leaveMore() {
    morePage_ = false;
    scroll_ = 0;
    // Back on "More sports ›".
    focus_ = static_cast<int>(grid_.size()) - 1;
    button_ = nextIndex();
  }

  void pressButton() {
    if (morePage_) {
      leaveMore();
    } else if (button_ == nextIndex()) {
      goTeams();
    } else if (button_ == (onboarding_ ? 1 : 2)) {
      noSports();
    } else {
      app_.nav().pop();  // Cancel
    }
  }

  bool leaguesKey(const KeyEvent& e) {
    const std::vector<Tile>& ts = tiles();
    int n = static_cast<int>(ts.size());
    if (e.key == Key::Back && morePage_) {
      leaveMore();
      return true;
    }
    if ((e.longPress && e.key == Key::Ok) || e.key == Key::Menu) {
      if (focus_ >= 0 && focus_ < n) options(ts[static_cast<size_t>(focus_)]);
      return true;
    }
    int buttons = static_cast<int>(buttonTitles().size());
    if (focus_ >= n) {  // the buttons
      switch (e.key) {
        case Key::Left: button_ = std::max(0, button_ - 1); return true;
        case Key::Right: button_ = std::min(buttons - 1, button_ + 1); return true;
        case Key::Up:
          if (n > 0) focus_ = std::min(n - 1, ((n - 1) / kLeagueCols) * kLeagueCols);
          return true;
        case Key::Ok: pressButton(); return true;
        default: return e.key != Key::Back;
      }
    }
    int col = focus_ % kLeagueCols;
    switch (e.key) {
      case Key::Left: if (col > 0) --focus_; return true;
      case Key::Right:
        if (col + 1 < kLeagueCols && focus_ + 1 < n) ++focus_;
        return true;
      case Key::Up:
        if (focus_ >= kLeagueCols) focus_ -= kLeagueCols;
        return true;
      case Key::Down:
        if (focus_ + kLeagueCols < n) {
          focus_ += kLeagueCols;
        } else if (focus_ / kLeagueCols < (n - 1) / kLeagueCols) {
          focus_ = n - 1;  // the last row is short
        } else {
          focus_ = n;
          button_ = morePage_ ? 0 : nextIndex();
        }
        return true;
      case Key::Ok:
        activate(ts[static_cast<size_t>(focus_)]);
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

  // A tile as drawn: Soccer lists its competitions, and a green line says
  // why a pick was ticked for you.
  setup_ui::PickTile look(const Tile& t) const {
    setup_ui::PickTile p;
    p.title = t.title;
    p.detail = t.detail;
    p.on = on(t);
    p.chevron = t.kind == Tile::Kind::More;
    if (t.kind == Tile::Kind::Soccer) {
      std::vector<std::string> ids = p.on ? soccerPicked() : soccerAgain();
      std::vector<std::string> plain, extra;
      std::string reason;
      for (const std::string& id : ids) {
        std::string why = p.on ? app_.store().pickReason(id) : std::string();
        if (why.empty()) {
          plain.push_back(briefName(id));
        } else {
          extra.push_back(briefName(id));
          if (reason.empty()) reason = why;
        }
      }
      if (plain.size() > 5) {
        size_t more = plain.size() - 5;
        plain.resize(5);
        plain.push_back(std::to_string(more) + " more");
      }
      for (size_t i = 0; i < plain.size(); ++i) p.detail += (i ? ", " : "") + plain[i];
      if (plain.empty()) p.detail = "Choose your competitions";
      if (!extra.empty()) p.why = "+ " + andList(extra) + ", " + lowerFirst(reason);
    } else if (p.on) {
      for (const std::string& id : t.ids) {
        std::string why = app_.store().pickReason(id);
        if (!why.empty()) {
          p.why = why;
          break;
        }
      }
    }
    return p;
  }

  void drawLeagues(ui::Ctx& c) {
    std::vector<setup_ui::PickTile> looks;
    bool reasons = false;
    for (const Tile& t : tiles()) {
      looks.push_back(look(t));
      reasons = reasons || !looks.back().why.empty();
    }
    std::string lead;
    if (morePage_) lead = "Each one you tick gets its own row in Live Events.";
    else if (!app_.store().mySports().configured)
      lead = reasons ? "We ticked the ones most people follow, and some for the languages you chose."
                     : "We ticked the ones most people follow. Each gets its own row in Live Events.";
    else lead = "Each one you tick gets its own row in Live Events. Other games are in More events and Search.";
    ui::label(c, morePage_ ? "More sports" : "What do you watch?", kLeft, kTop + 34, Weight::Bold, 52, theme::white());
    ui::label(c, lead, kLeft, kTop + 108, Weight::Regular, 22, theme::white(0.65f), 1760);
    // The grid, scrolled only when the focus would leave the view (so
    // tiles stay put under the pointer).
    float y = kTop + 160;
    float visible = bottom() - 90 - y;
    int n = static_cast<int>(looks.size());
    if (focus_ >= 0 && focus_ < n)
      scroll_ = setup_ui::keepRowInView(scroll_, focus_ / kLeagueCols, kLeagueH, kLeagueRowGap, visible);
    c.g.pushClip({0, y - 6, gfx::kLayoutWidth, visible + 12});
    for (int i = 0; i < n; ++i) {
      Rect r{kLeft + static_cast<float>(i % kLeagueCols) * (kLeagueW + kLeagueGap),
             y + static_cast<float>(i / kLeagueCols) * (kLeagueH + kLeagueRowGap) - scroll_, kLeagueW, kLeagueH};
      if (r.y + r.h < y - 6 || r.y > y + visible + 6) continue;
      c.hit(r, kHitTile + i);
      setup_ui::pickTile(c, r, looks[static_cast<size_t>(i)], i == focus_);
    }
    c.g.popClip();
    drawButtons(c, buttonTitles(), focus_ >= n);
  }

  // OK ticks; Options (OK held, ☰ on Fire TV, click and hold on Apple TV)
  // chooses a tile's leagues.
  void leaguesBar(ui::Ctx& c) {
    const RemoteKeys& k = app_.remoteKeys();
    hints::Bar b;
    b.keys.push_back(setup_ui::keyHint(k.clickpad ? "Click" : "OK", "Tick"));
    if (k.menu) {
      hints::Hint menu;
      menu.icons = {"key-menu"};
      menu.label = "Choose leagues";
      b.keys.push_back(menu);
    } else {
      b.keys.push_back(setup_ui::keyHint(k.clickpad ? "Click and hold" : "Hold OK", "Choose leagues"));
    }
    b.keys.push_back(setup_ui::keyHint("Back", morePage_ ? "All sports" : onboarding_ ? "Previous step" : "Close"));
    ui::keyBar(c, b);
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
    if (startedOnTeams_) {
      app_.nav().pop();
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
      if (startedOnTeams_) return false;  // back to Settings
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
    // As many leagues as fit above the reminders row and the buttons; the
    // list scrolls so the selected one is always drawn (Build 129: with a
    // dozen picks the rest were left out and Down moved focus through
    // leagues nobody could see).
    int rows = std::clamp(static_cast<int>((bottom() - 250 - y) / 70) + 1, 1, 10);
    int count = static_cast<int>(leagues.size());
    if (tab_ < railFirst_) railFirst_ = tab_;
    if (tab_ >= railFirst_ + rows) railFirst_ = tab_ - rows + 1;
    railFirst_ = std::clamp(railFirst_, 0, std::max(0, count - rows));
    if (railFirst_ > 0) ui::label(c, "\xe2\x96\xb2", kLeft + kRailW - 30, y - 30, Weight::Bold, 16, theme::white(0.5f));
    for (int i = railFirst_; i < count && i < railFirst_ + rows; ++i) {
      const sports::League* l = sports::leagueByShort(leagues[static_cast<size_t>(i)]);
      std::string t = l ? l->shortName : leagues[static_cast<size_t>(i)];
      Rect r{kLeft, y, kRailW, 60};
      c.hit(r, kHitTab + (i - railFirst_));
      bool sel = i == tab_;
      bool f = sel && focus_ == kRail;
      c.g.fill(r, f ? theme::white() : sel ? theme::white(0.2f) : theme::white(0.07f), 16);
      Color ink = f ? theme::black() : theme::white();
      ui::labelIn(c, t, {r.x + 22, r.y, r.w - 90, r.h}, Weight::Bold, 23, ink);
      const std::string& prefix = leagues[static_cast<size_t>(i)];
      int picks = static_cast<int>(std::count_if(draft_.teams.begin(), draft_.teams.end(), [&](const std::string& k) {
        return k.rfind(prefix + "|", 0) == 0;
      }));
      if (picks > 0)
        ui::labelIn(c, std::to_string(picks), {r.x, r.y, r.w - 22, r.h}, Weight::Medium, 19,
                    f ? theme::black(0.6f) : theme::white(0.6f), Align::Right);
      y += 70;
    }
    if (railFirst_ + rows < count) ui::label(c, "\xe2\x96\xbc", kLeft + kRailW - 30, y - 8, Weight::Bold, 16, theme::white(0.5f));
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
      // Focused: the calm focus (the badge looks the same focused or not);
      // a picked team's green ring sits inside the white outline.
      Rect up = f ? ui::calmFocus(c, r, 18) : r;
      if (!f) c.g.fill(r, theme::white(0.08f), 18);
      if (on) c.g.stroke(f ? Rect{up.x + 3, up.y + 3, up.w - 6, up.h - 6} : r, theme::hex(theme::kToggleOn), f ? 16 : 18, 3);
      EventSide side;
      side.abbr = t.abbreviation();
      side.logoUrl = t.badge.value_or("");
      auto hex = [](const std::optional<std::string>& h, uint32_t fb) {
        if (!h || h->size() < 6) return fb;
        return static_cast<uint32_t>(std::strtoul(h->c_str() + (h->front() == '#' ? 1 : 0), nullptr, 16));
      };
      side.color = hex(t.color, 0x3A3D44);
      side.alt = hex(t.altColor, 0xFFFFFF);
      events_ui::badge(c, side, r.x + (r.w - 78) * 0.5f, r.y + 18, 78);
      ui::label(c, t.name, r.x + 12, r.y + 116, Weight::Medium, 21, theme::white(), r.w - 24, Align::Center);
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
    for (const std::string& id : draft_.leagues) names.push_back(pickName(id));
    auto line = [&](const std::string& title, const std::string& value) {
      ui::label(c, title, kLeft, y, Weight::Bold, 26, theme::white());
      ui::paragraph(c, value, kLeft + 360, y + 2, 1300, Weight::Regular, 24, theme::white(0.75f), 2);
      y += 76;
    };
    line("Leagues", names.empty() ? "None: games are in More events" : str_join(names));
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
  bool startedOnTeams_;
  sports::MySportsPrefs draft_;
  std::vector<Tile> grid_, more_;
  bool morePage_ = false;  // step 1 shows "More sports"
  std::vector<std::string> soccerMemory_;  // Soccer's picks before it was unticked
  std::vector<sports::SportsTeam> teams_;
  std::vector<std::string> initialTeams_;
  int step_ = 1;
  int focus_ = 0;
  int button_ = 1;
  int tab_ = 0;
  int reviewRow_ = 1;
  int teamsRevision_ = -1;
  wake::Due wake_;
  wake::Steps spinner_;
  float scroll_ = 0, teamScroll_ = 0;
  int railFirst_ = 0;  // the first league drawn in the teams step's rail
};

}  // namespace

std::unique_ptr<Screen> makeMySports(App& app, bool onboarding, bool teams) {
  return std::make_unique<MySportsSetup>(app, onboarding, teams);
}

}  // namespace marigold::app
