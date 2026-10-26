// The game detail card (LiveEventDetailCard) and Where to Watch
// (LiveEventWherePanel) for Live Events.
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

class EventDetail : public Screen {
 public:
  EventDetail(App& app, std::string id) : Screen(app), id_(std::move(id)) {}

  bool opaque() const override { return false; }

  bool key(const KeyEvent& e) override {
    const EventTile* t = app_.store().eventTile(id_);
    int n = buttons(t);
    switch (e.key) {
      case Key::Left:
        focus_ = std::max(0, focus_ - 1);
        return true;
      case Key::Right:
        focus_ = std::min(n - 1, focus_ + 1);
        return true;
      case Key::Ok: {
        if (!t) return true;
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
    Rect card{(gfx::kLayoutWidth - 1150) * 0.5f, 200, 1150, 680};
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
    float sx = x + events_ui::pill(c, *t, x, y + 4, false);
    if (sx > x) sx += 12;
    std::string summary = t->heroStatus.empty() ? t->status : t->heroStatus;
    if (!t->venue.empty()) summary += " \xc2\xb7 " + t->venue;
    ui::label(c, summary, sx, y, Weight::Regular, 24, theme::white(0.8f), x + w - sx);
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
    for (int i = 0; i < n; ++i) {
      std::string title = buttonTitle(t, i);
      float pw = ui::chipWidth(c, title, 24) + 40;
      Rect pill{px, card.y + card.h - 120, pw, 70};
      bool f = i == focus_;
      c.g.fill(pill, f ? theme::white() : theme::white(0.12f), 35);
      ui::labelIn(c, title, pill, Weight::Bold, 24, f ? theme::black() : theme::white(), Align::Center);
      px += pw + 20;
    }
  }

 private:
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
  int focus_ = 0;
};

}  // namespace

std::unique_ptr<Screen> makeEventDetail(App& app, const std::string& eventId) {
  return std::make_unique<EventDetail>(app, eventId);
}

std::unique_ptr<Screen> makeWhereToWatch(App& app, const std::string& eventId) {
  std::string id = eventId;
  return std::make_unique<PanelScreen>(app, PanelScreen::Side::Trailing, 800.0f, [&app, id](ui::ListPanel& p) {
    WhereToWatch w = app.store().whereToWatch(id);
    p.subtitle = "WHERE TO WATCH";
    p.title = w.title;
    for (const auto& [section, sources] : w.sections) {
      p.items.push_back(ui::PanelItem::header(section));
      for (const WatchSource& s : sources) {
        ui::PanelItem it{ui::PanelItem::Kind::Chevron, s.title};
        it.detail = s.detail;
        std::string via = s.entryId.empty() ? s.channelId : s.entryId;
        it.select = [&app, id, via] {
          app.nav().pop();
          watchEvent(app, id, via);
        };
        p.items.push_back(it);
      }
    }
    if (!w.note.empty()) p.items.push_back(ui::PanelItem::note(w.note));
    if (w.sections.empty()) p.items.push_back(ui::PanelItem::note("Not on any of your channels."));
    p.hint = "Whichever you pick, the others stay as automatic backups.";
  });
}

}  // namespace marigold::app
