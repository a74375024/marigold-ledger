// Build 143: team names, guide titles and clocks matched more often. Clubs
// without their initials, names run together, other countries' names and
// nicknames, schools' spellings; two schools' games an hour apart; European
// guide titles, descriptions and shows; brand names as providers write them.
#include <algorithm>

#include "core/catalog/catalog.hpp"
#include "core/sports/brands.hpp"
#include "core/sports/matcher.hpp"
#include "core/sports/store.hpp"
#include "doctest.h"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::sports;
using namespace marigold::sports::matcher;
using sportsdata::game;
using sportsdata::team;
using sportsdata::utc;

namespace {

ProviderEvent provider(const std::string& id, const std::string& title, const std::string& league,
                       std::optional<Seconds> start, std::vector<std::string> feeds, const std::string& family = "fam") {
  ProviderEvent e;
  e.id = id;
  e.title = title;
  e.league = league;
  e.start = start;
  e.feeds = std::move(feeds);
  e.family = family;
  e.lang = "en";
  return e;
}

const SportsEvent* findGame(const std::vector<SportsEvent>& events, const std::string& gameID) {
  for (const SportsEvent& e : events) {
    if (e.game && e.game->id == gameID) return &e;
  }
  return nullptr;
}

bool hasFeed(const SportsEvent* e, const std::string& feed) {
  return e && std::any_of(e->sources.begin(), e->sources.end(),
                          [&](const EventSource& s) { return s.channelID == feed; });
}

bool has(const std::vector<std::string>& list, const std::string& v) {
  return std::find(list.begin(), list.end(), v) != list.end();
}

const auto all = [](int) { return true; };

std::vector<std::string> weak(const SportsTeam& t, bool college) {
  return weakerVariants(t, college, teamVariants(t, college));
}

}  // namespace

TEST_CASE("clubs without their initials or year, when no other team of the sport is called that") {
  Seconds at = utc(2026, 11, 5, 20, 0);
  std::vector<SportsGame> games = {
      game("UECL", "Soccer", "1", team("1", "Harbor FC"), team("2", "Valley Rovers"), at),
      game("UECL", "Soccer", "2", team("3", "F.C. Riverton"), team("4", "Lakeside 1999"), at),
      game("MLS", "Soccer", "3", team("5", "Harbor City SC"), team("6", "Portside Gulls FC"), at),
      // Another club called "Northgate" keeps "Northgate FC" from being
      // "Northgate".
      game("EPL", "Soccer", "4", team("7", "Northgate FC"), team("8", "Northgate"), at),
  };
  TeamIndex index(games);
  CHECK(index.matches("Harbor v Valley Rovers", all) == std::vector<int>{0});
  CHECK(index.matches("Riverton vs Lakeside", all) == std::vector<int>{1});
  CHECK(index.matches("FC Riverton - Lakeside 1999", all) == std::vector<int>{1});
  CHECK(index.matches("Harbor City vs Portside Gulls", all) == std::vector<int>{2});
  // A nickname of the club without its initials ("Thorns" of Portland
  // Thorns FC).
  CHECK(index.matches("Harbor City vs Gulls", all) == std::vector<int>{2});
  // Two clubs sharing the shorter name: the full names only.
  CHECK(index.matches("Northgate FC vs Northgate", all) == std::vector<int>{3});
  std::vector<std::array<std::vector<std::string>, 2>> names = sideNames(games);
  CHECK(has(names[0][0], "harbor"));
  CHECK(has(names[1][0], "riverton"));
  CHECK(has(names[1][1], "lakeside"));
  CHECK_FALSE(has(names[3][0], "northgate"));  // Northgate's own name, not Northgate FC's
  // Too little left: never two letters.
  CHECK(weakerVariants(team("9", "AZ FC"), false, {"az fc"}).empty());
  CHECK(weakerVariants(team("9", "City FC"), false, {"city fc"}).empty());
  // Not half a place's name ("Diego" of San Diego FC).
  CHECK(weak(team("10", "San Harbor FC"), false) == std::vector<std::string>{"san harbor"});
}

