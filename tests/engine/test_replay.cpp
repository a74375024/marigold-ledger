// Build 165, replay on the pretend TV: an Xtream provider whose copies of a
// channel say they keep (or don't keep) an archive, a fake video player
// whose flags the test sets, and the session's replay: start over, rewind,
// pause, the next stretch, catching up, failures, channel changes and
// Right, with nothing charged and never a link in the log.
#include <doctest.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/engine/replay.hpp"
#include "core/engine/session.hpp"
#include "core/engine/store.hpp"
#include "core/engine/views.hpp"
#include "core/sources/xtream.hpp"
#include "core/util/base64.hpp"
#include "core/util/json.hpp"
#include "engine/loaded_store.hpp"
#include "sources/mock_provider.hpp"

using marigold::engine::AccountForm;
using marigold::engine::Feed;
using marigold::engine::PlayerState;
using marigold::engine::Session;
using marigold::engine::Store;
namespace mtime = marigold::time;
namespace replay = marigold::engine::replay;
using Phase = Session::Replay::Phase;
using Why = Session::ReplayWhy;

namespace {

struct Player final : marigold::engine::Player {
  struct Shared {
    std::vector<Feed> plays;  // every feed handed over, in order
    PlayerState state;
    int stops = 0;
    int pauses = 0;
    bool canPause = true;
  };
  explicit Player(std::shared_ptr<Shared> s) : s_(std::move(s)) {}
  void attempt() override { s_->state = {}; }
  void play(const Feed& f, bool) override { s_->plays.push_back(f); }
  void stop() override { ++s_->stops; }
  bool pause() override {
    ++s_->pauses;
    return s_->canPause;
  }
  PlayerState state() const override { return s_->state; }
  std::shared_ptr<Shared> s_;
};

// One copy in get_live_streams: its archive days (0: none).
std::string stream(int id, const std::string& name, int category, int archiveDays, const std::string& epg = {}) {
  return "{\"num\":" + std::to_string(id) + ",\"name\":\"" + name + "\",\"stream_type\":\"live\",\"stream_id\":" +
         std::to_string(id) + ",\"stream_icon\":\"\",\"epg_channel_id\":" + (epg.empty() ? "null" : "\"" + epg + "\"") +
         ",\"category_id\":\"" +
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

// Build 166: a second provider ("Trex", a one-day archive), on its own host.
constexpr const char* kTrexHost = "http://trex.example.invalid:8080";

marigold::sources::xtream::Client mockClient(const std::string& host = std::string(mock::kHost)) {
  marigold::sources::Account a;
  a.kind = marigold::sources::AccountKind::Xtream;
  a.serverUrl = host;
  a.userName = std::string(mock::kUser);
  a.password = std::string(mock::kPass);
  auto client = marigold::sources::xtream::Client::make(a);
  REQUIRE(client);
  return client.value();
}

struct Rig {
  fake::Tv tv;
  std::unique_ptr<Store> store;
  std::shared_ptr<Player::Shared> player = std::make_shared<Player::Shared>();
  std::unique_ptr<Session> session;
  uint32_t carry = 0;  // fake ms not yet moved onto the wall clock

  // `guide`: the provider's XMLTV file (its channels espn.us and cnn.us).
  explicit Rig(const std::string& guide = {}) {
    auto c = mockClient();
    tv.pool.answer(c.accountInfoRequest().url, accountInfo());
    tv.pool.answer(c.categoriesRequest().url,
                   R"([{"category_id":"1","category_name":"US| SPORTS"},{"category_id":"2","category_name":"US| NEWS"}])");
    if (!guide.empty()) tv.pool.answer(c.guideURL(), guide);
    std::string espn = guide.empty() ? "" : "espn.us", cnn = guide.empty() ? "" : "cnn.us";
    // ESPN: three copies, the first and the third with an archive (the
    // same two days: Build 166 replays on the longest, see the two
    // providers' cases); CNN: two copies, neither has one.
    tv.pool.answer(c.streamsRequest().url, "[" + stream(501, "US: ESPN FHD", 1, 2, espn) + "," +
                                               stream(502, "US: ESPN HD", 1, 0, espn) + "," +
                                               stream(503, "US: ESPN SD", 1, 2, espn) + "," +
                                               stream(504, "US: CNN HD", 2, 0, cnn) + "," +
                                               stream(505, "US: CNN FHD", 2, 0, cnn) + "]");
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

  // Build 166: two providers. Dream (the mock host): ESPN keeping two days,
  // CNN none; Trex: ESPN and TNT keeping one day. `trexFirst`: Trex's
  // account added first.
  struct Two {};
  Rig(Two, bool trexFirst) {
    auto dream = mockClient(), trex = mockClient(kTrexHost);
    for (auto* c : {&dream, &trex}) {
      tv.pool.answer(c->accountInfoRequest().url, accountInfo());
      tv.pool.answer(c->categoriesRequest().url,
                     R"([{"category_id":"1","category_name":"US| SPORTS"},{"category_id":"2","category_name":"US| NEWS"}])");
    }
    tv.pool.answer(dream.streamsRequest().url,
                   "[" + stream(601, "US: ESPN FHD", 1, 2) + "," + stream(604, "US: CNN HD", 2, 0) + "]");
    tv.pool.answer(trex.streamsRequest().url,
                   "[" + stream(701, "US: ESPN HD", 1, 1) + "," + stream(702, "US: TNT HD", 1, 1) + "]");
    store = std::make_unique<Store>(tv.host());
    auto add = [this](const std::string& name, const std::string& host) {
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
    };
    if (trexFirst) add("Trex", kTrexHost);
    add("Dream", std::string(mock::kHost));
    if (!trexFirst) add("Trex", kTrexHost);
    session = std::make_unique<Session>(*store, tv.host(), std::make_unique<Player>(player), Session::Hooks{});
  }

  // The app's loop for `ms`: the wall clock moves with it (`wall`).
  void run(uint32_t ms, uint32_t step = 250, bool wall = true) {
    for (uint32_t t = 0; t < ms; t += step) {
      tv.ms += step;
      if (wall) {
        carry += step;
        if (carry >= 1000) {
          mtime::advanceForTests(carry / 1000);
          carry %= 1000;
        }
      }
      session->tick(tv.ms);
      tv.pool.drain();
    }
  }
  void picture() {
    run(250);
    player->state.picture = true;
    player->state.playing = true;
    run(500);
  }

  std::string entry(const std::string& name) const {
    for (const auto& g : store->channels()->all)
      if (g.name.find(name) != std::string::npos) return g.id;
    return {};
  }
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
  std::vector<std::string> replayLines() const {
    std::vector<std::string> out;
    for (const auto& e : logged("replay")) out.push_back(e.detail);
    return out;
  }
  // No line of the log carries a link or the login.
  void noLinks() const {
    for (const auto& e : session->log().entries()) {
      for (const std::string* f : {&e.detail, &e.feed, &e.provider, &e.channel}) {
        CHECK(f->find("timeshift") == std::string::npos);
        CHECK(f->find("://") == std::string::npos);
        CHECK(f->find("example.invalid") == std::string::npos);
        CHECK(f->find("/u/p/") == std::string::npos);
      }
    }
  }
  // Build 166: the raw channel of stream `id`, and its record (null: none).
  std::string channelOf(int id) const {
    std::string tail = "|" + std::to_string(id);
    for (const auto& g : store->channels()->all)
      for (const Feed& f : store->feeds(g.id))
        if (f.channelId.size() > tail.size() && f.channelId.substr(f.channelId.size() - tail.size()) == tail)
          return f.channelId;
    return {};
  }
  const marigold::engine::scores::Record* record(int id) const {
    std::string c = channelOf(id);
    return c.empty() ? nullptr : store->feedScores().find(c);
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
// "19:30" on the pretend TV (UTC).
std::string hm(mtime::Seconds t) {
  mtime::Civil c = mtime::civilFromSeconds(t);
  char buf[8];
  std::snprintf(buf, sizeof buf, "%02d:%02d", c.hour, c.minute);
  return buf;
}
mtime::Seconds minuteOf(mtime::Seconds t) { return t - t % 60; }
bool isReplay(const Feed& f) { return f.url.find("/timeshift/") != std::string::npos && f.replay; }

}  // namespace

TEST_CASE("replay: offered only where a copy keeps an archive") {
  Rig r;
  std::string cnn = r.entry("CNN"), espn = r.entry("ESPN");
  r.session->setShowingFull(true);
  REQUIRE(r.session->tune(cnn));
  CHECK(r.session->catchupDays() == 0);
  r.run(500);
  r.picture();
  // No archive: nothing offered, and Pause does nothing (the player says
  // live TV can't be paused).
  CHECK(r.session->catchupDays() == 0);
  size_t plays = r.player->plays.size();
  int stops = r.player->stops;
  CHECK_FALSE(r.session->pause());
  CHECK_FALSE(r.session->replayFrom(mtime::now() - 600, Why::Rewind));
  CHECK_FALSE(r.session->replay());
  CHECK(r.player->pauses == 0);
  CHECK(r.player->stops == stops);
  CHECK(r.player->plays.size() == plays);
  CHECK(r.replayLines().empty());
  // ESPN on its copy without an archive: the channel offers replay on the
  // copy that keeps one.
  r.watchLive(espn, 502);
  REQUIRE(r.session->catchupCopy());
  CHECK(*r.session->catchupCopy() == r.feedOf(501));
  CHECK(r.session->catchupDays() == 2);
  CHECK(r.session->behindLive() == 0);
}

TEST_CASE("replay: start over plays the programme's start on the archive copy; rewind moves; Go live returns") {
  Rig r;
  std::string espn = r.entry("ESPN");
  r.watchLive(espn, 502);
  size_t live = r.session->feedIndex();
  Feed liveFeed = r.session->feeds()[live];
  size_t copy = r.feedOf(501);
  std::string before = r.memory();
  size_t plays = r.player->plays.size();

  // Start over: the programme began 38 minutes ago.
  mtime::Seconds start = minuteOf(mtime::now() - 38 * 60);
  REQUIRE(r.session->replayFrom(start + 20, Why::StartOver));
  REQUIRE(r.session->replay());
  CHECK(r.session->replay()->phase == Phase::Opening);
  CHECK(r.session->replay()->feed == copy);
  CHECK(r.session->replay()->from == start);
  CHECK(r.session->opening());
  REQUIRE(r.player->plays.size() == plays + 1);
  const Feed& asked = r.player->plays.back();
  CHECK(isReplay(asked));
  CHECK(asked.channelId == r.session->feeds()[copy].channelId);
  // From that minute in the server's clock, until now and five minutes more.
  CHECK(asked.url == std::string(mock::kHost) + "/timeshift/u/p/43/" + serverMinute(start) + "/501.ts");
  // Live stays as it was: the recovery stands aside on the live copy.
  CHECK(r.session->feedIndex() == live);

  r.picture();
  REQUIRE(r.session->replay()->phase == Phase::Playing);
  CHECK(r.session->ready());
  CHECK_FALSE(r.session->opening());
  const mtime::Seconds behind0 = r.session->behindLive();
  r.run(60000);
  // As far behind live as when it started: the moment shown moves on with
  // the picture.
  mtime::Seconds behind = r.session->behindLive();
  CHECK(behind >= behind0 - 1);
  CHECK(behind <= behind0 + 1);
  CHECK(behind0 >= 38 * 60);
  CHECK(r.session->replayAt() >= start + 59);
  CHECK(replay::behindText(17 * 60 + 5) == "\xe2\x88\x92" "17:05");
  CHECK(replay::behindText(3600 + 2 * 60 + 5) == "\xe2\x88\x92" "1:02:05");

  // Rewind to a minute: the same copy from there.
  mtime::Seconds to = minuteOf(mtime::now() - 17 * 60);
  REQUIRE(r.session->replayFrom(to, Why::Rewind));
  CHECK(r.player->plays.back().url.find("/" + serverMinute(to) + "/501.ts") != std::string::npos);
  r.picture();

  // Go live: the live copy, its live link, nothing charged.
  r.player->state = {};
  r.session->goLive();
  CHECK_FALSE(r.session->replay());
  r.run(250);
  CHECK(r.player->plays.back().channelId == liveFeed.channelId);
  CHECK(r.player->plays.back().url == liveFeed.url);
  CHECK_FALSE(r.player->plays.back().replay);
  CHECK(r.session->feedIndex() == live);
  r.picture();
  r.run(25000);
  CHECK(r.memory() == before);

  std::vector<std::string> lines = r.replayLines();
  REQUIRE(lines.size() == 3);
  CHECK(lines[0] == "start over " + hm(start) + "; server clock UTC\xe2\x88\x92" "5");
  CHECK(lines[1] == "rewind to " + hm(to) + "; server clock UTC\xe2\x88\x92" "5");
  CHECK(lines[2] == "back to live");
  auto entries = r.logged("replay");
  CHECK(entries[0].feed == r.session->feeds()[copy].label);
  CHECK(entries[0].provider == "Mock IPTV");
  CHECK(entries[2].feed == liveFeed.label);
  r.noLinks();
}

TEST_CASE("replay: pause holds the picture, closes the stream within 3 s, and resumes from the paused minute") {
  Rig r;
  std::string espn = r.entry("ESPN");
  r.watchLive(espn, 501);
  size_t live = r.session->feedIndex();
  std::string before = r.memory();
  size_t plays = r.player->plays.size();
  int stops = r.player->stops;

  // Paused on live.
  mtime::Seconds pausedAt = mtime::now();
  REQUIRE(r.session->pause());
  REQUIRE(r.session->replay());
  CHECK(r.session->replay()->phase == Phase::Paused);
  CHECK(r.session->replay()->fromLive);
  CHECK(r.session->replay()->held);
  CHECK(r.player->pauses == 1);
  CHECK(r.player->stops == stops);  // the picture held a moment
  CHECK(r.session->ready());
  r.run(2750);
  CHECK(r.player->stops == stops);
  r.run(500);
  // The stream closed: the provider's connection is free.
  CHECK(r.player->stops == stops + 1);
  CHECK(r.session->replay()->released);
  // Paused however long: nothing more is opened.
  r.run(120000, 1000);
  CHECK(r.player->plays.size() == plays);
  CHECK(r.player->stops == stops + 1);

  // Play: the archive from the paused minute (a few seconds early).
  REQUIRE(r.session->resumePaused());
  REQUIRE(r.player->plays.size() == plays + 1);
  CHECK(isReplay(r.player->plays.back()));
  CHECK(r.player->plays.back().url.find("/" + serverMinute(minuteOf(pausedAt)) + "/501.ts") != std::string::npos);
  CHECK(r.session->replay()->phase == Phase::Opening);
  r.picture();
  CHECK(r.session->behindLive() >= 120);

  // Paused again during the replay, on a player that can't hold a picture:
  // the stream closes at once.
  r.run(30000);
  mtime::Seconds at = r.session->replayAt();
  r.player->canPause = false;
  stops = r.player->stops;
  REQUIRE(r.session->pause());
  CHECK(r.player->stops == stops + 1);
  CHECK_FALSE(r.session->replay()->held);
  CHECK(r.session->replay()->released);
  CHECK_FALSE(r.session->replay()->fromLive);
  r.run(5000);
  CHECK(r.session->replayAt() == at);
  REQUIRE(r.session->resumePaused());
  CHECK(r.player->plays.back().url.find("/" + serverMinute(minuteOf(at)) + "/501.ts") != std::string::npos);
  r.picture();
  r.session->goLive();
  r.run(250);
  CHECK(r.session->feedIndex() == live);
  r.picture();
  r.run(25000);
  CHECK(r.memory() == before);

  std::vector<std::string> lines = r.replayLines();
  REQUIRE(lines.size() == 5);
  CHECK(lines[0] == "paused at " + hm(pausedAt) + " (live)");
  CHECK(lines[1].rfind("resumed " + hm(minuteOf(pausedAt)) + "; server clock", 0) == 0);
  CHECK(lines[2] == "paused at " + hm(at));
  CHECK(lines[3].rfind("resumed " + hm(minuteOf(at)), 0) == 0);
  CHECK(lines[4] == "back to live");
  r.noLinks();
}

TEST_CASE("replay: a stretch that ends opens the next from where it ended; reaching live goes live") {
  Rig r;
  std::string espn = r.entry("ESPN");
  r.watchLive(espn, 501);
  size_t live = r.session->feedIndex();
  Feed liveFeed = r.session->feeds()[live];

  // Five hours back: the provider is asked for its longest stretch.
  mtime::Seconds from = minuteOf(mtime::now() - 5 * 3600);
  REQUIRE(r.session->replayFrom(from, Why::Rewind));
  CHECK(r.player->plays.back().url.find("/timeshift/u/p/180/" + serverMinute(from) + "/501.ts") != std::string::npos);
  r.picture();
  // Three hours of it; then the stream ends, still five hours behind live.
  r.run(3 * 3600 * 1000, 1000);
  CHECK(r.session->replayAt() >= from + 3 * 3600 - 2);
  size_t plays = r.player->plays.size();
  r.player->state.ended = true;
  r.run(250);
  REQUIRE(r.player->plays.size() == plays + 1);
  mtime::Seconds next = minuteOf(r.session->replayAt());
  CHECK(next >= from + 3 * 3600 - 60);
  CHECK(r.player->plays.back().url.find("/" + serverMinute(next) + "/501.ts") != std::string::npos);
  // Three hours again (live is still five hours on).
  CHECK(r.player->plays.back().url.find("/timeshift/u/p/180/") != std::string::npos);
  // Seamless: no notice, the picture's place kept, no spinner.
  CHECK(r.session->takeReplayNotice().empty());
  CHECK(r.session->replay()->next);
  CHECK(r.session->ready());
  CHECK_FALSE(r.session->opening());
  r.picture();
  CHECK_FALSE(r.session->replay()->next);

  // Catching up: the picture reaches live (within a minute).
  mtime::Seconds behind = r.session->behindLive();
  r.run(static_cast<uint32_t>(behind - 50) * 1000, 1000, false);
  CHECK_FALSE(r.session->replay());
  CHECK(r.session->takeReplayNotice() == "You\xe2\x80\x99re live");
  CHECK(r.session->takeReplayNotice().empty());
  r.run(250);
  CHECK(r.player->plays.back().channelId == liveFeed.channelId);
  CHECK_FALSE(r.player->plays.back().replay);
  std::vector<std::string> lines = r.replayLines();
  REQUIRE(lines.size() == 3);
  CHECK(lines[1].rfind("next stretch from " + hm(next) + "; server clock", 0) == 0);
  CHECK(lines[2] == "caught up");
  r.noLinks();
}

TEST_CASE("replay: a copy that fails tries the next with an archive, then goes live and says so") {
  Rig r;
  std::string espn = r.entry("ESPN");
  r.watchLive(espn, 502);
  size_t live = r.session->feedIndex();
  std::string before = r.memory();
  size_t first = r.feedOf(501), other = r.feedOf(503);

  mtime::Seconds from = minuteOf(mtime::now() - 3600);
  REQUIRE(r.session->replayFrom(from, Why::Rewind));
  CHECK(r.session->replay()->feed == first);
  // An error before any picture: the other copy with an archive, same minute.
  r.player->state.failed = true;
  r.player->state.error = "HTTP 404 from http://mock.example.invalid:8080/timeshift/u/p/65/x/501.ts";
  r.run(250);
  REQUIRE(r.session->replay());
  CHECK(r.session->replay()->feed == other);
  CHECK(r.player->plays.back().channelId == r.session->feeds()[other].channelId);
  CHECK(r.player->plays.back().url.find("/" + serverMinute(from) + "/503.ts") != std::string::npos);
  CHECK(r.session->takeReplayNotice().empty());
  // No picture there in 10 s: none left, back to live with a notice.
  r.run(9750);
  REQUIRE(r.session->replay());
  r.run(250);
  CHECK_FALSE(r.session->replay());
  CHECK(r.session->takeReplayNotice() == "Replay isn\xe2\x80\x99t available right now on this channel");
  r.run(250);
  CHECK(r.session->feedIndex() == live);
  CHECK(r.player->plays.back().channelId == r.session->feeds()[live].channelId);
  CHECK_FALSE(r.player->plays.back().replay);
  r.picture();
  r.run(25000);
  CHECK(r.memory() == before);

  std::vector<std::string> lines = r.replayLines();
  REQUIRE(lines.size() == 4);
  CHECK(lines[1].rfind("failed: HTTP 404", 0) == 0);
  CHECK(lines[1].find("; another copy") != std::string::npos);
  CHECK(lines[2].rfind("another copy from " + hm(from), 0) == 0);
  CHECK(lines[3] == "failed: no picture after 10 s; back to live");
  auto entries = r.logged("replay");
  CHECK(entries[1].feed == r.session->feeds()[first].label);
  CHECK(entries[3].feed == r.session->feeds()[other].label);
  r.noLinks();
}

TEST_CASE("replay: Up/Down plays the next channel live; Right returns to the moment, or live once it's gone") {
  Rig r;
  std::string espn = r.entry("ESPN"), cnn = r.entry("CNN");
  r.watchLive(cnn, 504);
  r.watchLive(espn, 501);
  std::string flipTo = cnn;

  mtime::Seconds from = minuteOf(mtime::now() - 40 * 60);
  REQUIRE(r.session->replayFrom(from, Why::Rewind));
  r.picture();
  r.run(60000);
  mtime::Seconds left = r.session->replayAt();
  // A channel change: CNN live, the replay ended.
  REQUIRE(r.session->tune(flipTo));
  CHECK_FALSE(r.session->replay());
  r.run(250);
  CHECK_FALSE(r.player->plays.back().replay);
  CHECK(r.session->entry() == cnn);
  r.picture();
  r.run(25000);
  // Right: ESPN's replay, from the moment it showed when left.
  REQUIRE(r.session->previousEntry() == espn);
  REQUIRE(r.session->previousTarget()->replayAt);
  CHECK(*r.session->previousTarget()->replayAt == left);
  size_t plays = r.player->plays.size();
  CHECK(r.session->previous() == espn);
  REQUIRE(r.session->replay());
  CHECK(r.player->plays.size() == plays + 1);  // straight to the archive, no live start first
  CHECK(isReplay(r.player->plays.back()));
  CHECK(r.player->plays.back().url.find("/" + serverMinute(minuteOf(left)) + "/501.ts") != std::string::npos);
  r.picture();
  CHECK(r.session->replay()->phase == Phase::Playing);
  // Live comes back on the copy that played live.
  r.session->goLive();
  r.run(250);
  CHECK(r.session->feedIndex() == r.feedOf(501));
  r.picture();
  r.run(25000);

  // Again, paused this time; then the archive (two days) no longer holds it.
  REQUIRE(r.session->replayFrom(minuteOf(mtime::now() - 3600), Why::Rewind));
  r.picture();
  REQUIRE(r.session->pause());
  REQUIRE(r.session->tune(cnn));
  r.run(250);
  r.picture();
  r.run(25000);
  REQUIRE(r.session->previousTarget()->replayAt);
  mtime::advanceForTests(2 * 86400);
  CHECK(r.session->previous() == espn);
  CHECK_FALSE(r.session->replay());
  r.run(250);
  CHECK_FALSE(r.player->plays.back().replay);
  r.picture();
  CHECK(r.session->ready());
  std::vector<std::string> lines = r.replayLines();
  REQUIRE(!lines.empty());
  CHECK(std::count_if(lines.begin(), lines.end(), [](const std::string& l) { return l.rfind("back to ", 0) == 0 && l != "back to live"; }) == 1);
  r.noLinks();
}

TEST_CASE("replay: the Rewind timeline holding live is no moment left; Back from it plays live again") {
  Rig r;
  std::string espn = r.entry("ESPN"), cnn = r.entry("CNN");
  r.watchLive(cnn, 504);
  r.watchLive(espn, 501);
  size_t plays = r.player->plays.size();
  // Opened from live: the picture holds, quietly.
  REQUIRE(r.session->pause(true));
  CHECK(r.replayLines().empty());
  // Back: live again, quietly.
  r.session->goLive(true);
  r.run(250);
  CHECK(r.player->plays.size() == plays + 1);
  CHECK_FALSE(r.player->plays.back().replay);
  CHECK(r.replayLines().empty());
  r.picture();
  r.run(25000);
  // Held, then another channel: Right plays ESPN live, not from then.
  REQUIRE(r.session->pause(true));
  REQUIRE(r.session->tune(cnn));
  r.run(250);
  r.picture();
  r.run(25000);
  REQUIRE(r.session->previousTarget());
  CHECK_FALSE(r.session->previousTarget()->replayAt);
  CHECK(r.session->previous() == espn);
  CHECK_FALSE(r.session->replay());
}

TEST_CASE("replay: the app leaving and coming back") {
  Rig r;
  std::string espn = r.entry("ESPN");
  r.watchLive(espn, 501);
  // Playing a replay: it plays on from the same moment.
  REQUIRE(r.session->replayFrom(minuteOf(mtime::now() - 1800), Why::Rewind));
  r.picture();
  r.run(30000);
  mtime::Seconds at = r.session->replayAt();
  r.session->suspend();
  CHECK_FALSE(r.session->replay());
  r.run(60000, 1000);
  size_t plays = r.player->plays.size();
  r.session->resume();
  REQUIRE(r.session->replay());
  REQUIRE(r.player->plays.size() == plays + 1);
  CHECK(r.player->plays.back().url.find("/" + serverMinute(minuteOf(at)) + "/501.ts") != std::string::npos);
  r.picture();
  // Paused: it stays paused, its stream closed.
  REQUIRE(r.session->pause());
  int stops = r.player->stops;
  r.session->suspend();
  CHECK(r.player->stops == stops + 1);
  r.session->resume();
  REQUIRE(r.session->replay());
  CHECK(r.session->replay()->phase == Phase::Paused);
  CHECK(r.player->plays.size() == plays + 1);
}

TEST_CASE("replay: the copy's own past is asked once per channel and dropped on leaving it") {
  Rig r;
  auto c = mockClient();
  std::string espn = r.entry("ESPN"), cnn = r.entry("CNN");
  mtime::Seconds now = mtime::now();
  auto b64 = [](const std::string& s) { return marigold::base64::encode(s); };
  std::string table = R"({"epg_listings":[)"
                      R"({"title":")" + b64("Morning Show") + R"(","start_timestamp":")" + std::to_string(now - 47 * 3600) +
                      R"(","stop_timestamp":")" + std::to_string(now - 46 * 3600) + R"(","has_archive":1},)"
                      R"({"title":")" + b64("Game Night") + R"(","start_timestamp":)" + std::to_string(now - 3600) +
                      R"(,"stop_timestamp":)" + std::to_string(now + 3600) + R"(,"has_archive":"1"},)"
                      R"({"title":")" + b64("Later") + R"(","start_timestamp":)" + std::to_string(now + 3600) +
                      R"(,"stop_timestamp":)" + std::to_string(now + 7200) + R"(,"has_archive":0}]})";
  std::string url = c.simpleDataTableRequest("501").url;
  r.tv.pool.answer(url, table);
  r.watchLive(espn, 501);
  int serial = r.session->pastSerial();
  r.session->wantPast();
  r.tv.pool.drain();
  CHECK(r.session->pastSerial() == serial + 1);
  std::vector<replay::Programme> t = r.session->timeline(now - 48 * 3600);
  auto named = [&t](const std::string& title) {
    return std::any_of(t.begin(), t.end(), [&](const replay::Programme& p) { return p.title == title; });
  };
  CHECK(named("Morning Show"));
  CHECK(named("Game Night"));
  auto lines = r.replayLines();
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == "past listings: 47 h, 2 in archive");
  // Asked once while the channel plays.
  r.session->wantPast();
  r.tv.pool.drain();
  CHECK(r.session->pastSerial() == serial + 1);
  CHECK(r.replayLines().size() == 1);
  // Another channel: dropped (and nothing to ask on a channel without one).
  r.watchLive(cnn, 504);
  r.session->wantPast();
  r.tv.pool.drain();
  r.watchLive(espn, 501);
  t = r.session->timeline(now - 48 * 3600);
  CHECK_FALSE(named("Morning Show"));
  // A failed request is fine: said once, the timeline works by minutes.
  r.tv.pool.answer(url, "nope", 500);
  r.session->wantPast();
  r.tv.pool.drain();
  lines = r.replayLines();
  REQUIRE(lines.size() == 2);
  CHECK(lines[1] == "past listings: the request failed");
  r.noLinks();
}

