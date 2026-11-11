// Build 153: which schedule a guide row shows (guide/choice.hpp): channel
// identity (another reference channel, station or country is set aside,
// an unknown one kept), coast (labels, lining schedules up at ±3 h, the
// shift to the row's coast), distinct schedules counted by source, the
// viewer's pick, and the outside build keeping countries and schedules
// apart. Guide channel names are real networks (the reference data knows
// them); every schedule is made up.
#include <memory>
#include <string>
#include <vector>

#include "core/catalog/reference.hpp"
#include "core/guide/choice.hpp"
#include "core/guide/outside.hpp"
#include "core/guide/xmltv.hpp"
#include "doctest.h"
#include "guide/xmltv_helpers.hpp"

using namespace marigold;
using namespace marigold::guide;
using namespace marigold::guide::choice;

namespace {

const time::Seconds kNowSec = time::secondsFromCivil({2026, 11, 11, 18, 0, 0});
const int kNow = static_cast<int>(kNowSec / 60);

// Hour-long programmes "<prefix> 0", "<prefix> 1"... from `from`.
Schedule hourly(const std::string& prefix, int from, int count, bool describe = false) {
  std::vector<RawProgram> list;
  for (int i = 0; i < count; ++i) {
    RawProgram p;
    p.s = from + i * 60;
    p.m = 60;
    p.t = prefix + " " + std::to_string(i);
    if (describe) p.d = Text("About " + p.t);
    list.push_back(std::move(p));
  }
  return std::make_shared<const std::vector<RawProgram>>(std::move(list));
}

// The same programmes `minutes` later (a West feed of an East schedule).
Schedule moved(const Schedule& s, int minutes) {
  std::vector<RawProgram> list = *s;
  for (RawProgram& p : list) p.s += minutes;
  return std::make_shared<const std::vector<RawProgram>>(std::move(list));
}

Candidate provider(const std::string& id, const std::string& name, Schedule raw,
                   const std::string& server = "xt:1") {
  Candidate c;
  c.kind = "provider guide";
  c.source = "pg:" + server;
  c.key = c.source + "|" + id;
  c.label = c.kind + " [" + id + "]";
  c.name = name;
  c.guideId = id;
  c.channelId = server + ":" + id;
  c.raw = std::move(raw);
  return c;
}

Candidate outside(const std::string& source, const std::string& sourceId, const std::string& country,
                  const std::string& name, Schedule raw, int shifted = 0) {
  Candidate c;
  c.outside = true;
  c.kind = "outside " + source;
  c.source = "og:" + sourceId;
  c.key = c.source + "|" + name;
  c.label = c.kind + " \"" + name + "\"";
  c.name = name;
  c.country = country;
  c.shifted = shifted;
  c.channelId = "o:" + name;
  c.raw = std::move(raw);
  return c;
}

Row row(const std::string& id) { return rowOf(id, std::string("US")); }

bool rejected(const Decision& d, const std::vector<Candidate>& cands, const std::string& key) {
  for (const Option& o : d.options)
    if (cands[o.index].key == key) return o.rejected;
  return false;
}

const Program* onAt(const std::vector<Program>& list, time::Seconds t) {
  for (const Program& p : list)
    if (p.isOn(t)) return &p;
  return nullptr;
}

bool haveReference() { return !catalog::reference::builtIn().empty(); }

}  // namespace

TEST_CASE("guide choice: names and ids read through the reference data") {
  if (!haveReference()) return;
  CHECK(identifyName("US: DISCOVERY WEST HD").ref == "discovery-channel");
  CHECK(identifyName("US: DISCOVERY WEST HD").coast == Coast::west);
  CHECK(identifyName("HBO East").coast == Coast::east);
  CHECK(identifyName("UK: Discovery HD").country == "UK");
  CHECK(identifyName("WCBS-DT").callsign == "WCBS");
  CHECK(identifyName("TimeShift 18").ref.empty());
  // The id picks the listings: a station's call sign, whole or at its end.
  CHECK(identifyId("WCBS.us").callsign == "WCBS");
  CHECK(identifyId("nbc30wvit.us").callsign == "WVIT");
  CHECK(identifyId("nyclifewnye.us").callsign == "WNYE");
  CHECK(identifyId("TheWeatherNetwork.ca").country == "CA");
  CHECK(identifyId("AMC.us").ref == "amc");
  CHECK(identifyId("historychannel.us").ref == "history");
  CHECK(identifyId("cbssportsnetworkusa.us").ref == "cbs-sports-network");
  // A sub-channel is not its station; a free app's channel not the network.
  CHECK(identifyId("WKMGDT2.us").callsign.empty());
  CHECK(identifyId("plutospiketv.us").ref != "paramount-network");

  Row r = rowOf("e|r:discovery-channel~west", std::nullopt);
  CHECK(r.ref == "discovery-channel");
  CHECK(r.west);
  CHECK(r.known);
  CHECK(r.country == "US");
  Row s = rowOf("e|cs:KNBC|NBC", std::nullopt);
  CHECK(s.callsign == "KNBC");
  CHECK_FALSE(s.west);
}

