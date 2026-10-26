// Outside guide: sources, matching by call sign vs name, clock-shift
// learning, merging, programmes, saved state and the whole build.
#include <map>

#include "core/guide/genre.hpp"
#include "core/guide/outside.hpp"
#include "core/guide/xmltv.hpp"
#include "doctest.h"

using namespace marigold;
using namespace marigold::guide;

namespace {

Channel userChannel(const std::string& id, const std::string& name,
                    std::optional<std::string> group = std::nullopt,
                    std::optional<std::string> epg = std::nullopt) {
  Channel c;
  c.id = id;
  c.serverId = "xt:1";
  c.name = name;
  c.group = std::move(group);
  c.epgId = std::move(epg);
  return c;
}

RawProgram raw(int s, int m, const std::string& t, int f = 0) {
  RawProgram p;
  p.s = s;
  p.m = m;
  p.t = t;
  p.f = f;
  return p;
}

// Titles unique to each channel: "<channel> show <n>".
std::vector<RawProgram> schedule(const std::string& channel, int start, int count, int shift = 0) {
  std::vector<RawProgram> list;
  for (int i = 0; i < count; ++i) {
    list.push_back(raw(start + i * 60 + shift, 60, channel + " show " + std::to_string(i)));
  }
  return list;
}

const time::Seconds kNow = time::secondsFromCivil({2026, 10, 21, 12, 0, 0});
const int kNowMin = static_cast<int>(kNow / 60);

}  // namespace

TEST_CASE("sources and shards") {
  CHECK(sources().size() == 13);
  CHECK(sources()[0].id() == "us-locals");
  CHECK(sources()[9].id() == "backup--epgtalk-us");
  CHECK(defaultShards() == std::set<std::string>{"fast", "us", "us-locals"});
  const Source& pw = sources().back();
  CHECK(pw.backup);
  CHECK(pw.calibrate);
  CHECK(pw.assumedShift == 480);
  CHECK(pw.appliesTo({"us"}));
  CHECK(pw.appliesTo({"us-locals"}));
  CHECK_FALSE(pw.appliesTo({"uk", "fast"}));
  CHECK(needsStationDirectory({"ca"}));
  CHECK_FALSE(needsStationDirectory({"us", "fast"}));
  for (const Source& s : sources()) CHECK(s.url.rfind("https://", 0) == 0);
}

TEST_CASE("Targets: call signs win over names; West feeds derived from East") {
  std::vector<Channel> channels = {
      userChannel("a", "US: NBC 4 (WNBC)", "US Locals"),
      userChannel("b", "US: Dream Sports"),
      userChannel("c", "US: Movie Box West"),
      userChannel("d", "US: Movie Box"),
      userChannel("e", "Some Station", std::nullopt, "KAAA.us"),
      userChannel("f", "(US) (Prime) KBBB"),
  };
  Targets targets(channels, {});
  CHECK(targets.bySign.at("WNBC") == std::vector<std::string>{"a"});
  CHECK(targets.bySign.at("KAAA") == std::vector<std::string>{"e"});
  CHECK(targets.bySign.at("KBBB") == std::vector<std::string>{"f"});
  CHECK(targets.byName.at("dreamsports") == std::vector<std::string>{"b"});

  auto bySign = targets.matches({"WNBC-DT", "Dream Sports"}, "WNBC.us", false);
  REQUIRE(bySign.size() == 1);
  CHECK(bySign[0].id == "a");
  CHECK(bySign[0].how == "by call sign WNBC");
  auto byName = targets.matches({"Dream Sports HD"}, "dream.us", false);
  REQUIRE(byName.size() == 1);
  CHECK(byName[0].how == "by name");
  auto west = targets.matches({"Movie Box"}, "moviebox.us", false, false, true);
  REQUIRE(west.size() == 2);
  CHECK(west[0].id == "d");
  CHECK(west[1].id == "c");
  CHECK(west[1].shift == 180);
  CHECK(targets.matches({"Movie Box West"}, "x", false, false, true).size() == 1);
  // A bare sign only counts as a call sign in a locals file (elsewhere it
  // still matches the name taken from the guide id "KAAA.us").
  CHECK(targets.matches({"KAAA"}, "x", false).at(0).how == "by name");
  CHECK(targets.matches({"KAAA"}, "x", true).at(0).how == "by call sign KAAA");
  CHECK(targets.matches({"WZZZ"}, "x", false).empty());
  CHECK(targets.matches({"Q"}, "x", true).empty());
}

