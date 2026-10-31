// Search (full screen, LiveTVSearchView): channels as you type, Live Events,
// and shows on now and later from the loaded guide. Channel scoring follows
// the tvOS search: exact 95, number 90, prefix 80, call-sign prefix 75, word
// prefix 70, contains 50, neighborhood 20; +5 favourite, +10 in My Lineup; at
// most 80 results.
#include <algorithm>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"
#include "core/engine/views.hpp"
#include "core/util/strings.hpp"

namespace marigold::app {
namespace {

using ui::Align;
using ui::Color;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;

constexpr size_t kMaxResults = 80;
constexpr float kRowH = 84;

constexpr size_t kMaxEvents = 40;
constexpr uint32_t kProgramDelayMs = 450;  // shows are searched once typing pauses

class SearchScreen : public Screen {
 public:
  SearchScreen(App& app, std::function<void(const std::string&)> pick) : Screen(app), pick_(std::move(pick)) {}

  void shown() override {
    if (!openedOnce_) {
      openedOnce_ = true;
      if (!app_.openKeyboard(query_)) app_.toast("This TV has no on-screen keyboard");
    }
  }

  void text(const std::string& value, bool closed, bool) override {
    if (value != query_) {
      query_ = value;
      search();
    }
    if (closed && !rows_.empty()) focus_ = firstFocusable(0, 1);
  }

  bool tick(uint32_t ms) override {
    if (programsAt_ && ms >= programsAt_) {
      programsAt_ = 0;
      programs_ = app_.store().searchPrograms(query_);
      searchingPrograms_ = false;
      layout();
      return true;
    }
    return programsAt_ != 0;
  }

  bool key(const KeyEvent& e) override {
    switch (e.key) {
      case Key::Up:
        if (focus_ >= 0) focus_ = firstFocusable(focus_ - 1, -1);
        return true;
      case Key::Down: {
        int next = firstFocusable(focus_ + 1, 1);
        if (next >= 0) focus_ = next;
        return true;
      }
      case Key::Ok:
        if (focus_ < 0) app_.openKeyboard(query_);
        else if (focus_ < static_cast<int>(rows_.size())) open(rows_[static_cast<size_t>(focus_)]);
        return true;
      case Key::Back:
        if (app_.keyboardOpen()) {
          app_.closeKeyboard();
          return true;
        }
        return false;
      default:
        return true;
    }
  }

  void hover(int id) override {
    if (id == 1) focus_ = -1;
    else if (id >= 100 && id - 100 < static_cast<int>(rows_.size()) && rows_[static_cast<size_t>(id - 100)].kind != Row::Kind::Header)
      focus_ = id - 100;
  }

