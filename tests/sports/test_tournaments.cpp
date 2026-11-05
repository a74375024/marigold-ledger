// Build 143: event and tournament sports. A golf tournament as its round on
// now (tee times, the next round after "Play Complete", done only after the
// last), the guide's own programmes of tours, racing series and fight
// nights joined to their events (any channel, any language's session
// words, replays left out), racing feeds by their series, and a Test's
// listings on any of its days. Every tournament, team and player is
// invented.
#include <algorithm>

#include "core/sports/board.hpp"
#include "core/sports/matcher.hpp"
#include "core/sports/profiles.hpp"
#include "core/sports/schedule.hpp"
#include "core/sports/scores.hpp"
#include "core/sports/store.hpp"
#include "core/util/strings.hpp"
#include "doctest.h"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::sports;
using namespace marigold::sports::matcher;
using sportsdata::game;
using sportsdata::team;
using sportsdata::utc;

namespace {

std::string golfEvent(const std::string& name, const std::string& status, const std::string& state, int period,
                      bool completed, const std::string& competitors = "", const std::string& end = "2026-11-08T05:00Z") {
  return R"({"id":"401","name":")" + name + R"(","shortName":"Short","date":"2026-11-05T05:00Z","endDate":")" + end +
         R"(","competitions":[{"status":{"period":)" + std::to_string(period) + R"(,"type":{"name":")" + status +
         R"(","state":")" + state + R"(","completed":)" + (completed ? "true" : "false") + R"(}},"competitors":[)" +
         competitors + "]}]}";
}

// A player's round with its tee time as the scoreboard writes it.
std::string teeAt(int period, const std::string& when) {
  return R"({"order":1,"score":"E","athlete":{"shortName":"A. Player"},"linescores":[{"period":)" +
         std::to_string(period) + R"(,"statistics":{"categories":[{"stats":[{"displayValue":"0"},{"displayValue":")" +
         when + R"("}]}]}}]})";
}

SportsGame golfGame(const std::string& body, const char* tour = "PGA") {
  auto games = schedule::parseGolfTournaments(R"({"events":[)" + body + "]}", *leagueByShort(tour));
  REQUIRE(games);
  REQUIRE(games->size() == 1);
  return games->front();
}

SportsGame tournament(const std::string& tour, const std::string& id, const std::string& name, Seconds start,
                      Seconds length, int roundsLeft, std::vector<std::string> networks = {"Golf Net"}) {
  SportsTeam side = team("g" + id, name);
  SportsGame g = game(tour, "Golf", id, side, side, start, std::move(networks));
  g.golf = true;
  g.end = start + Seconds{roundsLeft - 1} * 86'400 + length;
  return g;
}

SportsGame session(const std::string& league, const std::string& weekend, const std::string& kind,
                   const std::string& id, Seconds start) {
  SportsTeam side = team("r" + weekend, weekend);
  SportsGame g = game(league, "Motorsport", id, side, side, start);
  g.racing = true;
  g.cardKey = str::lower(league) + "|" + weekend + "|" + kind;
  g.customTitle = weekend + " \xC2\xB7 " + kind;
  return g;
}

GuideListing listing(const std::string& channel, const std::string& title, std::optional<std::string> subtitle,
                     Seconds start, Seconds end) {
  GuideListing l;
  l.entryID = channel;
  l.inLineup = true;
  l.lang = "en";
  l.title = subtitle ? title + " " + *subtitle : title;
  l.series = seriesOf(title, subtitle);
  l.start = start;
  l.end = end;
  return l;
}

ProviderEvent providerEvent(const std::string& id, const std::string& title, const std::string& league, Seconds start) {
  ProviderEvent e;
  e.id = id;
  e.title = title;
  e.league = league;
  e.start = start;
  e.feeds = {"xt|" + id};
  e.family = "ppv";
  e.lang = "en";
  return e;
}

std::vector<std::string> sourcesOf(const BuildResult& out, const std::string& gameID) {
  for (const SportsEvent& e : out.events) {
    if (!e.game || e.game->id != gameID) continue;
    std::vector<std::string> ids;
    for (const EventSource& s : e.sources) ids.push_back(s.channelID);
    return ids;
  }
  return {};
}

