// Build 167 (1.0.11), replay in Live Events: the rule for when a game gets
// replay (each condition, the one- and two-day edges, several channels,
// the channel named, the language), what the board's tiles carry of it
// (made with the board), the "Full games" row, the game banner's fields
// during a replay, and on the pretend TV a game's replay started from the
// board: the archive copy straight from a minute before the start with no
// live start first, the copy after a failed one, the notice and live (a
// live game) or the Final's card, the "End of the game" moment, and never a
// link in the log. Every team, channel and link is made up.
#include <doctest.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "core/engine/events_build.hpp"
#include "core/engine/game_replay.hpp"
#include "core/engine/replay.hpp"
#include "core/engine/session.hpp"
#include "core/engine/store.hpp"
#include "core/engine/views.hpp"
#include "core/sources/xtream.hpp"
#include "core/sports/board.hpp"
#include "core/sports/my_sports.hpp"
#include "core/sports/schedule.hpp"
#include "core/util/time.hpp"
#include "engine/loaded_store.hpp"
#include "sources/mock_provider.hpp"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::engine;
namespace gr = marigold::engine::game_replay;
namespace sb = marigold::sports::board;
using marigold::sports::EventSource;
using marigold::sports::LivePhase;
using marigold::sports::LiveStatus;
using marigold::sports::SportsEvent;
using State = marigold::sports::SportsEvent::State;
using sportsdata::eventOf;
using sportsdata::game;
using sportsdata::team;
using sportsdata::utc;

namespace {

// Monday 2026-11-16 20:00 UTC, on the minute.
const time::Seconds kNow = utc(2026, 11, 16, 20, 0);

EventSource listed(const std::string& id, EventSource::Kind kind, std::optional<time::Seconds> from,
                   std::optional<time::Seconds> until = std::nullopt, const std::string& lang = "en") {
  EventSource s;
  s.channelID = id;
  s.kind = kind;
  s.lang = lang;
  s.from = from;
  s.until = until;
  return s;
}

SportsEvent nba(time::Seconds start, std::vector<EventSource> sources, const std::string& id = "1") {
  return eventOf(game("NBA", "NBA", id, team("h" + id, "Harbor City Owls"), team("a" + id, "Valley Hawks"), start),
                 std::move(sources));
}

gr::Channels channels() {
  gr::Channels c;
  c["espn"] = {2, false, "ESPN"};
  c["nbatv"] = {1, false, "NBA TV"};
  c["tnt"] = {1, true, "TNT"};
  c["kcbs"] = {0, true, "KCBS"};
  return c;
}

const std::set<std::string> kEnglish = {"en"};

std::optional<gr::Pick> pickAt(const SportsEvent& e, State s, time::Seconds now = kNow) {
  return gr::pick(e, s, now, channels(), kEnglish);
}

}  // namespace

// ------------------------------------------------------------------ the rule

TEST_CASE("game replay: a guide channel whose listing has the game at its start, with an archive") {
  time::Seconds start = kNow - 40 * 60;
  SportsEvent e = nba(start, {listed("espn", EventSource::Kind::guide, start - 10 * 60, start + 3 * 3600)});
  auto p = pickAt(e, State::live);
  REQUIRE(p);
  CHECK(p->channelId == "espn");
  CHECK(p->channelName == "ESPN");
  CHECK(p->start == start);
  CHECK(p->from == start - 60);  // a minute before, to the minute
  CHECK(p->days == 2);
  CHECK_FALSE(p->otherChannel);

  // (a) Not a provider event feed, nor a team channel's "likely".
  CHECK_FALSE(pickAt(nba(start, {listed("espn", EventSource::Kind::provider, start)}), State::live));
  CHECK_FALSE(pickAt(nba(start, {listed("espn", EventSource::Kind::team, start)}), State::live));
  // ... with a listing (a channel airing the game's network has none) that
  // shows it at its start: from at most 5 minutes after, not ended by then.
  CHECK_FALSE(pickAt(nba(start, {listed("espn", EventSource::Kind::guide, std::nullopt)}), State::live));
  CHECK(pickAt(nba(start, {listed("espn", EventSource::Kind::guide, start + 5 * 60)}), State::live));
  CHECK_FALSE(pickAt(nba(start, {listed("espn", EventSource::Kind::guide, start + 6 * 60)}), State::live));
  CHECK_FALSE(pickAt(nba(start, {listed("espn", EventSource::Kind::guide, start - 3600, start)}), State::live));
  // My Lineup's kind counts as a guide channel.
  CHECK(pickAt(nba(start, {listed("espn", EventSource::Kind::lineup, start)}), State::live));
  // (b) The channel keeps no archive (or none Test all sources didn't find
  // broken: the store leaves those copies out of `days`), or isn't known.
  CHECK_FALSE(pickAt(nba(start, {listed("kcbs", EventSource::Kind::lineup, start)}), State::live));
  CHECK_FALSE(pickAt(nba(start, {listed("other", EventSource::Kind::guide, start)}), State::live));
}

