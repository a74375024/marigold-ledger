// Build 130: first-run setup's "Getting your guide ready" (Store::guideReady:
// the channels, My Lineup and the provider's listings, a failure, how far
// the download is), the guide header's words while the guides build, and
// what a row with no listings says (views::emptyRow, listingsOnTheWay).
#include <doctest.h>

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "core/catalog/lineup.hpp"
#include "core/engine/store.hpp"
#include "core/engine/views.hpp"
#include "core/net/http.hpp"
#include "core/util/time.hpp"
#include "engine/loaded_store.hpp"

using namespace marigold;
using namespace marigold::engine;

namespace {

constexpr const char* kList = "http://mock.example.invalid:8080/lighthouse.m3u";
constexpr const char* kGuide = "http://mock.example.invalid:8080/lighthouse.xml";

void answerList(fake::Tv& tv) {
  tv.pool.answer(kList,
                 "#EXTM3U\n"
                 "#EXTINF:-1 tvg-id=\"lighthouse.news\" group-title=\"Lighthouse TV\",Lighthouse News HD\n"
                 "http://mock.example.invalid:8080/a.m3u8\n"
                 "#EXTINF:-1 tvg-id=\"cove.sports\" group-title=\"Lighthouse TV\",Cove Sports\n"
                 "http://mock.example.invalid:8080/b.ts\n");
}

// A guide for Lighthouse News only, `hours` hour-long shows from an hour ago
// (enough of them and the file comes in several pieces).
void answerGuide(fake::Tv& tv, int hours) {
  std::string xml =
      "<?xml version=\"1.0\"?>\n<tv>\n<channel id=\"lighthouse.news\"><display-name>Lighthouse News</display-name>"
      "</channel>\n";
  time::Seconds hour = time::now() / 3600 * 3600;
  auto stamp = [](time::Seconds t) {
    time::Civil c = time::civilFromSeconds(t);
    char b[32];
    std::snprintf(b, sizeof b, "%04d%02d%02d%02d%02d00 +0000", c.year, c.month, c.day, c.hour, c.minute);
    return std::string(b);
  };
  for (int i = -1; i < hours - 1; ++i)
    xml += "<programme start=\"" + stamp(hour + i * 3600) + "\" stop=\"" + stamp(hour + (i + 1) * 3600) +
           "\" channel=\"lighthouse.news\"><title>Harbour Lights " + std::to_string(i + 2) +
           "</title><desc>The evening's stories from the bay, the ferries and the weather.</desc></programme>\n";
  xml += "</tv>\n";
  tv.pool.answer(kGuide, xml);
}

void addLighthouse(Store& store, bool guideLink) {
  AccountForm form = fake::m3uForm();
  form.server = kList;
  if (guideLink) form.guideUrl = kGuide;
  store.addAccount(form, [](std::string) {});
}

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

std::string entryNamed(const Store& store, const std::string& name) {
  for (const GuideChannel& g : store.channels()->all)
    if (g.name.find(name) != std::string::npos) return g.id;
  return "";
}

}  // namespace

TEST_CASE("guide ready: nothing is ticked before the channels are in") {
  fake::Tv tv;
  answerList(tv);
  Store store(tv.host());
  GuideReady r = store.guideReady();
  CHECK_FALSE(r.channels);
  CHECK_FALSE(r.lineup);
  CHECK_FALSE(r.listingsProblem);
  CHECK(r.listingsProgress == doctest::Approx(-1));
}

TEST_CASE("guide ready: a playlist with no guide file has its listings done once the channels are in") {
  fake::Tv tv;
  answerList(tv);
  Store store(tv.host());
  {  // Build 153: these made-up channels aren't on the channel list (Other).
    Settings shown = store.settings();
    shown.showOther = true;
    store.setSettings(shown);
  }
  addLighthouse(store, false);
  fake::settle(store, tv);
  GuideReady r = store.guideReady();
  CHECK(r.channels);
  CHECK(r.listings);  // nothing to wait for (the missing listings never are)
  CHECK_FALSE(r.listingsProblem);
  CHECK(r.problemLine.empty());
  CHECK(r.listingsProgress == doctest::Approx(-1));
  // My Lineup: once it is made and its rows are published.
  CHECK_FALSE(r.lineup);
  store.buildLineup(store.lineupPrefs());
  CHECK_FALSE(store.guideReady().lineup);  // its rows are on a worker
  fake::settle(store, tv);
  CHECK(store.guideReady().lineup);
}

