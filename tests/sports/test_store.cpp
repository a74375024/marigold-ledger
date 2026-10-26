// The store's Live Events logic: game listings from the guide, provider
// events as matcher input, the rebuild, "feeds moved on", rebuild pacing,
// event backups, every source of an event in backup order, the saved
// result shown at launch, and the source remembered per game.
#include "core/catalog/catalog.hpp"
#include "core/sports/store.hpp"
#include "doctest.h"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::sports;
using namespace marigold::sports::store;
using catalog::EntryInfo;
using catalog::LineupCategory;
using sportsdata::feed;
using sportsdata::game;
using sportsdata::source;
using sportsdata::team;
using sportsdata::utc;

namespace {

EntryInfo info(const std::string& id, const std::string& name, LineupCategory category,
               std::optional<std::string> callsign = std::nullopt, std::optional<std::string> market = std::string("US"),
               std::vector<std::string> members = {}) {
  EntryInfo e;
  e.id = id;
  e.name = name;
  e.category = category;
  e.callsign = std::move(callsign);
  e.market = std::move(market);
  e.members = std::move(members);
  return e;
}

Program program(const std::string& title, Seconds start, Seconds end, std::optional<std::string> episode = {},
                std::optional<std::string> overview = {}, bool sports = false) {
  Program p;
  p.title = title;
  p.start = start;
  p.end = end;
  p.episodeTitle = std::move(episode);
  p.overview = std::move(overview);
  p.genres = sports ? kGenreSports : 0;
  return p;
}

std::vector<std::string> ids(const std::vector<EventSource>& sources) {
  std::vector<std::string> out;
  for (const EventSource& s : sources) out.push_back(s.channelID);
  return out;
}

}  // namespace

TEST_CASE("listing channels: sports, local and Spanish ones you have, ranked") {
  std::unordered_map<std::string, EntryInfo> infos;
  infos["e|fox13"] = info("e|fox13", "FOX 13 (KSTU)", LineupCategory::local, std::string("KSTU"));
  infos["e|news"] = info("e|news", "Harbor News", LineupCategory::news);
  infos["e|ppv"] = info("e|ppv", "Fight PPV", LineupCategory::sports, std::nullopt, std::nullopt);
  infos["e|uni"] = info("e|uni", "Canal Uno", LineupCategory::local, std::nullopt, std::string("US-ES"));
  StationMarkets markets;
  markets.home = {"KSTU"};
  auto list = listingChannels({"e|fox13"}, {"e|news", "e|uni"}, infos, markets);
  REQUIRE(list.size() == 3);
  CHECK(list[0].id == "e|fox13");
  CHECK(list[0].inLineup);
  CHECK(list[0].rank == station_rank::lineupHome);
  CHECK(list[1].id == "e|ppv");  // sports of unknown country, though not in the browse list
  CHECK(list[2].id == "e|uni");
  CHECK(list[2].spanish);
  CHECK(list[2].rank == station_rank::elsewhere);
}

TEST_CASE("a programme as a game listing: two sides, or a sports title with a venue") {
  ListingChannel ch{"e|fox13", true, false, station_rank::lineupHome};
  Seconds s = utc(2026, 10, 25, 17, 0), e = s + 3 * 3600;
  auto game1 = listingFor(ch, "NFL Football", std::string("Team Alpha at Harbor City Gulls"), std::nullopt, true, s, e);
  REQUIRE(game1);
  CHECK(game1->title == "NFL Football Team Alpha at Harbor City Gulls");
  CHECK(game1->rank == station_rank::lineupHome);
  CHECK(game1->inLineup);
  auto venue = listingFor(ch, "NBA Basketball", std::nullopt,
                          std::string("From Otter Dome in River Town. The Otters host the Hawks."), false, s, e);
  REQUIRE(venue);
  CHECK(venue->venue == "otter dome");
  CHECK(venue->city == "river town");
  CHECK_FALSE(venue->detail);
  std::string longText(200, 'a');
  auto described = listingFor(ch, "College Football", std::nullopt, "Teams meet. " + longText, false, s, e);
  REQUIRE(described);
  CHECK(described->detail->size() == 120);
  CHECK_FALSE(listingFor(ch, "Evening News", std::nullopt, std::string("Local headlines and weather."), false, s, e));
  CHECK_FALSE(listingFor(ch, "NBA Basketball", std::nullopt, std::string("Short"), true, s, e));
}

