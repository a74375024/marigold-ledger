// Search (Build 113): the query cleaner, the matcher's rules and what each
// group finds, numbers from an empty field, spoilers, recent searches and
// the scope (your lineup and Live Events first).
// Made-up channels and teams (and a few well-known team names).
#include <doctest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "core/engine/search.hpp"
#include "core/engine/search_build.hpp"
#include "core/engine/views.hpp"
#include "core/sports/types.hpp"

using namespace marigold;
using namespace marigold::engine;
namespace se = marigold::engine::search;

namespace {

constexpr time::Seconds kNow = 1'800'000'000;

se::ChannelEntry channel(const std::string& id, const std::string& name, const std::string& number = "",
                         bool lineup = false) {
  se::ChannelEntry c;
  c.id = id;
  c.name = se::fold(name);
  c.number = number;
  c.inLineup = lineup;
  return c;
}

se::TeamEntry team(const std::string& league, const std::string& id, const std::string& name, bool college = false) {
  sports::SportsTeam t;
  t.id = id;
  t.name = name;
  se::TeamEntry x;
  x.key = league + "|" + id;
  x.name = name;
  x.league = league;
  x.names = se::teamNames(t, college);
  return x;
}

se::GameEntry game(const std::string& id, const std::string& title, std::vector<std::string> teams,
                   se::GameEntry::State state, time::Seconds start, bool watchable = true) {
  se::GameEntry g;
  g.id = id;
  g.title = title;
  g.names = {se::fold(title)};
  g.teams = std::move(teams);
  g.state = state;
  g.start = start;
  g.watchable = watchable;
  return g;
}

Listing show(const std::string& title, time::Seconds start, time::Seconds end) {
  Listing l;
  l.title = title;
  l.start = start;
  l.end = end;
  return l;
}

// A small TV: channels, a league's teams with a live and an upcoming game,
// and a guide.
struct Tv {
  se::Index index;
  std::shared_ptr<Listings> listings = std::make_shared<Listings>();

  Tv() {
    using S = se::GameEntry::State;
    index.channels = {channel("nbx", "NBX", "230", true), channel("nbxs", "NBX Sports", "231", true),
                      channel("sn2", "Sports Net 2", "206", true), channel("sn2w", "Sports Net 2 West", "2061"),
                      channel("goal", "Goal TV", "210"), channel("news", "City News 24", "24"),
                      channel("c49", "Coast 49", "49"), channel("sun", "Sunburst Movies", "300")};
    se::ChannelEntry local = channel("kbay", "Channel 4", "4");
    local.network = "nbc";
    local.channel = "4";
    local.stations = {"kbay", "kbyx"};  // its call sign, and the one it had before
    index.channels.push_back(local);
    se::ChannelEntry abroad = channel("abroad", "Deportes Norte", "900");
    abroad.browsable = false;
    index.channels.push_back(abroad);

    se::TeamEntry bulls = team("PBL", "1", "Northbridge Bulls");
    bulls.followed = true;
    bulls.liveGame = "g-live";
    se::TeamEntry kings = team("PBL", "2", "Harbor Kings");
    kings.liveGame = "g-live";
    se::TeamEntry rams = team("PBL", "3", "Riverton Rams");
    rams.nextGame = "g-sat";
    se::TeamEntry peaks = team("PBL", "4", "Valley Peaks");
    se::TeamEntry bye = team("PBL", "5", "Lakeside Owls");  // no game this week
    index.teams = {bulls, kings, rams, peaks, bye,
                   team("NFL", "sf", "San Francisco 49ers"), team("NBA", "phi", "Philadelphia 76ers"),
                   team("EPL", "mu", "Manchester United"), team("EPL", "tot", "Tottenham Hotspur"),
                   team("NBA", "sas", "San Antonio Spurs"), team("NHL", "mtl", "Montreal Canadiens"),
                   team("PFL", "h49", "Harbor 49s")};
    index.games = {game("g-live", "Bulls at Harbor Kings", {"PBL|1", "PBL|2"}, S::Live, kNow - 3600),
                   game("g-sat", "Rams at Bulls", {"PBL|3", "PBL|1"}, S::Upcoming, kNow + 3 * 86400),
                   game("g-stream", "Riverton Rams at Valley Peaks", {"PBL|3", "PBL|4"}, S::Upcoming,
                        kNow + 86400, false),
                   game("g-ended", "Comets at Blaze", {}, S::Ended, kNow - 4 * 3600)};
    index.games[0].favorite = index.games[1].favorite = true;
    for (int i = 0; i < 3; ++i) index.games[static_cast<size_t>(i)].league = "pbl";
    index.games[2].streamingOnly = true;
    se::LeagueEntry pbl;
    pbl.id = "PBL";
    pbl.title = "PBL";
    pbl.names = {"pbl", "pro basketball league"};
    pbl.followed = true;
    se::LeagueEntry basketball;
    basketball.id = "sport:basketball";
    basketball.title = "Basketball";
    basketball.names = {"basketball"};
    basketball.sport = true;
    index.leagues = {pbl, basketball};

    put("nbx", {show("Courtside Kitchen", kNow - 600, kNow + 1200), show("NBX Nightly News", kNow + 4000, kNow + 5800),
                show("NBX Nightly News", kNow + 40000, kNow + 41800)});
    put("goal", {show("Law & Order", kNow - 600, kNow + 3000), show("Jeopardy!", kNow + 3000, kNow + 4800),
                 show("The Great British Baking Show", kNow + 4800, kNow + 8400)});
    put("news", {show("The Office", kNow - 300, kNow + 1500), show("Lakers Tonight", kNow + 1500, kNow + 3300),
                 show("Comets at Blaze Replay", kNow + 3300, kNow + 6900)});
    index.listings = listings;
    for (const auto& [id, list] : listings->byChannel)
      for (const Listing& l : *list) index.shows[se::fold(l.title)].push_back({id, &l});
    index.finish();
  }

