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
  CHECK(exactKey("beIN SPORTS en EspaÃ±ol") == "beinsportsenespanol");
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
  data.elsewhere["arydigital"] = "PK:ur";
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
  CHECK(m.elsewhere == "PK:ur");
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
  CHECK(up->closed.empty());  // its defunct row is its old name
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
  CHECK(data.elsewhere.at("arydigital") == "PK:");
  CHECK_FALSE(data.elsewhere.count("up"));
}

TEST_CASE("directory: a table's channel column, past owners spanning rows") {
  const char* page =
      "== English language ==\n"
      "{| class=\"wikitable\"\n"
      "! Owner !! Channel !! Launched\n"
      "|-\n"
      "| rowspan=\"2\" | [[Entertainment Studios]] || [[Comedy.TV]] || 2009\n"
      "|-\n"
      "| [[Recipe.TV]] || 2009\n"
      "|-\n"
      "| [[AMC Networks]] || [[AMC (TV channel)|AMC]] || 1984\n"
      "|}\n";
  std::vector<Row> rows = parseList(page);
  REQUIRE(rows.size() == 3);
  CHECK(rows[0].text == "Comedy.TV");
  CHECK(rows[1].text == "Recipe.TV");
  CHECK(rows[2].text == "AMC");
  CHECK(rows[2].title == "AMC (TV channel)");
  CHECK_FALSE(rowNamed(rows, "Entertainment Studios"));
}

TEST_CASE("directory: umbrella articles, other channels' names, rebrands") {
  Sources src;
  src.rows["pay"] = {{"Entertainment Studios Networks", "Comedy.TV", "English language", ""},
                     {"Entertainment Studios Networks", "Recipe.TV", "English language", ""},
                     {"Cinemax", "Cinemax", "English language", ""}};
  src.rows["defunct"] = {{"FanDuel Sports Network North", "Fox Sports North", "Network rebrands", "2008–2021"}};
  src.pages["Entertainment Studios Networks"] = {{"Comedy.tv", "Recipe.tv"}, ""};
  src.pages["Cinemax"] = {{"ActionMAX", "Skinemax", "WCMX-LD"}, ""};
  src.pages["FanDuel Sports Network North"] = {{"Bally Sports North", "Fox Sports North"}, ""};
  src.iptv = parseIptvOrg(
      "id,name,alt_names,network,owners,country,categories,is_nsfw,launched,closed,replaced_by,website\n"
      "Cinemax.us,Cinemax,,,,US,movies,FALSE,,,,\n"
      "ActionMAX.us,ActionMAX,,,,US,movies,FALSE,,,,\n",
      "channel,id,name,alt_names,is_main,broadcast_area,timezones,languages,format\n");
  Data data = assemble(src, "2026-10-28");
  auto find = [&](const char* id) -> const Entry* {
    auto it = std::find_if(data.entries.begin(), data.entries.end(), [&](const Entry& e) { return e.id == id; });
    return it == data.entries.end() ? nullptr : &*it;
  };
  CHECK(find("Comedy.TV"));
  CHECK(find("Recipe.TV"));
  CHECK_FALSE(find("Entertainment Studios Networks"));
  const Entry* cinemax = find("Cinemax");
  REQUIRE(cinemax);
  CHECK(cinemax->iptvOrg == "Cinemax.us");
  CHECK(std::find(cinemax->aliases.begin(), cinemax->aliases.end(), "Skinemax") != cinemax->aliases.end());
  CHECK(std::find(cinemax->aliases.begin(), cinemax->aliases.end(), "ActionMAX") == cinemax->aliases.end());
  CHECK(std::find(cinemax->aliases.begin(), cinemax->aliases.end(), "WCMX-LD") == cinemax->aliases.end());
  const Entry* north = find("FanDuel Sports Network North");
  REQUIRE(north);
  CHECK(north->closed.empty());
  CHECK(std::find(north->aliases.begin(), north->aliases.end(), "Fox Sports North") != north->aliases.end());
}

