// Build 132: a game's sources in the viewer's languages first (My Lineup
// first whatever its language; stations folded per language; other
// countries' sports channels last and two at most), what the tile names and
// Watch starts on, Where to Watch's sections, the reminder, saved results
// from before (the "spanish" flag), and other countries' sports channels as
// guide listings. Every channel and team is made up.
#include <algorithm>

#include "core/sports/board.hpp"
#include "core/sports/matcher.hpp"
#include "core/sports/network_map.hpp"
#include "core/sports/store.hpp"
#include "core/util/json.hpp"
#include "doctest.h"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::sports;
using catalog::EntryInfo;
using catalog::LineupCategory;
using sportsdata::game;
using sportsdata::team;
using sportsdata::utc;

namespace {

const Seconds kNow = utc(2026, 10, 25, 15, 0);  // Sunday 11 AM Eastern

SportsGame nfl() {
  return game("NFL", "NFL", "1", team("1", "Harbor City Gulls", std::string("HCG")),
              team("2", "Team Alpha", std::string("TMA")), utc(2026, 10, 25, 17, 0), {"FOX"},
              std::string("Gull Field, Harbor City"));
}

matcher::GuideListing listing(const std::string& id, const std::string& lang, int rank, bool inLineup = false,
                              bool abroad = false) {
  matcher::GuideListing l;
  l.entryID = id;
  l.inLineup = inLineup;
  l.lang = lang;
  l.title = "Team Alpha at Harbor City Gulls";
  l.start = nfl().start;
  l.end = nfl().start + 3 * 3600;
  l.rank = rank;
  l.abroad = abroad;
  return l;
}

matcher::ProviderEvent feedEvent(const std::string& id, const std::string& lang) {
  matcher::ProviderEvent e;
  e.id = id;
  e.title = "NFL 01: Team Alpha @ Harbor City Gulls";
  e.league = "NFL";
  e.start = nfl().start;
  e.feeds = {"xt|" + id};
  e.family = "fam";
  e.lang = lang;
  return e;
}

matcher::BuildInput input() {
  matcher::BuildInput in;
  in.games = {nfl()};
  in.now = kNow;
  in.zone = time::Zone::usEastern();
  return in;
}

// The sources' ids, a space apart (a failed check shows them).
std::string ids(const std::vector<EventSource>& sources) {
  std::string out;
  for (const EventSource& s : sources) out += (out.empty() ? "" : " ") + s.channelID;
  return out;
}

const SportsEvent& theGame(const matcher::BuildResult& out) {
  for (const SportsEvent& e : out.events) {
    if (e.game && e.game->id == "NFL|1") return e;
  }
  FAIL("no game");
  return out.events.front();
}

EventSource src(const std::string& id, EventSource::Kind kind, const std::string& lang,
                std::optional<int> rank = station_rank::elsewhere) {
  EventSource s;
  s.channelID = id;
  s.kind = kind;
  s.lang = lang;
  s.rank = kind == EventSource::Kind::provider ? std::nullopt : rank;
  s.detail = kind == EventSource::Kind::provider ? "Provider event channel" : stationDetail(rank.value_or(4));
  return s;
}

board::ChannelFor names() {
  return [](const EventSource& s) -> std::optional<board::ChannelLabel> {
    return board::ChannelLabel{s.channelID, "Channel " + s.channelID, "Channel " + s.channelID, {}};
  };
}

board::Tile tileOf(const std::vector<EventSource>& sources, std::set<std::string> countries = {"US"}) {
  SportsEvent e = sportsdata::eventOf(nfl(), sources);
  board::Input in;
  in.events = {e};
  in.countries = std::move(countries);
  in.now = kNow;
  in.zone = time::Zone::usEastern();
  in.channelFor = names();
  board::Board b = board::build(in);
  REQUIRE(b.tiles.count(e.id));
  return b.tiles.at(e.id);
}

}  // namespace

