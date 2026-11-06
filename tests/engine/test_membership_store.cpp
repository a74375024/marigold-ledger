// Build 144 on the pretend TV: a provider's end date read at sign-in and
// with every channel list read (Xtream, and an M3U playlist from an Xtream
// panel), and the guide's once-a-day heads-up in the status line.
#include <doctest.h>

#include <string>

#include "core/engine/membership.hpp"
#include "core/engine/store.hpp"
#include "core/sources/xtream.hpp"
#include "engine/loaded_store.hpp"
#include "sources/mock_provider.hpp"

using marigold::engine::AccountForm;
using marigold::engine::Store;
using marigold::engine::StoreStatus;
namespace ms = marigold::engine::membership;
namespace mtime = marigold::time;
using fake::settle;

namespace {

struct ClockAhead {
  mtime::Seconds by = 0;
  void move(mtime::Seconds s) {
    mtime::advanceForTests(s);
    by += s;
  }
  ~ClockAhead() { mtime::advanceForTests(-by); }
};

struct XtreamLinks {
  std::string info, categories, streams;
};

XtreamLinks xtreamLinks() {
  marigold::sources::Account a;
  a.kind = marigold::sources::AccountKind::Xtream;
  a.serverUrl = std::string(mock::kHost);
  a.userName = std::string(mock::kUser);
  a.password = std::string(mock::kPass);
  auto client = marigold::sources::xtream::Client::make(a);
  REQUIRE(client);
  return {client.value().accountInfoRequest().url, client.value().categoriesRequest().url,
          client.value().streamsRequest().url};
}

std::string userInfo(const std::string& fields) {
  return R"({"user_info":{"auth":1,)" + fields + R"(},"server_info":{}})";
}

std::string addXtream(Store& store, fake::Tv& tv) {
  AccountForm form;
  form.kind = AccountForm::Kind::Xtream;
  form.name = "Backup TV";
  form.server = std::string(mock::kHost);
  form.user = std::string(mock::kUser);
  form.password = std::string(mock::kPass);
  std::string problem = "(not answered)";
  store.addAccount(form, [&](std::string p) { problem = std::move(p); });
  settle(store, tv);
  return problem;
}

// A frame of the app's loop: the store's tick, a tenth of a second on.
void frames(Store& store, fake::Tv& tv, int n) {
  for (int i = 0; i < n; ++i) {
    tv.ms += 100;
    store.tick(tv.ms);
  }
}

}  // namespace

TEST_CASE("membership: an Xtream end date and streams at once, renewed by itself on the next read") {
  fake::Tv tv;
  mock::Provider provider(120);
  XtreamLinks links = xtreamLinks();
  mtime::Seconds end = mtime::now() + 40 * 86400;
  tv.pool.answer(links.info, userInfo(R"("status":"Active","exp_date":")" + std::to_string(end) +
                                      R"(","max_connections":"2","is_trial":"1")"));
  tv.pool.answer(links.categories, provider.categoriesJson());
  tv.pool.answer(links.streams, provider.liveStreamsJson());
  Store store(tv.host());
  REQUIRE(addXtream(store, tv).empty());
  REQUIRE(store.accounts().size() == 1);
  ms::Membership m = store.accounts()[0].membership;
  CHECK(m.ends == end);
  CHECK(m.streams == 2);
  CHECK(m.trial);
  CHECK_FALSE(m.endedStatus);

  // Renewed with the provider: the next read of the channel list brings it.
  mtime::Seconds later = end + 365 * 86400;
  tv.pool.answer(links.info, userInfo(R"("status":"Active","exp_date":)" + std::to_string(later) +
                                      R"(,"max_connections":1,"is_trial":"0")"));
  int asked = tv.pool.asked[links.info];
  store.refreshAll();
  settle(store, tv);
  CHECK(tv.pool.asked[links.info] > asked);
  m = store.accounts()[0].membership;
  CHECK(m.ends == later);
  CHECK(m.streams == 1);
  CHECK_FALSE(m.trial);
  // Saved for the next launch.
  CHECK(tv.files.read("accounts.json").value_or("").find(std::to_string(later)) != std::string::npos);

  // The panel says it has ended.
  tv.pool.answer(links.info, userInfo(R"("status":"Expired","exp_date":"0")"));
  store.refreshAll();
  settle(store, tv);
  m = store.accounts()[0].membership;
  CHECK_FALSE(m.ends);
  CHECK(m.endedStatus);
  CHECK(ms::stage(m, mtime::now(), tv.zone) == ms::Stage::Ended);
}

