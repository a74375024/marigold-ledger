// Build 153: a row's listings chosen by channel identity, through the store.
// A playlist whose guide file gives History's copy another channel's
// schedule (a station's guide id): the row shows nothing rather than it,
// "Listings look wrong?" still offers it, the viewer's pick wins and is
// saved, and a pick whose schedule is gone is dropped. Discovery's own
// guide id shows as before. Names of real networks; made-up programmes.
#include <doctest.h>

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "core/catalog/reference.hpp"
#include "core/engine/store.hpp"
#include "core/util/time.hpp"
#include "engine/loaded_store.hpp"

using namespace marigold;
using namespace marigold::engine;

namespace {

constexpr const char* kList = "http://mock.example.invalid:8080/identity.m3u";
constexpr const char* kGuide = "http://mock.example.invalid:8080/identity.xml";

void answer(fake::Tv& tv) {
  tv.pool.answer(kList,
                 "#EXTM3U\n"
                 "#EXTINF:-1 tvg-id=\"nyclifewnye.us\" group-title=\"US Entertainment\",History\n"
                 "http://mock.example.invalid:8080/a.m3u8\n"
                 "#EXTINF:-1 tvg-id=\"DiscoveryChannel.us\" group-title=\"US Entertainment\",Discovery Channel\n"
                 "http://mock.example.invalid:8080/b.m3u8\n");
  time::Seconds hour = time::now() / 3600 * 3600;
  auto stamp = [](time::Seconds t) {
    time::Civil c = time::civilFromSeconds(t);
    char b[32];
    std::snprintf(b, sizeof b, "%04d%02d%02d%02d%02d00 +0000", c.year, c.month, c.day, c.hour, c.minute);
    return std::string(b);
  };
  std::string xml =
      "<?xml version=\"1.0\"?>\n<tv>\n"
      "<channel id=\"nyclifewnye.us\"><display-name>NYC Life</display-name></channel>\n"
      "<channel id=\"DiscoveryChannel.us\"><display-name>Discovery Channel</display-name></channel>\n";
  for (int i = -1; i < 12; ++i) {
    std::string when = "start=\"" + stamp(hour + i * 3600) + "\" stop=\"" + stamp(hour + (i + 1) * 3600) + "\"";
    xml += "<programme " + when + " channel=\"nyclifewnye.us\"><title>City Desk " + std::to_string(i + 1) +
           "</title></programme>\n";
    xml += "<programme " + when + " channel=\"DiscoveryChannel.us\"><title>Gold Rush " + std::to_string(i + 1) +
           "</title></programme>\n";
  }
  xml += "</tv>\n";
  tv.pool.answer(kGuide, xml);
}

std::string entryNamed(const Store& store, const std::string& name) {
  for (const GuideChannel& g : store.channels()->all)
    if (g.name == name) return g.id;
  return "";
}

std::string nowOn(const Store& store, const std::string& id) {
  auto li = store.listings();
  const std::vector<Listing>* v = li ? li->forChannel(id) : nullptr;
  if (!v) return "";
  time::Seconds now = time::now();
  for (const Listing& l : *v)
    if (l.isOn(now)) return l.title;
  return "";
}

}  // namespace

TEST_CASE("guide choice in the store: another channel's schedule, the viewer's pick, a pick gone") {
  if (catalog::reference::builtIn().empty()) return;
  fake::Tv tv;
  answer(tv);
  Store store(tv.host());
  AccountForm form = fake::m3uForm();
  form.server = kList;
  form.guideUrl = kGuide;
  store.addAccount(form, [](std::string) {});
  fake::settle(store, tv);
  std::string history = entryNamed(store, "History");
  std::string discovery = entryNamed(store, "Discovery Channel");
  REQUIRE(history == "e|r:history");
  REQUIRE(discovery == "e|r:discovery-channel");
  store.wantListings({history, discovery});
  fake::settle(store, tv);

  CHECK(nowOn(store, discovery).rfind("Gold Rush", 0) == 0);
  // History's guide id is a station's (WNYE, NYC Life): not shown.
  CHECK(nowOn(store, history).empty());
  std::vector<Store::GuideOption> options = store.guideOptions(history);
  REQUIRE(options.size() == 1);
  CHECK(options[0].now.rfind("City Desk", 0) == 0);
  CHECK(options[0].next.rfind("City Desk", 0) == 0);
  CHECK_FALSE(options[0].current);

  // The viewer knows better: it shows, and is saved with the lists.
  store.pickGuide(history, options[0].key);
  fake::settle(store, tv);
  CHECK(store.guidePicked(history));
  CHECK(nowOn(store, history).rfind("City Desk", 0) == 0);
  REQUIRE(store.guideOptions(history).size() == 1);
  CHECK(store.guideOptions(history)[0].current);
  std::string lists = tv.files.read("lists.json").value_or("");
  CHECK(lists.find("\"guidePicks\":{\"e|r:history\":\"" + options[0].key + "\"}") != std::string::npos);

  // A pick of a schedule its source no longer has is dropped.
  std::string source = options[0].key.substr(0, options[0].key.find('|'));
  store.pickGuide(history, source + "|gone.us");
  CHECK(store.guidePicked(history));
  fake::settle(store, tv);
  CHECK_FALSE(store.guidePicked(history));
  CHECK(nowOn(store, history).empty());
}
