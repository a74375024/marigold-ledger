// Live scores: phases and clocks per sport, scoreboard parsing (scores,
// possession, red zone, bases, fight cards, race sessions, tennis sets),
// the polling budget (which leagues, how often, backoff, daily cap) and
// merging statuses (when a game was seen final).
#include <set>

#include "core/sports/scores.hpp"
#include "doctest.h"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::sports;
using namespace marigold::sports::scores;
using sportsdata::game;
using sportsdata::team;
using sportsdata::utc;

namespace {

const League& L(const char* s) { return *leagueByShort(s); }

}  // namespace

TEST_CASE("phases from the scoreboard state and status name") {
  CHECK(livePhase(std::string("in"), "STATUS_IN_PROGRESS") == LivePhase::live);
  CHECK(livePhase(std::string("in"), "STATUS_RAIN_DELAY") == LivePhase::delayed);
  CHECK(livePhase(std::string("post"), "STATUS_FINAL") == LivePhase::final);
  CHECK(livePhase(std::string("pre"), "STATUS_SCHEDULED") == LivePhase::pre);
  CHECK(livePhase(std::nullopt, "") == LivePhase::pre);
  CHECK(livePhase(std::string("post"), "STATUS_POSTPONED") == LivePhase::postponed);
  CHECK(livePhase(std::string("in"), "STATUS_SUSPENDED") == LivePhase::postponed);
}

TEST_CASE("the tile's clock per sport") {
  auto live = [](const char* league, int period, const char* clock, std::optional<std::string> shortDetail = {},
                 const char* name = "STATUS_IN_PROGRESS") {
    return liveDetail(L(league), LivePhase::live, name, period, clock, shortDetail);
  };
  CHECK(live("NFL", 3, "4:12") == "Q3 4:12");
  CHECK(live("NFL", 5, "8:00") == "OT 8:00");
  CHECK(live("NBA", 6, "2:00") == "2OT 2:00");
  CHECK(live("NCAAW", 2, "1:00") == "Q2 1:00");
  CHECK(live("NCAAM", 2, "11:02") == "2nd 11:02");
  CHECK(live("NCAAM", 3, "4:00") == "OT 4:00");
  CHECK(live("NHL", 1, "12:00") == "1st 12:00");
  CHECK(live("NHL", 4, "3:00") == "OT 3:00");
  CHECK(live("NHL", 5, "0:00") == "SO");
  CHECK(live("EPL", 2, "67'") == "67'");
  CHECK(live("EPL", 2, "", std::string("HT")) == "HT");
  CHECK(live("MLB", 8, "0:00", std::string("Bot 8th")) == "Bot 8th");
  CHECK(live("NFL", 2, "0:00", std::nullopt, "STATUS_HALFTIME") == "Halftime");
  CHECK(live("NFL", 1, "0:00", std::string("End of 1st"), "STATUS_END_PERIOD") == "End of 1st");
  CHECK(liveDetail(L("NFL"), LivePhase::final, "STATUS_FINAL", 4, "0:00", std::string("FINAL")) == "Final");
  CHECK(liveDetail(L("NFL"), LivePhase::final, "STATUS_FINAL", 5, "0:00", std::string("Final/OT")) == "Final/OT");
  CHECK(liveDetail(L("NFL"), LivePhase::final, "STATUS_FINAL", 4, "0:00", std::nullopt) == "Final");
  CHECK(liveDetail(L("MLB"), LivePhase::delayed, "STATUS_DELAYED", 3, "", std::string("Rain Delay")) == "Rain Delay");
  CHECK(liveDetail(L("NFL"), LivePhase::pre, "STATUS_SCHEDULED", 0, "", std::nullopt).empty());
}