TEST_CASE("guide choice: another channel is set aside, an unknown one kept") {
  if (!haveReference()) return;
  Schedule good = hourly("Pawn Stars", kNow - 120, 30);
  Schedule nycLife = hourly("City Desk", kNow - 120, 30);
  Schedule unknown = hourly("Pawn Stars", kNow - 120, 30);
  std::vector<Candidate> cands = {
      provider("nyclifewnye.us", "|US| HISTORY", nycLife),
      provider("TS", "TimeShift 18", unknown, "xt:2"),
      outside("US national", "us-national", "US", "History HD", good),
  };
  Decision d = decide(row("e|r:history"), cands, kNow, kNowSec);
  CHECK(rejected(d, cands, cands[0].key));
  CHECK_FALSE(rejected(d, cands, cands[1].key));
  CHECK(d.rejected.find("other channel: nyclifewnye.us") != std::string::npos);
  // The unknown one agrees with the outside listing: one schedule, two sources.
  REQUIRE(d.chosen >= 0);
  CHECK(cands[static_cast<size_t>(d.chosen)].key != cands[0].key);
  CHECK(d.why.find("agrees with 1 other") != std::string::npos);
  std::vector<Program> list = listing(d, cands, kNowSec);
  REQUIRE(onAt(list, kNowSec));
  CHECK(onAt(list, kNowSec)->title == "Pawn Stars 2");

  // A station row: another station's guide id is set aside.
  std::vector<Candidate> local = {provider("nbc30wvit.us", "|US| NBC (KNBC)", hourly("CT Live", kNow - 60, 20)),
                                  outside("US locals", "us-locals", "US", "KNBC-DT", hourly("NBC4 News", kNow - 60, 20))};
  Decision k = decide(row("e|cs:KNBC|NBC"), local, kNow, kNowSec);
  CHECK(rejected(k, local, local[0].key));
  CHECK(k.rejected.find("other station") != std::string::npos);
  REQUIRE(k.chosen == 1);
  CHECK(k.why.rfind("outside US locals", 0) == 0);

  // A row whose only schedule is another channel's shows nothing.
  std::vector<Candidate> only = {provider("WCBS.us", "|US| CBS NEWS", hourly("The Price Is Right", kNow, 10))};
  Decision none = decide(row("e|r:cbs-news-24-7"), only, kNow, kNowSec);
  CHECK(none.chosen == -1);
  CHECK(none.options.size() == 1);
  CHECK(none.options.front().rejected);
}

TEST_CASE("guide choice: other countries") {
  if (!haveReference()) return;
  Schedule us = hourly("Gold Rush", kNow - 60, 20);
  Schedule uk = hourly("Wheeler Dealers", kNow - 60, 20);
  // A reference channel is the US one: the UK schedule never shows, even alone.
  std::vector<Candidate> alone = {outside("UK", "uk", "UK", "Discovery HD", uk)};
  Decision a = decide(row("e|r:discovery-channel"), alone, kNow, kNowSec);
  CHECK(a.chosen == -1);
  CHECK(a.rejected.find("other country: UK") != std::string::npos);
  // The provider's guide id says Canada (The Weather Channel's copy).
  std::vector<Candidate> weather = {provider("TheWeatherNetwork.ca", "CA EN: WEATHER NETWORK HD", uk),
                                    outside("US national", "us-national", "US", "The Weather Channel", us)};
  Decision w = decide(row("e|r:the-weather-channel"), weather, kNow, kNowSec);
  CHECK(w.chosen == 1);
  CHECK(rejected(w, weather, weather[0].key));
  // A row the reference doesn't know: another country only when a schedule
  // of its own country is there.
  std::vector<Candidate> unknownRow = {outside("UK", "uk", "UK", "Lighthouse Plus", uk)};
  CHECK(decide(row("e|n:US:lighthouseplus"), unknownRow, kNow, kNowSec).chosen == 0);
  unknownRow.push_back(outside("US national", "us-national", "US", "Lighthouse Plus HD", us));
  Decision both = decide(row("e|n:US:lighthouseplus"), unknownRow, kNow, kNowSec);
  CHECK(both.chosen == 1);
  CHECK(rejected(both, unknownRow, unknownRow[0].key));
}