const EventSource* sourceIn(const BuildResult& out, const std::string& gameID, const std::string& channel) {
  for (const SportsEvent& e : out.events) {
    if (!e.game || e.game->id != gameID) continue;
    for (const EventSource& s : e.sources)
      if (s.channelID == channel) return &s;
  }
  return nullptr;
}

}  // namespace

// MARK: Golf rounds

TEST_CASE("golf: a round from its tee times, the next one after Play Complete, done after the last") {
  // Round 1 done; its tee times from 8:30 to 14:44 (Eastern clock times
  // under the scoreboard's Pacific label).
  std::string tees = teeAt(1, "Thu Nov 05 08:30:00 PST 2026") + "," + teeAt(1, "Thu Nov 05 14:44:00 PST 2026");
  SportsGame g = golfGame(golfEvent("Harbor Championship", "STATUS_PLAY_COMPLETE", "post", 1, true, tees));
  CHECK_FALSE(g.completed);  // only the round is
  CHECK(g.start == utc(2026, 11, 6, 13, 30));  // round 2, a day later
  const Seconds round = 6 * 3600 + 14 * 60 + 5 * 3600;  // the last group's tee time and its round
  CHECK(lengthOf("Golf", &g) == round);
  REQUIRE(g.end);
  CHECK(*g.end == utc(2026, 11, 8, 13, 30) + round);  // the fourth day's finish
  // The round on now: the next day's once a round is two hours past its finish.
  SportsGame later = g;
  schedule::golfRoundOn(later, utc(2026, 11, 7, 2, 0));
  CHECK(later.start == g.start);
  schedule::golfRoundOn(later, utc(2026, 11, 7, 3, 0));
  CHECK(later.start == utc(2026, 11, 7, 13, 30));
  CHECK(lengthOf("Golf", &later) == round);
  schedule::golfRoundOn(later, utc(2026, 11, 20, 0, 0));
  CHECK(later.start == utc(2026, 11, 8, 13, 30));  // the last stays
  // In play: that round. The last round complete, or final: done.
  CHECK(golfGame(golfEvent("Harbor Championship", "STATUS_IN_PROGRESS", "in", 2, false, tees)).start ==
        utc(2026, 11, 6, 13, 30));
  CHECK(golfGame(golfEvent("Harbor Championship", "STATUS_PLAY_COMPLETE", "post", 4, true, tees)).completed);
  CHECK(golfGame(golfEvent("Harbor Championship", "STATUS_FINAL", "post", 4, true, tees)).completed);
  // Three rounds (a 54-hole event): the third's "Play Complete" is the end.
  CHECK(golfGame(golfEvent("Valley Invitational", "STATUS_PLAY_COMPLETE", "post", 3, true, tees,
                           "2026-11-07T05:00Z")).completed);
  CHECK_FALSE(golfGame(golfEvent("Valley Invitational", "STATUS_PLAY_COMPLETE", "post", 2, true, tees,
                                 "2026-11-07T05:00Z")).completed);
  // Another zone's label is read as written.
  CHECK(golfGame(golfEvent("Harbor Championship", "STATUS_IN_PROGRESS", "in", 1, false,
                           teeAt(1, "Thu Nov 05 08:30:00 MST 2026"))).start == utc(2026, 11, 5, 15, 30));
  // Saved and read back.
  auto back = gameFromJson(toJson(g));
  REQUIRE(back);
  CHECK(back->end == g.end);
}

TEST_CASE("golf: with no tee time out yet, 07:00 where it's played") {
  // Japan by its name: 07:00 there is 22:00 UTC the day before.
  SportsGame japan = golfGame(golfEvent("Harbor Japan Classic", "STATUS_SCHEDULED", "pre", 0, false), "LPGA");
  CHECK(japan.start == utc(2026, 11, 4, 22, 0));
  CHECK(lengthOf("Golf", &japan) == 11 * 3600);
  // A US tour's at home: 07:00 Eastern; the DP World Tour's: Central Europe.
  CHECK(golfGame(golfEvent("Harbor Championship", "STATUS_SCHEDULED", "pre", 0, false)).start ==
        utc(2026, 11, 5, 12, 0));
  CHECK(golfGame(golfEvent("Harbor Championship", "STATUS_SCHEDULED", "pre", 0, false), "DPWT").start ==
        utc(2026, 11, 5, 6, 0));
  CHECK(golfGame(golfEvent("Abu Dhabi Harbor Championship", "STATUS_SCHEDULED", "pre", 0, false), "DPWT").start ==
        utc(2026, 11, 5, 3, 0));
  // The seniors' and the second tour are tours too.
  CHECK(leagueByShort("CHAMP"));
  CHECK(leagueByShort("KFT"));
}

