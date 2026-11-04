// Build 132 benchmark, skipped by default (run with --no-skip -tc="bench:*"
// on a Release build): what other countries' sports channels cost.
//  - The provider's guide: a synthetic file of 6,000 channels and 48 hours
//    of programmes, read keeping 1,000 channels (before) and 1,190 (the 190
//    sports brands too): time, peak heap while reading and heap kept after.
//  - Live Events' rebuild from it: the listings read (listingChannels and
//    extractListings), the matcher (its "2 listings" lap and total) and
//    the board, without and with the 190.
// Every name is made up; nothing is written to disk.
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include "core/guide/provider.hpp"
#include "core/guide/xmltv.hpp"
#include "core/sports/board.hpp"
#include "core/sports/brands.hpp"
#include "core/sports/matcher.hpp"
#include "core/sports/store.hpp"
#include "core/util/time.hpp"
#include "doctest.h"

using namespace marigold;

namespace {

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

// Bytes in use on the heap (glibc); 0 elsewhere.
int64_t heapNow() {
#if defined(__GLIBC__)
  struct mallinfo2 m = mallinfo2();
  return static_cast<int64_t>(m.uordblks + m.hblkhd);
#else
  return 0;
#endif
}

constexpr int kFileChannels = 6000;
constexpr int kInScope = 1000;
constexpr int kBrands = 190;
constexpr int kHours = 48;

// 400 made-up towns (each with two clubs, as cities often have) and 20
// nicknames.
const char* const kHeads[] = {"Ash",  "Brook", "Cedar", "Dun",   "Elm",   "Fair", "Glen",  "Hart",  "Iron",  "Kings",
                              "Lark", "Marsh", "North", "Oak",   "Pine",  "Quarry", "Red", "Stone", "Thorn", "Wey"};
const char* const kTails[] = {"ford", "vale",  "field", "mouth", "bury",  "wick", "ton",   "haven", "moor",  "dale",
                              "gate", "holm",  "ley",   "port",  "stead", "wood", "cliff", "bridge", "by",   "combe"};
const char* const kNames[] = {"Gulls", "Hawks", "Rams", "Otters", "Elk", "Foxes", "Bears", "Herons", "Seals", "Wolves",
                              "Pines", "Owls", "Sharks", "Comets", "Bisons", "Lynx", "Ravens", "Falcons", "Tides", "Kings"};

std::string teamName(int n) {
  int town = n % 400;
  return std::string(kHeads[town % 20]) + kTails[town / 20] + " " + kNames[(n / 400 + town) % 20];
}

// Games: 200 soccer (on the sports brands and the sports channels), 100 NFL,
// 100 NBA, from now to two days on.
std::vector<sports::SportsGame> games(time::Seconds now) {
  std::vector<sports::SportsGame> out;
  for (int g = 0; g < 400; ++g) {
    sports::SportsGame game;
    std::string league = g < 200 ? "EPL" : g < 300 ? "NFL" : "NBA";
    game.id = league + "|" + std::to_string(g);
    game.league = g < 200 ? "Soccer" : league;
    game.leagueShort = league;
    game.homeFirst = g < 200;
    game.home.id = std::to_string(2 * g);
    game.home.name = teamName(2 * g);
    game.away.id = std::to_string(2 * g + 1);
    game.away.name = teamName(2 * g + 1);
    game.start = now / 3600 * 3600 + (g % 48) * 3600;
    out.push_back(std::move(game));
  }
  return out;
}

// A programme on channel `c` in hour `h`: a game every third hour on every
// fifth channel and on the brands (each channel writes it its own way), a
// show of its own otherwise.
std::string titleOf(int c, int h) {
  bool sports = c % 5 == 0 || (c >= kInScope && c < kInScope + kBrands);
  if (sports && h % 3 == 0) {
    int g = (c * 7 + h) % 400;
    if (c >= kInScope) g = (c + h) % 200;  // the brands show soccer
    static const char* const kWays[] = {"", "Live: ", "Football: ", "Matchday Live: ", "Premier Match: "};
    return kWays[c % 5 == 0 ? (c / 5) % 5 : c % 5] + teamName(2 * g) + (c % 2 ? " v " : " vs ") +
           teamName(2 * g + 1);
  }
  return "Harbor Evening Show " + std::to_string(c) + " Part " + std::to_string(h % 24);
}

std::string stamp(time::Seconds t) {
  time::Civil c = time::civilFromSeconds(t);
  char b[32];
  std::snprintf(b, sizeof b, "%04d%02d%02d%02d%02d00 +0000", c.year, c.month, c.day, c.hour, c.minute);
  return b;
}

std::string xmltv(time::Seconds now) {
  std::string xml = "<?xml version=\"1.0\"?>\n<tv>\n";
  xml.reserve(static_cast<size_t>(kFileChannels) * kHours * 330);
  for (int c = 0; c < kFileChannels; ++c)
    xml += "<channel id=\"ch" + std::to_string(c) + ".zz\"><display-name>Channel " + std::to_string(c) +
           "</display-name></channel>\n";
  time::Seconds hour = now / 3600 * 3600 - 3 * 3600;
  for (int c = 0; c < kFileChannels; ++c) {
    for (int h = 0; h < kHours + 3; ++h) {
      xml += "<programme start=\"" + stamp(hour + h * 3600) + "\" stop=\"" + stamp(hour + (h + 1) * 3600) +
             "\" channel=\"ch" + std::to_string(c) + ".zz\"><title>" + titleOf(c, h) +
             "</title><desc>Live coverage from the ground with the build-up, the match and reaction from both "
             "camps, with analysis from the studio panel (programme " +
             std::to_string(c) + "-" + std::to_string(h) + ").</desc><category>Sports</category></programme>\n";
    }
  }
  xml += "</tv>\n";
  return xml;
}

struct ReadResult {
  double ms = 0;
  int64_t peak = 0, kept = 0;
  size_t channels = 0;
  guide::provider::Saved saved;
};

ReadResult readGuide(const std::string& xml, int channels, time::Seconds now, bool trim) {
  std::vector<Channel> list;
  for (int c = 0; c < channels; ++c) {
    Channel ch;
    ch.id = "xt:1|" + std::to_string(c);
    ch.serverId = "xt:1";
    ch.name = "Channel " + std::to_string(c);
    ch.epgId = "ch" + std::to_string(c) + ".zz";
    list.push_back(std::move(ch));
  }
  // The brands are read for Live Events only (kept kEventsOnlyHours ahead).
  std::unordered_set<std::string> eventsOnly;
  for (int c = kInScope; trim && c < channels; ++c) eventsOnly.insert("xt:1|" + std::to_string(c));
  ReadResult out;
  int64_t base = heapNow();
  int64_t peak = base;
  Clock::time_point start = Clock::now();
  guide::provider::ReadExactFn read = [&](const auto& ids, guide::Window window, guide::provider::ReadFailure&) {
    guide::XmltvReader reader = guide::XmltvReader::exact(ids, window);
    for (size_t at = 0; at < xml.size(); at += 65536) {
      reader.feed(std::string_view(xml).substr(at, 65536));
      peak = std::max(peak, heapNow());
    }
    reader.finish();
    guide::provider::ExactRead r;
    r.programs = reader.result();
    peak = std::max(peak, heapNow());
    r.guideChannels = reader.guideChannelCount();
    r.programmes = reader.programmeCount();
    r.bytes = static_cast<int64_t>(xml.size());
    return std::optional<guide::provider::ExactRead>(std::move(r));
  };
  guide::provider::Outcome o = guide::provider::build(list, now, read, nullptr, &eventsOnly);
  out.ms = msSince(start);
  peak = std::max(peak, heapNow());
  out.peak = peak - base;
  out.kept = heapNow() - base;
  out.channels = o.saved.programs.size();
  out.saved = std::move(o.saved);
  return out;
}

double median(std::vector<double> v) {
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}

}  // namespace

