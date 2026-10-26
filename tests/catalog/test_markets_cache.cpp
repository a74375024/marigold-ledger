// Markets from wikitext, the saved catalog's JSON and first-screen subset,
// and whole-group admission.
#include "catalog/helpers.hpp"
#include "core/catalog/admission.hpp"
#include "core/catalog/cache.hpp"
#include "core/catalog/catalog.hpp"
#include "core/catalog/markets.hpp"
#include "doctest.h"

using namespace marigold;
using namespace marigold::catalog;
using testdata::channel;
using testdata::wednesdayNoon;

TEST_CASE("parseMarkets reads template links, once each") {
  std::string text =
      "* [[Template:Springfield TV|Springfield–Shelbyville (12)]]\n"
      "* [[ Template:Capital TV |Capital&nbsp;City]]\n"
      "* [[Template:TV lists|List of things]]\n"
      "* [[Template:Springfield TV|springfield–shelbyville]]\n"
      "* [[Template:Long|" +
      std::string(70, 'x') +
      "]]\n"
      "* [[Not a template|Elsewhere]]\n";
  auto list = markets::parseMarkets(text);
  REQUIRE(list.size() == 2);
  CHECK(list[0].name == "Springfield–Shelbyville");
  CHECK(list[0].templateName == std::string("Template:Springfield TV"));
  CHECK(list[1].name == "Capital City");
  CHECK(list[1].templateName == std::string("Template:Capital TV"));
}

TEST_CASE("parseMarkets: only the United States section, each template once") {
  // The page's shape in 2026: DMAs, then the FCC's TMAs (the same templates
  // under longer names), territories, then Canada and Mexico.
  std::string text =
      "intro [[Template:Ignored TV|Ignored]]\n"
      "==United States==\n===Contiguous States===\n====DMAs====\n"
      "*[[Template:Springfield TV|Springfield]] (#1)\n"
      "*[[Template:Capital TV|Capital City]] (#2)\n"
      "====TMAs====\n"
      "*(1) [[Template:Springfield TV|Springfield, ST\u2013Shelbyville, ST]]\n"
      "===U.S. territories===\n"
      "* [[Template:Island TV|Island Territory]]\n"
      "==Canada==\n"
      "*[[Template:Northtown TV|Northtown]]\n"
      "==Mexico==\n*[[Template:Southtown TV|Southtown]]\n";
  auto list = markets::parseMarkets(text);
  REQUIRE(list.size() == 3);
  CHECK(list[0].name == "Springfield");
  CHECK(list[1].name == "Capital City");
  CHECK(list[2].name == "Island Territory");
}

TEST_CASE("call signs in a market template, and the fallbacks") {
  auto signs =
      markets::callsignsInWikitext("[[KAAA-TV|KAAA]] [[KBBB]] [[ KAAA ]] [[WXYZ-LP|x]] [[KQ]]");
  CHECK(signs == std::vector<std::string>{"KAAA", "KBBB", "WXYZ"});
  // Disambiguated and section links name stations too.
  CHECK(markets::callsignsInWikitext("[[KQQQ (TV)|KQQQ]] [[WRRR (New Jersey)|WRRR]] [[KSSS-TV#History|KSSS]]") ==
        std::vector<std::string>{"KQQQ", "WRRR", "KSSS"});
  std::vector<guide::Station> stations = {{"KAAA", "ABC", {"springfield"}, "5"},
                                          {"KBBB", "NBC", {"shelbyville"}, "2"},
                                          {"KCCC", "CBS", {"capital", "city"}, "9"},
                                          {"KAAA", "CW", {"springfield"}, "5"}};
  markets::Market m{"Springfield–Shelbyville", std::nullopt};
  CHECK(markets::fallbackCallsigns(m, stations) == std::vector<std::string>{"KAAA", "KBBB"});
  auto fallback = markets::fallbackMarkets(stations);
  REQUIRE(fallback.size() == 3);
  CHECK(fallback[0].name == "Capital City");
  CHECK(fallback[1].name == "Shelbyville");
  CHECK(fallback[2].name == "Springfield");
}

TEST_CASE("suggested markets: the most channel names first") {
  std::vector<markets::Market> list = {{"Capital City", std::nullopt},
                                       {"Springfield–Shelbyville", std::nullopt},
                                       {"Ogdenville", std::nullopt},
                                       {"Abc", std::nullopt}};
  std::vector<std::string> names = {"FOX 5 Springfield", "Shelbyville News", "NBC Springfield",
                                    "Capital City 3", "Dream Channel"};
  auto picked = markets::suggested(list, names);
  REQUIRE(picked.size() == 2);
  CHECK(picked[0].name == "Springfield–Shelbyville");
  CHECK(picked[1].name == "Capital City");
  CHECK(markets::suggested(list, names, 1).size() == 1);
  auto json = markets::toJson(list);
  CHECK(markets::fromJson(json) == list);
  CHECK(markets::cacheFresh(0, markets::kMaxAge - 1, false));
  CHECK_FALSE(markets::cacheFresh(0, markets::kMaxAge, false));
  CHECK_FALSE(markets::cacheFresh(0, 1, true));
}

