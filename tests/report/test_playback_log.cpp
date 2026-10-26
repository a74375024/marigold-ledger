#include <string>

#include "core/playback/recovery.hpp"
#include "core/report/playback_log.hpp"
#include "doctest.h"

using marigold::report::isoTimeMs;
using marigold::report::PlaybackLog;
namespace mtime = marigold::time;

namespace {
// 2026-10-26 18:05:09.120 UTC.
constexpr int64_t kAt = 1793037909120;
}

TEST_CASE("playback log: times are ISO 8601 with milliseconds and the zone's offset") {
  CHECK(isoTimeMs(kAt, mtime::Zone::utc()) == "2026-10-26T18:05:09.120Z");
  CHECK(isoTimeMs(kAt, mtime::Zone::fixed(-4 * 3600)) == "2026-10-26T14:05:09.120-04:00");
  CHECK(isoTimeMs(kAt, mtime::Zone::fixed(5 * 3600 + 1800)) == "2026-10-26T23:35:09.120+05:30");
  CHECK(isoTimeMs(kAt, mtime::Zone::usEastern()) == "2026-10-26T14:05:09.120-04:00");
  CHECK(isoTimeMs(1000, mtime::Zone::utc()) == "1970-01-01T00:00:01.000Z");
  CHECK(isoTimeMs(-1, mtime::Zone::utc()) == "1969-12-31T23:59:59.999Z");
}

TEST_CASE("playback log: sessions number tune starts") {
  PlaybackLog log;
  CHECK(log.lastSession() == 0);
  CHECK(log.newSession() == 1);
  CHECK(log.newSession() == 2);
  CHECK(log.lastSession() == 2);
}

TEST_CASE("playback log: CSV export, newest last, cells quoted") {
  PlaybackLog log;
  int s = log.newSession();
  log.add(kAt, "tune", s, "US: ESPN FHD", "US: ESPN HD", "Provider A", "3 version(s)");
  log.add(kAt + 1500, "first frame", s, "US: ESPN FHD", "US: ESPN HD", "Provider A",
          "picture after 1.5 s, \"fast\"");
  log.addLoad(kAt + 2000, "channels", "12000 in 4.2 s");
  CHECK(log.size() == 3);
  CHECK(log.csv(mtime::Zone::utc()) ==
        "time,session,event,channel,feed,provider,detail\n"
        "\"2026-10-26T18:05:09.120Z\",\"1\",\"tune\",\"US: ESPN FHD\",\"US: ESPN HD\",\"Provider A\",\"3 version(s)\"\n"
        "\"2026-10-26T18:05:10.620Z\",\"1\",\"first frame\",\"US: ESPN FHD\",\"US: ESPN HD\",\"Provider A\","
        "\"picture after 1.5 s, \"\"fast\"\"\"\n"
        "\"2026-10-26T18:05:11.120Z\",\"0\",\"load: channels\",\"\",\"\",\"\",\"12000 in 4.2 s\"\n");
  CHECK(PlaybackLog().csv() == "time,session,event,channel,feed,provider,detail\n");
}

TEST_CASE("playback log: text export, one event per line") {
  PlaybackLog log;
  log.add(kAt, "switch", 4, "UK| Sky Sports Main", "UK| Sky Sports Main HD", "Provider B",
          "stalled 10 s\nmoving on");
  log.add(kAt, "load: guide", 0, "", "", "", "");
  CHECK(log.text(mtime::Zone::utc()) ==
        "2026-10-26T18:05:09.120Z  #4  switch  UK| Sky Sports Main / UK| Sky Sports Main HD "
        "(Provider B)  stalled 10 s moving on\n"
        "2026-10-26T18:05:09.120Z  #0  load: guide\n");
}

