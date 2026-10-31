// The playback session on the pretend TV: a fake video player whose flags
// the test sets, over a store loaded with a made-up playlist.
#include <doctest.h>

#include <memory>
#include <optional>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/engine/session.hpp"
#include "engine/loaded_store.hpp"
#include "sources/mock_provider.hpp"

using marigold::engine::Feed;
using marigold::engine::PlayerState;
using marigold::engine::Session;
using marigold::engine::Store;

namespace {

struct FakePlayer final : marigold::engine::Player {
  struct Shared {
    std::vector<std::string> plays;  // channel ids, in order
    std::set<std::string> dead;      // feeds that fail as soon as they start
    std::map<std::string, std::string> errors;  // feeds that fail with this message
    PlayerState state;
    // What every new attempt starts as (a stream with no picture...).
    PlayerState onPlay;
    // Connections to the source the relay closed, handed over once.
    std::vector<SourceClosed> closed;
    bool canRetry = false;
    int retries = 0, stops = 0;
  };
  explicit FakePlayer(std::shared_ptr<Shared> s) : s_(std::move(s)) {}
  void attempt() override { s_->state = s_->onPlay; }
  std::vector<SourceClosed> sourceClosed() override {
    std::vector<SourceClosed> out;
    out.swap(s_->closed);
    return out;
  }
  void play(const Feed& f, bool) override {
    s_->plays.push_back(f.channelId);
    if (s_->dead.count(f.channelId)) s_->state.failed = true;
    if (auto e = s_->errors.find(f.channelId); e != s_->errors.end()) {
      s_->state.failed = true;
      s_->state.error = e->second;
    }
  }
  std::optional<Retry> retryAnotherWay() override {
    if (!s_->canRetry) return std::nullopt;
    s_->canRetry = false;
    ++s_->retries;
    s_->state = {};
    return Retry{"retried", "another way"};
  }
  void stop() override { ++s_->stops; }
  PlayerState state() const override { return s_->state; }
  std::shared_ptr<Shared> s_;
};

struct Rig {
  fake::Tv tv;
  mock::Provider provider{200};
  std::unique_ptr<Store> store;
  std::shared_ptr<FakePlayer::Shared> player = std::make_shared<FakePlayer::Shared>();
  std::vector<std::string> toasts;
  std::unique_ptr<Session> session;

  Rig() {
    tv.pool.answer(fake::kPlaylist, provider.m3u());
    store = std::make_unique<Store>(tv.host());
    REQUIRE(fake::addPlaylist(*store, tv).empty());
    Session::Hooks hooks;
    hooks.toast = [this](const std::string& t) { toasts.push_back(t); };
    session = std::make_unique<Session>(*store, tv.host(), std::make_unique<FakePlayer>(player), hooks);
  }

  // Moves the clock on in steps, ticking the session like the app's loop.
  void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 250) {
      tv.ms += 250;
      session->tick(tv.ms);
      tv.pool.drain();
    }
  }
  void picture() {
    player->state.picture = true;
    run(500);
  }
  // Runs until the session is on feed `index` (at most two minutes).
  bool runUntilFeed(size_t index) {
    for (int i = 0; i < 480 && session->feedIndex() != index; ++i) run(250);
    run(500);  // the new feed is handed to the player
    return session->feedIndex() == index;
  }

  // Log lines of this kind, newest last.
  std::vector<std::string> logged(const std::string& event) const {
    std::vector<std::string> out;
    for (const auto& e : session->log().entries())
      if (e.event == event) out.push_back(e.detail);
    return out;
  }

  // Entries with at least `feeds` feeds, in guide order.
  std::vector<std::string> entries(size_t feeds) const {
    std::vector<std::string> out;
    for (const auto& g : store->channels()->all)
      if (store->feeds(g.id).size() >= feeds) out.push_back(g.id);
    return out;
  }
};

}  // namespace

