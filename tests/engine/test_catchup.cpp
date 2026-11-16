// Build 163, the catch-up test on the pretend TV: an Xtream provider whose
// copies of a channel say they keep (or don't keep) an archive, a fake video
// player whose flags the test sets, and the session's separate, throwaway
// playback of a moment back.
#include <doctest.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/engine/session.hpp"
#include "core/engine/store.hpp"
#include "core/sources/xtream.hpp"
#include "core/util/json.hpp"
#include "engine/loaded_store.hpp"
#include "sources/mock_provider.hpp"

using marigold::engine::AccountForm;
using marigold::engine::Feed;
using marigold::engine::PlayerState;
using marigold::engine::Session;
using marigold::engine::Store;
namespace mtime = marigold::time;
using Moment = Session::CatchupMoment;
using Phase = Session::CatchupTest::Phase;

namespace {

struct Player final : marigold::engine::Player {
  struct Shared {
    std::vector<Feed> plays;  // every feed handed over, in order
    PlayerState state;
    int stops = 0;
  };
  explicit Player(std::shared_ptr<Shared> s) : s_(std::move(s)) {}
  void attempt() override { s_->state = {}; }
  void play(const Feed& f, bool) override { s_->plays.push_back(f); }
  void stop() override { ++s_->stops; }
  PlayerState state() const override { return s_->state; }
  std::shared_ptr<Shared> s_;
};

// One copy in get_live_streams: its archive days (0: none).
std::string stream(int id, const std::string& name, int category, int archiveDays) {
  return "{\"num\":" + std::to_string(id) + ",\"name\":\"" + name + "\",\"stream_type\":\"live\",\"stream_id\":" +
         std::to_string(id) + ",\"stream_icon\":\"\",\"epg_channel_id\":null,\"category_id\":\"" +
         std::to_string(category) + "\",\"tv_archive\":" + (archiveDays ? "1" : "0") +
         ",\"tv_archive_duration\":\"" + std::to_string(archiveDays) + "\"}";
}

// The panel's clock five hours behind UTC, as its server_info says.
std::string accountInfo() {
  mtime::Seconds now = mtime::now();
  mtime::Civil local = mtime::civilFromSeconds(now - 5 * 3600);
  char when[32];
  std::snprintf(when, sizeof when, "%04d-%02d-%02d %02d:%02d:%02d", local.year, local.month, local.day, local.hour,
                local.minute, local.second);
  return R"({"user_info":{"auth":1,"status":"Active","exp_date":null,"max_connections":"1",)"
         R"("allowed_output_formats":["m3u8","ts"]},"server_info":{"timezone":"America/New_York",)"
         R"("timestamp_now":)" + std::to_string(now) + R"(,"time_now":")" + when + R"("}})";
}

struct Rig {
  fake::Tv tv;
  std::unique_ptr<Store> store;
  std::shared_ptr<Player::Shared> player = std::make_shared<Player::Shared>();
  std::unique_ptr<Session> session;

  Rig() {
    marigold::sources::Account a;
    a.kind = marigold::sources::AccountKind::Xtream;
    a.serverUrl = std::string(mock::kHost);
    a.userName = std::string(mock::kUser);
    a.password = std::string(mock::kPass);
    auto client = marigold::sources::xtream::Client::make(a);
    REQUIRE(client);
    const auto& c = client.value();
    tv.pool.answer(c.accountInfoRequest().url, accountInfo());
    tv.pool.answer(c.categoriesRequest().url,
                   R"([{"category_id":"1","category_name":"US| SPORTS"},{"category_id":"2","category_name":"US| NEWS"}])");
    // ESPN: three copies, the first and the third with an archive; CNN:
    // two copies, neither has one.
    tv.pool.answer(c.streamsRequest().url, "[" + stream(501, "US: ESPN FHD", 1, 2) + "," +
                                               stream(502, "US: ESPN HD", 1, 0) + "," +
                                               stream(503, "US: ESPN SD", 1, 3) + "," +
                                               stream(504, "US: CNN HD", 2, 0) + "," +
                                               stream(505, "US: CNN FHD", 2, 0) + "]");
    store = std::make_unique<Store>(tv.host());
    AccountForm form;
    form.kind = AccountForm::Kind::Xtream;
    form.name = "Mock IPTV";
    form.server = std::string(mock::kHost);
    form.user = std::string(mock::kUser);
    form.password = std::string(mock::kPass);
    std::string problem = "(not answered)";
    store->addAccount(form, [&](std::string p) { problem = std::move(p); });
    fake::settle(*store, tv);
    REQUIRE(problem.empty());
    session = std::make_unique<Session>(*store, tv.host(), std::make_unique<Player>(player), Session::Hooks{});
  }