  void draw(ui::Ctx& c) override {
    float x = 80, y = 60;
    ui::label(c, "Search", x, y, Weight::Bold, 44, theme::white());
    y += 80;
    Rect field{x, y, gfx::kLayoutWidth - 160, 76};
    c.hit(field, 1);
    bool ff = focus_ < 0;
    c.g.fill(field, ff ? theme::white() : theme::white(0.1f), 38);
    std::string shown = query_.empty() ? "Channels, teams or shows" : query_;
    ui::labelIn(c, shown, {field.x + 36, field.y, field.w - 72, field.h}, Weight::Medium, 28,
                ff ? theme::black(query_.empty() ? 0.45f : 1) : theme::white(query_.empty() ? 0.5f : 1));
    y += 110;
    if (str::trim(query_).empty()) {
      ui::label(c, "Search by channel name, number, call sign, team or show.", x, y, Weight::Regular, 24,
                theme::white(0.6f));
      return;
    }
    auto ch = app_.store().channels();
    auto li = app_.store().listings();
    float top = y, bottom = gfx::kLayoutHeight - 40;
    // Scroll so the focused row stays in view.
    float focusY = 0, focusH = 0, total = 0;
    for (size_t i = 0; i < rows_.size(); ++i) {
      float h = height(rows_[i]);
      if (static_cast<int>(i) == focus_) {
        focusY = total;
        focusH = h;
      }
      total += h;
    }
    if (focus_ >= 0) {
      if (focusY - 60 < scroll_) scroll_ = std::max(0.0f, focusY - 60);
      if (focusY + focusH + 60 > scroll_ + (bottom - top)) scroll_ = focusY + focusH + 60 - (bottom - top);
    } else {
      scroll_ = 0;
    }
    c.g.pushClip({0, top, gfx::kLayoutWidth, bottom - top});
    float ry = top - scroll_;
    float w = gfx::kLayoutWidth - 160;
    for (size_t i = 0; i < rows_.size(); ++i) {
      const Row& row = rows_[i];
      float h = height(row);
      float rowY = ry;
      ry += h;
      if (rowY + h < top || rowY > bottom) continue;
      if (row.kind == Row::Kind::Header) {
        ui::label(c, row.title, x, rowY + 14, Weight::Bold, 16, theme::white(0.55f));
        continue;
      }
      if (row.kind == Row::Kind::Note) {
        ui::label(c, row.title, x, rowY + 8, Weight::Regular, 22, theme::white(0.55f));
        continue;
      }
      Rect r{x, rowY, w, kRowH};
      c.hit(r, 100 + static_cast<int>(i));
      bool f = static_cast<int>(i) == focus_;
      Color fg = f ? theme::black() : theme::white();
      Color sub = f ? theme::black(0.6f) : theme::white(0.6f);
      c.g.fill(r, f ? theme::white() : theme::white(0.07f), theme::kRowRadius);
      if (row.kind == Row::Kind::Channel) {
        const GuideChannel* g = ch->find(row.id);
        if (!g) continue;
        ui::logo(c, g->logoUrl, g->name, {r.x + 20, r.y + 14, 100, 56}, f);
        ui::label(c, g->name, r.x + 150, r.y + 12, Weight::Bold, 25, fg, r.w - 180);
        std::string line = g->inLineup ? "My Lineup \xc2\xb7 " + g->section : g->section;
        if (g->feeds > 1) line += " \xc2\xb7 " + std::to_string(g->feeds) + " feeds";
        if (const std::vector<Listing>* v = li->forChannel(g->id)) {
          for (const Listing& l : *v) {
            if (l.isOn(c.now)) {
              line += " \xc2\xb7 Now: " + l.title;
              break;
            }
          }
        }
        ui::label(c, line, r.x + 150, r.y + 48, Weight::Medium, 19, sub, r.w - 180);
      } else if (row.kind == Row::Kind::Event) {
        const EventTile* t = app_.store().eventTile(row.id);
        if (!t) continue;
        float tx = r.x + 24;
        if (t->pill == EventTile::Pill::Live || t->pill == EventTile::Pill::OnNow)
          tx += ui::chip(c, "LIVE", tx, r.y + 14, 15, theme::hex(theme::kLive), theme::white()) + 10;
        std::string title = !t->heroTitle.empty() ? t->heroTitle : t->title;
        ui::label(c, title, tx, r.y + 12, Weight::Bold, 25, fg, r.x + r.w - 24 - tx);
        std::string line = t->leagueChip.empty() ? t->league : t->leagueChip;
        if (!t->status.empty()) line += (line.empty() ? "" : " \xc2\xb7 ") + t->status;
        if (!t->channelName.empty()) line += " \xc2\xb7 " + t->channelName;
        ui::label(c, line, r.x + 24, r.y + 48, Weight::Medium, 19, sub, r.w - 48);
      } else {
        const GuideChannel* g = ch->find(row.hit.entryId);
        const Listing& l = row.hit.listing;
        float tx = r.x + 24;
        if (l.isOn(c.now)) tx += ui::chip(c, "LIVE", tx, r.y + 14, 15, theme::hex(theme::kLive), theme::white()) + 10;
        ui::label(c, l.title, tx, r.y + 12, Weight::Bold, 25, fg, r.x + r.w - 24 - tx);
        std::string day = ui::dayWord(c, l.start);
        std::string line = (g ? g->name : std::string()) + " \xc2\xb7 " + (day.empty() ? "" : day + " ") +
                           ui::range(c, l.start, l.end, true);
        if (!l.episode.empty()) line += " \xc2\xb7 " + l.episode;
        if (app_.store().hasShowReminder(row.hit.entryId, l.start)) line += " \xc2\xb7 Reminder set";
        ui::label(c, line, r.x + 24, r.y + 48, Weight::Medium, 19, sub, r.w - 48);
      }
    }
    c.g.popClip();
  }

 private:
  struct Row {
    enum class Kind { Header, Note, Channel, Event, Program } kind = Kind::Header;
    std::string title;  // header / note
    std::string id;     // entry or event id
    ProgramHit hit;
  };

  static float height(const Row& r) {
    switch (r.kind) {
      case Row::Kind::Header: return 48;
      case Row::Kind::Note: return 44;
      default: return kRowH + 8;
    }
  }

  int firstFocusable(int from, int step) const {
    for (int i = from; i >= 0 && i < static_cast<int>(rows_.size()); i += step) {
      Row::Kind k = rows_[static_cast<size_t>(i)].kind;
      if (k != Row::Kind::Header && k != Row::Kind::Note) return i;
    }
    return -1;
  }