TEST_CASE("names run together, initials with stops, Saint and St., Peter's") {
  SportsTeam ararat = team("1", "Lake-Shore");
  CHECK(has(teamVariants(ararat, false), "lakeshore"));
  SportsTeam nec = team("2", "NEC Harborton", std::string("NEC"));
  nec.alternates = {"NEC"};
  CHECK(has(weak(nec, false), "n e c"));
  // Only for initials written in capitals.
  SportsTeam owls = team("3", "Owls Town");
  owls.alternates = {"Owls"};
  CHECK_FALSE(has(weak(owls, false), "o w l s"));
  SportsTeam mount = team("4", "Mount St Harbor Knights", std::nullopt, std::string("Mount St Harbor"));
  CHECK(has(weak(mount, true), "mount saint harbor"));
  SportsTeam saint = team("5", "Saint Valley's Peacocks", std::nullopt, std::string("Saint Valley's"));
  std::vector<std::string> v = weak(saint, true);
  CHECK(has(v, "saint valleys"));
  CHECK(has(v, "st valley s"));
  // A last "St" is a state school's: not "Saint".
  SportsTeam state = team("6", "Harbor St");
  CHECK_FALSE(has(weak(state, true), "harbor saint"));

  Seconds at = utc(2026, 11, 5, 20, 0);
  std::vector<SportsGame> games = {
      game("UEL", "Soccer", "1", nec, team("7", "Valley Rovers"), at),
      game("UECL", "Soccer", "2", ararat, team("8", "Brannton"), at),
  };
  TeamIndex index(games);
  CHECK(index.matches("N.E.C. vs Valley Rovers", all) == std::vector<int>{0});
  CHECK(index.matches("Brannton v LakeShore", all) == std::vector<int>{1});
  CHECK(index.matches("D.C. vs. Lake_Shore", all).empty());
}

TEST_CASE("other countries' names and Liga MX nicknames, both ways") {
  Seconds at = utc(2026, 11, 5, 20, 0);
  std::vector<SportsGame> games = {
      game("UECL", "Soccer", "1", team("1", "Red Star Belgrade"), team("2", "Inter D'Escaldes"), at),
      game("UECL", "Soccer", "2", team("3", "F.C. K\xC3\xB8" "benhavn"), team("4", "Iberia 1999"), at),
      game("BUN", "Soccer", "3", team("5", "FC Cologne"), team("6", "Bayer Leverkusen"), at),
      game("SA", "Soccer", "4", team("7", "Internazionale"), team("8", "Como"), at),
      game("LMX", "Soccer", "5", team("9", "Guadalajara"), team("10", "Tijuana"), at),
      game("LMX", "Soccer", "6", team("11", "Monterrey"), team("12", "Toluca"), at),
      game("LMX", "Soccer", "7", team("13", "Pachuca"), team("14", "Atlas"), at),
      game("LMX", "Soccer", "8", team("15", "Quer\xC3\xA9taro"), team("16", "Puebla"), at),
      game("MLS", "Soccer", "9", team("17", "Sporting Kansas City"), team("18", "San Diego FC"), at),
      game("L1", "Soccer", "10", team("19", "Brest"), team("20", "Lyon"), at),
  };
  TeamIndex index(games);
  CHECK(index.matches("Crvena zvezda v Inter Club d`Escaldes", all) == std::vector<int>{0});
  CHECK(index.matches("R\xC3\xB6" "da Stj\xC3\xA4rnan - Inter d'Escaldes", all) == std::vector<int>{0});
  CHECK(index.matches("FC Copenhagen vs Iberia Tbilisi", all) == std::vector<int>{1});
  CHECK(index.matches("FC K\xC3\xB6penhamn - Iberia 1999", all) == std::vector<int>{1});
  CHECK(index.matches("K\xC3\xB8" "benhavn v Iberia 1999", all) == std::vector<int>{1});
  CHECK(index.matches("1. FC K\xC3\xB6ln - Bayer 04 Leverkusen", all) == std::vector<int>{2});
  CHECK(index.matches("Inter - Como", all) == std::vector<int>{3});
  CHECK(index.matches("Chivas vs. Xolos", all) == std::vector<int>{4});
  CHECK(index.matches("Rayados vs. Diablos Rojos", all) == std::vector<int>{5});
  CHECK(index.matches("Tuzos vs. Rojinegros", all) == std::vector<int>{6});
  CHECK(index.matches("Gallos Blancos vs. Puebla", all) == std::vector<int>{7});
  CHECK(index.matches("Sporting KC vs. San Diego FC", all) == std::vector<int>{8});
  CHECK(index.matches("Stade Brestois 29 - Olympique Lyonnais", all) == std::vector<int>{9});
  // Not another pairing of the same names.
  CHECK(index.matches("Chivas vs. Rayados", all).empty());
}

