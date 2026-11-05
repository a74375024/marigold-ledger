// The player's game banner (Build 112): its status by the clock alone
// (never the scoreboard's), the league line under the matchup, and which
// game a guide channel is showing.
#include "core/sports/game_banner.hpp"
#include "doctest.h"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::sports;
using sportsdata::eventOf;
using sportsdata::feed;
using sportsdata::game;
using sportsdata::source;
using sportsdata::team;
using sportsdata::utc;
using banner::Chip;

namespace {

// Sunday 2026-10-25 00:20 UTC: 8:20 PM in New York.
const Seconds kKickoff = utc(2026, 10, 25, 0, 20);
const Seconds kMin = 60, kHour = 3600;

SportsEvent football(const std::string& id, Seconds start, std::vector<EventSource> sources = {}) {
  SportsGame g = game("NFL", "NFL", id, team("rdg", "Ridgeview Hawks", std::string("RDG")),
                      team("npt", "Northport Mariners", std::string("NPT")), start, {}, std::string("Ridgeview Stadium"));
  g.weekLabel = "Week 8";
  return eventOf(g, std::move(sources));
}

EventSource channel(const std::string& id) { return source(id, EventSource::Kind::guide); }

board::Board boardOf(const std::vector<SportsEvent>& events) {
  board::Board b;
  for (const SportsEvent& e : events) b.events.emplace(e.id, e);
  return b;
}

}  // namespace

TEST_CASE("game banner: before the start, a countdown") {
  SportsEvent e = football("1", kKickoff);
  CHECK(banner::status(e, kKickoff - 25 * kMin).chip == Chip::soon);
  CHECK(banner::status(e, kKickoff - 25 * kMin).text == "in 25 min");
  CHECK(banner::status(e, kKickoff - 25 * kMin - 30).text == "in 26 min");  // a minute started counts
  CHECK(banner::status(e, kKickoff - 10).text == "in 1 min");
  CHECK(banner::status(e, kKickoff - 60 * kMin).text == "in 1 h");
  CHECK(banner::status(e, kKickoff - 80 * kMin).text == "in 1 h 20 min");
  CHECK(banner::status(e, kKickoff - 4 * kHour - 40 * kMin).text == "in 5 h");
}

TEST_CASE("game banner: LIVE until the usual length, then Likely over for good") {
  SportsEvent e = football("1", kKickoff);
  Seconds end = *e.scheduledEnd();
  CHECK(end == kKickoff + e.expectedLength());
  CHECK(banner::status(e, kKickoff).chip == Chip::live);
  CHECK(banner::status(e, kKickoff).text == "LIVE");
  CHECK(banner::status(e, end - 1).chip == Chip::live);
  CHECK(banner::status(e, end).chip == Chip::likelyOver);
  CHECK(banner::status(e, end).text == "Likely over");
  // Long after (the tiles would say Ended by then): still only "Likely over".
  CHECK(banner::status(e, end + 6 * kHour).chip == Chip::likelyOver);
}

TEST_CASE("game banner: a later guide listing stretches the game") {
  SportsEvent e = football("1", kKickoff);
  Seconds usual = kKickoff + e.expectedLength();
  e.listingEnd = usual + 40 * kMin;
  CHECK(banner::status(e, usual + 10 * kMin).chip == Chip::live);
  CHECK(banner::status(e, usual + 40 * kMin).chip == Chip::likelyOver);
  // Beyond the sport's cap the listing doesn't count.
  e.listingEnd = kKickoff + 20 * kHour;
  CHECK(*e.scheduledEnd() == usual);
}

TEST_CASE("game banner: the scoreboard never decides") {
  SportsEvent e = football("1", kKickoff);
  // The scoreboard says it's over, or not started, or the schedule had it
  // done: the banner goes by the clock all the same.
  e.live = LivePhase::final;
  CHECK(banner::status(e, kKickoff + kHour).chip == Chip::live);
  e.live = LivePhase::pre;
  CHECK(banner::status(e, kKickoff + kHour).chip == Chip::live);
  e.live = LivePhase::live;
  CHECK(banner::status(e, kKickoff - 5 * kMin).chip == Chip::soon);
  e.live.reset();
  e.game->completed = true;
  CHECK(banner::status(e, kKickoff + kHour).chip == Chip::live);
  e.feedsEndedAt = kKickoff + 30 * kMin;
  CHECK(banner::status(e, kKickoff + kHour).chip == Chip::live);
}

