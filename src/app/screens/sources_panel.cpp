// Red key (or Sources in the player's action row): every feed behind a
// channel, best first, with what each is doing (playing, a backup, or why
// it failed lately), its quality, East/West and the provider's name (never
// a link). Picking one switches in place; the picture keeps playing beside
// the panel.
//
// Build 181, "This feed is wrong…": hold OK on a feed for a small menu
// beside the panel (Play this feed, Always start with this feed, This
// feed is wrong…); the reasons, and for "It's a different channel" which
// one it is (optional); then back here with a notice at the top and the
// feed's row under SET ASIDE focused, so OK again uses it again.
//
// Build 182: during a replay (paused or not) the panel is the replay's:
// "REPLAY · 8:42 PM" in the corner, the copies whose archive reaches that
// moment ("REPLAY FROM 8:42 PM WITH", in the order the replay would pick
// them, the check mark on the copy replaying) and the others, dimmed, with
// why ("CAN'T REPLAY 8:42 PM"). OK on one carries the replay on there from
// the same moment (the panel closes; the player says how it went). Hold OK
// on a replay row: Replay from this feed, This feed's replay is wrong… (its
// replay set aside for the channel; live keeps it). Going live stays on
// the controls' Live button.
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/panel_screen.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"
#include "app/ui/ui.hpp"
#include "core/catalog/lineup.hpp"
#include "core/engine/source_test.hpp"
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