TEST_CASE("extracting listings: the guide's window, then saved listings past it") {
  Seconds now = utc(2026, 10, 25, 15, 0);
  ListingChannel ch{"e|fox13", true, false, 0};
  std::vector<Program> programs = {
      program("Old Game: Team A vs Team B", now - 7 * 3600, now - 6 * 3600),
      program("Team Alpha at Harbor City Gulls", now + 2 * 3600, now + 5 * 3600),
      program("Morning News", now, now + 3600),
  };
  std::vector<guide::RawProgram> outside = {
      {static_cast<int>((now + 4 * 3600) / 60), 60, "Inside the guide: A vs B", std::nullopt, std::nullopt, 0},
      {static_cast<int>((now + 30 * 3600) / 60), 180, "Lake Pikes vs Mountain Goats", std::nullopt, std::nullopt, 2},
      {static_cast<int>((now + 60 * 3600) / 60), 180, "Too Far: A vs B", std::nullopt, std::nullopt, 2},
  };
  std::vector<guide::RawProgram> provider = {
      {static_cast<int>((now + 30 * 3600) / 60), 180, "Same minute: C vs D", std::nullopt, std::nullopt, 2},
      {static_cast<int>((now + 20 * 3600) / 60), 180, "Coast Crabs vs Desert Foxes", std::nullopt, std::nullopt, 2},
  };
  ExtractedListings out = extractListings({{ch, &programs, &outside, &provider}}, now);
  REQUIRE(out.listings.size() == 3);
  CHECK(out.listings[0].title == "Team Alpha at Harbor City Gulls");
  CHECK(out.listings[1].title == "Coast Crabs vs Desert Foxes");
  CHECK(out.listings[2].title == "Lake Pikes vs Mountain Goats");
  CHECK(out.ahead == 2);
  // Nothing at all for a channel: skipped.
  CHECK(extractListings({{ch, nullptr, nullptr, nullptr}}, now).listings.empty());
}

TEST_CASE("when listings are read again") {
  Seconds now = 10'000;
  CHECK(listingsNeedRead(std::nullopt, 1, 1, now));
  CHECK_FALSE(listingsNeedRead(now - 300, 1, 1, now));
  CHECK(listingsNeedRead(now - 700, 1, 1, now));
  CHECK(listingsNeedRead(now - 120, 2, 1, now));        // the guide moved on
  CHECK_FALSE(listingsNeedRead(now - 30, 2, 1, now));   // just read
  CHECK(listingsDue(now - 60, now - 60, now - 30, now) == "quiet");
  CHECK_FALSE(listingsDue(now - 10, now - 300, std::nullopt, now));
  CHECK(listingsDue(now - 10, now - 600, now - 700, now) == "deadline");
  CHECK_FALSE(listingsDue(now - 10, now - 600, now - 100, now));
}

TEST_CASE("provider events: Spanish by market or title; a guessed day from the raw slot text") {
  Seconds now = utc(2026, 10, 25, 15, 0);
  std::vector<catalog::LiveEvent> events = {
      sportsdata::liveEvent("1", "Team Alpha vs Harbor City Gulls", "NFL", now, {"xt|1"}),
      sportsdata::liveEvent("2", "Team Alpha vs Harbor City Gulls (Espa\xC3\xB1ol)", "NFL", now, {"xt|2"}),
      sportsdata::liveEvent("3", "Old slot", "NFL", now, {"xt|3"}, "fam", std::nullopt),
  };
  events[0].market = "US-ES";
  std::unordered_map<std::string, catalog::ChannelTraits> traits;
  traits["xt|3"].eventText = std::string("Team A vs Team B 7:30 PM ET");
  auto out = providerEvents(events, traits, now);
  REQUIRE(out.size() == 3);
  CHECK(out[0].spanish);
  CHECK(out[1].spanish);
  CHECK_FALSE(out[2].spanish);
  CHECK_FALSE(out[0].guessed);
  CHECK(out[2].guessed == catalog::parseEventDetailed("Team A vs Team B 7:30 PM ET", now).guessed);
  CHECK(out[2].guessed);
}

