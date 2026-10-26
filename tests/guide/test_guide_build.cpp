// The outside-guide build with fake downloads, the provider guide rules and
// the station directory.
#include <map>
#include <memory>

#include "core/guide/outside.hpp"
#include "core/guide/provider.hpp"
#include "core/guide/stations.hpp"
#include "core/guide/xmltv.hpp"
#include "doctest.h"
#include "guide/xmltv_helpers.hpp"

using namespace marigold;
using namespace marigold::guide;

namespace {

const time::Seconds kNowSec = time::secondsFromCivil({2026, 10, 21, 12, 0, 0});
const int kNow = static_cast<int>(kNowSec / 60);

Channel userChannel(const std::string& id, const std::string& name,
                    std::optional<std::string> group = std::nullopt) {
  Channel c;
  c.id = id;
  c.serverId = "xt:1";
  c.name = name;
  c.group = std::move(group);
  return c;
}

// A fake cache: files by source id, what each fetch answers, and how many
// times each file was read.
struct FakeIO {
  std::map<std::string, std::string> files;     // source id -> XMLTV text
  std::map<std::string, int> status;            // source id -> 200 / 304 / 0
  std::map<std::string, std::string> failures;  // source id -> error
  std::map<std::string, int> reads;
  std::vector<std::string> progress;

  BuildIO io() {
    BuildIO io;
    io.fetch = [this](const Source& source, const std::optional<std::string>&,
                      const std::optional<time::Seconds>&, bool) {
      BuildIO::FetchOutcome out;
      if (failures.count(source.id())) {
        out.error = failures[source.id()];
        return out;
      }
      Fetched f;
      f.file = source.id();
      f.status = status.count(source.id()) ? status[source.id()] : 200;
      f.etag = "\"" + source.id() + "\"";
      f.lastModified = kNowSec - 3600;
      f.bytes = static_cast<int64_t>(files[source.id()].size());
      out.fetched = f;
      return out;
    };
    io.read = [this](const Source& source, const std::string& file,
                     const std::shared_ptr<const Targets>& targets, Window window) {
      reads[file] += 1;
      XmltvReader reader = XmltvReader::forSource(source, targets, window);
      const std::string& xml = files[file];
      for (size_t i = 0; i < xml.size(); i += 333)
        reader.feed(std::string_view(xml).substr(i, 333));
      reader.finish();
      BuildIO::ReadOutcome out;
      out.result = reader.sourceResult();
      return out;
    };
    io.progress = [this](const std::string& line) { progress.push_back(line); };
    return io;
  }
};

std::string hours(const std::string& channel, const std::string& name, int from, int count,
                  const std::string& prefix = "Show ") {
  xmltvdata::Builder b;
  b.channel(channel, {name});
  for (int i = 0; i < count; ++i)
    b.programme(channel, from + i * 60, 60, prefix + std::to_string(i));
  return b.done();
}

}  // namespace

