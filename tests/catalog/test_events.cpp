// Event titles: every start-time format, provider zones, decorations,
// deduplication keys and league detection.
#include <map>

#include "catalog/helpers.hpp"
#include "core/catalog/catalog.hpp"
#include "core/catalog/text.hpp"
#include "doctest.h"

using namespace marigold;
using namespace marigold::catalog;
using testdata::utc;
using testdata::wednesdayNoon;

namespace {

ParsedEvent parse(const std::string& text, std::optional<std::string> market = std::nullopt) {
  return parseEventDetailed(text, wednesdayNoon(), market);
}

}  // namespace

TEST_CASE("day month time and a zone abbreviation or offset") {
  auto edt = parse("Team A vs Team B Fri 23 Oct 18:00 EDT (US)");
  CHECK(edt.title == "Team A vs Team B");
  CHECK(edt.start == utc(2026, 10, 23, 22, 0));
  CHECK_FALSE(edt.guessed);
  CHECK(parse("Team A vs Team B Fri 23 Oct 02:05 CEST (SE)").start == utc(2026, 10, 23, 0, 5));
  CHECK(parse("Team A vs Team B Fri 23 Oct 19:40 +03 (SA)").start == utc(2026, 10, 23, 16, 40));
  CHECK(parse("Team A vs Team B 23 Oct 19:40 AEDT").start == utc(2026, 10, 23, 8, 40));
  // An unknown zone word is left for the later formats.
  auto unknown = parse("Team A vs Team B 23 Oct 19:40 XYZ");
  CHECK(unknown.title.find("XYZ") != std::string::npos);
}

TEST_CASE("ISO and day-month-year dates in GMT") {
  auto iso = parse("Team A v Team B | 2026-10-18 | 15:30 (GMT)");
  CHECK(iso.title == "Team A v Team B");
  CHECK(iso.start == utc(2026, 10, 18, 15, 30));
  CHECK(parse("Team A v Team B 22-10-2026 | 00:00 (UTC)").start == utc(2026, 10, 22, 0, 0));
}

TEST_CASE("month day 24-hour: UK time, or the provider's zone") {
  auto uk = parse("Team A v Team B Oct 22 20:45");
  CHECK(uk.title == "Team A v Team B");
  CHECK(uk.start == utc(2026, 10, 22, 19, 45));  // BST
  CHECK(parse("Team A v Team B Oct 22 20:45", std::string("ES")).start ==
        utc(2026, 10, 22, 18, 45));
  // After the clocks go back (25 October in Europe).
  CHECK(parse("Team A v Team B Oct 27 20:45").start == utc(2026, 10, 27, 20, 45));
}

TEST_CASE("bracketed and start: timestamps are Eastern (or the provider's zone)") {
  auto paren = parse("Team A vs Team B (2026 10 21 19:30:55)");
  CHECK(paren.title == "Team A vs Team B");
  CHECK(paren.start == utc(2026, 10, 21, 23, 30));
  CHECK(parse("Team A vs Team B (2026-10-22 15:00:25)").start == utc(2026, 10, 22, 19, 0));
  CHECK(parse("Team A vs Team B (2026 10 21 19:30:55)", std::string("UK")).start ==
        utc(2026, 10, 21, 18, 30));
  auto range = parse("start:2026 10 17 20:25:00 stop:2026 10 17 23:00:00 | Team A vs Team B");
  CHECK(range.title == "Team A vs Team B");
  CHECK(range.start == utc(2026, 10, 18, 0, 25));
  // Placeholder years are not dates.
  CHECK_FALSE(parse("Slot (2099 01 01 00:00:00)").start);
}

TEST_CASE("day/month or month/day, the nearer one") {
  auto dm = parse("Team A vs Team B - 22/10 11:30");
  CHECK(dm.title == "Team A vs Team B");
  CHECK(dm.start == utc(2026, 10, 22, 15, 30));
  CHECK(parse("Team A vs Team B 10/22 11:30").start == utc(2026, 10, 22, 15, 30));
  // 05/10: 5 October is nearer than 10 May.
  CHECK(parse("Team A vs Team B 05/10 11:30").start == utc(2026, 10, 5, 15, 30));
  CHECK_FALSE(parse("Team A vs Team B 05/10 25:30").start);
}

