// Build 132: another country's sports channel (one of the brands, not in a
// country picked) has its listings kept from the provider's own guide for
// Live Events, and nothing else: not in the browse list or My Lineup, while
// another country's other channels stay out of the guide's scope. Its row
// resolves (the tile and Where to Watch name it) and plays (its feeds). A
// hidden group's are left out. Every name and link is made up.
#include <doctest.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>

#include "core/catalog/lineup.hpp"
#include "core/engine/store.hpp"
#include "core/util/time.hpp"
#include "engine/loaded_store.hpp"

using namespace marigold;
using namespace marigold::engine;

namespace {

constexpr const char* kList = "http://mock.example.invalid:8080/abroad.m3u";
constexpr const char* kGuide = "http://mock.example.invalid:8080/abroad.xml";

void answerList(fake::Tv& tv) {
  tv.pool.answer(kList,
                 "#EXTM3U\n"
                 "#EXTINF:-1 tvg-id=\"harbor.us\" group-title=\"US| SPORTS\",US: Harbor Sports Network\n"
                 "http://mock.example.invalid:8080/1.ts\n"
                 "#EXTINF:-1 tvg-id=\"skymain.uk\" group-title=\"UK| SPORTS\",UK: Sky Sports Main Event\n"
                 "http://mock.example.invalid:8080/2.ts\n"
                 "#EXTINF:-1 tvg-id=\"drama.uk\" group-title=\"UK| DRAMA\",UK: Harbour Drama\n"
                 "http://mock.example.invalid:8080/3.ts\n"
                 "#EXTINF:-1 tvg-id=\"dazn1.de\" group-title=\"DE| SPORT\",DE: DAZN 1\n"
                 "http://mock.example.invalid:8080/4.ts\n");
}

void answerGuide(fake::Tv& tv) {
  std::string xml = "<?xml version=\"1.0\"?>\n<tv>\n";
  time::Seconds hour = time::now() / 3600 * 3600;
  auto stamp = [](time::Seconds t) {
    time::Civil c = time::civilFromSeconds(t);
    char b[32];
    std::snprintf(b, sizeof b, "%04d%02d%02d%02d%02d00 +0000", c.year, c.month, c.day, c.hour, c.minute);
    return std::string(b);
  };
  for (const char* id : {"harbor.us", "skymain.uk", "drama.uk", "dazn1.de"}) {
    xml += std::string("<channel id=\"") + id + "\"><display-name>" + id + "</display-name></channel>\n";
    for (int i = -1; i < 6; ++i)
      xml += "<programme start=\"" + stamp(hour + i * 3600) + "\" stop=\"" + stamp(hour + (i + 1) * 3600) +
             "\" channel=\"" + id + "\"><title>Harbor United v Valley Rovers</title></programme>\n";
  }
  xml += "</tv>\n";
  tv.pool.answer(kGuide, xml);
}

std::string entryNamed(const Store& store, const std::string& name) {
  for (const GuideChannel& g : store.channels()->all)
    if (g.name.find(name) != std::string::npos) return g.id;
  return "";
}

bool listed(const Store& store, const std::string& id) {
  const std::vector<Listing>* l = store.listings()->forChannel(id);
  return l && !l->empty();
}

}  // namespace

TEST_CASE("engine: another country's sports channel is read for Live Events only, and plays") {
  fake::Tv tv;
  answerList(tv);
  answerGuide(tv);
  Store store(tv.host());
  {  // Build 153: these made-up channels aren't on the channel list (Other).
    Settings shown = store.settings();
    shown.showOther = true;
    store.setSettings(shown);
  }
  AccountForm form = fake::m3uForm();
  form.server = kList;
  form.guideUrl = kGuide;
  store.addAccount(form, [](std::string) {});
  fake::settle(store, tv);
  store.setGroupHidden("DE| SPORT", true);
  REQUIRE(store.refreshListings());
  fake::settle(store, tv);
  fake::settle(store, tv);

  std::string home = entryNamed(store, "Harbor Sports Network");
  std::string sky = entryNamed(store, "Sky Sports Main Event");
  std::string drama = entryNamed(store, "Harbour Drama");
  std::string dazn = entryNamed(store, "DAZN 1");
  REQUIRE_FALSE(home.empty());
  REQUIRE_FALSE(sky.empty());
  REQUIRE_FALSE(drama.empty());
  REQUIRE_FALSE(dazn.empty());
  // The provider's guide keeps the sports brand's listings (and the US
  // channel's), not another country's other channels', nor a hidden group's.
  CHECK(listed(store, home));
  CHECK(listed(store, sky));
  CHECK_FALSE(listed(store, drama));
  CHECK_FALSE(listed(store, dazn));
  // Live Events only: not browsable, not in the browse list or My Lineup.
  auto channels = store.channels();
  const GuideChannel* row = channels->find(sky);
  REQUIRE(row);
  CHECK_FALSE(row->browsable);
  CHECK_FALSE(row->inLineup);
  int at = channels->byId.at(sky);
  CHECK(std::find(channels->browse.begin(), channels->browse.end(), at) == channels->browse.end());
  CHECK(std::find(channels->lineup.begin(), channels->lineup.end(), at) == channels->lineup.end());
  CHECK_FALSE(store.lineupPrefs().countries.count("UK"));
  // It plays: its row resolves to its own feed.
  std::vector<Feed> feeds = store.feeds(sky);
  REQUIRE(feeds.size() == 1);
  CHECK_FALSE(feeds.front().channelId.empty());
}