TEST_CASE("game replay: live, likely over or final; never upcoming, postponed or untimed") {
  time::Seconds start = kNow - 40 * 60;
  SportsEvent e = nba(start, {listed("espn", EventSource::Kind::guide, start)});
  CHECK(pickAt(e, State::live));
  CHECK(pickAt(e, State::mayHaveEnded));
  CHECK(pickAt(e, State::finished));
  CHECK_FALSE(pickAt(e, State::upcoming));
  CHECK_FALSE(pickAt(e, State::postponed));
  CHECK_FALSE(pickAt(e, State::unknownTime));
  SportsEvent tbd = e;
  tbd.game->timeTbd = true;
  CHECK_FALSE(pickAt(tbd, State::finished));
  SportsEvent noStart = e;
  noStart.start.reset();
  CHECK_FALSE(pickAt(noStart, State::live));
}

TEST_CASE("game replay: the archive holds the start less a minute: 23 h on one day, 47 h on two") {
  auto at = [](const std::string& channel, time::Seconds ago) {
    time::Seconds start = kNow - ago;
    return pickAt(nba(start, {listed(channel, EventSource::Kind::guide, start)}), State::finished).has_value();
  };
  // One day (an hour kept clear of its end): started up to 23 h ago, less
  // the minute before the start.
  CHECK(at("nbatv", 22 * 3600 + 58 * 60));
  CHECK(at("nbatv", 22 * 3600 + 59 * 60));
  CHECK_FALSE(at("nbatv", 23 * 3600));
  CHECK_FALSE(at("nbatv", 30 * 3600));  // the Rules' "30 h ago, one-day archive: out"
  // Two days: 47 h.
  CHECK(at("espn", 46 * 3600 + 59 * 60));
  CHECK_FALSE(at("espn", 47 * 3600));
  CHECK(at("espn", 30 * 3600));
}

TEST_CASE("game replay: several channels: the longest archive, then My Lineup, then the one OK plays") {
  time::Seconds start = kNow - 3600;
  // NBA TV (one day) and ESPN (two): ESPN, though TNT is in My Lineup.
  auto p = pickAt(nba(start, {listed("tnt", EventSource::Kind::lineup, start), listed("nbatv", EventSource::Kind::guide, start),
                              listed("espn", EventSource::Kind::guide, start)}),
                  State::live);
  REQUIRE(p);
  CHECK(p->channelId == "espn");
  // OK plays TNT (My Lineup): the replay is on another channel, named.
  CHECK(p->otherChannel);
  CHECK(gr::startedLine(*p, time::Zone::utc(), true) == "Started 7:00 PM \xc2\xb7 on ESPN");
  CHECK(gr::startedLine(*p, time::Zone::utc(), false) == "Started 19:00 \xc2\xb7 on ESPN");
  // The same archive: My Lineup's first.
  p = pickAt(nba(start, {listed("nbatv", EventSource::Kind::guide, start), listed("tnt", EventSource::Kind::lineup, start)}),
             State::live);
  REQUIRE(p);
  CHECK(p->channelId == "tnt");
  CHECK_FALSE(p->otherChannel);
  CHECK(gr::startedLine(*p, time::Zone::utc(), true) == "Started 7:00 PM");
  // The same archive, neither in My Lineup: the one OK plays (English
  // before not known), whatever the order.
  gr::Channels two;
  two["a"] = {1, false, "Alpha"};
  two["b"] = {1, false, "Beta"};
  SportsEvent e = nba(start, {listed("a", EventSource::Kind::guide, start, std::nullopt, ""),
                              listed("b", EventSource::Kind::guide, start, std::nullopt, "en")});
  p = gr::pick(e, State::live, kNow, two, kEnglish);
  REQUIRE(p);
  CHECK(p->channelId == "b");
  CHECK_FALSE(p->otherChannel);
  // Only an event feed plays it live: the channel's replay is "on" it.
  p = pickAt(nba(start, {listed("feed", EventSource::Kind::provider, start), listed("espn", EventSource::Kind::guide, start)}),
             State::live);
  REQUIRE(p);
  CHECK(p->channelId == "espn");
}