TEST_CASE("AM/PM formats are US Eastern, whatever the provider") {
  auto dotted = parse("Team A vs Team B (10.21 7:30PM ET)");
  CHECK(dotted.title == "Team A vs Team B");
  CHECK(dotted.start == utc(2026, 10, 21, 23, 30));
  auto ordinal = parse("Team A vs Team B Wed 21st Oct 8:00PM ET");
  CHECK(ordinal.title == "Team A vs Team B");
  CHECK(ordinal.start == utc(2026, 10, 22, 0, 0));
  auto monthDay = parse("Team A vs Team B Oct 21 2:30AM ET");
  CHECK(monthDay.title == "Team A vs Team B");
  CHECK(monthDay.start == utc(2026, 10, 21, 6, 30));
  CHECK(parse("Team A vs Team B Oct 22 08:15 PM", std::string("UK")).start ==
        utc(2026, 10, 23, 0, 15));
  CHECK(parse("Team A vs Team B Nov. 2, 12:00PM").start == utc(2026, 11, 2, 17, 0));  // EST
}

TEST_CASE("European day.month in brackets and long dates are UK time") {
  CHECK(parse("Team A vs Team B [23.09 16:04]").start == utc(2026, 9, 23, 15, 4));
  auto longDate = parse("Team A vs Team B Thursday, 22 October 2026 17:45");
  CHECK(longDate.title == "Team A vs Team B");
  CHECK(longDate.start == utc(2026, 10, 22, 16, 45));
  // "29th Oct" with no ET: a UK listing (noon there, after the clocks went back).
  auto darts = parse("Players Championship 32 | Maasport   Board 1 | Thu 29th Oct 12:00PM");
  CHECK(darts.start == utc(2026, 10, 29, 12, 0));
  // Each board is a feed of the one event; a slot head and a description go.
  CHECK(darts.title == "Players Championship 32 \xc2\xb7 Maasport");
  auto described = parse("PDC event 01 : Players Championship 32 | Maasport The Players Championship continues in "
                         "Maaspoort, Den Bosch, Netherlands start:2026-10-29 12:00:00 stop:2026-10-29 19:00:00",
                         std::string("UK"));
  CHECK(described.title == darts.title);
  CHECK(described.start == darts.start);
  CHECK(parse("UFC 300: Pereira vs Hill").title.find("UFC 300") != std::string::npos);
}

TEST_CASE("a clock time alone: today in Eastern, guessed") {
  auto clock = parse("NFL: Team A at Team B 8:20pm");
  CHECK(clock.title == "NFL: Team A at Team B");
  CHECK(clock.start == utc(2026, 10, 22, 0, 20));
  CHECK(clock.guessed);
  CHECK(parse("Team A vs Team B 730PM").start == utc(2026, 10, 21, 23, 30));
  CHECK(parse("Team A vs Team B 12PM").start == utc(2026, 10, 21, 16, 0));
  CHECK(parse("Team A vs Team B 12AM").start == utc(2026, 10, 21, 4, 0));
  CHECK(parse("Team A vs Team B 8.30 PM ET").start == utc(2026, 10, 22, 0, 30));
}

TEST_CASE("a clock time with a named day near today") {
  auto named = parse("02:00PM UFC 333 Main Card (OCT 24)");
  CHECK(named.title == "UFC 333 Main Card");
  CHECK(named.start == utc(2026, 10, 24, 18, 0));
  CHECK_FALSE(named.guessed);
  // Too far from today: the day is kept in the title and the time is today's.
  auto far = parse("Team A vs Team B 7PM MAR 28");
  CHECK(far.start == utc(2026, 10, 21, 23, 0));
  CHECK(far.guessed);
  CHECK(far.title.find("MAR 28") != std::string::npos);
}

TEST_CASE("a clock time with a day that's past: a card that's over, not today's") {
  // Now is the 21st; the card was on the 14th.
  auto past = parse("02:00PM UFC 332 VOLKANOVKSI V EVLOEV (OCT 14)");
  CHECK_FALSE(past.guessed);
  CHECK(past.start == utc(2026, 10, 14, 18, 0));
  // March only within three days ("MAR" may be Spanish for Tuesday).
  CHECK(parse("Team A vs Team B 7PM MAR 28").guessed);
}

