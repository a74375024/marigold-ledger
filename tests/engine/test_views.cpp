// What the screens list: the rules every platform's guide, search and
// player banner share.
#include <doctest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "core/engine/events_model.hpp"
#include "core/engine/views.hpp"
#include "core/sports/types.hpp"
#include "core/model/channel.hpp"

using namespace marigold;
using namespace marigold::engine;
namespace views = marigold::engine::views;

namespace {

constexpr time::Seconds kNow = 1'800'000'000;

GuideChannel channel(const std::string& id, const std::string& name, const std::string& section,
                     const std::string& group = "G") {
  GuideChannel g;
  g.id = id;
  g.name = name;
  g.section = section;
  g.group = group;
  return g;
}

// Six channels in guide order; c5 is in a hidden group (only in `every`).
Channels sample() {
  Channels ch;
  ch.all = {channel("c0", "ESPN", "Sports"),  channel("c1", "FS1", "Sports"),
            channel("c2", "CNN", "News"),     channel("c3", "HGTV", "Entertainment"),
            channel("c4", "Nick", "Kids"),    channel("c5", "Hidden One", "Entertainment", "Hidden")};
  for (size_t i = 0; i < ch.all.size(); ++i) {
    ch.byId[ch.all[i].id] = static_cast<int>(i);
    ch.every.push_back(static_cast<int>(i));
    if (i != 5) ch.browse.push_back(static_cast<int>(i));
  }
  ch.lineup = {0, 2, 3};
  ch.favorites = {2};
  ch.recent = {3, 0};
  return ch;
}

Listing show(const std::string& title, time::Seconds start, time::Seconds end, uint8_t genres = 0) {
  Listing l;
  l.title = title;
  l.start = start;
  l.end = end;
  l.genres = genres;
  return l;
}

void put(Listings& li, const std::string& id, std::vector<Listing> v) {
  li.byChannel[id] = std::make_shared<const std::vector<Listing>>(std::move(v));
}

std::vector<std::string> kinds(const views::List& l) {
  std::vector<std::string> out;
  for (const views::Line& x : l.lines)
    out.push_back(x.kind == views::Line::Kind::Prompt ? "prompt" : x.kind == views::Line::Kind::Header ? "#" + x.title
                                                                                                        : std::to_string(x.channel));
  return out;
}

}  // namespace

TEST_CASE("views: a lineup not set up yet shows every channel under the setup prompt") {
  Channels ch = sample();
  views::List l = views::guide(ch, nullptr, views::Tab::Lineup, {}, kNow);
  CHECK(kinds(l) == std::vector<std::string>{"prompt", "#Sports", "0", "1", "#News", "2", "#Entertainment", "3",
                                             "#Kids", "4"});
  REQUIRE(l.sections.size() == 4);
  CHECK(l.sections[0].title == "Sports");
  CHECK(l.sections[0].firstLine == 1);
  CHECK(l.sections[0].count == 2);
  CHECK(views::flipList(ch, l) == std::vector<std::string>{"c0", "c1", "c2", "c3", "c4"});
}

TEST_CASE("views: a set-up lineup, favourites and recent") {
  Channels ch = sample();
  ch.lineupConfigured = true;
  CHECK(kinds(views::guide(ch, nullptr, views::Tab::Lineup, {}, kNow)) ==
        std::vector<std::string>{"#Sports", "0", "#News", "2", "#Entertainment", "3"});
  CHECK(kinds(views::guide(ch, nullptr, views::Tab::Favorites, {}, kNow)) == std::vector<std::string>{"#News", "2"});
  // Recent is in watch order, without headers.
  views::List recent = views::guide(ch, nullptr, views::Tab::Recent, {}, kNow);
  CHECK(kinds(recent) == std::vector<std::string>{"3", "0"});
  CHECK(recent.sections.empty());
  CHECK(views::guide(ch, nullptr, views::Tab::Events, {}, kNow).lines.empty());
}

TEST_CASE("views: one provider group includes its hidden channels") {
  Channels ch = sample();
  views::Filter hidden{views::Filter::Kind::Group, "Hidden"};
  CHECK(kinds(views::guide(ch, nullptr, views::Tab::All, hidden, kNow)) ==
        std::vector<std::string>{"#Entertainment", "5"});
  // Not in the plain All Channels list.
  auto all = views::flipList(ch, views::guide(ch, nullptr, views::Tab::All, {}, kNow));
  CHECK(std::find(all.begin(), all.end(), "c5") == all.end());
}