TEST_CASE("game banner: no time or postponed, no chip") {
  SportsEvent e = football("1", kKickoff);
  e.start.reset();
  CHECK(banner::status(e, kKickoff).chip == Chip::none);
  CHECK(banner::status(e, kKickoff).text.empty());
  SportsEvent p = football("2", kKickoff);
  p.game->postponed = true;
  CHECK(banner::status(p, kKickoff + kHour).chip == Chip::none);
  // The plain form: no end known is live from the start.
  CHECK(banner::status(kKickoff, std::nullopt, false, kKickoff + 9 * kHour).chip == Chip::live);
}

TEST_CASE("game banner: the league line") {
  time::Zone ny = time::Zone::usEastern();
  SportsEvent e = football("1", kKickoff);
  CHECK(banner::line(e, "Sunday Night", ny, true) == "NFL \xC2\xB7 WEEK 8 \xC2\xB7 SUNDAY NIGHT \xC2\xB7 RIDGEVIEW STADIUM");
  CHECK(banner::line(e, "", ny, true) == "NFL \xC2\xB7 WEEK 8 \xC2\xB7 RIDGEVIEW STADIUM");
  // A tag that starts with the league, or says it again, doesn't repeat it.
  CHECK(banner::line(e, "NFL \xC2\xB7 Women's", ny, true) == "NFL \xC2\xB7 WEEK 8 \xC2\xB7 WOMEN'S \xC2\xB7 RIDGEVIEW STADIUM");
  CHECK(banner::line(e, "nfl", ny, true) == "NFL \xC2\xB7 WEEK 8 \xC2\xB7 RIDGEVIEW STADIUM");
  e.game->venue.reset();
  e.game->weekLabel.reset();
  CHECK(banner::line(e, "", ny, true) == "NFL");
}

TEST_CASE("game banner: tennis court, F1 lights out") {
  time::Zone ny = time::Zone::usEastern();
  SportsGame t = game("ATP", "Tennis", "m1", team("p1", "Luc Moreau"), team("p2", "Marco Ricci"), kKickoff, {},
                      std::string("Centre Court"));
  t.tennis = true;
  t.note = "Indoor Open \xC2\xB7 QF";
  SportsEvent te = eventOf(t);
  CHECK(banner::line(te, *t.note, ny, true) == "ATP \xC2\xB7 INDOOR OPEN \xC2\xB7 QF \xC2\xB7 CENTRE COURT");

  SportsTeam gp = team("r1", "Coastal GP");
  SportsGame r = game("F1", "Motorsport", "w1|s5", gp, gp, utc(2026, 10, 25, 18, 0), {}, std::string("Coastal Circuit"));
  r.racing = true;
  r.cardKey = "f1|w1|race";
  r.customTitle = "Coastal GP \xC2\xB7 Race";
  SportsEvent re = eventOf(r);
  CHECK(banner::line(re, "", ny, true) == "F1 \xC2\xB7 COASTAL CIRCUIT \xC2\xB7 LIGHTS OUT 2:00 PM");
  CHECK(banner::line(re, "", ny, false) == "F1 \xC2\xB7 COASTAL CIRCUIT \xC2\xB7 LIGHTS OUT 14:00");
  // Qualifying has no lights out; nor does another series.
  re.game->cardKey = "f1|w1|qual";
  CHECK(banner::line(re, "", ny, true) == "F1 \xC2\xB7 COASTAL CIRCUIT");
  re.game->cardKey = "nascar|w1|race";
  re.game->leagueShort = re.leagueShort = "NASCAR";
  CHECK(banner::line(re, "", ny, true) == "NASCAR \xC2\xB7 COASTAL CIRCUIT");
}