TEST_CASE("a clock time with the day before the month, and a month next to the hour") {
  auto t = parse("AFC Totton vs Torquay United // UK Tue 21 Oct // ET Tue 21 Oct 3:45pm");
  CHECK_FALSE(t.guessed);
  CHECK(t.start == utc(2026, 10, 21, 19, 45));
  // Both times, UK first: the Eastern one.
  auto both = parse("AFC Totton vs Torquay United // UK Tue 21 Oct 7:45pm // ET Tue 21 Oct 3:45pm");
  CHECK(both.title == "AFC Totton vs Torquay United");
  CHECK(both.start == utc(2026, 10, 21, 19, 45));
  CHECK_FALSE(both.guessed);
  // "8:00 pm uk": UK time (BST until the 25th, so 19:00 UTC on the 21st).
  CHECK(parse("Dundee United v Rangers 8:00 pm uk").start == utc(2026, 10, 21, 19, 0));
  // "Oct 3:45pm" is not the 3rd of October.
  auto u = parse("Team A vs Team B Oct 3:45pm");
  CHECK(u.guessed);
  CHECK(u.start == utc(2026, 10, 21, 19, 45));
}

TEST_CASE("a channel listing several events in a row: the one named on now") {
  auto now = parse("NCAA Football: BOS vs. CAR \xE2\x80\xA2 WWE NXT \xE2\x80\xA2 SHSU vs. MOST (BOS vs. CAR ) (2026 10 21 20:00:25)");
  CHECK(now.title == "BOS vs. CAR");
  CHECK(now.start == utc(2026, 10, 22, 0, 0));
  CHECK(parse("NCAA Football: WWE NXT \xE2\x80\xA2 SHSU vs. MOST (WWE NXT )").title == "WWE NXT");
  // No name in parentheses: the first.
  CHECK(parse("Basketball: Stetson vs. Florida (EXH) \xE2\x80\xA2 Charlotte vs. North Carolina").title ==
        "Stetson vs. Florida (EXH)");
}

TEST_CASE("title decorations and tidying") {
  CHECK(parse("LIVE | Team A vs Team B").title == "Team A vs Team B");
  CHECK(parse("NEXT | Team A vs Team B").title == "Team A vs Team B");
  CHECK(parse("Team A vs Team B | 8K EXCLUSIVE | US: ESPN+ PPV 951").title == "Team A vs Team B");
  CHECK(parse("LIVE EVENT 01 - Team A vs Team B").title == "Team A vs Team B");
  CHECK(parse("NFL | 06 - Team A vs Team B").title == "Team A vs Team B");
  CHECK(parse("Team A vs Team B :Paramount+  07").title == "Team A vs Team B");
  CHECK(parse("ESPN+: Team A vs Team B").title == "Team A vs Team B");
  CHECK(parse("Team A vs Team B (team b)").title == "Team A vs Team B");
  CHECK(parse("Team A vs Team B ( )").title == "Team A vs Team B");
  CHECK(parse("Team A vs Team B (Replay)").title == "Team A vs Team B (Replay)");
  CHECK(parse("Team A | Team B").title == "Team A · Team B");
  CHECK(parse("Team A vs Team B @").title == "Team A vs Team B");
  CHECK(parse("Sat, 10/24 Team A vs Team B").title == "Team A vs Team B");
  CHECK(parse("  –  Team A vs Team B  ,").title == "Team A vs Team B");
  auto pair = parseEvent("Team A vs Team B Oct 21 2:30AM ET", wednesdayNoon());
  CHECK(pair.first == "Team A vs Team B");
  CHECK(pair.second == utc(2026, 10, 21, 6, 30));
}

