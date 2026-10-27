// The playback session on the pretend TV: a fake video player whose flags
// the test sets, over a store loaded with a made-up playlist.
#include <doctest.h>

#include <memory>
#include <optional>
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
    PlayerState state;
    bool canRetry = false;
    int retries = 0, stops = 0;
  };
  explicit FakePlayer(std::shared_ptr<Shared> s) : s_(std::move(s)) {}
  void attempt() override { s_->state = {}; }
  void play(const Feed& f, bool) override {
    s_->plays.push_back(f.channelId);
    if (s_->dead.count(f.channelId)) s_->state.failed = true;
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

  // The backup's picture comes up full screen: the corner card says which
  // feed stopped and which took over.
  r.picture();
  REQUIRE(r.session->backupNotice());
  CHECK(r.session->backupNotice()->from == 0);
  CHECK(r.session->backupNotice()->to == 1);
  CHECK(r.toasts.empty());
}

TEST_CASE("session: a backup taking over after a picture shows the corner card") {
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
