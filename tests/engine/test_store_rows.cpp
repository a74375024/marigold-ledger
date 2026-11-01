// The store's lighter UI thread (Build 120) shows exactly what it showed
// before: rows made once per lineup on a worker and patched by a tune or a
// favourite against every row worked out again the old way; the M3U links
// read after launch and merged on a worker; guides refreshed by their own
// age on a TV left on with the same channel list.
#include <doctest.h>

#include <cstdio>
#include <string>
#include <vector>

#include "core/catalog/lineup.hpp"
#include "core/engine/store.hpp"
#include "core/guide/outside.hpp"
#include "core/sources/account.hpp"
#include "core/sources/xtream.hpp"
#include "core/util/log.hpp"
#include "core/util/time.hpp"
#include "engine/loaded_store.hpp"
#include "sources/mock_provider.hpp"

using namespace marigold;
using namespace marigold::engine;

namespace {

// The first field two snapshots differ in ("" when none; versions aside).
std::string difference(const Channels& a, const Channels& b) {
  if (a.all.size() != b.all.size()) return "row count";
  for (size_t i = 0; i < a.all.size(); ++i) {
    const GuideChannel& x = a.all[i];
    const GuideChannel& y = b.all[i];
    std::string at = " of row " + std::to_string(i) + " (" + x.id + ")";
    if (x.id != y.id) return "id" + at;
    if (x.name != y.name) return "name" + at;
    if (x.subtitle != y.subtitle) return "subtitle" + at;
    if (x.logoUrl != y.logoUrl) return "logo" + at;
    if (x.section != y.section || x.sectionRank != y.sectionRank) return "section" + at;
    if (x.group != y.group) return "group" + at;
    if (x.number != y.number) return "number" + at;
    if (x.feeds != y.feeds) return "feeds" + at;
    if (x.west != y.west || x.partner != y.partner) return "coast" + at;
    if (x.favorite != y.favorite) return "favorite" + at;
    if (x.inLineup != y.inLineup) return "in lineup" + at;
    if (x.browsable != y.browsable) return "browsable" + at;
    if (x.logoSources != y.logoSources) return "logo sources" + at;
    if (x.radio != y.radio) return "radio" + at;
    if (x.style != y.style) return "style" + at;
  }
  if (a.lineup != b.lineup) return "lineup";
  if (a.browse != b.browse) return "browse";
  if (a.every != b.every) return "every";
  if (a.favorites != b.favorites) return "favorites";
  if (a.recent != b.recent) return "recent";
  if (a.byId != b.byId) return "byId";
  if (a.renamed != b.renamed) return "renamed";
  if (a.rawChannels != b.rawChannels) return "raw channels";
  if (a.lineupConfigured != b.lineupConfigured) return "lineup configured";
  return "";
}

// What is on screen equals every row worked out again now.
#define CHECK_ROWS(store)                                            \
  do {                                                               \
    auto shown_ = (store).channels();                                \
    auto again_ = (store).channelsNow();                             \
    REQUIRE(shown_);                                                 \
    CHECK(difference(*shown_, *again_) == std::string());            \
  } while (0)

// One loop pass: what's queued runs, then the tick that publishes.
void step(Store& store, fake::Tv& tv) {
  tv.pool.drain();
  tv.ms += 100;
  store.tick(tv.ms);
}

// The tick that publishes (before the queued work runs: a lineup edit's new
// lineup is still on its worker), then the passes that put the patch made
// on a worker in, and what the queued work publishes after it.
void publish(Store& store, fake::Tv& tv) {
  tv.ms += 16;
  store.tick(tv.ms);
  for (int i = 0; i < 3; ++i) step(store, tv);
}

// The newest log line holding `text` ("" when none).
std::string newest(const std::string& text) {
  std::vector<std::string> lines = log::recent(200);
  for (auto it = lines.rbegin(); it != lines.rend(); ++it)
    if (it->find(text) != std::string::npos) return *it;
  return "";
}

// now() moved on for one test, and back after it.
struct ClockAhead {
  time::Seconds by = 0;
  void move(time::Seconds s) {
    time::advanceForTests(s);
    by += s;
  }
  ~ClockAhead() { time::advanceForTests(-by); }
};

}  // namespace