TEST_CASE("session: a tune plays the first feed and is watched once full screen") {
  Rig r;
  auto ids = r.entries(1);
  REQUIRE(ids.size() >= 2);
  REQUIRE(r.session->tune(ids[0]));
  r.run(500);  // a tune settles first (a burst of CH+ only plays the last)
  REQUIRE(r.player->plays.size() == 1);
  CHECK(r.player->plays[0] == r.store->feeds(ids[0])[0].channelId);
  CHECK(r.session->active());
  r.picture();
  CHECK(r.session->ready());

  // A preview is not watched: no previous channel yet.
  REQUIRE(r.session->tune(ids[1]));
  CHECK(r.session->previousEntry().empty());
  r.session->setShowingFull(true);
  REQUIRE(r.session->tune(ids[0]));
  CHECK(r.session->previousEntry() == ids[1]);
  CHECK(r.session->previous() == ids[1]);
  CHECK(r.session->entry() == ids[1]);
  // Tuning what is already playing does not start it again.
  size_t plays = r.player->plays.size();
  REQUIRE(r.session->tune(ids[1]));
  CHECK(r.player->plays.size() == plays);
}

TEST_CASE("session: no picture in 20 seconds moves to the next feed and says why") {
  Rig r;
  auto ids = r.entries(2);
  REQUIRE(!ids.empty());
  auto feeds = r.store->feeds(ids[0]);
  r.session->setShowingFull(true);
  REQUIRE(r.session->tune(ids[0]));
  // A provider stream is tried again twice before the next feed (tvOS rule).
  REQUIRE(r.runUntilFeed(1));
  REQUIRE(r.player->plays.size() == 4);
  CHECK(r.player->plays[0] == feeds[0].channelId);
  CHECK(r.player->plays[2] == feeds[0].channelId);
  CHECK(r.player->plays[3] == feeds[1].channelId);
  auto problem = r.session->feedProblem(feeds[0].channelId);
  REQUIRE(problem);
  CHECK(problem->why == "no picture after 20 s");

  // The backup's picture comes up full screen: the notice says which feed
  // stopped and which took over (the player drops it on a first start), and
  // no toast.
  r.picture();
  REQUIRE(r.session->backupNotice());
  CHECK(r.session->backupNotice()->from == 0);
  CHECK(r.session->backupNotice()->to == 1);
  CHECK(r.toasts.empty());
}

TEST_CASE("session: a backup taking over after a picture is noticed (\"Back on\" in the banner)") {
  Rig r;
  auto ids = r.entries(2);
  REQUIRE(!ids.empty());
  r.session->setShowingFull(true);
  REQUIRE(r.session->tune(ids[0]));
  r.picture();
  r.run(5000);
  // The stream errors and never comes back.
  r.player->dead.insert(r.store->feeds(ids[0])[0].channelId);
  r.player->state = {};
  r.player->state.failed = true;
  REQUIRE(r.runUntilFeed(1));
  r.picture();
  REQUIRE(r.session->backupNotice());
  CHECK(r.session->backupNotice()->from == 0);
  CHECK(r.session->backupNotice()->to == r.session->feedIndex());
}

TEST_CASE("session: channel keys can look ahead without tuning") {
  Rig r;
  auto ids = r.entries(1);
  REQUIRE(ids.size() >= 3);
  REQUIRE(r.session->tune(ids[1], ids));
  r.run(500);
  size_t plays = r.player->plays.size();
  CHECK(r.session->flipTarget("", 1) == ids[2]);
  CHECK(r.session->flipTarget("", -1) == ids[0]);
  CHECK(r.session->flipTarget(ids[2], -1) == ids[1]);
  CHECK(r.session->flipTarget(ids[0], -1) == ids.back());  // round the list
  r.run(1000);
  CHECK(r.player->plays.size() == plays);
  CHECK(r.session->entry() == ids[1]);
  CHECK(r.session->flip(1) == ids[2]);
  CHECK(r.session->entry() == ids[2]);
}