TEST_CASE("Targets: network and city through the station directory") {
  std::vector<Station> stations = {{"KCCC", "CBS", {"capital", "city"}, "5"},
                                   {"KDDD", "CBS", {"springfield"}, "8"}};
  std::vector<Channel> channels = {userChannel("x", "US: CBS 5 Capital City")};
  Targets targets(channels, stations);
  CHECK(targets.bySign.at("KCCC") == std::vector<std::string>{"x"});
  auto m = targets.matches({"5.1 CBS Capital City"}, "id1", false, true);
  REQUIRE(m.size() == 1);
  CHECK(m[0].how == "by station KCCC");
  // The number must agree.
  CHECK(targets.matches({"9.1 CBS Capital City"}, "id2", false, true).empty());
  // Without stationNames, only names count.
  CHECK(targets.matches({"5.1 CBS Capital City"}, "id1", false, false).empty());
}

TEST_CASE("learnShift: one vote per channel, unique titles only") {
  ProgramsById reference, candidate;
  for (int c = 0; c < 25; ++c) {
    std::string id = "ch" + std::to_string(c);
    reference[id] = schedule(id, kNowMin, 6);
    candidate[id] = schedule(id, kNowMin, 6, c == 0 ? 420 : 480);  // ch0 disagrees, clearly
  }
  LearnedShift learned = learnShift(reference, candidate);
  REQUIRE(learned.shift);
  CHECK(*learned.shift == 480);
  CHECK(learned.perChannel == std::map<std::string, int>{{"ch0", 420}});
  CHECK(learned.note == "learned 8 h from 24 channels (8 h: 24, 7 h: 1)");

  // Fewer than 20 channels agreeing: unclear.
  ProgramsById fewRef, few;
  for (int c = 0; c < 19; ++c) {
    std::string id = "ch" + std::to_string(c);
    fewRef[id] = schedule(id, kNowMin, 6);
    few[id] = schedule(id, kNowMin, 6, 480);
  }
  LearnedShift unclear = learnShift(fewRef, few);
  CHECK_FALSE(unclear.shift);
  CHECK(unclear.note == "unclear (8 h: 19)");
  CHECK(unclear.perChannel.empty());

  // Repeated titles, short keys and off-hour gaps don't vote.
  ProgramsById repRef, rep;
  for (int c = 0; c < 25; ++c) {
    std::string id = "ch" + std::to_string(c);
    repRef[id] = {raw(kNowMin, 60, "Same"), raw(kNowMin + 60, 60, "Same Show"),
                  raw(kNowMin + 120, 60, "Same Show"), raw(kNowMin + 180, 60, "Odd gap here")};
    rep[id] = {raw(kNowMin + 480, 60, "LIVE: Same"), raw(kNowMin + 540, 60, "Same Show"),
               raw(kNowMin + 180 + 505, 60, "Odd gap here")};
  }
  CHECK_FALSE(learnShift(repRef, rep).shift);
  CHECK(learnShift({}, rep).note == "no overlap to check against (0 channels)");

  // The sample caps how many channels are checked.
  LearnedShift sampled = learnShift(reference, candidate, 10);
  CHECK_FALSE(sampled.shift);
  CHECK(sampled.note.find("unclear") == 0);
}

TEST_CASE("mergePrograms: only gaps are filled") {
  std::vector<RawProgram> base = {raw(100, 60, "A"), raw(200, 60, "B")};
  std::vector<RawProgram> extra = {raw(155, 45, "touches A by 5"), raw(150, 60, "clashes"),
                                   raw(40, 60, "before"), raw(255, 60, "after")};
  auto merged = mergePrograms(base, extra);
  std::vector<std::string> titles;
  for (const RawProgram& p : merged) titles.push_back(p.t);
  CHECK(titles == std::vector<std::string>{"before", "A", "touches A by 5", "B", "after"});
  auto fresh = mergePrograms({}, {raw(300, 10, "z"), raw(100, 10, "y")});
  CHECK(fresh.front().t == "y");
}

