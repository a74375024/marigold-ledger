// The engine (channels, guide, Live Events, settings) run on a pretend TV:
// the same Store the webOS app uses, with canned network answers. These are
// the checks a new platform (Android, tvOS) would otherwise only find on a
// real device.
#include <doctest.h>

#include <memory>
#include <string>

#include "core/catalog/lineup.hpp"
#include "core/engine/store.hpp"
#include "engine/loaded_store.hpp"
#include "sources/mock_provider.hpp"

using marigold::engine::AccountForm;
using marigold::engine::Store;
using marigold::engine::StoreStatus;

using fake::addPlaylist;
using fake::kPlaylist;
using fake::settle;

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

TEST_CASE("engine: an M3U account's guide link is read, and again after the next launch") {
  constexpr const char* kList = "http://mock.example.invalid:8080/harbor.m3u";
  constexpr const char* kGuide = "http://mock.example.invalid:8080/harbor.xml";
  fake::Tv tv;
  tv.pool.answer(kList,
                 "#EXTM3U\n"
                 "#EXTINF:-1 tvg-id=\"harbor.news\" group-title=\"Harbor TV\",Harbor News HD\n"
                 "http://mock.example.invalid:8080/a.m3u8\n"
                 "#EXTINF:-1 tvg-id=\"valley.sports\" group-title=\"Harbor TV\",Valley Sports\n"
                 "http://mock.example.invalid:8080/b.ts\n");
  {
    Store store(tv.host());
    AccountForm form = fake::m3uForm();
    form.server = kList;
    form.guideUrl = kGuide;
    std::string problem = "(not answered)";
    store.addAccount(form, [&](std::string p) { problem = std::move(p); });
    settle(store, tv);
    settle(store, tv);
    CHECK(problem.empty());
    CHECK(tv.pool.asked[kGuide] == 1);
  }
  // The guide didn't answer: the next launch, from the saved list, tries again.
  Store again(tv.host());
  settle(again, tv);
  settle(again, tv);
  CHECK(tv.pool.asked[kGuide] == 2);
}

TEST_CASE("engine: a channel's East and West versions are set one by one, side by side") {
  constexpr const char* kList = "http://mock.example.invalid:8080/coasts.m3u";
  fake::Tv tv;
  tv.pool.answer(kList,
                 "#EXTM3U\n"
                 "#EXTINF:-1 group-title=\"US Movies\",US: HBO\n"
                 "http://mock.example.invalid:8080/1.ts\n"
                 "#EXTINF:-1 group-title=\"US Movies\",US: HBO 2\n"
                 "http://mock.example.invalid:8080/2.ts\n"
                 "#EXTINF:-1 group-title=\"US Movies\",US: HBO West\n"
                 "http://mock.example.invalid:8080/3.ts\n"
                 "#EXTINF:-1 group-title=\"US News\",US: CNN\n"
                 "http://mock.example.invalid:8080/4.ts\n");
  Store store(tv.host());
  AccountForm form = fake::m3uForm();
  form.server = kList;
  store.addAccount(form, [](std::string) {});
  settle(store, tv);
  auto ch = store.channels();
  REQUIRE(ch);
  auto named = [&](const std::string& name) -> const marigold::engine::GuideChannel* {
    for (const auto& g : store.channels()->all)
      if (g.name == name) return &g;
    return nullptr;
  };
  const auto* east = named("HBO");
  const auto* west = named("HBO West");
  REQUIRE(east);
  REQUIRE(west);
  CHECK(east->partner == west->id);
  CHECK(west->partner == east->id);
  CHECK(west->west);
  REQUIRE(named("CNN"));
  CHECK(named("CNN")->partner.empty());
  CHECK(store.coastPairs() == std::vector<std::string>{east->id});
  // The West version right after the East one, before HBO 2.
  int e = ch->byId.at(east->id), w = ch->byId.at(west->id);
  CHECK(w == e + 1);

  std::string eastId = east->id, westId = west->id;
  store.setChannelCoast(westId, marigold::catalog::Coast::both);
  settle(store, tv);
  CHECK(store.channelCoast(eastId) == marigold::catalog::Coast::both);
  CHECK(store.channels()->find(eastId)->inLineup);
  CHECK(store.channels()->find(westId)->inLineup);
  store.setChannelCoast(eastId, marigold::catalog::Coast::west);
  settle(store, tv);
  CHECK(store.channelCoast(westId) == marigold::catalog::Coast::west);
  CHECK_FALSE(store.channels()->find(eastId)->inLineup);
  CHECK(store.channels()->find(westId)->inLineup);
  // Adding the other one back by hand makes it both.
  store.toggleLineup(eastId);
  settle(store, tv);
  CHECK(store.channelCoast(eastId) == marigold::catalog::Coast::both);
}