TEST_CASE("golf scores: the last round's Play Complete is final, an earlier one's is between rounds") {
  std::string last = R"({"events":[)" + golfEvent("Harbor Championship", "STATUS_PLAY_COMPLETE", "post", 4, true) + "]}";
  LiveStatus over = scores::statuses(last, *leagueByShort("PGA")).at("PGA|401");
  CHECK(over.phase == LivePhase::final);
  CHECK(over.detail == "Final");
  std::string first = R"({"events":[)" + golfEvent("Harbor Championship", "STATUS_PLAY_COMPLETE", "post", 1, true) + "]}";
  LiveStatus between = scores::statuses(first, *leagueByShort("PGA")).at("PGA|401");
  CHECK(between.phase == LivePhase::pre);
  CHECK(between.detail == "Round 1 done");
}

// MARK: Programmes

TEST_CASE("seriesOf: a tour's, racing series' or fight night's own programme") {
  auto none = std::optional<std::string>();
  auto sub = [](const char* s) { return std::optional<std::string>(s); };
  CHECK(seriesOf("PGA Tour Golf", sub("Harbor Championship, First Round")) == "golf|PGA");
  CHECK(seriesOf("LPGA Tour Golf", sub("Harbor Japan Classic, Second Round")) == "golf|LPGA");  // a "Classic"
  CHECK(seriesOf("Golf: Ladies PGA", none) == "golf|LPGA");
  CHECK(seriesOf("DP World Tour Golf", none) == "golf|DPWT");
  CHECK(seriesOf("Live DP World Tour Golf", none) == "golf|DPWT");
  CHECK(seriesOf("LIV Golf", sub("Harbor, Round 2")) == "golf|LIV");
  CHECK(seriesOf("PGA Tour Champions Golf", none) == "golf|CHAMP");
  CHECK(seriesOf("Korn Ferry Tour Golf", none) == "golf|KFT");
  CHECK(seriesOf("The Masters", sub("Final Round")) == "golf|MAJOR");
  CHECK(seriesOf("Golf", sub("Harbor Championship, Third Round")) == "golf|");
  CHECK(seriesOf("Live F1: Sao Paulo GP Qualifying", none) == "race|F1");
  CHECK(seriesOf("Formula 1: Harbor Grand Prix - Practice 1", none) == "race|F1");
  CHECK(seriesOf("F1: Rennen", none) == "race|F1");
  CHECK(seriesOf("En vivo: F\xC3\xB3rmula 1 - Carrera", none) == "race|F1");
  CHECK(seriesOf("NASCAR Cup Series Racing", sub("Championship Race")) == "race|NASCAR");
  CHECK(seriesOf("NASCAR Cup Series at Harbor", none) == "race|NASCAR");
  CHECK(seriesOf("Live MotoGP: Harbor Grand Prix - Sprint", none) == "race|MOTOGP");
  CHECK(seriesOf("Live WEC: 6 Hours of Harbor", none) == "race|WEC");
  CHECK(seriesOf("Live UFC Fight Night", none) == "fight|UFC");
  // Not the event: shows, highlights, replays, other classes.
  CHECK(seriesOf("Golf Central", none).empty());
  CHECK(seriesOf("Live From the Masters", none).empty());
  CHECK(seriesOf("PGA Tour Golf", sub("Highlights")).empty());
  CHECK(seriesOf("Golf Channel Classics", none).empty());
  CHECK(seriesOf("PGA Tour Flashback", none).empty());
  CHECK(seriesOf("F1 Show", none).empty());
  CHECK(seriesOf("F1 Academy: Race 1", none).empty());
  CHECK(seriesOf("NASCAR Xfinity Series Racing", none).empty());
  CHECK(seriesOf("NASCAR Race Hub", none).empty());  // a studio show
  CHECK(seriesOf("Live Moto2: Harbor Grand Prix - Race", none).empty());
  CHECK(seriesOf("UFC Fight Night: Press Conference", none).empty());
  CHECK(seriesOf("Golf", none).empty());  // nothing to say which
  CHECK(seriesOf("Evening News", none).empty());
  CHECK(seriesOf("The Open Road", none).empty());
}

