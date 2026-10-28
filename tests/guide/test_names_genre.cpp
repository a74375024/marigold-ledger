// Name keys (normalize, call signs, stations) and programme genres.
#include "core/guide/genre.hpp"
#include "core/guide/names.hpp"
#include "doctest.h"

using namespace marigold;
using namespace marigold::guide;

TEST_CASE("normalize: comparable names, West kept apart") {
  CHECK(normalize("US: ESPN HD") == "espn");
  CHECK(normalize("ESPN East") == "espn");
  CHECK(normalize("HBO West") == "hbo~west");
  CHECK(normalize("HBO (Pacific)") == "hbo~west");
  CHECK(normalize("A&E") == "aande");
  CHECK(normalize("Disney+ (US)") == "disneyplus");
  CHECK(normalize("[VIP] Dream Channel 1080p") == "dreamchannel");
  CHECK(normalize("UK| Thing One") == "thingone");
  CHECK(normalize("Canal Pacífico") == "canalpacfico");  // ASCII only, as in tvOS
  CHECK(normalize("West") == "");
  CHECK(normalize("HBO 2 WEST HD") == "hbo2~west");
  CHECK(normalize("US: A&E (West)") == "aande~west");
  CHECK(normalize("Telemundo (WEST) \xe1\xb4\xb4\xe1\xb4\xb0") == "telemundo~west");
  // Names with West or Pacific in them that aren't West feeds.
  CHECK(normalize("EPL West Ham") == "eplwestham");
  CHECK(normalize("Wild West TV") == "wildwest");
  CHECK(normalize("Pluto TV Wild West") == "plutowildwest");
  CHECK(normalize("Pacific Blue") == "pacificblue");
  CHECK(normalize("NBC West Palm Beach (WPTV)") == "nbcwestpalmbeach");
  CHECK(normalize("") == "");
}

TEST_CASE("callsigns: suffixes, sub-channels, context") {
  CHECK(callsigns("WNBC-DT", false) == std::vector<std::string>{"WNBC"});
  CHECK(callsigns("KABC-DT2", false) == std::vector<std::string>{"KABC-2"});
  CHECK(callsigns("KABC-TV1", false) == std::vector<std::string>{"KABC"});
  CHECK(callsigns("KABC HD", false) == std::vector<std::string>{"KABC"});
  CHECK(callsigns("Station (KGO)", false) == std::vector<std::string>{"KGO"});
  CHECK(callsigns("Station ( KGO )", false) == std::vector<std::string>{"KGO"});
  CHECK(callsigns("NBC 4 WNBC", false).empty());
  CHECK(callsigns("NBC 4 WNBC", true) == std::vector<std::string>{"WNBC"});
  CHECK(callsigns("kgo", true) == std::vector<std::string>{"KGO"});
  CHECK(callsigns("WILD KIDS", true).empty());
  CHECK(callsigns("WNBC WNBC-DT", true) == std::vector<std::string>{"WNBC"});
  CHECK(callsigns("XKABC", true).empty());
}

TEST_CASE("stationCallsign: network word plus city, number breaks ties") {
  StationsByNetwork stations = groupByNetwork({{"WAAA", "NBC", {"springfield"}, "5"},
                                               {"WBBB", "NBC", {"springfield"}, "12"},
                                               {"KCCC", "FOX", {"capital", "city"}, "2"},
                                               {"KDDD", "MYNETWORKTV", {"capital", "city"}, "20"}});
  CHECK(stationCallsign("NBC 5 Springfield", stations) == std::string("WAAA"));
  CHECK(stationCallsign("NBC 12 Springfield", stations) == std::string("WBBB"));
  CHECK(stationCallsign("US: NBC12 Springfield", stations) == std::string("WBBB"));
  CHECK(stationCallsign("NBC Springfield", stations) == std::string("WAAA"));  // first of equals
  CHECK(stationCallsign("FOX 2 LOCAL CAPITAL CITY", stations) == std::string("KCCC"));
  CHECK(stationCallsign("MyTV Capital City", stations) == std::string("KDDD"));
  CHECK_FALSE(stationCallsign("NBC Sports Springfield", stations));
  CHECK_FALSE(stationCallsign("NBC News Springfield", stations));
  CHECK_FALSE(stationCallsign("CBS 5 Springfield", stations));
  CHECK_FALSE(stationCallsign("NBC 5 Shelbyville", stations));
  CHECK_FALSE(stationCallsign("NBC 5 Springfield", {}));
  // Strict: a number in the name must be the station's.
  CHECK_FALSE(stationCallsign("NBC 7 Springfield", stations, true));
  CHECK(stationCallsign("NBC 7 Springfield", stations) == std::string("WAAA"));
  CHECK(stationCallsign("NBC Springfield", stations, true) == std::string("WAAA"));
}