TEST_CASE("replay: the provider's guide keeps six hours back for channels with catch-up, three for others") {
  // An hour-long programme every hour, from nine hours back.
  auto stamp = [](mtime::Seconds t) {
    mtime::Civil c = mtime::civilFromSeconds(t);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d%02d%02d%02d%02d00 +0000", c.year, c.month, c.day, c.hour, c.minute);
    return std::string(buf);
  };
  mtime::Seconds hour = mtime::now() - mtime::now() % 3600;
  std::string xml = "<?xml version=\"1.0\"?><tv><channel id=\"espn.us\"><display-name>ESPN</display-name></channel>"
                    "<channel id=\"cnn.us\"><display-name>CNN</display-name></channel>";
  for (const char* ch : {"espn.us", "cnn.us"})
    for (int h = -9; h <= 6; ++h)
      xml += std::string("<programme start=\"") + stamp(hour + h * 3600) + "\" stop=\"" + stamp(hour + (h + 1) * 3600) +
             "\" channel=\"" + ch + "\"><title>" + ch + " " + std::to_string(h) + "</title>"
             "<desc>About it.</desc></programme>";
  xml += "</tv>";
  Rig r(xml);
  std::string espn = r.entry("ESPN"), cnn = r.entry("CNN");
  REQUIRE(!espn.empty());
  auto earliest = [&](const std::string& entry) {
    std::vector<marigold::engine::Listing> l = r.store->pastListing(entry, mtime::now() - 12 * 3600);
    return l.empty() ? std::string() : l.front().title;
  };
  // ESPN (copies with an archive): what ended up to six hours before the
  // read; CNN: three.
  CHECK(earliest(espn) == "espn.us -6");
  CHECK(earliest(cnn) == "cnn.us -3");
  // The normal guide window is as it was: an hour before its start.
  const auto* shown = r.store->listings()->forChannel(espn);
  if (shown && !shown->empty()) CHECK(shown->front().start >= hour - 2 * 3600);
  // The guide read says how far back the file reached, in the playback log.
  r.watchLive(cnn, 504);
  bool said = false;
  // (The earliest programme ended eight hours and some minutes ago.)
  for (const auto& e : r.logged("guide"))
    said |= e.detail == "guide past: provider file reaches back 8 h" || e.detail == "guide past: provider file reaches back 9 h";
  CHECK(said);
  // The replay's timeline names the past from it.
  r.watchLive(espn, 501);
  std::vector<replay::Programme> t = r.session->timeline(hour - 6 * 3600);
  REQUIRE(!t.empty());
  CHECK(t.front().title == "espn.us -6");
}