TEST_CASE("outside build: mains, a failed main, stale shards, backups with a clock shift") {
  std::vector<Channel> channels = {userChannel("a", "US: Dream Sports"),
                                   userChannel("b", "US: NBC 4 (WAAA)", "US Locals")};
  FakeIO fake;
  fake.files["us-locals"] = hours("waaa.us", "WAAA-DT", kNow, 24);
  fake.files["us-national"] = hours("dream.us", "Dream Sports", kNow, 24);
  fake.failures["us-sports"] = "offline";
  fake.failures["backup--epgtalk-us"] = "HTTP 500";
  fake.files["backup--epgtalk-sports"] = hours("nobody.us", "Nobody", kNow, 5);
  // epg.pw writes times 8 h late; its listings reach past the main one's.
  fake.files["backup--epg-pw-us"] =
      hours("dream.us", "Dream Sports", kNow + 24 * 60 + 480, 6, "Later ");
  std::set<std::string> on = {"us", "us-locals"};
  BuildResult r = build(channels, on, nullptr, {}, kNowSec, fake.io());

  REQUIRE(r.programs.count("a"));
  REQUIRE(r.programs.count("b"));
  CHECK(r.programs.at("b")->size() == 24);
  const auto& a = *r.programs.at("a");
  REQUIRE(a.size() == 30);
  CHECK(a[24].t == "Later 0");
  CHECK(a[24].s == kNow + 24 * 60);  // moved back by the assumed 8 h
  CHECK(r.problems == std::vector<std::string>{"US sports: offline"});
  CHECK(r.notes.at("b") == "US locals: WAAA-DT (by call sign WAAA)");
  CHECK(r.notes.at("a") ==
        "US national: Dream Sports (by name); Backup: epg.pw US: Dream Sports (by name), times "
        "\xE2\x88\x92"
        "8 h");
  CHECK(r.nextCheck == kNowSec + kStaleRetry);
  CHECK(r.targetsKey == targetsKey(channels, on));
  CHECK(r.log.back().find("; stale: us, next check in 2 h") != std::string::npos);

  REQUIRE(r.states.size() == 7);
  CHECK(r.states.at("us-sports").stale == true);
  CHECK(r.states.at("us-sports").problem == std::string("offline"));
  CHECK(r.states.at("us-national").stale == false);
  CHECK(r.states.at("us-national").matched == 1);
  CHECK(r.states.at("us-national").etag == std::string("\"us-national\""));
  CHECK(r.states.at("us-national").checkedAt == kNowSec);
  CHECK(r.states.at("us-locals").stale == false);
  CHECK(r.states.at("backup--epgtalk-us").used == false);
  CHECK(r.states.at("backup--epgtalk-us").problem == std::string("HTTP 500"));
  CHECK(r.states.at("backup--epgtalk-sports").used == true);
  CHECK(r.states.at("backup--epgtalk-sports").extended == 0);
  CHECK(r.states.at("backup--epgtalk-locals").used == false);  // its shard isn't stale
  const SourceState& pw = r.states.at("backup--epg-pw-us");
  CHECK(pw.used == true);
  CHECK(pw.shift == 480);
  CHECK(pw.shiftNote == std::string("no overlap to check against (1 channels); using 8 h"));
  CHECK(pw.extended == 1);
  CHECK(pw.horizonMedian == kNow + 30 * 60);
  CHECK(fake.reads.count("backup--epgtalk-locals") == 0);
  CHECK_FALSE(fake.progress.empty());

  // Saved and loaded again: unchanged files and the same channels reuse it.
  Saved saved;
  saved.built = kNowSec;
  saved.shards = {on.begin(), on.end()};
  saved.channelCount = 2;
  saved.programs = r.programs;
  saved.notes = r.notes;
  saved.sources = r.states;
  saved.nextCheck = r.nextCheck;
  saved.targetsKey = r.targetsKey;
  saved.detailVersion = kDetailVersion;
  auto loaded = savedFromJson(toJson(saved));
  REQUIRE(loaded);
  FakeIO again = fake;
  again.reads.clear();
  again.failures.erase("us-sports");
  again.files["us-sports"] = hours("x.us", "X", kNow, 2);
  again.status = {{"us-locals", 304}, {"us-national", 304}, {"us-sports", 0}};
  BuildResult reused = build(channels, on, &*loaded, {}, kNowSec + 600, again.io());
  CHECK(again.reads.count("us-national") == 0);
  CHECK(reused.programs.at("a")->size() == 30);
  CHECK(reused.notes.at("b") == r.notes.at("b"));
  CHECK(reused.log[3] == "guide main sources unchanged: saved listings reused (2 channels)");

  // A different channel list reads the files again, and keeps the last
  // update's later listings where the new ones end sooner.
  std::vector<Channel> more = channels;
  more.push_back(userChannel("c", "US: Something New"));
  FakeIO shorter = again;
  shorter.reads.clear();
  shorter.files["us-national"] = hours("dream.us", "Dream Sports", kNow, 12);
  shorter.status.clear();
  BuildResult kept = build(more, on, &*loaded, {}, kNowSec + 600, shorter.io());
  CHECK(shorter.reads.at("us-national") == 1);
  CHECK(kept.programs.at("a")->size() == 30);
  bool keptLine = false;
  for (const std::string& line : kept.log)
    keptLine |= line.find("kept the last update's later listings") != std::string::npos;
  CHECK(keptLine);
}

TEST_CASE("outside build: nothing stale means no backups and a day's wait") {
  std::vector<Channel> channels = {userChannel("a", "US: Dream Sports")};
  FakeIO fake;
  fake.files["us-national"] = hours("dream.us", "Dream Sports", kNow, 24);
  fake.files["us-sports"] = hours("dream.us", "Dream Sports", kNow + 24 * 60, 6);
  BuildResult r = build(channels, {"us"}, nullptr, {}, kNowSec, fake.io());
  CHECK(r.nextCheck == kNowSec + kRefreshAge);
  CHECK(r.programs.at("a")->size() == 30);  // the second main fills the later hours
  CHECK(r.states.at("backup--epgtalk-us").used == false);
  CHECK(fake.reads.count("backup--epgtalk-us") == 0);
  // A short horizon on a backed-up shard is stale too.
  FakeIO shortFake;
  shortFake.files["us-national"] = hours("dream.us", "Dream Sports", kNow, 6);
  shortFake.files["us-sports"] = hours("dream.us", "Dream Sports", kNow, 6);
  BuildResult s = build(channels, {"us"}, nullptr, {}, kNowSec, shortFake.io());
  CHECK(s.states.at("us-national").stale == true);
  CHECK(s.nextCheck == kNowSec + kStaleRetry);
  // Cancelled before anything: nothing fetched, everything stale.
  BuildIO io = fake.io();
  io.cancelled = [] { return true; };
  BuildResult none = build(channels, {"us"}, nullptr, {}, kNowSec, io);
  CHECK(none.programs.empty());
}

