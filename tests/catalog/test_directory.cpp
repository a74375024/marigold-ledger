// The channel directory: keys, matching (exact, other names, loose,
// ambiguous, corrections), saving, and how CI assembles it from the list
// pages, the articles' redirects and Wikidata facts, and iptv-org.
#include <algorithm>

#include "core/catalog/directory.hpp"
#include "core/catalog/directory_sources.hpp"
#include "doctest.h"

using namespace marigold::catalog::directory;
using namespace marigold::catalog::directory::sources;

namespace {

Entry entry(const char* id, const char* name, std::vector<std::string> aliases = {}) {
  Entry e;
  e.id = id;
  e.name = name;
  e.aliases = std::move(aliases);
  e.list = "pay";
  return e;
}

const Row* rowNamed(const std::vector<Row>& rows, const char* text) {
  auto it = std::find_if(rows.begin(), rows.end(), [&](const Row& r) { return r.text == text; });
  return it == rows.end() ? nullptr : &*it;
}

}  // namespace

TEST_CASE("directory: name keys") {
  CHECK(exactKey("The Movie Channel (TMC) HD") == "themoviechannel");
  CHECK(exactKey("A&E West") == "aande");
  CHECK(exactKey("USA Network") == "usanetwork");
  CHECK(exactKey("ESPN US") == "espn");
  CHECK(exactKey("beIN Sports en Español") == "beinsportsenespanol");
  CHECK(exactKey("Wild West TV") == "wildwesttv");
  CHECK(looseKey("UP TV") == "up");
  CHECK(looseKey("UPtv") == "up");
  CHECK(looseKey("The Cowboy Channel") == "cowboy");
  CHECK(looseKey("FYI Network") == "fyi");
  CHECK(looseKey("MTV") == "mtv");
  CHECK(exactKey("") .empty());
}

TEST_CASE("directory: matching a provider's name") {
  Data data;
  data.entries = {entry("UPtv", "UPtv", {"UP TV", "Gospel Music Channel"}), entry("NewsNation", "NewsNation"),
                  entry("WGN America", "WGN America"), entry("Nick Jr.", "Nick Jr."),
                  entry("Nick Jr. Too", "Nick Jr. Too"), entry("Fight Network", "Fight Network"),
                  entry("Fight Channel", "The Fight Channel")};
  data.entries[2].closed = "2021";
  data.elsewhere["arydigital"] = "PK";
  Index index(data);
  Match m = index.match("UPtv");
  CHECK(m.kind == MatchKind::exact);
  REQUIRE(m.entry);
  CHECK(m.entry->id == "UPtv");
  CHECK(index.match("UP TV HD").kind == MatchKind::exact);  // the same letters
  CHECK(index.match("Gospel Music Channel").kind == MatchKind::alias);
  CHECK(index.match("Gospel Music Channel").entry->id == "UPtv");
  CHECK(index.match("Up").kind == MatchKind::loose);
  CHECK(index.match("Nick Jr Too").entry->id == "Nick Jr. Too");
  CHECK(index.match("Nick Jr").entry->id == "Nick Jr.");
  // Our corrections win, even over a listed name of its own.
  m = index.match("WGN America");
  CHECK(m.kind == MatchKind::alias);
  CHECK(m.entry->id == "NewsNation");
  // Two entries with one loose key: neither.
  CHECK(index.match("Fight").kind == MatchKind::ambiguous);
  // Not listed, but another country's channel.
  m = index.match("ARY DIGITAL");
  CHECK(m.kind == MatchKind::none);
  CHECK(m.elsewhere == "PK");
  CHECK(index.match("Zeta Channel").kind == MatchKind::none);
}

