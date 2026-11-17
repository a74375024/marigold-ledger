// Build 166, Test all sources on the pretend TV: the plan made
// from My Lineup, the probe (MPEG-TS sequence headers, HLS playlists, a
// timeout), the runner's pacing (one connection per provider, providers side
// by side, spaced gaps), refusals, watching, a restart, and what the results
// say (the ranking, the same service underneath, keeping one or two, found
// so far), with never a link or a login in the log and never a count of
// channels in the words on screen.
#include <doctest.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/engine/archive_timing.hpp"
#include "core/engine/source_probe.hpp"
#include "core/engine/source_test.hpp"
#include "core/engine/store.hpp"
#include "core/media/hls.hpp"
#include "core/sources/xtream.hpp"
#include "core/util/json.hpp"
#include "engine/loaded_store.hpp"
#include "playback/ts_builder.hpp"
#include "sources/mock_provider.hpp"

using marigold::engine::AccountForm;
using marigold::engine::Feed;
using marigold::engine::Store;
namespace mtime = marigold::time;
namespace st = marigold::engine::sourcetest;
namespace probe = marigold::engine::probe;
namespace scores = marigold::engine::scores;
namespace hls = marigold::media::hls;
using State = st::Runner::Provider::State;

namespace {

constexpr const char* kTrexHost = "http://trex.example.invalid:8080";

std::string stream(int id, const std::string& name, int category, int archiveDays) {
  return "{\"num\":" + std::to_string(id) + ",\"name\":\"" + name + "\",\"stream_type\":\"live\",\"stream_id\":" +
         std::to_string(id) + ",\"stream_icon\":\"\",\"epg_channel_id\":null,\"category_id\":\"" +
         std::to_string(category) + "\",\"tv_archive\":" + (archiveDays ? "1" : "0") +
         ",\"tv_archive_duration\":\"" + std::to_string(archiveDays) + "\"}";
}

std::string accountInfo() {
  mtime::Seconds now = mtime::now();
  mtime::Civil c = mtime::civilFromSeconds(now);
  char when[32];
  std::snprintf(when, sizeof when, "%04d-%02d-%02d %02d:%02d:%02d", c.year, c.month, c.day, c.hour, c.minute,
                c.second);
  return R"({"user_info":{"auth":1,"status":"Active","exp_date":null,"max_connections":"1",)"
         R"("allowed_output_formats":["m3u8","ts"]},"server_info":{"timezone":"UTC",)"
         R"("timestamp_now":)" + std::to_string(now) + R"(,"time_now":")" + when + R"("}})";
}

marigold::sources::xtream::Client client(const std::string& host) {
  marigold::sources::Account a;
  a.kind = marigold::sources::AccountKind::Xtream;
  a.serverUrl = host;
  a.userName = std::string(mock::kUser);
  a.password = std::string(mock::kPass);
  auto c = marigold::sources::xtream::Client::make(a);
  REQUIRE(c);
  return c.value();
}

// A stream's bytes: `seconds` of video at that size and rate.
std::string ts(int width, int height, double fps, tsgen::Codec codec = tsgen::Codec::H264, int seconds = 3) {
  tsgen::Scene s;
  s.format.codec = codec;
  s.format.width = width;
  s.format.height = height;
  if (codec == tsgen::Codec::Hevc) s.format.profile = 1;
  s.fps = fps;
  s.seconds = seconds;
  s.gopSeconds = 1;
  s.size = [](int, bool intra) { return intra ? 6000u : 900u; };
  tsgen::Bytes b = tsgen::build(s);
  return std::string(b.begin(), b.end());
}

// Padding only: a stream that never shows a picture.
std::string padding() {
  tsgen::Scene s;
  s.tablesOnly = true;
  s.seconds = 40;
  tsgen::Bytes b = tsgen::build(s);
  return std::string(b.begin(), b.end());
}

// A clock that moves `step` ms each time it's read.
probe::Clock ticking(uint32_t& now, uint32_t step) {
  return [&now, step] { return now += step; };
}

// Two Xtream providers: Dream (ESPN FHD keeping two days, CNN HD none) and
// Trex (ESPN HD and TNT HD, each keeping a day, or none).
struct Rig {
  fake::Tv tv;
  std::unique_ptr<Store> store;
  std::unique_ptr<st::Runner> runner;
  bool playing = false;
  std::vector<std::string> lines;
  uint32_t clockMs = 0;

  explicit Rig(int trexDays = 1, int dreamDays = 2) {
    auto dream = client(std::string(mock::kHost)), trex = client(kTrexHost);
    for (auto* c : {&dream, &trex}) {
      tv.pool.answer(c->accountInfoRequest().url, accountInfo());
      tv.pool.answer(c->categoriesRequest().url,
                     R"([{"category_id":"1","category_name":"US| SPORTS"},{"category_id":"2","category_name":"US| NEWS"}])");
    }
    tv.pool.answer(dream.streamsRequest().url,
                   "[" + stream(601, "US: ESPN FHD", 1, dreamDays) + "," + stream(604, "US: CNN HD", 2, 0) + "]");
    tv.pool.answer(trex.streamsRequest().url, "[" + stream(701, "US: ESPN HD", 1, trexDays) + "," +
                                                  stream(702, "US: TNT HD", 1, trexDays) + "]");
    store = std::make_unique<Store>(tv.host());
    for (auto [name, host] : {std::pair<const char*, std::string>{"Dream", std::string(mock::kHost)},
                              std::pair<const char*, std::string>{"Trex", std::string(kTrexHost)}}) {
      AccountForm form;
      form.kind = AccountForm::Kind::Xtream;
      form.name = name;
      form.server = host;
      form.user = std::string(mock::kUser);
      form.password = std::string(mock::kPass);
      std::string problem = "(not answered)";
      store->addAccount(form, [&](std::string p) { problem = std::move(p); });
      fake::settle(*store, tv);
      REQUIRE(problem.empty());
    }
    store->addToLineup({entry("ESPN"), entry("CNN"), entry("TNT")});
    fake::settle(*store, tv);
    makeRunner();
  }

  void makeRunner() {
    runner = std::make_unique<st::Runner>(*store, tv.host());
    runner->playing = [this] { return playing; };
    runner->log = [this](std::string_view event, std::string_view detail) {
      CHECK(event == "test");
      lines.emplace_back(detail);
    };
    runner->clock = [this] { return clockMs += 25; };
  }

  std::string entry(const std::string& name) const {
    for (const auto& g : store->channels()->all)
      if (g.name.find(name) != std::string::npos) return g.id;
    return {};
  }
  std::string copy(int id) const {
    std::string tail = "|" + std::to_string(id);
    for (const auto& r : store->lineupRows())
      for (const auto& c : r.copies)
        if (c.feedId.size() > tail.size() && c.feedId.substr(c.feedId.size() - tail.size()) == tail) return c.feedId;
    return {};
  }
  std::string liveUrl(int id) const {
    auto f = store->copyFeed(copy(id));
    REQUIRE(f);
    return f->url;
  }
  // The archive link the runner asks for now, `back` seconds back.
  std::string archiveUrl(int id, mtime::Seconds back) const {
    auto f = store->copyFeed(copy(id));
    REQUIRE(f);
    mtime::Seconds now = mtime::now();
    auto c = store->catchupFeed(*f, now - back, now);
    REQUIRE(c);
    return c->feed.url;
  }
  // The app's loop for `ms`.
  void run(uint32_t ms, uint32_t step = 500) {
    for (uint32_t t = 0; t < ms; t += step) {
      tv.ms += step;
      runner->tick(tv.ms);
      tv.pool.drain();
    }
  }
  const st::Runner::Provider* provider(const std::string& name) const {
    static st::Runner::Status s;
    s = runner->status();
    for (const auto& p : s.providers)
      if (p.name == name) return &p;
    return nullptr;
  }
  // Never a link, a host or the login in the test's log lines.
  void noLinks() const {
    for (const std::string& l : lines) {
      CHECK(l.find("://") == std::string::npos);
      CHECK(l.find("example.invalid") == std::string::npos);
      CHECK(l.find("/u/p/") == std::string::npos);
      CHECK(l.find("timeshift") == std::string::npos);
      CHECK(l.find("password") == std::string::npos);
      CHECK(l.find("username") == std::string::npos);
    }
  }
  bool logged(const std::string& part) const {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return l.find(part) != std::string::npos; });
  }
};

