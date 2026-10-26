// LiveTVWork: group annotation, rename-only refresh, the guide window,
// listing merges (the 15-minute rule, gap filling, genre tags) and lineup
// order.
#include "catalog/helpers.hpp"
#include "core/catalog/catalog.hpp"
#include "core/catalog/work.hpp"
#include "core/guide/genre.hpp"
#include "doctest.h"

using namespace marigold;
using namespace marigold::catalog;
using testdata::channel;
using testdata::utc;

namespace {

Program program(const std::string& title, time::Seconds start, int minutes, uint8_t genres = 0) {
  Program p;
  p.id = title;
  p.channelId = "c";
  p.title = title;
  p.start = start;
  p.end = start + minutes * 60;
  p.genres = genres;
  return p;
}

guide::RawProgram raw(const std::string& title, time::Seconds start, int minutes, int flags = 0) {
  guide::RawProgram p;
  p.s = static_cast<int>(start / 60);
  p.m = minutes;
  p.t = title;
  p.f = flags;
  return p;
}

}  // namespace

TEST_CASE("log text") {
  CHECK(sizeText(2'340'000) == "2.3 MB");
  CHECK(sizeText(340'000) == "340 KB");
  CHECK(sizeText(10) == "1 KB");
  CHECK(ageText(1000, 1045) == "45 s");
  CHECK(ageText(0, 600) == "10 min");
  CHECK(ageText(0, 3 * 3600 + 5) == "3 h");
}

TEST_CASE("annotated: tag first, then a prefix shared by 3+ channels") {
  std::vector<Channel> list = {channel("1", "US: NBC 4"),       channel("2", "US: CBS 2"),
                               channel("3", "us | ABC 7"),      channel("4", "UK| Thing One"),
                               channel("5", "[Denver] FOX 31"), channel("6", "Plain Name")};
  list[4].tags = {"Locals"};
  std::vector<Channel> out = annotated(list);
  CHECK(out[0].group == std::string("US"));
  CHECK(out[0].shortName == "NBC 4");
  CHECK(out[2].group == std::string("US"));  // same key, first spelling
  CHECK(out[2].shortName == "ABC 7");
  CHECK_FALSE(out[3].group);
  CHECK(out[3].shortName.empty());
  CHECK(out[4].group == std::string("Locals"));
  CHECK(out[4].shortName == "FOX 31");
  CHECK_FALSE(out[5].group);
  CHECK(out[5].displayName() == "Plain Name");
}

TEST_CASE("renamedEventSlots tells a rename from a real change") {
  std::vector<Channel> old = {channel("1", "ESPN+ 01: Team A vs Team B"), channel("2", "US: CNN")};
  std::unordered_map<std::string, ChannelTraits> traits;
  traits["xt:1|1"].familyKey = "espn+ #";
  traits["xt:1|2"];
  std::vector<Channel> renamed = old;
  renamed[0].name = "ESPN+ 01: Team C vs Team D";
  auto slots = renamedEventSlots(old, renamed, traits);
  REQUIRE(slots);
  REQUIRE(slots->size() == 1);
  CHECK((*slots)[0].name == "ESPN+ 01: Team C vs Team D");
  CHECK(renamedEventSlots(old, old, traits)->empty());
  std::vector<Channel> regular = old;
  regular[1].name = "US: CNN HD";
  CHECK_FALSE(renamedEventSlots(old, regular, traits));
  std::vector<Channel> regroup = old;
  regroup[0].group = "Other";
  CHECK_FALSE(renamedEventSlots(old, regroup, traits));
  std::vector<Channel> epg = old;
  epg[1].epgId = "cnn.us";
  CHECK_FALSE(renamedEventSlots(old, epg, traits));
  CHECK_FALSE(renamedEventSlots(old, {old[0]}, traits));
  std::vector<Channel> swapped = old;
  swapped[1].id = "xt:1|9";
  CHECK_FALSE(renamedEventSlots(old, swapped, traits));
  CHECK_FALSE(renamedEventSlots({}, {}, traits));
}

TEST_CASE("windowStart: the last half hour, in the given zone") {
  CHECK(windowStart(utc(2026, 10, 21, 12, 44) + 10, time::Zone::utc()) ==
        utc(2026, 10, 21, 12, 30));
  CHECK(windowStart(utc(2026, 10, 21, 12, 10), time::Zone::utc()) == utc(2026, 10, 21, 12, 0));
  // A +5:45 zone has its own half hours.
  time::Zone nepal = time::Zone::fixed(5 * 3600 + 45 * 60);
  CHECK(windowStart(utc(2026, 10, 21, 12, 10), nepal) == utc(2026, 10, 21, 11, 45));
}

TEST_CASE("bestListing: the other side must cover 15 minutes more to win") {
  time::Seconds start = utc(2026, 10, 21, 12, 0);
  std::vector<Program> server = {program("S1", start, 60), program("S2", start + 3600, 60)};
  // Outside covers 2h10m of the window: not enough to win.
  std::vector<Program> close = {program("O1", start, 130)};
  std::vector<Program> merged = bestListing(&server, &close, start);
  REQUIRE(merged.size() == 2);
  CHECK(merged[0].title == "S1");
  // 2h20m: outside wins; server programmes that clash by more than 5 minutes go.
  std::vector<Program> wide = {program("O1", start, 140)};
  merged = bestListing(&server, &wide, start);
  REQUIRE(merged.size() == 1);
  CHECK(merged[0].title == "O1");
  // Empty sides.
  CHECK(bestListing(&server, nullptr, start).size() == 2);
  CHECK(bestListing(nullptr, &wide, start).size() == 1);
  std::vector<Program> none;
  CHECK(bestListing(&none, &none, start).empty());
}

TEST_CASE("bestListing fills gaps and takes genre tags") {
  time::Seconds start = utc(2026, 10, 21, 12, 0);
  std::vector<Program> server = {program("Match", start, 60), program("Late", start + 7200, 60)};
  // Outside covers 135 minutes of the window, server 120: not 15 more, so
  // the server's listing stays first.
  std::vector<Program> outside = {
      program("Match (tagged)", start - 300, 65, guide::genre::kSports),  // more than half overlaps
      program("Gap show", start + 3600, 65),  // fills the hole; overlaps "Late" by 5 minutes only
      program("Clash", start + 7800, 10),     // overlaps "Late" by 10 minutes
  };
  std::vector<Program> merged = bestListing(&server, &outside, start);
  REQUIRE(merged.size() == 3);
  CHECK(merged[0].title == "Match");
  CHECK((merged[0].genres & guide::genre::kSports) != 0);  // tag from the other guide
  CHECK(merged[1].title == "Gap show");
  CHECK(merged[2].title == "Late");
  CHECK(merged[2].genres == 0);
}

TEST_CASE("listing: provider guide first, short guide and outside for gaps") {
  time::Seconds start = utc(2026, 10, 21, 12, 0);
  std::vector<guide::RawProgram> provider = {raw("Provider news", start, 60, guide::genre::kNews),
                                             raw("Old", start - 5 * 3600, 60)};
  std::vector<Program> shortGuide = {program("Short", start + 3600, 60)};
  std::vector<guide::RawProgram> outside = {raw("Outside late", start + 7200, 60)};
  std::vector<Program> list = listing("c1", &shortGuide, &provider, &outside, start);
  REQUIRE(list.size() == 3);
  CHECK(list[0].title == "Provider news");
  CHECK(list[0].id == "pg|c1|" + std::to_string(start / 60));
  CHECK_FALSE(list[0].fromOutsideGuide);
  CHECK(list[1].title == "Short");
  CHECK(list[2].title == "Outside late");
  CHECK(list[2].fromOutsideGuide);
  CHECK(listing("c1", nullptr, nullptr, nullptr, start).empty());

  std::set<std::string> ids = {"c1", "c2"};
  std::unordered_map<std::string, std::vector<Program>> server = {{"c1", shortGuide}};
  guide::SharedPrograms outsideById = {{"c1", std::make_shared<const std::vector<guide::RawProgram>>(outside)}};
  auto merged = mergedListings(ids, server, {}, outsideById, start);
  CHECK(merged.size() == 1);
  CHECK(merged.at("c1")->size() == 2);

  // Channels with the same outside schedule and nothing of their own share
  // one list; a channel with its own short guide gets its own.
  outsideById["c2"] = outsideById["c1"];
  outsideById["c3"] = outsideById["c1"];
  auto shared = mergedListings({"c1", "c2", "c3"}, server, {}, outsideById, start);
  REQUIRE(shared.size() == 3);
  CHECK(shared.at("c2").get() == shared.at("c3").get());
  CHECK(shared.at("c1").get() != shared.at("c2").get());
  CHECK(shared.at("c2")->size() == outside.size());
}

TEST_CASE("lineup: membership, guide order, browse list and groups") {
  std::vector<Channel> channels = {channel("1", "US: NBC 11"), channel("2", "US: CBS 2"),
                                   channel("3", "Channel 10"), channel("4", "Channel 2"),
                                   channel("5", "UK: Thing"),  channel("6", "Ghost")};
  channels[0].group = "Locals";
  channels[1].group = "Locals";
  channels[2].group = "Ent";
  channels[3].imageUrl = "https://example.invalid/logo.png";
  channels[4].isFavorite = true;
  auto info = [](const std::string& id, const std::string& name, LineupCategory category,
                 std::vector<std::string> members, std::optional<std::string> market) {
    EntryInfo e;
    e.id = id;
    e.name = name;
    e.category = category;
    e.members = std::move(members);
    e.market = std::move(market);
    e.virtualNumber = virtualNumber(name);
    return e;
  };
  LineupInput input;
  input.infos = {info("e|ch10", "Channel 10", LineupCategory::entertainment, {"xt:1|3"}, "US"),
                 info("e|nbc", "NBC 11", LineupCategory::local, {"xt:1|1"}, "US"),
                 info("e|ch2", "Channel 2", LineupCategory::entertainment, {"xt:1|4"}, "US"),
                 info("e|cbs", "CBS 2", LineupCategory::local, {"xt:1|2"}, "US"),
                 info("e|uk", "Thing", LineupCategory::international, {"xt:1|5"}, "UK"),
                 info("e|gone", "Gone", LineupCategory::news, {"xt:1|missing"}, "US")};
  input.channels = channels;
  for (size_t i = 0; i < channels.size(); ++i) input.indexByID[channels[i].id] = i;
  input.prefs.configured = true;
  input.prefs.requireGuide = true;
  input.prefs.added = {"e|ch2"};
  input.prefs.removed = {"e|ch10"};
  input.guideIDs = {"xt:1|3"};
  input.otherGroup = "Other";
  LineupOutput out = lineup(input);
  // Order: locals by number, then A-Z with numbers read as numbers.
  std::vector<std::string> ids;
  for (const Channel& c : out.entries) ids.push_back(c.id);
  CHECK(ids == std::vector<std::string>{"e|cbs", "e|nbc", "e|ch2", "e|ch10", "e|uk"});
  CHECK(out.entryIndex.at("e|ch2") == 2);
  CHECK(out.entries[2].imageUrl == "https://example.invalid/logo.png");
  CHECK(out.entries[2].shortName == "Channel 2");
  CHECK_FALSE(out.entries[2].number);
  // Added and favorite join; removed never does; locals need market call signs.
  CHECK(out.lineupIDs == std::set<std::string>{"e|ch2", "e|uk"});
  CHECK(out.lineupEntries.size() == 2);
  // Browse: lineup, favorites, and the chosen countries.
  CHECK(out.browseEntries.size() == 5);
  REQUIRE(out.groups.size() == 3);
  CHECK(out.groups[0] == std::make_pair(std::string("Locals"), 2));  // ties A-Z
  CHECK(out.groups[1] == std::make_pair(std::string("Other"), 2));
  CHECK(out.groups[2] == std::make_pair(std::string("Ent"), 1));
}