TEST_CASE("the rebuild: network channels, feeds, listings; watchable leagues") {
  Seconds now = utc(2026, 10, 25, 15, 0);
  SportsGame nfl = game("NFL", "NFL", "1", team("1", "Harbor City Gulls", std::string("HCG")),
                        team("2", "Team Alpha", std::string("TMA")), utc(2026, 10, 25, 17, 0), {"FOX"});
  SportsGame f1 = game("F1", "Motorsport", "700|3", team("r", "Harbor GP"), team("r", "Harbor GP"),
                       utc(2026, 10, 25, 19, 0));
  f1.racing = true;
  f1.cardKey = "f1|700|race";
  RebuildInput in;
  in.games = {nfl, f1};
  in.now = now;
  in.zone = time::Zone::usEastern();
  in.channelIndex.byKey["net:FOX"] = {{"e|fox13", true, false, station_rank::lineupHome}};
  in.f1Sources = {source("e|f1chan", EventSource::Kind::guide, false, std::nullopt, "In your channels")};
  in.events = {sportsdata::liveEvent("ev1", "NFL 01: Team Alpha @ Harbor City Gulls", "NFL", nfl.start, {"xt|1"})};
  in.previous = {};
  in.channelExists = [](const std::string&) { return true; };
  auto out = rebuild(in);
  REQUIRE(out);
  const SportsEvent* tile = nullptr;
  for (const SportsEvent& e : out->events) {
    if (e.game && e.game->id == "NFL|1") tile = &e;
  }
  REQUIRE(tile);
  CHECK(ids(tile->sources) == std::vector<std::string>{"e|fox13", "xt|1"});
  CHECK(tile->id == "ev1");
  const SportsEvent* race = nullptr;
  for (const SportsEvent& e : out->events) {
    if (e.game && e.game->id == "F1|700|3") race = &e;
  }
  REQUIRE(race);
  CHECK(ids(race->sources) == std::vector<std::string>{"e|f1chan"});
  CHECK(out->watchableLeagues == std::set<std::string>{"F1", "NFL"});
  CHECK(out->sources.national == 1);
  CHECK(coverageSummary(out->events, out->sources, now).find("2 games in 48 h: 1 on My Lineup") == 0);
}

TEST_CASE("a game only on provider feeds ends when every feed moved on") {
  Seconds now = utc(2026, 10, 25, 19, 0);
  SportsGame g = game("NBA", "NBA", "1", team("1", "River Town Otters"), team("2", "Valley Hawks"), now - 3600);
  SportsEvent before = sportsdata::eventOf(g, {feed("xt|1"), feed("xt|2")});
  SportsEvent after = sportsdata::eventOf(g, {});
  auto exists = [](const std::string&) { return true; };
  int ended = 0;
  auto out = markEndedByFeeds({after}, {before}, true, now, exists, &ended);
  CHECK(out[0].feedsEndedAt == now);
  CHECK(ended == 1);
  CHECK(out[0].state(now + 60) == SportsEvent::State::finished);
  // Kept on the next rebuild.
  auto again = markEndedByFeeds({after}, out, true, now + 600, exists);
  CHECK(again[0].feedsEndedAt == now);
  // Feeds back: not ended.
  CHECK_FALSE(markEndedByFeeds({before}, out, true, now + 600, exists)[0].feedsEndedAt);
  // Too early (45 minutes), a channel still showing it, a feed that's gone,
  // or no event list at all: not ended.
  SportsEvent early = sportsdata::eventOf(game("NBA", "NBA", "1", g.home, g.away, now - 1800), {});
  CHECK_FALSE(markEndedByFeeds({early}, {before}, true, now, exists)[0].feedsEndedAt);
  SportsEvent onChannel = sportsdata::eventOf(g, {source("e|espn", EventSource::Kind::guide)});
  CHECK_FALSE(markEndedByFeeds({onChannel}, {before}, true, now, exists)[0].feedsEndedAt);
  auto gone = [](const std::string& id) { return id != "xt|2"; };
  CHECK_FALSE(markEndedByFeeds({after}, {before}, true, now, gone)[0].feedsEndedAt);
  CHECK_FALSE(markEndedByFeeds({after}, {before}, false, now, exists)[0].feedsEndedAt);
}