TEST_CASE("replay timeline: minute steps, holding speeds up, programme jumps, the archive and live") {
  const mtime::Seconds live = 1'800'000'000 + 38 * 60 + 25;  // 20:38:25 or so
  const mtime::Seconds earliest = replay::archiveStart(live, 2);
  CHECK(earliest % 60 == 0);
  // Build 166: an hour in hand (a one-day archive failed exactly a day back).
  CHECK(earliest >= live - 2 * 86400 + 3600);
  CHECK(earliest < live - 2 * 86400 + 3660);
  CHECK(replay::archiveStart(live, 1) >= live - 23 * 3600);
  CHECK(replay::archiveStart(live, 1) < live - 23 * 3600 + 60);
  CHECK(replay::covers(1, live - 23 * 3600 + 60, live));
  CHECK_FALSE(replay::covers(1, live - 24 * 3600, live));
  CHECK_FALSE(replay::covers(1, live - 23 * 3600 - 60, live));
  CHECK_FALSE(replay::covers(0, live - 60, live));
  // Left from live: the minute it's in, then one minute at a time.
  mtime::Seconds at = replay::step(live, -1, 0, earliest, live);
  CHECK(at == live - 25);
  CHECK(replay::step(at, -1, 0, earliest, live) == at - 60);
  CHECK(replay::step(at, +1, 0, earliest, live) == live);  // never past live
  // Holding: one minute, then five, then fifteen.
  CHECK(replay::stepMinutes(0) == 1);
  CHECK(replay::stepMinutes(5) == 1);
  CHECK(replay::stepMinutes(6) == 5);
  CHECK(replay::stepMinutes(15) == 5);
  CHECK(replay::stepMinutes(16) == 15);
  CHECK(replay::step(at, -1, 10, earliest, live) == at - 300);
  CHECK(replay::step(at, -1, 30, earliest, live) == at - 900);
  // Clamped to the archive.
  CHECK(replay::step(earliest + 60, -1, 30, earliest, live) == earliest);

  // Programmes: 19:00-20:00 "Before", 20:00-21:00 "Now".
  mtime::Seconds eight = minuteOf(live) - 38 * 60;
  std::vector<replay::Programme> list = {{eight - 3600, eight, "Before", true}, {eight, eight + 3600, "Now", true}};
  REQUIRE(replay::on(list, live));
  CHECK(replay::on(list, live)->title == "Now");
  // Up: the start of this programme, then the one before; Down: the next,
  // and live after the last.
  mtime::Seconds p = replay::jump(list, at, -1, earliest, live);
  CHECK(p == eight);
  p = replay::jump(list, p, -1, earliest, live);
  CHECK(p == eight - 3600);
  CHECK(replay::jump(list, p, +1, earliest, live) == eight);
  CHECK(replay::jump(list, eight, +1, earliest, live) == live);
  // Before any listing: half hours.
  mtime::Seconds old = eight - 3 * 3600 + 10 * 60;
  CHECK(replay::jump(list, old, -1, earliest, live) == eight - 3 * 3600);
  CHECK(replay::jump(list, old, +1, earliest, live) == eight - 3 * 3600 + 1800);
  // Never before the archive.
  CHECK(replay::jump({}, earliest + 60, -1, earliest, live) == earliest);

  // The bar: from the start of the programme before (or two hours back).
  replay::Span s = replay::opening(list, live, earliest);
  CHECK(s.to == live);
  CHECK(s.from == live - 2 * 3600);
  std::vector<replay::Programme> longer = {{eight - 3 * 3600, eight, "Film", true}, {eight, eight + 3600, "Now", true}};
  CHECK(replay::opening(longer, live, earliest).from == eight - 3 * 3600);
  // Moving before it scrolls it back, its length kept; never before the archive.
  replay::Span moved = replay::follow(s, s.from - 600, earliest, live);
  CHECK(moved.length() == s.length());
  CHECK(moved.from < s.from - 600);
  CHECK(replay::follow(s, live - 600, earliest, live).from == s.from);
  replay::Span far = replay::follow(s, earliest, earliest, live);
  CHECK(far.from >= earliest - s.length() / 10);
  // A click on the bar: that minute.
  CHECK(replay::at(s, 0.5, earliest, live) == minuteOf(s.from + s.length() / 2));
  CHECK(replay::at(s, 1.0, earliest, live) == live);
  // The copy's own past fills the guide's gaps only.
  std::vector<replay::Programme> own = {{eight - 3600, eight, "Other title", true},
                                        {eight - 5 * 3600, eight - 4 * 3600, "Old", true}};
  std::vector<replay::Programme> all = replay::merged(list, own);
  REQUIRE(all.size() == 3);
  CHECK(all[0].title == "Old");
  CHECK(all[1].title == "Before");
  CHECK(replay::behindMinutes(17 * 60) == "\xe2\x88\x92" "17 min");
  CHECK(replay::behindMinutes(62 * 60) == "\xe2\x88\x92" "1 h 02 min");
}

