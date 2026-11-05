// Build 135: the player's Sources for a game (Where to Watch with ticks)
// ticks the source actually playing. A channel showing the game has the
// game's provider feeds as its backups (Store::feeds), so ticking "the
// first source whose feeds hold the channel playing" ticked the channel on
// top whatever feed was picked. Now: the one picked while it plays, else
// the backup that took over; a source is its own channels, never its
// backups. And a feed picked stays first even if it failed lately. Every
// name, team and link is made up.
#include <doctest.h>

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
// provider event feeds.
struct Rig {
  fake::Tv tv;
  std::unique_ptr<Store> store;
  std::shared_ptr<FakePlayer::Shared> player = std::make_shared<FakePlayer::Shared>();
  std::unique_ptr<Session> session;
  std::string game;

  Rig() {
    time::Seconds now = time::now();
    time::Seconds start = now - 30 * 60;
    sports::schedule::Saved s;
    s.fetchedAt = now;
    s.schema = sports::schedule::kSchema;
    s.games.push_back(sportsdata::game("NFL", "NFL", "401", sportsdata::team("1", "Harbor City Owls", "HCO"),
                                       sportsdata::team("2", "Valley Hawks", "VHK"), start, {"ESPN"}));
    tv.files.write("sports-schedule.json", sports::schedule::toJson(s), false);
    tv.pool.answer(kList,
                   "#EXTM3U\n"
                   "#EXTINF:-1 tvg-id=\"espn.us\" group-title=\"US| SPORTS\",US: ESPN\n"
                   "http://mock.example.invalid:8080/1.ts\n"
                   "#EXTINF:-1 tvg-id=\"espn.us\" group-title=\"US| SPORTS\",US: ESPN FHD\n"
                   "http://mock.example.invalid:8080/2.ts\n"
                   "#EXTINF:-1 group-title=\"US| NFL PPV\",NFL 01: Valley Hawks at Harbor City Owls\n"
                   "http://mock.example.invalid:8080/3.ts\n"
                   "#EXTINF:-1 group-title=\"US| NFL PPV\",NFL 02: Valley Hawks at Harbor City Owls (Away feed)\n"
                   "http://mock.example.invalid:8080/4.ts\n");
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
    REQUIRE_FALSE(game.empty());
    session = std::make_unique<Session>(*store, tv.host(), std::make_unique<FakePlayer>(player), Session::Hooks{});
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