TEST_CASE("a tour's programme on any channel: its tournament in play, with the listing's window") {
  // Round 2 from 13:30 UTC, four days' play; another tour's in Japan.
  const Seconds round = 11 * 3600;
  SportsGame pga = tournament("PGA", "401", "Harbor Bay Championship", utc(2026, 11, 6, 13, 30), round, 3);
  SportsGame lpga = tournament("LPGA", "501", "Valley Japan Classic", utc(2026, 11, 6, 0, 0), 7 * 3600, 3);
  BuildInput in;
  in.games = {pga, lpga};
  in.now = utc(2026, 11, 6, 18, 0);
  in.zone = time::Zone::usEastern();
  in.listings = {
      listing("e|golf", "PGA Tour Golf", std::string("Harbor Bay Championship, Second Round"), utc(2026, 11, 6, 20, 0),
              utc(2026, 11, 6, 23, 0)),
      // Tomorrow's round too (the next time it's on).
      listing("e|golf", "PGA Tour Golf", std::string("Third Round"), utc(2026, 11, 7, 20, 0), utc(2026, 11, 7, 23, 0)),
      // On a network's station, by the tour alone.
      listing("e|nbc", "PGA Tour Golf", std::nullopt, utc(2026, 11, 6, 19, 0), utc(2026, 11, 6, 22, 0)),
      // Abroad, in its own words.
      listing("e|skygolf", "Live LPGA Tour Golf", std::nullopt, utc(2026, 11, 7, 0, 0), utc(2026, 11, 7, 6, 0)),
      // Not the round: a show, highlights, and a listing after the day's play.
      listing("e|golf2", "Golf Central", std::nullopt, utc(2026, 11, 6, 23, 0), utc(2026, 11, 6, 23, 30)),
      listing("e|golf3", "PGA Tour Golf", std::string("Highlights"), utc(2026, 11, 6, 19, 0), utc(2026, 11, 6, 20, 0)),
      listing("e|golf4", "LPGA Tour Golf", std::nullopt, utc(2026, 11, 6, 12, 0), utc(2026, 11, 6, 15, 0)),
  };
  in.listings[3].abroad = true;
  auto out = build(in);
  REQUIRE(out);
  CHECK(sourcesOf(*out, "PGA|401") == std::vector<std::string>{"e|golf", "e|nbc"});
  CHECK(sourcesOf(*out, "LPGA|501") == std::vector<std::string>{"e|skygolf"});
  const EventSource* golf = sourceIn(*out, "PGA|401", "e|golf");
  REQUIRE(golf);
  CHECK(golf->from == utc(2026, 11, 6, 20, 0));  // today's listing, not tomorrow's
  CHECK(golf->until == utc(2026, 11, 6, 23, 0));
  CHECK(out->stats.seriesListings == 4);
  // The tile: later, at the listing's start; then live while it's on.
  SportsEvent e = *std::find_if(out->events.begin(), out->events.end(),
                                [](const SportsEvent& x) { return x.game && x.game->id == "PGA|401"; });
  CHECK(board::onAirFrom(e, in.now) == utc(2026, 11, 6, 19, 0));
  CHECK_FALSE(board::onAirFrom(e, utc(2026, 11, 6, 20, 30)));

  // A replay (the channel marks its live airings, not this one) isn't a way to watch.
  in.listings = {listing("e|golf", "PGA Tour Golf", std::nullopt, utc(2026, 11, 6, 20, 0), utc(2026, 11, 6, 23, 0))};
  in.listings[0].replay = true;
  CHECK(sourcesOf(*build(in), "PGA|401").empty());
}