// No count of channels (or sources, or copies) in words shown on screen.
void noCounts(const std::string& s) {
  for (size_t i = 0; i + 1 < s.size(); ++i) {
    if (s[i] < '0' || s[i] > '9' || s[i + 1] != ' ') continue;
    std::string rest = s.substr(i + 2, 8);
    CHECK_MESSAGE(rest.rfind("channel", 0) != 0, s);
    CHECK_MESSAGE(rest.rfind("source", 0) != 0, s);
    CHECK_MESSAGE(rest.rfind("cop", 0) != 0, s);
    CHECK_MESSAGE(rest.rfind("of ", 0) != 0, s);
  }
}

}  // namespace

// ------------------------------------------------------------- the probe

TEST_CASE("test all sources: the sequence header's size and rate, H.264 and HEVC") {
  struct Case {
    tsgen::Codec codec;
    int w, h;
    double fps;
    int wantFps;
  };
  for (Case c : {Case{tsgen::Codec::H264, 1920, 1080, 60, 60}, Case{tsgen::Codec::H264, 1280, 720, 59.94, 60},
                 Case{tsgen::Codec::H264, 3840, 2160, 50, 50}, Case{tsgen::Codec::Hevc, 1920, 1080, 60, 60},
                 Case{tsgen::Codec::Hevc, 1280, 720, 50, 50}, Case{tsgen::Codec::Hevc, 3840, 2160, 60, 60}}) {
    fake::Tv tv;
    const std::string url = "http://probe.example.invalid/live/1.ts";
    tv.pool.answer(url, ts(c.w, c.h, c.fps, c.codec));
    uint32_t now = 0;
    probe::Outcome o = probe::run(tv.pool, {url, {}}, ticking(now, 10), nullptr);
    CHECK(o.ok);
    CHECK_FALSE(o.refused);
    CHECK(o.width == c.w);
    CHECK(o.height == c.h);
    CHECK(o.fps == c.wantFps);
    CHECK(o.firstByteMs > 0);
    CHECK(o.pictureMs >= o.firstByteMs);
    CHECK(o.pictureMs < probe::kTimeoutMs);
    CHECK_FALSE(o.hls);
  }
}

TEST_CASE("test all sources: no picture within the time allowed is a failure; refusals are told apart") {
  fake::Tv tv;
  const std::string url = "http://probe.example.invalid/live/2.ts";
  tv.pool.answer(url, padding());
  uint32_t now = 0;
  probe::Outcome o = probe::run(tv.pool, {url, {}}, ticking(now, 200), nullptr);
  CHECK_FALSE(o.ok);
  CHECK_FALSE(o.refused);
  CHECK(o.why == "no picture in 12 s");
  CHECK(o.why.find("://") == std::string::npos);

  // Nothing answers: failed, not refused.
  probe::Outcome off = probe::run(tv.pool, {"http://probe.example.invalid/live/3.ts", {}}, ticking(now, 10), nullptr);
  CHECK_FALSE(off.ok);
  CHECK_FALSE(off.refused);

  // Refused: these statuses, a refused connection, connections used up.
  for (int status : {403, 429, 451, 458}) {
    const std::string u = "http://probe.example.invalid/live/r" + std::to_string(status) + ".ts";
    tv.pool.answer(u, "", status);
    probe::Outcome r = probe::run(tv.pool, {u, {}}, ticking(now, 10), nullptr);
    CHECK(r.refused);
    CHECK(r.why == "http " + std::to_string(status));
  }
  tv.pool.answer("http://probe.example.invalid/live/404.ts", "", 404);
  CHECK_FALSE(probe::run(tv.pool, {"http://probe.example.invalid/live/404.ts", {}}, ticking(now, 10), nullptr).refused);
  tv.pool.answer("http://probe.example.invalid/live/max.ts", "Max connections reached for this line.");
  CHECK(probe::run(tv.pool, {"http://probe.example.invalid/live/max.ts", {}}, ticking(now, 10), nullptr).refused);
  CHECK(probe::refusal(0, "transport (connect)", ""));
  CHECK(probe::refusal(0, "transport (curl 7)", ""));
  CHECK_FALSE(probe::refusal(0, "timeout", ""));
  CHECK_FALSE(probe::refusal(200, "", "#EXTM3U"));
}

TEST_CASE("test all sources: HLS, master to the highest variant to the segment a player starts with") {
  fake::Tv tv;
  const std::string base = "http://hls.example.invalid/live/ch1/";
  tv.pool.answer(base + "master.m3u8?t=1",
                 "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=800000,RESOLUTION=640x360\nlow/index.m3u8\n"
                 "#EXT-X-STREAM-INF:BANDWIDTH=6000000,RESOLUTION=1920x1080,FRAME-RATE=59.940,CODECS=\"avc1.640028,mp4a.40.2\"\n"
                 "hi/index.m3u8\n");
  tv.pool.answer(base + "hi/index.m3u8",
                 "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-MEDIA-SEQUENCE:100\n"
                 "#EXTINF:6.0,\nseg100.ts\n#EXTINF:6.0,\nseg101.ts\n#EXTINF:6.0,\nseg102.ts\n"
                 "#EXTINF:6.0,\nseg103.ts\n#EXTINF:6.0,\n/live/ch1/hi/seg104.ts\n");
  tv.pool.answer(base + "hi/seg102.ts", ts(1920, 1080, 60));
  uint32_t now = 0;
  probe::Outcome o = probe::run(tv.pool, {base + "master.m3u8?t=1", {"User-Agent: Test"}}, ticking(now, 10), nullptr);
  CHECK(o.ok);
  CHECK(o.hls);
  CHECK(o.height == 1080);
  CHECK(o.fps == 60);
  CHECK(tv.pool.asked[base + "low/index.m3u8"] == 0);
  CHECK(tv.pool.asked[base + "hi/seg102.ts"] == 1);
  CHECK(tv.pool.asked[base + "hi/seg104.ts"] == 0);

  // The parser on its own.
  hls::Playlist p = hls::parse("#EXTM3U\n#EXTINF:4,\na.ts\n#EXTINF:4,\nb.ts\n#EXT-X-ENDLIST\n");
  CHECK_FALSE(p.master);
  REQUIRE(hls::pickSegment(p));
  CHECK(hls::pickSegment(p)->uri == "a.ts");  // not live: from the start
  CHECK(hls::resolve("http://h.example.invalid/a/b/c.m3u8?x=1", "d.ts") == "http://h.example.invalid/a/b/d.ts");
  CHECK(hls::resolve("http://h.example.invalid/a/b/c.m3u8", "/e.ts") == "http://h.example.invalid/e.ts");
  CHECK(hls::resolve("https://h.example.invalid/a/c.m3u8", "//cdn.example.invalid/f.ts") ==
        "https://cdn.example.invalid/f.ts");
  CHECK(hls::resolve("http://h.example.invalid/a/c.m3u8", "http://o.example.invalid/g.ts") ==
        "http://o.example.invalid/g.ts");
  CHECK(hls::looksLikePlaylist("\xEF\xBB\xBF  #EXTM3U\n"));
  // A segment that isn't MPEG-TS (fMP4) plays, with the master's size.
  tv.pool.answer("http://hls.example.invalid/f/master.m3u8",
                 "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=5000000,RESOLUTION=1280x720\nv.m3u8\n");
  tv.pool.answer("http://hls.example.invalid/f/v.m3u8",
                 "#EXTM3U\n#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:4,\ns1.m4s\n#EXTINF:4,\ns2.m4s\n#EXTINF:4,\ns3.m4s\n");
  tv.pool.answer("http://hls.example.invalid/f/s1.m4s", std::string(5000, 'x'));
  probe::Outcome f = probe::run(tv.pool, {"http://hls.example.invalid/f/master.m3u8", {}}, ticking(now, 10), nullptr);
  CHECK(f.ok);
  CHECK(f.height == 720);
}

// -------------------------------------------------------------- the plan