  void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 250) {
      tv.ms += 250;
      session->tick(tv.ms);
      tv.pool.drain();
    }
  }
  void picture(const std::string& facts = {}) {
    run(250);
    player->state.picture = true;
    player->state.playing = true;
    player->state.facts = facts;
    run(500);
  }

  // The guide entry named after `name`.
  std::string entry(const std::string& name) const {
    for (const auto& g : store->channels()->all)
      if (g.name.find(name) != std::string::npos) return g.id;
    return {};
  }
  // The index in the session's feeds of stream `id`.
  size_t feedOf(int id) const {
    const auto& feeds = session->feeds();
    for (size_t i = 0; i < feeds.size(); ++i)
      if (feeds[i].channelId.size() > 4 && feeds[i].channelId.substr(feeds[i].channelId.size() - 4) == "|" + std::to_string(id))
        return i;
    return feeds.size();
  }
  // Tunes `entryId` full screen and plays stream `id` live with a picture.
  void watchLive(const std::string& entryId, int id) {
    session->setShowingFull(true);
    REQUIRE(session->tune(entryId));
    run(500);
    size_t i = feedOf(id);
    REQUIRE(i < session->feeds().size());
    if (session->feedIndex() != i) {
      REQUIRE(session->useFeed(i));
      run(500);
    }
    picture();
    REQUIRE(session->ready());
    REQUIRE(session->feedIndex() == i);
    run(25000);  // long enough to count as played
  }

  std::vector<marigold::report::PlaybackLog::Entry> logged(const std::string& event) const {
    std::vector<marigold::report::PlaybackLog::Entry> out;
    for (const auto& e : session->log().entries())
      if (e.event == event) out.push_back(e);
    return out;
  }

  // What the lists file says of feeds' reliability and the source each
  // channel last played on.
  std::string memory() {
    tv.pool.drain();
    auto text = tv.files.read("lists.json");
    if (!text) return {};
    auto v = marigold::json::parse(*text);
    if (!v) return {};
    return marigold::json::write((*v)["reliability"]) + "|" + marigold::json::write((*v)["lastFeed"]);
  }
};

// "2026-11-15:19-52": `t` in the server's clock (five hours behind UTC).
std::string serverMinute(mtime::Seconds t) {
  mtime::Civil c = mtime::civilFromSeconds(t - 5 * 3600);
  char buf[32];
  std::snprintf(buf, sizeof buf, "%04d-%02d-%02d:%02d-%02d", c.year, c.month, c.day, c.hour, c.minute);
  return buf;
}

mtime::Seconds minuteOf(mtime::Seconds t) { return t - t % 60; }

}  // namespace

TEST_CASE("catch-up test: the copies, the server clock and what is offered") {
  Rig r;
  std::string espn = r.entry("ESPN"), cnn = r.entry("CNN");
  REQUIRE(!espn.empty());
  REQUIRE(!cnn.empty());
  REQUIRE(r.store->feeds(espn).size() == 3);
  // The archive each copy claims, Xtream only.
  for (const Feed& f : r.store->feeds(espn)) {
    uint16_t days = r.store->catchupDays(f.channelId);
    if (f.channelId.find("|501") != std::string::npos) CHECK(days == 2);
    else if (f.channelId.find("|503") != std::string::npos) CHECK(days == 3);
    else CHECK(days == 0);
  }
  // Nothing offered before a picture, nor on a copy without an archive.
  r.session->setShowingFull(true);
  REQUIRE(r.session->tune(cnn));
  CHECK(r.session->catchupDays() == 0);
  r.run(500);
  r.picture();
  REQUIRE(r.session->ready());
  CHECK(r.session->catchupDays() == 0);
  CHECK_FALSE(r.session->startCatchupTest(Moment::TenMinutesAgo, mtime::now() - 600));
  CHECK_FALSE(r.session->catchupTest());

  // The copy with an archive: offered, with its days.
  r.watchLive(espn, 501);
  CHECK(r.session->catchupDays() == 2);
  r.watchLive(espn, 502);
  CHECK(r.session->catchupDays() == 0);
  CHECK_FALSE(r.session->startCatchupTest(Moment::TenMinutesAgo, mtime::now() - 600));
}