  void open(const Row& row) {
    if (row.kind == Row::Kind::Channel) {
      auto pick = pick_;
      std::string id = row.id;
      app_.nav().pop();
      if (pick) pick(id);
      return;
    }
    if (row.kind == Row::Kind::Event) {
      const EventTile* t = app_.store().eventTile(row.id);
      if (!t) return;
      std::string id = row.id;
      App& app = app_;
      if (t->watchable && !t->upcoming && !t->finished) {
        app.nav().pop();
        Playback& p = app.playback();
        if (!p.tune(id, app.store().eventFlipList(id))) {
          app.toast("This game has no working feed right now");
          return;
        }
        p.setFull();
        app.nav().push(makePlayer(app));
      } else {
        app.nav().push(makeEventDetail(app, id));
      }
      return;
    }
    if (row.kind == Row::Kind::Program) {
      App& app = app_;
      ProgramHit h = row.hit;
      if (h.listing.start > time::now() + 60) {
        app.nav().push(makeProgramDetail(app, h.entryId, h.listing));
        return;
      }
      app.nav().pop();
      Playback& p = app.playback();
      if (!p.tune(h.entryId)) {
        app.toast("This channel has no working feed right now");
        return;
      }
      p.setFull();
      app.nav().push(makePlayer(app));
    }
  }

  void search() {
    channels_.clear();
    events_.clear();
    programs_.clear();
    focus_ = -1;
    scroll_ = 0;
    std::string q = engine::views::folded(std::string(str::trim(query_)));
    programsAt_ = 0;
    searchingPrograms_ = false;
    if (!q.empty()) {
      channels_ = engine::views::searchChannels(*app_.store().channels(), query_, kMaxResults);
      events_ = app_.store().searchEvents(query_);
      if (events_.size() > kMaxEvents) events_.resize(kMaxEvents);
      if (q.size() >= 2) {
        searchingPrograms_ = true;
        programsAt_ = app_.ms() + kProgramDelayMs;
      }
    }
    layout();
  }

  void layout() {
    std::string keep = focus_ >= 0 && focus_ < static_cast<int>(rows_.size()) ? key(rows_[static_cast<size_t>(focus_)]) : "";
    rows_.clear();
    if (!channels_.empty()) {
      rows_.push_back({Row::Kind::Header, "CHANNELS", "", {}});  // never how many
      for (const std::string& id : channels_) rows_.push_back({Row::Kind::Channel, "", id, {}});
    }
    if (!events_.empty()) {
      rows_.push_back({Row::Kind::Header, "LIVE EVENTS (" + std::to_string(events_.size()) + ")", "", {}});
      for (const std::string& id : events_) rows_.push_back({Row::Kind::Event, "", id, {}});
    }
    if (engine::views::folded(std::string(str::trim(query_))).size() >= 2) {
      rows_.push_back({Row::Kind::Header, "SHOWS ON NOW & LATER", "", {}});
      if (programs_.empty())
        rows_.push_back({Row::Kind::Note, searchingPrograms_ ? "Searching the guide\xe2\x80\xa6" : "No shows found in the guide", "", {}});
      for (const ProgramHit& h : programs_) rows_.push_back({Row::Kind::Program, "", "", h});
    }
    if (!keep.empty()) {
      for (size_t i = 0; i < rows_.size(); ++i)
        if (key(rows_[i]) == keep) focus_ = static_cast<int>(i);
    }
    if (focus_ >= static_cast<int>(rows_.size())) focus_ = -1;
  }

  static std::string key(const Row& r) {
    switch (r.kind) {
      case Row::Kind::Channel: return "c" + r.id;
      case Row::Kind::Event: return "e" + r.id;
      case Row::Kind::Program: return "p" + r.hit.entryId + "|" + std::to_string(r.hit.listing.start);
      default: return "";
    }
  }

  std::function<void(const std::string&)> pick_;
  std::string query_;
  std::vector<std::string> channels_, events_;
  std::vector<ProgramHit> programs_;
  bool searchingPrograms_ = false;
  uint32_t programsAt_ = 0;
  std::vector<Row> rows_;
  int focus_ = -1;  // -1 = the field
  float scroll_ = 0;
  bool openedOnce_ = false;
};

}  // namespace

std::unique_ptr<Screen> makeSearch(App& app, std::function<void(const std::string&)> pick) {
  return std::make_unique<SearchScreen>(app, std::move(pick));
}

}  // namespace marigold::app