TEST_CASE("test all sources: the plan, from My Lineup: every copy once, the archive only where it's kept") {
  std::vector<scores::LineupRow> rows = {
      {"e|espn", "ESPN", {{"dream|1", "dream", "US: ESPN FHD", 2}, {"trex|1", "trex", "US| ESPN HD", 1},
                          {"emby|9", "emby", "ESPN", 0}}},
      {"e|cnn", "CNN", {{"dream|2", "dream", "US: CNN", 0}, {"xt|2", "xtream", "US| CNN", 0}}},
      {"e|cnn2", "CNN (again)", {{"dream|2", "dream", "US: CNN", 0}}},
      {"e|gone", "Gone", {{"old|1", "old", "Gone", 0}}},
  };
  st::Plan p = st::plan(rows, {"dream", "xtream", "trex", "emby"},
                        [](const std::string& c) { return c.rfind("emby|", 0) != 0; });
  REQUIRE(p.queues.size() == 4);
  auto kinds = [](const std::vector<st::Item>& q) {
    std::string out;
    for (const st::Item& i : q) out += i.channelId + (i.kind == st::Kind::Archive ? "A " : i.kind == st::Kind::Depth ? "D " : "L ");
    return out;
  };
  CHECK(kinds(p.queues[0]) == "dream|1A dream|1D dream|1L dream|2L ");
  CHECK(kinds(p.queues[1]) == "xt|2L ");
  CHECK(kinds(p.queues[2]) == "trex|1A trex|1D trex|1L ");
  CHECK(p.queues[3].empty());  // a server opens these: not read directly
  CHECK(p.total() == 8);
  CHECK(st::key("dream|1", st::Kind::Depth) == "dream|1#D");

  // The estimate: the longest queue at about 11.5 s a test.
  CHECK(st::estimateSeconds(p) == 4 * st::kItemMs / 1000);
  // 1,631 copies over three providers (Dream 573 with 175 archives, Xtream
  // 740, Trex 318 with 191): about three hours.
  st::Plan big;
  big.queues = {std::vector<st::Item>(573 + 2 * 175), std::vector<st::Item>(740), std::vector<st::Item>(318 + 2 * 191)};
  CHECK(st::estimateText(st::estimateSeconds(big)) == "About 3 to 4 hours");
  CHECK(st::estimateText(1500) == "About 30 minutes");
  CHECK(st::timeLeftText(7800000) == "about 2 h 10 min left");
  CHECK(st::timeLeftText(25 * 60000) == "about 25 min left");
  CHECK(st::timeLeftText(30000) == "almost done");
}

// ------------------------------------------------------------ the runner

TEST_CASE("test all sources: one connection per provider, providers side by side, spaced gaps; results recorded") {
  Rig r;
  std::string espnDream = r.liveUrl(601), cnn = r.liveUrl(604), espnTrex = r.liveUrl(701), tnt = r.liveUrl(702);
  r.tv.pool.answer(espnDream, ts(1280, 720, 60));  // says FHD, plays 720p
  r.tv.pool.answer(espnTrex, ts(1920, 1080, 60));
  r.tv.pool.answer(tnt, ts(1920, 1080, 30));
  // CNN never answers: it didn't start.
  r.tv.pool.answer(r.archiveUrl(601, st::kArchiveBack), ts(1920, 1080, 30));
  r.tv.pool.answer(r.archiveUrl(601, 2 * 86400 - st::kEdgeMargin), ts(1920, 1080, 30));
  r.tv.pool.answer(r.archiveUrl(701, st::kArchiveBack), ts(1920, 1080, 30));
  // Trex's edge (a day back) doesn't play.
  r.tv.pool.answer(r.archiveUrl(702, st::kArchiveBack), ts(1920, 1080, 30));

  CHECK_FALSE(r.runner->running());
  CHECK(r.runner->estimateNow() == 6 * st::kItemMs / 1000);  // Trex: two copies, archive and edge each
  r.runner->start();
  CHECK(r.runner->running());
  // One tick: both providers have a probe out, one each.
  r.tv.ms += 500;
  r.runner->tick(r.tv.ms);
  {
    auto s = r.runner->status();
    REQUIRE(s.providers.size() == 2);
    CHECK(s.providers[0].state == State::Testing);
    CHECK(s.providers[1].state == State::Testing);
    CHECK(st::providerLine(s.providers[0], mtime::now()).rfind("Now: ", 0) == 0);
  }
  r.tv.pool.drain();
  // Each provider's next test only after a gap of 5 to 10 s.
  std::map<std::string, std::vector<uint32_t>> starts;
  std::map<std::string, State> was;
  for (int i = 0; i < 400 && r.runner->running(); ++i) {
    r.tv.ms += 250;
    r.runner->tick(r.tv.ms);
    for (const auto& p : r.runner->status().providers) {
      if (p.state == State::Testing && was[p.name] != State::Testing) starts[p.name].push_back(r.tv.ms);
      was[p.name] = p.state;
    }
    r.tv.pool.drain();
    for (auto& [name, state] : was) state = State::Waiting;  // drained: answered
  }
  CHECK_FALSE(r.runner->running());
  CHECK(r.runner->lastTested() > 0);
  for (const auto& [name, at] : starts)
    for (size_t i = 1; i < at.size(); ++i) {
      CHECK(at[i] - at[i - 1] >= st::kGapMinMs);
      CHECK(at[i] - at[i - 1] <= st::kGapMaxMs + 500);
    }
  // Every test after the first (which went out before): Dream's four, Trex's
  // six, and (Build 172) the timing of each archive that played, in a turn
  // of its own: one on Dream, two on Trex.
  CHECK(starts["Dream"].size() == 4);
  CHECK(starts["Trex"].size() == 7);

  // Into the records: the live probe with what the archive did.
  const scores::Record* d = r.store->feedScores().find(r.copy(601));
  REQUIRE(d);
  CHECK(d->probes == doctest::Approx(1));
  CHECK(d->height == 720);
  CHECK(d->fps == 60);
  CHECK(d->archiveOk == 1);
  CHECK(d->archiveDays == 2);
  CHECK(d->probedAt > 0);
  const scores::Record* t = r.store->feedScores().find(r.copy(701));
  REQUIRE(t);
  CHECK(t->archiveOk == 1);
  CHECK(t->archiveDays == 0);  // its edge didn't play
  const scores::Record* c = r.store->feedScores().find(r.copy(604));
  REQUIRE(c);
  CHECK(c->probeFailures == doctest::Approx(1));
  // Each link once (ESPN's live twice: its timing read it too).
  CHECK(r.tv.pool.asked[espnDream] == 2);
  CHECK(r.tv.pool.asked[cnn] == 1);

  // The Sources panel's lines.
  mtime::Seconds now = mtime::now();
  CHECK(st::copyLine(d, r.store->claimedQuality(r.copy(601)), 2, now).rfind("720p 60 (says HD) \xc2\xb7 starts in ", 0) == 0);
  CHECK(st::copyLine(d, "FHD", 2, now).find("replay 2 days") != std::string::npos);
  CHECK(st::copyLine(t, "HD", 1, now).find("1080p 60") == 0);
  CHECK(st::copyLine(t, "HD", 1, now).find("replay under 1 day") != std::string::npos);
  CHECK(st::copyLine(c, "HD", 0, now) == "Didn't start \xc2\xb7 tried last");
  CHECK(st::copyLine(nullptr, "HD", 0, now).empty());

  // Found so far.
  std::vector<std::string> found = r.runner->foundSoFar();
  REQUIRE(found.size() == 3);
  CHECK(found[0] == "Some sources didn't start; they'll be tried last.");
  CHECK(found[1] == "A few say HD in their name but play at 720p.");
  CHECK(found[2] == "Replay goes back further on Dream than on Trex for the channels both carry.");

  // The log: a line per provider's start and end, and a summary; no link.
  CHECK(r.logged("Dream: started"));
  CHECK(r.logged("Trex: started"));
  CHECK(r.logged("Dream: done"));
  CHECK(r.logged("finished"));
  CHECK(r.logged("summary: Dream: started 50%"));
  r.noLinks();

  // The results' figures: the archive as the probes found it.
  scores::Rollup roll = r.store->providerRollup();
  REQUIRE(roll.providers.size() == 2);
  CHECK(roll.providers[0].replayWorks == doctest::Approx(1.0 / 3));
  CHECK(roll.providers[0].replayDays == 2);
  CHECK(roll.providers[1].replayDays == 1);
  REQUIRE(roll.providers[0].startFailures);
  CHECK(*roll.providers[0].startFailures == doctest::Approx(0.5));
  REQUIRE(roll.providers[1].fullHdCopies);
  CHECK(*roll.providers[1].fullHdCopies == doctest::Approx(1.0));
  CHECK(st::replayCell(roll.providers[0]) == "33% \xc2\xb7 2 days");
}

