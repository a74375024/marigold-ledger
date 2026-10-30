// The Live Events report: every event slot, what it was read as and what
// became of it.
#include <doctest.h>

#include <string>

#include "core/report/live_events_report.hpp"
#include "core/util/strings.hpp"

using namespace marigold;

namespace {

catalog::ChannelTraits slot(const std::string& family, int number, const std::string& text) {
  catalog::ChannelTraits t;
  t.familyKey = family;
  t.familyNumber = number;
  t.eventText = text;
  t.kind = catalog::ChannelKind::event;
  return t;
}

std::string lineWith(const std::string& csv, const std::string& needle) {
  for (std::string_view line : str::split(csv, '\n'))
    if (line.find(needle) != std::string_view::npos) return std::string(line);
  return {};
}

}  // namespace

TEST_CASE("live events report: on the board, left out by matching, by the board, and not an event") {
  const time::Seconds now = 1793397600;  // 2026-10-30
  catalog::CatalogResult cat;
  cat.traits["x|1"] = slot("grp:ca| tsn+ ppv", 1, "Harbor vs Valley");
  cat.traits["x|2"] = slot("grp:ca| tsn+ ppv", 2, "SC with Jay Onrait A");
  cat.traits["x|3"] = slot("grp:us| nfhs ppv", 1, "LIVE | CARROLLTON");
  cat.traits["x|4"] = slot("grp:ca| tsn+ ppv", 3, "NO EVENT STREAMING");
  cat.traits["x|5"] = slot("grp:us| nfhs ppv", 2, "Coast vs Bay");
  catalog::EventFamily fam;
  fam.id = "fam|grp:ca| tsn+ ppv";
  fam.name = "CA| TSN+ PPV";
  cat.families.push_back(fam);
  auto event = [&](const std::string& id, const std::string& title, const std::string& feed) {
    catalog::LiveEvent e;
    e.id = id;
    e.title = title;
    e.league = "Soccer";
    e.start = now;
    e.startGuessed = false;
    e.feeds = {feed};
    cat.events.push_back(e);
  };
  event("ev|1", "Harbor vs Valley", "x|1");
  event("ev|2", "SC with Jay Onrait A", "x|2");
  event("ev|3", "Carrollton", "x|3");
  event("ev|5", "Coast vs Bay", "x|5");

  std::vector<sports::SportsEvent> events(2);
  events[0].id = "t1";
  events[0].title = "Harbor vs Valley";
  events[0].sources = {{}};
  events[0].sources[0].channelID = "x|1";
  events[0].sources[0].kind = sports::EventSource::Kind::provider;
  events[1] = events[0];
  events[1].id = "t5";
  events[1].title = "Coast vs Bay";
  events[1].sources[0].channelID = "x|5";
  std::map<std::string, std::string> hidden = {{"x|3", "a school's name alone: no sport or opponent"}};
  sports::board::Board board;
  sports::board::Row live;
  live.title = "Live now";
  live.groups.resize(1);
  live.groups[0].tiles.resize(1);
  live.groups[0].tiles[0].id = "t1";
  board.rows.push_back(live);
  board.events["t1"] = events[0];
  board.left["t5"] = "high school (off in My Sports)";

  report::LiveEventsReportInput in;
  in.catalog = &cat;
  in.events = &events;
  in.hidden = &hidden;
  in.board = &board;
  in.groupStarts = [](const std::string& f) { return f == "grp:us| nfhs ppv" ? std::string("1 of 4 started") : ""; };
  in.now = now;
  in.zone = time::Zone::utc();
  std::string csv = report::liveEventsCsv(in);
  CHECK(str::startsWith(csv, "group,feed,slot,read_as,sport,starts,result,reason,tile,rows,group_starts\n"));
  CHECK(lineWith(csv, "\"Harbor vs Valley\"").find("\"CA| TSN+ PPV\",\"1\"") == 0);
  CHECK(lineWith(csv, "\"Harbor vs Valley\"").find("\"on the board\",\"\",\"Harbor vs Valley\",\"Live now\"") !=
        std::string::npos);
  CHECK(lineWith(csv, "CARROLLTON").find("\"left out\",\"a school's name alone: no sport or opponent\"") !=
        std::string::npos);
  CHECK(lineWith(csv, "CARROLLTON").find("\"1 of 4 started\"") != std::string::npos);
  CHECK(lineWith(csv, "Coast vs Bay").find("\"left out\",\"high school (off in My Sports)\"") != std::string::npos);
  CHECK(lineWith(csv, "Jay Onrait").find("\"left out\",\"no tile took it") != std::string::npos);
  CHECK(lineWith(csv, "NO EVENT").find("\"not an event\",\"a placeholder or ended slot\"") != std::string::npos);
}