TEST_CASE("programs: window, ids, flags and title genres") {
  std::vector<RawProgram> list = {
      raw(kNowMin - 120, 60, "Gone"), raw(kNowMin - 30, 60, "NFL Football"),
      raw(kNowMin + 30, 60, "Movie Night", 1 | 16 | 32), raw(kNowMin + 600, 60, "Too late")};
  list[1].st = "Week 8";
  list[1].d = "At the stadium";
  auto out = programs(list, "c1", kNow - 3600, kNow + 3 * 3600);
  REQUIRE(out.size() == 2);
  CHECK(out[0].id == "og|c1|" + std::to_string(kNowMin - 30));
  CHECK(out[0].genres == genre::kSports);
  CHECK(out[0].episodeTitle == std::string("Week 8"));
  CHECK(out[0].overview == std::string("At the stadium"));
  CHECK(out[0].start == kNow - 1800);
  CHECK(out[0].end == kNow + 1800);
  CHECK(out[0].fromOutsideGuide);
  CHECK(out[1].genres == 1);
  CHECK(out[1].isLive);
  CHECK(out[1].isPremiere);
  auto own = programs(list, "c1", kNow - 3600, kNow + 3 * 3600, "pg", false);
  CHECK(own[0].id.rfind("pg|", 0) == 0);
  CHECK_FALSE(own[0].fromOutsideGuide);
}

TEST_CASE("saved listings: JSON, file name, when to rebuild") {
  Saved saved;
  saved.built = kNow;
  saved.shards = {"us", "fast"};
  saved.channelCount = 100;
  saved.programs["c1"] = {raw(kNowMin, 30, "A & B \"quoted\""), raw(kNowMin + 30, 30, "C")};
  saved.programs["c1"][0].st = "Part 1";
  saved.notes = std::map<std::string, std::string>{{"c1", "US national: Dream (by name)"}};
  SourceState state;
  state.id = "us-national";
  state.name = "US national";
  state.etag = "\"abc\"";
  state.lastModified = kNow - 7200;
  state.status = 304;
  state.bytes = 12345678;
  state.horizonMedian = kNowMin + 600;
  state.parseSeconds = 1.5;
  state.stale = false;
  saved.sources = std::map<std::string, SourceState>{{state.id, state}};
  saved.nextCheck = kNow + kRefreshAge;
  saved.targetsKey = "100|abc|fast,us";
  saved.detailVersion = kDetailVersion;
  auto back = savedFromJson(toJson(saved));
  REQUIRE(back);
  CHECK(back->programs.at("c1").size() == 2);
  CHECK(back->programs.at("c1")[0].t == "A & B \"quoted\"");
  CHECK(back->programs.at("c1")[0].st == std::string("Part 1"));
  CHECK_FALSE(back->programs.at("c1")[1].st);
  CHECK(back->sources->at("us-national") == state);
  CHECK(back->notes->at("c1") == "US national: Dream (by name)");
  CHECK(toJson(*back) == toJson(saved));
  CHECK_FALSE(savedFromJson("{}"));
  CHECK(savedFileName("Profile 1/xt:9") == "listings-Profile_1_xt_9.json");
  CHECK(savedFileName(std::string(100, 'a')).size() == std::string("listings-.json").size() + 80);

  std::set<std::string> on = {"fast", "us"};
  CHECK(needsRebuild(nullptr, on, 10, kNow));
  CHECK_FALSE(needsRebuild(&saved, on, 100, kNow + 3600));
  CHECK(needsRebuild(&saved, on, 100, kNow + kRefreshAge + 1));
  CHECK(needsRebuild(&saved, {"us"}, 100, kNow + 3600));
  CHECK(needsRebuild(&saved, on, 126, kNow + 3600));  // 25 more than last time
  CHECK_FALSE(needsRebuild(&saved, on, 125, kNow + 3600));
  Saved old = saved;
  old.detailVersion.reset();
  CHECK(needsRebuild(&old, on, 100, kNow + 3600));
  Saved soon = saved;
  soon.nextCheck = kNow + 60;
  CHECK(needsRebuild(&soon, on, 100, kNow + 60));
}

TEST_CASE("SourceState line") {
  SourceState s;
  s.name = "US national";
  s.lastModified = time::secondsFromCivil({2026, 10, 21, 15, 5, 0});
  s.matched = 40;
  s.stale = true;
  CHECK(s.line(time::Zone::utc()) ==
        "US national · file from Wed Oct 21, 3:05 PM · 40 channels · out of date");
  SourceState b;
  b.name = "Backup";
  b.backup = true;
  b.used = true;
  b.extended = 3;
  b.shift = 480;
  b.problem = "HTTP 500";
  CHECK(b.line(time::Zone::utc()) ==
        "Backup · times corrected 8 h · in use, fills 3 channels · problem: HTTP 500");
  b.used = false;
  CHECK(b.line(time::Zone::utc()).find("standing by") != std::string::npos);
}
