// Player photos: the name key, the search request, picking the right player
// from the answer (tennis or motorsport, cut-out first, https only), and the
// budget: singles players only, one search each (kept 90 days, a miss a
// week), 6 s between searches, the daily cap, a 30-minute pause after a
// failure, saving as it goes, and old entries dropped. Players are invented.
#include "core/sports/player_photos.hpp"
#include "doctest.h"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::sports;
using namespace marigold::sports::player_photos;
using sportsdata::utc;

namespace {

const Seconds kNow = utc(2026, 10, 24, 18, 0);
const time::Zone kZone = time::Zone::utc();

std::string answer(const std::string& name, const std::string& sport, const std::string& cutout,
                   const std::string& thumb = "") {
  return R"({"player": [{"strPlayer": ")" + name + R"(", "strSport": ")" + sport + R"(", "strCutout": )" +
         (cutout.empty() ? "null" : "\"" + cutout + "\"") + R"(, "strThumb": ")" + thumb + R"("}]})";
}

}  // namespace

TEST_CASE("player photo key, search and answer") {
  CHECK(key("Player \xC3\x81lpha-Beta ") == "player alpha beta");
  CHECK(key("PLAYER ALPHA") == "player alpha");
  sources::Request r = searchRequest("Player \xC3\x81lpha O'Beta");
  CHECK(r.url == "https://www.thesportsdb.com/api/v1/json/123/searchplayers.php?p=Player_Alpha_O%27Beta");
  CHECK(r.timeoutSeconds == 15);

  std::string wanted = key("Player Alpha");
  CHECK(parse(answer("Player Alpha", "Tennis", "https://example.invalid/a-cut.png", "https://example.invalid/a.png"),
              wanted) == std::string("https://example.invalid/a-cut.png"));
  CHECK(parse(answer("Player Alpha", "Motorsport", "", "https://example.invalid/a.png"), wanted) ==
        std::string("https://example.invalid/a.png"));  // no cut-out: the photo
  CHECK_FALSE(parse(answer("Player Alpha", "Soccer", "https://example.invalid/a.png"), wanted));
  CHECK_FALSE(parse(answer("Player Alphonse", "Tennis", "https://example.invalid/a.png"), wanted));
  CHECK_FALSE(parse(answer("Player Alpha", "Tennis", "http://example.invalid/a.png"), wanted));
  CHECK_FALSE(parse(R"({"player": null})", wanted));
  CHECK_FALSE(parse("not json", wanted));
  CHECK(parse(answer("Player \xC3\x81lpha", "Tennis", "https://example.invalid/a.png"), wanted));

  CHECK(PlayerPhotos::smallUrl("https://example.invalid/a.png") == "https://example.invalid/a.png/small");
  CHECK(PlayerPhotos::smallUrl("https://example.invalid/a/") == "https://example.invalid/a/small");
  CHECK(PlayerPhotos::smallUrl("https://example.invalid/a.png?v=2") == "https://example.invalid/a.png/small?v=2");
}

