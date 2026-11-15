// Build 135: the player's Sources for a game (Where to Watch with ticks)
// ticks the source actually playing. A channel showing the game has the
// game's provider feeds as its backups (Store::feeds), so ticking "the
// first source whose feeds hold the channel playing" ticked the channel on
// top whatever feed was picked. Now: the one picked while it plays, else
// the backup that took over; a source is its own channels, never its
// backups. And a feed picked stays first even if it failed lately. Every
// name, team and link is made up.
//
// Build 159: Right goes back to what was viewed last, a channel or a game,
// on the source it played from.
#include <doctest.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "core/engine/session.hpp"
#include "core/engine/store.hpp"
#include "core/engine/views.hpp"
#include "core/sports/schedule.hpp"
#include "core/util/time.hpp"
#include "engine/loaded_store.hpp"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::engine;

namespace {

constexpr const char* kList = "http://mock.example.invalid:8080/where.m3u";
constexpr const char* kGuide = "http://mock.example.invalid:8080/where.xml";

std::string stamp(time::Seconds t) {
  time::Civil c = time::civilFromSeconds(t);
  char b[32];
  std::snprintf(b, sizeof b, "%04d%02d%02d%02d%02d00 +0000", c.year, c.month, c.day, c.hour, c.minute);
  return b;
}

// Plays what it's given; a feed in `dead` fails at once.
struct FakePlayer final : Player {
  struct Shared {
    std::vector<std::string> plays;
    std::set<std::string> dead;
    PlayerState state;
  };
  explicit FakePlayer(std::shared_ptr<Shared> s) : s_(std::move(s)) {}
  void attempt() override { s_->state = {}; }
  void play(const Feed& f, bool) override {
    s_->plays.push_back(f.channelId);
    if (s_->dead.count(f.channelId)) s_->state.failed = true;
  }
  void stop() override {}
  PlayerState state() const override { return s_->state; }
  std::shared_ptr<Shared> s_;
};

// A game on now: on a guide channel (two versions, in the guide) and on two
// provider event feeds. With `more`: a news channel, and a second game on a
// provider feed of its own.
struct Rig {
  fake::Tv tv;
  std::unique_ptr<Store> store;
  std::shared_ptr<FakePlayer::Shared> player = std::make_shared<FakePlayer::Shared>();
  std::unique_ptr<Session> session;
  std::string game, other;
  std::vector<std::string> toasts;

  explicit Rig(bool more = false) {
    time::Seconds now = time::now();
    time::Seconds start = now - 30 * 60;
    sports::schedule::Saved s;
    s.fetchedAt = now;
    s.schema = sports::schedule::kSchema;
    s.games.push_back(sportsdata::game("NFL", "NFL", "401", sportsdata::team("1", "Harbor City Owls", "HCO"),
                                       sportsdata::team("2", "Valley Hawks", "VHK"), start, {"ESPN"}));
    if (more)
      s.games.push_back(sportsdata::game("NFL", "NFL", "402", sportsdata::team("3", "Lake Town Bears", "LTB"),
                                         sportsdata::team("4", "River City Foxes", "RCF"), start));
    tv.files.write("sports-schedule.json", sports::schedule::toJson(s), false);
    std::string list =
        "#EXTM3U\n"
        "#EXTINF:-1 tvg-id=\"espn.us\" group-title=\"US| SPORTS\",US: ESPN\n"
        "http://mock.example.invalid:8080/1.ts\n"
        "#EXTINF:-1 tvg-id=\"espn.us\" group-title=\"US| SPORTS\",US: ESPN FHD\n"
        "http://mock.example.invalid:8080/2.ts\n"
        "#EXTINF:-1 group-title=\"US| NFL PPV\",NFL 01: Valley Hawks at Harbor City Owls\n"
        "http://mock.example.invalid:8080/3.ts\n"
        "#EXTINF:-1 group-title=\"US| NFL PPV\",NFL 02: Valley Hawks at Harbor City Owls (Away feed)\n"
        "http://mock.example.invalid:8080/4.ts\n";
    if (more)
      list +=
          "#EXTINF:-1 tvg-id=\"cnn.us\" group-title=\"US| NEWS\",US: CNN\n"
          "http://mock.example.invalid:8080/5.ts\n"
          "#EXTINF:-1 group-title=\"US| NFL PPV\",NFL 03: River City Foxes at Lake Town Bears\n"
          "http://mock.example.invalid:8080/6.ts\n";
    tv.pool.answer(kList, list);
    std::string xml =
        "<?xml version=\"1.0\"?>\n<tv>\n<channel id=\"espn.us\"><display-name>ESPN</display-name></channel>\n"
        "<programme start=\"" +
        stamp(start) + "\" stop=\"" + stamp(start + 4 * 3600) +
        "\" channel=\"espn.us\"><title>NFL Football</title><sub-title>Valley Hawks at Harbor City "
        "Owls</sub-title></programme>\n</tv>\n";
    tv.pool.answer(kGuide, xml);
    store = std::make_unique<Store>(tv.host());
    AccountForm form = fake::m3uForm();
    form.server = kList;
    form.guideUrl = kGuide;
    store->addAccount(form, [](std::string) {});
    fake::settle(*store, tv);
    REQUIRE(store->refreshListings());
    fake::settle(*store, tv);
    store->eventsOpen(true);
    fake::settle(*store, tv);
    for (const EventRow& r : store->events()->rows)
      for (const EventGroup& g : r.groups)
        for (const EventTile& t : g.tiles)
          if (t.title.find("Harbor City Owls") != std::string::npos && game.empty()) game = t.id;
          else if (t.title.find("Lake Town Bears") != std::string::npos && other.empty()) other = t.id;
    REQUIRE_FALSE(game.empty());
    if (more) REQUIRE_FALSE(other.empty());
    Session::Hooks hooks;
    hooks.toast = [this](const std::string& t) { toasts.push_back(t); };
    session = std::make_unique<Session>(*store, tv.host(), std::make_unique<FakePlayer>(player), hooks);
  }
  ~Rig() { store->eventsOpen(false); }

