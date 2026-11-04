// The game detail card (LiveEventDetailCard) and Where to Watch
// (LiveEventWherePanel) for Live Events. Build 127: under the card's
// actions, Follow (or Unfollow) each team and Hide its competition, with
// what hiding does under it while it has the focus; hiding leaves a note
// at the bottom to undo it.
#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/events_ui.hpp"
#include "app/screens/panel_screen.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"

namespace marigold::app {
namespace {

using ui::Align;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;

bool watchEvent(App& app, const std::string& id, const std::string& entryOrChannel = "") {
  Playback& p = app.playback();
  bool ok = entryOrChannel.empty() ? p.tune(id, app.store().eventFlipList(id))
                                   : p.tuneEventVia(id, entryOrChannel, app.store().eventFlipList(id));
  if (!ok) {
    app.toast("This game has no working feed right now");
    return false;
  }
  p.setFull();
  app.nav().push(makePlayer(app));
  return true;
}

constexpr int kHitAction = 1, kHitMore = 101;  // + the pill's index

class EventDetail : public Screen {
 public:
  EventDetail(App& app, std::string id) : Screen(app), id_(std::move(id)) {}
  const char* name() const override { return "game details"; }

  bool opaque() const override { return false; }

  void hover(int id) override {
    const EventTile* t = app_.store().eventTile(id_);
    if (id >= kHitMore && id - kHitMore < static_cast<int>(more(t).size())) {
      row_ = 1;
      focus_ = id - kHitMore;
    } else if (id >= kHitAction && id - kHitAction < buttons(t)) {
      row_ = 0;
      focus_ = id - kHitAction;
    }
  }

  bool key(const KeyEvent& e) override {
    const EventTile* t = app_.store().eventTile(id_);
    std::vector<More> extra = more(t);
    if (row_ == 1 && extra.empty()) row_ = 0;
    int n = row_ == 0 ? buttons(t) : static_cast<int>(extra.size());
    switch (e.key) {
      case Key::Left:
        focus_ = std::max(0, focus_ - 1);
        return true;
      case Key::Right:
        focus_ = std::min(n - 1, focus_ + 1);
        return true;
      case Key::Down:
        if (row_ == 0 && !extra.empty()) {
          row_ = 1;
          focus_ = std::min(focus_, static_cast<int>(extra.size()) - 1);
        }
        return true;
      case Key::Up:
        if (row_ == 1) {
          row_ = 0;
          focus_ = std::min(focus_, buttons(t) - 1);
        }
        return true;
      case Key::Ok: {
        if (!t) return true;
        if (row_ == 1) {
          if (focus_ >= 0 && focus_ < static_cast<int>(extra.size())) act(extra[static_cast<size_t>(focus_)]);
          return true;
        }
        std::string action = buttonTitle(t, focus_);
        std::string id = id_;
        App& app = app_;
        if (action == "Where to Watch") {
          app.nav().pop();
          app.nav().push(makeWhereToWatch(app, id));
        } else if (action == "Remind Me" || action == "Reminder Set") {
          bool on = app.store().toggleReminder(id);
          app.toast(on ? "We'll show a card when it starts" : "Reminder removed");
        } else {
          app.nav().pop();
          watchEvent(app, id);
        }
        return true;
      }
      default:
        return e.key != Key::Back;
    }
  }