TEST_CASE("provider guide: scope, rebuild, outcomes") {
  CHECK(provider::countryPrefix(std::string("US| NBC ᴴᴰ")) == std::string("US"));
  CHECK(provider::countryPrefix(std::string(" UK |x")) == std::string("UK"));
  CHECK_FALSE(provider::countryPrefix(std::string("Sports")));
  CHECK_FALSE(provider::countryPrefix(std::nullopt));
  Channel c = userChannel("a", "NBC", std::string("US| Locals"));
  CHECK_FALSE(provider::inScope(c, {"US"}));  // no guide id
  c.epgId = "nbc.us";
  CHECK(provider::inScope(c, {"US"}));
  CHECK_FALSE(provider::inScope(c, {"UK"}));
  c.group = "Sports";
  CHECK(provider::inScope(c, {"UK"}));  // no country prefix: always
  c.epgId = "";
  CHECK_FALSE(provider::inScope(c, {"UK"}));
  CHECK(provider::cacheKey("Main Profile", "xt:acct-1") == "Main_Profile_xt_acct_1");
  CHECK(provider::savedFileName("k") == "provider-k.json");

  provider::Saved saved;
  saved.built = kNowSec;
  saved.channelCount = 300;
  CHECK(provider::needsRebuild(nullptr, 1, kNowSec));
  CHECK_FALSE(provider::needsRebuild(&saved, 330, kNowSec + 3600));
  CHECK(provider::needsRebuild(&saved, 331, kNowSec + 3600));  // 300 + max(25, 30)
  CHECK(provider::needsRebuild(&saved, 300, kNowSec + provider::kRefreshAge + 1));

  std::vector<Channel> channels = {userChannel("a", "One"), userChannel("b", "Two"),
                                   userChannel("c", "Three")};
  channels[0].epgId = " One.US ";
  channels[1].epgId = "one.us";
  auto ids = provider::guideIds(channels);
  CHECK(ids.size() == 1);
  CHECK(ids.at("one.us") == std::vector<std::string>{"a", "b"});

  auto none = provider::build({channels[2]}, kNowSec, nullptr);
  CHECK(none.kind == provider::Outcome::Kind::unavailable);
  CHECK(none.message == "None of your channels have a guide id from the provider.");

  auto failing = [](int status, std::string message) {
    return [status, message](const auto&, Window,
                             provider::ReadFailure& failure) -> std::optional<provider::ExactRead> {
      failure.httpStatus = status;
      failure.message = message;
      return std::nullopt;
    };
  };
  auto notFound = provider::build(channels, kNowSec, failing(404, ""));
  CHECK(notFound.kind == provider::Outcome::Kind::unavailable);
  CHECK(notFound.message == "This provider doesn't publish a full guide (404).");
  auto serverError = provider::build(channels, kNowSec, failing(500, ""));
  CHECK(serverError.kind == provider::Outcome::Kind::failed);
  CHECK(serverError.message == "The server answered 500.");
  auto timeout = provider::build(channels, kNowSec, failing(0, "timed out"));
  CHECK(timeout.message == "timed out");

  auto empty = provider::build(channels, kNowSec, [](const auto&, Window, provider::ReadFailure&) {
    provider::ExactRead read;
    read.bytes = 100;
    return std::optional<provider::ExactRead>(read);
  });
  CHECK(empty.kind == provider::Outcome::Kind::unavailable);
  CHECK(empty.message == "The provider's guide is empty.");

  std::vector<std::string> lines;
  auto built = provider::build(
      channels, kNowSec,
      [&](const auto& wanted, Window window, provider::ReadFailure&) {
        CHECK(window.low == kNow - 180);
        CHECK(window.high == kNow + 48 * 60);
        XmltvReader reader = XmltvReader::exact(wanted, window);
        xmltvdata::Builder b;
        b.channel("ONE.us", {"One TV"})
            .programme("one.us", kNow + 60, 30, "Later")
            .programme("one.us", kNow, 30, "First");
        reader.feed(b.done());
        reader.finish();
        provider::ExactRead read;
        read.programs = reader.result();
        read.guideChannels = reader.guideChannelCount();
        read.programmes = reader.programmeCount();
        read.bytes = 4096;
        return std::optional<provider::ExactRead>(read);
      },
      [&](const std::string& line) { lines.push_back(line); });
  REQUIRE(built.kind == provider::Outcome::Kind::built);
  CHECK((*built.saved.programs.at("a"))[0].t == "First");  // sorted by start
  CHECK(built.saved.programs.at("b")->size() == 2);
  CHECK(built.saved.notes.at("a") == "Provider guide: One TV (by guide id)");
  CHECK(built.saved.channelCount == 3);
  CHECK(built.saved.guideChannels == 1);
  CHECK(built.saved.guideProgrammes == 2);
  CHECK(lines.size() == 1);
  auto back = provider::savedFromJson(provider::toJson(built.saved));
  REQUIRE(back);
  CHECK((*back->programs.at("b"))[1].t == "Later");
  CHECK(back->notes == built.saved.notes);
  CHECK(back->bytes == 4096);
}

