// The XMLTV reader: what it keeps, in chunks of any size; the time parser.
#include <memory>

#include "core/guide/genre.hpp"
#include "core/guide/xmltv.hpp"
#include "doctest.h"
#include "guide/xmltv_helpers.hpp"

using namespace marigold;
using namespace marigold::guide;
using xmltvdata::stamp;

namespace {

const int kNow = static_cast<int>(time::secondsFromCivil({2026, 10, 21, 12, 0, 0}) / 60);
const Window kWindow{kNow - 180, kNow + 48 * 60};

Channel userChannel(const std::string& id, const std::string& name,
                    std::optional<std::string> group = std::nullopt) {
  Channel c;
  c.id = id;
  c.name = name;
  c.group = std::move(group);
  return c;
}

std::shared_ptr<const Targets> targets() {
  static const std::vector<Channel> channels = {
      userChannel("u-sports", "US: Dream Sports"), userChannel("u-movies", "US: Movie Box"),
      userChannel("u-movies-west", "US: Movie Box West"),
      userChannel("u-local", "US: ABC 7 (KAAA)", "US Locals"),
      userChannel("u-late", "US: Late Channel")};
  return std::make_shared<const Targets>(channels, std::vector<Station>{});
}

std::string sampleGuide() {
  std::string longDesc(250, 'd');
  xmltvdata::Builder b;
  b.channel("dream.us", {"Dream Sports HD", "Dream Sports"})
      .channel("movie.us", {"Movie Box"})
      .channel("kaaa.us", {"KAAA-DT ABC 7"})
      .channel("kaaa-ld.us", {"KAAA-LD"})
      .channel("other.us", {"Nobody Has This"});
  b.programme("dream.us", kNow - 300, 60, "Too early")  // ends before the window
      .programme("dream.us", kNow, 120, "Tom &amp; Jerry Match",
                 "<sub-title>Final</sub-title><desc>" + longDesc +
                     "</desc><category>Sports</category><category>Soccer</category><live/>")
      .programme("dream.us", kNow + 600, 60, "Late Show", "<desc>Not soon, not sports</desc><new/>")
      .programme("dream.us", kNow + 60, 60, "Soon Show", "<desc>  Kept  </desc><premiere/>")
      .programme("dream.us", kNow + 49 * 60, 60, "Beyond the window")
      .programme("movie.us", kNow, 90, "<![CDATA[Feature]]>", "<category>Movie</category>")
      .programme("movie.us", kNow + 90, 0, "Zero length")  // stop == start: dropped
      .programme("kaaa.us", kNow, 30, "NFL Football", "<desc>At the stadium, later</desc>")
      .programme("kaaa-ld.us", kNow, 30, "Repeater 1")
      .programme("kaaa-ld.us", kNow + 30, 30, "Repeater 2")
      .programme("other.us", kNow, 30, "Unmatched")
      .programme("dream.us", kNow + 200, 30, "");  // no title: dropped
  // A channel listed after the first programme is never matched.
  b.channel("late.us", {"Late Channel"}).programme("late.us", kNow, 30, "Lost");
  return b.done();
}

SourceResult readAll(const std::string& xml, size_t chunk, bool locals = false) {
  XmltvReader reader(targets(), locals, kWindow);
  for (size_t i = 0; i < xml.size(); i += chunk)
    reader.feed(std::string_view(xml).substr(i, chunk));
  reader.finish();
  return reader.sourceResult();
}

}  // namespace

TEST_CASE("XMLTV: matched channels, window, flags, descriptions") {
  std::string xml = sampleGuide();
  SourceResult whole = readAll(xml, xml.size());
  CHECK(whole.guideChannels == 6);
  CHECK(whole.programmes == 13);
  REQUIRE(whole.programs.count("u-sports"));
  const auto& sports = whole.programs.at("u-sports");
  CHECK(sports.note == "Dream Sports HD (by name)");
  REQUIRE(sports.programs.size() == 3);
  const RawProgram& match = sports.programs[0];
  CHECK(match.t == "Tom & Jerry Match");
  CHECK(match.s == kNow);
  CHECK(match.m == 120);
  CHECK(match.st == std::string("Final"));
  REQUIRE(match.d);
  CHECK(match.d->size() == 199 + 3);  // 199 characters and "…"
  CHECK(match.f == (genre::kSports | 16));
  const RawProgram& late = sports.programs[1];
  CHECK(late.t == "Late Show");
  CHECK_FALSE(late.d);  // not on soon and not sports: description dropped
  CHECK(late.f == 32);
  const RawProgram& soon = sports.programs[2];
  CHECK(soon.d == std::string("Kept"));
  CHECK(soon.f == 32);

  // CDATA titles count; categories give the movie flag.
  REQUIRE(whole.programs.count("u-movies"));
  CHECK(whole.programs.at("u-movies").programs.size() == 1);
  CHECK(whole.programs.at("u-movies").programs[0].t == "Feature");
  CHECK(whole.programs.at("u-movies").programs[0].f == genre::kMovie);
  // The East feed stands in for the West one, three hours later.
  REQUIRE(whole.programs.count("u-movies-west"));
  const auto& west = whole.programs.at("u-movies-west");
  CHECK(west.programs[0].s == kNow + 180);
  CHECK(west.note == "Movie Box (by name, West feed 3 h later)");

  // The main station beats its low-power repeater, despite fewer listings;
  // titles give the genre when there are no categories; sports keep their
  // description.
  REQUIRE(whole.programs.count("u-local"));
  const auto& local = whole.programs.at("u-local");
  CHECK(local.note == "KAAA-DT ABC 7 (by call sign KAAA)");
  REQUIRE(local.programs.size() == 1);
  CHECK(local.programs[0].f == genre::kSports);
  CHECK(local.programs[0].d == std::string("At the stadium, later"));

  CHECK_FALSE(whole.programs.count("u-late"));
  CHECK(whole.programs.size() == 4);
  REQUIRE(whole.horizonMedian);
  CHECK(*whole.horizonMedian == kNow + 180 + 90);  // ends: 30, 90, 270, 660 minutes ahead

  // Same result in odd chunk sizes.
  for (size_t chunk : {size_t(1), size_t(3), size_t(17), size_t(1000)}) {
    CAPTURE(chunk);
    SourceResult r = readAll(xml, chunk);
    CHECK(r.guideChannels == whole.guideChannels);
    CHECK(r.programmes == whole.programmes);
    REQUIRE(r.programs.size() == whole.programs.size());
    for (const auto& [id, entry] : whole.programs) {
      const auto& other = r.programs.at(id);
      CHECK(other.note == entry.note);
      REQUIRE(other.programs.size() == entry.programs.size());
      for (size_t i = 0; i < entry.programs.size(); ++i) {
        CHECK(other.programs[i].t == entry.programs[i].t);
        CHECK(other.programs[i].s == entry.programs[i].s);
        CHECK(other.programs[i].d == entry.programs[i].d);
        CHECK(other.programs[i].f == entry.programs[i].f);
      }
    }
  }
}