  void put(const std::string& id, std::vector<Listing> v) {
    listings->byChannel[id] = std::make_shared<const std::vector<Listing>>(std::move(v));
  }

  se::Results find(const std::string& text, bool scores = true) const {
    se::Options o;
    o.now = kNow;
    o.showScores = scores;
    return se::run(index, se::parse(text, [this](const std::string& f) { return index.isTitle(f); }), o);
  }
};

std::vector<std::string> ids(const std::vector<se::Item>& items) {
  std::vector<std::string> out;
  for (const se::Item& i : items) out.push_back(i.id);
  return out;
}

bool has(const std::vector<se::Item>& items, const std::string& id) {
  return std::any_of(items.begin(), items.end(), [&](const se::Item& i) { return i.id == id; });
}

}  // namespace

TEST_CASE("search: folding, words and initials") {
  CHECK(se::fold("Law & Order: SVU") == "law and order svu");
  CHECK(se::fold("  Pacífico   TV ") == "pacifico tv");
  CHECK(se::fold("sn2") == "sn 2");
  CHECK(se::fold("49ers") == "49 ers");
  CHECK(se::fold("Jeopardy!") == "jeopardy");
  CHECK(se::fold("Grey's Anatomy") == "greys anatomy");
  CHECK(se::initials(se::words("northbridge bulls")) == "nb");
  CHECK(se::initials(se::words("sports net 2")) == "sn");
}

TEST_CASE("search: typos are counted per word, a swap is one edit") {
  CHECK(se::typoAllowance(4) == 0);
  CHECK(se::typoAllowance(5) == 1);
  CHECK(se::typoAllowance(7) == 1);
  CHECK(se::typoAllowance(8) == 2);
  CHECK(se::editDistance("bulls", "bulsl", 1) == 1);  // a swap
  CHECK(se::editDistance("jepordy", "jeopardy", 2) == 2);
  CHECK(se::editDistance("abc", "xyz", 1) == 2);  // past the cap
  se::Query q = se::parse("bulsl");
  CHECK(se::match(q, "northbridge bulls") == se::Hit::Typo);
  // Four letters: no typo allowed.
  CHECK(se::match(se::parse("kngz"), "kngs") == se::Hit::None);
  // Eight or more: two.
  CHECK(se::match(se::parse("nothbrige"), "northbridge bulls") == se::Hit::Typo);
  CHECK(se::match(se::parse("nthbrge"), "northbridge bulls") == se::Hit::None);
  se::Query exact = se::parse("bulsl");
  exact.exactOnly = true;
  CHECK(se::match(exact, "northbridge bulls") == se::Hit::None);
}

