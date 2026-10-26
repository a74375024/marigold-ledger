#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/report/channel_report.hpp"
#include "doctest.h"

using marigold::Channel;
using marigold::Program;
using marigold::catalog::CatalogResult;
using marigold::catalog::ChannelKind;
using marigold::catalog::ChannelTraits;
using marigold::catalog::EntryInfo;
using marigold::catalog::LineupCategory;
using marigold::guide::RawProgram;
using namespace marigold::report;
namespace provider = marigold::guide::provider;
namespace mtime = marigold::time;

namespace {

// 2026-10-26 18:00 UTC, a half hour.
constexpr mtime::Seconds kStart = 1793037600;
constexpr int kLow = static_cast<int>(kStart / 60);

// Splits CSV text into rows of cells (quoted cells, "" for a quote).
std::vector<std::vector<std::string>> parseCsv(const std::string& text) {
  std::vector<std::vector<std::string>> rows;
  std::vector<std::string> row;
  std::string cell;
  bool quoted = false;
  for (size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (quoted) {
      if (c == '"' && i + 1 < text.size() && text[i + 1] == '"') {
        cell += '"';
        ++i;
      } else if (c == '"') {
        quoted = false;
      } else {
        cell += c;
      }
    } else if (c == '"') {
      quoted = true;
    } else if (c == ',') {
      row.push_back(cell);
      cell.clear();
    } else if (c == '\n') {
      row.push_back(cell);
      cell.clear();
      rows.push_back(row);
      row.clear();
    } else {
      cell += c;
    }
  }
  return rows;
}

Channel channel(const std::string& id, const std::string& serverId, const std::string& name,
                std::optional<std::string> group = std::nullopt) {
  Channel c;
  c.id = id;
  c.serverId = serverId;
  c.itemId = id;
  c.name = name;
  c.group = group;
  return c;
}

Program program(const std::string& id, mtime::Seconds start, mtime::Seconds end, const std::string& title,
                uint8_t genres = 0) {
  Program p;
  p.id = id;
  p.start = start;
  p.end = end;
  p.title = title;
  p.genres = genres;
  return p;
}

RawProgram raw(int s, int m, int f = 0) {
  RawProgram p;
  p.s = s;
  p.m = m;
  p.t = "Show";
  p.f = f;
  return p;
}

// One row as column name -> value.
std::map<std::string, std::string> rowFor(const std::vector<std::vector<std::string>>& rows,
                                          const std::string& name) {
  const auto& header = rows.at(0);
  for (size_t r = 1; r < rows.size(); ++r) {
    if (rows[r].size() != header.size()) continue;
    std::map<std::string, std::string> out;
    for (size_t i = 0; i < header.size(); ++i) out[header[i]] = rows[r][i];
    if (out["name"] == name) return out;
  }
  return {};
}

struct Fixture {
  std::vector<Channel> channels;
  CatalogResult catalog;
  std::set<std::string> lineup, hidden;
  std::unordered_map<std::string, ServerGuideState> state;
  std::unordered_map<std::string, std::vector<Program>> server, programs;
  marigold::guide::ProgramsById outside, providerMatches;
  // What the report reads: the lists above, shared.
  mutable std::unordered_map<std::string, std::shared_ptr<const std::vector<Program>>> sharedPrograms;
  mutable marigold::guide::SharedPrograms sharedOutside, sharedProvider;
  std::unordered_map<std::string, std::pair<std::string, std::string>> logos;
  std::map<std::string, std::string> outsideNotes, providerNotes, names;
  std::map<std::string, provider::Status> providerStates;
  std::set<std::string> countries{"US"};
  marigold::guide::StationsByNetwork stations;

