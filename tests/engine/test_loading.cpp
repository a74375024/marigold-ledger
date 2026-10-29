// Loading the channel list without a wait screen: the guide header says
// when the list was updated (or couldn't be), a list an older version saved
// opens the guide until it's sorted again, and the sort reports how far it
// has got.
#include <doctest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "catalog/helpers.hpp"
#include "core/catalog/cache.hpp"
#include "core/catalog/catalog.hpp"
#include "core/engine/store.hpp"
#include "engine/loaded_store.hpp"
#include "sources/mock_provider.hpp"

using marigold::engine::LoadProgress;
using marigold::engine::Store;
using marigold::engine::StoreStatus;
using fake::addPlaylist;
using fake::kPlaylist;
using fake::settle;

namespace {

// The queued work done, then one frame.
void frame(Store& store, fake::Tv& tv, uint32_t ms = 100) {
  tv.pool.drain();
  tv.ms += ms;
  store.tick(tv.ms);
}

std::vector<std::string> savedLists(const fake::Tv& tv) {
  std::vector<std::string> names;
  for (const auto& f : std::filesystem::directory_iterator(tv.files.dir())) {
    std::string name = f.path().filename().string();
    if (name.rfind("catalog-", 0) == 0) names.push_back(name);
  }
  return names;
}

}  // namespace

TEST_CASE("loading: a changed list sorts in the background, then the header says it's updated") {
  fake::Tv tv;
  mock::Provider before(200), after(260);
  tv.pool.answer(kPlaylist, before.m3u());
  Store store(tv.host());
  REQUIRE(addPlaylist(store, tv).empty());
  CHECK(store.status().mark == StoreStatus::Mark::None);
  CHECK(store.loadProgress().stage == LoadProgress::Stage::Done);
  CHECK(store.status().received == 0);

  tv.pool.answer(kPlaylist, after.m3u());
  store.refreshAll();
  tv.ms += 100;
  store.tick(tv.ms);
  CHECK(store.status().mark == StoreStatus::Mark::Busy);
  CHECK(store.status().line == "Updating your channel list\xe2\x80\xa6");
  frame(store, tv);
  CHECK(store.status().mark == StoreStatus::Mark::Done);
  CHECK(store.status().line == "Channel list updated");
  // A few seconds, then it goes.
  frame(store, tv, 2800);
  CHECK(store.status().mark == StoreStatus::Mark::Done);
  frame(store, tv, 300);
  CHECK(store.status().mark == StoreStatus::Mark::None);
  CHECK(store.status().line.empty());
}

TEST_CASE("loading: an unchanged list says nothing when it's done") {
  fake::Tv tv;
  mock::Provider provider(200);
  tv.pool.answer(kPlaylist, provider.m3u());
  Store store(tv.host());
  REQUIRE(addPlaylist(store, tv).empty());
  store.refreshAll();
  frame(store, tv);
  frame(store, tv);
  CHECK(store.status().mark == StoreStatus::Mark::None);
}

TEST_CASE("loading: a list that couldn't be updated stays, and the header says so for a few seconds") {
  fake::Tv tv;
  mock::Provider provider(200);
  tv.pool.answer(kPlaylist, provider.m3u());
  Store store(tv.host());
  REQUIRE(addPlaylist(store, tv).empty());
  auto shown = store.channels();
  tv.pool.answer(kPlaylist, "down", 503);
  store.refreshAll();
  frame(store, tv);
  frame(store, tv);
  CHECK(store.status().mark == StoreStatus::Mark::Failed);
  CHECK(store.status().line.find("Couldn't update the channel list") == 0);
  CHECK(store.channels().get() == shown.get());
  frame(store, tv, 4100);
  CHECK(store.status().mark == StoreStatus::Mark::None);
}

TEST_CASE("loading: a list an older version saved opens the guide, then is sorted again") {
  fake::Tv tv;
  mock::Provider provider(200);
  tv.pool.answer(kPlaylist, provider.m3u());
  {
    Store store(tv.host());
    REQUIRE(addPlaylist(store, tv).empty());
  }
  // As the previous version left them.
  std::string now = "\"version\":" + std::to_string(marigold::catalog::cache::kVersion);
  std::string older = "\"version\":" + std::to_string(marigold::catalog::cache::kVersion - 1);
  std::vector<std::string> lists = savedLists(tv);
  REQUIRE(lists.size() == 2);
  for (const std::string& name : lists) {
    std::string text = tv.files.read(name).value_or("");
    REQUIRE(text.find(now) != std::string::npos);
    text.replace(text.find(now), now.size(), older);
    tv.files.write(name, text, false);
  }
  // The provider down at first: the guide has only the old list to show.
  tv.pool.answer(kPlaylist, "down", 503);
  Store again(tv.host());
  settle(again, tv);
  CHECK(again.status().phase == StoreStatus::Phase::Ready);
  REQUIRE(again.channels());
  CHECK(again.channels()->all.size() > 50);
  for (const std::string& name : savedLists(tv)) CHECK(tv.files.read(name).value_or("").find(older) != std::string::npos);
  // Then the same download as before, sorted again by this version (not
  // "unchanged") and saved as its own.
  tv.pool.answer(kPlaylist, provider.m3u());
  again.refreshAll();
  settle(again, tv);
  for (const std::string& name : savedLists(tv)) CHECK(tv.files.read(name).value_or("").find(now) != std::string::npos);
}

TEST_CASE("loading: the sort says how far it has got") {
  using namespace marigold;
  std::vector<Channel> list;
  for (int i = 0; i < 600; ++i) list.push_back(testdata::channel(std::to_string(i), "US: Channel " + std::to_string(i)));
  catalog::BuildProgress progress;
  CHECK(progress.pass.load() == catalog::BuildProgress::notStarted);
  catalog::build(list, testdata::utc(2026, 10, 29, 12, 0), {}, nullptr, &progress);
  CHECK(progress.pass.load() == catalog::BuildProgress::finished);
}