TEST_CASE("a tour's programme with two of its events on: the one it names, else the one on TV") {
  SportsGame main = tournament("PGA", "401", "Harbor Bay Championship", utc(2026, 11, 6, 13, 30), 11 * 3600, 3);
  SportsGame opposite = tournament("PGA", "402", "Valley Springs Open", utc(2026, 11, 6, 14, 0), 11 * 3600, 3, {});
  BuildInput in;
  in.games = {main, opposite};
  in.now = utc(2026, 11, 6, 18, 0);
  in.zone = time::Zone::usEastern();
  in.listings = {
      listing("e|a", "PGA Tour Golf", std::string("Valley Springs Open, Second Round"), utc(2026, 11, 6, 20, 0),
              utc(2026, 11, 6, 23, 0)),
      listing("e|b", "PGA Tour Golf", std::nullopt, utc(2026, 11, 6, 20, 0), utc(2026, 11, 6, 23, 0)),
      // A major by its name only matches the tournament of that name.
      listing("e|c", "The Masters", std::nullopt, utc(2026, 11, 6, 20, 0), utc(2026, 11, 6, 23, 0)),
  };
  auto out = build(in);
  REQUIRE(out);
  CHECK(sourcesOf(*out, "PGA|402") == std::vector<std::string>{"e|a"});
  CHECK(sourcesOf(*out, "PGA|401") == std::vector<std::string>{"e|b"});
}

TEST_CASE("golf feeds on any of the tournament's days to come") {
  SportsGame pga = tournament("PGA", "401", "Harbor Bay Championship", utc(2026, 11, 5, 13, 30), 11 * 3600, 4);
  SportsGame seniors = tournament("CHAMP", "601", "Valley Schwab Cup Championship", utc(2026, 11, 5, 14, 0), 9 * 3600, 3);
  BuildInput in;
  in.games = {pga, seniors};
  in.now = utc(2026, 11, 5, 21, 0);
  in.zone = time::Zone::usEastern();
  in.events = {providerEvent("d2", "Golf: Harbor Bay Championship: Day 2", "Golf", utc(2026, 11, 6, 20, 0)),
               providerEvent("x", "Golf: Harbor Bay Championship", "Golf", utc(2026, 11, 10, 20, 0)),
               // Another event's "Cup" isn't the seniors' tournament.
               providerEvent("cup", "Live | Lakeside Cup | Golf Day 2", "Golf", utc(2026, 11, 5, 20, 0)),
               // A feed of no known sport naming the tournament, on its day.
               providerEvent("o1", "NEXT | HARBOR BAY CHAMPIONSHIP | DAY 1", "Other", utc(2026, 11, 5, 20, 0)),
               // Not one naming a single word of it, nor a game.
               providerEvent("o2", "Harbor Championship Wrestling", "Other", utc(2026, 11, 5, 20, 0)),
               providerEvent("o3", "Harbor Bay Sharks vs Lakeside Cup", "Other", utc(2026, 11, 5, 20, 0))};
  in.events[2].start.reset();
  auto out = build(in);
  REQUIRE(out);
  CHECK(sourcesOf(*out, "PGA|401") == std::vector<std::string>{"xt|d2", "xt|o1"});  // not after its last day
  CHECK(sourcesOf(*out, "CHAMP|601").empty());
}