TEST_CASE("source order: My Lineup, then your language, unknown, another; then kind, rank, order found") {
  matcher::BuildInput in = input();
  in.listings = {listing("e|es", "es", station_rank::elsewhere), listing("e|unknown", "", station_rank::elsewhere),
                 listing("e|en", "en", station_rank::elsewhere), listing("e|lineup-es", "es", station_rank::lineup, true),
                 listing("e|local", "en", station_rank::home)};
  in.events = {feedEvent("es", "es"), feedEvent("en", "en")};
  in.languages = {"en"};
  auto out = matcher::build(in);
  REQUIRE(out);
  // My Lineup's Spanish channel first (you chose it); English channels by
  // rank, your English feed, then unknown, then Spanish.
  CHECK(ids(theGame(*out).sources) == "e|lineup-es e|local e|en xt|en e|unknown e|es xt|es");

  // US Spanish only: Spanish first.
  in.languages = {"es"};
  out = matcher::build(in);
  REQUIRE(out);
  CHECK(ids(theGame(*out).sources) == "e|lineup-es e|es xt|es e|unknown e|local e|en xt|en");

  // Both: one group, by kind and rank.
  in.languages = {"en", "es"};
  out = matcher::build(in);
  REQUIRE(out);
  CHECK(ids(theGame(*out).sources) == "e|lineup-es e|local e|es e|en xt|es xt|en");
  CHECK(theGame(*out).moreStations == 1);  // three stations of yours listed: the unknown one counted
}

TEST_CASE("source order: stations folded per language, so another language's never push yours out") {
  matcher::BuildInput in = input();
  // Four Spanish stations ranked above three English ones.
  for (int i = 0; i < 4; ++i) in.listings.push_back(listing("e|es" + std::to_string(i), "es", station_rank::market));
  for (int i = 0; i < 3; ++i) in.listings.push_back(listing("e|en" + std::to_string(i), "en", station_rank::elsewhere));
  in.listings.push_back(listing("e|en3", "", station_rank::elsewhere));
  in.languages = {"en"};
  auto out = matcher::build(in);
  REQUIRE(out);
  const SportsEvent& g = theGame(*out);
  // Three of yours (English and unknown together), three Spanish; one of
  // each counted, not listed. Nothing else removed.
  CHECK(ids(g.sources) == "e|en0 e|en1 e|en2 e|es0 e|es1 e|es2");
  CHECK(g.moreStations == 2);
  CHECK(out->stats.trimmedStations == 2);
}

TEST_CASE("other countries' sports channels: guide sources ranked last, two at most per game") {
  matcher::BuildInput in = input();
  in.listings = {listing("e|abroad-ar", "ar", station_rank::elsewhere, false, true),
                 listing("e|abroad-en1", "en", station_rank::elsewhere, false, true),
                 listing("e|abroad-en2", "en", station_rank::elsewhere, false, true),
                 listing("e|abroad-de", "de", station_rank::elsewhere, false, true),
                 listing("e|home", "en", station_rank::elsewhere)};
  in.events = {feedEvent("en", "en")};
  in.languages = {"en"};
  auto out = matcher::build(in);
  REQUIRE(out);
  const SportsEvent& g = theGame(*out);
  // Yours first; the two kept are the best two (yours before others).
  CHECK(ids(g.sources) == "e|home e|abroad-en1 e|abroad-en2 xt|en");
  CHECK(g.sources[1].kind == EventSource::Kind::guide);
  CHECK(g.sources[1].rank == station_rank::abroad);
  CHECK(g.sources[1].detail == "From another country");
  CHECK(out->stats.abroadDropped == 2);
  CHECK_FALSE(g.moreStations);

  // Only abroad: still two, and the game has them (yours first).
  in.listings = {listing("e|abroad-ar", "ar", station_rank::elsewhere, false, true),
                 listing("e|abroad-de", "de", station_rank::elsewhere, false, true),
                 listing("e|abroad-en", "en", station_rank::elsewhere, false, true)};
  in.events.clear();
  out = matcher::build(in);
  REQUIRE(out);
  CHECK(ids(theGame(*out).sources) == "e|abroad-en e|abroad-ar");
}