TEST_CASE("game replay: the channel is in the language of the game's best live source") {
  time::Seconds start = kNow - 3600;
  gr::Channels c = channels();
  c["univision"] = {0, true, "Univision"};
  SportsEvent e = nba(start, {listed("univision", EventSource::Kind::lineup, start, std::nullopt, "es"),
                              listed("espn", EventSource::Kind::guide, start, std::nullopt, "en")});
  // A Spanish viewer: OK plays Univision (no archive); ESPN is in English.
  CHECK_FALSE(gr::pick(e, State::live, kNow, c, {"es"}));
  // An English viewer: OK plays ESPN.
  CHECK(gr::pick(e, State::live, kNow, c, kEnglish));
  // Not known counts as English.
  CHECK(gr::sameLanguage("", "en"));
  CHECK(gr::sameLanguage("en", ""));
  CHECK_FALSE(gr::sameLanguage("", "es"));
  SportsEvent unmarked = nba(start, {listed("espn", EventSource::Kind::guide, start, std::nullopt, "")});
  CHECK(gr::pick(unmarked, State::live, kNow, c, kEnglish));
}

// ------------------------------------------------------- the board's tiles

namespace {

LiveStatus phaseOf(LivePhase p) {
  LiveStatus s;
  s.phase = p;
  return s;
}

const EventTile* findTile(const EventsBoard& b, const std::string& id, const std::string& row = {}) {
  for (const EventRow& r : b.rows) {
    if (!row.empty() && r.id != row) continue;
    for (const EventGroup& g : r.groups)
      for (const EventTile& t : g.tiles)
        if (t.id == id) return &t;
  }
  return nullptr;
}

const EventRow* findRow(const EventsBoard& b, const std::string& id) {
  for (const EventRow& r : b.rows)
    if (r.id == id) return &r;
  return nullptr;
}

struct BoardRig {
  events_build::BoardInput in;
  std::vector<SportsEvent> events;
  explicit BoardRig(time::Seconds now) {
    in.catalog = std::make_shared<catalog::CatalogResult>();
    in.guide = std::make_shared<engine::Channels>();
    in.channels = std::make_shared<std::vector<Channel>>();
    in.now = now;
    in.zone = time::Zone::utc();
    in.settings.showsUnwatchable = true;
    in.replayChannels = std::make_shared<gr::Channels>(channels());
    // My Sports: the NBA and the NHL picked, the Owls followed.
    in.prefs.configured = true;
    in.prefs.leagues = {"NBA", "NHL"};
    in.prefs.teams = {sports::teamKey("NBA", "h1")};
  }
  // A Final whose end the app saw `endedAgo` ago, after starting `ago` ago.
  void final(const std::string& id, time::Seconds ago, time::Seconds endedAgo, const std::string& channel,
             const std::string& league = "NBA") {
    time::Seconds start = in.now - ago;
    SportsEvent e = eventOf(game(league, league, id, team("h" + id, "Harbor City Owls"), team("a" + id, "Valley Hawks"), start),
                            {listed(channel, EventSource::Kind::guide, start - 600, start + 3 * 3600)});
    in.statuses[e.game->id] = phaseOf(LivePhase::final);
    in.finalSeen[e.game->id] = in.now - endedAgo;
    events.push_back(std::move(e));
  }
  std::shared_ptr<EventsBoard> make() {
    in.events = std::make_shared<std::vector<SportsEvent>>(events);
    return events_build::board(in).events;
  }
};

}  // namespace