TEST_CASE("search: the query cleaner, unless the whole query is a title") {
  se::Query q = se::parse("watch the bulls game");
  CHECK(q.text == "bulls");
  CHECK(q.cleaned);
  q = se::parse("news tonight");
  CHECK(q.text == "news");
  CHECK(q.when == se::When::Tonight);
  q = se::parse("basketball on now");
  CHECK(q.text == "basketball");
  CHECK(q.when == se::When::Now);
  q = se::parse("bulls vs kings");
  CHECK(q.tokens == std::vector<std::string>{"bulls", "kings"});
  // "Lakers Tonight" is a show: left as typed.
  auto title = [](const std::string& f) { return f == "lakers tonight"; };
  q = se::parse("Lakers Tonight", title);
  CHECK(q.text == "lakers tonight");
  CHECK(q.when == se::When::Any);
  CHECK_FALSE(q.cleaned);
  CHECK(se::parse("Lakers Tonight").when == se::When::Tonight);
  // One word stays, filler or not.
  CHECK(se::parse("the").text == "the");
}

TEST_CASE("search: numbers, spoken numbers and \"channel 4\"") {
  se::Query q = se::parse("channel 4");
  CHECK(q.digits);
  CHECK(q.text == "4");
  q = se::parse("two oh six");
  CHECK(q.digits);
  CHECK(q.text == "206");
  CHECK(se::parse("206").digits);
  CHECK_FALSE(se::parse("49ers").digits);
  Tv tv;
  se::Results r = tv.find("channel 4");
  REQUIRE(r.top);
  CHECK(r.top->id == "kbay");
  CHECK(r.top->tier == 1);
  // A local by network and number, and by its call signs (the old one too).
  CHECK(tv.find("nbc 4").top->id == "kbay");
  CHECK(tv.find("kbyx").top->id == "kbay");
}

TEST_CASE("search: & is and, and a leading \"the\" is optional") {
  CHECK(se::match(se::parse("law & order"), se::fold("Law and Order")) == se::Hit::Exact);
  CHECK(se::match(se::parse("law and order"), se::fold("Law & Order")) == se::Hit::Exact);
  CHECK(se::match(se::parse("office"), se::fold("The Office")) == se::Hit::Exact);
  CHECK(se::match(se::parse("the office"), se::fold("The Office")) == se::Hit::Exact);
  Tv tv;
  se::Results r = tv.find("the office");
  REQUIRE(r.top);
  CHECK(r.top->kind == se::Kind::Show);
  CHECK(r.top->name == "The Office");
  CHECK(r.top->tier == 4);  // an exact show title on now
}

TEST_CASE("search: tokens in any order, the last one a prefix") {
  std::string bake = se::fold("The Great British Baking Show");
  CHECK(se::match(se::parse("baking show great"), bake) == se::Hit::Tokens);
  CHECK(se::match(se::parse("great baking sh"), bake) == se::Hit::Tokens);
  // Only the last word may be unfinished.
  CHECK(se::match(se::parse("bak show great"), bake) == se::Hit::None);
  Tv tv;
  se::Results r = tv.find("baking show great");
  REQUIRE(r.top);
  CHECK(r.top->name == "The Great British Baking Show");
}

TEST_CASE("search: two letters match only word starts and initials") {
  CHECK(se::match(se::parse("nb"), "northbridge bulls") == se::Hit::Initials);
  CHECK(se::match(se::parse("nb"), "nbx sports") == se::Hit::Start);
  CHECK(se::match(se::parse("ne"), "city news 24") == se::Hit::Word);
  CHECK(se::match(se::parse("nb"), "sunburst movies") == se::Hit::None);  // never inside a word
  CHECK(se::match(se::parse("nbu"), "sunburst movies") == se::Hit::Inside);
  CHECK(se::match(se::parse("sn2"), "sports net 2") == se::Hit::Initials);
  // One character: starts only.
  CHECK(se::match(se::parse("n"), "nbx") == se::Hit::Start);
  CHECK(se::match(se::parse("n"), "city news 24") == se::Hit::None);
}