  ChannelReportInput input() const {
    ChannelReportInput in;
    in.channels = &channels;
    in.catalog = &catalog;
    in.lineupIds = &lineup;
    in.hiddenGroups = &hidden;
    in.guideState = &state;
    in.serverPrograms = &server;
    sharedPrograms.clear();
    for (const auto& [id, list] : programs) sharedPrograms[id] = std::make_shared<const std::vector<Program>>(list);
    sharedOutside = marigold::guide::share(marigold::guide::ProgramsById(outside));
    sharedProvider = marigold::guide::share(marigold::guide::ProgramsById(providerMatches));
    in.programs = &sharedPrograms;
    in.outsideMatches = &sharedOutside;
    in.outsideNotes = &outsideNotes;
    in.providerMatches = &sharedProvider;
    in.logos = &logos;
    in.providerNotes = &providerNotes;
    in.providerStates = &providerStates;
    in.providerCountries = &countries;
    in.providerNames = &names;
    in.stations = &stations;
    in.now = kStart + 600;
    in.windowStart = kStart;
    in.zone = mtime::Zone::utc();
    return in;
  }
};

Fixture sample() {
  Fixture f;
  // An Xtream local with a call sign, in My Lineup.
  Channel nbc = channel("xt:a|1", "xt:a", "US: NBC 4 WNBC", std::string("US| Locals"));
  nbc.shortName = "NBC 4 WNBC";
  nbc.number = std::string("104");
  nbc.tags = {"US Locals", "HD"};
  nbc.epgId = std::string("wnbc.us");
  nbc.isFavorite = true;
  f.channels.push_back(nbc);
  // A network-and-city name resolved through the station directory.
  Channel fox = channel("xt:a|2", "xt:a", "FOX 2 Capital City", std::string("US"));
  fox.epgId = std::string("fox2.us");
  f.channels.push_back(fox);
  // An event slot.
  Channel ppv = channel("xt:a|3", "xt:a", "US (ESPN+ 07) | Team A vs Team B 7:30 PM ET",
                        std::string("US Sports"));
  ppv.epgId = std::string("espnplus.us");
  f.channels.push_back(ppv);
  // An Emby channel in a hidden group.
  f.channels.push_back(channel("jf:b|9", "jf:b", "Thing One", std::string("Hidden Stuff")));
  // No group at all: the "Other" group.
  f.channels.push_back(channel("m3u:c|5", "m3u:c", "Loop Channel 24/7"));

  ChannelTraits t;
  t.category = LineupCategory::local;
  t.kind = ChannelKind::channel;
  t.market = std::string("US");
  t.core = std::string("NBC");
  t.scope = "local";
  f.catalog.traits["xt:a|1"] = t;
  ChannelTraits e;
  e.category = LineupCategory::sports;
  e.kind = ChannelKind::event;
  e.familyKey = std::string("us (espn+ #)");
  e.eventText = std::string("Team A vs Team B 7:30 PM ET");
  f.catalog.traits["xt:a|3"] = e;
  EntryInfo entry;
  entry.id = "e|nbc4";
  entry.name = "NBC 4 New York";
  entry.members = {"xt:a|1"};
  f.catalog.entries.push_back(entry);
  f.catalog.entryOf["xt:a|1"] = "e|nbc4";
  f.lineup = {"e|nbc4"};
  f.hidden = {"Hidden Stuff", "Other"};

  f.state["xt:a|1"] = ServerGuideState::Loaded;
  f.state["xt:a|2"] = ServerGuideState::Failed;
  f.state["xt:a|3"] = ServerGuideState::Loaded;  // loaded, nothing in it
  f.state["jf:b|9"] = ServerGuideState::Loading;
  f.server["xt:a|1"] = {
      program("xt|1|a", kStart - 3600, kStart, "Earlier"),                  // before the window
      program("xt|1|b", kStart, kStart + 1800, "News at 6", marigold::kGenreNews),
      program("xt|1|c", kStart + 1800, kStart + 3600, "Quiz"),
      program("xt|1|d", kStart + 3 * 3600, kStart + 4 * 3600, "Later"),    // after it
  };
  Program now = program("pg|xt:a|1|1", kStart, kStart + 1800, "News at 6",
                        marigold::kGenreNews | marigold::kGenreSports);
  now.episodeTitle = std::string("Tonight");
  now.overview = std::string("Line one\nmore at https://example.invalid/x end");
  f.programs["xt:a|1"] = {now};
  Program outsideNow = program("og|xt:a|2|1", kStart, kStart + 3600, "Movie Night");
  outsideNow.fromOutsideGuide = true;
  f.programs["xt:a|2"] = {outsideNow};
  f.programs["jf:b|9"] = {program("jf|9|1", kStart, kStart + 3600, "Thing")};
  f.programs["m3u:c|5"] = {program("m3u|5|1", kStart + 7200, kStart + 9000, "Not yet")};

  f.outside["xt:a|1"] = {raw(kLow - 60, 30), raw(kLow, 30, 4), raw(kLow + 30, 30), raw(kLow + 200, 40)};
  f.outsideNotes["xt:a|1"] = "US locals: WNBC (by call sign WNBC)";
  f.outside["xt:a|2"] = {raw(kLow, 60, 1)};
  f.providerMatches["xt:a|1"] = {raw(kLow, 30, 2), raw(kLow + 30, 30)};
  f.providerMatches["jf:b|9"] = {raw(kLow, 30)};  // ignored: not Xtream
  f.names = {{"xt:a", "Provider A"}, {"jf:b", "Home Server"}};
  f.providerStates["xt:a"] = provider::Status{provider::Status::Kind::ready, "", 1, 3, 10, kStart};
  f.stations = marigold::guide::groupByNetwork({{"KCCC", "FOX", {"capital", "city"}, "2"}});
  return f;
}

}  // namespace