TEST_CASE("other countries' sports channels: only when the listing names the game; never tennis coverage") {
  matcher::BuildInput in = input();
  matcher::GuideListing other = listing("e|abroad", "en", station_rank::elsewhere, false, true);
  other.title = "Harbor Football Weekly";
  in.listings = {other};
  auto out = matcher::build(in);
  REQUIRE(out);
  CHECK(theGame(*out).sources.empty());
  // A tennis channel abroad: its tournament coverage is no source.
  Seconds start = utc(2026, 10, 31, 13, 0);
  SportsGame basel = game("ATP", "Tennis", "b1", team("b1a", "Ann Player"), team("b1b", "Bea Racket"), start + 1800);
  basel.tennis = true;
  basel.tournament = std::string("Swiss Indoors Basel");
  basel.venue = std::string("Center Court");
  matcher::BuildInput tennis;
  tennis.games = {basel};
  tennis.now = start + 1800;
  matcher::GuideListing coverage = listing("e|abroad-tennis", "en", station_rank::elsewhere, false, true);
  coverage.title = "Center Court Live Basel-ATP Semifinals";
  coverage.start = start - 1800;
  coverage.end = start + 4 * 3600;
  coverage.tennisCoverage = true;
  matcher::GuideListing home = coverage;
  home.entryID = "e|tennis";
  home.abroad = false;
  tennis.listings = {coverage, home};
  out = matcher::build(tennis);
  REQUIRE(out);
  REQUIRE(out->events.size() == 1);
  CHECK(ids(out->events[0].sources) == "e|tennis");
}

TEST_CASE("the tile names, and Watch starts on, a source in your languages") {
  // English viewer: the English channel, though a Spanish one is listed first.
  board::Tile t = tileOf({src("e|es", EventSource::Kind::guide, "es"), src("e|en", EventSource::Kind::guide, "en")});
  CHECK(t.channelName == "Channel e|en");
  CHECK(t.footer == "Also in Spanish");
  // Not known counts as yours.
  CHECK(tileOf({src("e|ar", EventSource::Kind::guide, "ar"), src("e|x", EventSource::Kind::guide, "")}).channelName ==
        "Channel e|x");
  // Another language's channel and your language's feed: the feed (no channel named).
  board::Tile feedFirst =
      tileOf({src("e|ar", EventSource::Kind::guide, "ar"), src("xt|1", EventSource::Kind::provider, "en")});
  CHECK_FALSE(feedFirst.channelName);
  CHECK(feedFirst.footerPill == "Event feed");
  // Only another language's channels: the first, saying its language.
  board::Tile only = tileOf({src("e|ar", EventSource::Kind::guide, "ar", station_rank::abroad),
                             src("e|de", EventSource::Kind::guide, "de", station_rank::abroad)});
  CHECK(only.channelName == "Channel e|ar");
  CHECK(only.footer == "In Arabic");
  CHECK(only.watchable);
  // My Lineup's is named whatever its language.
  board::Tile mine = tileOf({src("e|es", EventSource::Kind::lineup, "es", station_rank::lineup),
                             src("e|en", EventSource::Kind::guide, "en")});
  CHECK(mine.channelName == "Channel e|es");
  CHECK(mine.footer == "In your lineup");
  // A Spanish viewer: the Spanish channel, also in English.
  board::Tile spanish = tileOf({src("e|en", EventSource::Kind::guide, "en"), src("e|es", EventSource::Kind::guide, "es")},
                               {"US-ES"});
  CHECK(spanish.channelName == "Channel e|es");
  CHECK(spanish.footer == "Also in English");
  // Nor another country's language on a feed (only English, Spanish and yours).
  CHECK(tileOf({src("e|en", EventSource::Kind::guide, "en"), src("xt|de", EventSource::Kind::provider, "de")}).footer ==
        "+ 1 backup feeds");
  CHECK(tileOf({src("e|en", EventSource::Kind::guide, "en"), src("xt|de", EventSource::Kind::provider, "de")},
               {"US", "DE"})
            .footer == "Also in German");
  // Another country's channel is never an "also".
  CHECK(tileOf({src("e|en", EventSource::Kind::guide, "en"), src("e|ar", EventSource::Kind::guide, "ar", station_rank::abroad),
                src("xt|1", EventSource::Kind::provider, "en")})
            .footer == "+ 1 backup feeds");
}

