// Search on the pretend TV: the index made from a loaded store, and recent
// searches saved by the names picked (never what was typed).
#include <doctest.h>

#include <string>

#include "core/engine/store.hpp"
#include "engine/loaded_store.hpp"
#include "sources/mock_provider.hpp"

using marigold::engine::Settings;
using marigold::engine::Store;
namespace se = marigold::engine::search;

TEST_CASE("engine: Search's index holds every channel, and is made again only when needed") {
  fake::Tv tv;
  mock::Provider provider(200);
  tv.pool.answer(fake::kPlaylist, provider.m3u());
  Store store(tv.host());
  REQUIRE(fake::addPlaylist(store, tv).empty());
  auto index = store.searchIndex();
  REQUIRE(index);
  CHECK(index->channels.size() == store.channels()->all.size());
  CHECK(store.searchIndex() == index);  // nothing changed: the same one
  // A channel is found by its own name.
  const auto& first = store.channels()->all.front();
  se::Options o;
  o.now = marigold::time::now();
  se::Results r = se::run(*index, se::parse(first.name), o);
  REQUIRE(r.top);
  CHECK(r.top->kind == se::Kind::Channel);
}

TEST_CASE("engine: recent searches are saved by name, eight kept, and the switch forgets them") {
  fake::Tv tv;
  {
    Store store(tv.host());
    CHECK(store.settings().recentSearches);  // on by default
    store.rememberSearch({"Sports Net 2", "c:sn2"});
    store.rememberSearch({"", "c:none"});  // nothing picked: not kept
    for (int i = 0; i < 10; ++i) store.rememberSearch({"Channel " + std::to_string(i), "c:" + std::to_string(i)});
    CHECK(store.recentSearches().size() == se::kRecentKept);
    std::string saved = tv.files.read("lists.json").value_or("");
    CHECK(saved.find("Channel 9") != std::string::npos);
    CHECK(saved.find("sn2\"") == std::string::npos);  // pushed out: only eight
  }
  Store again(tv.host());
  REQUIRE(again.recentSearches().size() == se::kRecentKept);
  CHECK(again.recentSearches().front().name == "Channel 9");
  again.forgetSearch("c:9");
  CHECK(again.recentSearches().front().name == "Channel 8");
  // Off forgets them, and keeps no more.
  Settings s = again.settings();
  s.recentSearches = false;
  again.setSettings(s);
  CHECK(again.recentSearches().empty());
  again.rememberSearch({"Goal TV", "c:goal"});
  CHECK(again.recentSearches().empty());
  Store third(tv.host());
  CHECK_FALSE(third.settings().recentSearches);
  CHECK(third.recentSearches().empty());
}