TEST_CASE("channel report: header") {
  const auto& columns = channelReportColumns();
  CHECK(columns.size() == 38);
  CHECK(columns.front() == "number");
  CHECK(columns.back() == "logo_status");
  std::string csv = channelReportCSV(ChannelReportInput());
  CHECK(csv ==
        "number,name,group,tags,hidden_group,favorite,callsign_in_name,station_lookup,match_key,"
        "server_guide,server_programs_next_3h,outside_listing,outside_programs_next_3h,now_title,"
        "now_source,entry,category,in_my_lineup,event_family,event_text,provider,country,guide_id,"
        "provider_guide,provider_guide_programs_next_3h,kind,market,core_network,scope,"
        "server_genre_tagged_next_3h,outside_genre_tagged_next_3h,"
        "provider_guide_genre_tagged_next_3h,now_genres,now_subtitle,now_description,"
        "outside_listings_end,logo_sources,logo_status\n");
}

TEST_CASE("channel report: one row per channel with the Swift rules") {
  Fixture f = sample();
  for (const auto& e : f.catalog.entries) f.logos[e.id] = {"logo set, provider", "shown"};
  std::string csv = channelReportCSV(f.input());
  auto rows = parseCsv(csv);
  REQUIRE(rows.size() == 6);
  for (const auto& r : rows) CHECK(r.size() == 38);
  CHECK(csv.back() == '\n');

  auto nbc = rowFor(rows, "US: NBC 4 WNBC");
  CHECK(nbc["number"] == "104");
  CHECK(nbc["logo_sources"] == "logo set, provider");
  CHECK(nbc["logo_status"] == "shown");
  CHECK(nbc["group"] == "US| Locals");
  CHECK(nbc["tags"] == "US Locals | HD");
  CHECK(nbc["hidden_group"] == "");
  CHECK(nbc["favorite"] == "yes");
  CHECK(nbc["callsign_in_name"] == "WNBC");  // a locals group: strong context
  CHECK(nbc["station_lookup"] == "");          // only looked up without a call sign
  CHECK(nbc["match_key"] == marigold::guide::normalize("NBC 4 WNBC"));
  CHECK(nbc["server_guide"] == "yes");
  CHECK(nbc["server_programs_next_3h"] == "2");
  CHECK(nbc["outside_listing"] == "US locals: WNBC (by call sign WNBC)");
  CHECK(nbc["outside_programs_next_3h"] == "2");
  CHECK(nbc["now_title"] == "News at 6");
  CHECK(nbc["now_source"] == "provider guide");
  CHECK(nbc["entry"] == "NBC 4 New York");
  CHECK(nbc["category"] == "local");
  CHECK(nbc["in_my_lineup"] == "yes");
  CHECK(nbc["event_family"] == "");
  CHECK(nbc["provider"] == "Provider A");
  CHECK(nbc["country"] == "US");
  CHECK(nbc["guide_id"] == "wnbc.us");
  CHECK(nbc["provider_guide"] == "not in provider guide");
  CHECK(nbc["provider_guide_programs_next_3h"] == "2");
  CHECK(nbc["kind"] == "channel");
  CHECK(nbc["market"] == "US");
  CHECK(nbc["core_network"] == "NBC");
  CHECK(nbc["scope"] == "local");
  CHECK(nbc["server_genre_tagged_next_3h"] == "1");
  CHECK(nbc["outside_genre_tagged_next_3h"] == "1");
  CHECK(nbc["provider_guide_genre_tagged_next_3h"] == "1");
  CHECK(nbc["now_genres"] == "sports news");
  CHECK(nbc["now_subtitle"] == "Tonight");
  CHECK(nbc["now_description"] == "Line one more at  end");
  // The last outside programme ends at kLow + 240 minutes = 22:00 UTC.
  CHECK(nbc["outside_listings_end"] == "2026-10-26 22:00");

  auto fox = rowFor(rows, "FOX 2 Capital City");
  CHECK(fox["callsign_in_name"] == "");
  CHECK(fox["station_lookup"] == "KCCC");
  CHECK(fox["server_guide"] == "failed");
  CHECK(fox["outside_listing"] == "matched");  // matched, no note
  CHECK(fox["now_source"] == "outside");
  CHECK(fox["now_genres"] == "");
  CHECK(fox["outside_genre_tagged_next_3h"] == "1");
  CHECK(fox["category"] == "");  // no traits
  CHECK(fox["scope"] == "");
  CHECK(fox["provider_guide_programs_next_3h"] == "0");
  CHECK(fox["outside_listings_end"] == "2026-10-26 19:00");

  auto ppv = rowFor(rows, "US (ESPN+ 07) | Team A vs Team B 7:30 PM ET");
  CHECK(ppv["server_guide"] == "none");
  CHECK(ppv["provider_guide"] == "event feed");
  CHECK(ppv["event_family"] == "us (espn+ #)");
  CHECK(ppv["event_text"] == "Team A vs Team B 7:30 PM ET");
  CHECK(ppv["kind"] == "event");
  CHECK(ppv["outside_listing"] == "no match");
  CHECK(ppv["now_title"] == "");

  auto emby = rowFor(rows, "Thing One");
  CHECK(emby["server_guide"] == "loading");
  CHECK(emby["hidden_group"] == "yes");
  CHECK(emby["now_source"] == "server");
  CHECK(emby["provider"] == "Home Server");
  CHECK(emby["provider_guide"] == "");
  CHECK(emby["provider_guide_programs_next_3h"] == "");
  CHECK(emby["provider_guide_genre_tagged_next_3h"] == "");
  CHECK(emby["country"] == "");

  auto loop = rowFor(rows, "Loop Channel 24/7");
  CHECK(loop["hidden_group"] == "yes");  // no group: "Other"
  CHECK(loop["server_guide"] == "not checked");
  CHECK(loop["now_title"] == "");  // its programme hasn't started
  CHECK(loop["provider"] == "");
}