TEST_CASE("views: on now goes by the show's genre, else the neighborhood") {
  Channels ch = sample();
  ch.lineupConfigured = true;
  ch.lineup = {0, 1, 2, 3, 4};
  Listings li;
  put(li, "c0", {show("Talk", kNow - 600, kNow + 600, kGenreNews)});        // tagged, not sports
  put(li, "c2", {show("Late game", kNow + 1200, kNow + 5000, kGenreSports)});  // sports in 20 min
  put(li, "c3", {show("Much later", kNow + 2400, kNow + 5000, kGenreSports)});  // past 30 min
  views::Filter sports{views::Filter::Kind::SportsNow, {}};
  // c0's shows are tagged and none is sports; c1 has no listings and is in
  // Sports; c2 has sports within 30 minutes; c3's starts too late.
  CHECK(views::flipList(ch, views::guide(ch, &li, views::Tab::Lineup, sports, kNow)) ==
        std::vector<std::string>{"c1", "c2"});

  auto counts = views::onNowCounts(ch, ch.lineup, &li, kNow);
  REQUIRE(counts.size() == 4);
  CHECK(counts[0].title == "Sports on now");
  CHECK(counts[0].count == 2);
  CHECK(counts[1].title == "News on now");
  CHECK(counts[1].count == 1);  // c0's talk show (c2 is tagged sports, not in News)
  CHECK(counts[3].count == 1);  // Nick, by its neighborhood

  auto sections = views::sectionCounts(ch, ch.lineup);
  REQUIRE(sections.size() == 4);
  CHECK(sections[0].title == "Sports");
  CHECK(sections[0].count == 2);
  CHECK(sections[0].filter == views::Filter{views::Filter::Kind::Section, "Sports"});
}

TEST_CASE("views: the mini guide groups the flip list") {
  Channels ch = sample();
  views::List l = views::grouped(ch, {"c2", "gone", "c0", "c1"});
  CHECK(kinds(l) == std::vector<std::string>{"#News", "2", "#Sports", "0", "1"});
  REQUIRE(l.sections.size() == 2);
  CHECK(l.sections[1].firstLine == 2);
}

TEST_CASE("views: the show at a time in the guide window") {
  std::vector<Listing> v = {show("A", 1000, 2000), show("B", 2000, 3000), show("C", 5000, 6000)};
  CHECK(views::programAt(v, 1500, 0, 10000) == 0);
  CHECK(views::programAt(v, 2000, 0, 10000) == 1);  // a show starts as the last ends
  CHECK(views::programAt(v, 3500, 0, 10000) == 2);  // a gap: the next one in the window
  CHECK(views::programAt(v, 3500, 0, 4000) == 1);   // ... not past the window: the last still visible
  CHECK(views::programAt(v, 3500, 3100, 4000) == -1);
  CHECK(views::programAt(v, 7000, 0, 10000) == 2);
  CHECK(views::programAt({}, 7000, 0, 10000) == -1);
}

TEST_CASE("views: on now and next") {
  Listings li;
  put(li, "c0", {show("A", 1000, 2000), show("B", 2000, 3000), show("C", 5000, 6000)});
  auto at = [&](time::Seconds t) {
    views::NowNext n = views::nowAndNext(li, "c0", t);
    return std::string(n.now ? n.now->title : "-") + "/" + (n.next ? n.next->title : "-");
  };
  CHECK(at(500) == "-/A");
  CHECK(at(1000) == "A/B");
  CHECK(at(2999) == "B/C");
  CHECK(at(4000) == "-/C");
  CHECK(at(5500) == "C/-");
  CHECK(at(7000) == "-/-");
  CHECK(views::nowAndNext(li, "nothing", 1500).now == nullptr);
}

TEST_CASE("views: channel search ranks names, numbers and stations") {
  Channels ch;
  auto add = [&](GuideChannel g) {
    ch.byId[g.id] = static_cast<int>(ch.all.size());
    ch.all.push_back(std::move(g));
  };
  add(channel("espn2", "ESPN 2", "Sports"));
  add(channel("espn", "ESPN", "Sports"));
  add(channel("news", "Sky Sports News", "Sports"));
  GuideChannel local = channel("wnbc", "WNBC", "Local");
  local.style = GuideChannel::Style::Local;
  local.subtitle = "NBC 4";
  local.number = 4;
  add(local);
  GuideChannel foreign = channel("espnbr", "ESPN Brasil", "Sports");
  foreign.browsable = false;  // kept for Live Events only
  add(foreign);
  GuideChannel tele = channel("tele", "Telemundo Deportes", "Español");
  tele.inLineup = true;
  add(tele);

  CHECK(views::searchChannels(ch, " espn ", 10) == std::vector<std::string>{"espn", "espn2"});
  CHECK(views::searchChannels(ch, "4", 10) == std::vector<std::string>{"wnbc"});
  CHECK(views::searchChannels(ch, "nbc", 10) == std::vector<std::string>{"wnbc"});
  CHECK(views::searchChannels(ch, "news", 10) == std::vector<std::string>{"news"});
  CHECK(views::searchChannels(ch, "espanol", 10) == std::vector<std::string>{"tele"});  // accents folded
  CHECK(views::searchChannels(ch, "sports", 10).front() == "news");  // a word in the name beats the neighborhood
  CHECK(views::searchChannels(ch, "sports", 1).size() == 1);
  CHECK(views::searchChannels(ch, "", 10).empty());
}