TEST_CASE("today's scoreboard: scores, possession, red zone, situation; nothing before the start") {
  const char* body = R"JSON({"events":[
    {"id":"401001","competitions":[{
      "status":{"period":"3","displayClock":"4:12","type":{"name":"STATUS_IN_PROGRESS","state":"in","shortDetail":"4:12 - 3rd"}},
      "competitors":[{"homeAway":"home","score":"21","team":{"id":"1"}},{"homeAway":"away","score":17,"team":{"id":"2"}}],
      "situation":{"downDistanceText":"3rd & 7 at HCG 35","possession":"2","isRedZone":true,
                   "lastPlay":{"text":"A pass to the right for 12 yards."}}}]},
    {"id":"401002","competitions":[{
      "status":{"type":{"name":"STATUS_SCHEDULED","state":"pre","shortDetail":"10/25 - 4:25 PM EDT"}},
      "competitors":[{"homeAway":"home","score":"0"},{"homeAway":"away","score":"0"}]}]},
    {"id":"401003","status":{"type":{"name":"STATUS_FINAL","state":"post","completed":true,"shortDetail":"Final"}},
     "competitions":[{"competitors":[{"homeAway":"home","score":"10"},{"homeAway":"away","score":"13"}]}]},
    {"competitions":[]}
  ]})JSON";
  Statuses s = statuses(body, L("NFL"));
  REQUIRE(s.size() == 3);
  const LiveStatus& live = s.at("NFL|401001");
  CHECK(live.phase == LivePhase::live);
  CHECK(live.detail == "Q3 4:12");
  CHECK(live.homeScore == "21");
  CHECK(live.awayScore == "17");
  CHECK(live.possession == "2");
  CHECK(live.redZone);
  CHECK(live.situation == "3rd & 7 at HCG 35");
  CHECK(live.lastPlay == "A pass to the right for 12 yards.");
  const LiveStatus& pre = s.at("NFL|401002");
  CHECK(pre.phase == LivePhase::pre);
  CHECK_FALSE(pre.homeScore);
  CHECK(pre.detail == "10/25 - 4:25 PM EDT");
  const LiveStatus& done = s.at("NFL|401003");
  CHECK(done.phase == LivePhase::final);  // the event's status when the competition has none
  CHECK(done.detail == "Final");
  CHECK(done.awayScore == "13");
  CHECK(statuses("garbage", L("NFL")).empty());
}

TEST_CASE("baseball: outs, count and runners; long last plays shortened") {
  std::string longPlay(130, 'x');
  std::string body = R"({"events":[{"id":"7","competitions":[{
      "status":{"period":8,"type":{"name":"STATUS_IN_PROGRESS","state":"in","shortDetail":"Bot 8th"}},
      "competitors":[{"homeAway":"home","score":"3"},{"homeAway":"away","score":"2"}],
      "situation":{"balls":3,"strikes":"1","outs":1,"onFirst":true,"onThird":true,"lastPlay":{"text":")" +
                     longPlay + R"("}}}]}]})";
  LiveStatus s = statuses(body, L("MLB")).at("MLB|7");
  CHECK(s.detail == "Bot 8th");
  CHECK(s.situation == "1 out \xC2\xB7 3-1 count");
  REQUIRE(s.bases);
  CHECK(*s.bases == std::vector<bool>{true, false, true});
  REQUIRE(s.lastPlay);
  CHECK(s.lastPlay->size() == 107 + 3);  // 107 characters and an ellipsis
}

TEST_CASE("fight cards: fights done, the fight on now, scrapped bouts") {
  const char* body = R"JSON({"events":[{"id":"600001","status":{"type":{"state":"in"}},"competitions":[
    {"cardSegment":"Early Prelims","status":{"type":{"state":"post","completed":true}}},
    {"cardSegment":"Early Prelims","status":{"type":{"state":"pre"}}},
    {"cardSegment":"Prelims","status":{"type":{"state":"post","completed":true}}},
    {"cardSegment":"Prelims","status":{"type":{"state":"in","name":"STATUS_IN_PROGRESS"}},
     "competitors":[{"athlete":{"displayName":"Ann One","flag":{"href":"https://example.invalid/c/bra.png"}}},
                    {"athlete":{"displayName":"Bea Two"}}]},
    {"cardSegment":"Main Card","status":{"type":{"state":"pre"}},
     "competitors":[{"athlete":{"displayName":"Max Stone"}},{"athlete":{"displayName":"Lee River"}}]},
    {"cardSegment":"Main Card","status":{"type":{"state":"post","name":"STATUS_CANCELED"}}}
  ]}]})JSON";
  Statuses s = statuses(body, L("UFC"));
  // The early prelims' last bout never ran: once a later part began, it's over.
  CHECK(s.at("UFC|600001|early").phase == LivePhase::final);
  const LiveStatus& prelims = s.at("UFC|600001|prelims");
  CHECK(prelims.phase == LivePhase::live);
  CHECK(prelims.detail == "fight 2 of 2");
  CHECK(prelims.boutA == "Ann One");
  CHECK(prelims.boutACountry == "BRA");
  CHECK(prelims.boutB == "Bea Two");
  const LiveStatus& main = s.at("UFC|600001|main");
  CHECK(main.phase == LivePhase::pre);
  CHECK(main.boutsTotal == 1);  // the canceled bout doesn't count
  CHECK(main.boutA == "Max Stone");
}