TEST_CASE("test all sources: a probe's result orders the channel's copies") {
  Rig r(0, 0);
  std::string espn = r.entry("ESPN");
  std::vector<Feed> before = r.store->feeds(espn);
  REQUIRE(before.size() == 2);
  // Whichever copy starts first today doesn't start; the other does.
  std::string first = before[0].channelId, second = before[1].channelId;
  for (int id : {601, 701, 604, 702})
    if (r.copy(id) != first) r.tv.pool.answer(r.liveUrl(id), ts(1920, 1080, 60));
  r.runner->start();
  r.run(120000);
  CHECK_FALSE(r.runner->running());
  std::vector<Feed> after = r.store->feeds(espn);
  REQUIRE(after.size() == 2);
  CHECK(after[0].channelId == second);
  CHECK(after[1].channelId == first);  // tried last
  // A copy that played is measured on its rows too.
  CHECK(after[0].quality.find("1080p") == 0);
}

TEST_CASE("test all sources: a refusal pauses that provider for 15 minutes; a second one stops it") {
  Rig r(0, 0);  // no archives: live tests only
  r.tv.pool.answer(r.liveUrl(701), "", 429);
  r.tv.pool.answer(r.liveUrl(702), "", 429);
  r.tv.pool.answer(r.liveUrl(601), ts(1920, 1080, 60));
  r.tv.pool.answer(r.liveUrl(604), ts(1920, 1080, 60));
  r.runner->start();
  r.run(1000);
  const st::Runner::Provider* trex = r.provider("Trex");
  REQUIRE(trex);
  CHECK(trex->state == State::Paused);
  CHECK(st::providerLine(*trex, mtime::now()) ==
        "It refused a connection. Trying again in 15 min; the others carry on.");
  CHECK(st::providerCorner(*trex) == "Paused");
  CHECK(r.logged("Trex: paused, it refused a connection (http 429); trying again in 15 min"));
  // The others carry on meanwhile.
  std::string a = r.liveUrl(701), b = r.liveUrl(702);
  r.run(30000);
  CHECK(r.provider("Dream")->state == State::Done);
  CHECK(r.provider("Trex")->state == State::Paused);
  CHECK(r.tv.pool.asked[a] + r.tv.pool.asked[b] == 1);
  CHECK(r.runner->running());
  // Fifteen minutes on, it's tried again; refused again, it's left out.
  mtime::advanceForTests(15 * 60 + 1);
  r.run(2000);
  CHECK(r.tv.pool.asked[a] + r.tv.pool.asked[b] == 2);
  CHECK((r.tv.pool.asked[a] == 2 || r.tv.pool.asked[b] == 2));  // the same copy again
  CHECK(r.logged("Trex: trying again"));
  CHECK(r.logged("Trex: stopped, it refused again (http 429)"));
  CHECK_FALSE(r.runner->running());  // nothing left: the run ends
  CHECK(r.logged("(stopped: it refused)"));
  r.noLinks();
}

TEST_CASE("test all sources: watching pauses the test; it carries on after") {
  Rig r(0, 0);
  for (int id : {601, 604, 701, 702}) r.tv.pool.answer(r.liveUrl(id), ts(1920, 1080, 60));
  r.runner->start();
  r.tv.ms += 500;
  r.runner->tick(r.tv.ms);  // two probes out
  r.playing = true;
  r.tv.ms += 100;
  r.runner->tick(r.tv.ms);
  r.tv.pool.drain();  // they stop at their first piece: nothing recorded
  CHECK(r.store->feedScores().size() == 0);
  auto s = r.runner->status();
  CHECK(s.waitingForPlay);
  CHECK(st::headerLine(s) == "0% \xc2\xb7 paused while you watch");
  CHECK(r.logged("paused while you watch"));
  r.run(60000);
  CHECK(r.store->feedScores().size() == 0);
  // Watching ends: a short wait, then the test carries on.
  r.playing = false;
  r.run(9000);
  CHECK(r.store->feedScores().size() == 0);
  r.run(2000);
  CHECK(r.store->feedScores().size() == 2);
  CHECK(r.logged("carried on"));
  r.run(60000);
  CHECK_FALSE(r.runner->running());
  CHECK(r.store->feedScores().size() == 4);
}

TEST_CASE("test all sources: progress is kept across a restart; a run older than two days is dropped") {
  Rig r(0, 0);
  for (int id : {601, 604, 701, 702}) r.tv.pool.answer(r.liveUrl(id), ts(1920, 1080, 60));
  r.runner->start();
  r.run(1000);  // one test each
  CHECK(r.store->feedScores().size() == 2);
  r.runner->saveNow();
  r.tv.pool.drain();
  r.runner.reset();
  // The app starts again: the test carries on with what's left.
  r.makeRunner();
  CHECK(r.runner->running());
  r.run(60000);
  CHECK_FALSE(r.runner->running());
  for (int id : {601, 604, 701, 702}) CHECK(r.tv.pool.asked[r.liveUrl(id)] == 1);
  CHECK(r.logged("carried on"));
  mtime::Seconds tested = r.runner->lastTested();
  CHECK(tested > 0);

  // A run left for more than two days isn't carried on.
  r.runner->start();
  r.run(1000);
  r.runner.reset();
  r.tv.pool.drain();
  mtime::advanceForTests(2 * 86400 + 60);
  r.makeRunner();
  CHECK_FALSE(r.runner->running());
  CHECK(r.runner->lastTested() == tested);  // the last finished run still counts
  r.run(1000);
  CHECK(r.logged("dropped"));
  auto saved = r.tv.files.read(st::kFile);
  REQUIRE(saved);
  CHECK(saved->find("example.invalid") == std::string::npos);
  CHECK(saved->find("\"run\"") == std::string::npos);
}

TEST_CASE("test all sources: stopping keeps what was found so far") {
  Rig r(0, 0);
  for (int id : {601, 604, 701, 702}) r.tv.pool.answer(r.liveUrl(id), ts(1920, 1080, 60));
  int ended = r.runner->endedSerial();
  r.runner->start();
  r.run(1000);
  r.runner->stop();
  CHECK_FALSE(r.runner->running());
  CHECK(r.runner->endedSerial() == ended + 1);
  CHECK(r.store->feedScores().size() == 2);
  CHECK(r.logged("stopped by you"));
  r.run(30000);
  CHECK(r.store->feedScores().size() == 2);
}

TEST_CASE("test all sources 1.0.17: a test stopped part way carries on from where it stopped") {
  Rig r(0, 0);
  for (int id : {601, 604, 701, 702}) r.tv.pool.answer(r.liveUrl(id), ts(1920, 1080, 60));
  CHECK_FALSE(r.runner->canCarryOn());
  CHECK_FALSE(r.runner->carry());
  r.runner->start();
  r.run(1000);  // one test each
  r.runner->stop();
  CHECK_FALSE(r.runner->running());
  CHECK(r.runner->canCarryOn());
  CHECK(r.logged("it can carry on from here this week"));
  auto c = r.runner->carry();
  REQUIRE(c);
  CHECK(c->fraction == doctest::Approx(0.5));
  CHECK(c->stoppedAt > 0);
  CHECK(c->leftSeconds > 0);
  REQUIRE(c->providers.size() == 2);
  for (const auto& [id, share] : c->providers) CHECK(share == doctest::Approx(0.5));
  // Nothing more runs while it's stopped.
  r.run(30000);
  CHECK(r.store->feedScores().size() == 2);

  // Kept across a restart, still stopped.
  r.runner->saveNow();
  r.tv.pool.drain();
  r.runner.reset();
  r.makeRunner();
  CHECK_FALSE(r.runner->running());
  CHECK(r.runner->canCarryOn());
  REQUIRE(r.runner->carry());
  CHECK(r.runner->carry()->fraction == doctest::Approx(0.5));

  // Carry on: only what's left is tested.
  r.runner->carryOn();
  CHECK(r.runner->running());
  CHECK(r.logged("carried on from 50%"));
  r.run(60000);
  CHECK_FALSE(r.runner->running());
  for (int id : {601, 604, 701, 702}) CHECK(r.tv.pool.asked[r.liveUrl(id)] == 1);
  CHECK(r.store->feedScores().size() == 4);
  CHECK(r.logged("finished"));
  // Done: nothing to carry on, and the saved file holds no run.
  CHECK_FALSE(r.runner->canCarryOn());
  r.runner->saveNow();
  r.tv.pool.drain();
  auto saved = r.tv.files.read(st::kFile);
  REQUIRE(saved);
  CHECK(saved->find("\"run\"") == std::string::npos);
  r.noLinks();
}