// ------------------------------------------------- Build 166: 1.0.10

TEST_CASE("replay 1.0.10: Dream (2 days) and Trex (1 day) copies, either order: the longest archive serves") {
  for (bool trexFirst : {false, true}) {
    CAPTURE(trexFirst);
    Rig r(Rig::Two{}, trexFirst);
    std::string espn = r.entry("ESPN");
    REQUIRE(!espn.empty());
    // Live on the Trex copy: replay still reaches two days back, on Dream.
    r.watchLive(espn, 701);
    REQUIRE(r.session->feeds().size() == 2);
    size_t dream = r.feedOf(601), trex = r.feedOf(701);
    REQUIRE(r.session->catchupCopy());
    CHECK(*r.session->catchupCopy() == dream);
    CHECK(r.session->catchupDays() == 2);
    // Thirty hours back: only Dream holds it.
    mtime::Seconds far = minuteOf(mtime::now() - 30 * 3600);
    REQUIRE(r.session->replayFrom(far, Why::Rewind));
    CHECK(r.session->replay()->feed == dream);
    CHECK(r.player->plays.back().url.rfind(std::string(mock::kHost) + "/timeshift/", 0) == 0);
    // Dream fails there: Trex's day doesn't reach it, so live, with the notice.
    r.player->state.failed = true;
    r.run(250);
    CHECK_FALSE(r.session->replay());
    CHECK(r.session->takeReplayNotice() == "Replay isn\xe2\x80\x99t available right now on this channel");
    r.run(250);
    CHECK(r.session->feedIndex() == trex);
    r.picture();
    r.run(25000);
    // Three hours back: Dream first (longest), and Trex takes over the same
    // minute when Dream fails.
    mtime::Seconds near = minuteOf(mtime::now() - 3 * 3600);
    REQUIRE(r.session->replayFrom(near, Why::Rewind));
    CHECK(r.session->replay()->feed == dream);
    r.player->state.failed = true;
    r.run(250);
    REQUIRE(r.session->replay());
    CHECK(r.session->replay()->feed == trex);
    CHECK(r.player->plays.back().url.rfind(std::string(kTrexHost) + "/timeshift/", 0) == 0);
    CHECK(r.player->plays.back().url.find("/" + serverMinute(near) + "/701.ts") != std::string::npos);
    r.picture();
    CHECK(r.session->replay()->phase == Phase::Playing);
    // During the replay the timeline still reaches Dream's two days.
    CHECK(r.session->catchupDays() == 2);
    // Further back than three days: held at Dream's edge, an hour in hand.
    mtime::Seconds now = mtime::now();
    REQUIRE(r.session->replayFrom(now - 3 * 86400, Why::Rewind));
    CHECK(r.session->replay()->feed == dream);
    CHECK(r.session->replay()->from == replay::archiveStart(now, 2));
    CHECK(r.session->replay()->from >= now - 47 * 3600);
    r.noLinks();
  }
}