TEST_CASE("race sessions and tennis matches") {
  Statuses race = statuses(R"({"events":[{"id":"700","competitions":[
      {"id":"1","status":{"type":{"state":"post"}}},
      {"id":"2","status":{"type":{"state":"in","shortDetail":"Lap 34 of 56"}}},
      {"id":"3","status":{"type":{"state":"in","shortDetail":"10/25 - 3:00 PM EDT"}}},
      {"id":"4","status":{"type":{"state":"pre","completed":true}}}]}]})",
                           L("F1"));
  CHECK(race.at("F1|700|1").detail == "Final");
  CHECK(race.at("F1|700|2").detail == "Lap 34 of 56");
  CHECK(race.at("F1|700|3").detail.empty());
  CHECK(race.at("F1|700|4").phase == LivePhase::final);
  Statuses tennis = statuses(R"({"events":[{"id":"80","groupings":[{"competitions":[
      {"id":"1","status":{"period":2,"type":{"state":"in"}},
       "competitors":[{"homeAway":"home","linescores":[{"value":6.0},{"value":"3"}]},
                      {"homeAway":"away","linescores":[{"value":4},{"value":2}]}]},
      {"id":"2","status":{"type":{"state":"pre"}},"competitors":[{"linescores":[{"value":1}]},{}]}]}]}]})",
                             L("ATP"));
  const LiveStatus& m = tennis.at("ATP|80|1");
  CHECK(m.detail == "Set 2");
  CHECK(*m.homeSets == std::vector<std::string>{"6", "3"});
  CHECK(*m.awaySets == std::vector<std::string>{"4", "2"});
  CHECK_FALSE(tennis.at("ATP|80|2").homeSets);  // nothing before the start
}

TEST_CASE("score requests: today's scoreboard by Eastern day, or the current listing") {
  CHECK(scoreboardRequest(L("NFL"), "20261025").url ==
        "https://site.api.espn.com/apis/site/v2/sports/football/nfl/scoreboard?dates=20261025");
  CHECK(scoreboardRequest(L("NCAAM"), "20261025").url ==
        "https://site.api.espn.com/apis/site/v2/sports/basketball/mens-college-basketball/scoreboard?dates=20261025&groups=50");
  CHECK(scoreboardRequest(L("F1"), "").url == "https://site.api.espn.com/apis/site/v2/sports/racing/f1/scoreboard");
  CHECK(scoreboardRequest(L("NFL"), "").timeoutSeconds == 15);
}

namespace {

std::string nflLive(const std::string& id) {
  return R"({"events":[{"id":")" + id +
         R"(","competitions":[{"status":{"period":1,"displayClock":"9:00","type":{"name":"STATUS_IN_PROGRESS","state":"in"}},
          "competitors":[{"homeAway":"home","score":"0"},{"homeAway":"away","score":"7"}]}]}]})";
}

RoundInput roundAt(Seconds now, std::vector<SportsGame> games) {
  RoundInput in;
  in.games = std::move(games);
  in.now = now;
  in.zone = time::Zone::usEastern();
  return in;
}

}  // namespace

