// My Sports' pages (Build 127), in the rail-and-grid layout of the teams
// step: Soccer's competitions (groups on the left, the competitions in a
// grid), the college sports (each sport's Top 25, conferences, schools and
// every Division I game, on its own) and a sport's schools by conference
// (the first tile of each conference follows all of it). Opened by
// Options on My Sports' tiles and by Live Events' "Choose competitions" /
// "Choose schools" tiles. They edit a copy of the picks; Back hands it
// back (My Sports keeps it until its last step, the board saves it). Also
// the undo note a game card's Hide leaves at the bottom of the screen.
#include <algorithm>
#include <cctype>
#include <map>
#include <set>

#include "app/app.hpp"
#include "app/screens/panel_screen.hpp"
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
constexpr float kRailW = 400;
constexpr float kGridX = 540, kGridRight = gfx::kLayoutWidth - 80;
constexpr float kBodyTop = kTop + 170;
constexpr float kBottom = 990;  // above the key bar
constexpr int kCols = 3;
constexpr float kGap = 24, kTileH = 112, kRowGap = 20;
constexpr float kOptionH = 104, kOptionStep = 120, kOptionW = 1180;
constexpr int kHitOption = 10, kHitRail = 20, kHitTile = 1000;
constexpr int kRail = -1;  // the focus on the rail (>= 0: a tile or option)
const char* const kLeftArrow = "\xe2\x97\x80";

float tileW() { return (kGridRight - kGridX - kGap * (kCols - 1)) / kCols; }

Rect tileRect(size_t i, float y, float scroll) {
  float col = static_cast<float>(i % kCols), row = static_cast<float>(i / kCols);
  return {kGridX + col * (tileW() + kGap), y + row * (kTileH + kRowGap) - scroll, tileW(), kTileH};
}

