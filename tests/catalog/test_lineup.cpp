// My Lineup: prefs (with tolerant JSON), market city parts and places, and
// the suggestion rules.
#include "core/catalog/lineup.hpp"
#include "doctest.h"

using namespace marigold;
using namespace marigold::catalog;

namespace {

EntryInfo entry(LineupCategory category, std::optional<std::string> market = std::string("US")) {
  EntryInfo e;
  e.id = "e|x";
  e.name = "Dream Channel";
  e.category = category;
  e.market = std::move(market);
  return e;
}

LineupPrefs bayArea() {
  LineupPrefs p;
  p.configured = true;
  p.markets = {
      {"San Francisco–Oakland–San Jose", std::string("Template:Bay Area TV"), {"KAAA", "KBBB"}}};
  return p;
}

}  // namespace

TEST_CASE("market city parts") {
  MarketChoice m{"San Francisco–Oakland–San Jose", std::nullopt, {}};
  CHECK(m.cityParts() == std::vector<std::string>{"san francisco", "oakland", "san jose"});
  MarketChoice dashes{"Fort Myers-Naples/Testville (Coast) & Bay", std::nullopt, {}};
  CHECK(dashes.cityParts() ==
        std::vector<std::string>{"fort myers", "naples", "testville", "coast"});
  MarketChoice saint{"Sprint St Louis, Abc", std::nullopt, {}};
  CHECK(saint.cityParts() == std::vector<std::string>{"sprint  louis"});
}

TEST_CASE("prefs helpers") {
  LineupPrefs p = bayArea();
  p.countries = {"US", "US-ES", "MX"};
  CHECK(p.guideCountries() == std::set<std::string>{"MX", "US"});
  CHECK(p.includes(std::string("US-ES")));
  CHECK_FALSE(p.includes(std::string("UK")));
  CHECK_FALSE(p.includes(std::nullopt));
  CHECK(p.marketCallsigns() == std::set<std::string>{"KAAA", "KBBB"});
  std::set<std::string> places = p.marketPlaces();
  CHECK(places.count("bay area"));
  CHECK(places.count("norcal"));
  CHECK(places.count("oakland"));
  CHECK_FALSE(places.count("socal"));
}

TEST_CASE("prefs JSON round trip and tolerance") {
  LineupPrefs p = bayArea();
  p.categories = {LineupCategory::local, LineupCategory::free};
  p.added = {"e|a"};
  p.removed = {"e|b"};
  p.suggested = {"e|c"};
  p.builtAt = 1790000000;
  p.homeMarket = "San Francisco–Oakland–San Jose";
  p.requireGuide = false;
  auto back = lineupPrefsFromJson(toJson(p));
  REQUIRE(back);
  CHECK(*back == p);
  // Older prefs: missing fields keep their defaults.
  auto old = lineupPrefsFromJson("{\"configured\": true}");
  REQUIRE(old);
  CHECK(old->configured);
  CHECK(old->categories == defaultOnCategories());
  CHECK(old->countries == std::set<std::string>{"US"});
  CHECK(old->requireGuide);
  // A field no longer used ("showHidden", saved until Build 112) is ignored.
  auto hidden = lineupPrefsFromJson("{\"configured\": true, \"showHidden\": true, \"added\": [\"e|a\"]}");
  REQUIRE(hidden);
  CHECK(hidden->added == std::set<std::string>{"e|a"});
  CHECK(toJson(*hidden).find("showHidden") == std::string::npos);
  // A wrong type or an unknown category fails the decode, like Swift's.
  CHECK_FALSE(lineupPrefsFromJson("{\"configured\": \"yes\"}"));
  CHECK_FALSE(lineupPrefsFromJson("{\"categories\": [\"local\", \"bogus\"]}"));
  CHECK_FALSE(lineupPrefsFromJson("not json"));
}

TEST_CASE("suggests: countries and kinds") {
  LineupPrefs p = bayArea();
  EntryInfo mx = entry(LineupCategory::international, std::string("MX"));
  CHECK_FALSE(suggests(mx, p, true, mx.name));
  p.countries.insert("MX");
  CHECK(suggests(mx, p, true, mx.name));
  CHECK_FALSE(suggests(mx, p, false, mx.name));
  p.requireGuide = false;
  CHECK(suggests(mx, p, false, mx.name));
  p.requireGuide = true;

  EntryInfo fast = entry(LineupCategory::free);
  fast.kind = ChannelKind::fast;
  CHECK_FALSE(suggests(fast, p, true, fast.name));  // free is opt-in
  p.categories.insert(LineupCategory::free);
  CHECK(suggests(fast, p, true, fast.name));
  CHECK_FALSE(suggests(fast, p, false, fast.name));
  EntryInfo radio = entry(LineupCategory::radio);
  radio.kind = ChannelKind::radio;
  p.categories.insert(LineupCategory::radio);
  CHECK(suggests(radio, p, true, radio.name));
  p.requireGuide = false;
  CHECK_FALSE(suggests(radio, p, false, radio.name));  // radio always needs a guide
  p.requireGuide = true;
  EntryInfo event = entry(LineupCategory::sports);
  event.kind = ChannelKind::event;
  CHECK_FALSE(suggests(event, p, true, event.name));
}