TEST_CASE("bench: the provider's guide with other countries' sports channels" * doctest::skip()) {
  time::Seconds now = time::now();
  std::string xml = xmltv(now);
  MESSAGE("synthetic guide: " << kFileChannels << " channels, " << xml.size() / (1024 * 1024) << " MiB");
  for (int round = 0; round < 3; ++round) {
    for (int channels : {kInScope, kInScope + kBrands, -(kInScope + kBrands)}) {
      // Negative: the 190 kept the whole 48 hours (not trimmed).
      bool trim = channels > 0;
      ReadResult r = readGuide(xml, std::abs(channels), now, trim);
      CHECK(r.channels == static_cast<size_t>(std::abs(channels)));
      MESSAGE("provider guide, " << std::abs(channels) << " channels kept" << std::string(trim ? "" : " (48 h each)") << ": "
                                 << r.ms << " ms, peak heap +" << r.peak / 1024 << " KiB, kept +" << r.kept / 1024
                                 << " KiB");
    }
  }
  // Which channels are a sports brand: what the scope check adds per channel.
  std::vector<std::string> names;
  for (int c = 0; c < 28000; ++c) names.push_back("UK| Harbor Channel " + std::to_string(c) + " HD");
  Clock::time_point t = Clock::now();
  int brands = 0;
  for (const std::string& n : names) brands += sports::brands::isSportsBrand(n) ? 1 : 0;
  MESSAGE("brand test over 28,000 names: " << msSince(t) << " ms (" << brands << ")");
}