TEST_CASE("catch-up test: plays the moment asked on the same copy, and a picture is \"worked\" with its time") {
  Rig r;
  std::string espn = r.entry("ESPN");
  r.watchLive(espn, 501);
  size_t live = r.session->feedIndex();
  Feed liveFeed = r.session->feeds()[live];
  std::string before = r.memory();
  // (What is compared: the copy's one success, and it as the channel's source.)
  REQUIRE(before.find("501") != std::string::npos);
  REQUIRE(before.find("[1,0]") != std::string::npos);
  std::string right = r.session->previousEntry();
  size_t plays = r.player->plays.size();

  mtime::Seconds from = mtime::now() - 600;
  REQUIRE(r.session->startCatchupTest(Moment::TenMinutesAgo, from));
  REQUIRE(r.session->catchupTest());
  CHECK(r.session->catchupTest()->phase == Phase::Opening);
  CHECK(r.session->catchupTest()->from == minuteOf(from));
  CHECK(r.session->catchupTest()->days == 2);
  CHECK(r.session->catchupTest()->feed == live);
  CHECK(r.session->opening());
  // The archive of the same copy, from that minute in the server's clock,
  // until now and five minutes more.
  REQUIRE(r.player->plays.size() == plays + 1);
  const Feed& asked = r.player->plays.back();
  CHECK(asked.channelId == liveFeed.channelId);
  CHECK(asked.label == liveFeed.label);
  CHECK(asked.url == std::string(mock::kHost) + "/timeshift/u/p/15/" + serverMinute(minuteOf(from)) + "/501.ts");

  // The picture after 2.25 s, then the player says its size.
  r.run(2000);
  r.player->state.picture = true;
  r.player->state.playing = true;
  r.run(250);
  REQUIRE(r.session->catchupTest()->phase == Phase::Worked);
  CHECK(r.session->catchupTest()->pictureMs == 2250);
  CHECK(r.session->ready());
  CHECK_FALSE(r.session->opening());
  CHECK(r.logged("catch-up test").empty());  // its size is waited for a moment
  r.player->state.facts = "1920x1080 59.94p \xc2\xb7 h264 High 8-bit \xc2\xb7 aac";
  r.run(250);
  auto lines = r.logged("catch-up test");
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].detail ==
        "10 min ago (archive says 2 days): worked, picture in 2.3 s, 1920x1080; server clock UTC\xe2\x88\x92" "5");
  CHECK(lines[0].feed == liveFeed.label);
  CHECK(lines[0].provider == "Mock IPTV");
  // Never a link or the login.
  for (const auto& e : r.session->log().entries()) {
    CHECK(e.detail.find("timeshift") == std::string::npos);
    CHECK(e.detail.find("://") == std::string::npos);
    CHECK(e.detail.find("example.invalid") == std::string::npos);
  }
  // Nothing more is tried; the recovery stands aside however long it runs.
  r.player->state.picture = false;
  r.player->state.failed = true;
  r.run(30000);
  CHECK(r.player->plays.size() == plays + 1);
  CHECK(r.logged("catch-up test").size() == 1);

  // Back to live: the copy before the test, its live link.
  r.player->state = {};
  r.session->endCatchupTest();
  CHECK_FALSE(r.session->catchupTest());
  r.run(250);  // handed to the player on the next frame
  REQUIRE(r.player->plays.size() == plays + 2);
  CHECK(r.player->plays.back().channelId == liveFeed.channelId);
  CHECK(r.player->plays.back().url == liveFeed.url);
  CHECK(r.session->feedIndex() == live);
  r.picture();
  CHECK(r.session->ready());
  r.run(25000);
  // Nothing charged or remembered, and Right goes where it went.
  CHECK(r.memory() == before);
  CHECK(r.session->previousEntry() == right);
  CHECK(r.logged("catch-up test").size() == 1);
}