TEST_CASE("bareCallsign and letterNumberTokens") {
  CHECK(bareCallsign("(US) (Prime) KAGS") == std::string("KAGS"));
  CHECK(bareCallsign("KAGS-DT2 news") == std::string("KAGS"));
  CHECK_FALSE(bareCallsign("kags"));
  CHECK_FALSE(bareCallsign("WEEK News"));
  CHECK_FALSE(bareCallsign("KGO"));
  CHECK(letterNumberTokens("san josé, 5.1") == std::vector<std::string>{"san", "josé", "5", "1"});
}

TEST_CASE("genre flags from categories") {
  CHECK(genre::flags("") == 0);
  CHECK(genre::flags("movie drama") == genre::kMovie);
  CHECK(genre::flags("sports event football") == genre::kSports);
  CHECK(genre::flags("news public affairs") == genre::kNews);
  CHECK(genre::flags("children animation") == genre::kKids);
  CHECK(genre::flags("animated film") == (genre::kKids | genre::kMovie));
  CHECK(genre::flags("talk") == 0);
}

TEST_CASE("genre flags from titles") {
  CHECK(genre::flagsForTitle("NFL Football") == genre::kSports);
  CHECK(genre::flagsForTitle("College Basketball") == genre::kSports);
  CHECK(genre::flagsForTitle("SportsCenter") == genre::kSports);
  CHECK(genre::flagsForTitle("Eyewitness News at 6") == genre::kNews);
  CHECK(genre::flagsForTitle("Today") == genre::kNews);
  CHECK(genre::flagsForTitle("Today's Kitchen") == 0);
  CHECK(genre::flagsForTitle("Today’s Kitchen") == 0);
  CHECK(genre::flagsForTitle("Cooking Hour", std::string("Pregame special")) == genre::kSports);
  CHECK(genre::flagsForTitle("Cooking Hour", std::string("weather")) == 0);  // news: title only
  CHECK(genre::flagsForTitle("Cooking Hour") == 0);
}

TEST_CASE("withTitleFlags and mergeFlags") {
  Program p;
  p.title = "NBA Basketball";
  p.start = 0;
  p.end = 3600;
  CHECK(genre::withTitleFlags(p).genres == genre::kSports);
  p.genres = genre::kMovie;
  CHECK(genre::withTitleFlags(p).genres == genre::kMovie);  // already tagged

  Program a, b, tagged;
  a.title = "A";
  a.start = 0;
  a.end = 3600;
  b.title = "B";
  b.start = 3600;
  b.end = 7200;
  tagged.title = "T";
  tagged.start = 1700;  // overlaps A by 1900 s: more than half
  tagged.end = 5300;    // overlaps B by 1700 s: less than half
  tagged.genres = genre::kNews;
  std::vector<Program> merged = genre::mergeFlags({a, b}, {tagged});
  CHECK(merged[0].genres == genre::kNews);
  CHECK(merged[1].genres == 0);
  CHECK(genre::mergeFlags({a}, {}).front().genres == 0);
  Program untagged = tagged;
  untagged.genres = 0;
  CHECK(genre::mergeFlags({a}, {untagged}).front().genres == 0);
}