TEST_CASE("game replay on the board: the tile carries it, made with the board; none for feeds or upcoming games") {
  time::Seconds now = time::now();
  now -= now % 60;
  BoardRig r(now);
  time::Seconds start = now - 40 * 60;
  r.events.push_back(nba(start, {listed("tnt", EventSource::Kind::lineup, start - 600),
                                 listed("espn", EventSource::Kind::guide, start - 600)},
                         "1"));
  r.events.push_back(nba(start, {listed("feed", EventSource::Kind::provider, start)}, "2"));
  r.events.push_back(nba(now + 3600, {listed("espn", EventSource::Kind::guide, now + 3600 - 600)}, "3"));
  r.in.statuses["NBA|1"] = phaseOf(LivePhase::live);
  std::shared_ptr<EventsBoard> b = r.make();
  const EventTile* live = b->tile("sev|NBA|1");
  REQUIRE(live);
  CHECK(live->hasReplay());
  CHECK(live->replayChannelId == "espn");
  CHECK(live->replayChannelName == "ESPN");
  CHECK(live->replayFrom == start - 60);
  CHECK(live->replayIsOtherChannel);  // OK plays TNT
  CHECK(live->replayLine.rfind("Started ", 0) == 0);
  CHECK(live->replayLine.find("on ESPN") != std::string::npos);
  CHECK(live->finalSeenAt == 0);
  // An event feed's game, an upcoming game: no mark, no menu line.
  const EventTile* feed = b->tile("sev|NBA|2");
  REQUIRE(feed);
  CHECK_FALSE(feed->hasReplay());
  const EventTile* later = b->tile("sev|NBA|3");
  REQUIRE(later);
  CHECK(later->upcoming);
  CHECK_FALSE(later->hasReplay());
  // No channel keeps an archive: nothing anywhere.
  r.in.replayChannels.reset();
  b = r.make();
  REQUIRE(b->tile("sev|NBA|1"));
  CHECK_FALSE(b->tile("sev|NBA|1")->hasReplay());
}

TEST_CASE("game replay on the board: a Final's tile has replay and when the final was seen") {
  time::Seconds now = time::now();
  BoardRig r(now);
  r.final("5", 3 * 3600, 30 * 60, "espn");
  std::shared_ptr<EventsBoard> b = r.make();
  const EventTile* t = b->tile("sev|NBA|5");
  REQUIRE(t);
  CHECK(t->finished);
  CHECK(t->hasReplay());
  CHECK(t->finalSeenAt == now - 30 * 60);
  // Within 12 h of its end it's in its sport's row, not "Full games".
  CHECK(findRow(*b, "full") == nullptr);
}

TEST_CASE("Full games: followed Finals past the 12 h window while their start is in the archive") {
  time::Seconds now = time::now();
  BoardRig r(now);
  // The Owls' game (followed), 20 h ago on ESPN (two days): kept.
  r.final("1", 20 * 3600, 17 * 3600, "espn");
  // A picked league's game (the NHL), on ESPN: kept too.
  r.final("2", 21 * 3600, 18 * 3600, "espn", "NHL");
  // A league not picked: gone, as before.
  r.final("3", 20 * 3600, 17 * 3600, "espn", "MLB");
  // 30 h ago on a one-day archive: out.
  r.final("4", 30 * 3600, 27 * 3600, "nbatv");
  // No archive: gone.
  r.final("6", 20 * 3600, 17 * 3600, "kcbs");
  std::shared_ptr<EventsBoard> b = r.make();
  const EventRow* full = findRow(*b, "full");
  REQUIRE(full);
  CHECK(full->title == "Full games");
  std::vector<std::string> ids;
  for (const EventGroup& g : full->groups)
    for (const EventTile& t : g.tiles) {
      ids.push_back(t.id);
      CHECK(t.hasReplay());
      CHECK(t.finished);
    }
  // Newest first.
  CHECK(ids == std::vector<std::string>{"sev|NBA|1", "sev|NHL|2"});
  CHECK_FALSE(findTile(*b, "sev|MLB|3"));
  CHECK_FALSE(findTile(*b, "sev|NBA|4"));
  CHECK_FALSE(findTile(*b, "sev|NBA|6"));
  // The row's title counts nothing.
  for (char ch : full->title) CHECK_FALSE((ch >= '0' && ch <= '9'));

  // A day on, their starts leave the archive (47 h): out of the row; the row
  // goes when nothing is left in it.
  r.in.now = now + 27 * 3600;
  b = r.make();
  CHECK_FALSE(findTile(*b, "sev|NBA|1"));
  CHECK(findRow(*b, "full") == nullptr);
}