TEST_CASE("replay 1.0.10: a one-day archive stops an hour short of a day, for rewind and Right") {
  Rig r(Rig::Two{}, false);
  std::string tnt = r.entry("TNT"), cnn = r.entry("CNN");
  REQUIRE(!tnt.empty());
  r.watchLive(cnn, 604);
  r.watchLive(tnt, 702);
  CHECK(r.session->catchupDays() == 1);
  // Exactly a day back (what failed on the provider): an hour later than that.
  mtime::Seconds now = mtime::now();
  REQUIRE(r.session->replayFrom(now - 86400, Why::Rewind));
  mtime::Seconds from = r.session->replay()->from;
  CHECK(from == replay::archiveStart(now, 1));
  CHECK(from >= now - 23 * 3600);
  CHECK(from < now - 23 * 3600 + 60);
  CHECK(r.player->plays.back().url.find("/" + serverMinute(from) + "/702.ts") != std::string::npos);
  r.picture();
  // Left at that moment for CNN; ten minutes on the moment is past the
  // edge: Right plays TNT live, not from then.
  REQUIRE(r.session->tune(cnn));
  r.run(250);
  r.picture();
  r.run(25000);
  REQUIRE(r.session->previousTarget());
  REQUIRE(r.session->previousTarget()->replayAt);
  mtime::advanceForTests(10 * 60);
  CHECK(r.session->previous() == tnt);
  CHECK_FALSE(r.session->replay());
  r.run(250);
  CHECK_FALSE(r.player->plays.back().replay);
  r.noLinks();
}