TEST_CASE("polling: only while looking, only leagues with a game on, once a minute while live") {
  Seconds kickoff = utc(2026, 10, 25, 17, 0);
  SportsGame nflGame = game("NFL", "NFL", "401001", team("1", "Harbor City Gulls"), team("2", "Team Alpha"), kickoff);
  SportsGame later = game("NHL", "NHL", "5", team("3", "Lake Pikes"), team("4", "Mountain Goats"), kickoff + 4 * 3600);
  LiveScores scores;
  CHECK(scores.start(Holder::board, true));
  CHECK(scores.looking());
  CHECK_FALSE(scores.start(Holder::board, true));  // already polling
  // Ten minutes before kickoff: nothing to ask; look again in 5 minutes.
  scores.beginRound(roundAt(kickoff - 600, {nflGame, later}));
  CHECK_FALSE(scores.nextRequest(kickoff - 600));
  CHECK(scores.waitSeconds() == kIdleWait);
  // Within 5 minutes of the start: that league, once.
  Seconds now = kickoff - 240;
  scores.beginRound(roundAt(now, {nflGame, later}));
  auto request = scores.nextRequest(now);
  REQUIRE(request);
  CHECK(request->league.shortName == "NFL");
  CHECK(request->day == "20261025");
  CHECK(request->waitMs == 0);
  scores.onResponse(200, nflLive("401001"), now);
  CHECK_FALSE(scores.nextRequest(now));
  CHECK(scores.waitSeconds() == kLiveInterval);  // a game live, scores on
  CHECK(scores.status("NFL|401001")->awayScore == "7");
  CHECK(scores.revision() == 1);
  CHECK(scores.requestsToday(now, time::Zone::usEastern()) == 1);
  // Due again only after a minute.
  scores.beginRound(roundAt(now + 30, {nflGame, later}));
  CHECK_FALSE(scores.nextRequest(now + 30));
  CHECK(scores.waitSeconds() == 30);
  // Scores off: every 2 minutes.
  RoundInput off = roundAt(now + 60, {nflGame, later});
  off.showsScores = false;
  scores.beginRound(off);
  REQUIRE(scores.nextRequest(now + 60));
  scores.onResponse(200, nflLive("401001"), now + 60);
  CHECK_FALSE(scores.nextRequest(now + 60));
  CHECK(scores.waitSeconds() == kQuietInterval);
  // Watching a channel: nothing, unless the event guide is open.
  RoundInput watching = roundAt(now + 600, {nflGame});
  watching.watching = true;
  scores.beginRound(watching);
  CHECK_FALSE(scores.nextRequest(now + 600));
  CHECK(scores.waitSeconds() == kWatchingWait);
  CHECK(scores.start(Holder::guide, true));  // the guide looks now
  scores.beginRound(watching);
  CHECK(scores.nextRequest(now + 600));
  scores.cancelRound();
  CHECK_FALSE(scores.stop(Holder::guide));
  CHECK(scores.stop(Holder::board));
  CHECK_FALSE(scores.looking());
  // The schedule switched off: nothing starts.
  LiveScores off2;
  CHECK_FALSE(off2.start(Holder::board, false));
}