TEST_CASE("a series' programme: the sessions on during it, by its words in any language") {
  const Seconds fri = utc(2026, 11, 6, 0, 0), sat = fri + 86'400, sun = sat + 86'400;
  BuildInput in;
  in.games = {session("F1", "Harbor GP", "fp1", "1|fp1", fri + 15 * 3600 + 1800),
              session("F1", "Harbor GP", "fp2", "1|fp2", fri + 19 * 3600),
              session("F1", "Harbor GP", "qual", "1|qual", sat + 18 * 3600),
              session("F1", "Harbor GP", "race", "1|race", sun + 17 * 3600),
              session("WEC", "6 Hours of Harbor", "qualifying - lmgt3", "2|q1", sat + 13 * 3600 + 1200),
              session("WEC", "6 Hours of Harbor", "hyperpole - hypercar", "2|hp", sat + 14 * 3600 + 1200),
              session("WEC", "6 Hours of Harbor", "fp3", "2|fp3", sat + 9 * 3600 + 1800),
              session("MOTOGP", "Harbor GP", "sprint", "3|sprint", sat + 16 * 3600),
              session("MOTOGP", "Harbor GP", "race", "3|race", sun + 16 * 3600),
              session("NASCAR", "NASCAR Cup Series at Harbor", "race", "4|race", sun + 20 * 3600)};
  in.now = sat + 12 * 3600;
  in.zone = time::Zone::usEastern();
  in.listings = {
      listing("e|sky", "Live F1: Harbor GP Qualifying", std::nullopt, sat + 17 * 3600, sat + 19 * 3600 + 1800),
      listing("e|de", "F1: Rennen", std::nullopt, sun + 16 * 3600 + 1800, sun + 19 * 3600 + 1800),
      // Qualifying again after the session: no session starts then.
      listing("e|sky2", "Live F1: Harbor GP Qualifying", std::nullopt, sat + 22 * 3600, sat + 23 * 3600 + 1800),
      // Its words say qualifying: not the race on at that time.
      listing("e|it", "F1: Qualifiche", std::nullopt, sun + 16 * 3600 + 1800, sun + 19 * 3600),
      listing("e|eu", "Live WEC: 6 Hours of Harbor - Hyperpole", std::nullopt, sat + 13 * 3600, sat + 15 * 3600),
      listing("e|tnt", "Live MotoGP: Harbor Grand Prix - Sprint", std::nullopt, sat + 15 * 3600 + 2700,
              sat + 16 * 3600 + 2700),
      listing("e|tnt2", "Live Moto2: Harbor Grand Prix - Race", std::nullopt, sun + 13 * 3600, sun + 14 * 3600),
      listing("e|nbc", "NASCAR Cup Series Racing", std::string("Championship Race"), sun + 19 * 3600 + 1800,
              sun + 23 * 3600 + 1800),
  };
  auto out = build(in);
  REQUIRE(out);
  CHECK(sourcesOf(*out, "F1|1|qual") == std::vector<std::string>{"e|sky"});
  CHECK(sourcesOf(*out, "F1|1|race") == std::vector<std::string>{"e|de"});
  CHECK(sourcesOf(*out, "WEC|2|q1") == std::vector<std::string>{"e|eu"});
  CHECK(sourcesOf(*out, "WEC|2|hp") == std::vector<std::string>{"e|eu"});
  CHECK(sourcesOf(*out, "WEC|2|fp3").empty());
  CHECK(sourcesOf(*out, "MOTOGP|3|sprint") == std::vector<std::string>{"e|tnt"});
  CHECK(sourcesOf(*out, "MOTOGP|3|race").empty());  // Moto2's race isn't it
  CHECK(sourcesOf(*out, "NASCAR|4|race") == std::vector<std::string>{"e|nbc"});
}

TEST_CASE("racing feeds by their series: NASCAR's race names, MotoGP and WEC sessions") {
  const Seconds fri = utc(2026, 11, 6, 0, 0), sat = fri + 86'400, sun = sat + 86'400;
  BuildInput in;
  in.games = {session("NASCAR", "NASCAR Cup Series at Harbor", "race", "4|race", sun + 20 * 3600),
              session("MOTOGP", "Qatar GP", "fp1", "3|fp1", fri + 11 * 3600 + 2700),
              session("MOTOGP", "Qatar GP", "practice", "3|pr", fri + 16 * 3600),
              session("MOTOGP", "Qatar GP", "sprint", "3|sprint", sat + 16 * 3600),
              session("WEC", "6 Hours of Harbor", "fp1", "2|fp1", fri + 9 * 3600 + 1800),
              session("WEC", "6 Hours of Harbor", "race", "2|race", sun + 10 * 3600)};
  in.now = fri + 8 * 3600;
  in.zone = time::Zone::usEastern();
  in.events = {
      providerEvent("n1", "NASCAR Cup Series at Harbor", "Motorsport", sun + 19 * 3600 + 1800),
      providerEvent("n2", "Nascar: NASCAR Cup Series Championship Race", "Motorsport", sun + 19 * 3600 + 1800),
      providerEvent("n3", "NASCAR Xfinity Series at Harbor", "Motorsport", sun + 19 * 3600 + 1800),
      providerEvent("m1", "MotoGP: Qatars GP vs Fri Traning 1", "Motorsport", fri + 11 * 3600 + 2100),
      providerEvent("m2", "MotoGP: Qatar GP Sprint", "Motorsport", sat + 16 * 3600),
      providerEvent("m3", "Moto2: Qatar GP Free Practice 1", "Motorsport", fri + 11 * 3600 + 2700),
      providerEvent("w1", "WEC: 6 Hours of Harbor Free Practice 1", "Motorsport", fri + 9 * 3600 + 1800),
      providerEvent("w2", "Race: 6 Hours of Harbor WEC 2026", "Other", sun + 9 * 3600 + 1800),
      // A MotoGP word in an F1 feed's group isn't a MotoGP feed.
      providerEvent("f1", "Formula 1: Harbor: Practice 1", "Motorsport", fri + 11 * 3600 + 2700),
  };
  in.events.back().family = "FORMULA 1 / MOTOGP";
  auto out = build(in);
  REQUIRE(out);
  CHECK(sourcesOf(*out, "NASCAR|4|race") == std::vector<std::string>{"xt|n1", "xt|n2"});
  CHECK(sourcesOf(*out, "MOTOGP|3|fp1") == std::vector<std::string>{"xt|m1"});
  CHECK(sourcesOf(*out, "MOTOGP|3|pr").empty());
  CHECK(sourcesOf(*out, "MOTOGP|3|sprint") == std::vector<std::string>{"xt|m2"});
  CHECK(sourcesOf(*out, "WEC|2|fp1") == std::vector<std::string>{"xt|w1"});
  CHECK(sourcesOf(*out, "WEC|2|race") == std::vector<std::string>{"xt|w2"});
}