TEST_CASE("views: the event guide lists games you can watch under their groups") {
  EventsBoard board;
  EventRow row;
  EventGroup live{"Live", {}}, later{"Later Today", {}}, none{"", {}};
  EventTile a, b, c, d;
  a.id = "a";
  a.watchable = true;
  b.id = "b";  // not watchable
  c.id = "c";
  d.id = "d";
  d.watchable = true;
  live.tiles = {a, b};
  later.tiles = {c};  // nothing watchable: no header
  none.tiles = {d};
  row.groups = {live, later, none};
  board.rows = {row};

  auto lines = views::eventGuide(board, 0);
  REQUIRE(lines.size() == 3);
  CHECK(lines[0].header == "Live");
  CHECK(lines[0].tile == nullptr);
  REQUIRE(lines[1].tile);
  CHECK(lines[1].tile->id == "a");
  REQUIRE(lines[2].tile);
  CHECK(lines[2].tile->id == "d");
  CHECK(views::eventGuide(board, 3).empty());
}

TEST_CASE("views: a row out of its section's block still counts with it") {
  Channels ch = sample();
  // Entertainment, News, Entertainment: one Entertainment of 2, not two.
  auto sections = views::sectionCounts(ch, {3, 2, 5});
  REQUIRE(sections.size() == 2);
  CHECK(sections[0].title == "Entertainment");
  CHECK(sections[0].count == 2);
  CHECK(sections[1].title == "News");
}

TEST_CASE("events: the tile's top line names the sport and the competition") {
  CHECK(tileSport("cricket") == "CRICKET");
  CHECK(tileSport("fieldHockey") == "FIELD HOCKEY");
  CHECK(tileSport("other").empty());  // not known: "LIVE EVENT", never a guess
  sports::SportsEvent atp;
  atp.league = "Tennis";
  atp.leagueShort = "ATP";
  CHECK(tileCompetition(&atp, "TENNIS", "Swiss Indoors Basel \xc2\xb7 R2") == "ATP \xc2\xb7 Swiss Indoors Basel \xc2\xb7 R2");
  sports::SportsEvent cricket;
  cricket.league = "Cricket";
  cricket.leagueShort = "CRIC";
  CHECK(tileCompetition(&cricket, "CRICKET", "3rd Test") == "3rd Test");  // "Cricket" says no more than the sport
  sports::SportsEvent soccer;
  soccer.league = "Soccer";
  soccer.leagueShort = "SOCCER";
  CHECK(tileCompetition(&soccer, "SOCCER", "SOCCER \xc2\xb7 WOMEN'S") == "WOMEN'S");
  sports::SportsEvent d2;
  d2.league = "Volleyball";
  d2.leagueShort = "D2WVB";
  CHECK(tileCompetition(&d2, "VOLLEYBALL", "") == "Women's Division II");
}

TEST_CASE("views: typed digits tune at once when no longer channel number starts with them") {
  using marigold::engine::Channels;
  using marigold::engine::GuideChannel;
  Channels ch;
  for (int n : {5, 7, 50, 702, 7020}) {
    GuideChannel g;
    g.number = n;
    ch.all.push_back(g);
  }
  using marigold::engine::views::digitsSettled;
  CHECK_FALSE(digitsSettled("", ch));
  CHECK_FALSE(digitsSettled("5", ch));    // 50 could follow
  CHECK(digitsSettled("50", ch));
  CHECK_FALSE(digitsSettled("7", ch));    // 702, 7020
  CHECK_FALSE(digitsSettled("702", ch));  // 7020
  CHECK(digitsSettled("7020", ch));
  CHECK(digitsSettled("9", ch));          // nothing starts with 9: "No channel 9" at once
  CHECK(digitsSettled("1234", ch));       // four digits is the most
}