  // The source titled `title` in Where to Watch.
  WhereToWatch where() const { return store->whereToWatch(game); }
  std::string via(const std::string& title) const {
    for (const auto& section : where().sections)
      for (const WatchSource& s : section.second)
        if (s.title == title) return s.entryId.empty() ? s.channelId : s.entryId;
    return "";
  }
  // The source the player's Sources ticks ("" none).
  std::string ticked() const {
    WhereToWatch w = where();
    const WatchSource* s = views::playingSource(w, session->playingChannel(), session->pickedSource());
    return s ? s->title : std::string();
  }
  void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 250) {
      tv.ms += 250;
      session->tick(tv.ms);
      tv.pool.drain();
    }
  }
  // The feed is handed to the player, then its picture comes up.
  void picture() {
    run(250);
    player->state.picture = true;
    run(500);
  }
  // A guide channel by the name it shows.
  std::string channel(const std::string& name) const {
    for (const GuideChannel& g : store->channels()->all)
      if (g.name == name) return g.id;
    return "";
  }
  // Log lines of this kind, newest last.
  std::vector<std::string> logged(const std::string& event) const {
    std::vector<std::string> out;
    for (const auto& e : session->log().entries())
      if (e.event == event) out.push_back(e.detail);
    return out;
  }
};

const std::string kChannel = "ESPN";
const std::string kHome = "Valley Hawks at Harbor City Owls";
const std::string kAway = "Valley Hawks at Harbor City Owls (Away feed)";

}  // namespace

TEST_CASE("engine: Where to Watch lists the channel above the game's feeds, which back the channel up") {
  Rig rig;
  WhereToWatch w = rig.where();
  REQUIRE(w.sections.size() == 2);
  REQUIRE(w.sections[0].second.size() == 1);
  CHECK(w.sections[0].second[0].title == kChannel);
  CHECK_FALSE(w.sections[0].second[0].entryId.empty());
  REQUIRE(w.sections[1].second.size() == 2);
  // The channel's feeds hold the game's feeds as backups (why the tick went
  // wrong); the source itself is its two versions.
  std::string entry = w.sections[0].second[0].entryId;
  std::string away = rig.via(kAway);
  bool backs = false;
  for (const Feed& f : rig.store->feeds(entry)) backs = backs || f.channelId == away;
  CHECK(backs);
  CHECK(w.sections[0].second[0].channels.size() == 2);
  for (const std::string& c : w.sections[0].second[0].channels) CHECK(c != away);
  CHECK(w.sections[1].second[1].channels == std::vector<std::string>{away});
}