TEST_CASE("eventZone by provider country") {
  CHECK(eventZone(std::string("uk"))->rule() == time::Zone::Rule::EU);
  CHECK(eventZone(std::string("AU"))->rule() == time::Zone::Rule::Sydney);
  CHECK(eventZone(std::string("MX"))->standardOffset() == -6 * 3600);
  CHECK(eventZone(std::string("CA"))->rule() == time::Zone::Rule::US);
  CHECK_FALSE(eventZone(std::string("US")));
  CHECK_FALSE(eventZone(std::nullopt));
}

TEST_CASE("eventKey: same game, same half hour") {
  time::Seconds t = utc(2026, 10, 21, 23, 30);
  CHECK(eventKey("Team A vs Team B", t) == eventKey("Team A @ Team B", t));
  CHECK(eventKey("Team A vs. Team B (Spanish)", t) == eventKey("team a v team b", t));
  CHECK(eventKey("NFL: Team A at Team B", t) == eventKey("Team A at Team B", t));
  CHECK(eventKey("Team A vs Team B", t) != eventKey("Team A vs Team B", t + 1800));
  CHECK(eventKey("Team A vs Team B", t) == eventKey("Team A vs Team B", t + 600));
  CHECK(eventKey("Team A vs Team B", std::nullopt) == "teamateamb|0");
}

TEST_CASE("leagueFor: a sport named first, then words, fights before soccer") {
  CHECK(leagueFor("ncaaf #", "", "Football: Team A vs Team B") == "College Football");
  CHECK(leagueFor("grp:us| nfl", "", "Football: Team A vs Team B") == "NFL");
  CHECK(leagueFor("x", "", "Football: Team A vs Team B") == "Football");
  CHECK(leagueFor("x", "", "American Football: Team A vs Team B") == "Football");
  CHECK(leagueFor("x", "", "Basketball: Team A vs Team B") == "Basketball");
  CHECK(leagueFor("x", "WNBA", "Basketball: Team A vs Team B") == "WNBA");
  CHECK(leagueFor("x", "", "Basketball: Men's Team A vs Team B") == "College Basketball");
  // A small-college provider's "hoops:" slots, its side written "Men`s".
  CHECK(leagueFor("x", "", "hoops:  Team A vs Team B _ Men`s (A vs B)") == "College Basketball");
  CHECK(leagueFor("x", "", "hoops:  Team A vs Team B") == "Basketball");
  CHECK(leagueFor("x", "NHL", "Hockey: Team A vs Team B") == "NHL");
  CHECK(leagueFor("x", "", "Field Hockey: Team A vs Team B") == "Field Hockey");
  CHECK(leagueFor("x", "", "Volleyball:  Team A vs Team B") == "Volleyball");
  CHECK(leagueFor("x", "", "Soccer: Team A vs Team B") == "Soccer");
  CHECK(leagueFor("x", "", "Baseball: Team A vs Team B") == "Baseball");
  CHECK(leagueFor("nba #", "", "Team A vs Team B") == "NBA");
  CHECK(leagueFor("x", "MiLB", "Team A vs Team B") == "MiLB");
  CHECK(leagueFor("ppv #", "", "UFC 333: Fighter A vs Fighter B") == "Fighting");
  CHECK(leagueFor("x", "", "UFC Fight Night: Fighter A vs Fighter B") == "Fighting");
  CHECK(leagueFor("x", "", "ufc_ppv main card") == "Fighting");
  CHECK(leagueFor("x", "", "Boxing Day Special") == "Other");
  CHECK(leagueFor("x", "", "Premier League: Team A vs Team B") == "Soccer");
  CHECK(leagueFor("x", "", "Team A FC vs Team B") == "Soccer");
  CHECK(leagueFor("f1 #", "", "Race Day") == "Motorsport");
  CHECK(leagueFor("x", "", "ATP Finals") == "Tennis");
  CHECK(leagueFor("x", "", "PGA Round 1") == "Golf");
  CHECK(leagueFor("x", "", "PDC Darts") == "Darts");
  CHECK(leagueFor("x", "", "IPL Final") == "Cricket");
  CHECK(leagueFor("x", "", "Rugby Test") == "Rugby");
  CHECK(leagueFor("x", "", "Something Else") == "Other");
}