TEST_CASE("a fight night's programme: the card's part on then") {
  SportsGame prelims = game("UFC", "Fighting", "600|prelims", team("p1", "UFC Fight Night Prelims"),
                            team("p1", "UFC Fight Night Prelims"), utc(2026, 11, 7, 22, 0));
  prelims.cardKey = "ufc||prelims";
  prelims.customTitle = "UFC Fight Night Prelims";
  SportsGame main = game("UFC", "Fighting", "600|main", team("f1", "Max Stone"), team("f2", "Lee River"),
                         utc(2026, 11, 8, 1, 0));
  main.cardKey = "ufc||main";
  BuildInput in;
  in.games = {prelims, main};
  in.now = utc(2026, 11, 7, 20, 0);
  in.zone = time::Zone::usEastern();
  in.listings = {listing("e|tnt", "Live UFC Fight Night", std::nullopt, utc(2026, 11, 8, 1, 0), utc(2026, 11, 8, 4, 0)),
                 listing("e|espn", "UFC Fight Night Prelims", std::nullopt, utc(2026, 11, 7, 22, 0),
                         utc(2026, 11, 8, 1, 0)),
                 // Next week's card isn't on.
                 listing("e|x", "Live UFC Fight Night", std::nullopt, utc(2026, 11, 14, 1, 0), utc(2026, 11, 14, 4, 0))};
  auto out = build(in);
  REQUIRE(out);
  CHECK(sourcesOf(*out, "UFC|600|main") == std::vector<std::string>{"e|tnt"});
  CHECK(sourcesOf(*out, "UFC|600|prelims") == std::vector<std::string>{"e|espn"});
}

TEST_CASE("a Test's listing on any of its days, not only the first") {
  SportsGame test = game("CRIC", "Cricket", "78", team("1", "Harbor"), team("2", "Valley Islands"),
                         utc(2026, 11, 5, 3, 30));
  test.end = utc(2026, 11, 9, 3, 30);
  test.matchFormat = "Test";
  BuildInput in;
  in.games = {test};
  in.now = utc(2026, 11, 6, 4, 0);
  in.zone = time::Zone::usEastern();
  in.listings = {listing("e|willow", "Harbor vs Valley Islands, 2nd Test, Day 2", std::nullopt, utc(2026, 11, 6, 3, 30),
                         utc(2026, 11, 6, 11, 30)),
                 // Overnight between two days: not a day's play.
                 listing("e|other", "Harbor vs Valley Islands", std::nullopt, utc(2026, 11, 6, 15, 0),
                         utc(2026, 11, 6, 18, 0))};
  auto out = build(in);
  REQUIRE(out);
  CHECK(sourcesOf(*out, "CRIC|78") == std::vector<std::string>{"e|willow"});
}