TEST_CASE("directory: saved and read back") {
  Data data;
  data.made = "2026-10-28";
  data.entries = {entry("UPtv", "UPtv", {"UP TV"})};
  data.entries[0].language = "en";
  data.entries[0].places = {"minnesota"};
  data.entries[0].closed = "2024-07-01";
  data.entries[0].wikidata = "Q1";
  data.entries[0].iptvOrg = "UPtv.us";
  data.elsewhere["arydigital"] = "PK";
  auto back = fromJson(toJson(data));
  REQUIRE(back);
  CHECK(back->made == "2026-10-28");
  REQUIRE(back->entries.size() == 1);
  CHECK(back->entries[0] == data.entries[0]);
  CHECK(back->elsewhere == data.elsewhere);
  CHECK_FALSE(fromJson("nope"));
}

TEST_CASE("directory: a list page's channels") {
  const char* page =
      "Intro text with [[Cable television]].\n"
      "== Sports ==\n"
      "{| class=\"wikitable sortable\"\n"
      "! Channel !! Owner\n"
      "|-\n"
      "| [[ESPN]] || [[The Walt Disney Company]]\n"
      "|-\n"
      "| style=\"x\" | [[UPtv|UP]] || [[InspirationNow]]<ref>cite</ref>\n"
      "|-\n"
      "| [[File:Logo.png|20px]] || Willow Xtra || Willow\n"
      "|-\n"
      "| [[Starz Encore#Channels|Starz Encore Action]] || Starz\n"
      "|}\n"
      "== Spanish-language ==\n"
      "* [[Mexicanal]] – {{nowrap|Mexican programs}}\n"
      "* ''[[Cine Mexicano]]''\n"
      "== See also ==\n"
      "* [[List of television networks]]\n";
  std::vector<Row> rows = parseList(page);
  const Row* espn = rowNamed(rows, "ESPN");
  REQUIRE(espn);
  CHECK(espn->title == "ESPN");
  CHECK(espn->section == "Sports");
  const Row* up = rowNamed(rows, "UP");
  REQUIRE(up);
  CHECK(up->title == "UPtv");
  const Row* willow = rowNamed(rows, "Willow Xtra");  // unlinked, after a logo
  REQUIRE(willow);
  CHECK(willow->title.empty());
  const Row* action = rowNamed(rows, "Starz Encore Action");  // a section of another article
  REQUIRE(action);
  CHECK(action->title.empty());
  const Row* mexicanal = rowNamed(rows, "Mexicanal");
  REQUIRE(mexicanal);
  CHECK(mexicanal->section == "Spanish-language");
  CHECK(rowNamed(rows, "Cine Mexicano"));
  CHECK_FALSE(rowNamed(rows, "Channel"));                      // the header row
  CHECK_FALSE(rowNamed(rows, "List of television networks"));  // See also
  CHECK(plainText("[[A&E Networks|A&E]] ''West''<ref name=x/> {{citation needed}}") == "A&E West");
}