TEST_CASE("guide ready: the provider's listings tick once their guide is built; no size, no bar") {
  fake::Tv tv;
  answerList(tv);
  answerGuide(tv, 120);
  Store store(tv.host());
  {  // Build 153: these made-up channels aren't on the channel list (Other).
    Settings shown = store.settings();
    shown.showOther = true;
    store.setSettings(shown);
  }
  addLighthouse(store, true);
  REQUIRE(stepUntil(store, tv, [&] { return providerRunning(store); }));
  GuideReady r = store.guideReady();
  CHECK(r.channels);
  CHECK_FALSE(r.listings);
  CHECK_FALSE(r.listingsProblem);
  CHECK(r.listingsProgress == doctest::Approx(-1));

  // The server gives no size: the bar stays away the whole download.
  std::vector<float> seen;
  tv.pool.duringBody = [&] { seen.push_back(store.guideReady().listingsProgress); };
  fake::settle(store, tv);
  tv.pool.duringBody = nullptr;
  REQUIRE(seen.size() > 1);
  for (float p : seen) CHECK(p == doctest::Approx(-1));
  r = store.guideReady();
  CHECK(r.listings);
  CHECK_FALSE(r.listingsProblem);
  CHECK(r.listingsProgress == doctest::Approx(-1));
}

TEST_CASE("guide ready: the bar follows the download when the provider says how big it is") {
  fake::Tv tv;
  answerList(tv);
  answerGuide(tv, 120);
  tv.pool.announceLength = true;
  Store store(tv.host());
  addLighthouse(store, true);
  REQUIRE(stepUntil(store, tv, [&] { return providerRunning(store); }));
  CHECK(store.guideReady().listingsProgress == doctest::Approx(-1));  // not started yet
  std::vector<float> seen;
  tv.pool.duringBody = [&] {
    GuideReady r = store.guideReady();
    if (!r.listings) seen.push_back(r.listingsProgress);
  };
  fake::settle(store, tv);
  tv.pool.duringBody = nullptr;
  REQUIRE(seen.size() > 2);
  CHECK(seen.front() > 0);
  CHECK(seen.front() < 1);
  for (size_t i = 1; i < seen.size(); ++i) CHECK(seen[i] >= seen[i - 1]);
  CHECK(seen.back() == doctest::Approx(1));
  GuideReady r = store.guideReady();
  CHECK(r.listings);
  CHECK(r.listingsProgress == doctest::Approx(-1));  // nothing running
}

TEST_CASE("guide ready: a provider guide that can't be downloaded is a problem, said in plain words") {
  fake::Tv tv;
  answerList(tv);  // the guide link isn't answered: offline
  Store store(tv.host());
  {  // Build 153: these made-up channels aren't on the channel list (Other).
    Settings shown = store.settings();
    shown.showOther = true;
    store.setSettings(shown);
  }
  addLighthouse(store, true);
  fake::settle(store, tv);
  GuideReady r = store.guideReady();
  CHECK(r.channels);
  CHECK_FALSE(r.listings);
  CHECK(r.listingsProblem);
  CHECK(r.problemLine == "Your provider's listings couldn't be loaded right now");
  CHECK(r.listingsProgress == doctest::Approx(-1));
}

TEST_CASE("guide header: the provider's first guide, then an update, both with the spinner") {
  fake::Tv tv;
  answerList(tv);
  answerGuide(tv, 24);
  Store store(tv.host());
  addLighthouse(store, true);
  REQUIRE(stepUntil(store, tv, [&] { return providerRunning(store); }));
  CHECK(store.status().line == "Loading the guide for the first time\xe2\x80\xa6");
  CHECK(store.status().mark == StoreStatus::Mark::Busy);
  fake::settle(store, tv);
  CHECK(store.status().line.empty());

  // Asked for again: there is a saved one now.
  REQUIRE(store.refreshListings());
  REQUIRE(stepUntil(store, tv, [&] { return providerRunning(store); }));
  CHECK(store.status().line == "Updating the provider's guide\xe2\x80\xa6");
  CHECK(store.status().mark == StoreStatus::Mark::Busy);
  fake::settle(store, tv);
}