TEST_CASE("suggests: core networks, teams, locals") {
  LineupPrefs p = bayArea();
  EntryInfo cnn = entry(LineupCategory::news);
  cnn.core = "CNN";
  CHECK(suggests(cnn, p, false, "CNN"));  // always, without a guide
  p.categories.erase(LineupCategory::news);
  CHECK_FALSE(suggests(cnn, p, true, "CNN"));
  p = bayArea();

  EntryInfo team = entry(LineupCategory::sports);
  team.scope = "team";
  CHECK_FALSE(suggests(team, p, true, team.name));

  EntryInfo local = entry(LineupCategory::local);
  local.scope = "local";
  local.callsign = "KAAA";
  CHECK(suggests(local, p, false, "FOX 2"));
  local.callsign = "KBBB-DT";
  CHECK(suggests(local, p, false, "FOX 2"));
  local.callsign = "KZZZ";
  CHECK_FALSE(suggests(local, p, true, "FOX 2"));
  local.callsign.reset();
  CHECK_FALSE(suggests(local, p, true, "FOX 2"));
  p.categories.erase(LineupCategory::local);
  local.callsign = "KAAA";
  CHECK_FALSE(suggests(local, p, true, "FOX 2"));

  // US Spanish stations: only the market's.
  p = bayArea();
  p.countries.insert("US-ES");
  EntryInfo es = entry(LineupCategory::spanish, std::string("US-ES"));
  es.callsign = "KAAA";
  CHECK(suggests(es, p, false, "Telemundo 48"));
  es.callsign = "KZZZ";
  CHECK_FALSE(suggests(es, p, true, "Telemundo 48"));
  es.callsign.reset();
  CHECK(suggests(es, p, true, "Telemundo Pacifico"));
}

TEST_CASE("suggests: regional channels only in their market") {
  LineupPrefs p = bayArea();
  EntryInfo rsn = entry(LineupCategory::sports);
  rsn.scope = "regional:bay area";
  CHECK(suggests(rsn, p, false, "NBC Sports Bay Area"));  // sports: no guide needed
  rsn.scope = "regional:boston";
  CHECK_FALSE(suggests(rsn, p, true, "NBC Sports Boston"));
  EntryInfo unknown = entry(LineupCategory::local);
  unknown.scope = "regional:?";
  CHECK(suggests(unknown, p, false, "FOX 2 Oakland"));
  CHECK_FALSE(suggests(unknown, p, true, "FOX 5 Testville"));
  EntryInfo news = entry(LineupCategory::news);
  news.scope = "regional:oakland";
  CHECK_FALSE(suggests(news, p, false, "Oakland News"));
  CHECK(suggests(news, p, true, "Oakland News"));
}

TEST_CASE("suggests: national channels by category and guide") {
  LineupPrefs p = bayArea();
  EntryInfo sports = entry(LineupCategory::sports);
  CHECK_FALSE(suggests(sports, p, false, "Dream Sports"));
  CHECK(suggests(sports, p, true, "Dream Sports"));
  CHECK(suggests(sports, p, false, "Dream Sports San Jose"));  // names the market
  EntryInfo ent = entry(LineupCategory::entertainment);
  CHECK_FALSE(suggests(ent, p, false, ent.name));
  CHECK(suggests(ent, p, true, ent.name));
  p.requireGuide = false;
  CHECK(suggests(ent, p, false, ent.name));
  EntryInfo intl = entry(LineupCategory::international);
  CHECK_FALSE(suggests(intl, p, true, intl.name));  // not picked
  p.categories.insert(LineupCategory::international);
  CHECK(suggests(intl, p, true, intl.name));
  CHECK_FALSE(suggests(intl, p, false, intl.name));
}