TEST_CASE("XMLTV: a repeater wins only when the main station isn't listed") {
  xmltvdata::Builder b;
  b.channel("kaaa-ld.us", {"KAAA-LD"}).programme("kaaa-ld.us", kNow, 30, "Repeater");
  SourceResult r = readAll(b.done(), 64);
  REQUIRE(r.programs.count("u-local"));
  CHECK(r.programs.at("u-local").programs[0].t == "Repeater");
}

TEST_CASE("XMLTV: a West feed of its own beats one derived from East") {
  xmltvdata::Builder b;
  b.channel("movie.us", {"Movie Box"})
      .channel("moviewest.us", {"Movie Box West"})
      .programme("movie.us", kNow, 60, "East 1")
      .programme("movie.us", kNow + 60, 60, "East 2")
      .programme("moviewest.us", kNow, 60, "Own West");
  SourceResult r = readAll(b.done(), 5);
  CHECK(r.programs.at("u-movies-west").programs.size() == 1);
  CHECK(r.programs.at("u-movies-west").programs[0].t == "Own West");
  CHECK(r.programs.at("u-movies").programs.size() == 2);
}

TEST_CASE("XMLTV: exact ids for a provider's own guide") {
  std::unordered_map<std::string, std::vector<std::string>> ids = {{"dream.us", {"p1", "p2"}}};
  XmltvReader reader = XmltvReader::exact(ids, kWindow);
  int pauses = 0;
  reader.setPauseHook([&] { ++pauses; });
  xmltvdata::Builder b;
  b.channel("DREAM.us", {"Dream"}).channel("Other.us", {"Other"});
  for (int i = 0; i < 4000; ++i)
    b.programme(i % 2 ? "Dream.US" : "Other.us", kNow + i % 1000, 1, "Show");
  std::string xml = b.done();
  for (size_t i = 0; i < xml.size(); i += 4096) reader.feed(std::string_view(xml).substr(i, 4096));
  reader.finish();
  auto result = reader.result();
  REQUIRE(result.size() == 2);
  CHECK(result.at("p1").programs.size() == 2000);
  CHECK(result.at("p1").note == "Dream (by guide id)");
  CHECK(reader.guideChannelCount() == 2);
  CHECK(reader.programmeCount() == 4000);
  CHECK(pauses == 1);
  CHECK(reader.bytesFed() == xml.size());
}

TEST_CASE("XMLTV time parser") {
  int base = static_cast<int>(time::secondsFromCivil({2026, 10, 21, 20, 0, 0}) / 60);
  CHECK(XmltvReader::minutes("20261021200000 +0000") == base);
  CHECK(XmltvReader::minutes("20261021200000 +0200") == base - 120);
  CHECK(XmltvReader::minutes("20261021200000 -0500") == base + 300);
  CHECK(XmltvReader::minutes("20261021200000 +0530") == base - 330);
  CHECK(XmltvReader::minutes("20261021200000") == base);
  CHECK(XmltvReader::minutes("202610212000") == base);
  CHECK(XmltvReader::minutes("20261021200000+0100") == base - 60);
  CHECK(XmltvReader::minutes("20260101000000 +0000") ==
        static_cast<int>(time::secondsFromCivil({2026, 1, 1, 0, 0, 0}) / 60));
  CHECK(XmltvReader::minutes("20240229120000 +0000") ==
        static_cast<int>(time::secondsFromCivil({2024, 2, 29, 12, 0, 0}) / 60));
  CHECK_FALSE(XmltvReader::minutes("2026102120"));
  CHECK_FALSE(XmltvReader::minutes("2026-10-21 20:00"));
  CHECK_FALSE(XmltvReader::minutes(""));
  // A malformed offset is ignored.
  CHECK(XmltvReader::minutes("20261021200000 +xx00") == base);
  CHECK(stamp(base) == "20261021200000 +0000");
}