TEST_CASE("directory: Wikipedia's and Wikidata's answers") {
  const char* pages = R"json({"continue":{"rdcontinue":"123|abc","continue":"||"},
    "query":{"normalized":[{"from":"uPtv","to":"UPtv"}],
             "redirects":[{"from":"Up TV","to":"UPtv"},{"from":"HBO Zone","to":"HBO","tofragment":"Channels"}],
             "pages":[{"title":"UPtv","redirects":[{"title":"Gospel Music Channel"},{"title":"UP (TV network)"},
                                                  {"title":"UP channels","fragment":"List"}],
                       "pageprops":{"wikibase_item":"Q42"}},
                      {"title":"Nowhere","missing":true}]}})json";
  auto answer = parsePagesAnswer(pages);
  REQUIRE(answer);
  CHECK(answer->leadsTo.at("uPtv") == "UPtv");
  CHECK(answer->leadsTo.at("HBO Zone").empty());
  REQUIRE(answer->pages.count("UPtv"));
  CHECK(answer->pages.at("UPtv").wikidata == "Q42");
  CHECK(answer->pages.at("UPtv").redirects == std::vector<std::string>{"Gospel Music Channel", "UP (TV network)"});
  CHECK_FALSE(answer->pages.count("Nowhere"));
  CHECK(answer->continueQuery == "&continue=%7C%7C&rdcontinue=123%7Cabc");
  CHECK(pagesRequestUrl({"A b", "C"}, "").find("titles=A%20b%7CC") != std::string::npos);

  const char* entities = R"json({"entities":{"Q42":{"aliases":{"en":[{"value":"UP"}]},
    "claims":{"P407":[{"mainsnak":{"datavalue":{"value":{"id":"Q1860"}}}}],
              "P576":[{"mainsnak":{"datavalue":{"value":{"time":"+2024-07-01T00:00:00Z"}}}}]}},
    "Q7":{"claims":{"P407":[{"mainsnak":{"datavalue":{"value":{"id":"Q999"}}}}],
              "P576":[{"mainsnak":{"datavalue":{"value":{"time":"+2019-00-00T00:00:00Z"}}}}]}}}})json";
  auto facts = parseEntities(entities);
  CHECK(facts.at("Q42").aliases == std::vector<std::string>{"UP"});
  CHECK(facts.at("Q42").language == "en");
  CHECK(facts.at("Q42").closed == "2024-07-01");
  CHECK(facts.at("Q7").language == "Q999");
  CHECK(facts.at("Q7").closed == "2019");
}

TEST_CASE("directory: iptv-org rows and its noise") {
  const char* channels =
      "id,name,alt_names,network,owners,country,categories,is_nsfw,launched,closed,replaced_by,website\n"
      "UPtv.us,UPtv,UP TV;Gospel Music Channel,,,US,family,FALSE,,,,\n"
      "UrbanaPublicTelevision.us,Urbana Public Television,UPTV,,,US,legislative,FALSE,,,,\n"
      "KGODT1.us,KGO-DT1,,,,US,general,FALSE,,,,\n"
      "PlutoTVMTVClassic.us,MTV Classic Pluto,,,,US,music,FALSE,,,,\n"
      "Adult.us,Adult One,,,,US,xxx,TRUE,,,,\n"
      "ARYDigital.pk,ARY Digital,,,,PK,entertainment,FALSE,,,,\n";
  const char* feeds =
      "channel,id,name,alt_names,is_main,broadcast_area,timezones,languages,format\n"
      "UPtv.us,SD,SD,,TRUE,c/US,America/New_York,eng,480i\n"
      "ARYDigital.pk,SD,SD,,TRUE,c/PK,Asia/Karachi,urd,480i\n";
  std::vector<IptvChannel> list = parseIptvOrg(channels, feeds);
  REQUIRE(list.size() == 6);
  CHECK(list[0].altNames == std::vector<std::string>{"UP TV", "Gospel Music Channel"});
  CHECK(list[0].language == "en");
  CHECK(list[5].language == "ur");
  CHECK_FALSE(iptvNoise(list[0]));
  CHECK(iptvNoise(list[1]));  // public access
  CHECK(iptvNoise(list[2]));  // a station
  CHECK(iptvNoise(list[3]));  // a free-streaming copy
  CHECK(iptvNoise(list[4]));  // adult
}