TEST_CASE("sport words count only as whole words") {
  // Words that used to hide inside others.
  CHECK(leagueFor("x", "", "Conflict Zone: The Documentary") == "Other");      // nfl
  CHECK(leagueFor("x", "", "The Influencers Live") == "Other");                // nfl
  CHECK(leagueFor("x", "", "Multiple Choice Quiz Night") == "Other");          // ipl
  CHECK(leagueFor("x", "", "A Moral Obligation") == "Other");                  // liga
  CHECK(leagueFor("x", "", "Reunited: Family Special") == "Other");            // united
  // Still found.
  CHECK(leagueFor("x", "", "NFL: Team A at Team B") == "NFL");
  CHECK(leagueFor("x", "", "Team A vs Team B (NBA)") == "NBA");
  CHECK(leagueFor("x", "", "Team A vs Harbor FC") == "Soccer");                 // "fc" at the end
  CHECK(leagueFor("x", "", "Harbor United vs Team B") == "Soccer");
  CHECK(leagueFor("x", "", "Liga MX: Team A vs Team B") == "Soccer");
  CHECK(leagueFor("x", "", "UFC 300: Main Card") == "Fighting");                // not soccer's "fc"
  CHECK(leagueFor("x", "", "Formula1 Grand Prix") == "Motorsport");             // digits don't join words
  CHECK(leagueFor("x", "", "F1: Practice 1") == "Motorsport");
  CHECK(leagueFor("x", "", "LIV Golf: Round 2") == "Golf");
  CHECK(leagueFor("x", "", "IPL: Team A vs Team B") == "Cricket");
  CHECK(leagueFor("x", "", "Men's Basketball: Team A vs Team B") == "College Basketball");
  CHECK(leagueFor("x", "", "Basketball: Team A vs. Team B (EXH) (Team A vs. Team B") == "College Basketball");
  // Pro wrestling is its own league, even under a "Wrestling:" or fight word.
  CHECK(leagueFor("x", "", "WWE NXT") == "Pro Wrestling");
  CHECK(leagueFor("x", "", "Wrestling: AEW Dynamite") == "Pro Wrestling");
  CHECK(leagueFor("x", "PPV", "Ring of Honor Wrestling") == "Pro Wrestling");
  CHECK(leagueFor("x", "", "Wrestling: Team A at Team B") == "Wrestling");
  CHECK(leagueFor("x", "", "UFC Fight Night") == "Fighting");
  CHECK(leagueFor("x", "", "KS Harbor vs. KF Valley") == "Soccer");
  CHECK(leagueFor("x", "", "Harbor FK vs. Valley") == "Soccer");
}

TEST_CASE("whole words") {
  CHECK(text::hasWord("nfl sunday", "nfl"));
  CHECK(text::hasWord("sunday nfl", "nfl"));
  CHECK(text::hasWord("(nfl)", "nfl"));
  CHECK_FALSE(text::hasWord("conflict", "nfl"));
  CHECK_FALSE(text::hasWord("nfls", "nfl"));
  CHECK(text::hasWord("nfl2024", "nfl"));
  CHECK(text::hasWord("women's", "women"));
  CHECK_FALSE(text::hasWord("womens", "women"));
  CHECK(text::hasWord("harbor fc", "fc "));     // spaces around a word are ignored
  CHECK_FALSE(text::hasWord("ufc 300", "fc "));
  CHECK_FALSE(text::hasWord("f\xc3\xbatbol", "tbol"));  // accented letters join words
  CHECK_FALSE(text::hasWord("anything", " "));
}

TEST_CASE("a darts event's boards and its UK listing are one event") {
  std::string ppv = "PPV Live Events", uk = "UK| PDC BOARD PPV";
  std::vector<Channel> list;
  for (int b = 1; b <= 5; ++b) {
    std::string n = std::to_string(b);
    list.push_back(testdata::channel(n, "PDC 0" + n + " | Players Championship 32 | Maasport   Board " + n + " | Thu 29th Oct 12:00PM", ppv));
  }
  list.push_back(testdata::channel("6", "PDC event 01 : Players Championship 32 | Maasport The Players Championship continues in Maaspoort, "
                                        "Den Bosch, Netherlands start:2026-10-29 12:00:00 stop:2026-10-29 19:00:00", uk));
  CatalogResult r = build(list, utc(2026, 10, 29, 15, 0));
  std::vector<const LiveEvent*> darts;
  for (const LiveEvent& e : r.events)
    if (e.title.find("Players Championship") != std::string::npos) darts.push_back(&e);
  REQUIRE(darts.size() == 1);
  CHECK(darts[0]->title == "Players Championship 32 \xc2\xb7 Maasport");
  CHECK(darts[0]->start == utc(2026, 10, 29, 12, 0));
  CHECK(darts[0]->feeds.size() == 6);
}