// "8:42 PM" (or "20:42"), as the screens say a time.
std::string clockAt(App& app, time::Seconds t) {
  const time::Zone& z = app.zone();
  ui::Ctx tmp{app.renderer(), app.textRenderer(), nullptr, nullptr, time::now(), 0, &z};
  tmp.clock24 = app.store().settings().clock24;
  return ui::clock(tmp, t);
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
        const GuideChannel* version = app.store().channels()->find(vid);
        app.toast((version ? version->name : std::string("That version")) + " has no working feed right now");
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


// Build 181: Sources' own state across rebuilds: the panel itself (the
// notice goes on it, the menus go back to it) and a feed just set aside
// whose row takes the focus ("OK again to undo").
struct SourcesState {
  PanelScreen* screen = nullptr;
  std::string focusCopy;
  bool focusReplay = false;  // Build 182: its replay record's row
  int focusRow = -1;  // its row, as the last build placed it
};

// A feed as Sources shows it: its index in the list it plays from (the
// session's while the channel plays, else the store's), and its number.
// Feeds set aside for the channel aren't among them; their numbers stay
// theirs (setaside::feedNumbers).
struct Shown {
  size_t index = 0;
  Feed feed;
  int number = 0;
};

std::vector<Shown> shownFeeds(App& app, const std::string& id) {
  Playback& pb = app.playback();
  std::vector<Feed> feeds = pb.entry() == id ? pb.feeds() : app.store().feeds(id);
  // Build 182: the store's numbering, which the replay's rows share.
  std::vector<int> numbers = app.store().feedNumbers(id, feeds);
  std::vector<Shown> out;
  for (size_t i = 0; i < feeds.size(); ++i)
    if (numbers[i] > 0) out.push_back({i, feeds[i], numbers[i]});
  return out;
}

// The number Sources shows a copy under now (0: not shown).
int numberOf(App& app, const std::string& id, const std::string& copy) {
  for (const Shown& s : shownFeeds(app, id))
    if (s.feed.channelId == copy) return s.number;
  return 0;
}

std::string channelName(App& app, const std::string& id) {
  const GuideChannel* g = app.store().channels()->find(id);
  return g ? (g->partner.empty() ? g->name : catalogName(g)) : std::string("this channel");
}

// "KNTV · FEED 3 · DREAM": the small line over the menus' titles.
std::string feedHeading(App& app, const std::string& id, const std::string& copy, int number, bool channel) {
  std::string out = str::upper(setaside::feedName(number));
  std::string provider = app.store().providerName(copy);
  if (!provider.empty()) out += " \xc2\xb7 " + str::upper(provider);
  return channel ? str::upper(channelName(app, id)) + " \xc2\xb7 " + out : out;
}

// Plays feed `index` of the list Sources shows (OK on its row).
void playFeed(App& app, const std::string& id, size_t index) {
  Playback& play = app.playback();
  if (play.entry() != id && !play.tune(id)) return;
  play.useFeed(index);
}

// Build 182: Sources is the replay's while one of this channel is on.
bool replayMode(App& app, const std::string& id) {
  Playback& pb = app.playback();
  return pb.entry() == id && pb.replay().has_value();
}

// The replay carries on on copy `index` from the moment showing; Sources
// closes so the player's notice and tag show (the copy already replaying:
// nothing).
void replayFrom(App& app, const std::shared_ptr<SourcesState>& state, size_t index) {
  Playback& pb = app.playback();
  if (!pb.replay() || pb.replay()->feed == index) return;
  if (!pb.replayOn(index)) return;
  if (state->screen) {
    app.nav().popTo(state->screen);
    if (app.nav().top() == state->screen) app.nav().pop();
  }
}

// The set-aside is done: back to Sources, its notice at the top ("Feed 3
// set aside for KNTV" over what plays now and "OK again to undo"), the
// focus on the feed's row under SET ASIDE, where OK uses it again.
void setAsideNow(App& app, const std::shared_ptr<SourcesState>& state, setaside::Record r) {
  std::string id = r.entryId;
  std::string copy = r.copyId;
  engine::Session::SetAsideOutcome out = app.playback().setAside(std::move(r));
  const setaside::Record& s = out.record;
  std::string title = setaside::feedName(s.feed) + " set aside for " + channelName(app, id);
  std::string detail;
  if (out.switched) {
    int now = numberOf(app, id, out.playing);
    detail = (now > 0 ? "Playing " + setaside::feedName(now) : std::string("Playing another feed")) +
             " \xc2\xb7 OK again to undo";
  } else if (out.alone) {
    detail = "It's the only feed, so it plays on \xc2\xb7 OK again to undo";
  } else if (setaside::playable(s.reason)) {
    detail = "It still plays; its listings aren't used \xc2\xb7 OK again to undo";
  } else if (!s.namedName.empty()) {
    detail = "It's on " + s.namedName + " now \xc2\xb7 OK again to undo";
  } else {
    detail = "OK again to undo";
  }
  if (!state->screen) return;
  app.nav().popTo(state->screen);
  state->focusCopy = copy;
  state->focusReplay = false;
  state->screen->rebuild();
  if (state->focusRow >= 0) state->screen->panel().focusIndex(state->focusRow);
  state->focusCopy.clear();
  state->screen->notice(title, detail, 5000);
}

// Build 182: a copy's replay set aside: back to Sources, its notice ("Feed
// 2's replay set aside for KNTV" over "Replay on Feed 4 from 8:42 PM · OK
// again to undo"), the focus on its row under SET ASIDE.
void setAsideReplayNow(App& app, const std::shared_ptr<SourcesState>& state, setaside::Record r) {
  std::string id = r.entryId;
  std::string copy = r.copyId;
  r.scope = setaside::Scope::Replay;  // the store makes it agree with the reason
  engine::Session::SetAsideOutcome out = app.playback().setAside(std::move(r));
  const setaside::Record& s = out.record;
  std::string title = setaside::feedName(s.feed) + "'s replay set aside for " + channelName(app, id);
  std::string detail;
  if (out.switched) {
    int now = numberOf(app, id, out.playing);
    detail = "Replay on " + (now > 0 ? setaside::feedName(now) : std::string("another feed")) + " from " +
             clockAt(app, app.playback().replayAt()) + " Â· OK again to undo";
  } else if (s.scope == setaside::Scope::ReplayListings) {
    detail = "Its programme names won't be used Â· OK again to undo";
  } else if (out.alone) {
    detail = "It's the only replay, so it plays on Â· OK again to undo";
  } else {
    detail = "It still plays live; its replay won't be used Â· OK again to undo";
  }
  if (!state->screen) return;
  app.nav().popTo(state->screen);
  state->focusCopy = copy;
  state->focusReplay = true;
  state->screen->rebuild();
  if (state->focusRow >= 0) state->screen->panel().focusIndex(state->focusRow);
  state->focusCopy.clear();
  state->focusReplay = false;
  state->screen->notice(title, detail, 5000);
}

// "What's wrong with its replay?": a replay's reasons, each with what it
// means.
std::unique_ptr<Screen> makeReplayReason(App& app, std::shared_ptr<SourcesState> state, setaside::Record base) {
  std::string heading = feedHeading(app, base.entryId, base.copyId, base.feed, true);
  return std::make_unique<PanelScreen>(app, PanelScreen::Side::Trailing, 760.0f,
                                       [&app, state, base, heading](ui::ListPanel& p) {
    p.subtitle = heading;
    p.title = "What's wrong with its replay?";
    for (setaside::Reason why : setaside::replayReasons()) {
      ui::PanelItem it{ui::PanelItem::Kind::Action, setaside::title(why)};
      it.detail = setaside::replayDetail(why);
      setaside::Record r = base;
      r.reason = why;
      it.select = [&app, state, r] { setAsideReplayNow(app, state, r); };
      p.items.push_back(it);
    }
  });
}

// Build 182: hold OK on a replay row: the small menu beside Sources.
void replayMenu(App& app, const std::shared_ptr<SourcesState>& state, const std::string& id,
                const engine::Session::ReplaySource& s) {
  std::string copy = app.playback().feeds()[s.index].channelId;
  size_t index = s.index;
  int number = s.number;
  std::string heading = feedHeading(app, id, copy, number, false);
  app.nav().push(std::make_unique<PanelScreen>(app, PanelScreen::Side::Beside, 520.0f,
                                               [&app, state, id, copy, index, number, heading](ui::ListPanel& p) {
    p.subtitle = heading;
    ui::PanelItem play{ui::PanelItem::Kind::Action, "Replay from this feed"};
    play.select = [&app, state, index] {
      if (state->screen) app.nav().popTo(state->screen);
      replayFrom(app, state, index);
    };
    p.items.push_back(play);
    ui::PanelItem wrong{ui::PanelItem::Kind::Action, "This feed's replay is wrongâ¦"};
    wrong.select = [&app, state, id, copy, number] {
      if (state->screen) app.nav().popTo(state->screen);
      setaside::Record r;
      r.entryId = id;
      r.copyId = copy;
      r.feed = number;
      r.scope = setaside::Scope::Replay;
      app.nav().push(makeReplayReason(app, state, r));
    };
    p.items.push_back(wrong);
  }));
}

// "Which channel is it?" (optional): channels whose guide shows now what
// this feed's own listing has on, Find the channel…, Not sure.
std::unique_ptr<Screen> makeWhich(App& app, std::shared_ptr<SourcesState> state, setaside::Record base) {
  std::string heading = feedHeading(app, base.entryId, base.copyId, base.feed, true);
  auto panel = std::make_unique<PanelScreen>(app, PanelScreen::Side::Trailing, 760.0f,
                                             [&app, state, base, heading](ui::ListPanel& p) {
    p.subtitle = heading;
    p.title = "Which channel is it?";
    p.items.push_back(ui::PanelItem::note("Optional. It helps it go where it belongs."));
    auto pick = [&app, state, base](const std::string& named) {
      setaside::Record r = base;
      r.named = named;
      setAsideNow(app, state, r);
    };
    time::Seconds now = time::now();
    std::string on = app.store().copyOnNow(base.copyId, now);
    std::vector<std::string> same = app.store().sameShowNow(base.copyId, base.entryId, now);
    if (!same.empty()) {
      p.items.push_back(ui::PanelItem::header("ON NOW, IT LOOKS LIKE"));
      for (const std::string& id : same) {
        const GuideChannel* g = app.store().channels()->find(id);
        if (!g) continue;
        ui::PanelItem it{ui::PanelItem::Kind::Action, g->name};
        it.detail = "On now: " + on + ", as on this feed";
        it.select = [pick, id] { pick(id); };
        p.items.push_back(it);
      }
      p.items.push_back(ui::PanelItem::header("OR"));
    }
    ui::PanelItem find{ui::PanelItem::Kind::Action, "Find the channel\xe2\x80\xa6"};
    find.select = [&app, pick, heading, base] {
      app.nav().push(makeChannelFind(app, heading, base.entryId, [pick](const std::string& id) { pick(id); }));
    };
    p.items.push_back(find);
    ui::PanelItem unsure{ui::PanelItem::Kind::Action, "Not sure"};
    unsure.select = [pick] { pick(""); };
    p.items.push_back(unsure);
    if (!same.empty())
      p.items.push_back(
          ui::PanelItem::note("The suggestion is a channel whose guide shows what this feed is showing now."));
  });
  return panel;
}

// "What's wrong with it?": the reasons, each with what it means.
std::unique_ptr<Screen> makeReason(App& app, std::shared_ptr<SourcesState> state, setaside::Record base) {
  std::string heading = feedHeading(app, base.entryId, base.copyId, base.feed, true);
  return std::make_unique<PanelScreen>(app, PanelScreen::Side::Trailing, 760.0f,
                                       [&app, state, base, heading](ui::ListPanel& p) {
    p.subtitle = heading;
    p.title = "What's wrong with it?";
    for (setaside::Reason why : setaside::reasons()) {
      ui::PanelItem it{ui::PanelItem::Kind::Action, setaside::title(why)};
      it.detail = setaside::detail(why);
      setaside::Record r = base;
      r.reason = why;
      it.select = [&app, state, r] {
        if (r.reason == setaside::Reason::OtherChannel) app.nav().push(makeWhich(app, state, r));
        else setAsideNow(app, state, r);
      };
      p.items.push_back(it);
    }
  });
}

// Hold OK on a feed: the small menu beside Sources.
void feedMenu(App& app, const std::shared_ptr<SourcesState>& state, const std::string& id, const Shown& f) {
  std::string copy = f.feed.channelId;
  size_t index = f.index;
  int number = f.number;
  std::string heading = feedHeading(app, id, copy, number, false);
  app.nav().push(std::make_unique<PanelScreen>(app, PanelScreen::Side::Beside, 520.0f,
                                               [&app, state, id, copy, index, number, heading](ui::ListPanel& p) {
    p.subtitle = heading;
    ui::PanelItem play{ui::PanelItem::Kind::Action, "Play this feed"};
    play.select = [&app, state, id, index] {
      if (state->screen) app.nav().popTo(state->screen);
      playFeed(app, id, index);
    };
    p.items.push_back(play);
    ui::PanelItem pin{ui::PanelItem::Kind::Action, "Always start with this feed"};
    bool pinned = app.store().pinnedFeed(id) == copy;
    if (pinned) pin.detail = "It starts with this one now";
    pin.select = [&app, state, id, copy, pinned] {
      if (state->screen) app.nav().popTo(state->screen);
      if (pinned) return;
      app.store().pinFeed(id, copy);
      app.toast("Will start with this feed");
    };
    p.items.push_back(pin);
    {
      ui::PanelItem wrong{ui::PanelItem::Kind::Action, "This feed is wrong\xe2\x80\xa6"};
      wrong.select = [&app, state, id, copy, number] {
        if (state->screen) app.nav().popTo(state->screen);
        setaside::Record r;
        r.entryId = id;
        r.copyId = copy;
        r.feed = number;
        app.nav().push(makeReason(app, state, r));
      };
      p.items.push_back(wrong);
    }
  }));
}

// SET ASIDE: the channel's feeds set aside, each with why (amber), OK to
// use it again; and what being set aside means here.
void setAsideRows(App& app, ui::ListPanel& p, const std::shared_ptr<SourcesState>& state, const std::string& id) {
  std::vector<setaside::Record> aside = app.store().setAsidesFor(id);
  if (aside.empty()) return;
  p.items.push_back(ui::PanelItem::header("SET ASIDE"));
  time::Seconds now = time::now();
  std::string name = channelName(app, id);
  std::vector<std::string> elsewhere;
  bool notPlayed = false, listings = false;
  bool replays = false;
  for (const setaside::Record& r : aside) {
    bool replay = setaside::isReplay(r.scope);
    // Still shown above (listings or replay set aside): the number it has there.
    int number = r.scope != setaside::Scope::Whole ? numberOf(app, id, r.copyId) : 0;
    ui::PanelItem it{ui::PanelItem::Kind::Action, setaside::asideName(r, number)};
    it.detail = setaside::rowLine(r, app.store().providerName(r.copyId),
                                  engine::sourcetest::testedDay(r.at, now, app.zone()));
    it.warn = true;
    it.third = replay ? "OK to use its replay again" : "OK to use it again";
    it.okLabel = "Use again";
    std::string copy = r.copyId;
    std::string line = setaside::feedName(number > 0 ? number : r.feed) + (replay ? "'s replay" : "") +
                       " is back on " + name;
    it.select = [&app, state, id, copy, line, replay] {
      app.playback().useAgain(id, copy, replay);
      if (state->screen) state->screen->notice(line, "", 3500);
    };
    if (copy == state->focusCopy && replay == state->focusReplay) state->focusRow = static_cast<int>(p.items.size());
    p.items.push_back(it);
    if (replay) replays = true;
    else (setaside::playable(r.reason) ? listings : notPlayed) = true;
    if (!r.namedName.empty()) elsewhere.push_back(r.namedName);
  }
  std::string about;
  if (notPlayed) about = "Set aside: not played, replayed or used for listings on " + name + ".";
  if (listings) about += std::string(about.empty() ? "" : " ") + "Listings set aside: still played, but its listings "
                         "aren't used on " + name + ".";
  if (replays) about += std::string(about.empty() ? "" : " ") + "Replay set aside: still played live, but not "
                        "replayed on " + name + " (or, for programme names, its own names aren't used on Rewind).";
  if (elsewhere.size() == 1) about += " It's offered on " + elsewhere[0] + " instead.";
  else if (elsewhere.size() > 1) about += " Those named as another channel are offered there instead.";
  p.items.push_back(ui::PanelItem::note(about));
}

// Build 182, a replay's Sources: "REPLAY · 8:42 PM" in the corner; the
// copies that reach the moment, then those that can't (dimmed, with why;
// hold OK on one: the live menu).
void replayRows(App& app, ui::ListPanel& p, const std::shared_ptr<SourcesState>& state, const std::string& id,
                const std::vector<Shown>& shown, bool channel) {
  Playback& pb = app.playback();
  engine::Session::ReplaySources rows = pb.replaySources();
  std::string when = clockAt(app, rows.at);
  p.corner = "REPLAY \xc2\xb7 " + when;
  if (!rows.reaching.empty()) p.items.push_back(ui::PanelItem::header("REPLAY FROM " + when + " WITH"));
  for (const engine::Session::ReplaySource& s : rows.reaching) {
    ui::PanelItem it{ui::PanelItem::Kind::Check, s.label};
    it.detail = engine::Session::replaySourceLine(s, when);
    it.warn = s.note == engine::Session::ReplaySource::Note::Stalled;
    it.on = s.playing;
    size_t index = s.index;
    it.select = [&app, state, index] { replayFrom(app, state, index); };
    if (channel) it.hold = [&app, state, id, s] { replayMenu(app, state, id, s); };
    p.items.push_back(it);
  }
  if (!rows.cannot.empty()) p.items.push_back(ui::PanelItem::header("CAN'T REPLAY " + when));
  for (const engine::Session::ReplaySource& s : rows.cannot) {
    ui::PanelItem it{ui::PanelItem::Kind::Check, s.label};
    it.detail = engine::Session::replaySourceLine(s, when);
    it.on = s.playing;  // its replay set aside while it was the only one: it plays on
    it.dim = true;      // can't carry the replay on: OK does nothing here
    if (channel)
      for (const Shown& f : shown)
        if (f.index == s.index) it.hold = [&app, state, id, f] { feedMenu(app, state, id, f); };
    p.items.push_back(it);
  }
}

}  // namespace