TEST_CASE("test all sources 1.0.17: Start over tests everything again; a stopped test is kept a week") {
  Rig r(0, 0);
  for (int id : {601, 604, 701, 702}) r.tv.pool.answer(r.liveUrl(id), ts(1920, 1080, 60));
  r.runner->start();
  r.run(1000);
  r.runner->stop();
  REQUIRE(r.runner->canCarryOn());
  // Start over: from the beginning.
  r.runner->start();
  CHECK_FALSE(r.runner->canCarryOn());
  r.run(60000);
  CHECK_FALSE(r.runner->running());
  // The two tested before Stop, twice; the other two once.
  int asked = 0, twice = 0;
  for (int id : {601, 604, 701, 702}) {
    asked += r.tv.pool.asked[r.liveUrl(id)];
    twice += r.tv.pool.asked[r.liveUrl(id)] == 2;
  }
  CHECK(asked == 6);
  CHECK(twice == 2);

  // Stopped, then left more than a week: dropped.
  r.runner->start();
  r.run(1000);
  r.runner->stop();
  r.runner->saveNow();
  r.tv.pool.drain();
  r.runner.reset();
  mtime::advanceForTests(6 * 86400);
  r.makeRunner();
  CHECK(r.runner->canCarryOn());  // six days: still there
  r.runner.reset();
  mtime::advanceForTests(86400 + 60);
  r.makeRunner();
  CHECK_FALSE(r.runner->canCarryOn());
  CHECK_FALSE(r.runner->running());
  r.run(1000);
  CHECK(r.logged("a test stopped more than a week ago was dropped"));
}

TEST_CASE("test all sources 1.0.17: a run that ended because a provider turned it away carries on, trying it again") {
  Rig r(0, 0);
  r.tv.pool.answer(r.liveUrl(701), "", 429);
  r.tv.pool.answer(r.liveUrl(702), "", 429);
  r.tv.pool.answer(r.liveUrl(601), ts(1920, 1080, 60));
  r.tv.pool.answer(r.liveUrl(604), ts(1920, 1080, 60));
  r.runner->start();
  r.run(31000);
  mtime::advanceForTests(15 * 60 + 1);
  r.run(2000);
  REQUIRE_FALSE(r.runner->running());
  CHECK(r.logged("ended part way (providers turned it away)"));
  REQUIRE(r.runner->canCarryOn());
  auto c = r.runner->carry();
  REQUIRE(c);
  CHECK(c->fraction == doctest::Approx(0.5));
  // Trex works again: carrying on tests only its copies.
  r.tv.pool.answer(r.liveUrl(701), ts(1920, 1080, 60));
  r.tv.pool.answer(r.liveUrl(702), ts(1920, 1080, 60));
  r.runner->carryOn();
  r.run(60000);
  CHECK_FALSE(r.runner->running());
  CHECK(r.tv.pool.asked[r.liveUrl(601)] == 1);
  CHECK(r.tv.pool.asked[r.liveUrl(604)] == 1);
  CHECK(r.store->feedScores().size() == 4);
  CHECK_FALSE(r.runner->canCarryOn());
  r.noLinks();
}

// --------------------------------------------------------- the results

TEST_CASE("test all sources: providers ranked by playing reliably, then speed and picture, then lineup and replay") {
  scores::Rollup r;
  auto fig = [](const char* id, double fails, uint32_t ms, double hd, double lineup, double replay) {
    scores::ProviderFigures f;
    f.provider = id;
    f.tested = 300;
    f.startFailures = fails;
    f.medianStartMs = ms;
    f.fullHdCopies = hd;
    f.lineup = lineup;
    f.replayWorks = replay;
    return f;
  };
  // The mock-up's figures.
  r.providers = {fig("dream", 0.18, 2000, 0.70, 0.77, 0.36), fig("xtream", 0.03, 1800, 0.81, 0.90, 0),
                 fig("trex", 0.03, 1900, 0.84, 0.88, 0.54)};
  std::vector<size_t> order = st::rank(r);
  REQUIRE(order.size() == 3);
  CHECK(r.providers[order[0]].provider == "trex");
  CHECK(r.providers[order[1]].provider == "xtream");
  CHECK(r.providers[order[2]].provider == "dream");
  // The formula: 0.5 works + 0.15 speed + 0.15 picture + 0.12 lineup + 0.08 replay.
  scores::ProviderFigures perfect = fig("p", 0, 1500, 1, 1, 1);
  perfect.tested = 1e9;
  CHECK(st::rankScore(perfect) == doctest::Approx(1.0).epsilon(0.001));
  scores::ProviderFigures unknown;
  CHECK(st::rankScore(unknown) == doctest::Approx(0.5 * 0.5 + 0.15 * 0.5 + 0.15 * 0.5));
  // Reliability first: a provider that fails a lot loses to a slower one.
  r.providers = {fig("fast", 0.30, 1500, 1, 1, 1), fig("steady", 0.02, 6000, 0.3, 0.5, 0)};
  CHECK(r.providers[st::rank(r)[0]].provider == "steady");
}

TEST_CASE("test all sources: the same service underneath, and backups from another one") {
  std::vector<scores::LineupRow> rows;
  const char* names[] = {"ESPN", "CNN", "TNT", "AMC", "FX", "TBS"};
  for (int i = 0; i < 6; ++i) {
    std::string n = names[i];
    rows.push_back({"e|" + n, n,
                    {{"dream|" + std::to_string(i), "dream", n + " FHD", 2},
                     {"xt|" + std::to_string(i), "xtream", "US| " + n + " \xe1\xb4\xb4\xe1\xb4\xb0", 0},
                     {"trex|" + std::to_string(i), "trex", "US: " + n + " \xe1\xb4\xb4\xe1\xb4\xb0", 1}}});
  }
  CHECK(st::normalName("US| ESPN  HD ") == "us espn hd");
  CHECK(st::normalName("US: ESPN HD") == "us espn hd");
  scores::Book book;
  mtime::Seconds now = mtime::now();
  auto same = st::sameServices(rows, {"dream", "xtream", "trex"}, book, now);
  REQUIRE(same.size() == 1);
  CHECK(same[0].a == "xtream");
  CHECK(same[0].b == "trex");
  CHECK(same[0].nameShare == doctest::Approx(1.0));
  CHECK_FALSE(same[0].failureOverlap);  // nothing failed: not measurable
  CHECK(st::isSame(same, "trex", "xtream"));
  CHECK_FALSE(st::isSame(same, "dream", "trex"));

  // Their failures go together: still the same service.
  scores::Probe bad;
  for (int i = 0; i < 4; ++i) {
    book.recordProbe("xt|" + std::to_string(i), bad, now);
    book.recordProbe("trex|" + std::to_string(i), bad, now);
  }
  same = st::sameServices(rows, {"dream", "xtream", "trex"}, book, now);
  REQUIRE(same.size() == 1);
  REQUIRE(same[0].failureOverlap);
  CHECK(*same[0].failureOverlap == doctest::Approx(1.0));
  // They fail apart: separate after all.
  scores::Book apart;
  scores::Probe good;
  good.ok = true;
  good.startMs = 1500;
  for (int i = 0; i < 6; ++i) {
    apart.recordProbe("xt|" + std::to_string(i), i < 3 ? bad : good, now);
    apart.recordProbe("trex|" + std::to_string(i), i < 3 ? good : bad, now);
  }
  CHECK(st::sameServices(rows, {"dream", "xtream", "trex"}, apart, now).empty());

  // The backup: after a Trex copy, Dream's rather than Xtream's.
  std::vector<std::string> ids = {"trex|0", "xtream|0", "xtream|9", "dream|0"};
  st::preferSeparateBackup(ids, [&same](std::string_view a, std::string_view b) { return st::isSame(same, a, b); });
  CHECK(ids == std::vector<std::string>{"trex|0", "dream|0", "xtream|0", "xtream|9"});
  // Already from another service, or the same provider's: as it was.
  ids = {"trex|0", "dream|0", "xtream|0"};
  st::preferSeparateBackup(ids, [&same](std::string_view a, std::string_view b) { return st::isSame(same, a, b); });
  CHECK(ids == std::vector<std::string>{"trex|0", "dream|0", "xtream|0"});
  ids = {"trex|0", "trex|1", "xtream|0", "dream|0"};
  st::preferSeparateBackup(ids, [&same](std::string_view a, std::string_view b) { return st::isSame(same, a, b); });
  CHECK(ids == std::vector<std::string>{"trex|0", "trex|1", "xtream|0", "dream|0"});

  // Build 169: copies ordered by their measured picture first (Xtream's
  // and Trex's 1080p ahead of Dream's 720p); the backup still comes from
  // a separate service.
  scores::Book pictures;
  scores::Probe full = good, hd = good;
  full.height = 1080;
  full.fps = 60;
  hd.height = 720;
  hd.fps = 60;
  pictures.recordProbe("xtream|0", full, now);
  pictures.recordProbe("trex|0", full, now);
  pictures.recordProbe("dream|0", hd, now);
  ids = {"dream|0", "xtream|0", "trex|0"};
  scores::orderMeasured(ids, 0, [&](const std::string& id) { return pictures.standing(id, now); });
  CHECK(ids == std::vector<std::string>{"xtream|0", "trex|0", "dream|0"});
  st::preferSeparateBackup(ids, [&same](std::string_view a, std::string_view b) { return st::isSame(same, a, b); });
  CHECK(ids == std::vector<std::string>{"xtream|0", "dream|0", "trex|0"});
}