TEST_CASE("Where to Watch: your channels, a section per other language, then the feeds; no counts") {
  SportsEvent e = sportsdata::eventOf(
      nfl(), {src("e|lineup-ar", EventSource::Kind::lineup, "ar", station_rank::lineup),
              src("e|en", EventSource::Kind::guide, "en"), src("e|x", EventSource::Kind::guide, ""),
              src("xt|en", EventSource::Kind::provider, "en"), src("e|es", EventSource::Kind::guide, "es"),
              src("xt|es", EventSource::Kind::provider, "es"), src("e|ar", EventSource::Kind::guide, "ar", station_rank::abroad),
              src("e|zz", EventSource::Kind::guide, "zz", station_rank::abroad)});
  board::Input in;
  in.events = {e};
  in.countries = {"US"};
  in.now = kNow;
  in.zone = time::Zone::usEastern();
  in.channelFor = names();
  board::Board b = board::build(in);
  CHECK(b.languages == std::set<std::string>{"en"});
  board::WherePanel where = board::wherePanel(b, e, names());
  std::vector<std::string> titles;
  for (const auto& s : where.sections) titles.push_back(s.title);
  CHECK(titles == std::vector<std::string>{"Your channels", "In Spanish", "In Arabic", "In another language",
                                           "Event feeds from your provider"});
  CHECK(where.sections[0].items.size() == 3);  // My Lineup's (Arabic), English, unknown
  CHECK(where.sections[0].items[0].channelID == "e|lineup-ar");
  CHECK(where.sections[1].items.size() == 2);  // the Spanish channel and feed
  CHECK(where.sections[1].items[1].spanish);
  CHECK(where.sections[2].items[0].detail == "From another country");
  CHECK(where.sections[4].items.size() == 1);
  for (const auto& s : where.sections) CHECK(s.title.find_first_of("0123456789") == std::string::npos);
  // The details card's Watch: your language's channel.
  board::DetailCard card = board::detailCard(e, kNow, time::Zone::usEastern(), board::Settings(), names(), {"en"});
  CHECK(card.watchSourceID == "e|lineup-ar");
  SportsEvent foreign = sportsdata::eventOf(nfl(), {src("e|ar", EventSource::Kind::guide, "ar", station_rank::abroad),
                                                    src("xt|en", EventSource::Kind::provider, "en")});
  CHECK_FALSE(board::detailCard(foreign, kNow, time::Zone::usEastern(), board::Settings(), names(), {"en"}).watchLabel);
  foreign.sources.pop_back();
  CHECK(board::detailCard(foreign, kNow, time::Zone::usEastern(), board::Settings(), names(), {"en"}).watchSourceID ==
        "e|ar");
}

TEST_CASE("Watch's backup order and the reminder: your languages' channels, then their feeds, then the rest") {
  store::Resolver r;
  r.sourceChannel = [](const EventSource& s) -> std::optional<store::ChannelRef> {
    return store::ChannelRef{s.channelID, "Name of " + s.channelID};
  };
  r.languages = {"en"};
  SportsEvent e = sportsdata::eventOf(
      nfl(), {src("e|ar", EventSource::Kind::guide, "ar", station_rank::abroad), src("e|en", EventSource::Kind::guide, "en"),
              src("xt|ar", EventSource::Kind::provider, "ar"), src("xt|en", EventSource::Kind::provider, "en"),
              src("e|team-ar", EventSource::Kind::team, "ar"), src("e|team", EventSource::Kind::team, "")});
  CHECK(store::eventCandidates(e, std::nullopt, r) ==
        std::vector<std::string>{"e|en", "xt|en", "e|ar", "xt|ar", "e|team", "e|team-ar"});
  // Picked: first, whatever its language.
  CHECK(store::eventCandidates(e, std::string("e|ar"), r).front() == "e|ar");
  CHECK(store::reminderTarget(e, r)->channelID == "e|en");
  SportsEvent feedsOnly = sportsdata::eventOf(
      nfl(), {src("e|ar", EventSource::Kind::guide, "ar", station_rank::abroad), src("xt|en", EventSource::Kind::provider, "en")});
  CHECK(store::reminderTarget(feedsOnly, r)->channelID == "xt|en");
  CHECK(store::eventCandidates(feedsOnly, std::nullopt, r) == std::vector<std::string>{"xt|en", "e|ar"});
}