TEST_CASE("rebuild pacing: every 6 s during launch, your changes at once, one at a time") {
  RebuildScheduler s;
  auto due = s.request("schedule", 0.3, 100);
  REQUIRE(due);
  CHECK(*due == doctest::Approx(100.3));
  CHECK(s.begin() == "schedule");
  // Asked while running: runs again after.
  CHECK_FALSE(s.request("lineup", 3, 100.5));
  auto again = s.end(101);
  CHECK(again == "lineup");
  // Within the launch window: at least 6 s after the last rebuild.
  due = s.request("lineup", 2, 101);
  REQUIRE(due);
  CHECK(*due == doctest::Approx(107));
  // A sooner due time replaces it; a later one doesn't.
  CHECK_FALSE(s.request("tick", 0.5, 102));
  due = s.request("my sports", 0.3, 102);
  REQUIRE(due);
  CHECK(*due == doctest::Approx(102.3));
  CHECK(s.begin() == "my sports");
  CHECK_FALSE(s.end(103));
  // After the launch window, no pacing.
  due = s.request("tick", 0.5, 200);
  REQUIRE(due);
  CHECK(*due == doctest::Approx(200.5));
}

TEST_CASE("fingerprints and channel index keys change with their inputs") {
  Fingerprint f;
  f.eventsRevision = 1;
  std::string a = fingerprint(f, 1800 * 10);
  CHECK(a == fingerprint(f, 1800 * 10 + 900));  // the same half hour
  CHECK(a != fingerprint(f, 1800 * 11));
  f.hidden = {"Hidden Group"};
  CHECK(a != fingerprint(f, 1800 * 10));
  StationMarkets m;
  std::string k1 = channelIndexKey(1, {"e|a"}, m, {}, {});
  CHECK(k1 == channelIndexKey(1, {"e|a"}, m, {}, {}));
  CHECK(k1 != channelIndexKey(1, {"e|b"}, m, {}, {}));
  m.home = {"KSTU"};
  CHECK(k1 != channelIndexKey(1, {"e|a"}, m, {}, {}));
  CHECK(k1 != channelIndexKey(1, {"e|a"}, StationMarkets(), {}, {{"NFL", "NFL", false, {team("1", "A")}}}));
}

TEST_CASE("event backups: a game on now on a channel lends it its feeds") {
  Seconds now = utc(2026, 10, 25, 18, 0);
  SportsGame live = game("NFL", "NFL", "1", team("1", "A"), team("2", "B"), now - 3600);
  SportsGame later = game("NFL", "NFL", "2", team("3", "C"), team("4", "D"), now + 3 * 3600);
  std::vector<SportsEvent> events = {
      sportsdata::eventOf(later, {source("e|fox13", EventSource::Kind::lineup), feed("xt|9")}),
      sportsdata::eventOf(live, {source("e|fox13", EventSource::Kind::lineup), feed("xt|1"), feed("xt|2")}),
  };
  auto index = backupIndex(events);
  CHECK(index.at("e|fox13") == std::vector<size_t>{0, 1});
  CHECK_FALSE(index.count("xt|1"));
  CHECK(eventBackupFeeds("e|fox13", events, index, now) == std::vector<std::string>{"xt|1", "xt|2"});
  CHECK(eventBackupFeeds("e|other", events, index, now).empty());
  // Within 15 minutes of its start the upcoming game lends its feeds too.
  CHECK(eventBackupFeeds("e|fox13", {events[0]}, backupIndex({events[0]}), later.start - 600) ==
        std::vector<std::string>{"xt|9"});
}