TEST_CASE("station directory: rows, carried-over cities, channel numbers") {
  std::string text =
      "{| class=\"wikitable\"\n"
      "|-\n"
      "| rowspan=2 | [[Springfield, Examplestate|Springfield]] || [[KAAA-TV|KAAA]] || 5.1 || x\n"
      "|-\n"
      "| [[KBBB]] || [[Some Network|Net]] 12\n"
      "|-\n"
      "| [[File:Logo.png]] [[The City of Testville (city)|The City of Testville]] || [[WCCC-DT]] "
      "|| 7\n"
      "|-\n"
      "| no call sign here || 9\n"
      "|-\n"
      "| [[KDDD]] [[Ogdenville]] || [[KEEE]] ||\n"
      "|}\n";
  auto list = stations::parse(text, "ABC");
  REQUIRE(list.size() == 4);
  CHECK(list[0] == Station{"KAAA", "ABC", {"springfield"}, "5"});
  CHECK(list[1] == Station{"KBBB", "ABC", {"springfield"}, "12"});
  CHECK(list[2] == Station{"WCCC", "ABC", {"of", "testville"}, "7"});
  CHECK(list[3].callsign == "KDDD");  // the first call-sign link; no city link before it
  CHECK(list[3].cityTokens == std::vector<std::string>{"of", "testville"});
  CHECK(list[3].channelNumber.empty());

  auto merged = stations::collect({{"ABC", text},
                                   {"ABC", "|-\n[[Springfield]] [[KAAA]] 9"},
                                   {"NBC", "|-\n[[Springfield]] [[KAAA]] 9"}});
  CHECK(merged.size() == 5);  // KAAA/ABC once, KAAA/NBC again

  // A cell naming two cities ("Newark–New York City"): an entry for each.
  auto twin = stations::parse("|-\n| [[Newark, New Jersey|Newark]]-[[Sample City]]\n| rowspan=\"2\" | [[Some State]]\n"
                              "! scope=\"row\" | [[WQQQ]]\n| 13 ||\n|}\n",
                              "PBS");
  REQUIRE(twin.size() == 2);
  CHECK(twin[0].cityTokens == std::vector<std::string>{"newark"});
  CHECK(twin[1].cityTokens == std::vector<std::string>{"sample"});
  CHECK(twin[1].channelNumber == "13");
  // The state in the next inline cell is not a city.
  auto inline_ = stations::parse("|-\n| [[Buffalo, Some State|Buffalo]] || [[Some State]] || [[WRRR-TV|WRRR]] || 2\n", "NBC");
  REQUIRE(inline_.size() == 1);
  CHECK(inline_[0].cityTokens == std::vector<std::string>{"buffalo"});
  auto json = stations::toJson(merged);
  CHECK(stations::fromJson(json) == merged);
  CHECK_FALSE(stations::fromJson("{}"));
  CHECK(stations::pages().size() == 10);
  CHECK(stations::isFresh(0, stations::kMaxAge - 1, false));
  CHECK_FALSE(stations::isFresh(0, stations::kMaxAge - 1, true));
  CHECK(stations::wikiRequestUrl("List of A & B (x)") ==
        "https://en.wikipedia.org/w/api.php?action=parse&page=List%20of%20A%20%26%20B%20%28x%29"
        "&prop=wikitext&format=json&redirects=1");
  CHECK(stations::wikitextFromResponse("{\"parse\":{\"wikitext\":{\"*\":\"[[KAAA]]\"}}}") ==
        std::string("[[KAAA]]"));
  CHECK_FALSE(stations::wikitextFromResponse("{\"error\":{}}"));
}