// MARK: Build 125: slot reading

TEST_CASE("a service tail naming its country sets the slot's clock, AM/PM too") {
  // 21 October: Denmark and Norway on CEST (+2), Finland on EEST (+3).
  auto dk = parse("Team A vs Team B @ Oct 21 5:55 PM :Viaplay DK  03");
  CHECK(dk.title == "Team A vs Team B");
  CHECK(dk.start == utc(2026, 10, 21, 15, 55));
  CHECK(parse("Team A vs Team B @ Oct 22 02:05 :Telia FI  13").start == utc(2026, 10, 21, 23, 5));
  CHECK(parse("Football: Team A vs Team B @ Oct 21 20:40 :Tv2Play NO  11").start == utc(2026, 10, 21, 18, 40));
  CHECK(parse("Team A vs Team B @ Oct 21 6:45 PM :Tv4 Play SE  06").start == utc(2026, 10, 21, 16, 45));
  CHECK(parse("Team A vs Team B @ Oct 22 01:00 AM :MAX ES  01").start == utc(2026, 10, 21, 23, 0));
  CHECK(parse("Team A vs Team B @ Oct 21 7:00 PM :MAX US 01").start == utc(2026, 10, 21, 23, 0));
  // VG is Norway's, with no country code; "18:15 PM" is 18:15.
  auto vg = parse("Team A - Team B @ Oct 21 18:15 PM :VG  01");
  CHECK(vg.start == utc(2026, 10, 21, 16, 15));
  CHECK(vg.title == "Team A vs Team B");
  // No country in the tail: AM/PM stays Eastern.
  CHECK(parse("NCAAM: Team A vs Team B @ Oct 21 5:00 PM :NCAAB  07").start == utc(2026, 10, 21, 21, 0));
  // A date-time after the "@", in the tail's country; an ISO time in UTC.
  CHECK(parse("Serie A: Team A-Team B @ 2026-10-21 18:15:00 :Tv2Play DK  01").start == utc(2026, 10, 21, 16, 15));
  auto iso = parse("Basket: Team A vs Team B @ 2026-10-21T17:50:00.000Z :Expressen  01");
  CHECK(iso.start == utc(2026, 10, 21, 17, 50));
  CHECK(iso.title == "Basket: Team A vs Team B");
}

TEST_CASE("UEFA slots give the kickoff in UK time, 24-hour with a pm or not (Build 136)") {
  // 21 October: the UK on BST (+1). 20:00 is the 8 PM UK kickoff, not 8 PM Eastern.
  for (const std::optional<std::string>& market : {std::optional<std::string>(), std::optional<std::string>("US"),
                                                   std::optional<std::string>("UK")}) {
    auto late = parse("UEFA | 08 - Team A vs Team B 20:00pm", market);
    CHECK(late.title == "Team A vs Team B");
    CHECK(late.start == utc(2026, 10, 21, 19, 0));
    CHECK(late.guessed);
    CHECK(parse("UEFA | 11   Team C vs Team D 20:00 pm", market).start == utc(2026, 10, 21, 19, 0));
    CHECK(parse("UEFA | 01 - Team E vs Team F 5:45pm", market).start == utc(2026, 10, 21, 16, 45));
    CHECK(parse("UEFA | 21 - Team G vs Team H 17:45 pm", market).start == utc(2026, 10, 21, 16, 45));
  }
  // Other groups' "NN | 08 -" heads keep the US habit.
  CHECK(parse("NFL | 15 - Team A at Team B 8:15pm").start == utc(2026, 10, 22, 0, 15));
}