TEST_CASE("feed order: quality, then reliability with a 0.25 margin; pinned versions first") {
  std::map<std::string, double> reliability = {{"a", 0.5}, {"b", 0.6}, {"c", 0.2}, {"d", 0.5}};
  Reliability r = [&](const std::string& id) { return reliability.at(id); };
  // Within the margin the given order holds; clearly worse moves back.
  CHECK(orderedByReliability({"a", "b", "c", "d"}, r) == std::vector<std::string>{"a", "b", "d", "c"});
  std::map<std::string, int> quality = {{"a", 1}, {"b", 3}, {"c", 3}, {"d", 0}};
  auto q = [&](const std::string& id) { return quality.at(id); };
  CHECK(bestFeeds({"a", "b", "c", "d"}, q, r) == std::vector<std::string>{"b", "a", "d", "c"});
  EntryInfo entry = info("e|1", "Channel", LineupCategory::sports, std::nullopt, std::string("US"), {"a", "b", "gone", "d"});
  auto exists = [](const std::string& id) { return id != "gone"; };
  CHECK(baseVersions("e|1", &entry, std::string("d"), r, exists) == std::vector<std::string>{"d", "a", "b"});
  CHECK(baseVersions("raw|1", nullptr, std::nullopt, r, exists) == std::vector<std::string>{"raw|1"});
}

namespace {

Resolver resolver() {
  Resolver r;
  r.sourceChannel = [](const EventSource& s) -> std::optional<ChannelRef> {
    if (s.channelID == "gone") return std::nullopt;
    return ChannelRef{s.channelID, "Name of " + s.channelID};
  };
  r.baseVersions = [](const std::string& id) -> std::vector<std::string> {
    if (id == "e|fox13") return {"xt|fox13a", "xt|fox13b"};
    if (id == "e|team") return {"xt|team"};
    return {id};
  };
  r.bestFeeds = [](const std::vector<std::string>& feeds) {
    std::vector<std::string> out(feeds.rbegin(), feeds.rend());  // pretend the last is best
    return out;
  };
  r.isEntry = [](const std::string& id) { return id.rfind("e|", 0) == 0; };
  return r;
}

}  // namespace

TEST_CASE("every source of an event in backup order") {
  SportsGame g = game("NFL", "NFL", "1", team("1", "A"), team("2", "B"), utc(2026, 10, 25, 17, 0));
  SportsEvent e = sportsdata::eventOf(g, {source("e|fox13", EventSource::Kind::lineup), feed("xt|1"),
                                          source("e|team", EventSource::Kind::team), feed("xt|2"),
                                          source("gone", EventSource::Kind::guide)});
  // Channels (with their versions), provider feeds best first, the team's channel last.
  CHECK(eventCandidates(e, std::nullopt, resolver()) ==
        std::vector<std::string>{"xt|fox13a", "xt|fox13b", "xt|2", "xt|1", "xt|team"});
  // A picked source first.
  CHECK(eventCandidates(e, std::string("xt|1"), resolver()) ==
        std::vector<std::string>{"xt|1", "xt|fox13a", "xt|fox13b", "xt|2", "xt|team"});
  // The remembered source leads when nothing is picked.
  WatchPlan plan = watchPlan(e, std::nullopt, std::string("xt|2"), resolver());
  CHECK(plan.candidates.front() == "xt|2");
  CHECK(plan.reason == "the source you were on");
  CHECK_FALSE(plan.entry);
  // A guide channel picked: watch the channel itself.
  plan = watchPlan(e, std::string("e|fox13"), std::string("xt|2"), resolver());
  CHECK(plan.entry == "e|fox13");
  CHECK_FALSE(plan.reason);
  CHECK(plan.candidates.front() == "xt|fox13a");
  // Nothing to play.
  SportsEvent none = sportsdata::eventOf(g, {source("gone", EventSource::Kind::guide)});
  CHECK(watchPlan(none, std::nullopt, std::nullopt, resolver()).empty);
  CHECK(std::string(kNoSourceMessage) == "Nothing in your channels is showing this yet");
  // A reminder points at the best channel, for the game's usual length.
  auto target = reminderTarget(e, resolver());
  REQUIRE(target);
  CHECK(target->channelID == "e|fox13");
  CHECK(target->channelName == "Name of e|fox13");
  CHECK(target->end - target->start == e.expectedLength());
  CHECK_FALSE(reminderTarget(none, resolver()));
}