TEST_CASE("guide listings: a tour's programme kept, a replay marked where the guide marks live airings") {
  store::ListingChannel ch{"e|golf", true, "en", station_rank::lineup};
  ch.sportsChannel = true;
  const Seconds now = utc(2026, 11, 6, 18, 0);
  auto kept = store::listingFor(ch, "PGA Tour Golf", std::string("Harbor Bay Championship, Second Round"),
                                guide::Text(), true, now, now + 3 * 3600);
  REQUIRE(kept);
  CHECK(kept->series == "golf|PGA");
  CHECK(matcher::listingFromJson(matcher::toJson(*kept)) == *kept);
  CHECK_FALSE(store::listingFor(ch, "Golf Central", std::nullopt, guide::Text(), true, now, now + 1800));
  // The channel marks its live airings: the unmarked one is shown again.
  auto raw = [&](Seconds at, const char* title, int flags) {
    return guide::RawProgram{static_cast<int>(at / 60), 180, title, std::nullopt, guide::Text(), flags};
  };
  std::vector<guide::RawProgram> marked = {raw(now + 3600, "PGA Tour Golf", 2 | 16), raw(now + 5 * 3600, "PGA Tour Golf", 2)};
  auto out = store::extractListings({{ch, nullptr, &marked, nullptr}}, now);
  REQUIRE(out.listings.size() == 2);
  CHECK_FALSE(out.listings[0].replay);
  CHECK(out.listings[1].replay);
  // A guide that never marks live airings: none is taken for a replay.
  std::vector<guide::RawProgram> unmarked = {raw(now + 3600, "PGA Tour Golf", 2), raw(now + 5 * 3600, "PGA Tour Golf", 2)};
  auto plain = store::extractListings({{ch, nullptr, &unmarked, nullptr}}, now);
  REQUIRE(plain.listings.size() == 2);
  CHECK_FALSE(plain.listings[1].replay);
}

TEST_CASE("golf between rounds: the next round, later, not over") {
  // Round 1 done ("Play Complete"): the schedule's tournament moved on to
  // round 2, tomorrow from 13:30 UTC; a channel's listing then.
  SportsGame g = tournament("PGA", "401", "Harbor Bay Championship", utc(2026, 11, 5, 13, 30), 11 * 3600, 4);
  schedule::golfRoundOn(g, utc(2026, 11, 6, 3, 0));
  CHECK(g.start == utc(2026, 11, 6, 13, 30));
  CHECK_FALSE(g.completed);
  SportsEvent e = sportsdata::eventOf(g);
  EventSource golf = sportsdata::source("e|golf", EventSource::Kind::lineup, false, station_rank::lineup, "In My Lineup");
  golf.from = utc(2026, 11, 6, 20, 0);
  golf.until = utc(2026, 11, 6, 23, 0);
  e.sources = {golf};
  board::Input in;
  in.events = {e};
  in.now = utc(2026, 11, 6, 4, 0);
  in.zone = *time::Zone::named("America/Los_Angeles");
  in.channelFor = [](const EventSource& s) -> std::optional<board::ChannelLabel> {
    return board::ChannelLabel{s.channelID, "Channel " + s.channelID, "Channel " + s.channelID, {}};
  };
  LiveStatus done;
  done.phase = LivePhase::pre;
  done.detail = "Round 1 done";
  in.statuses["PGA|401"] = done;
  board::Board b = board::build(in);
  auto t = b.tiles.find("sev|PGA|401");
  REQUIRE(t != b.tiles.end());
  CHECK(t->second.state == SportsEvent::State::upcoming);
  CHECK(t->second.statusText == "5:30 AM");  // round 2's first tee time (Pacific), tomorrow
  // Once it's on, later until the listing's start (Build 141).
  in.statuses.clear();
  in.now = utc(2026, 11, 6, 15, 0);
  board::Board on = board::build(in);
  CHECK(on.tiles.at("sev|PGA|401").state == SportsEvent::State::upcoming);
  CHECK(on.events.at("sev|PGA|401").start == utc(2026, 11, 6, 20, 0));
  // Past the last round's finish: over.
  SportsGame last = g;
  schedule::golfRoundOn(last, utc(2026, 11, 20, 0, 0));
  e.game = last;
  e.start = last.start;
  CHECK(last.start == utc(2026, 11, 8, 13, 30));
  CHECK(e.state(utc(2026, 11, 8, 20, 0)) == SportsEvent::State::live);
  CHECK(e.state(utc(2026, 11, 9, 6, 0)) == SportsEvent::State::finished);
}