TEST_CASE("test all sources: keeping one or two, the best pair and the channels you'd lose by name") {
  scores::Rollup r;
  auto prov = [](const char* id, double lineup, double replay, double fails) {
    scores::ProviderFigures f;
    f.provider = id;
    f.lineup = lineup;
    f.replay = replay;
    f.tested = 100;
    f.startFailures = fails;
    return f;
  };
  r.providers = {prov("dream", 0.77, 0.36, 0.18), prov("xtream", 0.90, 0, 0.03), prov("trex", 0.88, 0.54, 0.03)};
  r.pairs = {{"dream", "xtream", 0.95, 0.36}, {"dream", "trex", 0.94, 0.66}, {"xtream", "trex", 0.96, 0.54}};
  std::vector<st::SameService> same = {{"xtream", "trex", 0.9, std::nullopt}};
  std::vector<st::Choice> c = st::choices(r, same);
  REQUIRE(c.size() == 6);
  CHECK(c[0].providers == std::vector<std::string>{"dream", "trex"});
  CHECK(c[0].best);
  CHECK(c[0].kind == st::Choice::Kind::Separate);
  CHECK(c[1].providers == std::vector<std::string>{"xtream", "trex"});
  CHECK(c[1].kind == st::Choice::Kind::Same);
  CHECK_FALSE(c[1].best);
  CHECK(c[2].providers == std::vector<std::string>{"dream", "xtream"});
  CHECK(c[3].kind == st::Choice::Kind::Alone);
  CHECK(c[3].providers.size() == 1);
  // Two separate services with the most lineup (to five points), then replay.
  r.pairs = {{"dream", "xtream", 0.99, 0.30}, {"dream", "trex", 0.90, 0.66}};
  c = st::choices(r, {});
  CHECK(c[0].providers == std::vector<std::string>{"dream", "xtream"});

  std::vector<scores::LineupRow> rows = {
      {"e|1", "ESPN", {{"dream|1", "dream", "ESPN", 0}, {"trex|1", "trex", "ESPN", 0}}},
      {"e|2", "WE tv All Reality", {{"xt|2", "xtream", "WE", 0}}},
      {"e|3", "AMC Thrillers", {{"xt|3", "xtream", "AMC T", 0}}},
      {"e|4", "OAN Plus", {{"xt|4", "xtream", "OAN", 0}}},
      {"e|5", "365BLK", {{"trex|5", "trex", "365", 0}}},
  };
  st::Lost l = st::lost(rows, {"dream", "trex"});
  CHECK(l.names == std::vector<std::string>{"WE tv All Reality", "AMC Thrillers", "OAN Plus"});
  CHECK_FALSE(l.more);
  l = st::lost(rows, {"dream"}, 2);
  CHECK(l.names == std::vector<std::string>{"WE tv All Reality", "AMC Thrillers"});
  CHECK(l.more);
  CHECK(st::lost(rows, {"dream", "xtream", "trex"}).names.empty());
}

TEST_CASE("test all sources: the words on screen give percentages, never a count") {
  mtime::Seconds now = mtime::now();
  st::Runner::Provider p;
  p.name = "Dream";
  p.done = 52;
  p.total = 100;
  p.now = "CNN \xc2\xb7 replay 2 h back";
  p.state = State::Testing;
  std::vector<std::string> words = {st::providerLine(p, now), st::providerCorner(p)};
  CHECK(words[0] == "Now: CNN \xc2\xb7 replay 2 h back");
  CHECK(words[1] == "52%");
  p.state = State::Stopped;
  words.push_back(st::providerLine(p, now));
  p.state = State::Paused;
  p.until = now + 600;
  words.push_back(st::providerLine(p, now));
  CHECK(words.back() == "It refused a connection. Trying again in 10 min; the others carry on.");
  st::Runner::Status s;
  s.fraction = 0.38;
  s.leftMs = 7800000;
  words.push_back(st::headerLine(s));
  CHECK(words.back() == "38% \xc2\xb7 about 2 h 10 min left");
  words.push_back(st::estimateText(11000));
  words.push_back(st::timeLeftText(3600000));
  scores::ProviderFigures f;
  f.replayWorks = 0.54;
  f.replayDays = 1;
  words.push_back(st::replayCell(f));
  CHECK(words.back() == "54% \xc2\xb7 1 day");
  f.replayWorks = 0;
  CHECK(st::replayCell(f) == "None");
  mtime::Zone utc = mtime::Zone::utc();
  mtime::Seconds day = now - now % 86400;
  CHECK(st::testedText(day + 3600 + 12 * 60, day + 7200, utc, false) == "today 1:12 AM");
  CHECK(st::testedText(day + 3600 + 12 * 60, day + 7200, utc, true) == "today 01:12");
  CHECK(st::testedText(day - 3600, day + 7200, utc, false) == "yesterday 11:00 PM");
  words.push_back(st::testedText(day - 5 * 86400, day, utc, false));
  CHECK(st::testedDay(day + 60, day + 7200, utc) == "today");
  CHECK(st::testedDay(day - 60, day + 7200, utc) == "yesterday");
  CHECK(st::testedDay(day - 5 * 86400, day, utc) == st::testedText(day - 5 * 86400, day, utc, false));
  scores::Record rec;
  rec.probes = 1;
  rec.startMs = {1400};
  rec.height = 1080;
  rec.fps = 60;
  rec.archiveOk = 1;
  rec.archiveDays = 1;
  words.push_back(st::copyLine(&rec, "FHD", 1, now));
  CHECK(words.back() == "1080p 60 \xc2\xb7 starts in 1.4 s \xc2\xb7 replay 1 day");
  for (const std::string& w : words) noCounts(w);
  CHECK(st::pictureText(2160, 50) == "4K 50");
  CHECK(st::pictureText(1088, 0) == "1080p");
}

// ------------------------------------------- Build 170: archive clocks

namespace {

// A copy's own guide for the last six hours, its local start texts `offset`
// seconds east of UTC.
std::string clockTable(int offset) {
  mtime::Seconds hour = mtime::now() - mtime::now() % 3600;
  std::string body = R"({"epg_listings":[)";
  for (int i = 6; i >= 0; --i) {
    mtime::Seconds start = hour - i * 3600;
    mtime::Civil c = mtime::civilFromSeconds(start + offset);
    char local[32];
    std::snprintf(local, sizeof local, "%04d-%02d-%02d %02d:%02d:%02d", c.year, c.month, c.day, c.hour, c.minute,
                  c.second);
    body += std::string(i == 6 ? "" : ",") + R"({"title":"U2hvdw==","start":")" + local +
            R"(","start_timestamp":")" + std::to_string(start) + R"(","stop_timestamp":")" +
            std::to_string(start + 3600) + R"(","has_archive":1})";
  }
  return body + "]}";
}

}  // namespace