TEST_CASE("channel report: provider guide notes") {
  Fixture f = sample();
  f.channels.resize(2);
  Channel noId = channel("xt:a|7", "xt:a", "US: Plain HD", std::string("US"));
  Channel uk = channel("xt:a|8", "xt:a", "UK| Thing Two", std::string("UK| Entertainment"));
  uk.epgId = std::string("thing2.uk");
  Channel shortGuide = channel("xt:z|1", "xt:z", "US: Other Provider", std::string("US"));
  shortGuide.epgId = std::string("other.us");
  f.channels.insert(f.channels.end(), {noId, uk, shortGuide});
  f.providerNotes["xt:a|2"] = "no programmes for fox2.us";
  f.programs["xt:z|1"] = {program("xt|z|1", kStart, kStart + 600 + 1, "Now")};
  auto rows = parseCsv(channelReportCSV(f.input()));
  CHECK(rowFor(rows, "FOX 2 Capital City")["provider_guide"] == "no programmes for fox2.us");
  CHECK(rowFor(rows, "US: Plain HD")["provider_guide"] == "no guide id");
  CHECK(rowFor(rows, "UK| Thing Two")["provider_guide"] == "country not selected");
  CHECK(rowFor(rows, "US: Other Provider")["provider_guide"] == "provider guide not loaded");
  CHECK(rowFor(rows, "US: Other Provider")["now_source"] == "provider short guide");

  f.providerStates["xt:z"] = provider::Status{provider::Status::Kind::updating, "", 0, 0, 0, 0};
  CHECK(rowFor(parseCsv(channelReportCSV(f.input())), "US: Other Provider")["provider_guide"] ==
        "provider guide loading");
  f.providerStates["xt:z"] =
      provider::Status{provider::Status::Kind::failed, "the server answered 404", 0, 0, 0, 0};
  CHECK(rowFor(parseCsv(channelReportCSV(f.input())), "US: Other Provider")["provider_guide"] ==
        "provider guide: the server answered 404");
  f.providerStates["xt:z"] =
      provider::Status{provider::Status::Kind::unavailable, "no full guide", 0, 0, 0, 0};
  CHECK(rowFor(parseCsv(channelReportCSV(f.input())), "US: Other Provider")["provider_guide"] ==
        "provider guide: no full guide");
}