TEST_CASE("engine: published rows equal every row worked out again, through tunes, favourites and lineup edits") {
  fake::Tv tv;
  mock::Provider provider(1500);
  tv.pool.answer(fake::kPlaylist, provider.m3u());
  Store store(tv.host());
  REQUIRE(fake::addPlaylist(store, tv).empty());
  auto ch = store.channels();
  REQUIRE(ch->all.size() > 100);
  CHECK_ROWS(store);

  // My Lineup set up.
  catalog::LineupPrefs p = store.lineupPrefs();
  p.configured = true;
  store.buildLineup(p);
  fake::settle(store, tv);
  ch = store.channels();
  REQUIRE(!ch->lineup.empty());
  CHECK_ROWS(store);

  // Tunes and favourites: each tick's publish is a patch.
  std::vector<std::string> ids;
  for (size_t i = 0; i < ch->all.size(); i += ch->all.size() / 7) ids.push_back(ch->all[i].id);
  for (const std::string& id : ids) {
    store.noteWatched(id);
    publish(store, tv);
    CHECK_ROWS(store);
  }
  CHECK(store.channels()->recent.size() == ids.size());
  for (const std::string& id : ids) {
    store.toggleFavorite(id);
    publish(store, tv);
    CHECK_ROWS(store);
  }
  CHECK(store.channels()->favorites.size() == ids.size());
  store.toggleFavorite(ids[1]);
  publish(store, tv);
  CHECK_ROWS(store);

  // Lineup edits: published before the new lineup is in, and after.
  const std::string out = ch->all[static_cast<size_t>(ch->lineup.front())].id;
  store.toggleLineup(out);
  publish(store, tv);
  CHECK_ROWS(store);
  CHECK_FALSE(store.channels()->find(out)->inLineup);
  fake::settle(store, tv);
  CHECK_ROWS(store);
  store.addToLineup({ids[2], ids[3]});
  publish(store, tv);
  CHECK_ROWS(store);
  fake::settle(store, tv);
  CHECK_ROWS(store);
  store.setCoast(catalog::Coast::west);
  fake::settle(store, tv);
  CHECK_ROWS(store);
  store.setLineupCountries({"US", "UK"});
  fake::settle(store, tv);
  CHECK_ROWS(store);

  // A hidden group, settings.
  const std::string group = store.channels()->all.front().group;
  store.setGroupHidden(group, true);
  publish(store, tv);
  CHECK_ROWS(store);
  Settings s = store.settings();
  s.allChannelsTab = !s.allChannelsTab;
  s.fillMissingListings = false;
  store.setSettings(s);
  fake::settle(store, tv);
  CHECK_ROWS(store);

  // A list that really changed.
  mock::Provider bigger(1800);
  tv.pool.answer(fake::kPlaylist, bigger.m3u());
  store.refreshAll();
  fake::settle(store, tv);
  CHECK(store.channels()->all.size() > ch->all.size());
  CHECK_ROWS(store);

  // The next launch, from the saved lists and list.
  tv.pool.drain();
  size_t favorites = store.channels()->favorites.size();
  Store again(tv.host());
  fake::settle(again, tv);
  CHECK_ROWS(again);
  CHECK(again.channels()->favorites.size() == favorites);
  CHECK(again.channels()->recent.size() == store.channels()->recent.size());
}

TEST_CASE("engine: a refresh that only renames event slots patches the rows it has") {
  fake::Tv tv;
  mock::Provider provider(600);
  // An Xtream account (its channel ids don't change when a slot is renamed).
  sources::Account a;
  a.kind = sources::AccountKind::Xtream;
  a.serverUrl = std::string(mock::kHost);
  a.userName = std::string(mock::kUser);
  a.password = std::string(mock::kPass);
  auto client = sources::xtream::Client::make(a);
  REQUIRE(client);
  const std::string streamsURL = client.value().streamsRequest().url;
  tv.pool.answer(client.value().accountInfoRequest().url, R"({"user_info":{"auth":1,"status":"Active"},"server_info":{}})");
  tv.pool.answer(client.value().categoriesRequest().url, provider.categoriesJson());
  std::string before = provider.liveStreamsJson();
  tv.pool.answer(streamsURL, before);
  Store store(tv.host());
  AccountForm form;
  form.kind = AccountForm::Kind::Xtream;
  form.name = "Mock";
  form.server = a.serverUrl;
  form.user = a.userName;
  form.password = a.password;
  store.addAccount(form, [](std::string) {});
  fake::settle(store, tv);
  REQUIRE(store.channels()->all.size() > 20);
  store.toggleFavorite(store.channels()->all.front().id);
  fake::settle(store, tv);
  CHECK_ROWS(store);

  // The minute's timing line says no publish worked every row out again.
  tv.ms += 61000;
  store.tick(tv.ms);
  uint64_t version = store.channels()->version;
  std::string after = before;
  for (size_t at = after.find("7:30 PM ET"); at != std::string::npos; at = after.find("7:30 PM ET", at + 1))
    after.replace(at, 10, "8:00 PM ET");
  tv.pool.answer(streamsURL, after);
  store.refreshAll();
  fake::settle(store, tv);
  CHECK(newest("event names changed").find("refresh: only") != std::string::npos);
  CHECK(store.channels()->version != version);
  CHECK_ROWS(store);
  store.noteWatched(store.channels()->all.back().id);
  publish(store, tv);
  CHECK_ROWS(store);
  tv.ms += 61000;
  store.tick(tv.ms);
  std::string line = newest("timing publishes");
  CHECK(line.find(" full=0 ") != std::string::npos);
  CHECK(line.find(" onWorker=0 ") == std::string::npos);  // patched off the UI thread
}