std::unique_ptr<Screen> makeSources(App& app, const std::string& entryId) {
  auto current = std::make_shared<std::string>(entryId);
  auto state = std::make_shared<SourcesState>();
  auto panel = std::make_unique<PanelScreen>(app, PanelScreen::Side::Trailing, 760.0f, [&app, current,
                                                                                      state](ui::ListPanel& p) {
    std::string id = *current;
    auto ch = app.store().channels();
    const GuideChannel* g = ch->find(id);
    p.subtitle = "SOURCES";
    p.title = g ? (g->partner.empty() ? g->name : catalogName(g)) : "Sources";
    versionRows(app, p, current);
    Playback& pb = app.playback();
    std::vector<Shown> feeds = shownFeeds(app, id);
    state->focusRow = -1;
    if (feeds.empty()) {
      p.items.push_back(ui::PanelItem::note("No feeds for this channel right now."));
      setAsideRows(app, p, state, id);
      return;
    }
    std::string pinned = app.store().pinnedFeed(id);
    bool channel = g != nullptr && !app.store().isEvent(id);
    bool replaying = replayMode(app, id);
    if (replaying) {
      replayRows(app, p, state, id, feeds, channel);
    } else {
      p.items.push_back(ui::PanelItem::header(
          g && !g->partner.empty() ? (g->west ? "WATCH WITH (WEST)" : "WATCH WITH (EAST)") : "WATCH WITH"));
    }
    bool watching = pb.entry() == id;
    time::Seconds now = time::now();
    // Build 166: what Test all sources (and real plays) measured on
    // each copy, and when the channel was last tested.
    const engine::scores::Book& book = app.store().feedScores();
    time::Seconds tested = 0;
    for (const Shown& f : feeds)
      if (const engine::scores::Record* rec = book.find(f.feed.channelId)) tested = std::max(tested, rec->probedAt);
    if (tested > 0 && !replaying) p.corner = "tested " + engine::sourcetest::testedDay(tested, now, app.zone());
    // Build 181: a guide channel's feeds can be set aside for it (hold OK).
    for (size_t k = 0; k < feeds.size() && !replaying; ++k) {
      const Shown& f = feeds[k];
      size_t i = f.index;
      ui::PanelItem it{ui::PanelItem::Kind::Check, setaside::feedName(f.number)};
      bool playing = watching && pb.feedIndex() == i;
      // What it is doing: playing, failed lately (and why), or a backup.
      std::string status;
      if (playing) status = pb.ready() ? "Playing now" : "Opening\xe2\x80\xa6";
      else if (auto problem = pb.feedProblem(f.feed.channelId))
        status = "Last try " + agoText(problem->at, now) + ": " + problem->why;
      else status = k == 0 ? "Main feed" : "Backup";
      // The first copy is the one a tune starts with; the second, its backup.
      if (k == 0) {
        it.tag = "STARTS WITH";
        it.tagStyle = 1;
      } else if (k == 1) {
        it.tag = "BACKUP";
        it.tagStyle = 2;
      }
      const engine::scores::Record* rec = book.find(f.feed.channelId);
      std::string measured =
          engine::sourcetest::copyLine(rec, app.store().claimedQuality(f.feed.channelId),
                                       app.store().catchupDays(f.feed.channelId), now);
      if (!measured.empty()) {
        // "1080p 60 · starts in 1.4 s · replay 1 day · Trex", or "Didn't
        // start · tried last" (in amber).
        it.detail = status + " \xc2\xb7 " + measured + " \xc2\xb7 " + app.store().providerName(f.feed.channelId);
        it.warn = rec && rec->at(now).mostlyFails();
      } else {
        it.detail = status + " \xc2\xb7 " + f.feed.label;
      }
      if (f.feed.channelId == pinned) it.detail += " \xc2\xb7 Always first";
      // Build 182: its replay set aside here (it still plays live).
      if (app.store().notReplayedOn(id, f.feed.channelId)) {
        it.detail += " \xc2\xb7 replay set aside";
        it.warn = true;
      }
      it.on = playing;
      it.select = [&app, id, i] { playFeed(app, id, i); };
      Shown held = f;
      if (channel) it.hold = [&app, state, id, held] { feedMenu(app, state, id, held); };
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
      for (size_t k = 0; k < feeds.size(); ++k) {
        const Shown& f = feeds[k];
        ui::PanelItem it{ui::PanelItem::Kind::Check, k == 0 ? "Main feed" : setaside::feedName(f.number)};
        it.detail = f.feed.label;
        it.on = f.feed.channelId == pinned;
        std::string copy = f.feed.channelId;
        it.select = [&app, id, copy] {
          app.store().pinFeed(id, copy);
          app.toast("Will start with this feed");
        };
        p.items.push_back(it);
      }
    }
    if (app.store().hasGuideChoice(id)) {
      // Build 153: the player reaches the channel's listings choice here.
      p.items.push_back(ui::PanelItem::header("GUIDE"));
      ui::PanelItem listings{ui::PanelItem::Kind::Chevron, "Listings look wrong?"};
      listings.detail = app.store().guidePicked(id) ? "Showing the listings you picked" : "Pick the ones that match what's on";
      listings.select = [&app, id] { app.nav().push(makeGuideChoice(app, id)); };
      p.items.push_back(listings);
    }
    setAsideRows(app, p, state, id);
    if (replaying) {
      p.hint = "Picking one carries on the replay from the same moment. For live, press Live in the controls.";
      return;
    }
    p.hint = channel ? "OK switches at once; the others stay as automatic backups. Hold OK on a feed for more."
                     : "OK switches at once; the others stay as automatic backups. Back closes.";
  });
  state->screen = panel.get();
  panel->liveRows = true;  // "Opening…" turns into "Playing now"
  return panel;
}

}  // namespace marigold::app