TEST_CASE("guide header: the missing listings fill in with the spinner") {
  fake::Tv tv;
  answerList(tv);
  Store store(tv.host());
  addLighthouse(store, false);
  REQUIRE(stepUntil(store, tv, [&] { return outsideRunning(store); }));
  CHECK(store.status().line == "Filling in missing listings\xe2\x80\xa6");
  CHECK(store.status().mark == StoreStatus::Mark::Busy);
  fake::settle(store, tv);
  CHECK(store.status().line.empty());
}

TEST_CASE("guide header: the words for each guide build") {
  GuideBuildLine none = guideBuildLine(false, false, false, false);
  CHECK(none.line.empty());
  CHECK(none.mark == StoreStatus::Mark::None);
  GuideBuildLine first = guideBuildLine(true, true, true, false);  // the provider's goes first
  CHECK(first.line == "Loading the guide for the first time\xe2\x80\xa6");
  CHECK(first.mark == StoreStatus::Mark::Busy);
  GuideBuildLine update = guideBuildLine(true, false, false, false);
  CHECK(update.line == "Updating the provider's guide\xe2\x80\xa6");
  CHECK(update.mark == StoreStatus::Mark::Busy);
  GuideBuildLine filling = guideBuildLine(false, false, true, false);
  CHECK(filling.line == "Filling in missing listings\xe2\x80\xa6");
  CHECK(filling.mark == StoreStatus::Mark::Busy);
  // Waiting for full-screen playback to end: no spinner, nothing moves.
  GuideBuildLine waiting = guideBuildLine(false, false, true, true);
  CHECK(waiting.line == "Missing listings wait until you stop watching");
  CHECK(waiting.mark == StoreStatus::Mark::None);
}

TEST_CASE("guide rows: listings on the way while a guide builds, then none coming") {
  fake::Tv tv;
  answerList(tv);
  answerGuide(tv, 24);
  Store store(tv.host());
  {  // Build 153: these made-up channels aren't on the channel list (Other).
    Settings shown = store.settings();
    shown.showOther = true;
    store.setSettings(shown);
  }
  addLighthouse(store, true);
  REQUIRE(stepUntil(store, tv, [&] { return providerRunning(store); }));
  std::string cove = entryNamed(store, "Cove Sports");
  REQUIRE(!cove.empty());
  CHECK(store.listingsOnTheWay(cove));
  fake::settle(store, tv);  // the provider's guide, then the missing listings (offline here)
  CHECK_FALSE(store.guideLoading());
  CHECK_FALSE(store.listingsOnTheWay(cove));
  CHECK_FALSE(store.savedListingsReading());
  CHECK(store.listings()->forChannel(cove) == nullptr);
}

TEST_CASE("views: what a row with nothing to draw says") {
  using views::EmptyRow;
  using views::emptyRow;
  // A later window: none for this time, whatever is on its way.
  CHECK(emptyRow(true, false, false, true, false) == EmptyRow::NoneForThisTime);
  // Listings, just none now: no guide info.
  CHECK(emptyRow(false, true, false, true, false) == EmptyRow::NoGuideInfo);
  // A relaunch reading the saved guide: placeholders, no words yet.
  CHECK(emptyRow(false, false, false, true, true) == EmptyRow::Placeholders);
  CHECK(emptyRow(false, false, false, false, true) == EmptyRow::Placeholders);
  // A request that came back empty while a build still runs: on the way.
  CHECK(emptyRow(false, false, true, true, true) == EmptyRow::OnTheWay);
  CHECK(emptyRow(false, false, false, true, false) == EmptyRow::OnTheWay);
  // Nothing more coming.
  CHECK(emptyRow(false, false, false, false, false) == EmptyRow::NoGuideInfo);
  CHECK(emptyRow(false, false, true, false, false) == EmptyRow::NoGuideInfo);
  CHECK(std::string(views::kListingsOnTheWay) == "Listings on the way");
}