TEST_CASE("Full games: a Final the games no longer hold is put back while its replay lasts") {
  time::Seconds now = time::now();
  BoardRig r(now);
  r.final("1", 20 * 3600, 17 * 3600, "espn");
  SportsEvent kept = r.events.back();
  kept.live = LivePhase::final;  // as the board copied it
  r.events.clear();
  r.in.statuses.clear();
  r.in.kept = {kept};
  std::shared_ptr<EventsBoard> b = r.make();
  REQUIRE(findRow(*b, "full"));
  CHECK(findTile(*b, "sev|NBA|1", "full"));
  // The games hold it again (another id, the same game): once.
  SportsEvent again = kept;
  again.id = "other|NBA|1";
  r.events = {again};
  b = r.make();
  int seen = 0;
  for (const EventRow& row : b->rows)
    for (const EventGroup& g : row.groups)
      for (const EventTile& t : g.tiles) seen += t.id == "sev|NBA|1" || t.id == "other|NBA|1";
  CHECK(seen == 1);
}

// --------------------------------------------------------- the game banner

TEST_CASE("game banner during a replay: REPLAY and how far in, Live now; caught up as before") {
  time::Seconds start = kNow - 3600;
  views::GameReplayBanner b = views::gameReplayBanner(true, start, start + 38 * 60 + 40, false, "NBA GameTime");
  CHECK(b.replay);
  CHECK(b.in == "38 min in");
  CHECK(b.liveNow == "Live now: NBA GameTime");
  // A live game started over: the game itself is live.
  b = views::gameReplayBanner(true, start, start + 5 * 60, true, "NBA GameTime");
  CHECK(b.liveNow == "Live now: this game");
  // The minute before the start: nothing below zero.
  b = views::gameReplayBanner(true, start, start - 60, false, "");
  CHECK(b.in == "0 min in");
  CHECK(b.liveNow.empty());
  // Caught up: as today.
  b = views::gameReplayBanner(false, start, kNow, true, "NBA GameTime");
  CHECK_FALSE(b.replay);
  CHECK(b.in.empty());
  CHECK(b.liveNow.empty());
  // A Final's tag, and Go live named for the channel.
  CHECK(views::replayIn(start, start + 48 * 60 + 10) == "48:10 in");
  CHECK(views::replayIn(start, start + 3600 + 2 * 60 + 5) == "1:02:05 in");
  CHECK(views::replayIn(start, start - 60) == "0:00 in");
  CHECK(views::channelLive("NBA TV") == "NBA TV live");
}

// ------------------------------------------------- on the pretend TV