TEST_CASE("the saved result: watchable games, checked against today's feeds") {
  Seconds now = utc(2026, 10, 25, 16, 0);
  SportsGame g1 = game("NFL", "NFL", "1", team("1", "Harbor City Gulls"), team("2", "Team Alpha"), now + 3600);
  SportsGame g2 = game("NBA", "NBA", "2", team("3", "River Town Otters"), team("4", "Valley Hawks"), now - 10 * 3600);
  SportsGame g3 = game("NHL", "NHL", "3", team("5", "Lake Pikes"), team("6", "Mountain Goats"), now + 7200);
  std::vector<SportsEvent> events = {
      sportsdata::eventOf(g1, {source("e|fox13", EventSource::Kind::lineup, false, 0), feed("xt|1"), feed("xt|2")}),
      sportsdata::eventOf(g2, {feed("xt|3")}),
      sportsdata::eventOf(g3, {}),
      sportsdata::plainEvent("p1", "Darts Night", "Darts", now, {feed("xt|4")}),
  };
  std::vector<catalog::LiveEvent> raw = {
      sportsdata::liveEvent("a", "NFL: Team Alpha @ Harbor City Gulls", "NFL", now, {"xt|1"}),
      sportsdata::liveEvent("b", "NFL: Harbor City Gulls vs Team Alpha", "NFL", now, {"xt|2"}),
      sportsdata::liveEvent("c", "River Town Otters vs Valley Hawks", "NBA", now, {"xt|3"}),
  };
  matcher::GuideListing listing;
  listing.entryID = "e|fox13";
  listing.title = "Team Alpha at Harbor City Gulls";
  listing.start = now + 3600;
  listing.end = now + 4 * 3600;
  std::string last;
  // Listings not read yet and none at hand: a weaker result isn't saved.
  CHECK_FALSE(resultToSave(events, raw, {}, false, now, last));
  auto saved = resultToSave(events, raw, {listing}, true, now, last);
  REQUIRE(saved);
  REQUIRE(saved->events.size() == 2);  // games with a source only
  CHECK(saved->feedKeys.at("xt|1") == "harbor city gulls|team alpha");
  CHECK(saved->feedKeys.at("xt|2") == "harbor city gulls|team alpha");
  CHECK(saved->feedKeys.at("xt|3") == "river town otters|valley hawks");
  // Not the same one twice.
  CHECK_FALSE(resultToSave(events, raw, {listing}, true, now, last));

  auto back = savedResultFromJson(toJson(*saved));
  REQUIRE(back);
  CHECK(back->events == saved->events);
  CHECK(back->feedKeys == saved->feedKeys);
  REQUIRE(back->listings);
  CHECK(*back->listings == std::vector<matcher::GuideListing>{listing});

  // Next launch: slot xt|2 now shows another matchup; xt|1 the same.
  std::vector<catalog::LiveEvent> today = {
      sportsdata::liveEvent("a", "NFL: Harbor City Gulls vs Team Alpha", "NFL", now, {"xt|1"}),
      sportsdata::liveEvent("b", "NFL: Lake Pikes vs Mountain Goats", "NFL", now, {"xt|2"}),
  };
  auto restored = restore(*back, today, now + 600);
  REQUIRE(restored);
  REQUIRE(restored->events.size() == 1);  // the NBA game finished
  CHECK(ids(restored->events[0].sources) == std::vector<std::string>{"e|fox13", "xt|1"});
  CHECK(restored->dropped == 1);
  CHECK(restored->ageMinutes == 10);
  CHECK(restored->listings.size() == 1);
  // Before the channel list loads, provider feeds can't be checked: dropped.
  CHECK(ids(restore(*back, {}, now + 600)->events[0].sources) == std::vector<std::string>{"e|fox13"});
  // Fills in only while no game is shown; non-game tiles are kept.
  std::vector<SportsEvent> current = {sportsdata::plainEvent("p1", "Darts Night", "Darts", now, {feed("xt|4")})};
  auto filled = fillIn(restored->events, current, true, [](const EventSource& s) { return s.channelID != "xt|1"; });
  REQUIRE(filled);
  REQUIRE(filled->size() == 2);
  CHECK(ids((*filled)[0].sources) == std::vector<std::string>{"e|fox13"});
  CHECK((*filled)[1].id == "p1");
  CHECK_FALSE(fillIn(restored->events, events, true, nullptr));
  SavedResult future = *back;
  future.version = 2;
  CHECK_FALSE(restore(future, today, now));
  CHECK_FALSE(savedResultFromJson("{\"version\":1}"));
}