TEST_CASE("saved before Build 132: the spanish flag is Spanish; saved now: the language, and spanish as before") {
  json::Value old = json::Value::object();
  old.set("channelID", "e|uni");
  old.set("kind", 1);
  old.set("spanish", true);
  old.set("detail", "In your channels");
  auto read = sourceFromJson(old);
  REQUIRE(read);
  CHECK(read->lang == "es");
  CHECK(read->spanish());
  old.set("spanish", false);
  CHECK(sourceFromJson(old)->lang.empty());  // not known until the next rebuild
  EventSource now = src("e|ar", EventSource::Kind::guide, "ar", station_rank::abroad);
  json::Value saved = toJson(now);
  CHECK(saved["lang"].asString() == "ar");
  CHECK_FALSE(saved["spanish"].asBool());
  CHECK(*sourceFromJson(saved) == now);
  CHECK(toJson(src("e|es", EventSource::Kind::guide, "es"))["spanish"].asBool());

  json::Value oldListing = json::Value::object();
  oldListing.set("entryID", "e|uni");
  oldListing.set("inLineup", false);
  oldListing.set("spanish", true);
  oldListing.set("title", "Team Alpha at Harbor City Gulls");
  oldListing.set("start", static_cast<int64_t>(kNow));
  oldListing.set("end", static_cast<int64_t>(kNow + 3600));
  auto l = matcher::listingFromJson(oldListing);
  REQUIRE(l);
  CHECK(l->lang == "es");
  CHECK_FALSE(l->abroad);
  matcher::GuideListing abroad = listing("e|abroad", "de", station_rank::abroad, false, true);
  CHECK(*matcher::listingFromJson(matcher::toJson(abroad)) == abroad);
}