TEST_CASE("Nordic and other European markets read their own clock") {
  CHECK(parse("Team A v Team B Oct 22 20:45", std::string("DK")).start == utc(2026, 10, 22, 18, 45));
  CHECK(parse("Team A v Team B Oct 22 20:45", std::string("FI")).start == utc(2026, 10, 22, 17, 45));
  CHECK(parse("Team A vs Team B (2026-10-21 17:30:00)", std::string("PE")).start == utc(2026, 10, 21, 22, 30));
  // US markets keep European-style listings on UK time.
  CHECK(parse("Team A v Team B Oct 22 20:45", std::string("US")).start == utc(2026, 10, 22, 19, 45));
}

TEST_CASE("a family's own country, whatever group a copy is in") {
  CHECK(familyMarket("au (stan #)") == std::optional<std::string>("AU"));
  CHECK(familyMarket("ca | cfl #") == std::optional<std::string>("CA"));
  CHECK(familyMarket("us (espn+ #)") == std::optional<std::string>("US"));
  CHECK(familyMarket("(fanatiz #)") == std::optional<std::string>("PE"));   // UTC-5
  CHECK(familyMarket("(mx) (vix #)") == std::optional<std::string>("US"));  // Eastern, not Mexico City
  CHECK(familyMarket("(mx) (dazn #)") == std::optional<std::string>("MX"));
  CHECK(familyMarket("grp:dk| viaplay ppv") == std::nullopt);
  CHECK(familyMarket("ppv event #") == std::nullopt);
  CHECK(familyMarket("ab (stan #)") == std::nullopt);  // no such market
  ChannelTraits copy;
  copy.familyKey = std::string("au (stan #)");
  copy.market = std::string("US");
  CHECK(slotMarket(copy) == std::optional<std::string>("AU"));
  ChannelTraits plain;
  plain.familyKey = std::string("grp:dk| viaplay ppv");
  plain.market = std::string("DK");
  CHECK(slotMarket(plain) == std::optional<std::string>("DK"));
  // Both copies of a Sydney listing read the same; the competition after
  // two spaces goes after the matchup, without this season.
  auto sydney = parse("Team A v Team B  Harbor Cup Matchday 4 2026/2027 (2026-10-22 06:55:50)", slotMarket(copy));
  CHECK(sydney.start == utc(2026, 10, 21, 19, 55));
  CHECK(sydney.title == "Team A v Team B \xc2\xb7 Harbor Cup Matchday 4");
}

TEST_CASE("\"all\" is no competition and says nothing about the sport") {
  auto all = parse("Next | Team Bears vs. Team Hawks | all | 21-10-2026 | 23:57 (GMT) | 8K EXCLUSIVE | BR: SOCCER PPV 14");
  CHECK(all.title == "Team Bears vs. Team Hawks");
  CHECK(all.noCompetition);
  CHECK(all.start == utc(2026, 10, 21, 23, 57));
  auto named = parse("Next | Team A - Team B | Ligue 2 | 2026-10-21 | 19:45 (GMT) | 8K EXCLUSIVE | CH: DAZN PPV 15");
  CHECK(named.title == "Team A vs Team B \xc2\xb7 Ligue 2");
  CHECK_FALSE(named.noCompetition);
  // In a soccer group, an "all" slot is read by its own title.
  std::string group = "BR| SOCCER PPV";
  std::vector<Channel> list = {
      testdata::channel("1", "Next | Team Bears vs. Team Hawks | all | 21-10-2026 | 23:57 (GMT) | 8K EXCLUSIVE | BR: SOCCER PPV 1",
                        group),
      testdata::channel("2", "Next | Harbor FC vs. Valley FC | Serie A | 21-10-2026 | 18:00 (GMT) | 8K EXCLUSIVE | BR: SOCCER PPV 2",
                        group),
      testdata::channel("3", "Next | Alpha Player vs. Bravo Player | all | 21-10-2026 | 15:00 (GMT) | 8K EXCLUSIVE | BR: SOCCER PPV 3",
                        group),
  };
  CatalogResult r = build(list, wednesdayNoon());
  std::map<std::string, std::string> leagues;
  for (const LiveEvent& e : r.events) leagues[e.title] = e.league;
  CHECK(leagues["Team Bears vs. Team Hawks"] == "Other");
  CHECK(leagues["Alpha Player vs. Bravo Player"] == "Other");
  CHECK(leagues["Harbor FC vs. Valley FC \xc2\xb7 Serie A"] == "Soccer");
}