TEST_CASE("feed scores in play: a start's time, stalls, failures and time played go into the copy's record") {
  Rig r;
  std::string espn = r.entry("ESPN"), cnn = r.entry("CNN");
  CHECK(r.record(501) == nullptr);
  r.watchLive(espn, 501);
  const marigold::engine::scores::Record* rec = r.record(501);
  REQUIRE(rec);
  CHECK(rec->plays == doctest::Approx(1));
  CHECK(rec->failures == 0);
  REQUIRE(rec->startMs.size() == 1);
  CHECK(rec->startMs[0] >= 250);
  CHECK(rec->startMs[0] <= 2000);
  // The picture measured on it (after 20 s of play).
  r.player->state.facts = "1920x1080 59.94p \xc2\xb7 h264 \xc2\xb7 aac";
  r.run(22000);
  CHECK(r.record(501)->height == 1080);
  CHECK(r.record(501)->fps == 60);
  // A stall the viewer sees counts once.
  r.player->state.buffering = true;
  r.run(4000);
  r.player->state.buffering = false;
  r.run(1000);
  CHECK(r.record(501)->stalls == doctest::Approx(1));

  // A replay and back to live: nothing new counted for or against a copy.
  auto counted = [&r](int id) {
    const auto* x = r.record(id);
    return x ? x->plays + x->failures + x->stalls : 0.0;
  };
  double c501 = counted(501), c503 = counted(503);
  REQUIRE(r.session->replayFrom(minuteOf(mtime::now() - 1800), Why::Rewind));
  r.picture();
  r.run(30000);
  r.session->goLive();
  r.run(250);
  r.picture();
  r.run(5000);
  CHECK(counted(501) == doctest::Approx(c501));
  CHECK(counted(503) == doctest::Approx(c503));

  // Leaving: the time its picture played goes in.
  REQUIRE(r.session->tune(cnn));
  r.run(250);
  CHECK(r.record(501)->playedS >= 50);
  // CNN never shows a picture: start failures on its copies, no plays.
  r.player->state = {};
  r.run(90000);
  const auto* bad = r.record(504);
  REQUIRE(bad);
  CHECK(bad->failures >= 1);
  CHECK(bad->plays == 0);

  // Saved in its own file (at most every 30 s, and when the app leaves)
  // and read back by the next run.
  r.session->saveLogNow();
  r.tv.pool.drain();
  auto text = r.tv.files.read("feed-scores.json");
  REQUIRE(text);
  CHECK(text->find("://") == std::string::npos);
  Store again(r.tv.host());
  const auto* back = again.feedScores().find(r.channelOf(501));
  REQUIRE(back);
  CHECK(back->plays == doctest::Approx(r.record(501)->plays).epsilon(0.01));
  CHECK(back->height == 1080);
  r.noLinks();
}