TEST_CASE("search: \"nb\" puts the followed team's live game on top") {
  Tv tv;
  se::Results r = tv.find("nb");
  REQUIRE(r.top);
  CHECK(r.top->kind == se::Kind::Game);
  CHECK(r.top->id == "g-live");
  CHECK(r.top->tier == 2);
  CHECK(r.top->playable);
  CHECK(ids(r.channels) == std::vector<std::string>{"nbx", "nbxs"});
  CHECK(ids(r.teams) == std::vector<std::string>{"PBL|1"});
  // The top result isn't repeated, and the team's game isn't listed again.
  CHECK_FALSE(has(r.games, "g-live"));
  // "nbx" is a channel's exact name: it leads, and the Bulls drop out.
  r = tv.find("nbx");
  CHECK(r.top->id == "nbx");
  CHECK(r.teams.empty());
  // One letter: channels that start with it and the team you follow, no
  // games or shows.
  r = tv.find("n");
  CHECK(ids(r.teams) == std::vector<std::string>{"PBL|1"});
  CHECK(r.games.empty());
  CHECK(r.shows.empty());
  CHECK_FALSE(r.nothing);
}

TEST_CASE("search: a team's top result is its first game you can watch") {
  Tv tv;
  se::Results r = tv.find("riverton");
  REQUIRE(r.top);
  CHECK(r.top->id == "g-sat");  // Rams at Bulls, in your channels
  CHECK(ids(r.teams) == std::vector<std::string>{"PBL|3"});
  // The streaming-only game is listed, never on top, and can't be played.
  REQUIRE(has(r.games, "g-stream"));
  for (const se::Item& g : r.games)
    if (g.id == "g-stream") CHECK_FALSE(g.playable);
  // A team with no game this week is still found.
  r = tv.find("lakeside");
  REQUIRE(r.top);
  CHECK(r.top->kind == se::Kind::Team);
  CHECK(r.top->id == "PBL|5");
  // Two team words put their game first.
  r = tv.find("bulls kings");
  CHECK(r.top->id == "g-live");
}

TEST_CASE("search: team nicknames, and an ambiguous one finds every team") {
  Tv tv;
  se::Results r = tv.find("niners");
  REQUIRE(r.top);
  CHECK(r.top->id == "NFL|sf");
  CHECK(tv.find("sixers").top->id == "NBA|phi");
  CHECK(tv.find("man utd").top->id == "EPL|mu");
  CHECK(tv.find("habs").top->id == "NHL|mtl");
  r = tv.find("spurs");
  std::vector<std::string> spurs = ids(r.teams);
  if (r.top) spurs.push_back(r.top->id);
  CHECK(std::count(spurs.begin(), spurs.end(), "EPL|tot") == 1);
  CHECK(std::count(spurs.begin(), spurs.end(), "NBA|sas") == 1);
  // "49ers" typed with letters finds the team.
  CHECK(tv.find("49ers").top->id == "NFL|sf");
  CHECK(se::nicknamesOf("manchester united") == std::vector<std::string>{"man u", "man united", "man utd"});
}

TEST_CASE("search: typos find shows and say so") {
  Tv tv;
  se::Results r = tv.find("law and ordr");
  REQUIRE(r.top);
  CHECK(r.top->name == "Law & Order");
  CHECK(r.showing == "Law & Order");
  r = tv.find("jepordy");
  REQUIRE(r.top);
  CHECK(r.top->name == "Jeopardy!");
  CHECK(r.showing == "Jeopardy!");
  // "Showing Northbridge Bulls": the team's name, not its game's.
  r = tv.find("nothbridge");
  REQUIRE(r.top);
  CHECK(r.top->id == "g-live");
  CHECK(r.showing == "Northbridge Bulls");
  // "Search for … instead": exact only.
  se::Query q = se::parse("nothbridge");
  q.exactOnly = true;
  se::Options o;
  o.now = kNow;
  CHECK(se::run(tv.index, q, o).nothing);
  // An exact match never says "Showing".
  CHECK(tv.find("goal tv").showing.empty());
}

TEST_CASE("search: genres, sports and leagues") {
  Tv tv;
  se::Results r = tv.find("news");
  REQUIRE(r.top);
  // "news" names a channel word too, but the genre leads (tier 5).
  CHECK(r.top->id == "genre:news");
  r = tv.find("basketball");
  CHECK(r.top->id == "sport:basketball");
  r = tv.find("pbl");
  CHECK(r.top->id == "PBL");
  CHECK(has(r.games, "g-sat"));  // the league lists its games
  CHECK(has(tv.find("movi").genres, "genre:movies"));
}