TEST_CASE("polling: failures back off, several leagues share the pace, the daily cap") {
  Seconds now = utc(2026, 10, 25, 17, 30);
  std::vector<SportsGame> games = {
      game("NFL", "NFL", "1", team("1", "A"), team("2", "B"), now - 1800),
      game("NHL", "NHL", "2", team("3", "C"), team("4", "D"), now - 600),
      game("NBA", "NBA", "3", team("5", "E"), team("6", "F"), now - 600),
  };
  LiveScores scores;
  scores.start(Holder::board, true);
  scores.beginRound(roundAt(now, games));
  std::vector<std::string> asked;
  int answered = 0;
  while (auto r = scores.nextRequest(now)) {
    asked.push_back(r->league.shortName);
    if (answered > 0) CHECK(r->waitMs == kRequestSpacingMs);
    // NBA fails; the others answer with nothing live.
    scores.onResponse(r->league.shortName == "NBA" ? 503 : 200, "{\"events\":[]}", now);
    answered += 1;
  }
  CHECK(asked == std::vector<std::string>{"NBA", "NFL", "NHL"});  // A-Z, one each
  // Three leagues on at once: 120 s x 1.5; the failure waits 2 minutes.
  CHECK(scores.waitSeconds() == 120);
  scores.beginRound(roundAt(now + 130, games));
  auto retry = scores.nextRequest(now + 130);
  REQUIRE(retry);
  CHECK(retry->league.shortName == "NBA");
  scores.onResponse(0, "", now + 130);
  CHECK_FALSE(scores.nextRequest(now + 130));  // NFL and NHL aren't due yet
  // Second failure: 5 minutes.
  scores.beginRound(roundAt(now + 200, games));
  std::vector<std::string> second;
  while (auto r = scores.nextRequest(now + 200)) {
    second.push_back(r->league.shortName);
    scores.onResponse(200, "{\"events\":[]}", now + 200);
  }
  CHECK(second == std::vector<std::string>{"NFL", "NHL"});
  CHECK(scores.waitSeconds() == 180);
  scores.beginRound(roundAt(now + 300, games));
  CHECK_FALSE(scores.nextRequest(now + 300));  // nobody is due
  // Games no scoreboard listed three times stop keeping a league polled.
  scores.beginRound(roundAt(now + 1000, games));
  while (auto r = scores.nextRequest(now + 1000)) scores.onResponse(200, "{\"events\":[]}", now + 1000);
  scores.beginRound(roundAt(now + 2000, games));
  std::vector<std::string> left;
  while (auto r = scores.nextRequest(now + 2000)) {
    left.push_back(r->league.shortName);
    scores.onResponse(200, "{\"events\":[]}", now + 2000);
  }
  CHECK(left == std::vector<std::string>{"NBA"});  // it failed, so it was missed fewer times

  // The daily cap: nothing more until tomorrow.
  LiveScores capped(LiveScores::DailyCount{"2026-10-25", kDailyCap});
  capped.start(Holder::board, true);
  capped.beginRound(roundAt(now, games));
  CHECK_FALSE(capped.nextRequest(now));
  CHECK(capped.waitSeconds() == kCappedWait);
  // A new day resets the count.
  capped.beginRound(roundAt(now + 86'400, {game("NFL", "NFL", "9", team("1", "A"), team("2", "B"), now + 86'400)}));
  CHECK(capped.nextRequest(now + 86'400));
  CHECK(capped.daily().day == "2026-10-26");
  CHECK(capped.daily().count == 1);
}

TEST_CASE("merging statuses: when a game was seen final") {
  Seconds now = utc(2026, 10, 25, 21, 0);
  LiveScores scores;
  LiveStatus live;
  live.phase = LivePhase::live;
  live.detail = "Q4 1:00";
  scores.apply({{"NFL|1", live}}, now - 600, {});
  LiveStatus final;
  final.phase = LivePhase::final;
  final.detail = "Final";
  scores.apply({{"NFL|1", final}, {"NFL|2", final}}, now, {});
  // Seen ending now; already over when first looked at: 20 minutes ago.
  CHECK(scores.finalSeen().at("NFL|1") == now);
  CHECK(scores.finalSeen().at("NFL|2") == now - 20 * 60);
  int revision = scores.revision();
  scores.apply({{"NFL|1", final}}, now + 60, {});
  CHECK(scores.revision() == revision);  // nothing changed
  CHECK(scores.finalSeen().at("NFL|1") == now);
}

TEST_CASE("suspended: resumes later in baseball and tennis, postponed elsewhere") {
  bool suspended = false;
  CHECK(livePhase(L("MLB"), std::string("in"), "STATUS_SUSPENDED", &suspended) == LivePhase::delayed);
  CHECK(suspended);
  CHECK(livePhase(L("ATP"), std::string("in"), "STATUS_SUSPENDED", &suspended) == LivePhase::delayed);
  CHECK(suspended);
  CHECK(livePhase(L("NFL"), std::string("in"), "STATUS_SUSPENDED", &suspended) == LivePhase::postponed);
  CHECK_FALSE(suspended);
  CHECK(livePhase(L("EPL"), std::string("in"), "STATUS_SUSPENDED", &suspended) == LivePhase::postponed);
  CHECK(livePhase(L("MLB"), std::string("post"), "STATUS_POSTPONED", &suspended) == LivePhase::postponed);
  CHECK_FALSE(suspended);
  CHECK(livePhase(L("MLB"), std::string("in"), "STATUS_RAIN_DELAY", &suspended) == LivePhase::delayed);
  CHECK_FALSE(suspended);  // a delay, not a suspension

  std::string body = R"({"events":[{"id":"8","competitions":[{
      "status":{"period":6,"type":{"name":"STATUS_SUSPENDED","state":"in","shortDetail":"Suspended - Rain"}},
      "competitors":[{"homeAway":"home","score":"4"},{"homeAway":"away","score":"3"}]}]}]})";
  LiveStatus s = statuses(body, L("MLB")).at("MLB|8");
  CHECK(s.phase == LivePhase::delayed);
  CHECK(s.suspended);
  CHECK(s.detail == "Suspended - Rain");
  // The same game in football: postponed, as before.
  LiveStatus f = statuses(body, L("NFL")).at("NFL|8");
  CHECK(f.phase == LivePhase::postponed);
  CHECK_FALSE(f.suspended);
}