TEST_CASE("channel report: no links, logins or server addresses") {
  Fixture f = sample();
  Channel leaky = channel("xt:a|66", "xt:a", "Bad http://example.invalid:8080/live/someone/secret/66.ts",
                          std::string("US"));
  leaky.tags = {"see token=abc123", "provider.example.invalid"};
  leaky.epgId = std::string("x.us");
  f.channels.push_back(leaky);
  f.channels.push_back(channel("xt:a|67", "xt:a", "provider.example.invalid Sports", std::string("US")));
  f.providerStates["xt:a"] = provider::Status{
      provider::Status::Kind::failed, "couldn't reach https://provider.example.invalid:8080/xmltv.php", 0, 0, 0, 0};
  f.names["xt:a"] = "PROVIDER.example.invalid";
  Program p = program("pg|xt:a|66|1", kStart, kStart + 3600, "Show at provider.example.invalid");
  p.overview = std::string("Stream: rtmp://example.invalid/x and more");
  f.programs["xt:a|66"] = {p};
  ChannelReportInput in = f.input();
  in.privateWords = {"provider.example.invalid", "", "ab"};
  std::string csv = channelReportCSV(in);
  CHECK(csv.find("example.invalid") == std::string::npos);
  CHECK(csv.find("someone") == std::string::npos);
  CHECK(csv.find("abc123") == std::string::npos);
  CHECK(csv.find("://") == std::string::npos);
  CHECK(csv.find("providerexample") == std::string::npos);
  CHECK(csv.find("httpexample") == std::string::npos);
  auto rows = parseCsv(csv);
  auto row = rowFor(rows, "Bad [link]");
  CHECK(row["tags"] == "see token=*** | [hidden]");
  CHECK(row["provider"] == "[hidden]");
  CHECK(row["now_title"] == "Show at [hidden]");
  CHECK(row["now_description"] == "Stream: [link] and more");
  // Short words are not blanked ("ab" would hit ordinary names).
  CHECK(rowFor(rows, "FOX 2 Capital City")["name"] == "FOX 2 Capital City");
}