TEST_CASE("listing channels: other countries' sports channels you have, not hidden, read last") {
  auto entryInfo = [](const std::string& id, const std::string& name, LineupCategory category,
                      std::optional<std::string> market) {
    EntryInfo e;
    e.id = id;
    e.name = name;
    e.category = category;
    e.market = std::move(market);
    return e;
  };
  std::unordered_map<std::string, EntryInfo> infos;
  infos["e|espn"] = entryInfo("e|espn", "Harbor Sports Network", LineupCategory::sports, std::string("US"));
  infos["e|sky"] = entryInfo("e|sky", "Sky Sports Main Event", LineupCategory::international, std::string("UK"));
  infos["e|bein"] = entryInfo("e|bein", "beIN SPORTS 1", LineupCategory::international, std::string("AR"));
  infos["e|hidden"] = entryInfo("e|hidden", "DAZN 1", LineupCategory::international, std::string("DE"));
  infos["e|drama"] = entryInfo("e|drama", "Harbor Drama", LineupCategory::international, std::string("UK"));
  infos["e|news"] = entryInfo("e|news", "Sky Sports News", LineupCategory::international, std::string("UK"));
  infos["e|tsn"] = entryInfo("e|tsn", "TSN 1", LineupCategory::international, std::string("CA"));
  infos["e|unknown"] = entryInfo("e|unknown", "Willow", LineupCategory::sports, std::nullopt);
  auto entry = [](const std::string& id, const std::string& group) {
    Channel c;
    c.id = id;
    c.name = id;
    c.group = group;
    return c;
  };
  std::vector<Channel> entries = {entry("e|espn", "US| SPORTS"),   entry("e|sky", "UK| SPORTS"),
                                  entry("e|bein", "AR| SPORTS"),   entry("e|hidden", "DE| SPORT"),
                                  entry("e|drama", "UK| DRAMA"),   entry("e|news", "UK| SPORTS"),
                                  entry("e|tsn", "CA| SPORTS"),    entry("e|unknown", "SPORTS")};
  // Canada picked: TSN is in the browse list (a brand of a country you picked).
  std::vector<std::string> abroad =
      store::abroadChannels(entries, {}, {"e|espn", "e|tsn"}, infos, {"DE| SPORT"}, "Other");
  CHECK(abroad == std::vector<std::string>{"e|sky", "e|bein"});
  auto list = store::listingChannels({}, {"e|espn", "e|tsn"}, infos, StationMarkets(), abroad);
  std::vector<std::string> got;
  for (const auto& c : list) got.push_back(c.id);
  CHECK(got == std::vector<std::string>{"e|espn", "e|tsn", "e|unknown", "e|sky", "e|bein"});
  CHECK_FALSE(list[1].abroad);  // a country you picked: an ordinary source
  CHECK(list[1].rank == station_rank::elsewhere);
  CHECK(list[1].lang == "en");
  CHECK(list[2].cricketChannel);
  CHECK(list[3].abroad);
  CHECK(list[3].rank == station_rank::abroad);
  CHECK(list[3].lang == "en");
  CHECK_FALSE(list[3].sportsChannel);  // never tennis coverage
  CHECK(list[4].lang == "ar");
  // A listing on it is a game listing like any other, marked so.
  auto l = store::listingFor(list[3], "Premier League: Harbor United v Valley Rovers", std::nullopt, guide::Text(),
                             true, kNow, kNow + 7200);
  REQUIRE(l);
  CHECK(l->abroad);
  CHECK(l->lang == "en");
  CHECK_FALSE(store::listingFor(list[3], "Center Court Live: Harbor-ATP", std::nullopt, guide::Text(), true, kNow,
                                kNow + 7200));
}

TEST_CASE("the rebuild's fingerprint follows the viewer's languages") {
  store::Fingerprint f;
  f.languages = {"en"};
  std::string english = store::fingerprint(f, kNow);
  f.languages = {"en", "es"};
  CHECK(store::fingerprint(f, kNow) != english);
  f.languages = {"en"};
  CHECK(store::fingerprint(f, kNow) == english);
}

TEST_CASE("provider feeds: each feed by its own channel's country in a group of several countries") {
  // A provider group of several countries' sports channels: its events take
  // the first channel's market ("LV"); each feed is its own channel's.
  catalog::LiveEvent mixed = sportsdata::liveEvent("1", "Super Harbor Sport 1", "Other", kNow, {"x|al", "x|none"},
                                                   "grp:sports on air");
  mixed.market = "LV";
  catalog::LiveEvent numbered =
      sportsdata::liveEvent("2", "Team Alpha vs Harbor City Gulls", "NFL", kNow, {"x|us", "x|bare"}, "us (espn+ #)");
  numbered.market = "US";
  std::unordered_map<std::string, catalog::ChannelTraits> traits;
  traits["x|al"].market = "AL";
  traits["x|none"];
  traits["x|us"].market = "US";
  traits["x|bare"];
  auto out = store::providerEvents({mixed, numbered}, traits, kNow);
  REQUIRE(out.size() == 2);
  CHECK(out[0].lang == "lv");
  CHECK(out[0].langOf("x|al") == "sq");
  CHECK(out[0].langOf("x|none").empty());  // its group's market says nothing of it
  // A numbered family's country is its feeds' (their own say nothing else).
  CHECK(out[1].langOf("x|us") == "en");
  CHECK(out[1].langOf("x|bare") == "en");
}