TEST_CASE("catalog cache: JSON round trip, version and signature checks") {
  std::vector<Channel> list = {channel("1", "US: CNN HD"),
                               channel("2", "US: ESPN"),
                               channel("e1", "ESPN+ 01: Team A vs Team B 7:30 PM ET", "US| ESPN+"),
                               channel("e2", "ESPN+ 02", "US| ESPN+"),
                               channel("e3", "ESPN+ 03", "US| ESPN+"),
                               channel("e4", "ESPN+ 04", "US| ESPN+"),
                               channel("e5", "ESPN+ 05: Team C vs Team D 9PM", "US| ESPN+")};
  list[0].epgId = "cnn.us";
  list[0].number = "202";
  list[0].imageUrl = "https://example.invalid/cnn.png";
  list[0].tags = {"News", "US"};
  list[1].isFavorite = true;
  cache::Saved saved;
  saved.signature = "profile-1|xt:1";
  saved.savedAt = wednesdayNoon();
  saved.channels = list;
  saved.catalog = build(list, wednesdayNoon());
  saved.xtreamFormats = {{"xt:1", {"ts", "m3u8"}}};
  std::string text = cache::toJson(saved);
  auto back = cache::fromJson(text, saved.signature);
  REQUIRE(back);
  CHECK(back->channels.size() == list.size());
  CHECK(back->channels[0].epgId == std::string("cnn.us"));
  CHECK(back->channels[0].number == std::string("202"));
  CHECK(back->channels[0].imageUrl == "https://example.invalid/cnn.png");
  CHECK(back->channels[0].tags == list[0].tags);
  CHECK(back->channels[1].isFavorite);
  CHECK(back->catalog.entries.size() == saved.catalog.entries.size());
  CHECK(back->catalog.entries[0].core == saved.catalog.entries[0].core);
  CHECK(back->catalog.events.size() == saved.catalog.events.size());
  CHECK(back->catalog.events[0].start == saved.catalog.events[0].start);
  CHECK(back->catalog.events[0].startGuessed == saved.catalog.events[0].startGuessed);
  CHECK(back->catalog.families[0].feeds == saved.catalog.families[0].feeds);
  CHECK(back->catalog.traits.at("xt:1|e1").familyKey == std::string("espn+ #"));
  CHECK(back->catalog.traits.at("xt:1|e1").kind == ChannelKind::event);
  CHECK(back->catalog.entryOf == saved.catalog.entryOf);
  CHECK(back->xtreamFormats == saved.xtreamFormats);
  CHECK(cache::toJson(*back) == text);
  CHECK_FALSE(cache::fromJson(text, "another profile"));
  CHECK_FALSE(cache::fromJson("{\"version\": 2}", saved.signature));
  CHECK_FALSE(cache::fromJson("[]", saved.signature));

  // First-screen part: the chosen entries and families and their channels.
  std::string cnn = saved.catalog.entryOf.at("xt:1|1");
  auto first = cache::firstPart(saved, {cnn}, {"fam|espn+ #"});
  CHECK(first.catalog.entries.size() == 1);
  CHECK(first.catalog.families.size() == 1);
  CHECK(first.channels.size() == 6);  // CNN + five feeds
  CHECK(first.catalog.traits.size() == 6);
  CHECK(first.catalog.entryOf.size() == 1);
  CHECK(first.catalog.events.size() == saved.catalog.events.size());
  auto none = cache::firstPart(saved, {}, {});
  CHECK(none.channels.empty());
  CHECK(none.catalog.events.empty());

  CHECK(cache::fileName("abc", cache::Part::first) ==
        "catalog-" + std::string("e16801510db89efd") + "-first.json");
  CHECK(cache::fileName("abc", cache::Part::full).find("-full.json") != std::string::npos);
}

TEST_CASE("admitGroups admits whole groups in provider order") {
  std::vector<Channel> list;
  auto add = [&](const std::string& group, int count) {
    for (int i = 0; i < count; ++i) {
      list.push_back(channel(group + std::to_string(i), "Ch",
                             group.empty() ? std::nullopt : std::optional<std::string>(group)));
    }
  };
  add("A", 4);
  add("B", 7);  // would cross a cap of 10 after A: cut
  add("C", 3);
  add("", 2);   // no group: one group of its own
  add("D", 2);  // doesn't fit any more
  list.push_back(channel("t1", "Tagged", std::nullopt));
  list.back().tags = {"C"};  // the first tag stands in for the group
  Admission a = admitGroups(list, 10);
  CHECK(a.admittedGroups == 3);  // A, C (with the tagged one), ungrouped
  CHECK(a.cutGroups == 2);       // B, D
  REQUIRE(a.channels.size() == 10);
  CHECK(a.channels[0].id == "xt:1|A0");
  CHECK(a.channels[4].id == "xt:1|C0");
  CHECK(a.channels[7].id == "xt:1|0");
  CHECK(a.channels[9].id == "xt:1|t1");  // original order kept

  // Everything fits under the default cap.
  Admission all = admitGroups(list);
  CHECK(all.channels.size() == list.size());
  CHECK(all.cutGroups == 0);
  CHECK(all.admittedGroups == 5);
  // A single group larger than the cap is cut whole.
  Admission big = admitGroups(list, 3);
  CHECK(big.channels.size() == 2);  // the ungrouped two fit; D's two then don't
  CHECK(big.channels[0].id == "xt:1|0");
  CHECK(admitGroups({}, 10).channels.empty());
  CHECK(kMaxChannels == 30000);
}