TEST_CASE("engine: the player's Sources for a game ticks the feed picked, not the channel on top") {
  Rig rig;
  // The owner's report: picking a feed below the channel left the tick on
  // the channel.
  for (const std::string& title : {kAway, kHome, kAway}) {
    CAPTURE(title);
    std::string via = rig.via(title);
    REQUIRE(rig.session->tuneEventVia(rig.game, via));
    CHECK(rig.session->playingChannel() == via);  // the pick plays first
    CHECK(rig.session->pickedSource() == via);
    CHECK(rig.ticked() == title);
  }
  // The channel picked: ticked.
  REQUIRE(rig.session->tuneEventVia(rig.game, rig.via(kChannel)));
  CHECK(rig.ticked() == kChannel);
  // Watch (nothing picked): what plays first, the channel.
  rig.session->stop();
  REQUIRE(rig.session->tune(rig.game));
  CHECK(rig.session->pickedSource().empty());
  CHECK(rig.ticked() == kChannel);
  // Nothing playing: nothing ticked.
  rig.session->stop();
  CHECK(rig.ticked().empty());
}

TEST_CASE("engine: a feed picked that fails hands the tick to the backup that plays") {
  Rig rig;
  std::string away = rig.via(kAway);
  rig.player->dead.insert(away);
  REQUIRE(rig.session->tuneEventVia(rig.game, away));
  CHECK(rig.ticked() == kAway);  // opening
  for (int i = 0; i < 240 && rig.session->playingChannel() == away; ++i) rig.run(250);
  REQUIRE(rig.session->playingChannel() != away);
  // The backup is the channel (its versions come next): ticked, the pick not.
  CHECK(rig.ticked() == kChannel);
}

TEST_CASE("engine: a feed picked starts first even when this TV failed to play it lately") {
  Rig rig;
  std::string away = rig.via(kAway);
  rig.store->markUnplayable(away);
  std::vector<Feed> feeds = rig.store->eventFeeds(rig.game, away);
  REQUIRE_FALSE(feeds.empty());
  CHECK(feeds.front().channelId == away);
  // Not picked, it still goes last.
  std::vector<Feed> any = rig.store->eventFeeds(rig.game, "");
  REQUIRE_FALSE(any.empty());
  CHECK(any.back().channelId == away);
}

TEST_CASE("views: the source playing is the one picked while it plays, else the first that holds the channel") {
  auto source = [](const std::string& title, const std::string& entry, const std::string& channel,
                   std::vector<std::string> channels) {
    WatchSource s;
    s.title = title;
    s.entryId = entry;
    s.channelId = channel;
    s.channels = std::move(channels);
    return s;
  };
  WhereToWatch w;
  w.sections.push_back({"Your channels",
                        {source("Harbor Sports", "e|harbor", "", {"raw1", "raw2"}),
                         source("Harbor Sports West", "e|harborwest", "", {"raw2", "raw3"})}});
  w.sections.push_back({"Event feeds from your provider", {source("Feed A", "", "rawA", {"rawA"})}});
  auto title = [&](const std::string& playing, const std::string& picked) {
    const WatchSource* s = views::playingSource(w, playing, picked);
    return s ? s->title : std::string("-");
  };
  CHECK(title("rawA", "rawA") == "Feed A");
  CHECK(title("rawA", "") == "Feed A");
  CHECK(title("rawA", "e|harbor") == "Feed A");  // the pick failed: the backup playing
  CHECK(title("raw1", "rawA") == "Harbor Sports");
  // A channel two sources share: the one picked, else the first.
  CHECK(title("raw2", "e|harborwest") == "Harbor Sports West");
  CHECK(title("raw2", "") == "Harbor Sports");
  CHECK(title("raw3", "e|harbor") == "Harbor Sports West");
  // Nothing playing, or a channel no source is: none.
  CHECK(title("", "rawA") == "-");
  CHECK(title("rawZ", "") == "-");
}

// --- Right goes back to what you watched (Build 159) ---------------------------