TEST_CASE("search: finished games come last, never on top, only with scores on") {
  Tv tv;
  se::Results r = tv.find("comets");
  // The replay airing ranks above the finished game.
  REQUIRE(r.top);
  CHECK(r.top->kind == se::Kind::Show);
  CHECK(r.top->name == "Comets at Blaze Replay");
  REQUIRE(has(r.games, "g-ended"));
  CHECK(r.games.back().id == "g-ended");
  // Show scores off: hidden.
  r = tv.find("comets", false);
  CHECK_FALSE(has(r.games, "g-ended"));
  CHECK(se::showTag(se::fold("Comets at Blaze Replay")) == "REPLAY");
  CHECK(se::showTag(se::fold("PBL Highlights")) == "HIGHLIGHTS");
  CHECK(se::showTag(se::fold("Courtside Kitchen")).empty());
}

TEST_CASE("search: score text is never matched") {
  EventTile t;
  t.id = "g1";
  t.title = "Bulls at Harbor Kings";
  t.sides.resize(2);
  t.sides[0].name = "Bulls";
  t.sides[0].score = "21";
  t.sides[1].name = "Harbor Kings";
  t.sides[1].score = "14";
  t.sides[1].home = true;
  t.heroTitle = "Bulls 21 \xe2\x80\x93 14 Harbor Kings";
  t.heroStatus = "Q3 4:12 \xc2\xb7 3rd & 7 at HK 35";
  t.situation = "3rd & 7 at HK 35";
  t.leaders.push_back({"1", "S. Harbor", "", "-12", "14"});
  t.podium.push_back({"VER", 0});
  t.pill = EventTile::Pill::Live;
  t.watchable = true;
  se::GameEntry g = se::gameEntry(t);
  CHECK(g.title == "Bulls at Harbor Kings");
  CHECK(g.state == se::GameEntry::State::Live);
  for (const std::string& n : g.names) {
    CHECK(n.find("21") == std::string::npos);
    CHECK(n.find("14") == std::string::npos);
    CHECK(n.find("3rd") == std::string::npos);
    CHECK(n.find("harbor s") == std::string::npos);
    CHECK(n.find("ver") == std::string::npos);
  }
  se::Index index;
  index.games = {g};
  index.finish();
  se::Options o;
  o.now = kNow;
  CHECK(se::run(index, se::parse("21"), o).games.empty());
  CHECK(se::run(index, se::parse("3rd and 7"), o).games.empty());
  // The name it is shown and remembered by is the matchup.
  se::Results r = se::run(index, se::parse("bulls"), o);
  REQUIRE(r.top);
  CHECK(r.top->name == "Bulls at Harbor Kings");
}

TEST_CASE("search: another country's sports channel only when named exactly") {
  Tv tv;
  CHECK_FALSE(has(tv.find("deportes").channels, "abroad"));
  CHECK(tv.find("deportes norte").top->id == "abroad");
}

TEST_CASE("search: nothing found, and never for one character") {
  Tv tv;
  CHECK(tv.find("lacrosse").nothing);
  CHECK_FALSE(tv.find("q").nothing);
  CHECK_FALSE(tv.find("bulls").nothing);
}

TEST_CASE("search: LG's number keys from an empty field") {
  Tv tv;
  // "49" is a channel, and also starts a team's name: it waits for OK.
  se::Digits d = se::digits(tv.index, "49");
  CHECK(d.channel == "c49");
  CHECK(d.waits);
  CHECK(d.team == "San Francisco 49ers");
  // "206" plays when the number is complete: here 2061 could follow, so it
  // waits for the next digit (or the pause), never for OK.
  d = se::digits(tv.index, "206");
  CHECK(d.channel == "sn2");
  CHECK_FALSE(d.waits);
  CHECK(d.also == std::vector<std::string>{"sn2w"});
  Channels ch;
  GuideChannel a;
  a.id = "sn2";
  a.number = 206;
  ch.all = {a};
  CHECK(views::digitsSettled("206", ch));
  GuideChannel b;
  b.id = "sn2w";
  b.number = 2061;
  ch.all.push_back(b);
  CHECK_FALSE(views::digitsSettled("206", ch));
  // A number with no channel and no name.
  d = se::digits(tv.index, "777");
  CHECK(d.channel.empty());
  CHECK_FALSE(d.waits);
}