TEST_CASE("feed scores in order: a clearly better copy goes first; none of it without history; pinned still first") {
  Rig r;
  std::string espn = r.entry("ESPN");
  auto order = [&r, &espn] {
    std::vector<std::string> out;
    for (const Feed& f : r.store->feeds(espn)) out.push_back(f.channelId.substr(f.channelId.size() - 3));
    return out;
  };
  CHECK(order() == std::vector<std::string>{"501", "502", "503"});
  // 503 starts every time, fast, at 1080p60; 501 mostly fails.
  std::string c501 = r.channelOf(501), c503 = r.channelOf(503);
  for (int i = 0; i < 4; ++i) r.store->noteStart(c503, 1500);
  r.store->notePicture(c503, marigold::engine::quality::Picture{1080, 60, false});
  r.store->noteStart(c501, 9000);
  for (int i = 0; i < 3; ++i) r.store->noteStartFailed(c501);
  // 502 has no history: it keeps its place; the measured two swap.
  CHECK(order() == std::vector<std::string>{"503", "502", "501"});
  // "Always start with" 501: it stays first.
  r.store->pinFeed(espn, c501);
  CHECK(order() == std::vector<std::string>{"501", "502", "503"});
  r.store->pinFeed(espn, "");
  CHECK(order() == std::vector<std::string>{"503", "502", "501"});
  // Probes (Test all sources, Build 166) feed the same records: 502 measured
  // well now goes ahead of 501 too, and being about as good as 503 it
  // keeps its catalog place ahead of it.
  marigold::engine::scores::Probe p;
  p.ok = true;
  p.startMs = 1400;
  p.height = 1080;
  p.fps = 60;
  for (int i = 0; i < 4; ++i) r.store->recordProbe(r.channelOf(502), p);
  CHECK(order() == std::vector<std::string>{"502", "503", "501"});
  CHECK(r.store->feedScore(r.channelOf(502)));
}

