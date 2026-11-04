// The first run's guide: listings for the rows the guide opens on once the
// lineup is made, a forced provider guide asked for while the first one
// downloads kept for after it, and a reload during a guide build that
// starts the builds again whatever the channel list turns out to be.
#include <doctest.h>

#include <cstdio>
#include <functional>
#include <vector>
#include <string>

#include "core/catalog/lineup.hpp"
#include "core/engine/store.hpp"
#include "core/guide/outside.hpp"
#include "core/sources/account.hpp"
#include "core/sources/xtream.hpp"
#include "core/util/time.hpp"
#include "engine/loaded_store.hpp"
#include "sources/mock_provider.hpp"

using namespace marigold;
using namespace marigold::engine;

namespace {

constexpr const char* kList = "http://mock.example.invalid:8080/harbor.m3u";
constexpr const char* kGuide = "http://mock.example.invalid:8080/harbor.xml";

// Two channels, one of them in the guide file.
void answerHarbor(fake::Tv& tv) {
  tv.pool.answer(kList,
                 "#EXTM3U\n"
                 "#EXTINF:-1 tvg-id=\"harbor.news\" group-title=\"Harbor TV\",Harbor News HD\n"
                 "http://mock.example.invalid:8080/a.m3u8\n"
                 "#EXTINF:-1 tvg-id=\"valley.sports\" group-title=\"Harbor TV\",Valley Sports\n"
                 "http://mock.example.invalid:8080/b.ts\n");
  std::string xml =
      "<?xml version=\"1.0\"?>\n<tv>\n<channel id=\"harbor.news\"><display-name>Harbor News</display-name></channel>\n";
  time::Seconds hour = time::now() / 3600 * 3600;
  auto stamp = [](time::Seconds t) {
    time::Civil c = time::civilFromSeconds(t);
    char b[32];
    std::snprintf(b, sizeof b, "%04d%02d%02d%02d%02d00 +0000", c.year, c.month, c.day, c.hour, c.minute);
    return std::string(b);
  };
  for (int i = -1; i < 24; ++i)
    xml += "<programme start=\"" + stamp(hour + i * 3600) + "\" stop=\"" + stamp(hour + (i + 1) * 3600) +
           "\" channel=\"harbor.news\"><title>Harbor Report " + std::to_string(i + 2) + "</title></programme>\n";
  xml += "</tv>\n";
  tv.pool.answer(kGuide, xml);
}

void addHarbor(Store& store, bool guideLink) {
  AccountForm form = fake::m3uForm();
  form.server = kList;
  if (guideLink) form.guideUrl = kGuide;
  store.addAccount(form, [](std::string) {});
}

// One loop pass at a time until `done` holds after a tick: what that tick
// started is queued, not run yet (a build under way).
bool stepUntil(Store& store, fake::Tv& tv, const std::function<bool()>& done) {
  for (int i = 0; i < 400; ++i) {
    tv.pool.drain();
    tv.ms += 100;
    store.tick(tv.ms);
    if (done()) return true;
  }
  return false;
}

bool providerRunning(const Store& store) { return store.guideListings().providerRunning; }
bool outsideRunning(const Store& store) {
  return store.guideListings().outsideLine.rfind("Updating outside listings", 0) == 0;
}

int outsideAsks(fake::Tv& tv) {
  int n = 0;
  for (const guide::Source& s : guide::sources()) n += tv.pool.asked[s.url];
  return n;
}

int shortEpgAsks(fake::Tv& tv) {
  int n = 0;
  for (const auto& [url, times] : tv.pool.asked)
    if (url.find("action=get_short_epg") != std::string::npos) n += times;
  return n;
}

}  // namespace

TEST_CASE("first run: the rows the guide opens on get their listings once the lineup is made") {
  fake::Tv tv;
  mock::Provider provider(300);
  sources::Account a;
  a.kind = sources::AccountKind::Xtream;
  a.serverUrl = std::string(mock::kHost);
  a.userName = std::string(mock::kUser);
  a.password = std::string(mock::kPass);
  auto client = sources::xtream::Client::make(a);
  REQUIRE(client);
  tv.pool.answer(client.value().accountInfoRequest().url, R"({"user_info":{"auth":1,"status":"Active"},"server_info":{}})");
  tv.pool.answer(client.value().categoriesRequest().url, provider.categoriesJson());
  tv.pool.answer(client.value().streamsRequest().url, provider.liveStreamsJson());
  Store store(tv.host());
  AccountForm form;
  form.kind = AccountForm::Kind::Xtream;
  form.name = "Mock";
  form.server = a.serverUrl;
  form.user = a.userName;
  form.password = a.password;
  store.addAccount(form, [](std::string) {});
  fake::settle(store, tv);
  auto ch = store.channels();
  REQUIRE(ch->all.size() > 20);
  REQUIRE(!ch->browse.empty());

  // Nothing on screen asked (no key pressed): the store asked for the
  // first rows itself once the lineup was in (the provider's full guide
  // can't be read here, so it covers none of them).
  int asked = shortEpgAsks(tv);
  CHECK(asked > 0);
  // The first row among them (its channel id ends in its stream id).
  const GuideChannel& first = ch->all[static_cast<size_t>(ch->browse.front())];
  std::vector<Feed> feeds = store.feeds(first.id);
  REQUIRE(!feeds.empty());
  bool firstAsked = false;
  for (const Feed& f : feeds) {
    std::string stream = f.channelId.substr(f.channelId.rfind('|') + 1);
    if (tv.pool.asked[client.value().shortEpgRequest(stream).url] > 0) firstAsked = true;
  }
  CHECK(firstAsked);

  // Once: settling on, the store asks nothing more by itself.
  fake::settle(store, tv);
  CHECK(shortEpgAsks(tv) == asked);
}