TEST_CASE("catch-up test: no picture in 10 s is \"failed\", with no reconnect and no other copy") {
  Rig r;
  std::string cnn = r.entry("CNN"), espn = r.entry("ESPN");
  r.watchLive(cnn, 504);
  r.watchLive(espn, 501);
  std::string right = r.session->previousEntry();
  REQUIRE(right == cnn);
  size_t live = r.session->feedIndex();
  std::string before = r.memory();
  size_t plays = r.player->plays.size();
  int stops = r.player->stops;

  // The start of the programme, an hour ago.
  mtime::Seconds start = minuteOf(mtime::now() - 3600);
  REQUIRE(r.session->startCatchupTest(Moment::ProgrammeStart, start));
  CHECK(r.player->plays.back().url ==
        std::string(mock::kHost) + "/timeshift/u/p/65/" + serverMinute(start) + "/501.ts");
  r.run(9750);
  CHECK(r.session->catchupTest()->phase == Phase::Opening);
  r.run(250);
  REQUIRE(r.session->catchupTest()->phase == Phase::Failed);
  CHECK(r.session->catchupTest()->timedOut);
  CHECK_FALSE(r.session->opening());
  CHECK(r.player->stops == stops + 1);
  // Only said: nothing else plays, however long it waits.
  r.run(60000);
  CHECK(r.player->plays.size() == plays + 1);
  CHECK(r.session->feedIndex() == live);
  auto lines = r.logged("catch-up test");
  REQUIRE(lines.size() == 1);
  mtime::Civil at = mtime::civilFromSeconds(start);  // the pretend TV's clock is UTC
  char hm[8];
  std::snprintf(hm, sizeof hm, "%02d:%02d", at.hour, at.minute);
  CHECK(lines[0].detail == std::string("start of programme ") + hm +
                               " (archive says 2 days): failed, no picture after 10 s; server clock UTC\xe2\x88\x92" "5");
  CHECK(lines[0].detail.find("http") == std::string::npos);
  CHECK(r.memory() == before);
  CHECK(r.session->previousEntry() == right);

  // Back to live on the same copy.
  r.session->endCatchupTest();
  r.run(250);
  CHECK(r.player->plays.back().channelId == r.session->feeds()[live].channelId);
  CHECK(r.player->plays.back().url.find("/timeshift/") == std::string::npos);
  r.picture();
  r.run(25000);
  CHECK(r.memory() == before);
  CHECK(r.session->previousEntry() == right);
}