TEST_CASE("soccer knockouts: penalties and the aggregate over two legs") {
  // Shaped like a real second leg that went to penalties: aggregate and
  // shootout goals as numbers, the leg as an object.
  std::string pens = R"({"events":[{"id":"9","competitions":[{
      "status":{"displayClock":"120'","period":5,"type":{"name":"STATUS_FINAL_PEN","state":"post","completed":true,"shortDetail":"FT-Pens"}},
      "leg":{"value":2,"displayValue":"2nd Leg"},
      "competitors":[{"homeAway":"home","score":"2","aggregateScore":2.0,"shootoutScore":3},
                     {"homeAway":"away","score":"1","aggregateScore":2.0,"shootoutScore":2}]}]}]})";
  LiveStatus s = statuses(pens, L("UCL")).at("UCL|9");
  CHECK(s.phase == LivePhase::final);
  CHECK(s.detail == "FT-Pens");
  CHECK(s.homePens == 3);
  CHECK(s.awayPens == 2);
  CHECK(s.homeAggregate == 2);
  CHECK(s.awayAggregate == 2);
  CHECK(s.situation == "Aggregate 2\xE2\x80\x93" "2");  // over: the aggregate, not "Penalties"

  // The shootout on now.
  std::string shootout = R"({"events":[{"id":"10","competitions":[{
      "status":{"period":5,"type":{"name":"STATUS_SHOOTOUT","state":"in","shortDetail":"Pens"}},
      "leg":{"value":2},
      "competitors":[{"homeAway":"home","score":"1","aggregateScore":3,"shootoutScore":4},
                     {"homeAway":"away","score":"1","aggregateScore":3,"shootoutScore":3}]}]}]})";
  LiveStatus live = statuses(shootout, L("UCL")).at("UCL|10");
  CHECK(live.situation == "Penalties");

  // A second leg without a shootout; a first leg shows no aggregate.
  std::string second = R"({"events":[{"id":"11","competitions":[{
      "status":{"displayClock":"67'","period":2,"type":{"name":"STATUS_IN_PROGRESS","state":"in","shortDetail":"67'"}},
      "leg":{"value":2,"displayValue":"2nd Leg"},
      "competitors":[{"homeAway":"home","score":"2","aggregateScore":3.0},
                     {"homeAway":"away","score":"0","aggregateScore":1.0}]}]}]})";
  LiveStatus leg2 = statuses(second, L("UCL")).at("UCL|11");
  CHECK_FALSE(leg2.homePens);
  CHECK(leg2.situation == "Aggregate 3\xE2\x80\x93" "1");
  std::string first = R"({"events":[{"id":"12","competitions":[{
      "status":{"period":2,"type":{"name":"STATUS_IN_PROGRESS","state":"in","shortDetail":"67'"}},
      "leg":{"value":1,"displayValue":"1st Leg"},
      "competitors":[{"homeAway":"home","score":"1","aggregateScore":1.0},
                     {"homeAway":"away","score":"0","aggregateScore":0.0}]}]}]})";
  LiveStatus leg1 = statuses(first, L("UCL")).at("UCL|12");
  CHECK_FALSE(leg1.homeAggregate);
  CHECK_FALSE(leg1.situation);
}