namespace {

// now() moved on for one test, and back after it.
struct ClockAhead {
  time::Seconds by = 0;
  void move(time::Seconds s) {
    time::advanceForTests(s);
    by += s;
  }
  ~ClockAhead() { time::advanceForTests(-by); }
};

// Live Events made again, with the board as it is now.
void rebuild(Rig& rig) {
  rig.store->eventsOpen(false);
  fake::settle(*rig.store, rig.tv);
  rig.store->eventsOpen(true);
  for (int i = 0; i < 4; ++i) fake::settle(*rig.store, rig.tv);
}

// The provider feed a game's Where to Watch lists first.
std::string firstFeed(const Rig& rig, const std::string& eventId) {
  for (const auto& section : rig.store->whereToWatch(eventId).sections)
    for (const WatchSource& s : section.second)
      if (!s.channelId.empty()) return s.channelId;
  return "";
}

}  // namespace

TEST_CASE("engine: Right goes from a game to the channel before it and back to the game, on the same source") {
  Rig rig(true);
  std::string cnn = rig.channel("CNN");
  std::string away = rig.via(kAway);
  REQUIRE_FALSE(cnn.empty());
  REQUIRE_FALSE(away.empty());
  std::vector<std::string> games = rig.store->eventFlipList(rig.game);
  rig.session->setShowingFull(true);
  REQUIRE(rig.session->tune(cnn));
  rig.picture();
  REQUIRE(rig.session->tuneEventVia(rig.game, away, games));
  rig.picture();
  CHECK(rig.session->previousEntry() == cnn);
  // Right: the channel...
  CHECK(rig.session->previous() == cnn);
  CHECK(rig.session->entry() == cnn);
  CHECK(rig.session->pickedSource().empty());
  CHECK(std::find(rig.session->flipList().begin(), rig.session->flipList().end(), cnn) !=
        rig.session->flipList().end());
  rig.picture();
  // ... and Right again: the game, on the feed it played from, with Up and
  // Down through the games.
  const Session::Viewed* back = rig.session->previousTarget();
  REQUIRE(back);
  CHECK(back->game);
  CHECK(back->entry == rig.game);
  CHECK(back->source == away);
  CHECK(back->gameId == rig.store->eventGameId(rig.game));
  CHECK(rig.session->previous() == rig.game);
  CHECK(rig.session->pickedSource() == away);
  CHECK(rig.session->playingChannel() == away);
  CHECK(rig.ticked() == kAway);
  CHECK(rig.session->flipList() == games);
  rig.picture();
  CHECK(rig.session->previous() == cnn);
  CHECK(rig.logged("back to") == std::vector<std::string>{"channel", "game", "channel"});
}

TEST_CASE("engine: Right swaps two games, each on its own source") {
  Rig rig(true);
  std::string away = rig.via(kAway);
  std::string foxes = firstFeed(rig, rig.other);
  REQUIRE_FALSE(foxes.empty());
  rig.session->setShowingFull(true);
  REQUIRE(rig.session->tuneEventVia(rig.game, away, rig.store->eventFlipList(rig.game)));
  rig.picture();
  REQUIRE(rig.session->tuneEventVia(rig.other, foxes, rig.store->eventFlipList(rig.other)));
  rig.picture();
  for (int i = 0; i < 2; ++i) {
    CHECK(rig.session->previous() == rig.game);
    CHECK(rig.session->playingChannel() == away);
    CHECK(rig.session->pickedSource() == away);
    rig.picture();
    CHECK(rig.session->previous() == rig.other);
    CHECK(rig.session->playingChannel() == foxes);
    rig.picture();
  }
  CHECK(rig.logged("back to") == std::vector<std::string>(4, "game"));
}

TEST_CASE("engine: another source or a camera of the game playing leaves Right's target alone") {
  Rig rig(true);
  std::string cnn = rig.channel("CNN");
  std::string away = rig.via(kAway), home = rig.via(kHome);
  std::vector<std::string> games = rig.store->eventFlipList(rig.game);
  rig.session->setShowingFull(true);
  REQUIRE(rig.session->tune(cnn));
  rig.picture();
  REQUIRE(rig.session->tuneEventVia(rig.game, away, games));
  rig.picture();
  CHECK(rig.session->previousEntry() == cnn);
  // Where to Watch: another source, in place.
  REQUIRE(rig.session->tuneEventVia(rig.game, home, games));
  rig.picture();
  CHECK(rig.session->previousEntry() == cnn);
  // A camera (the cameras panel).
  std::vector<Feed> camera = rig.store->eventFeeds(rig.game, away);
  REQUIRE_FALSE(camera.empty());
  camera.resize(1);
  camera[0].channelId = "cam|onboard-1";
  camera[0].label = "Onboard 1";
  REQUIRE(rig.session->tuneCamera(camera));
  rig.picture();
  CHECK(rig.session->previousEntry() == cnn);
  CHECK(rig.session->previous() == cnn);
  rig.picture();
  // Back to the game: on the source picked before the camera, not the camera.
  const Session::Viewed* back = rig.session->previousTarget();
  REQUIRE(back);
  CHECK(back->entry == rig.game);
  CHECK(back->source == home);
  CHECK(rig.session->previous() == rig.game);
  CHECK(rig.session->playingChannel() == home);
  CHECK(rig.session->pickedSource() == home);
}