void header(ui::Ctx& c, const std::string& kick, const std::string& title, const std::string& lead) {
  c.g.fill({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, theme::hex(theme::kBackground));
  ui::label(c, kick, kLeft, kTop, Weight::Bold, 18, theme::white(0.5f));
  ui::label(c, title, kLeft, kTop + 34, Weight::Bold, 52, theme::white());
  if (!lead.empty()) ui::label(c, lead, kLeft, kTop + 108, Weight::Regular, 22, theme::white(0.65f), 1760);
}

// The grid's tiles from `y`, scrolled to keep the focus in view.
void grid(ui::Ctx& c, const std::vector<setup_ui::PickTile>& tiles, int focus, float y, float& scroll) {
  float visible = kBottom - 20 - y;
  if (focus >= 0) scroll = setup_ui::keepRowInView(scroll, focus / kCols, kTileH, kRowGap, visible);
  c.g.pushClip({kGridX - 10, y - 6, gfx::kLayoutWidth - kGridX, visible + 12});
  for (size_t i = 0; i < tiles.size(); ++i) {
    Rect r = tileRect(i, y, scroll);
    if (r.y + r.h < y - 6 || r.y > y + visible + 6) continue;
    c.hit(r, kHitTile + static_cast<int>(i));
    setup_ui::pickTile(c, r, tiles[i], static_cast<int>(i) == focus);
  }
  c.g.popClip();
}

// Arrows in a grid of `n` tiles: the new focus, kRail for Left off the
// first column, or `focus` unchanged.
int gridMove(Key k, int focus, int n) {
  switch (k) {
    case Key::Left: return focus % kCols > 0 ? focus - 1 : kRail;
    case Key::Right: return focus + 1 < n && (focus + 1) % kCols != 0 ? focus + 1 : focus;
    case Key::Up: return focus >= kCols ? focus - kCols : focus;
    case Key::Down: return focus + kCols < n ? focus + kCols : focus;
    default: return focus;
  }
}

void bar(ui::Ctx& c, const RemoteKeys& keys, bool inGrid, const std::string& railWord) {
  hints::Bar b;
  std::string ok = keys.clickpad ? "Click" : "OK";
  if (inGrid) {
    b.keys.push_back(setup_ui::keyHint(kLeftArrow, railWord));
    b.keys.push_back(setup_ui::keyHint(ok, "Tick"));
  } else {
    b.keys.push_back(setup_ui::keyHint(ok, "Open"));
  }
  b.keys.push_back(setup_ui::keyHint("Back", "Save"));
  ui::keyBar(c, b);
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

// Ticks or unticks a pick; ticking brings back one that was hidden.
void setPick(sports::MySportsPrefs& p, const std::string& id, bool on) {
  p.leagues.erase(std::remove(p.leagues.begin(), p.leagues.end(), id), p.leagues.end());
  if (!on) return;
  p.leagues.push_back(id);
  p.hidden.erase(std::remove(p.hidden.begin(), p.hidden.end(), id), p.hidden.end());
}

std::string upper(std::string s) {
  for (char& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  return s;
}

std::string sentence(std::string s) {
  for (size_t i = 1; i < s.size(); ++i) s[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
  return s;
}

// "NCAAF, NCAAM": the first few names, then "+2".
std::string firstFew(const std::vector<std::string>& names, size_t few) {
  std::string out;
  for (size_t i = 0; i < names.size() && i < few; ++i) out += (i ? ", " : "") + names[i];
  if (names.size() > few) out += " +" + std::to_string(names.size() - few);
  return out;
}

// ------------------------------------------------------------------ Soccer

// The rail's groups, in this order; any other group a competition names
// comes after them.
const char* const kSoccerGroups[] = {"Popular",        "National teams", "England",
                                     "Spain",          "Italy and Germany", "Rest of Europe",
                                     "Mexico and the Americas", "Women's", "Every other"};

class SoccerPicks : public Screen {
 public:
  SoccerPicks(App& app, const sports::MySportsPrefs& prefs, PicksDone done)
      : Screen(app), draft_(prefs), done_(std::move(done)) {
    const std::vector<sports::SoccerCompetition>& all = sports::soccerCompetitions();
    auto add = [&](const std::string& g) {
      if (contains(groups_, g)) return;
      for (const sports::SoccerCompetition& s : all)
        if (contains(s.groups, g)) {
          groups_.push_back(g);
          return;
        }
    };
    for (const char* g : kSoccerGroups) add(g);
    for (const sports::SoccerCompetition& s : all)
      for (const std::string& g : s.groups) add(g);
    load();
    // On the first competition, ready to tick.
    if (!shown_.empty()) focus_ = 0;
  }
  const char* name() const override { return "soccer picks"; }

  bool key(const KeyEvent& e) override {
    if (e.key == Key::Back) {
      finish();
      return true;
    }
    int n = static_cast<int>(shown_.size());
    if (focus_ == kRail) {
      switch (e.key) {
        case Key::Up: setGroup(group_ - 1); return true;
        case Key::Down: setGroup(group_ + 1); return true;
        case Key::Right:
        case Key::Ok:
          if (n > 0) focus_ = 0;
          return true;
        default: return true;
      }
    }
    if (e.key == Key::Ok) {
      if (focus_ < n) toggle(shown_[static_cast<size_t>(focus_)]);
      return true;
    }
    focus_ = gridMove(e.key, focus_, n);
    return true;
  }

  void hover(int id) override {
    if (id >= kHitTile && id - kHitTile < static_cast<int>(shown_.size())) {
      focus_ = id - kHitTile;
    } else if (id >= kHitRail && id - kHitRail < static_cast<int>(groups_.size())) {
      focus_ = kRail;
      setGroup(id - kHitRail);
    }
  }

  void draw(ui::Ctx& c) override {
    header(c, "MY SPORTS \xc2\xb7 SOCCER", "Which competitions?",
           "Ticked ones get matches in your Soccer row. National-team games are in every group they belong to.");
    setup_ui::railList(c, kLeft, kBodyTop, kRailW, kBottom - 20, groups_, group_, focus_ == kRail, railScroll_,
                       kHitRail);
    const std::vector<sports::SoccerCompetition>& all = sports::soccerCompetitions();
    std::vector<setup_ui::PickTile> tiles;
    tiles.reserve(shown_.size());
    for (size_t i : shown_) {
      setup_ui::PickTile t;
      t.title = all[i].name;
      t.detail = all[i].place;
      t.on = contains(draft_.leagues, all[i].id);
      tiles.push_back(std::move(t));
    }
    grid(c, tiles, focus_, kBodyTop, scroll_);
    bar(c, app_.remoteKeys(), focus_ != kRail, "Groups");
  }

 private:
  void load() {
    shown_.clear();
    scroll_ = 0;
    if (group_ < 0 || group_ >= static_cast<int>(groups_.size())) return;
    const std::vector<sports::SoccerCompetition>& all = sports::soccerCompetitions();
    for (size_t i = 0; i < all.size(); ++i)
      if (contains(all[i].groups, groups_[static_cast<size_t>(group_)])) shown_.push_back(i);
  }
  void setGroup(int g) {
    if (g < 0 || g >= static_cast<int>(groups_.size()) || g == group_) return;
    group_ = g;
    load();
  }
  void toggle(size_t i) {
    const std::string& id = sports::soccerCompetitions()[i].id;
    setPick(draft_, id, !contains(draft_.leagues, id));
  }
  void finish() {
    PicksDone done = done_;
    sports::MySportsPrefs out = draft_;
    app_.nav().pop();
    if (done) done(out);
  }

  sports::MySportsPrefs draft_;
  PicksDone done_;
  std::vector<std::string> groups_;
  std::vector<size_t> shown_;  // the group's competitions (soccerCompetitions() indices)
  int group_ = 0;
  int focus_ = kRail;
  float scroll_ = 0, railScroll_ = 0;
};

// ------------------------------------------------------------------ College

struct CollegeSport {
  std::string league, name;  // "NCAAF", "Football"
};

// A college league's name on the rail: "Men's lacrosse", "Softball".
std::string sportName(const sports::League& l) {
  std::string sport = l.group;
  if (sport.rfind("College ", 0) == 0) sport = sport.substr(8);
  std::string lower = sport;
  for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  if (l.name.rfind("Men's", 0) == 0) return "Men's " + lower;
  if (l.name.rfind("Women's", 0) == 0) return "Women's " + lower;
  return sentence(sport);
}

// The rail: football, both basketballs and volleyball always; the other
// college sports when their sport is picked (lacrosse, softball...); and
// `extra` (a board row's league) when it is none of these.
std::vector<CollegeSport> collegeSports(const sports::MySportsPrefs& p, const std::string& extra) {
  std::vector<CollegeSport> out = {
      {"NCAAF", "Football"}, {"NCAAM", "Men's basketball"}, {"NCAAW", "Women's basketball"}, {"NCAAWV", "Volleyball"}};
  auto known = [&](const std::string& league) {
    return std::any_of(out.begin(), out.end(), [&](const CollegeSport& s) { return s.league == league; });
  };
  for (const std::string& id : p.leagues) {
    if (id.rfind("S:", 0) != 0) continue;
    for (const sports::League& l : sports::sportLeagues(id))
      if (l.shortName.rfind("NCAA", 0) == 0 && !known(l.shortName)) out.push_back({l.shortName, sportName(l)});
  }
  if (!extra.empty() && !known(extra)) {
    const sports::League* l = sports::leagueByShort(extra);
    out.push_back({extra, l ? sportName(*l) : extra});
  }
  return out;
}

class CollegePicks : public Screen {
 public:
  CollegePicks(App& app, const sports::MySportsPrefs& prefs, const std::string& league, PicksDone done)
      : Screen(app), draft_(prefs), done_(std::move(done)) {
    sports_ = collegeSports(draft_, league);
    for (size_t i = 0; i < sports_.size(); ++i)
      if (sports_[i].league == league) tab_ = static_cast<int>(i);
    loadSport();
  }
  const char* name() const override { return "college picks"; }

  void shown() override { loadNames(); }

  bool key(const KeyEvent& e) override { return onSchools_ ? schoolsKey(e) : optionsKey(e); }

  void hover(int id) override {
    if (onSchools_) {
      if (id >= kHitTile && id - kHitTile < static_cast<int>(schoolTiles())) {
        focus_ = id - kHitTile;
      } else if (id >= kHitRail && id - kHitRail < static_cast<int>(conferences_.size())) {
        focus_ = kRail;
        setConference(id - kHitRail);
      }
      return;
    }
    if (id >= kHitOption && id < kHitOption + kOptions) {
      focus_ = id - kHitOption;
    } else if (id >= kHitRail && id - kHitRail < static_cast<int>(sports_.size())) {
      focus_ = kRail;
      setSport(id - kHitRail);
    }
  }

  void draw(ui::Ctx& c) override {
    if (onSchools_) drawSchools(c);
    else drawOptions(c);
  }

  // The conferences page edits the draft through these.
  std::vector<sports::Conference> conferences() const { return conferences_; }
  bool conferenceOn(const std::string& key) const { return contains(pick().conferences, key); }
  void toggleConference(const std::string& key) {
    sports::CollegePick& p = edit();
    auto it = std::find(p.conferences.begin(), p.conferences.end(), key);
    if (it != p.conferences.end()) p.conferences.erase(it);
    else p.conferences.push_back(key);
  }
  const CollegeSport& sport() const { return sports_[static_cast<size_t>(tab_)]; }

 private:
  static constexpr int kOptions = 4;  // Top 25, conferences, schools, every game

  // This sport's settings as saved (the defaults when it has none).
  sports::CollegePick pick() const { return sports::collegePick(draft_, sport().league); }
  sports::CollegePick& edit() {
    const std::string& league = sport().league;
    auto it = draft_.college.find(league);
    if (it == draft_.college.end()) {
      added_.insert(league);
      it = draft_.college.emplace(league, sports::collegePick(draft_, league)).first;
    }
    return it->second;
  }

  // The sport's conferences and its schools' names (for the lines under
  // the options), from the schedules the store has.
  void loadSport() {
    conferences_ = app_.store().collegeConferences(sport().league);
    loadNames();
  }
  void loadNames() {
    schoolNames_.clear();
    for (const sports::Conference& k : conferences_)
      for (const auto& [id, school] : app_.store().conferenceSchools(sport().league, k.key)) schoolNames_[id] = school;
  }

  void setSport(int t) {
    if (t < 0 || t >= static_cast<int>(sports_.size()) || t == tab_) return;
    tab_ = t;
    loadSport();
  }

  void finish() {
    // A sport opened and left as it was isn't saved as a setting of its own.
    for (const std::string& league : added_) {
      auto it = draft_.college.find(league);
      if (it != draft_.college.end() && it->second == sports::CollegePick{}) draft_.college.erase(it);
    }
    PicksDone done = done_;
    sports::MySportsPrefs out = draft_;
    app_.nav().pop();
    if (done) done(out);
  }

  // ------------------------------------------------ the options (3a)

  bool optionsKey(const KeyEvent& e) {
    if (e.key == Key::Back) {
      finish();
      return true;
    }
    if (focus_ == kRail) {
      switch (e.key) {
        case Key::Up: setSport(tab_ - 1); return true;
        case Key::Down: setSport(tab_ + 1); return true;
        case Key::Right:
        case Key::Ok: focus_ = 0; return true;
        default: return true;
      }
    }
    switch (e.key) {
      case Key::Left: focus_ = kRail; return true;
      case Key::Up: focus_ = std::max(0, focus_ - 1); return true;
      case Key::Down: focus_ = std::min(kOptions - 1, focus_ + 1); return true;
      case Key::Ok: choose(focus_); return true;
      default: return true;
    }
  }

  void choose(int option) {
    switch (option) {
      case 0: edit().top25 = !pick().top25; break;
      case 1: openConferences(); break;
      case 2:
        onSchools_ = true;
        focus_ = conferences_.empty() ? kRail : 0;
        conference_ = 0;
        loadSchools();
        break;
      case 3: edit().everyGame = !pick().everyGame; break;
      default: break;
    }
  }

  void openConferences();

  std::string conferencesLine() const {
    std::vector<std::string> names;
    for (const std::string& key : pick().conferences) {
      std::string name;
      for (const sports::Conference& k : conferences_)
        if (k.key == key) name = k.name;
      if (!name.empty()) names.push_back(name);
    }
    size_t unknown = pick().conferences.size() - names.size();
    if (names.empty()) return unknown ? "Picked" : "None";
    std::string line = firstFew(names, 3);
    return unknown && names.size() <= 3 ? line + " +" + std::to_string(unknown) : line;
  }

  std::vector<std::string> followedSchools() const {
    std::vector<std::string> ids;
    std::string prefix = sport().league + "|";
    for (const std::string& key : draft_.teams)
      if (key.rfind(prefix, 0) == 0) ids.push_back(key.substr(prefix.size()));
    return ids;
  }

  std::string schoolsLine() const {
    std::vector<std::string> ids = followedSchools();
    std::vector<std::string> names;
    for (const std::string& id : ids) {
      auto it = schoolNames_.find(id);
      if (it != schoolNames_.end()) names.push_back(it->second);
    }
    if (ids.empty()) return "None";
    if (names.empty()) return "Picked";
    std::string line = firstFew(names, 3);
    size_t unknown = ids.size() - names.size();
    return unknown && names.size() <= 3 ? line + " +" + std::to_string(unknown) : line;
  }

  void drawOptions(ui::Ctx& c) {
    header(c, "MY SPORTS \xc2\xb7 COLLEGE", "College sports",
           "A game shows when either team is ranked, in your conferences, or one of your schools.");
    std::vector<std::string> names;
    for (const CollegeSport& s : sports_) names.push_back(s.name);
    setup_ui::railList(c, kLeft, kBodyTop, kRailW, kBottom - 20, names, tab_, focus_ == kRail, railScroll_, kHitRail);
    sports::CollegePick p = pick();
    bool football = sport().league == "NCAAF";
    struct Row {
      const char* title;
      std::string detail;
      bool toggle, on;
    };
    const Row rows[kOptions] = {
        {"Top 25", "This week's ranked teams, wherever they play", true, p.top25},
        {"Your conferences", conferencesLine(), false, false},
        {"Your schools", schoolsLine(), false, false},
        {"Every Division I game",
         football ? "Over a hundred games on a fall Saturday, most on streaming-only channels"
                  : "Every game of every school, most on streaming-only channels",
         true, p.everyGame},
    };
    for (int i = 0; i < kOptions; ++i) {
      Rect r{kGridX, kBodyTop + static_cast<float>(i) * kOptionStep, kOptionW, kOptionH};
      c.hit(r, kHitOption + i);
      const Row& row = rows[i];
      setup_ui::optionRow(c, r, row.title, row.detail, row.toggle, row.on, focus_ == i);
    }
    hints::Bar b;
    std::string ok = app_.remoteKeys().clickpad ? "Click" : "OK";
    if (focus_ != kRail) b.keys.push_back(setup_ui::keyHint(kLeftArrow, "Sports"));
    b.keys.push_back(setup_ui::keyHint(ok, "Select"));
    b.keys.push_back(setup_ui::keyHint("Back", "Save"));
    ui::keyBar(c, b);
  }

  // ------------------------------------------------ the schools (3b)

  // "All of the <conference>", then its schools.
  size_t schoolTiles() const { return conferences_.empty() ? 0 : 1 + schoolList_.size(); }

  void setConference(int k) {
    if (k < 0 || k >= static_cast<int>(conferences_.size()) || k == conference_) return;
    conference_ = k;
    loadSchools();
  }
  void loadSchools() {
    schoolList_.clear();
    gridScroll_ = 0;
    if (conference_ < 0 || conference_ >= static_cast<int>(conferences_.size())) return;
    schoolList_ =
        app_.store().conferenceSchools(sport().league, conferences_[static_cast<size_t>(conference_)].key);
  }

  void leaveSchools() {
    onSchools_ = false;
    focus_ = 2;  // back on "Your schools"
    loadNames();
  }

  bool schoolsKey(const KeyEvent& e) {
    if (e.key == Key::Back) {
      leaveSchools();
      return true;
    }
    int n = static_cast<int>(schoolTiles());
    if (focus_ == kRail) {
      switch (e.key) {
        case Key::Up: setConference(conference_ - 1); return true;
        case Key::Down: setConference(conference_ + 1); return true;
        case Key::Right:
        case Key::Ok:
          if (n > 0) focus_ = 0;
          return true;
        default: return true;
      }
    }
    if (e.key == Key::Ok) {
      if (focus_ == 0) {
        toggleConference(conferences_[static_cast<size_t>(conference_)].key);
      } else if (focus_ < n) {
        std::string key = sports::teamKey(sport().league, schoolList_[static_cast<size_t>(focus_ - 1)].first);
        auto it = std::find(draft_.teams.begin(), draft_.teams.end(), key);
        if (it != draft_.teams.end()) draft_.teams.erase(it);
        else draft_.teams.push_back(key);
      }
      return true;
    }
    focus_ = gridMove(e.key, focus_, n);
    return true;
  }

  void drawSchools(ui::Ctx& c) {
    header(c, "MY SPORTS \xc2\xb7 COLLEGE " + upper(sport().name) + " \xc2\xb7 SCHOOLS", "Your schools",
           "Their games are in your row whatever the ranking. The first tile follows a whole conference.");
    std::vector<std::string> names;
    for (const sports::Conference& k : conferences_) names.push_back(k.name);
    setup_ui::railList(c, kLeft, kBodyTop, kRailW, kBottom - 20, names, conference_, focus_ == kRail, schoolRail_,
                       kHitRail);
    if (conferences_.empty()) {
      ui::paragraph(c, "Schools show here once this week's games are in. Their conferences come with the schedule.",
                    kGridX, kBodyTop + 10, 1100, Weight::Medium, 24, theme::white(0.6f), 2);
    } else {
      const sports::Conference& k = conferences_[static_cast<size_t>(conference_)];
      std::vector<setup_ui::PickTile> tiles;
      tiles.reserve(schoolTiles());
      setup_ui::PickTile all;
      all.title = "All of the " + k.name;
      all.detail = "Every " + k.name + " game";
      all.on = conferenceOn(k.key);
      all.strong = true;
      tiles.push_back(all);
      for (const auto& [id, schoolName] : schoolList_) {
        setup_ui::PickTile t;
        t.title = schoolName;
        t.on = contains(draft_.teams, sports::teamKey(sport().league, id));
        tiles.push_back(std::move(t));
      }
      grid(c, tiles, focus_, kBodyTop, gridScroll_);
    }
    bar(c, app_.remoteKeys(), focus_ != kRail, "Conferences");
  }

  sports::MySportsPrefs draft_;
  PicksDone done_;
  std::vector<CollegeSport> sports_;
  std::set<std::string> added_;  // sports given settings here (finish())
  std::vector<sports::Conference> conferences_;
  std::map<std::string, std::string> schoolNames_;  // the sport's schools by id
  int tab_ = 0;
  int focus_ = 0;  // on the options first
  float railScroll_ = 0;
  // The schools page.
  bool onSchools_ = false;
  int conference_ = 0;
  std::vector<std::pair<std::string, std::string>> schoolList_;
  float gridScroll_ = 0, schoolRail_ = 0;
};

// "Your conferences": the sport's conferences A to Z, ticked in a side
// panel over the college page.
void CollegePicks::openConferences() {
  CollegePicks* self = this;
  app_.nav().push(std::make_unique<PanelScreen>(app_, PanelScreen::Side::Trailing, 760.0f, [self](ui::ListPanel& p) {
    p.subtitle = "COLLEGE " + upper(self->sport().name);
    p.title = "Your conferences";
    std::vector<sports::Conference> all = self->conferences();
    if (all.empty())
      p.items.push_back(ui::PanelItem::note("Conferences show here once this week's games are in."));
    for (const sports::Conference& k : all) {
      ui::PanelItem it{ui::PanelItem::Kind::Check, k.name};
      it.on = self->conferenceOn(k.key);
      std::string key = k.key;
      it.select = [self, key] { self->toggleConference(key); };
      p.items.push_back(it);
    }
    p.hint = "Every game of a ticked conference is in your row.";
  }));
}

// ------------------------------------------------------------------ Undo

constexpr uint32_t kUndoMs = 6000;
constexpr int kHitUndo = 1, kHitNote = 2;

class UndoNote : public Screen {
 public:
  UndoNote(App& app, std::string text, std::function<void()> undo)
      : Screen(app), text_(std::move(text)), undo_(std::move(undo)), until_(app.ms() + kUndoMs) {
    version_ = app_.store().events()->version;
  }
  const char* name() const override { return "undo note"; }
  bool opaque() const override { return false; }

  bool tick(uint32_t ms) override {
    if (ms >= until_) {
      app_.nav().pop();
      return true;
    }
    // The board under it changed (the game's league left its row): drawn again.
    uint64_t v = app_.store().events()->version;
    if (v == version_) return false;
    version_ = v;
    return true;
  }
  uint32_t wakeAt() const override { return until_; }

  void hover(int id) override { pointer_ = id; }
  void pointerGone() override { pointer_ = 0; }

  bool key(const KeyEvent& e) override {
    // OK on Undo (the key, or a click on it); anything else closes the note.
    bool undo = e.key == Key::Ok && (pointer_ == 0 || pointer_ == kHitUndo);
    app_.nav().pop();
    if (undo && undo_) undo_();
    return true;
  }

  void draw(ui::Ctx& c) override {
    // The toast's look, with a button.
    const char* button = "Undo";
    float tw = ui::textWidth(c, text_, Weight::Medium, 24);
    float bw = ui::textWidth(c, button, Weight::Bold, 22) + 44;
    float w = std::min(28 + tw + 28 + bw + 12, 1400.0f);
    Rect r{(gfx::kLayoutWidth - w) * 0.5f, gfx::kLayoutHeight - 160.0f, w, 72};
    c.hit({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, kHitNote);
    c.g.fill(r, theme::hex(0x2A2D33, 0.97f), 20);
    ui::labelIn(c, text_, {r.x + 28, r.y, w - 28 - bw - 40, r.h}, Weight::Medium, 24, theme::white());
    Rect b{r.x + r.w - bw - 12, r.y + 12, bw, 48};
    c.hit(b, kHitUndo);
    bool f = !c.pointerActive || pointer_ == kHitUndo;
    c.g.fill(b, f ? theme::white() : theme::white(0.16f), 12);
    ui::labelIn(c, button, b, Weight::Bold, 22, f ? theme::black() : theme::white(), Align::Center);
  }

 private:
  std::string text_;
  std::function<void()> undo_;
  uint32_t until_;
  uint64_t version_ = 0;
  int pointer_ = 0;
};

}  // namespace

std::unique_ptr<Screen> makeSoccerPicks(App& app, const sports::MySportsPrefs& prefs, PicksDone done) {
  return std::make_unique<SoccerPicks>(app, prefs, std::move(done));
}

std::unique_ptr<Screen> makeCollegePicks(App& app, const sports::MySportsPrefs& prefs, const std::string& league,
                                         PicksDone done) {
  return std::make_unique<CollegePicks>(app, prefs, league, std::move(done));
}

sports::MySportsPrefs boardPicks(App& app) {
  const sports::MySportsPrefs& my = app.store().mySports();
  if (my.configured) return my;
  // Not set up yet: the board goes by the defaults (nothing is saved).
  sports::MySportsPrefs p = my;
  sports::MySportsPrefs d = app.store().defaultMySports();
  p.leagues = d.leagues;
  for (const auto& [league, pick] : d.college) p.college.emplace(league, pick);
  return p;
}

namespace {

// Saved from the board only when something changed; picking there sets
// My Sports up.
PicksDone saveFromBoard(App& app, const sports::MySportsPrefs& before) {
  return [&app, before](const sports::MySportsPrefs& after) {
    if (after == before) return;
    sports::MySportsPrefs n = after;
    n.configured = true;
    n.prompted = true;
    app.store().setMySports(n);
    app.toast("My Sports saved");
  };
}

}  // namespace

void openSoccerPicks(App& app) {
  sports::MySportsPrefs p = boardPicks(app);
  app.nav().push(makeSoccerPicks(app, p, saveFromBoard(app, p)));
}

void openCollegePicks(App& app, const std::string& league) {
  sports::MySportsPrefs p = boardPicks(app);
  app.nav().push(makeCollegePicks(app, p, league.empty() ? std::string("NCAAF") : league, saveFromBoard(app, p)));
}

std::string pickName(const std::string& id) {
  if (id == sports::kProWrestlingPick) return "Pro wrestling";
  if (id == sports::kHighSchoolPick) return "High school";
  for (const sports::SoccerCompetition& s : sports::soccerCompetitions())
    if (s.id == id) return s.name;
  return sports::nameOf(id).value_or(id);
}

std::unique_ptr<Screen> makeUndoNote(App& app, std::string text, std::function<void()> undo) {
  return std::make_unique<UndoNote>(app, std::move(text), std::move(undo));
}

}  // namespace marigold::app