TEST_CASE("new sports: rugby minutes, lacrosse quarters, volleyball sets, race top three") {
  // Rugby: the running minute.
  std::string rugby = R"({"events":[{"id":"1","competitions":[{
      "status":{"displayClock":"64'","period":2,"type":{"name":"STATUS_SECOND_HALF","state":"in","shortDetail":"2nd Half"}},
      "competitors":[{"homeAway":"home","score":"24"},{"homeAway":"away","score":"17"}]}]}]})";
  LiveStatus r = statuses(rugby, L("6N")).at("6N|1");
  CHECK(r.detail == "64'");
  CHECK(r.homeScore == "24");

  // Lacrosse: quarters as football reads them.
  std::string lax = R"({"events":[{"id":"2","competitions":[{
      "status":{"displayClock":"8:12","period":3,"type":{"name":"STATUS_IN_PROGRESS","state":"in","shortDetail":"8:12 - 3rd"}},
      "competitors":[{"homeAway":"home","score":"11"},{"homeAway":"away","score":"9"}]}]}]})";
  CHECK(statuses(lax, L("NCAAML")).at("NCAAML|2").detail == "Q3 8:12");
  CHECK(statuses(lax, L("PLL")).at("PLL|2").detail == "Q3 8:12");

  // Volleyball: sets won as the score, the set on now in the tile's order
  // (away first, as US college tiles are).
  std::string vb = R"({"events":[{"id":"3","competitions":[{
      "status":{"period":4,"type":{"name":"STATUS_IN_PROGRESS","state":"in","shortDetail":"4th Set"}},
      "competitors":[{"homeAway":"home","score":"2","linescores":[{"value":25.0},{"value":22.0},{"value":25.0},{"value":11.0}]},
                     {"homeAway":"away","score":"1","linescores":[{"value":20.0},{"value":25.0},{"value":19.0},{"value":14.0}]}]}]}]})";
  LiveStatus v = statuses(vb, L("NCAAWV")).at("NCAAWV|3");
  CHECK(v.homeScore == "2");
  CHECK(v.situation == "Set 4 \xC2\xB7 14\xE2\x80\x93" "11");
  // Over: no set line.
  std::string vbDone = R"({"events":[{"id":"4","competitions":[{
      "status":{"period":3,"type":{"name":"STATUS_FINAL","state":"post","shortDetail":"Final"}},
      "competitors":[{"homeAway":"home","score":"3","linescores":[{"value":25.0}]},{"homeAway":"away","score":"0","linescores":[{"value":12.0}]}]}]}]})";
  CHECK_FALSE(statuses(vbDone, L("NCAAWV")).at("NCAAWV|4").situation);

  // A finished race: the top three by finishing order.
  std::string race = R"({"events":[{"id":"5","competitions":[{"id":"9",
      "status":{"type":{"name":"STATUS_FINAL","state":"post","completed":true}},
      "competitors":[{"order":2,"athlete":{"shortName":"T. Gibbs"}},{"order":1,"athlete":{"shortName":"C. Bell"}},
                     {"order":3,"athlete":{"shortName":"T. Reddick"}},{"order":4,"athlete":{"shortName":"D. Hamlin"}}]}]}]})";
  CHECK(statuses(race, L("NASCAR")).at("NASCAR|5|9").top3 == std::vector<std::string>{"BEL", "GIB", "RED"});
  CHECK(statuses(race, L("IRL")).at("IRL|5|9").top3.size() == 3);
}

TEST_CASE("golf: the round, the top two with ties, flags and holes played") {
  std::string body = R"({"events":[{"id":"401","name":"Harbor Championship","shortName":"Harbor Champ.","date":"2026-04-09T04:00Z",
    "competitions":[{"status":{"period":3,"type":{"name":"STATUS_IN_PROGRESS","state":"in"}},
      "competitors":[
        {"order":2,"score":"-10","athlete":{"shortName":"M. Valley","flag":{"href":"https://example.invalid/f/esp.png"}},
         "linescores":[{"period":1},{"period":2},{"period":3,"linescores":[{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}]}]},
        {"order":1,"score":"-12","athlete":{"shortName":"S. Harbor","flag":{"href":"https://example.invalid/f/irl.png"}},
         "linescores":[{"period":1},{"period":2},{"period":3,"linescores":[{},{},{},{},{},{},{},{},{},{},{},{},{},{}]}]},
        {"order":3,"score":"-10","athlete":{"shortName":"P. Third"}}]}]}]})";
  LiveStatus s = statuses(body, L("PGA")).at("PGA|401");
  CHECK(s.phase == LivePhase::live);
  CHECK(s.detail == "Round 3");
  REQUIRE(s.leaders.size() == 2);
  CHECK(s.leaders[0].pos == "1");
  CHECK(s.leaders[0].name == "S. Harbor");
  CHECK(s.leaders[0].score == "-12");
  CHECK(s.leaders[0].thru == "14");
  CHECK(s.leaders[0].flag == "https://example.invalid/f/irl.png");
  CHECK(s.leaders[1].pos == "T2");  // level with the third
  CHECK(s.leaders[1].thru == "F");
  // Between rounds: the next one is to come, the leaderboard stays.
  std::string done = body;
  done.replace(done.find("STATUS_IN_PROGRESS"), 18, "STATUS_PLAY_COMPLETE");
  LiveStatus between = statuses(done, L("PGA")).at("PGA|401");
  CHECK(between.phase == LivePhase::pre);
  CHECK(between.detail == "Round 3 done");
  CHECK(between.leaders.size() == 2);
}