TEST_CASE("schools: the guide's spellings both ways, a bracket or University left out when no other school has the name") {
  Seconds at = utc(2026, 11, 6, 0, 0);
  SportsTeam oleMiss = team("1", "Ole Miss Rebels", std::string("MISS"), std::string("Ole Miss"));
  oleMiss.alternates = {"Ole Miss", "Rebels"};
  SportsTeam utsa = team("2", "UTSA Roadrunners", std::string("UTSA"), std::string("UTSA"));
  SportsTeam southern = team("3", "Southern Miss Golden Eagles", std::string("USM"), std::string("Southern Miss"));
  SportsTeam miamiOH = team("4", "Miami (OH) RedHawks", std::string("M-OH"), std::string("Miami (OH)"));
  SportsTeam miami = team("5", "Miami Hurricanes", std::string("MIA"), std::string("Miami"));
  SportsTeam grace = team("6", "Grace University Lancers", std::string("GRCU"), std::string("Grace University"));
  SportsTeam centenary = team("7", "Centenary (LA) Gentlemen", std::string("CTLA"), std::string("Centenary (LA)"));
  SportsTeam unionTN = team("8", "Union (TN) Bulldogs", std::string("UNTN"), std::string("Union (TN)"));
  SportsTeam unionNY = team("9", "Union (NY) Dutchmen", std::string("UNNY"), std::string("Union (NY)"));
  std::vector<SportsGame> games = {
      game("NCAAM", "College Basketball", "1", oleMiss,
           team("20", "Harbor State Owls", std::nullopt, std::string("Harbor State")), at),
      game("NCAAM", "College Basketball", "2", utsa, southern, at),
      game("NCAAM", "College Basketball", "3", miamiOH,
           team("21", "Lakeside Hawks", std::nullopt, std::string("Lakeside")), at),
      game("NCAAM", "College Basketball", "4", miami, team("22", "Valley Otters"), at),
      game("NCAAM", "College Basketball", "5", grace, centenary, at),
      game("NCAAM", "College Basketball", "6", unionTN, team("23", "River Bears", std::nullopt, std::string("River")),
           at),
      game("NCAAM", "College Basketball", "7", unionNY, team("24", "Bay Gulls"), at),
  };
  TeamIndex index(games);
  CHECK(index.matches("Mississippi vs. Harbor State", all) == std::vector<int>{0});
  CHECK(index.matches("Texas-San Antonio vs. Southern Mississippi", all) == std::vector<int>{1});
  CHECK(index.matches("Miami (Ohio) vs. Lakeside", all) == std::vector<int>{2});
  CHECK(index.matches("Grace vs. Centenary", all) == std::vector<int>{4});
  // "Miami" is the Hurricanes' own name; "Union" is two schools'.
  CHECK(index.matches("Miami vs. Lakeside", all).empty());
  CHECK(index.matches("Union vs. River", all).empty());
  CHECK(index.matches("Union (TN) vs. River", all) == std::vector<int>{5});
}