TEST_CASE("playback log: links and credentials never stay in a line") {
  PlaybackLog log;
  log.add(kAt, "error", 1, "US: NBC 4", "US: NBC 4 HD", "Provider A",
          "can't open HTTP://example.invalid:8080/live/someone/secret/123.ts (403)");
  log.add(kAt, "error", 1, "US: NBC 4", "rtmp://example.invalid/x", "Provider A",
          "retry with password=hunter2 and token=abc");
  const auto& e = log.entries();
  CHECK(e[0].detail == "can't open <link> (403)");
  CHECK(e[1].feed == "<link>");
  CHECK(e[1].detail == "retry with password=*** and token=***");
  std::string all = log.csv() + log.text() + log.toJson();
  CHECK(all.find("example.invalid") == std::string::npos);
  CHECK(all.find("hunter2") == std::string::npos);
  CHECK(all.find("secret") == std::string::npos);
}

TEST_CASE("playback log: bounded to the newest 4000 events and capped fields") {
  PlaybackLog log;
  for (int i = 0; i < 4100; ++i) log.add(kAt + i, "tick", i, "", "", "", std::to_string(i));
  CHECK(log.size() == PlaybackLog::kCap);
  CHECK(log.entries().front().detail == "100");
  CHECK(log.entries().back().detail == "4099");

  PlaybackLog one;
  // A long detail is cut at a character boundary ("é" is two bytes).
  std::string longText(PlaybackLog::kFieldCap - 1, 'x');
  longText += "\xC3\xA9 and more";
  one.add(kAt, "error", 1, "", "", "", longText);
  CHECK(one.entries()[0].detail.size() == PlaybackLog::kFieldCap - 1);
  one.clear();
  CHECK(one.size() == 0);
}

TEST_CASE("playback log: recovery Log actions feed it, other actions don't") {
  marigold::playback::Action logAction;
  logAction.kind = marigold::playback::Action::Kind::Log;
  logAction.text = "reconnect";
  logAction.detail = "stall 10 s at https://example.invalid/a.m3u8";
  marigold::playback::Action note;
  note.kind = marigold::playback::Action::Kind::Note;
  note.text = "Reconnecting…";
  PlaybackLog log;
  int s = log.newSession();
  log.add(kAt, logAction, s, "US: ESPN FHD", "US: ESPN HD", "Provider A");
  log.add(kAt, note, s, "US: ESPN FHD");
  REQUIRE(log.size() == 1);
  CHECK(log.entries()[0].event == "reconnect");
  CHECK(log.entries()[0].detail == "stall 10 s at <link>");
  CHECK(log.entries()[0].session == s);

  // Every Log action a real recovery run produces lands safely.
  marigold::playback::Recovery recovery;
  PlaybackLog fromRun;
  int run = fromRun.newSession();
  auto feed = [&](const marigold::playback::Actions& actions) {
    for (const auto& a : actions) fromRun.add(0, a, run, "US: ESPN FHD");
  };
  feed(recovery.tune(0, {{"xt:a|1", marigold::playback::FeedKind::Xtream},
                         {"xt:a|2", marigold::playback::FeedKind::Xtream}}));
  feed(recovery.tick(400));
  CHECK(fromRun.size() > 0);
}

TEST_CASE("playback log: saved and restored") {
  PlaybackLog log;
  log.newSession();
  int s = log.newSession();
  log.add(kAt, "tune", s, "US: ESPN FHD", "US: ESPN HD", "Provider A", "1 version(s)");
  log.add(kAt + 5, "first frame", s, "US: ESPN FHD", "", "", "0.8 s");
  uint64_t before = log.revision();
  std::string saved = log.toJson();

  PlaybackLog back;
  REQUIRE(back.restore(saved));
  CHECK(back.size() == 2);
  CHECK(back.lastSession() == 2);
  CHECK(back.csv(mtime::Zone::utc()) == log.csv(mtime::Zone::utc()));
  CHECK(back.newSession() == 3);
  CHECK(log.revision() == before);

  PlaybackLog bad;
  bad.add(kAt, "kept", 1);
  CHECK_FALSE(bad.restore("not json"));
  CHECK_FALSE(bad.restore("{\"at\": 1}"));
  CHECK(bad.size() == 1);
  // A saved file edited to hold a link is scrubbed on the way in.
  REQUIRE(bad.restore("[{\"at\": 5, \"session\": 1, \"event\": \"error\", "
                      "\"detail\": \"see http://example.invalid/x\"}, {\"session\": 2}]"));
  CHECK(bad.size() == 1);
  CHECK(bad.entries()[0].detail == "see <link>");
}