TEST_CASE("engine: a game that has left the board goes back to the channel it played on, with a note") {
  Rig rig(true);
  std::string espn = rig.via(kChannel);
  std::string cnn = rig.channel("CNN");
  REQUIRE(rig.store->eventOfGame(rig.store->eventGameId(rig.game)) == rig.game);
  rig.session->setShowingFull(true);
  REQUIRE(rig.session->tuneEventVia(rig.game, espn, rig.store->eventFlipList(rig.game)));
  rig.picture();
  REQUIRE(rig.session->tune(cnn));
  rig.picture();
  const Session::Viewed* back = rig.session->previousTarget();
  REQUIRE(back);
  CHECK(back->channel == espn);
  // Two days on, the board made again: the game's tile is gone.
  ClockAhead clock;
  clock.move(2 * 86400);
  rebuild(rig);
  REQUIRE_FALSE(rig.store->isEvent(rig.game));
  REQUIRE(rig.session->previousTarget());
  CHECK(rig.session->gameNow(*rig.session->previousTarget()).empty());
  CHECK(rig.store->eventOfGame(back->gameId).empty());
  rig.toasts.clear();
  CHECK(rig.session->previous() == espn);
  CHECK(rig.session->entry() == espn);
  CHECK(rig.session->bouncing());
  REQUIRE(rig.toasts.size() == 1);
  CHECK(rig.toasts[0] == "That game has ended \xc2\xb7 back on ESPN");
  REQUIRE_FALSE(rig.logged("back to").empty());
  CHECK(rig.logged("back to").back() == "its channel; the game has ended");
  for (const auto& e : rig.session->log().entries()) CHECK(e.detail.find("http") == std::string::npos);
}

TEST_CASE("engine: a game on a provider feed alone that has left the board: Right only says so") {
  Rig rig(true);
  std::string away = rig.via(kAway);
  std::string cnn = rig.channel("CNN");
  rig.session->setShowingFull(true);
  REQUIRE(rig.session->tuneEventVia(rig.game, away, rig.store->eventFlipList(rig.game)));
  rig.picture();
  REQUIRE(rig.session->tune(cnn));
  rig.picture();
  ClockAhead clock;
  clock.move(2 * 86400);
  rebuild(rig);
  REQUIRE_FALSE(rig.store->isEvent(rig.game));
  rig.toasts.clear();
  CHECK(rig.session->previous().empty());
  CHECK(rig.session->entry() == cnn);
  CHECK_FALSE(rig.session->bouncing());
  CHECK(rig.toasts == std::vector<std::string>{"That game has ended"});
}

TEST_CASE("engine: back from the background after the game left the board, playback ends quietly") {
  Rig rig(true);
  std::string away = rig.via(kAway);
  rig.session->setShowingFull(true);
  REQUIRE(rig.session->tuneEventVia(rig.game, away, rig.store->eventFlipList(rig.game)));
  rig.picture();
  size_t plays = rig.player->plays.size();
  size_t lines = rig.session->log().size();
  rig.session->suspend();
  ClockAhead clock;
  clock.move(2 * 86400);
  rebuild(rig);
  rig.run(60'000);
  REQUIRE_FALSE(rig.store->isEvent(rig.game));
  rig.session->resume();
  CHECK_FALSE(rig.session->active());
  CHECK(rig.player->plays.size() == plays);
  CHECK(rig.logged("resumed") == std::vector<std::string>{"the game is over; stopped"});
  const auto& entries = rig.session->log().entries();
  for (size_t i = lines; i < entries.size(); ++i) {
    CAPTURE(entries[i].event);
    CHECK((entries[i].event == "paused" || entries[i].event == "resumed" || entries[i].event == "memory" ||
           entries[i].event == "live events"));
  }
}