TEST_CASE("test all sources 1.0.13: each replay copy's archive clock read first; the archive probes ask in it") {
  Rig r;  // both servers say UTC
  auto dream = client(std::string(mock::kHost)), trex = client(kTrexHost);
  std::string t601 = dream.simpleDataTableRequest("601").url, t701 = trex.simpleDataTableRequest("701").url,
              t702 = trex.simpleDataTableRequest("702").url;
  r.tv.pool.answer(t601, clockTable(3600));
  r.tv.pool.answer(t701, clockTable(3600));
  r.tv.pool.answer(t702, clockTable(0));
  for (int id : {601, 604, 701, 702}) r.tv.pool.answer(r.liveUrl(id), ts(1920, 1080, 60));
  // The archive two hours back in each copy's own clock (the wall clock
  // stands still in this test).
  mtime::Seconds now = mtime::now(), back = now - st::kArchiveBack;
  int minutes = marigold::sources::xtream::timeshiftMinutes(back, now);
  std::string a601 = *dream.timeshiftURL("601", back, minutes, 3600);
  std::string a701 = *trex.timeshiftURL("701", back, minutes, 3600);
  std::string a702 = *trex.timeshiftURL("702", back, minutes, 0);
  std::string a601Server = r.archiveUrl(601, st::kArchiveBack);  // in the server's clock, as before
  CHECK(a601Server != a601);
  for (const std::string* u : {&a601, &a701, &a702}) r.tv.pool.answer(*u, ts(1920, 1080, 30));
  CHECK(r.runner->estimateNow() == 6 * st::kItemMs / 1000);  // the same plan
  r.runner->start();
  r.run(200000);
  CHECK_FALSE(r.runner->running());
  // One table per replay copy, none for a copy without replay.
  CHECK(r.tv.pool.asked[t601] == 1);
  CHECK(r.tv.pool.asked[t701] == 1);
  CHECK(r.tv.pool.asked[t702] == 1);
  CHECK(r.tv.pool.asked[dream.simpleDataTableRequest("604").url] == 0);
  // The archive probes in those clocks.
  CHECK(r.tv.pool.asked[a601] == 1);
  CHECK(r.tv.pool.asked[a701] == 1);
  CHECK(r.tv.pool.asked[a702] == 1);
  CHECK(r.tv.pool.asked[a601Server] == 0);
  const scores::Record* d = r.store->feedScores().find(r.copy(601));
  REQUIRE(d);
  CHECK(d->archiveOk == 1);
  // Kept for the copies (and replays) from now on.
  auto c601 = r.store->archiveClock(r.copy(601));
  CHECK(c601.from == Store::ClockFrom::Table);
  CHECK(c601.offset == 3600);
  CHECK(r.store->archiveClock(r.copy(702)).offset == 0);
  CHECK(r.archiveUrl(601, st::kArchiveBack) == a601);
  // One line per provider, in words.
  CHECK(r.logged("Dream: archive clocks: UTC+1 on all replay channels; server says UTC"));
  CHECK(r.logged("Trex: archive clocks: UTC (half), UTC+1 (half)"));
  for (const std::string& l : r.lines)
    if (l.find("archive clocks") != std::string::npos) noCounts(l);
  r.noLinks();
}

TEST_CASE("test all sources 1.0.13: a refused table pauses the provider like a refused probe") {
  Rig r(1, 0);  // Trex keeps a day, Dream nothing
  auto trex = client(kTrexHost);
  r.tv.pool.answer(trex.simpleDataTableRequest("701").url, "", 429);
  r.tv.pool.answer(trex.simpleDataTableRequest("702").url, "", 429);
  for (int id : {601, 604}) r.tv.pool.answer(r.liveUrl(id), ts(1920, 1080, 60));
  r.runner->start();
  r.run(1000);
  const st::Runner::Provider* p = r.provider("Trex");
  REQUIRE(p);
  CHECK(p->state == State::Paused);
  CHECK(r.logged("Trex: paused, it refused a connection (http 429); trying again in 15 min"));
  // No archive probe went out for it.
  for (const auto& [url, n] : r.tv.pool.asked) CHECK(url.find("/timeshift/") == std::string::npos);
  r.noLinks();
}

TEST_CASE("test all sources 1.0.13: archive clocks in words, never a count") {
  CHECK(st::clocksText({}, 0).empty());
  CHECK(st::clocksText({3600, 3600}, 3600) == "UTC+1 on all replay channels");
  CHECK(st::clocksText({3600}, 0) == "UTC+1 on all replay channels; server says UTC");
  CHECK(st::clocksText({3600, 3600, 0}, 0) == "UTC+1 (most), UTC (some); server says UTC");
  CHECK(st::clocksText({0, 0, 3600, -5 * 3600}, 0) == "UTC (half), UTC\xe2\x88\x92" "5 (some), UTC+1 (some)");
  CHECK(st::clocksText({3600}, std::nullopt) == "UTC+1 on all replay channels; server says nothing");
}

// ------------------------------------------------- Build 172: the timing

namespace {

// `seconds` of 25 fps video whose clock starts at the encoder's time `t`.
std::string timedClip(mtime::Seconds t, int seconds) {
  int64_t wrap = int64_t{1} << 33;
  int64_t pts0 = ((t - 1700000000) * 90000 % wrap + wrap) % wrap;
  tsgen::Muxer m;
  tsgen::VideoFormat f;
  for (int i = 0; i < seconds * 25; ++i) {
    if (i % 10 == 0) {
      m.section(0, tsgen::pat());
      m.section(tsgen::kPmtPid, tsgen::pmt(0x1B, 0));
    }
    int64_t pts = (pts0 + static_cast<int64_t>(i) * 3600) % wrap;
    m.pes(tsgen::kVideoPid, tsgen::videoPes(pts, pts, tsgen::picture(f, i % 50 == 0, 3000, 0, static_cast<uint8_t>(i))));
  }
  return std::string(m.out.begin(), m.out.end());
}

// Answers a copy's timing reads: its live picture now, and the archive
// starting at `startOf(moment)`.
void answerTiming(Rig& r, int id, const std::function<mtime::Seconds(mtime::Seconds)>& startOf, int liveStatus = 200) {
  auto req = r.store->timingRequest(r.copy(id));
  REQUIRE(req);
  mtime::Seconds now = mtime::now();
  r.tv.pool.answer(req->live, liveStatus == 200 ? timedClip(now - 3, 4) : std::string(), liveStatus);
  mtime::Seconds first = marigold::engine::archive::firstMoment(now);
  for (mtime::Seconds at : {first, first + marigold::engine::archive::kApart})
    r.tv.pool.answer(*req->linkFor(at), timedClip(startOf(at), 5));
}

}  // namespace

TEST_CASE("test all sources 1.0.15: each replay copy's archive timing, in a turn of its own; a line per provider") {
  Rig r;
  for (int id : {601, 604, 701, 702}) r.tv.pool.answer(r.liveUrl(id), ts(1920, 1080, 60));
  for (int id : {601, 701, 702}) r.tv.pool.answer(r.archiveUrl(id, st::kArchiveBack), ts(1920, 1080, 30));
  // Dream's archive starts at the minute asked; Trex's at the half hour.
  answerTiming(r, 601, [](mtime::Seconds t) { return t; });
  for (int id : {701, 702}) answerTiming(r, id, [](mtime::Seconds t) { return t / 1800 * 1800; });
  r.runner->start();
  r.run(300000);
  CHECK_FALSE(r.runner->running());
  auto dream = r.store->archiveTiming(r.copy(601));
  REQUIRE(dream);
  CHECK(dream->kind == marigold::engine::archive::Kind::Exact);
  auto trex = r.store->archiveTiming(r.copy(702));
  REQUIRE(trex);
  CHECK(trex->kind == marigold::engine::archive::Kind::Chunked);
  CHECK(trex->chunkMinutes == 30);
  CHECK_FALSE(r.store->archiveTiming(r.copy(604)));  // no replay: not measured
  CHECK(r.logged("Dream: archive timing: exact on all replay channels"));
  CHECK(r.logged("Trex: archive timing: 30-min chunks (reads through up to 4 min) on all replay channels"));
  for (const std::string& l : r.lines)
    if (l.find("archive timing") != std::string::npos) noCounts(l);
  // The summary's other words are as before.
  CHECK(r.logged("summary: "));
  r.noLinks();
}

TEST_CASE("test all sources 1.0.15: a timing read refused pauses the provider; the others carry on") {
  Rig r;
  for (int id : {601, 604, 701, 702}) r.tv.pool.answer(r.liveUrl(id), ts(1920, 1080, 60));
  for (int id : {601, 701, 702}) r.tv.pool.answer(r.archiveUrl(id, st::kArchiveBack), ts(1920, 1080, 30));
  answerTiming(r, 601, [](mtime::Seconds t) { return t; });
  answerTiming(r, 701, [](mtime::Seconds t) { return t; }, 429);
  r.runner->start();
  r.run(120000);
  CHECK(r.logged("Trex: paused, it refused a connection (http 429); trying again in 15 min"));
  const st::Runner::Provider* trex = r.provider("Trex");
  REQUIRE(trex);
  CHECK(trex->state == State::Paused);
  // Dream finished meanwhile.
  CHECK(r.logged("Dream: done"));
  CHECK(r.store->archiveTiming(r.copy(601)));
  CHECK_FALSE(r.store->archiveTiming(r.copy(701)));
  r.noLinks();
}