TEST_CASE("searching: one at a time, 6 s apart, saved") {
  PlayerPhotos photos;
  CHECK(photos.request({"Player Alpha", "Player Bravo", "Solo", "Player Alpha", "Pair One / Pair Two"}, kNow));
  CHECK(photos.queued() == 3);  // singles players only (a name with a space), once each
  CHECK(photos.draining());
  CHECK_FALSE(photos.request({"Player Charlie"}, kNow));  // queued; the drain running picks it up
  CHECK(photos.queued() == 4);

  auto first = photos.nextRequest(kNow, kZone);
  REQUIRE(first);
  CHECK(first->name == "Player Alpha");
  CHECK(first->waitSeconds == 0);
  CHECK_FALSE(photos.nextRequest(kNow, kZone));  // waiting for the answer
  photos.onResponse(200, answer("Player Alpha", "Tennis", "https://example.invalid/a.png"), kNow);
  CHECK(photos.photo("player alpha") == std::string("https://example.invalid/a.png"));
  CHECK(photos.revision() == 1);

  auto second = photos.nextRequest(kNow + 1, kZone);
  REQUIRE(second);
  CHECK(second->name == "Player Bravo");
  CHECK(second->waitSeconds == 5);  // 6 s after the first
  photos.onResponse(200, R"({"player": null})", kNow + 6);  // no photo: a miss
  CHECK_FALSE(photos.photo("Player Bravo"));
  CHECK(photos.revision() == 1);

  auto third = photos.nextRequest(kNow + 20, kZone);
  REQUIRE(third);
  CHECK(third->name == "Pair One / Pair Two");
  CHECK(third->waitSeconds == 0);
  photos.onResponse(200, "{}", kNow + 20);
  REQUIRE(photos.nextRequest(kNow + 30, kZone));
  photos.onResponse(200, "{}", kNow + 30);
  CHECK_FALSE(photos.nextRequest(kNow + 40, kZone));  // done
  CHECK_FALSE(photos.draining());
  CHECK(photos.saved().requestCount == 4);

  auto saved = photos.persist(kNow + 40);
  REQUIRE(saved);
  CHECK(saved->entries.size() == 4);
  CHECK_FALSE(photos.persist(kNow + 40));  // nothing new

  // Kept 90 days; a miss is tried again after a week.
  CHECK_FALSE(photos.request({"Player Alpha", "Player Bravo"}, kNow + 86'400));
  CHECK(photos.request({"Player Alpha", "Player Bravo"}, kNow + kMissLife + 7));
  CHECK(photos.queued() == 1);
  CHECK(photos.nextRequest(kNow + kMissLife + 7, kZone)->name == "Player Bravo");

  // Read back.
  auto back = savedFromJson(toJson(*saved));
  REQUIRE(back);
  PlayerPhotos again(*back);
  CHECK(again.photo("Player Alpha") == std::string("https://example.invalid/a.png"));
  CHECK(toJson(again.saved()) == toJson(*saved));
  CHECK(again.photo("PLAYER ALPHA") == std::string("https://example.invalid/a.png"));
  CHECK_FALSE(again.photo("Player Bravo"));
  CHECK_FALSE(savedFromJson("[]"));
}

TEST_CASE("a failure pauses 30 minutes and keeps the name; the daily cap waits for tomorrow") {
  PlayerPhotos photos;
  REQUIRE(photos.request({"Player Alpha"}, kNow));
  REQUIRE(photos.nextRequest(kNow, kZone));
  photos.onResponse(429, "", kNow);
  CHECK_FALSE(photos.draining());
  CHECK(photos.queued() == 1);
  CHECK(photos.pausedUntil() == kNow + kFailurePause);
  CHECK_FALSE(photos.request({"Player Alpha"}, kNow + 60));
  CHECK(photos.request({}, kNow + kFailurePause));
  CHECK(photos.nextRequest(kNow + kFailurePause, kZone)->name == "Player Alpha");

  Saved full;
  full.requestDay = "2026-10-24";
  full.requestCount = kDailyCap;
  PlayerPhotos capped(full);
  REQUIRE(capped.request({"Player Alpha"}, kNow));
  CHECK_FALSE(capped.nextRequest(kNow, kZone));
  CHECK(capped.pausedUntil() == utc(2026, 10, 25, 0, 0));
  CHECK_FALSE(capped.request({"Player Bravo"}, kNow + 3600));
  CHECK(capped.request({}, utc(2026, 10, 25, 0, 0)));
  auto r = capped.nextRequest(utc(2026, 10, 25, 0, 0), kZone);
  REQUIRE(r);
  CHECK(capped.saved().requestCount == 1);
}

TEST_CASE("saving as it goes, and old entries dropped") {
  PlayerPhotos photos;
  std::vector<std::string> names;
  for (int i = 0; i < 12; ++i) names.push_back("Player " + std::string(1, static_cast<char>('A' + i)));
  REQUIRE(photos.request(names, kNow));
  int writes = 0;
  Seconds t = kNow;
  while (auto r = photos.nextRequest(t, kZone)) {
    t += r->waitSeconds;
    photos.onResponse(200, "{}", t);
    if (photos.persistDue()) {
      REQUIRE(photos.persist(t));
      writes += 1;
    }
  }
  CHECK(writes == 1);  // after the 10th
  CHECK(photos.persist(t));

  Saved old;
  old.entries["player old"] = Entry{"https://example.invalid/o.png", kNow - kKeep - 1};
  old.entries["player new"] = Entry{"https://example.invalid/n.png", kNow - 86'400};
  PlayerPhotos keep(old);
  REQUIRE(keep.request({"Player Zulu"}, kNow));
  REQUIRE(keep.nextRequest(kNow, kZone));
  keep.onResponse(200, "{}", kNow);
  auto saved = keep.persist(kNow);
  REQUIRE(saved);
  CHECK_FALSE(saved->entries.count("player old"));
  CHECK(saved->entries.count("player new"));
  CHECK(saved->entries.count("player zulu"));
}