TEST_CASE("bench: Live Events' rebuild with other countries' sports channels" * doctest::skip()) {
  using namespace marigold::sports;
  time::Seconds now = time::now();
  std::vector<SportsGame> schedule = games(now);
  // The entries: the first 1,000 in scope (every fifth a sports channel,
  // the rest locals and others), then 190 brands of other countries.
  std::unordered_map<std::string, catalog::EntryInfo> infos;
  std::vector<Channel> entries;
  std::vector<std::string> browse;
  std::unordered_map<std::string, std::shared_ptr<const std::vector<Program>>> programs;
  const char* const brandNames[] = {"Sky Sports Main Event", "TNT Sports 1", "beIN SPORTS 1", "DAZN 1",
                                    "Sky Sport Bundesliga 1", "Canal+ Foot",  "Sport TV 1",    "TSN 1",
                                    "SSC Sport 1",           "SuperSport Grandstand"};
  const char* const markets[] = {"UK", "UK", "AR", "DE", "DE", "FR", "PT", "CA", "AR", "ZA"};
  for (int c = 0; c < kInScope + kBrands; ++c) {
    catalog::EntryInfo info;
    info.id = "e|" + std::to_string(c);
    bool brand = c >= kInScope;
    info.name = brand ? std::string(brandNames[c % 10]) + " " + std::to_string(c) : "Harbor Channel " + std::to_string(c);
    info.category = brand ? catalog::LineupCategory::international
                    : c % 5 == 0 ? catalog::LineupCategory::sports
                                 : catalog::LineupCategory::entertainment;
    info.market = brand ? std::string(markets[c % 10]) : std::string("US");
    Channel e;
    e.id = info.id;
    e.name = info.name;
    e.group = brand ? std::string(markets[c % 10]) + "| SPORTS" : std::string("US| ALL");
    entries.push_back(e);
    if (!brand) browse.push_back(info.id);
    auto list = std::make_shared<std::vector<Program>>();
    // What the engine's listings hold for a channel: from an hour ago, 15
    // hours (Store::providerListing's window).
    time::Seconds hour = now / 3600 * 3600 - 2 * 3600;
    for (int h = 1; h < 16; ++h) {
      Program p;
      p.title = titleOf(c, h);
      p.start = hour + h * 3600;
      p.end = p.start + 3600;
      p.genres = kGenreSports;
      list->push_back(std::move(p));
    }
    programs[info.id] = std::move(list);
    infos.emplace(info.id, std::move(info));
  }
  for (bool withBrands : {false, true}) {
    std::vector<double> read, total, lap, board;
    size_t listings = 0, sources = 0;
    for (int round = 0; round < 7; ++round) {
      Clock::time_point t = Clock::now();
      std::vector<std::string> abroad;
      if (withBrands) abroad = store::abroadChannels(entries, {}, browse, infos, {}, "Other");
      std::vector<store::ListingChannel> chans = store::listingChannels({}, browse, infos, StationMarkets(), abroad);
      std::vector<store::ListingInput> inputs;
      for (const store::ListingChannel& lc : chans) {
        store::ListingInput li;
        li.channel = lc;
        auto p = programs.find(lc.id);
        if (p != programs.end()) li.programs = p->second.get();
        inputs.push_back(li);
      }
      store::ExtractedListings extracted = store::extractListings(inputs, now);
      read.push_back(msSince(t));
      listings = extracted.listings.size();
      matcher::BuildInput in;
      in.games = schedule;
      in.listings = std::move(extracted.listings);
      in.now = now;
      in.zone = time::Zone::usEastern();
      t = Clock::now();
      auto built = matcher::build(in);
      total.push_back(msSince(t));
      REQUIRE(built);
      lap.push_back(built->stats.milliseconds["2 listings"]);
      sources = 0;
      for (const SportsEvent& e : built->events) sources += e.sources.size();
      board::Input b;
      b.events = built->events;
      b.countries = {"US"};
      b.now = now;
      b.zone = time::Zone::usEastern();
      b.channelFor = [](const EventSource& s) -> std::optional<board::ChannelLabel> {
        return board::ChannelLabel{s.channelID, s.channelID, s.channelID, {}};
      };
      t = Clock::now();
      board::Board made = board::build(b);
      board.push_back(msSince(t));
      CHECK_FALSE(made.tiles.empty());
    }
    MESSAGE(std::string(withBrands ? "with" : "without") << " the 190: listings read " << median(read) << " ms (" << listings
                                               << " game listings), matcher " << median(total) << " ms (listings lap "
                                               << median(lap) << " ms), board " << median(board) << " ms, "
                                               << sources << " sources");
  }
}