TEST_CASE("directory: iptv-org's free streaming channels, a tier below the lists") {
  CHECK(withoutService("Pluto TV Cheers") == "Cheers");
  CHECK(withoutService("Westerns by Pluto TV") == "Westerns");
  CHECK(withoutService("Bowling TV") == "Bowling TV");
  Sources src;
  src.rows["pay"] = {{"", "FYI", "English language", "FYI"}};
  src.iptv = parseIptvOrg(
      "id,name,alt_names,network,owners,country,categories,is_nsfw,launched,closed,replaced_by,website\n"
      "CheersPlusFrasier.us,Cheers + Frasier,,Pluto TV,,US,series,FALSE,,,,\n"
      "PlutoTVMTVClassic.us,Pluto TV MTV Classic,,Pluto TV,,US,music,FALSE,,,,\n"
      "FYIPluto.us,FYI,,Pluto TV,,US,series,FALSE,,,,\n"
      "CityNews.us,City News,,,,US,news,FALSE,,,,\n"
      "OldFast.us,Old Fast,,,,US,movies,FALSE,,2023-01-01,,\n"
      "Deals.us,Deals,,Infomercials,,US,shop,FALSE,,,,\n"
      "PlutoTVHorror.us,Pluto TV Horror,,Pluto TV,,US,movies,FALSE,,,,\n",
      "channel,id,name,alt_names,is_main,broadcast_area,timezones,languages,format\n"
      "CheersPlusFrasier.us,SD,SD,,TRUE,c/US,America/New_York,eng,480i\n"
      "PlutoTVMTVClassic.us,SD,SD,,TRUE,c/US,America/New_York,eng,480i\n"
      "FYIPluto.us,SD,SD,,TRUE,c/US,America/New_York,eng,480i\n"
      "CityNews.us,SD,SD,,TRUE,ct/USNYC,America/New_York,eng,480i\n"
      "OldFast.us,SD,SD,,TRUE,c/US,America/New_York,eng,480i\n"
      "Deals.us,SD,SD,,TRUE,c/US,America/New_York,eng,480i\n"
      "PlutoTVHorror.us,SD,SD,,TRUE,c/US,America/New_York,eng,480i\n");
  Data data = assemble(src, "2026-10-28");
  auto find = [&](const char* id) -> const Entry* {
    auto it = std::find_if(data.entries.begin(), data.entries.end(), [&](const Entry& e) { return e.id == id; });
    return it == data.entries.end() ? nullptr : &*it;
  };
  const Entry* cheers = find("iptv:CheersPlusFrasier.us");
  REQUIRE(cheers);
  CHECK(cheers->list == "free");
  CHECK(cheers->language == "en");
  const Entry* mtv = find("iptv:PlutoTVMTVClassic.us");
  REQUIRE(mtv);
  CHECK(mtv->name == "MTV Classic");
  CHECK_FALSE(find("iptv:FYIPluto.us"));  // the lists have FYI
  CHECK_FALSE(find("iptv:CityNews.us"));  // one city's
  CHECK_FALSE(find("iptv:OldFast.us"));   // shut down
  CHECK_FALSE(find("iptv:Deals.us"));     // infomercials
  // Matching: a listed channel first, the free tier after.
  Index index(data);
  CHECK(index.match("Cheers + Frasier").entry == cheers);
  CHECK(index.match("MTV Classic").entry == mtv);
  CHECK(index.match("FYI").entry->id == "FYI");
  // A one-word name stays with its service.
  REQUIRE(find("iptv:PlutoTVHorror.us"));
  CHECK(find("iptv:PlutoTVHorror.us")->name == "Pluto TV Horror");
  CHECK(index.match("HORROR").kind == MatchKind::none);
}

TEST_CASE("directory: an article's other name that is its own iptv-org channel stays") {
  Sources src;
  src.rows["pay"] = {{"Spectrum News NECN", "Spectrum News NECN", "Regional channels", "Boston"},
                     {"FX Movie Channel", "FX Movie Channel", "English language", ""}};
  src.pages["Spectrum News NECN"] = {{"NECN", "New England Cable News"}, ""};
  src.pages["FX Movie Channel"] = {{"FXM", "Fox Movie Channel"}, ""};
  src.iptv = parseIptvOrg(
      "id,name,alt_names,network,owners,country,categories,is_nsfw,launched,closed,replaced_by,website\n"
      "NECN.us,NECN,,,,US,news,FALSE,,,,\n"
      "FXM.us,FXM,,,,US,movies,FALSE,,,,\n",
      "channel,id,name,alt_names,is_main,broadcast_area,timezones,languages,format\n"
      "NECN.us,SD,SD,,TRUE,r/US-NE,America/New_York,eng,480i\n"
      "FXM.us,SD,SD,,TRUE,c/US,America/New_York,spa,480i\n");
  Data data = assemble(src, "2026-10-28");
  Index index(data);
  REQUIRE(index.match("NECN HD").entry);
  CHECK(index.match("NECN HD").entry->id == "Spectrum News NECN");
  CHECK(index.match("NECN HD").entry->iptvOrg == "NECN.us");
  const Entry* fxm = index.match("FXM").entry;
  REQUIRE(fxm);
  CHECK(fxm->id == "FX Movie Channel");
  CHECK(fxm->language == "en");  // its list section, over iptv-org's feed
}

TEST_CASE("directory: iptv-org's two rows of one channel are one, of two channels two") {
  Sources src;
  src.rows["pay"] = {{"FX Movie Channel", "FX Movie Channel", "English language", ""},
                     {"C-SPAN", "C-SPAN", "English language", ""}};
  src.pages["FX Movie Channel"] = {{"FXM", "Fox Movie Channel"}, ""};
  src.pages["C-SPAN"] = {{"C-SPAN2"}, ""};
  src.iptv = parseIptvOrg(
      "id,name,alt_names,network,owners,country,categories,is_nsfw,launched,closed,replaced_by,website\n"
      "FXMovieChannel.us,FX Movie Channel,,FX Channels,FX Networks LLC,US,movies,FALSE,1994-10-31,,,\n"
      "FXM.us,FXM,,FX Channels,FX Networks LLC,US,movies,FALSE,1994-10-31,,,\n"
      "CSPAN.us,C-SPAN,,,National Cable Satellite Corporation,US,news,FALSE,1979-03-19,,,\n"
      "CSPAN2.us,C-SPAN 2,C-SPAN2,,National Cable Satellite Corporation,US,news,FALSE,1986-06-02,,,\n",
      "channel,id,name,alt_names,is_main,broadcast_area,timezones,languages,format\n"
      "FXMovieChannel.us,SD,SD,,TRUE,c/US,America/New_York,eng,480i\n"
      "FXM.us,SD,SD,,TRUE,c/US,America/New_York,eng,480i\n"
      "CSPAN.us,SD,SD,,TRUE,c/US,America/New_York,eng,480i\n"
      "CSPAN2.us,SD,SD,,TRUE,c/US,America/New_York,eng,480i\n");
  Data data = assemble(src, "2026-10-28");
  Index index(data);
  REQUIRE(index.match("FXM").entry);
  CHECK(index.match("FXM").entry->id == "FX Movie Channel");  // not a free-tier copy
  REQUIRE(index.match("C-SPAN 2").entry);
  CHECK(index.match("C-SPAN 2").entry->list == "free");  // its own channel, not C-SPAN
  CHECK(index.match("C-SPAN").entry->id == "C-SPAN");
}