namespace {

std::string stamp(time::Seconds t) {
  time::Civil c = time::civilFromSeconds(t);
  char b[32];
  std::snprintf(b, sizeof b, "%04d%02d%02d%02d%02d00 +0000", c.year, c.month, c.day, c.hour, c.minute);
  return b;
}

std::string stream(int id, const std::string& name, int archiveDays) {
  return "{\"num\":" + std::to_string(id) + ",\"name\":\"" + name + "\",\"stream_type\":\"live\",\"stream_id\":" +
         std::to_string(id) + ",\"stream_icon\":\"\",\"epg_channel_id\":\"espn.us\",\"category_id\":\"1\","
         "\"tv_archive\":" + (archiveDays ? "1" : "0") + ",\"tv_archive_duration\":\"" + std::to_string(archiveDays) +
         "\"}";
}

std::string accountInfo() {
  return R"({"user_info":{"auth":1,"status":"Active","exp_date":null,"max_connections":"1",)"
         R"("allowed_output_formats":["m3u8","ts"]},"server_info":{"timezone":"UTC",)"
         R"("timestamp_now":)" + std::to_string(time::now()) + R"(}})";
}

struct TvPlayer final : Player {
  struct Shared {
    std::vector<Feed> plays;
    PlayerState state;
  };
  explicit TvPlayer(std::shared_ptr<Shared> s) : s_(std::move(s)) {}
  void attempt() override { s_->state = {}; }
  void play(const Feed& f, bool) override { s_->plays.push_back(f); }
  void stop() override {}
  bool pause() override { return true; }
  PlayerState state() const override { return s_->state; }
  std::shared_ptr<Shared> s_;
};

bool isReplay(const Feed& f) { return f.replay && f.url.find("/timeshift/") != std::string::npos; }
bool endsWith(const std::string& s, const std::string& tail) {
  return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

// An Xtream provider: ESPN in three copies, the first and third keeping two
// days; a game on ESPN that began 40 minutes ago, its listing from its start.
struct TvRig {
  fake::Tv tv;
  std::unique_ptr<Store> store;
  std::shared_ptr<TvPlayer::Shared> player = std::make_shared<TvPlayer::Shared>();
  std::unique_ptr<Session> session;
  std::string game;
  time::Seconds start = 0;
  uint32_t carry = 0;

  TvRig() {
    time::Seconds now = time::now();
    start = now - now % 60 - 40 * 60;
    sports::schedule::Saved s;
    s.fetchedAt = now;
    s.schema = sports::schedule::kSchema;
    s.games.push_back(game_("401"));
    tv.files.write("sports-schedule.json", sports::schedule::toJson(s), false);
    marigold::sources::Account a;
    a.kind = marigold::sources::AccountKind::Xtream;
    a.serverUrl = std::string(mock::kHost);
    a.userName = std::string(mock::kUser);
    a.password = std::string(mock::kPass);
    auto client = marigold::sources::xtream::Client::make(a);
    REQUIRE(client);
    auto c = client.value();
    tv.pool.answer(c.accountInfoRequest().url, accountInfo());
    tv.pool.answer(c.categoriesRequest().url, R"([{"category_id":"1","category_name":"US| SPORTS"}])");
    tv.pool.answer(c.streamsRequest().url, "[" + stream(501, "US: ESPN FHD", 2) + "," + stream(502, "US: ESPN HD", 0) +
                                               "," + stream(503, "US: ESPN SD", 2) + "]");
    tv.pool.answer(c.guideURL(),
                   "<?xml version=\"1.0\"?>\n<tv>\n<channel id=\"espn.us\"><display-name>ESPN</display-name></channel>\n"
                   "<programme start=\"" + stamp(start) + "\" stop=\"" + stamp(start + 4 * 3600) +
                       "\" channel=\"espn.us\"><title>NFL Football</title><sub-title>Valley Hawks at Harbor City "
                       "Owls</sub-title></programme>\n</tv>\n");
    store = std::make_unique<Store>(tv.host());
    AccountForm form;
    form.kind = AccountForm::Kind::Xtream;
    form.name = "Mock IPTV";
    form.server = std::string(mock::kHost);
    form.user = std::string(mock::kUser);
    form.password = std::string(mock::kPass);
    std::string problem = "(not answered)";
    store->addAccount(form, [&](std::string p) { problem = std::move(p); });
    fake::settle(*store, tv);
    REQUIRE(problem.empty());
    store->refreshListings();
    fake::settle(*store, tv);
    store->eventsOpen(true);
    fake::settle(*store, tv);
    // The games' listings are read again every 10 minutes (sports::store::
    // kListingsStale): the channel then has its listing's start and end.
    time::advanceForTests(610);
    fake::settle(*store, tv);
    fake::settle(*store, tv);
    for (const EventRow& r : store->events()->rows)
      for (const EventGroup& g : r.groups)
        for (const EventTile& t : g.tiles)
          if (t.title.find("Harbor City Owls") != std::string::npos && game.empty()) game = t.id;
    REQUIRE_FALSE(game.empty());
    session = std::make_unique<Session>(*store, tv.host(), std::make_unique<TvPlayer>(player), Session::Hooks{});
  }
  ~TvRig() { store->eventsOpen(false); }

  sports::SportsGame game_(const std::string& id) const {
    return sportsdata::game("NFL", "NFL", id, team("1", "Harbor City Owls", std::string("HCO")),
                team("2", "Valley Hawks", std::string("VHK")), start, {"ESPN"});
  }
  const EventTile* tile() const { return store->eventTile(game); }
  // The board's tile as the board's Start over / Watch replay hands it over.
  Session::GameReplay replayOf(const EventTile& t, bool finished, time::Seconds finalAt = 0) const {
    Session::GameReplay g;
    g.eventId = t.id;
    g.channelId = t.replayChannelId;
    g.channelName = t.replayChannelName;
    g.start = t.start;
    g.from = t.replayFrom;
    g.finished = finished;
    g.finalAt = finalAt;
    return g;
  }
  void run(uint32_t ms, uint32_t step = 250) {
    for (uint32_t t = 0; t < ms; t += step) {
      tv.ms += step;
      carry += step;
      if (carry >= 1000) {
        time::advanceForTests(carry / 1000);
        carry %= 1000;
      }
      session->tick(tv.ms);
      tv.pool.drain();
    }
  }
  void picture() {
    run(250);
    player->state.picture = true;
    player->state.playing = true;
    run(500);
  }
  void fail() {
    player->state.failed = true;
    run(500);
  }
  std::vector<std::string> replayLines() const {
    std::vector<std::string> out;
    for (const auto& e : session->log().entries())
      if (e.event == "replay") out.push_back(e.detail);
    return out;
  }
  void noLinks() const {
    for (const auto& e : session->log().entries())
      for (const std::string* f : {&e.detail, &e.feed, &e.provider, &e.channel}) {
        CHECK(f->find("://") == std::string::npos);
        CHECK(f->find("timeshift") == std::string::npos);
        CHECK(f->find("example.invalid") == std::string::npos);
      }
  }
  std::string espn() const {
    for (const GuideChannel& g : store->channels()->all)
      if (g.name.find("ESPN") != std::string::npos) return g.id;
    return {};
  }
};

bool anyLine(const std::vector<std::string>& lines, const std::string& part) {
  return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return l.find(part) != std::string::npos; });
}

}  // namespace