TEST_CASE("channel report: description cut at 160 characters, quotes doubled") {
  Fixture f = sample();
  f.channels.resize(1);
  std::string longText;
  for (int i = 0; i < 100; ++i) longText += "\xC3\xA9";  // 100 two-byte characters
  longText += std::string(100, 'z');
  f.programs["xt:a|1"][0].overview = longText;
  f.programs["xt:a|1"][0].title = "The \"Big\" Game";
  std::string csv = channelReportCSV(f.input());
  CHECK(csv.find("\"The \"\"Big\"\" Game\"") != std::string::npos);
  auto row = rowFor(parseCsv(csv), "US: NBC 4 WNBC");
  std::string expected;
  for (int i = 0; i < 100; ++i) expected += "\xC3\xA9";
  expected += std::string(60, 'z');
  CHECK(row["now_description"] == expected);
}

TEST_CASE("channel report: progress every 2000 rows") {
  std::vector<Channel> many;
  for (int i = 0; i < 4500; ++i)
    many.push_back(channel("m3u:c|" + std::to_string(i), "m3u:c", "Channel " + std::to_string(i)));
  ChannelReportInput in;
  in.channels = &many;
  in.windowStart = kStart;
  in.now = kStart;
  std::vector<std::pair<int, int>> calls;
  std::string csv = channelReportCSV(in, [&](int done, int total) { calls.emplace_back(done, total); });
  CHECK(calls == std::vector<std::pair<int, int>>{{2000, 4500}, {4000, 4500}});
  CHECK(parseCsv(csv).size() == 4501);
}

TEST_CASE("channel report: which guides to check first") {
  CatalogResult cat;
  EntryInfo a;
  a.id = "e|a";
  a.members = {"xt:a|1", "xt:a|2"};
  EntryInfo b;
  b.id = "e|b";
  b.members = {"xt:a|3"};
  EntryInfo empty;
  empty.id = "e|empty";
  EntryInfo dup;
  dup.id = "e|dup";
  dup.members = {"xt:a|1"};
  cat.entries = {a, b, empty, dup};
  ChannelTraits event;
  event.familyKey = std::string("us (ppv #)");
  cat.traits["xt:a|9"] = event;
  std::set<std::string> raw = {"xt:a|9", "xt:a|10", "xt:a|11"};
  auto isRaw = [&](const std::string& id) { return raw.count(id) > 0; };
  auto covered = [](const std::string& id) { return id == "xt:a|3"; };
  std::vector<std::string> order = {"e|a", "e|b", "e|empty", "xt:a|9", "e|dup", "xt:a|10", "nope", "xt:a|11"};
  CHECK(guideCheckIds(order, cat, isRaw, covered) == std::vector<std::string>{"xt:a|1", "xt:a|10", "xt:a|11"});
  CHECK(guideCheckIds(order, cat, isRaw, covered, 2) == std::vector<std::string>{"xt:a|1", "xt:a|10"});
  CHECK(guideCheckIds(order, cat, nullptr, nullptr) == std::vector<std::string>{"xt:a|1", "xt:a|3"});
  CHECK(kCheckLimit == 2500);
}

TEST_CASE("channel report: provider guide covers the window") {
  CHECK(covers({raw(kLow - 30, 31)}, kStart));
  CHECK_FALSE(covers({raw(kLow - 30, 30)}, kStart));
  CHECK(covers({raw(kLow + 179, 10)}, kStart));
  CHECK_FALSE(covers({raw(kLow + 180, 10)}, kStart));
  CHECK_FALSE(covers({}, kStart));
}