TEST_CASE("guide choice: coast by lining schedules up, and the shift to the row's coast") {
  if (!haveReference()) return;
  Schedule east = hourly("Afraid", kNow - 240, 40, true);
  Schedule west = moved(east, 180);
  SUBCASE("a West label on East times: lined up with the trusted listing, no shift") {
    std::vector<Candidate> cands = {provider("DiscoveryChannel.us", "US: DISCOVERY WEST HD", east),
                                    outside("US national", "us-national", "US", "Discovery Channel HD",
                                            hourly("Afraid", kNow - 240, 40))};
    Decision d = decide(row("e|r:discovery-channel"), cands, kNow, kNowSec);
    REQUIRE(d.chosen == 0);  // the fuller one (descriptions)
    CHECK(d.shift == 0);
    CHECK(d.why.find("agrees with 1 other") != std::string::npos);
    CHECK(d.why.find("West label but East times") != std::string::npos);
    CHECK(listing(d, cands, kNowSec).size() > 10);
    CHECK(onAt(listing(d, cands, kNowSec), kNowSec)->title == "Afraid 4");
  }
  SUBCASE("a West label on West times: moved 3 h back for an East row") {
    std::vector<Candidate> cands = {provider("DiscoveryChannel.us", "US: DISCOVERY WEST HD", west),
                                    outside("US national", "us-national", "US", "Discovery Channel HD",
                                            hourly("Afraid", kNow - 240, 40))};
    Decision d = decide(row("e|r:discovery-channel"), cands, kNow, kNowSec);
    REQUIRE(d.chosen == 0);
    CHECK(d.shift == -180);
    CHECK(d.fill.size() == 1);
    CHECK(onAt(listing(d, cands, kNowSec), kNowSec)->title == "Afraid 4");
  }
  SUBCASE("a West row with only East listings: 3 h later") {
    std::vector<Candidate> cands = {outside("US national", "us-national", "US", "HBO East", east)};
    Decision d = decide(row("e|r:hbo~west"), cands, kNow, kNowSec);
    REQUIRE(d.chosen == 0);
    CHECK(d.shift == 180);
    CHECK(d.why.find("shifted +3 h for West") != std::string::npos);
    // At 6 pm the West feed shows what the East one showed at 3 pm.
    CHECK(onAt(listing(d, cands, kNowSec), kNowSec)->title == "Afraid 1");
  }
  SUBCASE("an East row with only a West schedule: 3 h earlier") {
    std::vector<Candidate> cands = {provider("HBOWest.us", "US: HBO WEST HD", west)};
    Decision d = decide(row("e|r:hbo"), cands, kNow, kNowSec);
    REQUIRE(d.chosen == 0);
    CHECK(d.shift == -180);
    CHECK(d.why.find("shifted \xE2\x88\x92" "3 h for East") != std::string::npos);
    CHECK(onAt(listing(d, cands, kNowSec), kNowSec)->title == "Afraid 4");
  }
  SUBCASE("an outside East listing made West by the build is West already") {
    std::vector<Candidate> cands = {outside("US national", "us-national", "US", "HBO", west, 180)};
    Decision d = decide(row("e|r:hbo~west"), cands, kNow, kNowSec);
    REQUIRE(d.chosen == 0);
    CHECK(d.shift == 0);
  }
}