TEST_CASE("a dash or an x between two sides is a match; stages and dates aren't sides") {
  CHECK(parse("LIVE | ALPHA PLAYER - BRAVO PLAYER | Wed 21 Oct 15:58 CEST (FR) | 8K EXCLUSIVE | FR: MAX PPV 25").title ==
        "ALPHA PLAYER vs BRAVO PLAYER");
  CHECK(parse("NEXT | HARBOR - VALLEY FF | Wed 21 Oct 18:50 CEST (NO) | 8K EXCLUSIVE | NO: VGTV PPV 3").title ==
        "HARBOR vs VALLEY FF");
  CHECK(parse("Next | Harbor x Valley | Serie A | 2026-10-21 | 17:00 (GMT)").title == "Harbor vs Valley \xc2\xb7 Serie A");
  CHECK(parse("Paris Masters - Court Central").title == "Paris Masters - Court Central");
  CHECK(parse("Harbor Open - Round 2").title == "Harbor Open - Round 2");
  CHECK(parse("Golf: The Masters - Round 2").title == "Golf: The Masters - Round 2");
  CHECK(parse("NEXT | MON, 10/21 - HARBOR FC | Wed 21 Oct 17:00 EDT (US)").title.find("vs") == std::string::npos);
  CHECK(parse("Harbor vs Valley 4:45am Sun").title == "Harbor vs Valley Sun");
  // A league's name, then two clubs joined by a hyphen.
  CHECK(parse("3F Superliga: Harbor-F.C. Valley @ 2026-10-21 18:30:00 :Tv2Play DK  02").title ==
        "3F Superliga: Harbor vs F.C. Valley");
  CHECK(parse("Tennis: Paris-Bercy").title == "Tennis: Paris-Bercy");
}

TEST_CASE("Surname, First is a player; a doubles pair stays as written") {
  auto singles = parse("Alpha, Anna vs Van De Bravo, Bea @ Oct 21 3:40 PM - WTA Harbor :Tennis  06");
  CHECK(singles.title == "Anna Alpha vs Bea Van De Bravo \xc2\xb7 WTA Harbor");
  CHECK(parse("Alpha A, Bravo B vs Charlie C, Delta D").title == "Alpha A, Bravo B vs Charlie C, Delta D");
}

TEST_CASE("a competition glued to the matchup goes after it") {
  auto lead = parse("NEXT | LIGUE 2 HARBOR - VALLEY | Wed 21 Oct 20:40 CEST (DK) | 8K EXCLUSIVE | DK: VIAPLAY PPV 9");
  CHECK(lead.title == "HARBOR vs VALLEY \xc2\xb7 LIGUE 2");
  CHECK(lead.start == utc(2026, 10, 21, 18, 40));
  CHECK(parse("NEXT | H\xc3\x84RBOR - VALLEY ALLSVENSKAN | Wed 21 Oct 18:45 CEST (SE)").title ==
        "H\xc3\x84RBOR vs VALLEY \xc2\xb7 ALLSVENSKAN");
  CHECK(parse("NEXT | DFB-POKAL FRAUEN HARBOR - VALLEY | Wed 21 Oct 17:55 CEST (NL)").title ==
        "HARBOR vs VALLEY \xc2\xb7 DFB-POKAL FRAUEN");
  // A women's side marked "(F)" says so in words.
  CHECK(parse("Harbor (F) vs. Valley (F) (2026-10-21 17:40:00)").title == "Harbor Women vs. Valley Women");
  CHECK(parse("Harbor (W) - Valley (W) | Cup").title == "Harbor Women vs Valley Women \xc2\xb7 Cup");
  // Two spaces before a country and a time: no competition.
  CHECK(parse("Team A vs Team B  UK 2:00pm").title.find("\xc2\xb7") == std::string::npos);
}