TEST_CASE("membership: the guide's heads-up shows for a few seconds, once a day, across launches") {
  fake::Tv tv;
  mock::Provider provider(120);
  XtreamLinks links = xtreamLinks();
  mtime::Seconds end = mtime::now() + 5 * 86400;
  tv.pool.answer(links.info, userInfo(R"("exp_date":")" + std::to_string(end) + R"(")"));
  tv.pool.answer(links.categories, provider.categoriesJson());
  tv.pool.answer(links.streams, provider.liveStreamsJson());
  std::string said;
  {
    Store store(tv.host());
    REQUIRE(addXtream(store, tv).empty());
    REQUIRE(store.status().phase == StoreStatus::Phase::Ready);
    said = ms::headsUp("Backup TV", store.accounts()[0].membership, mtime::now(), tv.zone);
    REQUIRE(said == "Backup TV ends in 5 days");
    CHECK(store.status().line != said);  // not before the guide opens
    store.guideOpened();
    frames(store, tv, 1);
    CHECK(store.status().line == said);
    CHECK(store.status().mark == StoreStatus::Mark::Failed);  // the amber "!"
    frames(store, tv, 75);
    CHECK(store.status().line == said);  // still up after 7.5 seconds
    frames(store, tv, 10);
    CHECK(store.status().line.empty());  // gone after 8
    // Opening the guide again the same day: nothing.
    store.guideOpened();
    frames(store, tv, 5);
    CHECK(store.status().line.empty());
  }
  {
    // The next launch, the same day: still nothing.
    Store store(tv.host());
    settle(store, tv);
    store.guideOpened();
    frames(store, tv, 5);
    CHECK(store.status().line != said);
  }
  {
    // The next day: once again.
    ClockAhead clock;
    clock.move(86400);
    Store store(tv.host());
    settle(store, tv);
    store.guideOpened();
    frames(store, tv, 1);
    CHECK(store.status().line == "Backup TV ends in 4 days");
  }
}

TEST_CASE("membership: the heads-up waits for the channel list's update line") {
  fake::Tv tv;
  mock::Provider provider(120);
  XtreamLinks links = xtreamLinks();
  tv.pool.answer(links.info, userInfo(R"("exp_date":")" + std::to_string(mtime::now() + 2 * 86400) + R"(")"));
  tv.pool.answer(links.categories, provider.categoriesJson());
  tv.pool.answer(links.streams, provider.liveStreamsJson());
  Store store(tv.host());
  REQUIRE(addXtream(store, tv).empty());
  // An update under way: its line keeps the place.
  tv.pool.hold(links.streams);
  store.refreshAll();
  tv.pool.drain();
  store.guideOpened();
  frames(store, tv, 3);
  CHECK(store.status().mark == StoreStatus::Mark::Busy);
  CHECK(store.status().line.find("ends") == std::string::npos);
  // Done: then it gets its turn.
  tv.pool.letGo(links.streams);
  store.guideOpened();  // (already waiting: no second one)
  bool shown = false;
  for (int i = 0; i < 200 && !shown; ++i) {
    tv.pool.drain();
    frames(store, tv, 1);
    shown = store.status().line == "Backup TV ends in 2 days";
  }
  CHECK(shown);
}

TEST_CASE("membership: an M3U playlist from an Xtream panel asks the panel; any other playlist doesn't") {
  // On kHost, mock.example.invalid:
  const std::string panelList = std::string(mock::kHost) + "/get.php?username=" + std::string(mock::kUser) +  // .invalid
                                "&password=" + std::string(mock::kPass) + "&type=m3u_plus&output=ts";
  fake::Tv tv;
  mock::Provider provider(120);
  XtreamLinks links = xtreamLinks();
  mtime::Seconds end = mtime::now() + 90 * 86400;
  tv.pool.answer(panelList, provider.m3u());
  tv.pool.answer(links.info, userInfo(R"("exp_date":")" + std::to_string(end) + R"(","max_connections":"3")"));
  {
    Store store(tv.host());
    AccountForm form = fake::m3uForm();
    form.server = panelList;
    std::string problem = "(not answered)";
    store.addAccount(form, [&](std::string p) { problem = std::move(p); });
    settle(store, tv);
    REQUIRE(problem.empty());
    REQUIRE(store.accounts().size() == 1);
    CHECK(store.accounts()[0].kind == "M3U playlist");
    CHECK(store.accounts()[0].membership.ends == end);
    CHECK(store.accounts()[0].membership.streams == 3);
    CHECK(tv.pool.asked[links.info] >= 1);
  }
  // A plain playlist link: no panel to ask, and no date.
  fake::Tv plain;
  plain.pool.answer(fake::kPlaylist, provider.m3u());
  Store store(plain.host());
  REQUIRE(fake::addPlaylist(store, plain).empty());
  CHECK_FALSE(store.accounts()[0].membership.ends);
  for (const auto& [url, n] : plain.pool.asked) CHECK(url.find("player_api.php") == std::string::npos);
}