TEST_CASE("two schools' games an hour apart in two sports: the one at the feed's time") {
  Seconds six = utc(2026, 11, 5, 23, 0);  // 6 PM Eastern
  SportsTeam iowa = team("1", "Harbor State Cyclones", std::string("HSU"), std::string("Harbor State"));
  iowa.alternates = {"Harbor State"};
  SportsTeam ucf = team("2", "Lakeside Knights", std::string("LKU"), std::string("Lakeside"));
  ucf.alternates = {"Lakeside"};
  BuildInput in;
  in.now = six - 3600;
  in.zone = time::Zone::usEastern();
  in.games = {game("NCAAWV", "Volleyball", "v1", ucf, iowa, six),
              game("NCAAFH", "Field Hockey", "f1", iowa, ucf, six + 3600)};
  in.events = {provider("e6", "Harbor State vs. Lakeside", "Other", six + 60, {"e|6"}, "espn+ #"),
               provider("e7", "Harbor State vs. Lakeside", "Other", six + 3600 + 60, {"e|7"}, "espn+ #"),
               // Halfway between: still ambiguous.
               provider("e8", "Harbor State vs. Lakeside", "Other", six + 1800, {"e|8"}, "espn+ #")};
  auto out = build(in);
  REQUIRE(out);
  const SportsEvent* volleyball = findGame(out->events, "NCAAWV|v1");
  const SportsEvent* hockey = findGame(out->events, "NCAAFH|f1");
  CHECK(hasFeed(volleyball, "e|6"));
  CHECK_FALSE(hasFeed(volleyball, "e|7"));
  CHECK(hasFeed(hockey, "e|7"));
  CHECK_FALSE(hasFeed(hockey, "e|6"));
  CHECK_FALSE(hasFeed(volleyball, "e|8"));
  CHECK_FALSE(hasFeed(hockey, "e|8"));

  // At the same time: the game whose away side the title names first.
  in.games[1].start = six;
  in.events = {provider("t1", "Harbor State vs. Lakeside", "Other", six, {"e|t1"}, "espn+ #"),
               provider("t2", "Lakeside vs. Harbor State", "Other", six, {"e|t2"}, "espn+ #")};
  out = build(in);
  REQUIRE(out);
  CHECK(hasFeed(findGame(out->events, "NCAAWV|v1"), "e|t1"));  // Harbor State away at Lakeside
  CHECK(hasFeed(findGame(out->events, "NCAAFH|f1"), "e|t2"));
  // Elsewhere the order says nothing: still ambiguous.
  in.events = {provider("t3", "Harbor State vs. Lakeside", "Other", six, {"e|t3"}, "other #")};
  out = build(in);
  REQUIRE(out);
  CHECK_FALSE(hasFeed(findGame(out->events, "NCAAWV|v1"), "e|t3"));
  CHECK_FALSE(hasFeed(findGame(out->events, "NCAAFH|f1"), "e|t3"));
}

TEST_CASE("feeds' and guides' sports with any apostrophe") {
  CHECK(catalog::leagueFor("", "", "NCAA Men`s Basketball: Harbor vs. Valley") == "College Basketball");
  CHECK(catalog::leagueFor("", "", "NCAA Women\xE2\x80\x99s Basketball: Harbor vs. Valley") == "College Basketball");
  CHECK(catalog::leagueFor("", "", "NCAA Women's Basketball: Harbor vs. Valley") == "College Basketball");
  CHECK(catalog::leagueFor("", "", "Harbor vs. Valley") == "Other");
}

TEST_CASE("European guide titles: a dash or an x between two teams of a game") {
  CHECK(pairedTitle("Napoli - Lazio"));
  CHECK(pairedTitle("AEK Atene - Real Madrid"));
  CHECK(pairedTitle("Omonia FC x SL Benfica"));
  CHECK(pairedTitle("Harbor \xE2\x80\x93 Valley"));
  CHECK_FALSE(pairedTitle("Napoli"));
  CHECK_FALSE(pairedTitle("Show - Episode - Part 2"));
  CHECK_FALSE(pairedTitle("2026 - 27"));
  CHECK_FALSE(pairedTitle("A very long programme name with many words - and more words after it here"));
  CHECK_FALSE(looksLikeGame("Napoli - Lazio"));  // looksLikeGame stays as it was

  Seconds at = utc(2026, 11, 5, 20, 0);
  BuildInput in;
  in.now = at - 3600;
  in.zone = time::Zone::usEastern();
  in.games = {game("SA", "Soccer", "1", team("1", "Harbor"), team("2", "Valley"), at, {}, std::nullopt, true)};
  in.events = {provider("p1", "Harbor - Valley", "", at, {"x|1"}),
               provider("p2", "Morning Show - Highlights Hour", "", at, {"x|2"})};
  GuideListing listing;
  listing.entryID = "e|sky";
  listing.title = "Harbor x Valley";
  listing.start = at - 300;
  listing.end = at + 2 * 3600;
  in.listings = {listing};
  auto out = build(in);
  REQUIRE(out);
  const SportsEvent* g = findGame(out->events, "SA|1");
  CHECK(hasFeed(g, "x|1"));
  CHECK(hasFeed(g, "e|sky"));
  // The show stays a plain event of its own, not a game's.
  CHECK(std::any_of(out->events.begin(), out->events.end(), [](const SportsEvent& e) { return e.id == "p2"; }));
}