TEST_CASE("first run: a forced provider guide asked for while the first one downloads runs after it") {
  fake::Tv tv;
  answerHarbor(tv);
  Store store(tv.host());
  addHarbor(store, true);
  REQUIRE(stepUntil(store, tv, [&] { return providerRunning(store); }));
  CHECK(tv.pool.asked[kGuide] == 0);  // queued, not read yet

  // Countries chosen during setup: the provider guide is asked for again,
  // forced (same channels, so nothing else would build it).
  store.setLineupCountries({"US", "UK"});
  tv.ms += 100;
  store.tick(tv.ms);  // the forced one's turn comes while the first runs
  CHECK(providerRunning(store));
  fake::settle(store, tv);
  CHECK(tv.pool.asked[kGuide] == 2);  // the first, then the forced one
  CHECK_FALSE(providerRunning(store));
  REQUIRE(store.guideListings().providers.size() == 1);
  CHECK(store.guideListings().providers.front().line.rfind("Updated", 0) == 0);

  // Asked for once, built once.
  fake::settle(store, tv);
  CHECK(tv.pool.asked[kGuide] == 2);
}

TEST_CASE("first run: a reload during the first provider guide starts it again when the list comes back the same") {
  fake::Tv tv;
  answerHarbor(tv);
  Store store(tv.host());
  addHarbor(store, true);
  REQUIRE(stepUntil(store, tv, [&] { return providerRunning(store); }));
  int lists = tv.pool.asked[kList];

  // Reload channels and listings: the build is cancelled, the list read
  // again and found unchanged.
  store.refreshAll();
  CHECK_FALSE(providerRunning(store));
  fake::settle(store, tv);
  CHECK(tv.pool.asked[kList] == lists + 1);
  CHECK(tv.pool.asked[kGuide] == 1);
  REQUIRE(store.guideListings().providers.size() == 1);
  CHECK(store.guideListings().providers.front().line.rfind("Updated", 0) == 0);
  CHECK(store.listings()->byChannel.size() == 1);  // Harbor News has its listings

  // Nothing more after that.
  fake::settle(store, tv);
  CHECK(tv.pool.asked[kGuide] == 1);
}

TEST_CASE("first run: a reload during the outside listings starts them again") {
  fake::Tv tv;
  answerHarbor(tv);
  Store store(tv.host());
  addHarbor(store, false);
  REQUIRE(stepUntil(store, tv, [&] { return outsideRunning(store); }));
  CHECK(outsideAsks(tv) == 0);  // queued, not read yet

  store.refreshAll();
  CHECK_FALSE(outsideRunning(store));
  fake::settle(store, tv);
  CHECK(outsideAsks(tv) > 0);  // offline here, but tried
  CHECK_FALSE(outsideRunning(store));
}

TEST_CASE("first run: while the guide loads, refreshing it or the lineup starts nothing more") {
  fake::Tv tv;
  answerHarbor(tv);
  Store store(tv.host());
  addHarbor(store, true);
  REQUIRE(stepUntil(store, tv, [&] { return store.guideLoading(); }));
  // The first guide is due to start: already loading.
  CHECK_FALSE(store.refreshListings());
  REQUIRE(stepUntil(store, tv, [&] { return providerRunning(store); }));
  CHECK(store.guideLoading());
  CHECK_FALSE(store.refreshListings());
  CHECK_FALSE(store.refreshSuggestions());
  CHECK_FALSE(store.lineupPrefs().configured);  // no suggestions made
  fake::settle(store, tv);
  CHECK(tv.pool.asked[kGuide] == 1);  // no second build

  // Done loading (the outside listings too, offline here): both work again.
  CHECK_FALSE(store.guideLoading());
  CHECK(store.refreshSuggestions());
  CHECK(store.lineupPrefs().configured);
  fake::settle(store, tv);
  CHECK(store.refreshListings());
  fake::settle(store, tv);
  CHECK(tv.pool.asked[kGuide] == 2);
}
