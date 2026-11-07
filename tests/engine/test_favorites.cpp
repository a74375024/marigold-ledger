// Favourites (Build 149): any number of channels, each can be unfavourited
// again, through a lineup rebuild (launch, a channel list read, a My Lineup
// edit) and a restart. My Lineup's build takes the local favourites in (a
// favourite always joins My Lineup); that must not make them stick.
#include <doctest.h>

#include <string>
#include <vector>

#include "core/catalog/lineup.hpp"
#include "core/engine/store.hpp"
#include "engine/loaded_store.hpp"
#include "sources/mock_provider.hpp"

using namespace marigold;
using namespace marigold::engine;

namespace {

bool fav(const Store& store, const std::string& id) {
  const GuideChannel* g = store.channels()->find(id);
  REQUIRE(g);
  return g->favorite;
}

size_t favCount(const Store& store) { return store.channels()->favorites.size(); }

// A lineup rebuild, as a launch, a channel list read or a My Lineup edit makes.
void rebuild(Store& store, fake::Tv& tv) {
  store.buildLineup(store.lineupPrefs());
  fake::settle(store, tv);
}

}  // namespace

TEST_CASE("favourites: many channels, each one can be removed again, through lineup rebuilds") {
  fake::Tv tv;
  mock::Provider provider(400);
  tv.pool.answer(fake::kPlaylist, provider.m3u());
  Store store(tv.host());
  REQUIRE(fake::addPlaylist(store, tv).empty());
  catalog::LineupPrefs p = store.lineupPrefs();
  p.configured = true;
  store.buildLineup(p);
  fake::settle(store, tv);
  auto ch = store.channels();
  REQUIRE(ch->all.size() > 20);
  std::vector<std::string> ids;
  for (size_t i = 0; i < 5; ++i) ids.push_back(ch->all[i * 3].id);
  REQUIRE(favCount(store) == 0);

  // Five favourites, one at a time, with a rebuild between each.
  for (size_t i = 0; i < ids.size(); ++i) {
    store.toggleFavorite(ids[i]);
    fake::settle(store, tv);
    CHECK(fav(store, ids[i]));
    rebuild(store, tv);
    CHECK(favCount(store) == i + 1);
    for (size_t k = 0; k <= i; ++k) CHECK(fav(store, ids[k]));
  }

  // Unfavourite the first: gone at once, and after a rebuild.
  store.toggleFavorite(ids[0]);
  fake::settle(store, tv);
  CHECK_FALSE(fav(store, ids[0]));
  CHECK(favCount(store) == 4);
  rebuild(store, tv);
  CHECK_FALSE(fav(store, ids[0]));
  CHECK(favCount(store) == 4);

  // Unfavourite them all.
  for (size_t i = 1; i < ids.size(); ++i) {
    store.toggleFavorite(ids[i]);
    fake::settle(store, tv);
    CHECK_FALSE(fav(store, ids[i]));
  }
  CHECK(favCount(store) == 0);
  rebuild(store, tv);
  CHECK(favCount(store) == 0);

  // Favourite again after unfavouriting.
  store.toggleFavorite(ids[2]);
  fake::settle(store, tv);
  CHECK(fav(store, ids[2]));
  CHECK(favCount(store) == 1);
}

TEST_CASE("favourites: kept across a restart, and removable after it") {
  fake::Tv tv;
  mock::Provider provider(400);
  tv.pool.answer(fake::kPlaylist, provider.m3u());
  std::vector<std::string> ids;
  {
    Store store(tv.host());
    REQUIRE(fake::addPlaylist(store, tv).empty());
    catalog::LineupPrefs p = store.lineupPrefs();
    p.configured = true;
    store.buildLineup(p);
    fake::settle(store, tv);
    auto ch = store.channels();
    for (size_t i = 0; i < 3; ++i) ids.push_back(ch->all[i * 4].id);
    for (const std::string& id : ids) {
      store.toggleFavorite(id);
      fake::settle(store, tv);
    }
    CHECK(favCount(store) == 3);
  }
  Store again(tv.host());
  fake::settle(again, tv);
  REQUIRE(again.channels()->all.size() > 20);
  for (const std::string& id : ids) CHECK(fav(again, id));
  again.toggleFavorite(ids[1]);
  fake::settle(again, tv);
  CHECK_FALSE(fav(again, ids[1]));
  CHECK(fav(again, ids[0]));
  CHECK(fav(again, ids[2]));
  rebuild(again, tv);
  CHECK_FALSE(fav(again, ids[1]));
  CHECK(favCount(again) == 2);
}

TEST_CASE("favourites: a second one while the first is still being published") {
  fake::Tv tv;
  mock::Provider provider(400);
  tv.pool.answer(fake::kPlaylist, provider.m3u());
  Store store(tv.host());
  REQUIRE(fake::addPlaylist(store, tv).empty());
  catalog::LineupPrefs p = store.lineupPrefs();
  p.configured = true;
  store.buildLineup(p);
  fake::settle(store, tv);
  auto ch = store.channels();
  std::vector<std::string> ids;
  for (size_t i = 0; i < 4; ++i) ids.push_back(ch->all[i * 5].id);
  // Each toggle lands between the ticks of the one before (its patch on a
  // worker, not yet published).
  for (int gap = 0; gap < 4; ++gap) {
    for (const std::string& id : ids) {
      store.toggleFavorite(id);
      for (int k = 0; k < gap; ++k) {
        tv.ms += 16;
        store.tick(tv.ms);
      }
    }
    fake::settle(store, tv);
    INFO("gap " << gap);
    for (const std::string& id : ids) CHECK(fav(store, id) == (gap % 2 == 0));
    CHECK(favCount(store) == (gap % 2 == 0 ? ids.size() : 0));
  }
  // And with a worker's job half done: one pass of the pool between toggles.
  for (const std::string& id : ids) {
    store.toggleFavorite(id);
    tv.ms += 16;
    store.tick(tv.ms);
    tv.pool.drain();
  }
  fake::settle(store, tv);
  for (const std::string& id : ids) CHECK(fav(store, id));
  CHECK(favCount(store) == ids.size());
}

TEST_CASE("favourites: the store answers at once, before the rows are published (menus read it)") {
  fake::Tv tv;
  mock::Provider provider(400);
  tv.pool.answer(fake::kPlaylist, provider.m3u());
  Store store(tv.host());
  REQUIRE(fake::addPlaylist(store, tv).empty());
  auto ch = store.channels();
  const std::string a = ch->all[0].id, b = ch->all[3].id;
  CHECK_FALSE(store.isFavorite(a));
  store.toggleFavorite(a);
  // The hold menu redraws right after OK: it must say "Remove" now, so a
  // second OK doesn't undo the first.
  CHECK(store.isFavorite(a));
  CHECK_FALSE(store.channels()->find(a)->favorite);  // the rows follow on the next tick
  store.toggleFavorite(b);
  CHECK(store.isFavorite(b));
  CHECK(store.isFavorite(a));
  fake::settle(store, tv);
  CHECK(fav(store, a));
  CHECK(fav(store, b));
  store.toggleFavorite(a);
  CHECK_FALSE(store.isFavorite(a));
  fake::settle(store, tv);
  CHECK_FALSE(fav(store, a));
  CHECK(fav(store, b));
}