TEST_CASE("game replay from the board: the channel's archive copy from a minute before the start, no live first") {
  TvRig r;
  const EventTile* t = r.tile();
  REQUIRE(t);
  REQUIRE(t->hasReplay());
  CHECK(t->replayChannelId == r.espn());
  CHECK(t->replayFrom == r.start - 60);
  CHECK_FALSE(t->replayIsOtherChannel);
  CHECK(t->replayLine == "Started " + sb::clockText(r.start, time::Zone::utc(), true));
  r.session->setShowingFull(true);
  REQUIRE(r.session->tuneGameReplay(r.replayOf(*t, false), r.store->eventFlipList(r.game)));
  // Build 170: its copy's archive clock first (its own guide; unanswered
  // here, so the server's).
  r.run(250);
  // The one feed handed over is the archive's, of a copy keeping one.
  REQUIRE(r.player->plays.size() == 1);
  const Feed& asked = r.player->plays.back();
  CHECK(isReplay(asked));
  CHECK((endsWith(asked.channelId, "|501") || endsWith(asked.channelId, "|503")));
  REQUIRE(r.session->replay());
  CHECK(r.session->replay()->from == r.start - 60);
  REQUIRE(r.session->gameReplay());
  CHECK(r.session->gameReplay()->channelId == r.espn());
  CHECK(r.session->entry() == r.game);
  CHECK(anyLine(r.replayLines(), "game from "));
  r.picture();
  CHECK(r.session->ready());
  CHECK(r.session->behindLive() > 35 * 60);
  // A live game's replay has no end card.
  r.run(2000);
  CHECK_FALSE(r.session->takeEndOfGame());
  // Go live: the source OK plays, live.
  r.session->goLive();
  r.run(500);
  CHECK_FALSE(r.session->replay());
  CHECK_FALSE(isReplay(r.player->plays.back()));
  r.noLinks();
}

TEST_CASE("game replay from the board: a copy that fails gives way to the next, then live with the notice") {
  TvRig r;
  const EventTile* t = r.tile();
  REQUIRE(t);
  REQUIRE(r.session->tuneGameReplay(r.replayOf(*t, false)));
  // Build 170: its copy's archive clock first (its own guide; unanswered
  // here, so the server's).
  r.run(250);
  std::string first = r.player->plays.back().channelId;
  r.fail();
  // The other copy keeping an archive, the same minute.
  REQUIRE(r.session->replay());
  const Feed& next = r.player->plays.back();
  CHECK(isReplay(next));
  CHECK(next.channelId != first);
  CHECK(r.session->replay()->from == r.start - 60);
  CHECK(anyLine(r.replayLines(), "another copy from "));
  // None left: live, saying so.
  r.fail();
  r.run(500);
  CHECK_FALSE(r.session->replay());
  CHECK(r.session->takeReplayNotice() == "Replay isn\xe2\x80\x99t available right now on this channel");
  CHECK_FALSE(isReplay(r.player->plays.back()));
  CHECK_FALSE(r.session->takeGameBack());
  r.noLinks();
}