TEST_CASE("East/West feeds: which markets get the West feeds") {
  CHECK(marketIsWest("San Francisco–Oakland–San Jose") == true);
  CHECK(marketIsWest("San Francisco-Oakland-San Jose") == true);
  CHECK(marketIsWest("Denver") == true);              // Mountain: the West feeds
  CHECK(marketIsWest("Phoenix (Prescott)") == true);
  CHECK(marketIsWest("Honolulu") == true);
  CHECK(marketIsWest("Anchorage") == true);
  CHECK(marketIsWest("Portland, OR") == true);
  CHECK(marketIsWest("Portland-Auburn") == false);    // Maine
  CHECK(marketIsWest("New York") == false);
  CHECK(marketIsWest("Chicago") == false);             // Central: the East feeds
  CHECK(marketIsWest("Dallas–Fort Worth") == false);   // spelled as the list page does
  CHECK(marketIsWest("St. Louis") == false);
  CHECK(marketIsWest("North Platte") == false);
  CHECK_FALSE(marketIsWest("Somewhere Else").has_value());
}

TEST_CASE("East/West feeds: Automatic follows the home market, then the time zone") {
  constexpr int pacific = -8 * 3600, mountain = -7 * 3600, central = -6 * 3600, london = 0;
  LineupPrefs p = bayArea();
  CHECK(resolveCoast(p, london) == Coast::west);
  p.markets.push_back({"New York", std::nullopt, {}});
  CHECK(resolveCoast(p, pacific) == Coast::west);  // the first market is home
  p.homeMarket = "New York";
  CHECK(resolveCoast(p, pacific) == Coast::east);
  p.coast = Coast::west;
  CHECK(resolveCoast(p, central) == Coast::west);
  p.coast = Coast::both;
  CHECK(resolveCoast(p, central) == Coast::both);

  LineupPrefs none;
  CHECK(resolveCoast(none, pacific) == Coast::west);
  CHECK(resolveCoast(none, mountain) == Coast::west);
  CHECK(resolveCoast(none, central) == Coast::east);
  CHECK(resolveCoast(none, london) == Coast::east);
  none.markets = {{"Somewhere Else", std::nullopt, {}}};
  CHECK(resolveCoast(none, pacific) == Coast::west);
}

TEST_CASE("East/West feeds: one version of each pair is suggested") {
  auto core = [](std::string id, std::string name) {
    EntryInfo e = entry(LineupCategory::movies);
    e.id = std::move(id);
    e.name = name;
    e.core = name;
    return e;
  };
  std::vector<EntryInfo> entries = {core("e|core:HBO", "HBO"), core("e|core:HBO~west", "HBO"),
                                    core("e|core:Starz~west", "Starz"),  // only the West version
                                    core("e|core:Cinemax", "Cinemax")};
  LineupPrefs p = bayArea();
  p.categories.insert(LineupCategory::movies);
  auto picked = [&](Coast coast) {
    CoastFeeds coasts = CoastFeeds::of(entries, coast);
    std::set<std::string> out;
    for (const EntryInfo& e : entries) {
      if (suggests(e, p, true, e.name, coasts)) out.insert(e.id);
    }
    return out;
  };
  CHECK(CoastFeeds::of(entries, Coast::west).paired == std::set<std::string>{"e|core:HBO"});
  CHECK(picked(Coast::west) ==
        std::set<std::string>{"e|core:HBO~west", "e|core:Starz~west", "e|core:Cinemax"});
  CHECK(picked(Coast::east) ==
        std::set<std::string>{"e|core:HBO", "e|core:Starz~west", "e|core:Cinemax"});
  CHECK(picked(Coast::both).size() == 4);
  // Not passing the pairs keeps the old rule (both).
  for (const EntryInfo& e : entries) CHECK(suggests(e, p, true, e.name));
}

TEST_CASE("East/West feeds: the setting is saved, and old saves read as Automatic") {
  LineupPrefs p = bayArea();
  CHECK(p.coast == Coast::automatic);
  for (Coast c : {Coast::east, Coast::west, Coast::both, Coast::automatic}) {
    p.coast = c;
    auto back = lineupPrefsFromJson(toJson(p));
    REQUIRE(back);
    CHECK(back->coast == c);
    CHECK(*back == p);
  }
  auto old = lineupPrefsFromJson(R"({"configured":true})");
  REQUIRE(old);
  CHECK(old->coast == Coast::automatic);
  auto later = lineupPrefsFromJson(R"({"configured":true,"coast":"mountain"})");
  REQUIRE(later);
  CHECK(later->coast == Coast::automatic);
  CHECK_FALSE(lineupPrefsFromJson(R"({"configured":true,"coast":3})"));
}