TEST_CASE("competitions are generic sports titles; venues in German, Italian, French and Spanish") {
  for (const char* t : {"Premier League", "LALIGA EA SPORTS", "Bundesliga", "Serie A", "UEFA Europa League",
                        "Liga MX", "Fu\xC3\x9F" "ball: 2. Bundesliga", "Calcio", "EFL Championship", "Volleyball",
                        "NCAA Softball"}) {
    CHECK_MESSAGE(isGenericSportsTitle(t), t);
  }
  CHECK_FALSE(isGenericSportsTitle("Serie Animata"));
  CHECK_FALSE(isGenericSportsTitle("Evening News"));
  auto de = venueOf("Aus der Harbor Arena in Hafenstadt. Das Topspiel des Spieltags.");
  REQUIRE(de);
  CHECK(de->first == "harbor arena");
  CHECK(de->second == "hafenstadt");
  auto it = venueOf("Dallo Stadio del Porto di Portovecchio, la sfida della giornata.");
  REQUIRE(it);
  CHECK(it->first == "stadio del porto");
  CHECK(it->second == "portovecchio");
  auto fr = venueOf("Depuis le Parc du Port \xC3\xA0 Villeport.");
  REQUIRE(fr);
  CHECK(fr->first == "parc du port");
  auto es = venueOf("Desde el Estadio del Puerto en Puertoviejo.");
  REQUIRE(es);
  CHECK(es->first == "estadio del puerto");
  CHECK(es->second == "puertoviejo");
  CHECK_FALSE(venueOf("Dalla parte dei tifosi."));
}

TEST_CASE("shows around a game in other languages are left out") {
  for (const char* t : {"Previa: Harbor vs Valley", "Resumen Harbor vs Valley", "Repetici\xC3\xB3n: Harbor - Valley",
                        "Bundesliga Zusammenfassung", "Sintesi Harbor - Valley", "R\xC3\xA9sum\xC3\xA9 Harbor - Valley"}) {
    CHECK_MESSAGE(excluded(normalized(t)), t);
  }
  CHECK_FALSE(excluded(normalized("Harbor vs Valley")));
}

TEST_CASE("tennis cities: Hongkong") {
  CHECK(namesTennisEvent("WTA: Kvartfinaler, Hongkong") == namesTennisEvent("WTA: Quarterfinals, Hong Kong"));
}

TEST_CASE("brand names as providers write them, with their language") {
  struct Case {
    const char* name;
    const char* lang;
  };
  for (const Case& c : {Case{"CA: SPORTSN ET ONE", "en"}, Case{"VIP: EURO SPORT 1", ""}, Case{"NOW: TNT SPORT 2", ""},
                        Case{"FR: LIGUE1+ 3", "fr"}, Case{"VIP: LIGUE 1+ 7", "fr"}, Case{"VIP: CANAL+ LIVE 4", "fr"},
                        Case{"FR: CANAL+ PREMIER LEAGUE", "fr"}, Case{"VIP: RMC SPORT 1", "fr"},
                        Case{"IT: SKY CALCIO 2", "it"}, Case{"ES: M. LIGA DE CAMPEONES 3", "es"},
                        Case{"M+: M. LALIGA 4", "es"}, Case{"VO: M.LIGA CAMPEONES 2", "es"},
                        Case{"UK: LALIGATV HYPERMOTION", ""}, Case{"VO: FOX SPORTS 2 MX", "es"},
                        Case{"VO: FOX SPORTS PREMIUM ARG", "es"}, Case{"SE: VIAPLAY SPORT", ""},
                        Case{"NL: ZIGGO SPORT 3", "nl"}, Case{"NL: ESPN 2", ""}, Case{"CH: BLUE SPORT 1", ""},
                        Case{"CH: MYSPORTS 1", ""}, Case{"DE: SPORT1 HD", ""}, Case{"DE: SPORTDIGITAL FUSSBALL", "de"},
                        Case{"PL: ELEVEN SPORTS 1", ""}, Case{"PL: TVP SPORT", "pl"},
                        Case{"PL: POLSAT SPORT PREMIUM 1", "pl"}}) {
    const brands::Brand* b = brands::find(c.name);
    REQUIRE_MESSAGE(b, c.name);
    CHECK_MESSAGE(std::string(b->lang) == c.lang, c.name);
  }
  // The words, not parts of others.
  CHECK_FALSE(brands::find("UK: TNT SPORTSWEAR CHANNEL"));
  CHECK_FALSE(brands::find("US: SPORTSMAN CHANNEL"));
  CHECK(std::string(brands::find("UK: TNT SPORTS 1")->words) == " tnt sports ");
}