TEST_CASE("providers in the playback log: once a day after the app starts, and on export") {
  Rig r(Rig::Two{}, false);
  std::string espn = r.entry("ESPN"), tnt = r.entry("TNT"), cnn = r.entry("CNN");
  // No lineup yet: the daily lines wait for it.
  r.run(61000);
  CHECK(r.logged("providers").empty());
  r.store->addToLineup({espn, tnt, cnn});
  fake::settle(*r.store, r.tv);
  r.watchLive(espn, 701);
  r.run(61000);
  auto lines = r.logged("providers");
  REQUIRE(lines.size() == 3);
  CHECK(lines[0].provider == "Dream");
  CHECK(lines[0].session == 0);
  // Dream: ESPN and CNN (2 of 3), alone on CNN, replay on ESPN.
  CHECK(lines[0].detail == "no plays yet; lineup 67%, only here 33%, replay 33%");
  CHECK(lines[1].provider == "Trex");
  CHECK(lines[1].detail.rfind("plays 1.0, picture ", 0) == 0);
  CHECK(lines[1].detail.find("start failures 0%; lineup 67%, only here 33%, replay 67%") != std::string::npos);
  CHECK(lines[2].provider == "Dream + Trex");
  CHECK(lines[2].detail == "together: lineup 100%, replay 67%");  // CNN has none
  // Not again the same day, in this run or the next.
  r.run(120000);
  CHECK(r.logged("providers").size() == 3);
  {
    r.session->saveLogNow();
    r.tv.pool.drain();
    Session next(*r.store, r.tv.host(), std::make_unique<Player>(r.player), Session::Hooks{});
    size_t before = next.log().size();
    next.tick(r.tv.ms + 1000);
    next.tick(r.tv.ms + 70000);
    size_t added = 0;
    for (size_t i = before; i < next.log().size(); ++i) added += next.log().entries()[i].event == "providers";
    CHECK(added == 0);
  }
  // On export: as of now, whatever the day.
  CHECK(r.session->logProviders(true));
  CHECK(r.logged("providers").size() == 6);
  r.noLinks();
}

// Build 167: a row's replay is the longest usable archive among its own
// copies: what the providers say, less what Test all sources found broken,
// lowered where it measured less. Published with the rows (and the same as
// every row worked out again).
TEST_CASE("replay marks 1.0.11: a row's replay days, its depth by provider, and what the test found") {
  Rig r(Rig::Two{}, false);
  std::string espn = r.entry("ESPN"), tnt = r.entry("TNT"), cnn = r.entry("CNN");
  REQUIRE(!espn.empty());
  REQUIRE(!tnt.empty());
  REQUIRE(!cnn.empty());
  auto days = [&](const std::string& id) {
    const auto* g = r.store->channels()->find(id);
    REQUIRE(g);
    const auto* again = r.store->channelsNow()->find(id);
    REQUIRE(again);
    CHECK(again->replayDays == g->replayDays);
    return static_cast<int>(g->replayDays);
  };
  // Claimed: ESPN two days (Dream) and one (Trex), TNT one, CNN none.
  CHECK(days(espn) == 2);
  CHECK(days(tnt) == 1);
  CHECK(days(cnn) == 0);
  using ByProvider = std::vector<std::pair<std::string, uint16_t>>;
  CHECK(r.store->replayByProvider(espn) == ByProvider{{"Dream", 2}, {"Trex", 1}});
  CHECK(r.store->replayByProvider(cnn).empty());
  CHECK(marigold::engine::views::replayAbout(r.store->replayByProvider(espn)) ==
        "2 days on Dream, 1 day on Trex. Pause, rewind and start over work on it, and its games can be watched "
        "from the start.");
  // The "Can go back" filter: ESPN and TNT, not CNN.
  {
    auto ch = r.store->channels();
    marigold::engine::views::Filter back{marigold::engine::views::Filter::Kind::CanGoBack, {}};
    CHECK(marigold::engine::views::passes(*ch->find(espn), back, nullptr, mtime::now()));
    CHECK(marigold::engine::views::passes(*ch->find(tnt), back, nullptr, mtime::now()));
    CHECK_FALSE(marigold::engine::views::passes(*ch->find(cnn), back, nullptr, mtime::now()));
  }
  auto probe = [&](int id, bool archiveOk, uint16_t depth) {
    marigold::engine::scores::Probe p;
    p.ok = true;
    p.startMs = 1500;
    p.archiveOk = archiveOk;
    p.archiveDays = depth;
    r.store->recordProbe(r.channelOf(id), p);
    fake::settle(*r.store, r.tv);
  };
  // Dream's two-day archive measured at one day: ESPN goes back one day.
  probe(601, true, 1);
  CHECK(days(espn) == 1);
  CHECK(r.store->replayByProvider(espn) == ByProvider{{"Dream", 1}, {"Trex", 1}});
  // Trex's played (its depth not confirmed): its claim stands.
  probe(701, true, 0);
  CHECK(days(espn) == 1);
  // Dream's didn't play at all: only Trex's day counts.
  probe(601, false, 0);
  CHECK(days(espn) == 1);
  CHECK(r.store->replayByProvider(espn) == ByProvider{{"Trex", 1}});
  // TNT's only archive is broken: no replay there, nothing in the filter.
  probe(702, false, 0);
  CHECK(days(tnt) == 0);
  CHECK(r.store->replayByProvider(tnt).empty());
  // A live probe without an archive result changes nothing.
  marigold::engine::scores::Probe live;
  live.ok = true;
  live.startMs = 900;
  r.store->recordProbe(r.channelOf(701), live);
  fake::settle(*r.store, r.tv);
  CHECK(days(espn) == 1);
}

TEST_CASE("replay marks 1.0.11: the guide's Start over tunes straight into the archive from the programme's start") {
  Rig r(Rig::Two{}, false);
  std::string espn = r.entry("ESPN"), cnn = r.entry("CNN");
  REQUIRE(!espn.empty());
  mtime::Seconds start = minuteOf(mtime::now() - 40 * 60);
  // Nothing playing: the archive from the start, no live start first.
  size_t plays = r.player->plays.size();
  r.session->setShowingFull(true);
  REQUIRE(r.session->startOverFrom(espn, start));
  r.run(250);
  REQUIRE(r.player->plays.size() == plays + 1);
  CHECK(isReplay(r.player->plays.back()));
  REQUIRE(r.session->replay());
  CHECK(r.session->replay()->from == start);
  CHECK(r.session->entry() == espn);
  std::vector<std::string> lines = r.replayLines();
  REQUIRE(!lines.empty());
  CHECK(lines.back().rfind("start over " + hm(start), 0) == 0);
  r.picture();
  CHECK(r.session->replay()->phase == Phase::Playing);
  // The channel playing (behind live): its replay from the new moment.
  mtime::Seconds earlier = minuteOf(mtime::now() - 90 * 60);
  REQUIRE(r.session->startOverFrom(espn, earlier));
  REQUIRE(r.session->replay());
  CHECK(r.session->replay()->from == earlier);
  // A channel with no archive: live, as a tune.
  REQUIRE(r.session->startOverFrom(cnn, start));
  r.run(250);
  CHECK_FALSE(r.session->replay());
  CHECK(r.session->entry() == cnn);
  CHECK_FALSE(isReplay(r.player->plays.back()));
  r.noLinks();
}
