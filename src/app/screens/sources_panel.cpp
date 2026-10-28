// Red key (or Sources in the player's action row): every feed behind a
// channel, best first, with what each is doing (playing, a backup, or why
// it failed lately), its quality, East/West and the provider's name (never
// a link). Picking one switches in place; the picture keeps playing beside
// the panel.
#include <algorithm>
#include <memory>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/panel_screen.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"
#include "core/catalog/lineup.hpp"
#include "core/util/strings.hpp"

namespace marigold::app {
namespace {

std::string agoText(time::Seconds t, time::Seconds now) {
  time::Seconds d = std::max<time::Seconds>(0, now - t);
  if (d < 60) return "just now";
  if (d < 3600) return std::to_string(d / 60) + " min ago";
  return std::to_string(d / 3600) + " h ago";
}

// "HBO" for both of HBO's versions (the panel names the version below).
std::string catalogName(const GuideChannel* g) {
  if (!g->west) return g->name;
  for (const char* tail : {" West", " (West)", " Pacific", " (Pacific)"}) {
    if (str::endsWith(g->name, tail)) return g->name.substr(0, g->name.size() - std::string(tail).size());
  }
  return g->name;
}

// What a channel shows now ("" without listings).
std::string nowOn(App& app, const std::string& id, time::Seconds now) {
  auto li = app.store().listings();
  const std::vector<Listing>* v = li ? li->forChannel(id) : nullptr;
  if (!v) return "";
  for (const Listing& l : *v) {
    if (l.isOn(now)) return l.title + (l.episode.empty() ? "" : " \xc2\xb7 " + l.episode);
  }
  return "";
}

// East/West: both versions (the one playing switches in place), and the
// other one into My Lineup in one press.
void versionRows(App& app, ui::ListPanel& p, std::shared_ptr<std::string> current) {
  auto ch = app.store().channels();
  const GuideChannel* g = ch->find(*current);
  const GuideChannel* other = g && !g->partner.empty() ? ch->find(g->partner) : nullptr;
  if (!other) return;
  Playback& pb = app.playback();
  time::Seconds now = time::now();
  p.items.push_back(ui::PanelItem::header("VERSION"));
  const GuideChannel* east = g->west ? other : g;
  const GuideChannel* west = g->west ? g : other;
  for (const GuideChannel* v : {east, west}) {
    ui::PanelItem it{ui::PanelItem::Kind::Check, v->west ? "West" : "East"};
    it.coast = v->west ? 1 : 0;
    bool playing = pb.entry() == v->id;
    std::string on = nowOn(app, v->id, now);
    if (playing) it.detail = "Playing now" + (on.empty() ? std::string() : " \xc2\xb7 " + on);
    else it.detail = on.empty() ? std::string(v->west ? "3 h behind" : "Eastern times")
                                : "Now: " + on + (v->west ? " \xc2\xb7 3 h behind" : "");
    it.on = playing;
    std::string vid = v->id;
    it.select = [&app, vid, current] {
      Playback& play = app.playback();
      // Switches what's playing; from the guide (nothing of this channel
      // playing) the rows only say what's on.
      if (play.entry() == vid || play.entry() != catalog::coastPartnerId(vid)) return;
      if (!play.tune(vid)) {
        app.toast("This version has no working feed right now");
        return;
      }
      *current = vid;
    };
    p.items.push_back(it);
  }
  if (other->inLineup && g->inLineup) {
    ui::PanelItem both{ui::PanelItem::Kind::Check, "Both versions are in My Lineup"};
    both.on = true;
    both.detail = other->name + " is right " + (other->west ? "below " : "above ") + g->name + " \xc2\xb7 OK removes it";
    std::string oid = other->id, name = other->name;
    both.select = [&app, oid, name] {
      app.store().toggleLineup(oid);
      app.toast(name + " removed from My Lineup");
    };
    p.items.push_back(both);
  } else {
    // The one not in My Lineup (the other one, or this one when only the
    // other is there).
    const GuideChannel* missing = other->inLineup ? g : other;
    const GuideChannel* partner = missing == g ? other : g;
    ui::PanelItem add{ui::PanelItem::Kind::Action, "+ Add " + missing->name + " to My Lineup"};
    add.detail = std::string("Goes right ") + (missing->west ? "below " : "above ") + partner->name +
                 ", so it is one channel away";
    std::string mid = missing->id, line = missing->name + " added to My Lineup, right " +
                                          (missing->west ? "below " : "above ") + partner->name;
    add.select = [&app, mid, line] {
      app.store().toggleLineup(mid);
      app.toast(line);
    };
    p.items.push_back(add);
  }
}

}  // namespace

std::unique_ptr<Screen> makeSources(App& app, const std::string& entryId) {
  auto current = std::make_shared<std::string>(entryId);
  auto panel = std::make_unique<PanelScreen>(app, PanelScreen::Side::Trailing, 760.0f, [&app, current](ui::ListPanel& p) {
    std::string id = *current;
    auto ch = app.store().channels();
    const GuideChannel* g = ch->find(id);
    p.subtitle = "SOURCES";
    p.title = g ? (g->partner.empty() ? g->name : catalogName(g)) : "Sources";
    versionRows(app, p, current);
    Playback& pb = app.playback();
    std::vector<Feed> feeds = pb.entry() == id ? pb.feeds() : app.store().feeds(id);
    if (feeds.empty()) {
      p.items.push_back(ui::PanelItem::note("No feeds for this channel right now."));
      return;
    }
    std::string pinned = app.store().pinnedFeed(id);
    p.items.push_back(ui::PanelItem::header(g && !g->partner.empty() ? (g->west ? "WATCH WITH (WEST)" : "WATCH WITH (EAST)")
                                                                     : "WATCH WITH"));
    bool watching = pb.entry() == id;
    time::Seconds now = time::now();
    for (size_t i = 0; i < feeds.size(); ++i) {
      ui::PanelItem it{ui::PanelItem::Kind::Check, "Feed " + std::to_string(i + 1)};
      bool playing = watching && pb.feedIndex() == i;
      // What it is doing: playing, failed lately (and why), or a backup.
      std::string status;
      if (playing) status = pb.ready() ? "Playing now" : "Opening\xe2\x80\xa6";
      else if (auto problem = pb.feedProblem(feeds[i].channelId))
        status = "Last try " + agoText(problem->at, now) + ": " + problem->why;
      else status = i == 0 ? "Main feed" : "Backup";
      it.detail = status + " \xc2\xb7 " + feeds[i].label;
      if (feeds[i].channelId == pinned) it.detail += " \xc2\xb7 Always first";
      it.on = playing;
      it.select = [&app, id, i] {
        Playback& play = app.playback();
        if (play.entry() != id && !play.tune(id)) return;
        play.useFeed(i);
      };
      p.items.push_back(it);
    }
    if (feeds.size() > 1 && app.store().channels()->find(id)) {
      // "Always start with" (pinned feed): Automatic, or one version.
      p.items.push_back(ui::PanelItem::header("ALWAYS START WITH"));
      ui::PanelItem automatic{ui::PanelItem::Kind::Check, "Automatic"};
      automatic.detail = "The feed that played last, else the most reliable";
      automatic.on = pinned.empty();
      automatic.select = [&app, id] {
        app.store().pinFeed(id, "");
        app.toast("Picks its best feed automatically");
      };
      p.items.push_back(automatic);
      for (size_t i = 0; i < feeds.size(); ++i) {
        ui::PanelItem it{ui::PanelItem::Kind::Check, i == 0 ? "Main feed" : "Feed " + std::to_string(i + 1)};
        it.detail = feeds[i].label;
        it.on = feeds[i].channelId == pinned;
        std::string channel = feeds[i].channelId;
        it.select = [&app, id, channel] {
          app.store().pinFeed(id, channel);
          app.toast("Will start with this feed");
        };
        p.items.push_back(it);
      }
    }
    p.hint = "OK switches at once; the others stay as automatic backups. Back closes.";
  });
  panel->liveRows = true;  // "Opening…" turns into "Playing now"
  return panel;
}

}  // namespace marigold::app
