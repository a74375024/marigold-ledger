// Search as you type (shared by the Search screen and the player's search,
// Build 132): every letter looks through everything but the shows, the
// shows once typing pauses, and what a query found stays for the pause's
// pass and a refresh. Made-up channels and shows.
#include <doctest.h>

#include <memory>
#include <string>
#include <vector>

#include "core/engine/search.hpp"
#include "core/engine/search_live.hpp"

using namespace marigold;
using namespace marigold::engine;
namespace se = marigold::engine::search;

namespace {

constexpr time::Seconds kNow = 1'800'000'000;

se::ChannelEntry channel(const std::string& id, const std::string& name, const std::string& number) {
  se::ChannelEntry c;
  c.id = id;
  c.name = se::fold(name);
  c.number = number;
  return c;
}

Listing show(const std::string& title, time::Seconds start, time::Seconds end) {
  Listing l;
  l.title = title;
  l.start = start;
  l.end = end;
  return l;
}

std::shared_ptr<const se::Index> tv() {
  auto index = std::make_shared<se::Index>();
  auto listings = std::make_shared<Listings>();
  index->channels = {channel("pulse", "Pulse Sports", "340"), channel("pulse2", "Pulse Sports 2", "341"),
                     channel("harbor", "Harbor One", "12")};
  listings->byChannel["pulse"] = std::make_shared<const std::vector<Listing>>(
      std::vector<Listing>{show("Pulse Nightly", kNow + 3600, kNow + 5400), show("Quiz Night", kNow - 600, kNow + 1200)});
  listings->byChannel["harbor"] = std::make_shared<const std::vector<Listing>>(
      std::vector<Listing>{show("The Harbor Line", kNow - 600, kNow + 1800)});
  index->listings = listings;
  for (const auto& [id, list] : listings->byChannel)
    for (const Listing& l : *list) index->shows[se::fold(l.title)].push_back({id, &l});
  index->finish();
  return index;
}

se::Options options() {
  se::Options o;
  o.now = kNow;
  return o;
}

std::vector<std::string> ids(const std::vector<se::Item>& items) {
  std::vector<std::string> out;
  for (const se::Item& i : items) out.push_back(i.id);
  return out;
}

}  // namespace

TEST_CASE("search live: each letter without the shows, then the shows once typing pauses") {
  auto index = tv();
  se::Live live;
  se::Query q = se::parseFor(index, "quiz", false);
  se::Live::Pass p = live.run(index, q, options(), false, false, 1000);
  CHECK(p.found);
  CHECK_FALSE(p.shows);
  CHECK_FALSE(live.showsIn());
  CHECK(live.showsDue() == 1000 + se::kShowsDelayMs);
  // No show yet (the best one would be the top result).
  CHECK(live.results().shows.empty());
  CHECK_FALSE((live.results().top && live.results().top->kind == se::Kind::Show));
  // Another letter before the pause: the shows stay due when they were.
  live.run(index, se::parseFor(index, "quiz ", false), options(), false, false, 1200);
  CHECK(live.showsDue() == 1000 + se::kShowsDelayMs);
  // A new query typed: its shows wait for a pause of its own.
  live.typed();
  CHECK(live.showsDue() == 0);
  live.run(index, q, options(), false, false, 1300);
  CHECK(live.showsDue() == 1300 + se::kShowsDelayMs);
  // The pause's pass keeps what the letter found and looks through the shows.
  p = live.run(index, q, options(), true, true, 1750);
  CHECK_FALSE(p.found);
  CHECK(p.shows);
  CHECK(live.showsIn());
  CHECK(live.showsDue() == 0);
  se::Results whole = se::run(*index, q, options());
  CHECK(ids(live.results().shows) == ids(whole.shows));
  REQUIRE(live.results().top);
  CHECK(live.results().top->kind == se::Kind::Show);
  CHECK(live.results().top->name == "Quiz Night");
  // A refresh with the same query and index looks through nothing again.
  p = live.run(index, q, options(), true, true, 5000);
  CHECK_FALSE(p.found);
  CHECK_FALSE(p.shows);
  REQUIRE(live.results().top);
  CHECK(live.results().top->name == "Quiz Night");
}

TEST_CASE("search live: the same results as run(), in steps") {
  auto index = tv();
  for (const char* text : {"p", "pu", "pulse", "pulse 2", "harbor", "341", "night"}) {
    se::Live live;
    se::Query q = se::parseFor(index, text, false);
    live.run(index, q, options(), false, false, 0);
    live.run(index, q, options(), true, true, 500);
    se::Results r = se::run(*index, q, options());
    CHECK(ids(live.results().channels) == ids(r.channels));
    CHECK(ids(live.results().shows) == ids(r.shows));
    CHECK(live.results().top.has_value() == r.top.has_value());
    if (r.top && live.results().top) CHECK(live.results().top->id == r.top->id);
  }
}

TEST_CASE("search live: one character has its shows at once; a new index looks again") {
  auto index = tv();
  se::Live live;
  live.run(index, se::parseFor(index, "p", false), options(), false, false, 0);
  CHECK(live.showsIn());
  CHECK(live.showsDue() == 0);
  se::Query q = se::parseFor(index, "harbor", false);
  live.run(index, q, options(), true, false, 0);
  // The store made the index again: kept results don't count.
  auto again = tv();
  se::Live::Pass p = live.run(again, q, options(), true, true, 100);
  CHECK(p.found);
  CHECK(p.shows);
  CHECK(live.index().get() == again.get());
  // Scope changed: looked through again too.
  se::Options scoped = options();
  scoped.scoped = true;
  CHECK(live.run(again, q, scoped, true, true, 200).found);
}

TEST_CASE("search live: a query that is a title stays as typed") {
  auto index = tv();
  CHECK(se::parseFor(index, "The Harbor Line", false).text == "the harbor line");
  CHECK(se::parseFor(index, "harbor", true).exactOnly);
  CHECK(se::sameQuery(se::parseFor(index, "pulse", false), se::parseFor(index, "Pulse", false)));
  CHECK_FALSE(se::sameQuery(se::parseFor(index, "pulse", false), se::parseFor(index, "pulse", true)));
  // No index yet: plain parsing.
  CHECK(se::parseFor(nullptr, "pulse", false).text == "pulse");
}