TEST_CASE("session: the platform's other way is tried before the next feed") {
  Rig r;
  auto ids = r.entries(2);
  REQUIRE(!ids.empty());
  REQUIRE(r.session->tune(ids[0]));
  r.player->canRetry = true;
  r.player->state.failed = true;
  r.run(500);
  CHECK(r.player->retries == 1);
  CHECK(r.session->feedIndex() == 0);
  bool logged = false;
  for (const auto& e : r.session->log().entries())
    if (e.event == "retried") logged = true;
  CHECK(logged);
}

TEST_CASE("session: CH+ and CH- wrap around the flip list") {
  Rig r;
  auto ids = r.entries(1);
  REQUIRE(ids.size() >= 3);
  std::vector<std::string> flip(ids.begin(), ids.begin() + 3);
  REQUIRE(r.session->tune(flip[0], flip));
  CHECK(r.session->flip(-1) == flip[2]);
  CHECK(r.session->flip(+1) == flip[0]);
  CHECK(r.session->flip(+1) == flip[1]);
}

TEST_CASE("session: the playback log is saved and read back") {
  Rig r;
  auto ids = r.entries(1);
  REQUIRE(!ids.empty());
  REQUIRE(r.session->tune(ids[0]));
  r.run(11000);
  r.session->stop();
  CHECK(r.player->stops >= 1);
  r.run(11000);  // saved every 10 s
  REQUIRE(r.tv.files.read("playback-log.json"));
  Session again(*r.store, r.tv.host(), std::make_unique<FakePlayer>(r.player), {});
  CHECK(again.log().entries().size() == r.session->log().entries().size());
}