TEST_CASE("catch-up test: a player error fails it at once; another copy is only one with an archive") {
  Rig r;
  std::string espn = r.entry("ESPN");
  r.watchLive(espn, 501);
  size_t first = r.feedOf(501), plain = r.feedOf(502), other = r.feedOf(503);
  REQUIRE(first != plain);
  REQUIRE(other < r.session->feeds().size());
  std::string before = r.memory();

  mtime::Seconds from = mtime::now() - 86400;
  REQUIRE(r.session->startCatchupTest(Moment::Yesterday, from));
  CHECK(r.player->plays.back().url.find("/timeshift/u/p/180/") != std::string::npos);
  r.player->state.failed = true;
  r.player->state.error = "HTTP 404 from http://mock.example.invalid:8080/timeshift/u/p/180/x/501.ts";
  r.run(250);
  REQUIRE(r.session->catchupTest()->phase == Phase::Failed);
  CHECK_FALSE(r.session->catchupTest()->timedOut);
  CHECK(r.session->catchupTest()->error.find("example.invalid") == std::string::npos);
  CHECK(r.session->catchupTest()->error.find("/u/p/") == std::string::npos);
  // The other copy with an archive, never the one without.
  REQUIRE(r.session->catchupOtherCopy());
  CHECK(*r.session->catchupOtherCopy() == other);
  REQUIRE(r.session->catchupTryOtherCopy());
  CHECK(r.session->catchupTest()->feed == other);
  CHECK(r.session->catchupTest()->days == 3);
  CHECK(r.session->catchupTest()->phase == Phase::Opening);
  CHECK(r.player->plays.back().channelId == r.session->feeds()[other].channelId);
  CHECK(r.player->plays.back().url.find("/" + serverMinute(minuteOf(from)) + "/503.ts") != std::string::npos);
  // Both copies tried: none left.
  CHECK_FALSE(r.session->catchupOtherCopy());
  r.run(10000);
  REQUIRE(r.session->catchupTest()->phase == Phase::Failed);
  CHECK_FALSE(r.session->catchupTryOtherCopy());

  auto lines = r.logged("catch-up test");
  REQUIRE(lines.size() == 2);
  CHECK(lines[0].detail.rfind("yesterday, same time (archive says 2 days): failed, ", 0) == 0);
  CHECK(lines[0].feed == r.session->feeds()[first].label);
  CHECK(lines[1].detail.rfind("yesterday, same time (archive says 3 days): failed, no picture after 10 s", 0) == 0);
  CHECK(lines[1].feed == r.session->feeds()[other].label);
  for (const auto& e : lines) CHECK(e.detail.find("://") == std::string::npos);

  // Back to live: the copy that played before the test (not the one tried last).
  r.player->state = {};
  r.session->endCatchupTest();
  r.run(250);
  CHECK(r.session->feedIndex() == first);
  CHECK(r.player->plays.back().channelId == r.session->feeds()[first].channelId);
  r.picture();
  r.run(25000);
  CHECK(r.memory() == before);
}

TEST_CASE("catch-up test: a channel change, a second test, or the app leaving ends it with its line") {
  Rig r;
  std::string espn = r.entry("ESPN"), cnn = r.entry("CNN");
  r.watchLive(espn, 501);

  // Tested again while one opens: that one ends before a result.
  REQUIRE(r.session->startCatchupTest(Moment::TenMinutesAgo, mtime::now() - 600));
  r.run(1000);
  REQUIRE(r.session->startCatchupTest(Moment::Yesterday, mtime::now() - 86400));
  auto lines = r.logged("catch-up test");
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].detail.find("ended before a result (tested again)") != std::string::npos);

  // The same channel tuned again: back to live.
  size_t plays = r.player->plays.size();
  REQUIRE(r.session->tune(espn));
  CHECK_FALSE(r.session->catchupTest());
  r.run(250);
  CHECK(r.player->plays.size() == plays + 1);
  CHECK(r.player->plays.back().url.find("/timeshift/") == std::string::npos);
  r.picture();
  r.run(25000);

  // The app leaves during a test: it ends, and live plays on return.
  REQUIRE(r.session->startCatchupTest(Moment::TenMinutesAgo, mtime::now() - 600));
  r.session->suspend();
  CHECK_FALSE(r.session->catchupTest());
  r.session->resume();
  r.run(250);
  CHECK(r.player->plays.back().url.find("/timeshift/") == std::string::npos);
  r.picture();
  CHECK(r.session->ready());
  r.run(25000);

  // Another channel: the test ends, that channel plays live.
  REQUIRE(r.session->startCatchupTest(Moment::TenMinutesAgo, mtime::now() - 600));
  REQUIRE(r.session->tune(cnn));
  CHECK_FALSE(r.session->catchupTest());
  CHECK(r.session->entry() == cnn);
  r.run(500);
  CHECK(r.player->plays.back().url.find("/timeshift/") == std::string::npos);
  lines = r.logged("catch-up test");
  REQUIRE(lines.size() == 4);
  CHECK(lines[2].detail.find("ended before a result (app in background)") != std::string::npos);
  CHECK(lines[3].detail.find("ended before a result (another channel)") != std::string::npos);
  // Right goes back to the channel of the test, as it played live.
  CHECK(r.session->previousEntry() == espn);
}