TEST_CASE("test all sources 1.0.16: a timing turn that finds the archive an hour off sets the provider's clock") {
  Rig r;  // both servers say UTC
  auto dream = client(std::string(mock::kHost));
  // Dream's table says UTC too; its archive runs on UTC+1.
  r.tv.pool.answer(dream.simpleDataTableRequest("601").url, clockTable(0));
  for (int id : {601, 604, 701, 702}) r.tv.pool.answer(r.liveUrl(id), ts(1920, 1080, 60));
  for (int id : {601, 701, 702}) r.tv.pool.answer(r.archiveUrl(id, st::kArchiveBack), ts(1920, 1080, 30));
  answerTiming(r, 601, [](mtime::Seconds t) { return t - 3600; });
  for (int id : {701, 702}) answerTiming(r, id, [](mtime::Seconds t) { return t; });
  r.runner->start();
  r.run(300000);
  CHECK_FALSE(r.runner->running());
  // Measured: UTC+1, over its table's UTC; the recording itself exact.
  Store::ArchiveClock c601 = r.store->archiveClock(r.copy(601));
  CHECK(c601.from == Store::ClockFrom::Measured);
  CHECK(c601.offset == 3600);
  auto t601 = r.store->archiveTiming(r.copy(601));
  REQUIRE(t601);
  CHECK(t601->kind == marigold::engine::archive::Kind::Exact);
  CHECK(t601->offset == 0);
  // The account's other copies take it at once.
  Store::ArchiveClock c604 = r.store->archiveClock(r.copy(604));
  CHECK(c604.measured);
  CHECK(c604.offset == 3600);
  // Trex measured on its own clock: unchanged.
  CHECK(r.store->archiveClock(r.copy(701)).offset == 0);
  CHECK(r.store->archiveClock(r.copy(701)).measured);
  // Its line says the clock the video showed.
  CHECK(r.logged("Dream: archive clocks: UTC+1 on all replay channels; server says UTC"));
  CHECK(r.logged("Dream: archive timing: exact on all replay channels"));
  r.noLinks();
}

TEST_CASE("test all sources 1.0.17: the replay probe asks as the player; a timing read that can't tell says why") {
  Rig r;
  for (int id : {601, 604, 701, 702}) r.tv.pool.answer(r.liveUrl(id), ts(1920, 1080, 60));
  // Dream's replay starts with a few bytes before its packets, in a tiny
  // first piece (1.0.16's reads gave up on such a start).
  r.tv.pool.firstPiece = 40;
  for (int id : {601, 701, 702})
    r.tv.pool.answer(r.archiveUrl(id, st::kArchiveBack), std::string(20, '\0') + ts(1920, 1080, 30));
  // Its timing reads: the live picture, an archive answering a web page.
  auto req = r.store->timingRequest(r.copy(601));
  REQUIRE(req);
  r.tv.pool.answer(req->live, ts(1920, 1080, 60));
  mtime::Seconds first = marigold::engine::archive::firstMoment(mtime::now());
  for (mtime::Seconds at : {first, first + marigold::engine::archive::kApart}) {
    r.tv.pool.answer(*req->linkFor(at), "<html><body>busy</body></html>");
    r.tv.pool.mime(*req->linkFor(at), "text/html");
  }
  for (int id : {701, 702}) answerTiming(r, id, [](mtime::Seconds t) { return t; });
  r.runner->start();
  r.run(300000);
  CHECK_FALSE(r.runner->running());
  // The replay probe: the player's agent, no compressed body.
  const std::vector<std::string>& sent = r.tv.pool.sent[r.archiveUrl(601, st::kArchiveBack)];
  CHECK(std::find(sent.begin(), sent.end(), std::string("User-Agent: ") + marigold::http::kPlayerAgent) != sent.end());
  CHECK(std::find(sent.begin(), sent.end(), "Accept-Encoding: identity") != sent.end());
  // Dream's replay played; its clock couldn't be read, and the log says why.
  const marigold::engine::scores::Record* rec = r.store->feedScores().find(r.copy(601));
  REQUIRE(rec);
  CHECK(rec->archiveOk == 1);
  bool said = false;
  for (const std::string& l : r.lines)
    said = said || l.find("Dream: a replay's clock couldn't be read (") != std::string::npos;
  CHECK(said);
  CHECK(r.logged("not a video stream (text/html) (archive)"));
  r.noLinks();
}

// ---------------------------------------- Build 177: ad markers (SCTE-35)

namespace {

// ts() with an SCTE-35 stream in its PMT (`declared`) and a splice_insert
// after its first tables (`cue`).
std::string markedTs(bool declared, bool cue) {
  tsgen::Scene s;
  s.fps = 30;
  s.seconds = 3;
  s.gopSeconds = 1;
  s.size = [](int, bool intra) { return intra ? 6000u : 900u; };
  s.cueType = declared ? 0x86 : 0;
  if (cue) s.cueFrames = {0};
  tsgen::Bytes b = tsgen::build(s);
  return std::string(b.begin(), b.end());
}

}  // namespace

TEST_CASE("test all sources Build 177: ad markers per copy, a line per provider at the run's end, shares only") {
  // The probe reads them on the bytes it reads anyway.
  {
    fake::Tv tv;
    tv.pool.answer("http://p.example.invalid/live/1.ts", markedTs(true, true));
    uint32_t now = 0;
    probe::Outcome o = probe::run(tv.pool, {"http://p.example.invalid/live/1.ts", {}}, ticking(now, 10), nullptr);
    CHECK(o.ok);
    CHECK(o.ads.declared);
    CHECK(o.ads.cues == 1);
    CHECK(o.ads.spliceInsert == 1);
    tv.pool.answer("http://p.example.invalid/live/2.ts", ts(1920, 1080, 30));
    probe::Outcome plain = probe::run(tv.pool, {"http://p.example.invalid/live/2.ts", {}}, ticking(now, 10), nullptr);
    CHECK(plain.ads.tables);
    CHECK_FALSE(plain.ads.declared);
    CHECK(plain.ads.cues == 0);
  }

  Rig r;
  for (int id : {601, 604, 701, 702}) r.tv.pool.answer(r.liveUrl(id), ts(1920, 1080, 60));
  // Dream's replay declares them and carries a cue; Trex's: one declares
  // them (no cue in what's read), one has none.
  r.tv.pool.answer(r.archiveUrl(601, st::kArchiveBack), markedTs(true, true));
  r.tv.pool.answer(r.archiveUrl(701, st::kArchiveBack), markedTs(true, false));
  r.tv.pool.answer(r.archiveUrl(702, st::kArchiveBack), markedTs(false, false));
  r.runner->start();
  r.run(300000);
  CHECK_FALSE(r.runner->running());
  CHECK(r.logged("Dream: ad markers in replays: declared on 100% of copies tested, cues seen on 100%"));
  CHECK(r.logged("Dream: ad markers live: declared on 0% of copies tested, cues seen on 0%"));
  CHECK(r.logged("Trex: ad markers in replays: declared on 50% of copies tested, cues seen on 0%"));
  CHECK(r.logged("Trex: ad markers live: declared on 0% of copies tested, cues seen on 0%"));
  // At the run's end, before its summary; never a count, never a link.
  size_t ads = r.lines.size(), summary = r.lines.size();
  for (size_t i = 0; i < r.lines.size(); ++i) {
    if (r.lines[i].find("ad markers") != std::string::npos) {
      ads = std::min(ads, i);
      noCounts(r.lines[i]);
      CHECK(r.lines[i].find(" 1 ") == std::string::npos);
      CHECK(r.lines[i].find(" 2 ") == std::string::npos);
    }
    if (r.lines[i].rfind("summary: ", 0) == 0) summary = i;
  }
  CHECK(ads < summary);
  CHECK(summary < r.lines.size());
  r.noLinks();

  // The words from the copies' values: replay then live, '-' not read.
  CHECK(st::adMarkersLine({"3-", "1-", "0-", "--"}, false) ==
        "ad markers in replays: declared on 67% of copies tested, cues seen on 33%");
  CHECK(st::adMarkersLine({"3-", "1-"}, true).empty());
  CHECK(st::adMarkersLine({"-2", "-0"}, true) == "ad markers live: declared on 0% of copies tested, cues seen on 50%");
  CHECK(st::adMarkersLine({}, false).empty());
}
