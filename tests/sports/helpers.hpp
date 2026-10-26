// Synthetic teams, games, events and scoreboard JSON for the sports tests.
// Every team is invented; logos are on the reserved example.invalid domain.
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/catalog/types.hpp"
#include "core/sports/types.hpp"
#include "core/util/time.hpp"

namespace sportsdata {

using marigold::sports::EventSource;
using marigold::sports::Seconds;
using marigold::sports::SportsEvent;
using marigold::sports::SportsGame;
using marigold::sports::SportsTeam;

inline Seconds utc(int y, int mo, int d, int h, int mi) {
  return marigold::time::secondsFromCivil({y, mo, d, h, mi, 0});
}

// Saturday 2026-10-24 18:00 UTC (2:00 PM in New York, 7:00 PM in London).
inline Seconds saturday() { return utc(2026, 10, 24, 18, 0); }

inline SportsTeam team(const std::string& id, const std::string& name, std::optional<std::string> abbr = std::nullopt,
                       std::optional<std::string> location = std::nullopt) {
  SportsTeam t;
  t.id = id;
  t.name = name;
  t.shortName = std::move(abbr);
  t.location = std::move(location);
  return t;
}

inline SportsGame game(const std::string& league, const std::string& group, const std::string& id, SportsTeam home,
                       SportsTeam away, Seconds start, std::vector<std::string> networks = {},
                       std::optional<std::string> venue = std::nullopt, bool soccer = false) {
  SportsGame g;
  g.id = league + "|" + id;
  g.league = group;
  g.leagueShort = league;
  g.isSoccer = soccer;
  g.home = std::move(home);
  g.away = std::move(away);
  g.start = start;
  g.networks = std::move(networks);
  g.venue = std::move(venue);
  return g;
}

// A game as Live Events shows it, with the given sources.
inline SportsEvent eventOf(const SportsGame& g, std::vector<EventSource> sources = {}) {
  SportsEvent e;
  e.id = "sev|" + g.id;
  e.title = g.title();
  e.league = g.league;
  e.leagueShort = g.leagueShort;
  e.start = g.start;
  e.game = g;
  e.sources = std::move(sources);
  return e;
}

inline SportsEvent plainEvent(const std::string& id, const std::string& title, const std::string& league,
                              std::optional<Seconds> start, std::vector<EventSource> sources = {}) {
  SportsEvent e;
  e.id = id;
  e.title = title;
  e.league = league;
  e.leagueShort = league;
  e.start = start;
  e.sources = std::move(sources);
  return e;
}

inline EventSource source(const std::string& id, EventSource::Kind kind, bool spanish = false,
                          std::optional<int> rank = std::nullopt, const std::string& detail = "") {
  EventSource s;
  s.channelID = id;
  s.kind = kind;
  s.spanish = spanish;
  s.rank = rank;
  s.detail = detail;
  return s;
}

inline EventSource feed(const std::string& id, bool spanish = false) {
  return source(id, EventSource::Kind::provider, spanish, std::nullopt, "Provider event channel");
}

inline marigold::catalog::LiveEvent liveEvent(const std::string& id, const std::string& title,
                                              const std::string& league, std::optional<Seconds> start,
                                              std::vector<std::string> feeds, const std::string& family = "fam",
                                              std::optional<bool> guessed = false) {
  marigold::catalog::LiveEvent e;
  e.id = id;
  e.title = title;
  e.league = league;
  e.start = start;
  e.feeds = std::move(feeds);
  e.family = family;
  e.startGuessed = guessed;
  return e;
}

// One scoreboard competitor ("home" or "away").
inline std::string competitor(const std::string& homeAway, const std::string& teamJson,
                              const std::string& extra = "") {
  return "{\"homeAway\":\"" + homeAway + "\",\"team\":" + teamJson + (extra.empty() ? "" : "," + extra) + "}";
}

inline std::string teamJson(const std::string& id, const std::string& name, const std::string& abbr,
                            const std::string& more = "") {
  return "{\"id\":\"" + id + "\",\"displayName\":\"" + name + "\",\"abbreviation\":\"" + abbr + "\"" +
         (more.empty() ? "" : "," + more) + "}";
}

}  // namespace sportsdata