TEST_CASE("search: recent searches keep the picked name, eight of them") {
  std::vector<se::Recent> list;
  // What was picked, by name: "Sports Net 2", not the "sn2" typed.
  CHECK(se::remember(list, {"Sports Net 2", "sn2"}));
  CHECK(list.front().name == "Sports Net 2");
  // Nothing picked (a search that found nothing) is never kept.
  CHECK_FALSE(se::remember(list, {"", "x"}));
  CHECK_FALSE(se::remember(list, {"lacrosse", ""}));
  CHECK(list.size() == 1);
  for (int i = 0; i < 10; ++i) se::remember(list, {"Channel " + std::to_string(i), "c" + std::to_string(i)});
  CHECK(list.size() == se::kRecentKept);
  CHECK(list.front().name == "Channel 9");
  // Picking one again moves it to the front, once.
  se::remember(list, {"Channel 5", "c5"});
  CHECK(list.front().id == "c5");
  CHECK(std::count_if(list.begin(), list.end(), [](const se::Recent& r) { return r.id == "c5"; }) == 1);
  CHECK(se::kRecentShown == 4);
}

TEST_CASE("search: placeholders aren't shows") {
  CHECK(se::placeholderShow(show("To Be Announced", kNow, kNow + 3600), "Goal TV"));
  CHECK(se::placeholderShow(show("Goal TV", kNow, kNow + 3600), "Goal TV"));
  CHECK(se::placeholderShow(show("Highlights", kNow, kNow + 24 * 3600), "Goal TV"));
  CHECK_FALSE(se::placeholderShow(show("Match Day Live", kNow, kNow + 3600), "Goal TV"));
}

// ---------------------------------------------------------------- scope (Build 115)

namespace {

se::Results scopedRun(const se::Index& index, const std::string& text, bool scoped) {
  se::Options o;
  o.now = kNow;
  o.scoped = scoped;
  return se::run(index, se::parse(text, [&index](const std::string& f) { return index.isTitle(f); }), o);
}

// An item anywhere in the results (the top result too).
const se::Item* itemIn(const se::Results& r, const std::string& id) {
  if (r.top && r.top->id == id) return &*r.top;
  for (const auto* group : {&r.channels, &r.teams, &r.games, &r.shows, &r.genres})
    for (const se::Item& i : *group)
      if (i.id == id) return &i;
  return nullptr;
}

const se::Item* showIn(const se::Results& r, const std::string& name) {
  if (r.top && r.top->kind == se::Kind::Show && r.top->name == name) return &*r.top;
  for (const se::Item& i : r.shows)
    if (i.name == name) return &i;
  return nullptr;
}

// The same results: the top, every group's items in order, the line over
// them, and nothing.
std::string listed(const se::Results& r) {
  std::string out = r.top ? std::to_string(static_cast<int>(r.top->kind)) + ":" + r.top->id : "-";
  for (const auto* group : {&r.channels, &r.teams, &r.games, &r.shows, &r.genres}) {
    out += " |";
    for (const se::Item& i : *group) out += " " + i.id;
  }
  return out + " | " + r.showing + (r.nothing ? " nothing" : "");
}

}  // namespace

TEST_CASE("search scope: what counts as yours, for each kind") {
  Tv tv;
  tv.index.lineupSet = true;
  // Channels: My Lineup and Favorites.
  se::Results r = scopedRun(tv.index, "nbx", false);
  REQUIRE(itemIn(r, "nbx"));
  CHECK_FALSE(itemIn(r, "nbx")->outside);
  r = scopedRun(tv.index, "goal", false);
  REQUIRE(itemIn(r, "goal"));
  CHECK(itemIn(r, "goal")->outside);
  tv.index.channels.edit()[tv.index.channelById["goal"]].favorite = true;
  CHECK_FALSE(itemIn(scopedRun(tv.index, "goal", false), "goal")->outside);
  tv.index.channels.edit()[tv.index.channelById["goal"]].favorite = false;
  // Shows: airing on your channels.
  r = scopedRun(tv.index, "courtside kitchen", false);
  REQUIRE(showIn(r, "Courtside Kitchen"));
  CHECK_FALSE(showIn(r, "Courtside Kitchen")->outside);
  r = scopedRun(tv.index, "the office", false);
  REQUIRE(showIn(r, "The Office"));
  CHECK(showIn(r, "The Office")->outside);
  // Games: ones you can watch.
  r = scopedRun(tv.index, "bulls at harbor kings", false);
  REQUIRE(itemIn(r, "g-live"));
  CHECK_FALSE(itemIn(r, "g-live")->outside);
  r = scopedRun(tv.index, "riverton", false);
  REQUIRE(itemIn(r, "g-stream"));
  CHECK(itemIn(r, "g-stream")->outside);
  // Teams: followed, or with a game you can watch this week.
  CHECK_FALSE(itemIn(scopedRun(tv.index, "northbridge bulls", false), "PBL|1")->outside);
  CHECK_FALSE(itemIn(scopedRun(tv.index, "harbor kings", false), "PBL|2")->outside);
  CHECK_FALSE(itemIn(scopedRun(tv.index, "riverton", false), "PBL|3")->outside);
  CHECK(itemIn(scopedRun(tv.index, "valley peaks", false), "PBL|4")->outside);  // its game is streaming only
  CHECK(itemIn(scopedRun(tv.index, "lakeside owls", false), "PBL|5")->outside);  // a bye
  // Genres, sports and leagues: always.
  r = scopedRun(tv.index, "basketball", false);
  REQUIRE(itemIn(r, "sport:basketball"));
  CHECK_FALSE(itemIn(r, "sport:basketball")->outside);
  // Before a lineup is set up, nothing is outside.
  tv.index.lineupSet = false;
  CHECK_FALSE(itemIn(scopedRun(tv.index, "goal", false), "goal")->outside);
  CHECK_FALSE(itemIn(scopedRun(tv.index, "riverton", false), "g-stream")->outside);
}