TEST_CASE("game banner: the game a guide channel is showing") {
  SportsEvent early = football("early", kKickoff - 4 * kHour, {channel("e|sn")});
  SportsEvent late = football("late", kKickoff, {channel("e|sn"), feed("f|1")});
  SportsEvent elsewhere = football("other", kKickoff, {channel("e|news")});
  board::Board b = boardOf({early, late, elsewhere});
  Seconds earlyEnd = *early.scheduledEnd();

  // On now by the clock.
  CHECK(banner::onChannel(b, {"e|sn"}, kKickoff - 3 * kHour) == early.id);
  // Past the early game's usual end, within its overrun: still that game,
  // even with the late one starting within the hour.
  CHECK(banner::onChannel(b, {"e|sn"}, earlyEnd + 5 * kMin) == early.id);
  // The overrun gone: the next game, starting soon.
  CHECK(banner::onChannel(b, {"e|sn"}, earlyEnd + 31 * kMin) == late.id);
  // Both on by the clock: the one that started last.
  SportsEvent overlap = football("overlap", kKickoff - kHour, {channel("e|sn")});
  board::Board b2 = boardOf({overlap, late});
  CHECK(banner::onChannel(b2, {"e|sn"}, kKickoff + 10 * kMin) == late.id);

  // More than an hour before: not yet; long after: none.
  board::Board one = boardOf({late});
  CHECK(banner::onChannel(one, {"e|sn"}, kKickoff - 61 * kMin).empty());
  CHECK(banner::onChannel(one, {"e|sn"}, kKickoff - 59 * kMin) == late.id);
  CHECK(banner::onChannel(one, {"e|sn"}, *late.scheduledEnd() + 2 * kHour).empty());
  // Its other coast's version counts; a provider feed is not a guide channel.
  CHECK(banner::onChannel(one, {"e|sn-west", "e|sn"}, kKickoff) == late.id);
  CHECK(banner::onChannel(one, {"f|1"}, kKickoff).empty());
  CHECK(banner::onChannel(one, {"e|news"}, kKickoff).empty());
}

TEST_CASE("game banner: studio shows, whip-arounds, finished and postponed games aren't on the channel") {
  SportsEvent studio = football("studio", kKickoff, {channel("e|sn")});
  board::Board b = boardOf({studio});
  b.studio.insert(studio.id);
  CHECK(banner::onChannel(b, {"e|sn"}, kKickoff).empty());

  SportsEvent whip = football("w", kKickoff, {channel("e|rz")});
  whip.id = "whip|NFL";
  CHECK(banner::onChannel(boardOf({whip}), {"e|rz"}, kKickoff).empty());

  SportsEvent done = football("done", kKickoff, {channel("e|sn")});
  done.game->completed = true;
  CHECK(banner::onChannel(boardOf({done}), {"e|sn"}, kKickoff + kHour).empty());
  SportsEvent off = football("off", kKickoff, {channel("e|sn")});
  off.game->postponed = true;
  CHECK(banner::onChannel(boardOf({off}), {"e|sn"}, kKickoff + kHour).empty());
  SportsEvent untimed = football("untimed", kKickoff, {channel("e|sn")});
  untimed.start.reset();
  CHECK(banner::onChannel(boardOf({untimed}), {"e|sn"}, kKickoff).empty());
}

TEST_CASE("game banner: the title starts after a doubles pair's flags (Build 141)") {
  // The player's banner: marks 60 tall from x 96, 8 apart; the title at
  // 236 for a player a side (12 after the second flag).
  const float x = 96, size = 60, between = 8, gap = 12, minX = 236;
  CHECK(banner::flagSize(1, size) == doctest::Approx(60));
  CHECK(banner::flagSize(2, size) == doctest::Approx(37));
  CHECK(banner::markWidth(0, size) == doctest::Approx(60));  // a country code or a badge
  CHECK(banner::markWidth(1, size) == doctest::Approx(60));
  CHECK(banner::markWidth(2, size) == doctest::Approx(78));  // 37 + 4 + 37
  CHECK(banner::markWidth(3, size) == doctest::Approx(78));  // never more than a pair
  // Singles: unchanged, at 236.
  float singles = banner::titleX(x, banner::markWidth(1, size), banner::markWidth(1, size), between, gap, minX);
  CHECK(singles == doctest::Approx(236));
  // Doubles: the second pair's flags end at 96 + 78 + 8 + 78 = 260 (past
  // 236, where the title used to sit over them); the title 12 after.
  float pair = banner::markWidth(2, size);
  float doubles = banner::titleX(x, pair, pair, between, gap, minX);
  CHECK(x + pair + between + pair == doctest::Approx(260));
  CHECK(doubles == doctest::Approx(272));
  CHECK(doubles - (x + pair + between + pair) == doctest::Approx(singles - (x + 60 + between + 60)));
  // A pair on one side only.
  CHECK(banner::titleX(x, banner::markWidth(1, size), pair, between, gap, minX) == doctest::Approx(254));
  // Narrower marks never pull the title left of 236.
  CHECK(banner::titleX(x, 40, 40, between, gap, minX) == doctest::Approx(236));
}