TEST_CASE("guide choice: distinct schedules count sources, not copies") {
  if (!haveReference()) return;
  Schedule bad = hourly("Aliens", kNow - 120, 30);
  Schedule good = hourly("Pawn Stars", kNow - 120, 30);
  // Three copies on one wrong schedule (one provider's guide id), one copy
  // on the right one from another provider, and an outside listing that agrees.
  std::vector<MemberGuide> members;
  for (int i = 0; i < 3; ++i) {
    MemberGuide m;
    m.id = "xt:1:" + std::to_string(i);
    m.serverId = "xt:1";
    m.xtream = true;
    m.epgId = "h2east.us";
    m.provider = bad;
    m.providerNote = "Provider guide: US: HISTORY (by guide id)";
    members.push_back(m);
  }
  MemberGuide right;
  right.id = "xt:2:9";
  right.serverId = "xt:2";
  right.xtream = true;
  right.epgId = "hist2.us";
  right.provider = good;
  right.providerNote = "Provider guide: |US| HISTORY HD (by guide id)";
  right.outside = hourly("Pawn Stars", kNow - 120, 30);
  right.outsideNote = "US national: History HD (by name); UK: History (by name) [other country, not used]";
  members.push_back(right);
  std::vector<Candidate> cands = candidates(members);
  REQUIRE(cands.size() == 3);  // the three copies are one schedule
  CHECK(cands[2].outside);
  CHECK(cands[2].name == "History HD");
  CHECK(cands[2].country == "US");
  Decision d = decide(row("e|r:history"), cands, kNow, kNowSec);
  REQUIRE(d.chosen >= 0);
  CHECK(cands[static_cast<size_t>(d.chosen)].raw->front().t.rfind("Pawn Stars", 0) == 0);
  CHECK(d.rejected.find("disagrees") != std::string::npos);
  REQUIRE(d.options.size() == 2);
  CHECK(cands[d.options[1].index].key == cands[0].key);
}

TEST_CASE("guide choice: the viewer's pick wins while its schedule is there") {
  if (!haveReference()) return;
  std::vector<Candidate> cands = {provider("hist2.us", "|US| HISTORY", hourly("Aliens", kNow - 60, 20)),
                                  outside("US national", "us-national", "US", "History HD", hourly("Pawn Stars", kNow - 60, 20)),
                                  outside("US sports", "us-sports", "US", "History", hourly("Pawn Stars", kNow - 60, 20))};
  Decision automatic = decide(row("e|r:history"), cands, kNow, kNowSec);
  REQUIRE(automatic.chosen >= 1);
  Decision picked = decide(row("e|r:history"), cands, kNow, kNowSec, cands[0].key);
  CHECK(picked.chosen == 0);
  CHECK(picked.picked);
  CHECK(picked.automatic == automatic.chosen);
  CHECK(picked.why.rfind("viewer's pick", 0) == 0);
  CHECK(onAt(listing(picked, cands, kNowSec), kNowSec)->title == "Aliens 1");
  // Its schedule gone: the automatic choice, and the pick reported gone.
  std::vector<Candidate> later(cands.begin() + 1, cands.end());
  Decision gone = decide(row("e|r:history"), later, kNow, kNowSec, cands[0].key);
  CHECK(gone.pickGone);
  CHECK_FALSE(gone.picked);
  CHECK(gone.chosen >= 0);
}

TEST_CASE("guide choice: notes, agreement, bounds") {
  std::vector<NotePart> parts = noteParts(
      "US national: History Television HD (Canada) (by name, West feed 3 h later); Backup: epg.pw US: "
      "History (by name), times \xE2\x88\x92" "8 h; UK: History (by name) [other country, not used]");
  REQUIRE(parts.size() == 3);
  CHECK(parts[0].source == "US national");
  CHECK(parts[0].name == "History Television HD (Canada)");
  CHECK(parts[0].how == "by name, West feed 3 h later");
  CHECK(parts[1].source == "Backup: epg.pw US");
  CHECK(parts[1].name == "History");
  CHECK(parts[2].unused);
  CHECK(providerGuideName("Provider guide: US: AMC PLUS (by guide id)") == "US: AMC PLUS");

  Schedule a = hourly("Show", kNow, 12);
  std::vector<Mark> ma = marks(a.get(), nullptr, kNow - 300, kNow + 900);
  std::vector<Mark> mb = marks(moved(a, 180).get(), nullptr, kNow - 300, kNow + 900);
  CHECK(agreement(ma, 0, ma, 0, kNow).agrees());
  CHECK(agreement(ma, 180, mb, 0, kNow).agrees());
  CHECK(agreement(ma, 0, mb, 0, kNow).differs());
  // At most kMaxMarks programmes are compared.
  Schedule many = std::make_shared<const std::vector<RawProgram>>([] {
    std::vector<RawProgram> v;
    for (int i = 0; i < 200; ++i) v.push_back({kNow + i * 2, 2, "Short " + std::to_string(i), {}, {}, 0});
    return v;
  }());
  std::vector<Mark> mm = marks(many.get(), nullptr, kNow, kNow + 400);
  CHECK(agreement(mm, 0, mm, 0, kNow).compared == static_cast<int>(kMaxMarks));
  CHECK(titleKey("Live: Monday Night Football") == titleKey("Monday Night Football"));
  CHECK(titleKey("New: The Voice") == titleKey("The Voice"));
}