TEST_CASE("directory: assembled in layers") {
  Sources src;
  src.rows["pay"] = {{"UPtv", "UP", "Family", "UP | InspirationNow"},
                     {"Starz Encore", "Starz Encore Action", "Movies", "Starz Encore Action | Starz"},
                     {"", "Willow Xtra", "Sports", "Willow Xtra"},
                     {"Pac-12 Network", "Pac-12 Bay Area", "Sports", "Pac-12 Bay Area"}};
  src.rows["spanish"] = {{"Mexicanal", "Mexicanal", "Networks", "Mexicanal"}};
  src.rows["regional sports"] = {{"FanDuel Sports Network North", "FanDuel Sports Network North", "Midwest",
                                  "FanDuel Sports Network North | Minnesota, Minneapolis"}};
  src.rows["defunct"] = {{"WGN America", "WGN America", "Defunct", "WGN America | 1978–2021"},
                         {"UPtv", "Gospel Music Channel", "Renamed", "Gospel Music Channel | 2004–2013"}};
  src.pages["UPtv"] = {{"UP TV", "Gospel Music Channel"}, "Q42"};
  src.pages["Starz Encore"] = {{"Encore (TV network)"}, "Q1"};
  src.pages["Mexicanal"] = {{}, ""};
  src.pages["FanDuel Sports Network North"] = {{"Fox Sports North", "Bally Sports North"}, ""};
  src.pages["WGN America"] = {{}, ""};
  src.entities["Q42"] = {{"UP Faith & Family"}, "en", ""};
  src.iptv = parseIptvOrg(
      "id,name,alt_names,network,owners,country,categories,is_nsfw,launched,closed,replaced_by,website\n"
      "UPtv.us,UPtv,UPtv Network,,,US,family,FALSE,,,,\n"
      "UrbanaPublicTelevision.us,Urbana Public Television,UPTV,,,US,legislative,FALSE,,,,\n"
      "WillowXtra.us,Willow Xtra,Willow Extra,,,US,sports,FALSE,,,,\n"
      "AnotherUP.us,UP,,,,US,family,FALSE,,,,\n"
      "ARYDigital.pk,ARY Digital,,,,PK,entertainment,FALSE,,,,\n",
      "channel,id,name,alt_names,is_main,broadcast_area,timezones,languages,format\n"
      "WillowXtra.us,SD,SD,,TRUE,c/US,America/New_York,eng,480i\n");
  Data data = assemble(src, "2026-10-28");
  auto find = [&](const char* id) -> const Entry* {
    auto it = std::find_if(data.entries.begin(), data.entries.end(), [&](const Entry& e) { return e.id == id; });
    return it == data.entries.end() ? nullptr : &*it;
  };
  const Entry* up = find("UPtv");
  REQUIRE(up);
  CHECK(up->name == "UPtv");
  CHECK(up->list == "pay");    // also on the defunct list (its old name), but current
  CHECK(up->closed.empty() == false);  // the defunct row marks it; phase 1 keeps what the sources say
  CHECK(up->wikidata == "Q42");
  CHECK(up->language == "en");
  for (const char* alias : {"UP", "Gospel Music Channel", "UP Faith & Family"}) {
    CAPTURE(alias);
    CHECK(std::find(up->aliases.begin(), up->aliases.end(), alias) != up->aliases.end());
  }
  // Two iptv-org channels share its names (one of them public access, left
  // out; another "UP"): no iptv-org enrichment rather than a wrong one.
  CHECK(up->iptvOrg.empty());
  // A row linking into a bigger article is its own entry, without its facts.
  const Entry* action = find("Starz Encore Action");
  REQUIRE(action);
  CHECK(action->wikidata.empty());
  CHECK_FALSE(find("Starz Encore"));
  const Entry* xtra = find("Willow Xtra");
  REQUIRE(xtra);
  CHECK(xtra->iptvOrg == "WillowXtra.us");
  CHECK(xtra->language == "en");
  CHECK(std::find(xtra->aliases.begin(), xtra->aliases.end(), "Willow Extra") != xtra->aliases.end());
  CHECK(find("Pac-12 Bay Area"));  // not the Pac-12 Network article's channel
  CHECK(find("Mexicanal")->language == "es");
  const Entry* north = find("FanDuel Sports Network North");
  REQUIRE(north);
  CHECK(std::find(north->places.begin(), north->places.end(), "minnesota") != north->places.end());
  CHECK(std::find(north->aliases.begin(), north->aliases.end(), "Fox Sports North") != north->aliases.end());
  CHECK(find("WGN America")->closed == "2021");
  CHECK(data.elsewhere.at("arydigital") == "PK");
  CHECK_FALSE(data.elsewhere.count("up"));
}