TEST_CASE("session: the feed that played last starts first next time, unless one is pinned") {
  Rig r;
  auto ids = r.entries(2);
  REQUIRE(ids.size() >= 2);
  auto feeds = r.store->feeds(ids[0]);
  r.session->setShowingFull(true);
  REQUIRE(r.session->tune(ids[0]));
  r.run(500);
  // The viewer picks the second feed and it plays.
  r.session->useFeed(1);
  r.run(500);
  r.picture();
  r.run(60'000);
  // Another channel, then back: the second feed starts.
  REQUIRE(r.session->tune(ids[1]));
  r.run(1000);
  REQUIRE(r.session->tune(ids[0]));
  r.run(500);
  CHECK(r.player->plays.back() == feeds[1].channelId);
  CHECK(r.store->feeds(ids[0])[0].channelId == feeds[1].channelId);
  // A pinned feed still wins.
  r.store->pinFeed(ids[0], feeds[0].channelId);
  CHECK(r.store->feeds(ids[0])[0].channelId == feeds[0].channelId);
}

TEST_CASE("session: a version this TV can't play is left at once and tried last next time") {
  Rig r;
  auto ids = r.entries(2);
  REQUIRE_FALSE(ids.empty());
  auto feeds = r.store->feeds(ids[0]);
  REQUIRE(feeds.size() >= 2);
  r.player->errors[feeds[0].channelId] = "601 Resource Allocation Error";
  r.player->dead.insert(feeds[1].channelId);
  r.session->setShowingFull(true);
  REQUIRE(r.session->tune(ids[0]));
  r.run(500);
  // Straight on to the next version: no same-version retries, no grace.
  REQUIRE(r.runUntilFeed(1));
  CHECK(std::count(r.player->plays.begin(), r.player->plays.end(), feeds[0].channelId) == 1);
  auto problem = r.session->feedProblem(feeds[0].channelId);
  REQUIRE(problem);
  CHECK(problem->why == "this TV can't play it");
  r.run(120'000);
  // Next time it comes after the channel's other versions...
  auto again = r.store->feeds(ids[0]);
  CHECK(again.front().channelId != feeds[0].channelId);
  CHECK(again.back().channelId == feeds[0].channelId);
  // ...unless the viewer pinned it.
  r.store->pinFeed(ids[0], feeds[0].channelId);
  CHECK(r.store->feeds(ids[0]).front().channelId == feeds[0].channelId);
  r.store->pinFeed(ids[0], "");
  // Once it plays, it's back in its place.
  r.store->recordFeed(feeds[0].channelId, true);
  CHECK(r.store->feeds(ids[0]).back().channelId != feeds[0].channelId);
}

TEST_CASE("session: Back to is the last channel stayed on, and previous() swaps the two") {
  Rig r;
  auto ids = r.entries(1);
  REQUIRE(ids.size() >= 4);
  r.session->setShowingFull(true);
  REQUIRE(r.session->tune(ids[0]));
  r.run(11000);  // watched for a while
  REQUIRE(r.session->tune(ids[1]));
  CHECK(r.session->previousEntry() == ids[0]);
  r.run(2000);   // a quick flip past ids[1]...
  REQUIRE(r.session->tune(ids[2]));
  r.run(2000);   // ...and ids[2]
  REQUIRE(r.session->tune(ids[3]));
  CHECK(r.session->previousEntry() == ids[0]);  // still the game you started from
  CHECK(r.session->previous() == ids[0]);
  CHECK(r.session->previousEntry() == ids[3]);  // bouncing swaps at once
  r.run(1000);
  CHECK(r.session->previous() == ids[3]);
  CHECK(r.session->previousEntry() == ids[0]);
}

// --- the picture check (Build 115) -------------------------------------------

TEST_CASE("session: a black picture on the relay gets one reconnect, then the next version") {
  Rig r;
  auto ids = r.entries(2);
  REQUIRE_FALSE(ids.empty());
  auto feeds = r.store->feeds(ids[0]);
  PlayerState black;
  black.picture = true;  // the decoder reported in; the frames are black
  black.playing = true;
  black.streamChecked = true;
  black.stream = marigold::media::Verdict::Black;
  black.streamDetail = "I 432 B, 16 kbps";
  r.player->onPlay = black;
  r.session->setShowingFull(true);
  REQUIRE(r.session->tune(ids[0]));
  r.run(500);
  REQUIRE(r.player->plays.size() == 1);
  r.run(7000);
  CHECK(r.player->plays.size() == 1);
  // 8 s of black: the same version again, once...
  r.run(3000);
  REQUIRE(r.player->plays.size() == 2);
  CHECK(r.player->plays[1] == feeds[0].channelId);
  // ...then the next one.
  REQUIRE(r.runUntilFeed(1));
  CHECK(r.player->plays[2] == feeds[1].channelId);
  auto problem = r.session->feedProblem(feeds[0].channelId);
  REQUIRE(problem);
  CHECK(problem->why == "black picture");
  auto checks = r.logged("picture check");
  REQUIRE_FALSE(checks.empty());
  CHECK(checks.front() == "black 8 s; I 432 B, 16 kbps");
}

TEST_CASE("session: sound without a frame drawn is a failed start with one retry") {
  Rig r;
  auto ids = r.entries(2);
  REQUIRE_FALSE(ids.empty());
  auto feeds = r.store->feeds(ids[0]);
  PlayerState sound;
  sound.playing = true;  // uMS "playing", and not a frame
  r.player->onPlay = sound;
  REQUIRE(r.session->tune(ids[0]));
  r.run(8500);
  CHECK_FALSE(r.session->ready());
  REQUIRE(r.runUntilFeed(1));
  REQUIRE(r.player->plays.size() == 3);  // one retry, then the next version
  CHECK(r.player->plays[1] == feeds[0].channelId);
  CHECK(r.session->feedProblem(feeds[0].channelId)->why == "no picture");
}

TEST_CASE("session: frames the relay sees fine while the TV shows none never flip") {
  Rig r;
  auto ids = r.entries(2);
  REQUIRE_FALSE(ids.empty());
  PlayerState shown;
  shown.playing = true;
  shown.streamChecked = true;
  shown.stream = marigold::media::Verdict::Picture;
  r.player->onPlay = shown;
  REQUIRE(r.session->tune(ids[0]));
  r.run(60'000);
  CHECK(r.session->ready());
  CHECK(r.player->plays.size() == 1);
}

TEST_CASE("session: the relay's pictures outweigh a player that lists no video track") {
  Rig r;
  auto ids = r.entries(2);
  REQUIRE_FALSE(ids.empty());
  PlayerState shown;
  shown.playing = true;
  shown.videoTracks = 0;  // a track list read wrong
  shown.streamChecked = true;
  shown.stream = marigold::media::Verdict::Picture;
  r.player->onPlay = shown;
  REQUIRE(r.session->tune(ids[0]));
  r.run(60'000);
  CHECK(r.session->ready());
  CHECK(r.player->plays.size() == 1);
}

TEST_CASE("session: no picture anywhere asks, and Keep listening plays the sound unchecked") {
  Rig r;
  auto ids = r.entries(2);
  REQUIRE_FALSE(ids.empty());
  PlayerState audio;
  audio.playing = true;
  audio.videoTracks = 0;  // the player lists no video
  r.player->onPlay = audio;
  r.session->setShowingFull(true);
  REQUIRE(r.session->tune(ids[0]));
  for (int i = 0; i < 400 && !r.session->noPicture(); ++i) r.run(250);
  REQUIRE(r.session->noPicture());
  CHECK_FALSE(r.session->failed());
  CHECK(r.session->ready());  // the sound plays on
  CHECK(r.session->noPictureMessage().find("The sound plays on") != std::string::npos);
  size_t plays = r.player->plays.size();
  CHECK(plays == r.store->feeds(ids[0]).size());  // each version once, no retries
  r.run(30'000);
  CHECK(r.player->plays.size() == plays);
  r.session->keepListening();
  CHECK_FALSE(r.session->noPicture());
  CHECK(r.store->audioOnly(ids[0]));
  CHECK_FALSE(r.logged("keep listening").empty());

  // Next time: sound only, not checked.
  r.session->stop();
  REQUIRE(r.session->tune(ids[0]));
  r.run(60'000);
  CHECK(r.session->ready());
  CHECK_FALSE(r.session->noPicture());
  CHECK(r.player->plays.size() == plays + 1);
}

TEST_CASE("session: a source that keeps closing moves to the next version, no retries") {
  Rig r;
  auto ids = r.entries(2);
  REQUIRE_FALSE(ids.empty());
  auto feeds = r.store->feeds(ids[0]);
  REQUIRE(r.session->tune(ids[0]));
  r.run(500);
  r.picture();
  for (int i = 0; i < 3; ++i) {
    r.run(2500);
    r.player->closed.push_back({2400, 310 * 1024, true});
  }
  REQUIRE(r.runUntilFeed(1));
  CHECK(r.player->plays.size() == 2);
  CHECK(r.session->feedProblem(feeds[0].channelId)->why == "the source keeps closing");
  CHECK(r.logged("relay").size() == 3);
  CHECK(r.logged("relay").front() == "closed after 2 s, 310 KB; reopened it (1 time)");
  REQUIRE(r.logged("source keeps closing").size() == 1);
}

TEST_CASE("session: what plays is logged once, with the picture") {
  Rig r;
  auto ids = r.entries(1);
  REQUIRE_FALSE(ids.empty());
  REQUIRE(r.session->tune(ids[0]));
  r.run(500);
  r.player->state.facts = "1920x1080 59.94p \xc2\xb7 h264 High 8-bit \xc2\xb7 aac";
  r.run(2000);
  CHECK(r.logged("video").empty());  // no picture yet
  r.picture();
  r.run(5000);
  auto video = r.logged("video");
  REQUIRE(video.size() == 1);
  CHECK(video[0] == "1920x1080 59.94p \xc2\xb7 h264 High 8-bit \xc2\xb7 aac");
}