TEST_CASE("search scope: your lineup and Live Events first, what's outside named on the last row") {
  Tv tv;
  tv.index.lineupSet = true;
  se::Results r = scopedRun(tv.index, "sports net", true);
  CHECK(r.scoped);
  CHECK_FALSE(r.outsideOnly);
  CHECK(itemIn(r, "sn2"));
  CHECK_FALSE(itemIn(r, "sn2w"));  // outside, and not named exactly
  REQUIRE(r.outside.size() == 1);
  CHECK(r.outside.front().kind == se::Kind::Channel);
  CHECK(r.outside.front().id == "sn2w");
  // A team you can watch stays; its streaming-only game is left out and
  // named instead.
  r = scopedRun(tv.index, "riverton", true);
  REQUIRE(r.top);
  CHECK(r.top->id == "g-sat");
  CHECK(ids(r.teams) == std::vector<std::string>{"PBL|3"});
  CHECK_FALSE(itemIn(r, "g-stream"));
  REQUIRE(r.outside.size() == 1);
  CHECK(r.outside.front().name == "Riverton Rams at Valley Peaks");
  // Shows on other channels the same way; at most three names.
  r = scopedRun(tv.index, "n", true);
  CHECK(r.outside.size() <= 3);
  // Nothing outside: no names.
  CHECK(scopedRun(tv.index, "nbx", true).outside.empty());
  // The run in steps (each letter, then the shows) is the same.
  for (const char* text : {"sports net", "riverton", "law", "jepordy", "2061", "nbx"}) {
    se::Options o;
    o.now = kNow;
    o.scoped = true;
    se::Query q = se::parse(text);
    se::Results steps = se::merge(tv.index, se::find(tv.index, q, o), se::findShows(tv.index, q, o));
    CHECK(listed(steps) == listed(se::run(tv.index, q, o)));
  }
}

TEST_CASE("search scope: an exact name or number outside your lineup still comes up, marked") {
  Tv tv;
  tv.index.lineupSet = true;
  // A number: the top result, marked.
  se::Results r = scopedRun(tv.index, "2061", true);
  REQUIRE(r.top);
  CHECK(r.top->id == "sn2w");
  CHECK(r.top->outside);
  // A channel's name, a show's title, a team's name.
  r = scopedRun(tv.index, "goal tv", true);
  REQUIRE(r.top);
  CHECK(r.top->id == "goal");
  CHECK(r.top->outside);
  r = scopedRun(tv.index, "jeopardy", true);
  REQUIRE(showIn(r, "Jeopardy!"));
  CHECK(showIn(r, "Jeopardy!")->outside);
  r = scopedRun(tv.index, "valley peaks", true);
  REQUIRE(itemIn(r, "PBL|4"));
  CHECK(itemIn(r, "PBL|4")->outside);
  // Another country's channels: still only by their exact name.
  CHECK_FALSE(itemIn(scopedRun(tv.index, "deportes", true), "abroad"));
  r = scopedRun(tv.index, "deportes norte", true);
  REQUIRE(r.top);
  CHECK(r.top->id == "abroad");
  CHECK(r.top->outside);
  // LG's number keys: a number outside your lineup plays when complete,
  // unless its channel is in a hidden group (then only OK tunes it).
  se::Digits d = se::digits(tv.index, "2061");
  CHECK(d.channel == "sn2w");
  CHECK_FALSE(d.waits);
  auto& list = tv.index.channels.edit();
  list[tv.index.channelById["sn2w"]].hidden = true;
  CHECK(se::digits(tv.index, "2061").waits);
  list[tv.index.channelById["sn2"]].hidden = true;  // yours: as before
  CHECK_FALSE(se::digits(tv.index, "206").waits);
  tv.index.lineupSet = false;
  CHECK_FALSE(se::digits(tv.index, "2061").waits);
}