TEST_CASE("game replay from the board: a live game whose archive doesn't reach its start plays live") {
  TvRig r;
  const EventTile* t = r.tile();
  REQUIRE(t);
  Session::GameReplay g = r.replayOf(*t, false);
  g.from = time::now() - 3 * 86400;  // past every archive
  REQUIRE(r.session->tuneGameReplay(g));
  r.run(500);
  CHECK_FALSE(r.session->replay());
  REQUIRE(r.player->plays.size() == 1);
  CHECK_FALSE(isReplay(r.player->plays.back()));
  CHECK(r.session->takeReplayNotice() == "Replay isn\xe2\x80\x99t available right now on this channel");
  // A Final with nothing to play: nothing starts at all.
  TvRig f;
  const EventTile* ft = f.tile();
  REQUIRE(ft);
  g = f.replayOf(*ft, true);
  g.from = time::now() - 3 * 86400;
  CHECK_FALSE(f.session->tuneGameReplay(g));
  f.run(500);
  CHECK(f.player->plays.empty());
  CHECK_FALSE(f.session->active());
}

TEST_CASE("a Final's replay: the end card only once past the moment the final was seen, never if unseen") {
  TvRig r;
  const EventTile* t = r.tile();
  REQUIRE(t);
  // The app saw the final three minutes after the start (as if).
  time::Seconds seen = r.start + 3 * 60;
  REQUIRE(r.session->tuneGameReplay(r.replayOf(*t, true, seen)));
  REQUIRE(r.session->replay());
  // A Final's channel is its own first: Go live is that channel live.
  CHECK(r.session->pickedSource() == r.espn());
  r.picture();
  // Before the final's moment: no card.
  r.run(60 * 1000, 1000);
  CHECK_FALSE(r.session->takeEndOfGame());
  CHECK(r.session->replayAt() < seen);
  // Past it: once.
  r.run(4 * 60 * 1000, 1000);
  REQUIRE(r.session->replay());
  CHECK(r.session->replayAt() >= seen);
  CHECK(r.session->takeEndOfGame());
  CHECK_FALSE(r.session->takeEndOfGame());
  r.run(30 * 1000, 1000);
  CHECK_FALSE(r.session->takeEndOfGame());
  CHECK(r.session->replay());  // it plays on
  CHECK(anyLine(r.replayLines(), "past the final"));
  CHECK(Session::kEndCardMs == 15000);

  // The final not seen by the app: never.
  TvRig u;
  const EventTile* ut = u.tile();
  REQUIRE(ut);
  REQUIRE(u.session->tuneGameReplay(u.replayOf(*ut, true, 0)));
  u.picture();
  u.run(6 * 60 * 1000, 1000);
  CHECK(u.session->replay());
  CHECK_FALSE(u.session->takeEndOfGame());
  u.noLinks();
}

TEST_CASE("a Final's replay that fails on every copy goes back to its card, not live") {
  TvRig r;
  const EventTile* t = r.tile();
  REQUIRE(t);
  REQUIRE(r.session->tuneGameReplay(r.replayOf(*t, true, r.start + 3600)));
  // Build 170: its copy's archive clock first (its own guide; unanswered
  // here, so the server's).
  r.run(250);
  r.fail();
  REQUIRE(r.session->replay());  // the other copy
  size_t plays = r.player->plays.size();
  r.fail();
  CHECK_FALSE(r.session->replay());
  CHECK(r.session->takeGameBack());
  CHECK_FALSE(r.session->takeGameBack());
  CHECK(r.session->takeReplayNotice() == "Replay isn\xe2\x80\x99t available right now on this channel");
  // Nothing live was started for it.
  CHECK(r.player->plays.size() == plays);
  CHECK(anyLine(r.replayLines(), "back to the game's card"));
  r.noLinks();
}