TEST_CASE("engine: the saved M3U links are read after launch, or at once by a tune that comes first") {
  fake::Tv tv;
  mock::Provider provider(300);
  tv.pool.answer(fake::kPlaylist, provider.m3u());
  std::string entry;
  size_t feeds = 0;
  {
    Store store(tv.host());
    REQUIRE(fake::addPlaylist(store, tv).empty());
    auto ch = store.channels();
    for (const GuideChannel& g : ch->all) {
      if (store.feeds(g.id).empty()) continue;
      entry = g.id;
      feeds = store.feeds(g.id).size();
      break;
    }
    REQUIRE(!entry.empty());
    CHECK(!store.feeds(entry).front().url.empty());
    tv.pool.drain();
  }
  REQUIRE(tv.files.read("m3u-streams.json"));
  {
    // A tune before the first frame's tick (the saved list in, no tick):
    // read on the spot.
    Store store(tv.host());
    tv.pool.drain();
    CHECK(store.feeds(entry).size() == feeds);
    tv.pool.cancelAll();  // the app's workers stop before its store goes
  }
  {
    // Read on a worker after the first frame; the same links downloaded
    // again aren't written again.
    Store store(tv.host());
    fake::settle(store, tv);
    std::remove(tv.files.path("m3u-streams.json").c_str());
    store.refreshAll();
    fake::settle(store, tv);
    CHECK(store.feeds(entry).size() == feeds);
    CHECK_FALSE(tv.files.read("m3u-streams.json"));
    // A provider removed: its links go.
    store.removeAccount(store.accounts().front().id);
    fake::settle(store, tv);
    auto left = tv.files.read("m3u-streams.json");
    REQUIRE(left);
    CHECK(left->find("example.invalid") == std::string::npos);
  }
}

TEST_CASE("engine: a TV left on with the same channel list refreshes its guides when they are due") {
  constexpr const char* kList = "http://mock.example.invalid:8080/harbor.m3u";
  constexpr const char* kGuide = "http://mock.example.invalid:8080/harbor.xml";
  fake::Tv tv;
  tv.pool.answer(kList,
                 "#EXTM3U\n"
                 "#EXTINF:-1 tvg-id=\"harbor.news\" group-title=\"Harbor TV\",Harbor News HD\n"
                 "http://mock.example.invalid:8080/a.m3u8\n"
                 "#EXTINF:-1 tvg-id=\"valley.sports\" group-title=\"Harbor TV\",Valley Sports\n"
                 "http://mock.example.invalid:8080/b.ts\n");
  // Two days of hour-long programmes from an hour ago.
  std::string xml = "<?xml version=\"1.0\"?>\n<tv>\n<channel id=\"harbor.news\"><display-name>Harbor News</display-name></channel>\n";
  time::Seconds hour = time::now() / 3600 * 3600;
  auto stamp = [](time::Seconds t) {
    time::Civil c = time::civilFromSeconds(t);
    char b[32];
    std::snprintf(b, sizeof b, "%04d%02d%02d%02d%02d00 +0000", c.year, c.month, c.day, c.hour, c.minute);
    return std::string(b);
  };
  for (int i = -1; i < 48; ++i)
    xml += "<programme start=\"" + stamp(hour + i * 3600) + "\" stop=\"" + stamp(hour + (i + 1) * 3600) +
           "\" channel=\"harbor.news\"><title>Harbor Report " + std::to_string(i + 2) + "</title></programme>\n";
  xml += "</tv>\n";
  tv.pool.answer(kGuide, xml);
  auto outsideAsks = [&tv] {
    int n = 0;
    for (const guide::Source& s : guide::sources()) n += tv.pool.asked[s.url];
    return n;
  };

  ClockAhead clock;
  Store store(tv.host());
  AccountForm form = fake::m3uForm();
  form.server = kList;
  form.guideUrl = kGuide;
  store.addAccount(form, [](std::string) {});
  fake::settle(store, tv);
  fake::settle(store, tv);
  REQUIRE(tv.pool.asked[kGuide] == 1);
  int lists = tv.pool.asked[kList];
  int outside = outsideAsks();
  REQUIRE(outside > 0);  // offline here: tried again later

  // Left on: a minute at a time, the list read again every couple of hours
  // and unchanged each time.
  auto minutes = [&](int n) {
    for (int i = 0; i < n; ++i) {
      clock.move(60);
      tv.pool.drain();
      tv.ms += 60000;
      store.tick(tv.ms);
      tv.pool.drain();
      tv.ms += 5000;
      store.tick(tv.ms);
    }
  };
  minutes(11 * 60);
  CHECK(tv.pool.asked[kList] > lists + 3);
  CHECK(tv.pool.asked[kGuide] == 1);  // not due yet (12 h)
  CHECK(outsideAsks() > outside);     // the failed outside guide, tried again
  minutes(90);
  CHECK(tv.pool.asked[kGuide] == 2);  // due: read again, once
  minutes(60);
  CHECK(tv.pool.asked[kGuide] == 2);

  // Asleep past its age: read again on waking.
  clock.move(13 * 3600);
  for (int i = 0; i < 60; ++i) step(store, tv);
  CHECK(tv.pool.asked[kGuide] == 3);
}