TEST_CASE("a language tag doesn't set a feed's clock; its country does") {
  // "(Apple) (MLS) 013 | Montréal vs. Vancouver (French) (... 14:25:46)": a
  // US feed in French, its time US Eastern like its English copy (read on
  // Paris time it was six hours early).
  Seconds now = utc(2026, 11, 7, 16, 0);
  catalog::ChannelTraits french;
  french.country = std::string("US");
  french.market = std::string("FR");
  french.familyKey = std::string("(apple) (mls) #");
  french.familyNumber = 13;
  french.kind = catalog::ChannelKind::event;
  french.eventText = std::string("Harbor vs. Valley (French) (2026-11-07 14:25:46)");
  CHECK(catalog::slotMarket(french) == std::optional<std::string>("US"));
  catalog::ChannelTraits english = french;
  english.market = std::string("US");
  english.familyNumber = 12;
  english.eventText = std::string("Harbor vs. Valley (2026-11-07 14:25:41)");
  Channel a, b;
  a.id = "m3u:x|12";
  a.name = "(Apple) (MLS) 012 | Harbor vs. Valley (2026-11-07 14:25:41)";
  b.id = "m3u:x|13";
  b.name = "(Apple) (MLS) 013 | Harbor vs. Valley (French) (2026-11-07 14:25:46)";
  std::unordered_map<std::string, catalog::ChannelTraits> traits = {{a.id, english}, {b.id, french}};
  catalog::EventsResult out = catalog::buildEvents({a, b}, traits, now);
  size_t feeds = 0;
  for (const catalog::LiveEvent& e : out.events) {
    REQUIRE(e.start);
    CHECK(*e.start >= utc(2026, 11, 7, 19, 25));  // 14:25 Eastern
    CHECK(*e.start < utc(2026, 11, 7, 19, 26));
    feeds += e.feeds.size();
  }
  CHECK(feeds == 2);
  // A channel of another country still reads its own clock.
  catalog::ChannelTraits danish;
  danish.country = std::string("DK");
  danish.market = std::string("DK");
  CHECK(catalog::slotMarket(danish) == std::optional<std::string>("DK"));
}

TEST_CASE("a guide's dashed title on a sports channel is read as a possible game") {
  store::ListingChannel sports{"e|sky", false, "it", 0};
  sports.sportsChannel = true;
  store::ListingChannel news{"e|news", false, "en", 0};
  Seconds s = utc(2026, 11, 5, 20, 0), e = s + 2 * 3600;
  CHECK(store::listingFor(sports, "Harbor - Valley", std::nullopt, guide::Text(), false, s, e));
  CHECK(store::listingFor(news, "Harbor - Valley", std::nullopt, guide::Text(), true, s, e));  // a sports slot
  CHECK_FALSE(store::listingFor(news, "Harbor - Valley", std::nullopt, guide::Text(), false, s, e));
  CHECK_FALSE(store::listingFor(sports, "Morning - Part One - Repeat", std::nullopt, guide::Text(), false, s, e));
}