TEST_CASE("the source remembered per game: 6 hours, 40 games") {
  Seconds now = utc(2026, 10, 25, 18, 0);
  SportsGame g = game("NFL", "NFL", "1", team("1", "A"), team("2", "B"), now);
  SportsEvent e = sportsdata::eventOf(g);
  e.id = "tile-id";
  CHECK(SourceMemory::key(e) == "NFL|1");
  SourceMemory memory;
  CHECK(memory.remember(e, "xt|2", now));
  CHECK(memory.remembered(e, now + 3600) == "xt|2");
  CHECK_FALSE(memory.remembered(e, now + 6 * 3600));
  // The same source within a minute: nothing to write.
  CHECK_FALSE(memory.remember(e, "xt|2", now + 30));
  CHECK(memory.remember(e, "xt|3", now + 30));
  // Events without a game go by their tile id.
  SportsEvent plain = sportsdata::plainEvent("p1", "Darts Night", "Darts", now);
  CHECK(SourceMemory::key(plain) == "p1");
  // At most 40 games: the oldest go.
  for (int i = 0; i < 45; ++i) {
    SportsEvent other = sportsdata::plainEvent("p" + std::to_string(i + 10), "Event", "Other", now);
    memory.remember(other, "xt|x", now + 100 + i);
  }
  CHECK(memory.stored().size() == SourceMemory::kCap);
  CHECK_FALSE(memory.remembered(e, now + 200));  // the oldest
  CHECK(memory.stored().at("p54") == "xt|x|" + std::to_string(now + 144));
  // Entries past their life are dropped on the next write.
  SourceMemory loaded(SourceMemory::fromJson(memory.toJson()));
  CHECK(loaded.stored() == memory.stored());
  CHECK(loaded.remember(e, "xt|9", now + 7 * 3600));
  CHECK(loaded.stored().size() == 1);
  CHECK(SourceMemory::fromJson("[]").empty());
  // What to remember when leaving.
  CHECK(feedToRemember(true, std::string("xt|1"), std::string("xt|2"), {"xt|1", "xt|2"}) == "xt|1");
  CHECK(feedToRemember(false, std::string("xt|1"), std::string("xt|2"), {"xt|1", "xt|2"}) == "xt|2");
  CHECK_FALSE(feedToRemember(false, std::string("xt|1"), std::string("xt|7"), {"xt|1", "xt|2"}));
}