TEST_CASE("outside build: one country per channel, another schedule kept apart") {
  // A copy named like a network: the US file lists it, the UK file has a
  // channel of the same name (other programmes, other times), and a second
  // US file lists a different channel under that name.
  Channel c;
  c.id = "a";
  c.serverId = "xt:1";
  c.name = "US: Harbor Channel";
  auto file = [](const std::string& id, const std::string& name, const std::string& prefix) {
    xmltvdata::Builder b;
    b.channel(id, {name});
    for (int i = 0; i < 24; ++i) b.programme(id, kNow - 60 + i * 60, 60, prefix + " " + std::to_string(i));
    return b.done();
  };
  std::map<std::string, std::string> files = {
      {"us-national", file("harbor.us", "Harbor Channel", "Tides")},
      {"us-sports", file("harbor2.us", "Harbor Channel", "Regatta")},
      {"uk", file("harbor.uk", "Harbor Channel", "Docks")},
  };
  BuildIO io;
  io.fetch = [&](const Source& source, const std::optional<std::string>&, const std::optional<time::Seconds>&, bool) {
    BuildIO::FetchOutcome out;
    if (!files.count(source.id())) {
      out.error = "none";
      return out;
    }
    Fetched f;
    f.file = source.id();
    out.fetched = f;
    return out;
  };
  io.read = [&](const Source& source, const std::string& name, const std::shared_ptr<const Targets>& targets,
                Window window) {
    XmltvReader reader = XmltvReader::forSource(source, targets, window);
    reader.feed(files[name]);
    reader.finish();
    BuildIO::ReadOutcome out;
    out.result = reader.sourceResult();
    return out;
  };
  BuildResult r = build({c}, {"us", "uk"}, nullptr, {}, kNowSec, io);
  REQUIRE(r.programs.count("a"));
  for (const RawProgram& p : *r.programs.at("a")) CHECK(p.t.rfind("Tides", 0) == 0);
  REQUIRE(r.others.count("a"));
  REQUIRE(r.others.at("a").size() == 1);
  CHECK(r.others.at("a")[0].note == "US sports: Harbor Channel (by name)");
  CHECK(r.others.at("a")[0].programs->front().t.rfind("Regatta", 0) == 0);
  CHECK(r.notes.at("a").find("UK: Harbor Channel (by name) [other country, not used]") != std::string::npos);
  CHECK(r.notes.at("a").find("[another schedule, kept apart]") != std::string::npos);

  // Saved and read back with the lists kept apart.
  Saved saved;
  saved.built = kNowSec;
  saved.shards = {"uk", "us"};
  saved.programs = r.programs;
  saved.notes = r.notes;
  saved.others = r.others;
  auto loaded = savedFromJson(toJson(saved));
  REQUIRE(loaded);
  REQUIRE(loaded->others.count("a"));
  CHECK(loaded->others.at("a")[0].note == "US sports: Harbor Channel (by name)");
  CHECK(*loaded->others.at("a")[0].programs == *r.others.at("a")[0].programs);
  CHECK(*loaded->programs.at("a") == *r.programs.at("a"));

  // The row's choice sees both US schedules, never the UK one.
  MemberGuide m;
  m.id = "a";
  m.serverId = "xt:1";
  m.outside = loaded->programs.at("a");
  m.outsideNote = loaded->notes->at("a");
  for (const Apart& apart : loaded->others.at("a")) m.others.emplace_back(apart.programs, apart.note);
  std::vector<Candidate> cands = candidates({m});
  REQUIRE(cands.size() == 2);
  CHECK(cands[0].source == "og:us-national");
  CHECK(cands[1].source == "og:us-sports");
}