  void draw(ui::Ctx& c) override {
    ui::scrim(c, 0.6f);
    const EventTile* t = app_.store().eventTile(id_);
    std::vector<More> extra = more(t);
    // Taller with a row of Follow and Hide under the actions.
    float h = extra.empty() ? 680 : 800;
    Rect card{(gfx::kLayoutWidth - 1150) * 0.5f, (gfx::kLayoutHeight - h) * 0.5f, 1150, h};
    c.g.fill(card, theme::hex(0x1C1F25, 0.98f), 34);
    if (!t) {
      ui::labelIn(c, "This game is no longer listed.", card, Weight::Medium, 26, theme::white(0.7f), Align::Center);
      return;
    }
    float x = card.x + 60, y = card.y + 50, w = card.w - 120;
    if (!t->leagueChip.empty()) ui::chip(c, t->leagueChip, x, y, 20, theme::white(0.14f), theme::white());
    y += 56;
    float tx = x;
    if (t->sides.size() == 2) {
      events_ui::badge(c, t->sides[0], tx, y, 70, false);
      events_ui::badge(c, t->sides[1], tx + 84, y, 70, false);
      tx += 180;
    }
    y += 6;
    ui::paragraph(c, t->heroTitle.empty() ? t->title : t->heroTitle, tx, y, card.x + card.w - 60 - tx, Weight::Bold, 44,
                  theme::white(), 2, 1.15f);
    y += 120;
    // The state and status, as on the tile (live: red text alone), then
    // where it plays.
    bool live = events_ui::live(*t);
    std::string summary = t->heroStatus.empty() ? t->status : t->heroStatus;
    if (!live && !t->venue.empty()) summary += " \xc2\xb7 " + t->venue;
    float sw = events_ui::status(c, *t, summary, x, y, Weight::Regular, 24, theme::white(0.8f), w, 4, 12);
    if (live && !t->venue.empty())
      ui::label(c, " \xc2\xb7 " + t->venue, x + sw, y, Weight::Regular, 24, theme::white(0.8f), w - sw);
    y += 50;
    ui::paragraph(c, t->whereLine, x, y, w, Weight::Regular, 22, theme::white(0.7f), 2);
    y += 70;
    if (!t->watchable)
      ui::paragraph(c,
                    "It isn't on any of your channels or provider event feeds. If your provider adds a feed for it, "
                    "it shows up here by itself.",
                    x, y, w, Weight::Regular, 20, theme::white(0.55f), 2);
    // Pills.
    float px = x;
    int n = buttons(t);
    float py = card.y + card.h - (extra.empty() ? 120 : 280);
    for (int i = 0; i < n; ++i) {
      std::string title = buttonTitle(t, i);
      float pw = ui::chipWidth(c, title, 24) + 40;
      Rect pill{px, py, pw, 70};
      c.hit(pill, kHitAction + i);
      bool f = row_ == 0 && i == focus_;
      c.g.fill(pill, f ? theme::white() : theme::white(0.12f), 35);
      ui::labelIn(c, title, pill, Weight::Bold, 24, f ? theme::black() : theme::white(), Align::Center);
      px += pw + 20;
    }
    if (extra.empty()) return;
    // Follow each team, Hide the competition: smaller, under the actions.
    px = x;
    py += 96;
    for (size_t i = 0; i < extra.size(); ++i) {
      float pw = ui::chipWidth(c, extra[i].title, 22) + 36;
      if (px + pw > card.x + card.w - 60) break;
      Rect pill{px, py, pw, 60};
      c.hit(pill, kHitMore + static_cast<int>(i));
      bool f = row_ == 1 && static_cast<int>(i) == focus_;
      c.g.fill(pill, f ? theme::white() : theme::white(0.08f), 30);
      if (!f) c.g.stroke(pill, theme::white(0.18f), 30, 1.5f);
      ui::labelIn(c, extra[i].title, pill, Weight::Bold, 22, f ? theme::black() : theme::white(), Align::Center);
      px += pw + 16;
    }
    // What the focused one does.
    if (row_ == 1 && focus_ >= 0 && focus_ < static_cast<int>(extra.size()))
      ui::label(c, extra[static_cast<size_t>(focus_)].line, x, py + 76, Weight::Regular, 21, theme::white(0.6f), w);
  }

 private:
  // The second row: Follow / Unfollow each team, then Hide.
  struct More {
    enum class Kind { Follow, Hide } kind;
    std::string title, line;
    std::string key, name;  // the team's key and name, or the competition's id and name
  };
  std::vector<More> more(const EventTile* t) const {
    std::vector<More> out;
    if (!t) return out;
    for (const EventTile::FollowSide& s : t->follow) {
      if (s.key.empty()) continue;
      bool on = app_.store().followsTeam(s.key);
      out.push_back({More::Kind::Follow, (on ? "Unfollow " : "Follow ") + s.name,
                     on ? "Their games stop leading Live Events." : "Their games lead Live Events.", s.key, s.name});
    }
    if (!t->competitionId.empty())
      out.push_back({More::Kind::Hide, "Hide " + t->competitionName, events_ui::hideLine(app_, *t), t->competitionId,
                     t->competitionName});
    return out;
  }
  void act(const More& m) {
    App& app = app_;
    if (m.kind == More::Kind::Follow) {
      bool now = !app.store().followsTeam(m.key);
      app.store().followTeam(m.key, now);
      app.toast(now ? "Following " + m.name : "Stopped following " + m.name);
      return;
    }
    std::string id = m.key, name = m.name;
    app.nav().pop();
    events_ui::hide(app, id, name);
  }