TEST_CASE("search scope: nothing in your lineup shows what's outside at once") {
  Tv tv;
  tv.index.lineupSet = true;
  se::Results r = scopedRun(tv.index, "jepordy", true);
  CHECK(r.scoped);
  CHECK(r.outsideOnly);
  CHECK_FALSE(r.nothing);
  CHECK(r.outside.empty());
  REQUIRE(r.top);
  CHECK(r.top->name == "Jeopardy!");
  // As everything would list them.
  CHECK(listed(r) == listed(scopedRun(tv.index, "jepordy", false)));
  CHECK(listed(scopedRun(tv.index, "law", true)) == listed(scopedRun(tv.index, "law", false)));
  // Nothing anywhere: as before.
  r = scopedRun(tv.index, "lacrosse", true);
  CHECK(r.nothing);
  CHECK_FALSE(r.outsideOnly);
}

TEST_CASE("search scope: everything is the unscoped run, and before a lineup everything is yours") {
  Tv tv;
  std::vector<std::string> queries = {"n",   "nb",          "nbx",     "sports net", "riverton", "goal", "law and ordr",
                                      "jepordy", "2061",    "206",     "49",         "news",     "basketball",
                                      "lakeside", "deportes norte", "bulls kings", "the office", "lacrosse"};
  std::vector<std::string> before;
  for (const std::string& q : queries) before.push_back(listed(scopedRun(tv.index, q, false)));
  // Before a lineup is set up, a scoped run is everything.
  for (size_t i = 0; i < queries.size(); ++i) {
    se::Results r = scopedRun(tv.index, queries[i], true);
    CHECK(listed(r) == before[i]);
    CHECK_FALSE(r.scoped);
    CHECK(r.outside.empty());
  }
  // With a lineup, "everything" is the same results in the same order.
  tv.index.lineupSet = true;
  for (size_t i = 0; i < queries.size(); ++i) {
    se::Results r = scopedRun(tv.index, queries[i], false);
    CHECK(listed(r) == before[i]);
    CHECK_FALSE(r.scoped);
  }
}

TEST_CASE("search scope: the index knows the lineup and hidden groups") {
  auto channels = std::make_shared<Channels>();
  for (const char* name : {"Sports Net 2", "Sports Net 2 West", "Harbor 24"}) {
    GuideChannel g;
    g.id = std::string("e|") + name;
    g.name = name;
    channels->byId[g.id] = static_cast<int>(channels->all.size());
    channels->all.push_back(g);
  }
  channels->all[0].inLineup = true;
  channels->lineup = {0};
  channels->browse = {0, 1};  // Harbor 24's group is hidden
  channels->lineupConfigured = true;
  search_build::GuideInput in;
  in.channels = channels;
  in.listings = std::make_shared<Listings>();
  in.now = kNow;
  std::shared_ptr<se::Index> index = search_build::guide(in);
  CHECK(index->lineupSet);
  CHECK_FALSE(index->channels[0].hidden);
  CHECK_FALSE(index->channels[1].hidden);
  CHECK(index->channels[2].hidden);
  CHECK(index->channels[0].mine());
  CHECK_FALSE(index->channels[1].mine());
  // Kept with the channels when only the listings change.
  search_build::GuideInput again = in;
  again.previous = index;
  CHECK(search_build::guide(again)->lineupSet);
  // Before a lineup is set up: everything is yours.
  channels->lineupConfigured = false;
  in.previous.reset();
  index = search_build::guide(in);
  CHECK_FALSE(index->lineupSet);
  CHECK(listed(scopedRun(*index, "sports net", true)) == listed(scopedRun(*index, "sports net", false)));
}
