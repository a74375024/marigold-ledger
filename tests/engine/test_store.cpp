// The engine (channels, guide, Live Events, settings) run on a pretend TV:
// the same Store the webOS app uses, with canned network answers. These are
// the checks a new platform (Android, tvOS) would otherwise only find on a
// real device.
#include <doctest.h>

#include <memory>
#include <string>

#include "core/engine/store.hpp"
#include "engine/fake_host.hpp"
#include "sources/mock_provider.hpp"

using marigold::engine::AccountForm;
using marigold::engine::Store;
using marigold::engine::StoreStatus;

namespace {

constexpr const char* kPlaylist = "http://mock.example.invalid:8080/playlist.m3u";

AccountForm m3uForm() {
  AccountForm f;
  f.kind = AccountForm::Kind::M3U;
  f.name = "Test playlist";
  f.server = kPlaylist;
  return f;
}

// Lets everything settle: queued work runs, then the engine ticks as the
// app's frame loop would, ten seconds on (past every short delay).
void settle(Store& store, fake::Tv& tv) {
  for (int i = 0; i < 100; ++i) {
    tv.pool.drain();
    tv.ms += 100;
    store.tick(tv.ms);
  }
  tv.pool.drain();
}

// Adds the playlist account and lets everything it starts settle.
std::string addPlaylist(Store& store, fake::Tv& tv) {
  std::string problem = "(not answered)";
  store.addAccount(m3uForm(), [&](std::string p) { problem = std::move(p); });
  settle(store, tv);
  return problem;
}

}  // namespace

TEST_CASE("engine: an M3U account loads its channels") {
  fake::Tv tv;
  mock::Provider provider(200);
  tv.pool.answer(kPlaylist, provider.m3u());
  Store store(tv.host());
  CHECK(store.status().phase == StoreStatus::Phase::NoAccounts);

  CHECK(addPlaylist(store, tv).empty());
  CHECK(store.status().phase == StoreStatus::Phase::Ready);
  REQUIRE(store.channels());
  CHECK(store.channels()->all.size() > 50);
  REQUIRE(store.accounts().size() == 1);
  // The account is saved for the next launch.
  CHECK(tv.files.read("accounts.json"));
}

TEST_CASE("engine: an unchanged playlist is not sorted again") {
  fake::Tv tv;
  mock::Provider provider(200);
  tv.pool.answer(kPlaylist, provider.m3u());
  Store store(tv.host());
  REQUIRE(addPlaylist(store, tv).empty());
  auto first = store.channels();
  REQUIRE(first);
  int downloads = tv.pool.asked[kPlaylist];

  store.refreshAll();
  settle(store, tv);
  CHECK(tv.pool.asked[kPlaylist] > downloads);  // it did look again
  CHECK(store.channels().get() == first.get());  // and kept the same list
}

TEST_CASE("engine: a broken playlist link is refused without showing the link") {
  fake::Tv tv;
  tv.pool.answer(kPlaylist, "not found", 404);
  Store store(tv.host());
  std::string problem = addPlaylist(store, tv);
  CHECK(problem.find("404") != std::string::npos);
  CHECK(problem.find("example.invalid") == std::string::npos);
  CHECK(store.accounts().empty());
}

TEST_CASE("engine: a page that isn't a playlist is refused") {
  fake::Tv tv;
  tv.pool.answer(kPlaylist, "<html><body>Welcome</body></html>");
  Store store(tv.host());
  CHECK(addPlaylist(store, tv) == "That link isn't an M3U playlist");
  CHECK(store.accounts().empty());
}

TEST_CASE("engine: the next launch finds the saved account") {
  fake::Tv tv;
  mock::Provider provider(200);
  tv.pool.answer(kPlaylist, provider.m3u());
  {
    Store store(tv.host());
    REQUIRE(addPlaylist(store, tv).empty());
  }
  Store again(tv.host());
  REQUIRE(again.accounts().size() == 1);
  CHECK(again.accounts()[0].name == "Test playlist");
}

TEST_CASE("engine: a changed playlist is sorted again") {
  fake::Tv tv;
  mock::Provider before(200), after(260);
  tv.pool.answer(kPlaylist, before.m3u());
  Store store(tv.host());
  REQUIRE(addPlaylist(store, tv).empty());
  auto first = store.channels();
  REQUIRE(first);

  tv.pool.answer(kPlaylist, after.m3u());
  store.refreshAll();
  settle(store, tv);
  REQUIRE(store.channels());
  CHECK(store.channels().get() != first.get());
  CHECK(store.channels()->all.size() > first->all.size());
}