  int buttons(const EventTile* t) const {
    if (!t) return 0;
    int n = 1;                    // Where to Watch
    if (t->upcoming) ++n;         // Remind Me
    if (t->watchable && !t->upcoming && !t->finished) ++n;  // Watch
    return n;
  }
  std::string buttonTitle(const EventTile* t, int i) const {
    std::vector<std::string> list;
    if (t->upcoming) list.push_back(app_.store().hasReminder(id_) ? "Reminder Set" : "Remind Me");
    if (t->watchable && !t->upcoming && !t->finished)
      list.push_back("Watch " + (t->channelName.empty() ? std::string("Now") : t->channelName + " Now"));
    list.push_back("Where to Watch");
    return i >= 0 && i < static_cast<int>(list.size()) ? list[static_cast<size_t>(i)] : std::string();
  }

  std::string id_;
  int row_ = 0;  // 0 the actions, 1 Follow and Hide
  int focus_ = 0;
};

}  // namespace

std::unique_ptr<Screen> makeEventDetail(App& app, const std::string& eventId) {
  return std::make_unique<EventDetail>(app, eventId);
}

std::unique_ptr<Screen> makeWhereToWatch(App& app, const std::string& eventId) {
  std::string id = eventId;
  auto panel = std::make_unique<PanelScreen>(app, PanelScreen::Side::Trailing, 800.0f, [&app, id](ui::ListPanel& p) {
    WhereToWatch w = app.store().whereToWatch(id);
    p.subtitle = "WHERE TO WATCH";
    p.title = w.title;
    // While the game plays (the player's Sources), the rows are checks like
    // a channel's Sources panel: the one playing is ticked and says so, and
    // picking another switches in place.
    Playback& pb = app.playback();
    bool watching = pb.entry() == id;
    std::string playing = watching ? pb.playingChannel() : std::string();
    bool found = false;
    auto isPlaying = [&](const WatchSource& s) {
      if (found || playing.empty()) return false;
      if (s.entryId.empty()) return s.channelId == playing;
      for (const Feed& f : app.store().feeds(s.entryId))
        if (f.channelId == playing) return true;
      return false;
    };
    for (const auto& [section, sources] : w.sections) {
      p.items.push_back(ui::PanelItem::header(section));
      for (const WatchSource& s : sources) {
        ui::PanelItem it{watching ? ui::PanelItem::Kind::Check : ui::PanelItem::Kind::Chevron, s.title};
        it.detail = s.detail;
        if (isPlaying(s)) {
          found = true;
          it.on = true;
          std::string now = pb.ready() ? "Playing now" : "Opening\xe2\x80\xa6";
          it.detail = s.detail.empty() ? now : now + " \xc2\xb7 " + s.detail;
        }
        std::string via = s.entryId.empty() ? s.channelId : s.entryId;
        it.select = [&app, id, via] {
          Playback& play = app.playback();
          if (play.entry() == id) {
            if (!play.tuneEventVia(id, via, app.store().eventFlipList(id)))
              app.toast("That source has no working feed right now");
            return;
          }
          app.nav().pop();
          watchEvent(app, id, via);
        };
        p.items.push_back(it);
      }
    }
    if (!w.note.empty()) p.items.push_back(ui::PanelItem::note(w.note));
    if (w.sections.empty()) p.items.push_back(ui::PanelItem::note("Not on any of your channels."));
    p.hint = watching ? "OK switches at once; the others stay as automatic backups. Back closes."
                      : "Whichever you pick, the others stay as automatic backups.";
  });
  panel->liveRows = true;  // the tick follows a switch ("Opening…", then "Playing now")
  return panel;
}

}  // namespace marigold::app
