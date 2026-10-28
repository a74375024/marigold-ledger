#include "core/sports/matcher.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <unordered_set>

#include "core/catalog/catalog.hpp"
#include "core/guide/names.hpp"
#include "core/sports/board.hpp"
#include "core/sports/network_map.hpp"
#include "core/sports/profiles.hpp"
#include "core/sports/text.hpp"

namespace marigold::sports::matcher {

namespace {

constexpr const char* kProviderDetail = "Provider event channel";

bool has(std::string_view hay, std::string_view needle) { return hay.find(needle) != std::string_view::npos; }

std::vector<std::string> splitWords(const std::string& normalizedText) { return text::words(normalizedText); }

std::string trimmed(const std::string& s) { return std::string(str::trimUnicode(s)); }

EventSource providerSource(const std::string& feed, bool spanish) {
  EventSource s;
  s.channelID = feed;
  s.kind = EventSource::Kind::provider;
  s.spanish = spanish;
  s.detail = kProviderDetail;
  return s;
}

bool containsSource(const std::vector<EventSource>& list, const std::string& id) {
  return std::any_of(list.begin(), list.end(), [&](const EventSource& s) { return s.channelID == id; });
}

void addFeeds(SportsEvent& value, const ProviderEvent& event) {
  for (const std::string& feed : event.feeds) {
    if (!containsSource(value.sources, feed)) value.sources.push_back(providerSource(feed, event.spanish));
  }
}

const std::set<std::string>& genericWords() {
  static const std::set<std::string> kWords = {
      "fc",   "cf",  "sc",  "afc", "club", "city",     "united", "the",       "team", "state",
      "university", "college", "real", "athletic", "sporting", "new", "san", "los", "las", "st"};
  return kWords;
}

bool sameLeague(const std::string& league, const SportsGame& game) {
  if (league.empty() || league == "Other") return false;
  if (league == game.league) return true;
  auto it = sportFamilies().find(league);
  return it != sportFamilies().end() && it->second.count(game.league) > 0;
}

bool compatible(const std::string& league, const SportsGame& game) {
  if (league.empty() || league == "Other" || league == game.league) return true;
  auto it = sportFamilies().find(league);
  return it != sportFamilies().end() && it->second.count(game.league) > 0;
}

SportsEvent plainEvent(const ProviderEvent& event) {
  SportsEvent e;
  e.id = event.id;
  e.title = event.title;
  e.league = event.league;
  e.leagueShort = shortLeague(event.league);
  e.start = event.start;
  for (const std::string& feed : event.feeds) e.sources.push_back(providerSource(feed, event.spanish));
  e.family = event.family;
  return e;
}

bool closeEnough(std::optional<Seconds> a, std::optional<Seconds> b) {
  if (!a || !b) return true;
  return std::llabs(*a - *b) <= 90 * 60;
}

struct RaceWords {
  std::set<std::string> words;
  std::optional<std::string> number;
};

// The race session a provider feed is: an F1 feed naming the session
// ("Formula 1: United States: Race [Main Broadcast]", "2617 GP F1
// Qualifying") at about its time, or the session on at the feed's time.
std::optional<int> raceTarget(const ProviderEvent& event, const std::vector<int>& sessions,
                              const std::vector<SportsGame>& games, const std::map<int, RaceWords>& raceWords,
                              const std::vector<std::string>& raceWordList,
                              const std::function<bool(std::string_view)>& isCameraName) {
  // Cheap tests first: most feeds aren't racing.
  std::string raw = event.title + " " + event.family;
  bool maybeF1 = str::icontains(raw, "f1") || str::icontains(raw, "formula");
  std::string lowered = raceWordList.empty() ? "" : str::lower(raw);
  bool maybeOther = !raceWordList.empty() &&
                    std::any_of(raceWordList.begin(), raceWordList.end(),
                                [&](const std::string& w) { return has(lowered, w); });
  if (!maybeF1 && !maybeOther) return std::nullopt;
  std::string norm = normalized(raw);
  bool isF1 = maybeF1 && (has(norm, " f1 ") || has(norm, " formula 1 ") || has(norm, " formula one ") ||
                          has(norm, " f1tv "));
  if (!isF1 && !maybeOther) return std::nullopt;
  // Cameras go to the race's cameras panel; shows around it aren't it.
  if ((isCameraName && isCameraName(event.title)) || isStudioShow(event.title)) return std::nullopt;
  std::optional<std::string> kind;
  if (isF1) kind = race_session::kindInTitle(normalized(event.title));
  // A guessed day isn't a time to go by (other races compare the clock).
  // (A flag and a plain value rather than an optional: GCC -O2 misreads the
  // optional as maybe uninitialized here.)
  const bool hasStart = !event.guessed && event.start.has_value();
  const Seconds startAt = hasStart ? *event.start : 0;
  if (!kind && !hasStart && !(maybeOther && event.start)) return std::nullopt;
  std::set<std::string> titleWords;
  if (maybeOther) {
    for (const std::string& w : splitWords(norm)) titleWords.insert(w);
  }
  // Another race (NASCAR) only from a racing feed, not a game.
  bool otherAllowed = maybeOther &&
                      (event.league == "Motorsport" || event.league == "Other" || event.league.empty()) &&
                      !looksLikeGame(event.title);
  bool saysNascar = has(norm, " nascar ") || has(norm, " cup series ");
  std::optional<std::pair<int, Seconds>> best, bestOther;
  for (int index : sessions) {
    const SportsGame& game = games[static_cast<size_t>(index)];
    Seconds gap = hasStart ? std::llabs(startAt - game.start) : 0;
    if (game.leagueShort == "F1") {
      if (!isF1) continue;
      if (kind && !(game.cardKey && str::endsWith(*game.cardKey, "|" + *kind))) continue;
      // A named session: the nearest of its kind (they're a week apart; a
      // feed's clock may be hours off). Otherwise the one on then.
      if (gap > (kind ? 14 * 3600 : 90 * 60)) continue;
      if (!best || gap < best->second) best = std::make_pair(index, gap);
    } else {
      // Another race: its name's word or number, with "NASCAR" or both, at
      // about its time (a day-less time: the same clock today).
      auto info = raceWords.find(index);
      if (!otherAllowed || info == raceWords.end()) continue;
      bool hasWord = std::any_of(info->second.words.begin(), info->second.words.end(),
                                 [&](const std::string& w) { return titleWords.count(w) > 0; });
      bool hasNumber = info->second.number && titleWords.count(*info->second.number) > 0;
      if (!((hasWord && (hasNumber || saysNascar)) || (hasNumber && saysNascar))) continue;
      if (event.guessed && event.start) {
        Seconds full = std::llabs(*event.start - game.start);
        Seconds rem = full % 86'400;
        if (std::min(rem, 86'400 - rem) > 75 * 60 || full >= 36 * 3600) continue;
      } else {
        if (!hasStart || gap > 3 * 3600) continue;
      }
      if (!bestOther || gap < bestOther->second) bestOther = std::make_pair(index, gap);
    }
  }
  // A NASCAR feed in a mixed "F1 / NASCAR" group goes to its race.
  if (bestOther) return bestOther->first;
  if (best) return best->first;
  return std::nullopt;
}

// The golf tournament a provider feed is ("Golf: The Masters - Round 2",
// "PGA Tour: Genesis Championship"): the one whose distinctive name words
// the title has most of, on the tour it names if it names one, on that day.
std::optional<int> golfTarget(const ProviderEvent& event, const std::vector<int>& tournaments,
                              const std::vector<SportsGame>& games) {
  if (tournaments.empty() || event.league != "Golf") return std::nullopt;
  static const std::set<std::string> kStop = {"championship", "championships", "classic", "invitational",
                                              "tournament",   "golf",          "presented", "series",
                                              "stroke",       "play",          "the",       "tour"};
  static const std::vector<std::pair<const char*, const char*>> kTours = {
      {" pga tour ", "PGA"}, {" lpga ", "LPGA"}, {" liv golf ", "LIV"}, {" dp world ", "DPWT"}};
  std::string title = normalized(event.title + " " + event.family);
  std::optional<std::string> tour;
  for (const auto& [words, shortName] : kTours) {
    if (has(title, words)) tour = shortName;
  }
  const bool hasStart = !event.guessed && event.start.has_value();
  std::optional<std::pair<int, int>> best;  // index, words matched
  for (int index : tournaments) {
    const SportsGame& game = games[static_cast<size_t>(index)];
    if (tour && game.leagueShort != *tour) continue;
    // The day's play runs from the round's start into the evening.
    if (hasStart && (*event.start < game.start - 4 * 3600 || *event.start > game.start + 14 * 3600)) continue;
    int matched = 0;
    for (const std::string& w : splitWords(normalized(game.home.name))) {
      if (text::count(w) >= 3 && !kStop.count(w) && has(title, " " + w + " ")) matched += 1;
    }
    if (matched == 0 && !tour) continue;
    if (!best || matched > best->second) best = std::make_pair(index, matched);
  }
  if (!best) return std::nullopt;
  return best->first;
}

int64_t elapsedMs(std::chrono::steady_clock::time_point since) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - since)
      .count();
}

}  // namespace

// MARK: Listings

json::Value toJson(const GuideListing& l) {
  json::Value v = json::Value::object();
  v.set("entryID", l.entryID);
  v.set("inLineup", l.inLineup);
  v.set("spanish", l.spanish);
  v.set("title", l.title);
  v.set("start", static_cast<int64_t>(l.start));
  v.set("end", static_cast<int64_t>(l.end));
  if (l.detail) v.set("detail", *l.detail);
  if (l.venue) v.set("venue", *l.venue);
  if (l.city) v.set("city", *l.city);
  if (l.rank) v.set("rank", *l.rank);
  return v;
}

std::optional<GuideListing> listingFromJson(const json::Value& v) {
  if (!v["entryID"].isString() || !v["title"].isString() || !v["start"].isNumber() || !v["end"].isNumber()) {
    return std::nullopt;
  }
  GuideListing l;
  l.entryID = v["entryID"].asString();
  l.inLineup = v["inLineup"].asBool();
  l.spanish = v["spanish"].asBool();
  l.title = v["title"].asString();
  l.start = v["start"].asInt();
  l.end = v["end"].asInt();
  if (v["detail"].isString()) l.detail = v["detail"].asString();
  if (v["venue"].isString()) l.venue = v["venue"].asString();
  if (v["city"].isString()) l.city = v["city"].asString();
  if (v["rank"].isNumber()) l.rank = static_cast<int>(v["rank"].asInt());
  return l;
}

std::optional<std::pair<std::string, std::string>> venueOf(std::string_view description) {
  auto m = rx::first(
      "^\\s*(?:live\\s+)?(?:from|at)\\s+(?:the\\s+)?(.+?)\\s+in\\s+([A-Z][^,.;]+?)(?:,\\s*[A-Z][A-Za-z.]*\\.?)?(?:[.;]|$)",
      description, true);
  if (!m || !m->has(1) || !m->has(2)) return std::nullopt;
  std::string venue = trimmed(normalized(m->group(1)));
  std::string city = trimmed(normalized(m->group(2)));
  if (text::count(venue) < 4 || city.empty()) return std::nullopt;
  return std::make_pair(venue, city);
}

bool isGenericSportsTitle(std::string_view title) {
  return rx::test(
      "\\b(nfl|nba|wnba|nhl|mlb|mls|nwsl|ncaa|college|basketball|football|hockey|baseball|soccer|f[u\xC3\xBA]tbol|b[e\xC3\xA9]isbol|baloncesto)\\b",
      title, true);
}

const std::set<std::string>& completeLeagues() {
  static const std::set<std::string> kLeagues = [] {
    std::set<std::string> out;
    for (const Profile& p : profiles())
      if (p.completeSchedule && p.leagueShort.empty()) out.insert(p.league);
    return out;
  }();
  return kLeagues;
}

const std::set<std::string>& supportedLeagues() {
  static const std::set<std::string> kLeagues = {"NFL", "College Football", "NBA", "MLB",
                                                 "NHL", "MLS",              "Soccer"};
  return kLeagues;
}

std::string Stats::summary() const {
  std::vector<std::string> steps;
  for (const auto& [step, ms] : milliseconds) steps.push_back(step + " " + std::to_string(ms));
  auto n = [](int v) { return std::to_string(v); };
  std::string out = "games " + n(games) + ", events " + n(events) + " \xE2\x86\x92 " + n(filtered) +
                    " game-like, " + n(matchedEvents) + " matched, " + n(guessedMatched) + " by day, " +
                    n(replays) + " replays and " + n(stale) + " stale hidden, " + n(mergedPlain) +
                    " duplicate feeds merged, listings " + n(listings) + " \xE2\x86\x92 " +
                    n(matchedListings) + " matched (" + n(venueListings) + " by venue, " +
                    n(describedListings) + " by description), " + n(trimmedStations) +
                    " extra stations folded, school pairs " + n(schoolPairs) + ", name pairs " +
                    n(namePairs) + ", fight feeds " + n(fightFeeds) + ", shows " + n(shows) +
                    " and past cards " + n(pastCards) + " hidden";
  if (!offsets.empty()) out += ", offsets " + str::join(offsets, "; ");
  out += "; ms: " + str::join(steps, ", ");
  if (aborted) out += "; ABORTED: " + *aborted;
  return out;
}

// MARK: Build

std::optional<BuildResult> build(const BuildInput& in) {
  const std::vector<SportsGame>& games = in.games;
  const Seconds now = in.now;
  Stats stats;
  stats.events = static_cast<int>(in.events.size());
  stats.games = static_cast<int>(games.size());
  stats.listings = static_cast<int>(in.listings.size());
  auto clock = std::chrono::steady_clock::now();
  auto lap = [&](const std::string& step) {
    stats.milliseconds[step] = static_cast<int>(elapsedMs(clock));
    clock = std::chrono::steady_clock::now();
  };
  auto stop = [&]() -> bool {
    if (!in.shouldStop) return false;
    if (auto reason = in.shouldStop()) {
      stats.aborted = *reason;
      return true;
    }
    return false;
  };

  TeamIndex index(games);
  lap("0 index");
  const size_t gameCount = games.size();
  std::vector<std::optional<SportsEvent>> byGame(gameCount);
  std::vector<std::optional<std::string>> firstProviderID(gameCount);
  auto entry = [&](size_t gameIndex) -> SportsEvent {
    if (byGame[gameIndex]) return *byGame[gameIndex];
    const SportsGame& game = games[gameIndex];
    SportsEvent e;
    e.id = "sev|" + game.id;
    e.title = game.title();
    e.league = game.league;
    e.leagueShort = game.leagueShort;
    e.start = game.start;
    e.game = game;
    return e;
  };

  // 0. Channels airing each game's TV network.
  for (size_t i = 0; i < gameCount; ++i) {
    auto it = in.channelSources.find(games[i].id);
    if (it == in.channelSources.end() || it->second.empty()) continue;
    SportsEvent value = entry(i);
    value.sources = it->second;
    byGame[i] = std::move(value);
  }

  // 1. Provider event feeds -> games. Only titles that look like a game
  // (two sides) or are in a league the schedule covers.
  const Seconds low = now - 8 * 3600, high = now + 8 * 86'400;
  std::vector<ProviderEvent> plain;
  std::vector<ProviderEvent> unmatched;
  // 1a. Which scheduled games each game-like title names (any time).
  std::vector<std::pair<ProviderEvent, std::vector<int>>> pending;
  // Fight cards by promotion and number ("ufc|333"), for feeds named after
  // the card rather than the fighters.
  std::map<std::string, std::vector<int>> cards;
  for (size_t i = 0; i < gameCount; ++i) {
    if (!games[i].cardKey) continue;
    std::vector<std::string> parts = text::components(*games[i].cardKey, "|");
    if (parts.size() != 3) continue;
    cards[parts[0] + "|" + parts[1]].push_back(static_cast<int>(i));
  }
  // Race weekend sessions, for feeds named after a session; golf
  // tournaments, for feeds named after one.
  std::vector<int> raceSessions, golfTournaments, tennisMatches;
  for (size_t i = 0; i < gameCount; ++i) {
    if (games[i].isRacing()) raceSessions.push_back(static_cast<int>(i));
    if (games[i].isGolf()) golfTournaments.push_back(static_cast<int>(i));
    if (games[i].isTennis()) tennisMatches.push_back(static_cast<int>(i));
  }
  // Other races (NASCAR) are found by their name's distinctive words and
  // number ("YellaWood 500" -> yellawood, 500).
  std::map<int, RaceWords> raceWords;
  static const std::set<std::string> kRaceStop = {"nascar", "series", "racing", "speedway",
                                                  "motor",  "sports", "grand"};
  for (int i : raceSessions) {
    const SportsGame& game = games[static_cast<size_t>(i)];
    if (game.leagueShort == "F1") continue;
    std::vector<std::string> tokens = splitWords(normalized(game.home.name));
    RaceWords info;
    for (const std::string& t : tokens) {
      if (text::count(t) >= 5 && !text::isAllDigits(t) && !kRaceStop.count(t)) info.words.insert(t);
    }
    for (const std::string& t : tokens) {
      if (text::count(t) >= 3 && text::isAllDigits(t)) {
        info.number = t;
        break;
      }
    }
    if (!info.words.empty() || info.number) raceWords[i] = info;
  }
  std::vector<std::string> raceWordList;
  if (!raceWords.empty()) {
    std::set<std::string> all = {"nascar"};
    for (const auto& [i, info] : raceWords) all.insert(info.words.begin(), info.words.end());
    raceWordList.assign(all.begin(), all.end());
  }
  for (const ProviderEvent& event : in.events) {
    bool inWindow = !event.start || (*event.start > low && *event.start < high);
    if (inWindow && !raceSessions.empty()) {
      if (auto target = raceTarget(event, raceSessions, games, raceWords, raceWordList, in.isCameraName)) {
        size_t t = static_cast<size_t>(*target);
        stats.filtered += 1;
        SportsEvent value = entry(t);
        // The main broadcast first, then other feeds.
        bool main = has(str::lower(event.title), "main broadcast");
        for (const std::string& feed : event.feeds) {
          if (containsSource(value.sources, feed)) continue;
          EventSource source = providerSource(feed, event.spanish);
          auto at = std::find_if(value.sources.begin(), value.sources.end(),
                                 [](const EventSource& s) { return s.kind == EventSource::Kind::provider; });
          if (main && at != value.sources.end()) {
            value.sources.insert(at, source);
          } else {
            value.sources.push_back(source);
          }
        }
        if (!value.family) value.family = event.family;
        byGame[t] = std::move(value);
        if (!firstProviderID[t]) firstProviderID[t] = event.id;
        stats.matchedEvents += 1;
        continue;
      }
    }
    if (inWindow) {
      if (auto target = golfTarget(event, golfTournaments, games)) {
        size_t t = static_cast<size_t>(*target);
        stats.filtered += 1;
        SportsEvent value = entry(t);
        for (const std::string& feed : event.feeds) {
          if (!containsSource(value.sources, feed)) value.sources.push_back(providerSource(feed, event.spanish));
        }
        if (!value.family) value.family = event.family;
        byGame[t] = std::move(value);
        if (!firstProviderID[t]) firstProviderID[t] = event.id;
        stats.matchedEvents += 1;
        continue;
      }
    }
    // A tennis tournament's feed ("Day 3 WTA 500"): each of the day's
    // matches gets it, so every match is a tile with its score.
    if (inWindow && !tennisMatches.empty() && event.league == "Tennis") {
      std::vector<int> targets = tennisTargets(event.title, event.family, event.guessed ? std::nullopt : event.start,
                                               now, tennisMatches, games);
      if (!targets.empty()) {
        stats.filtered += 1;
        for (int gi : targets) {
          size_t g = static_cast<size_t>(gi);
          SportsEvent value = entry(g);
          addFeeds(value, event);
          if (!value.family) value.family = event.family;
          byGame[g] = std::move(value);
        }
        stats.matchedEvents += 1;
        continue;
      }
    }
    // A card named by its number goes by the card and its times; one named
    // only by its fighters ("UFC: Pereira vs Hill") by the fighters.
    if (inWindow && !cards.empty() && event.league == "Fighting") {
      auto key = fightKey(event.title);
      if (key && (!key->number.empty() || !looksLikeGame(event.title))) {
        std::vector<int> targets =
            fightTargets(*key, event.guessed ? std::nullopt : event.start, cards, games);
        if (!targets.empty()) {
          stats.filtered += 1;
          stats.fightFeeds += 1;
          for (int gi : targets) {
            size_t g = static_cast<size_t>(gi);
            SportsEvent value = entry(g);
            addFeeds(value, event);
            if (!value.family) value.family = event.family;
            byGame[g] = std::move(value);
            if (!firstProviderID[g] && targets.size() == 1) firstProviderID[g] = event.id;
          }
          stats.matchedEvents += 1;
          continue;
        }
      }
    }
    if (!inWindow || !(looksLikeGame(event.title) || supportedLeagues().count(event.league))) {
      plain.push_back(event);
      continue;
    }
    stats.filtered += 1;
    if (games.empty()) {
      unmatched.push_back(event);
      continue;
    }
    std::vector<int> candidates = index.matches(
        event.title, [&](int g) { return compatible(event.league, games[static_cast<size_t>(g)]); },
        [&](int g) { return sameLeague(event.league, games[static_cast<size_t>(g)]); });
    if (candidates.empty() && event.start && !event.guessed) {
      // One game of any league at that time with both teams named, even by
      // its own abbreviations ("NJ vs. DAL" from a channel listing several
      // events under one sport's name): the time settles it.
      auto atTime = [&](int g) { return std::llabs(games[static_cast<size_t>(g)].start - *event.start) <= 45 * 60; };
      std::vector<int> timed = index.matches(event.title, atTime, atTime);
      if (timed.size() == 1) candidates = timed;
    }
    // An event of unknown league matching games of more than one league
    // (Panthers, Rangers, Kings...) is too ambiguous.
    bool ambiguous = false;
    if (event.league.empty() || event.league == "Other") {
      std::set<std::string> leagues;
      for (int g : candidates) leagues.insert(games[static_cast<size_t>(g)].league);
      ambiguous = leagues.size() > 1;
      // Two schools playing soccer Thursday and volleyball Friday: the
      // feed's time says which.
      if (ambiguous && event.start && !event.guessed) {
        std::vector<int> near;
        std::set<std::string> nearLeagues;
        for (int g : candidates) {
          if (std::llabs(games[static_cast<size_t>(g)].start - *event.start) > 90 * 60) continue;
          near.push_back(g);
          nearLeagues.insert(games[static_cast<size_t>(g)].league);
        }
        if (nearLeagues.size() == 1) {
          candidates = std::move(near);
          ambiguous = false;
        }
      }
    }
    if (candidates.empty() || ambiguous) {
      unmatched.push_back(event);
      continue;
    }
    pending.emplace_back(event, std::move(candidates));
  }

  // 1b. Each event channel group's clock offset, learned from titles that
  // name a scheduled game: the same whole-hour gap on most of them means the
  // group writes another time zone (UTC, CET...). An hour early is usually
  // the pregame show, not a time zone: ignored.
  std::map<std::string, std::map<int, int>> votes;
  std::map<std::string, int> voteTotals;
  for (const auto& item : pending) {
    const ProviderEvent& event = item.first;
    if (!event.start || event.guessed) continue;
    std::optional<Seconds> nearest;
    // Tennis times are a court's order of play: no clock to learn from.
    for (int g : item.second) {
      const SportsGame& game = games[static_cast<size_t>(g)];
      if (game.isTennis()) continue;
      Seconds gap = *event.start - game.start;
      if (std::llabs(gap) <= 14 * 3600 && (!nearest || std::llabs(gap) < std::llabs(*nearest))) nearest = gap;
    }
    if (!nearest) continue;
    double hours = std::round(static_cast<double>(*nearest) / 3600.0);
    if (std::fabs(static_cast<double>(*nearest) - hours * 3600) > 20 * 60) continue;
    votes[event.family][static_cast<int>(hours)] += 1;
    voteTotals[event.family] += 1;
  }
  std::map<std::string, Seconds> offsets;
  for (const auto& [family, counts] : votes) {
    std::optional<std::pair<int, int>> best;
    for (const auto& [hours, count] : counts) {
      // Most votes; on a tie the smaller shift.
      if (!best || count > best->second || (count == best->second && std::abs(hours) < std::abs(best->first))) {
        best = std::make_pair(hours, count);
      }
    }
    if (!best || std::abs(best->first) < 2 || best->second < 2 || best->second * 2 <= voteTotals[family]) {
      continue;
    }
    offsets[family] = Seconds{best->first} * 3600;
  }
  for (const auto& [family, offset] : offsets) {
    if (stats.offsets.size() >= 8) break;
    int hours = static_cast<int>(offset / 3600);
    stats.offsets.push_back(family + " " + (offset > 0 ? "+" : "") + std::to_string(hours) + "h");
  }
  auto corrected = [&](const ProviderEvent& event) -> ProviderEvent {
    if (event.guessed || !event.start) return event;
    auto it = offsets.find(event.family);
    if (it == offsets.end()) return event;
    ProviderEvent copy = event;
    copy.start = *event.start - it->second;
    return copy;
  };
  // How far apart two moments are on the clock, ignoring the day.
  auto clockGap = [](Seconds a, Seconds b) {
    Seconds gap = std::llabs(a - b) % 86'400;
    return std::min(gap, 86'400 - gap);
  };

  // 1c. The schedule decides. A dated title is the game when it airs within
  // 3 h of it; further away it's a replay (hidden). A title with no day goes
  // to that matchup's next game (at its clock time when one is given).
  // Per channel group, titles with no day that named a finished game
  // (yesterday's slate still in its slots) vs. a game still to come.
  std::map<std::string, int> staleByFamily, freshByFamily;
  const Seconds startOfToday = text::startOfDay(now, in.zone);

  // 0. Titles with no day that another feed dates: "SMITH vs PUELLO 5:00pm"
  // in one group, "Matchroom Boxing: Smith vs Puello (10.24 2:00PM ET)" in
  // another. When every dated copy aired before today, the day-less one is a
  // slot left over from it (so are its group's other day-less titles, unless
  // one of them is dated today or later too).
  std::set<std::string> leftovers;
  std::map<std::string, int> leftoverByFamily, currentByFamily;
  {
    struct Dated {
      std::set<std::string> words;
      Seconds start;
    };
    std::vector<Dated> dated;
    std::unordered_map<std::string, std::vector<size_t>> byWord;
    std::vector<const ProviderEvent*> datedEvents;
    for (const ProviderEvent& event : in.events) datedEvents.push_back(&event);
    for (const ProviderEvent& event : in.pastEvents) datedEvents.push_back(&event);
    for (const ProviderEvent* e : datedEvents) {
      const ProviderEvent& event = *e;
      if (event.guessed || !event.start) continue;
      std::set<std::string> words = titleWords(event.title);
      if (words.size() < 2) continue;
      for (const std::string& w : words) byWord[w].push_back(dated.size());
      dated.push_back({std::move(words), *event.start});
    }
    for (const ProviderEvent& event : in.events) {
      if (!event.guessed) continue;
      std::set<std::string> words = titleWords(event.title);
      if (words.size() < 2) continue;
      // Candidates through the rarest word; a copy has every word.
      const std::vector<size_t>* fewest = nullptr;
      for (const std::string& w : words) {
        auto it = byWord.find(w);
        if (it == byWord.end()) {
          fewest = nullptr;
          break;
        }
        if (!fewest || it->second.size() < fewest->size()) fewest = &it->second;
      }
      if (!fewest) continue;
      bool before = false, current = false;
      for (size_t i : *fewest) {
        const Dated& d = dated[i];
        if (!std::includes(d.words.begin(), d.words.end(), words.begin(), words.end())) continue;
        (d.start < startOfToday ? before : current) = true;
      }
      std::string familyKey = event.family + "|" + event.league;
      if (current) {
        currentByFamily[familyKey] += 1;
      } else if (before) {
        leftovers.insert(event.id);
        leftoverByFamily[familyKey] += 1;
      }
    }
  }
  // A day-less title that is a leftover: dated before today elsewhere, or in
  // a group whose day-less titles are (one at least, none current: a group
  // of slots the provider stopped updating).
  auto leftover = [&](const ProviderEvent& event) {
    if (!event.guessed) return false;
    if (leftovers.count(event.id)) return true;
    std::string familyKey = event.family + "|" + event.league;
    auto old = leftoverByFamily.find(familyKey);
    return !event.family.empty() && old != leftoverByFamily.end() && !currentByFamily.count(familyKey);
  };

  // College football: the week's schedule has every game the schools in it
  // play, so a day-less title pairing one of them with a school it doesn't
  // play this week is a slot from an earlier week ("Boise State @
  // Washington State 6PM").
  std::vector<SportsTeam> fbs;
  std::set<std::string> fbsIDs;
  for (const SportsGame& game : games) {
    if (game.league != "College Football") continue;
    for (const SportsTeam* team : {&game.home, &game.away}) {
      if (fbsIDs.insert(team->id).second) fbs.push_back(*team);
    }
  }
  std::optional<TeamIndex> fbsIndex, schoolsIndex;
  if (!fbs.empty()) {
    fbsIndex = TeamIndex::fromTeams(fbs, true);
    if (!in.schools.empty()) schoolsIndex = TeamIndex::fromTeams(in.schools);
  }
  // One side named as the schedule names the school ("BYU", "UCF", "USC",
  // "Wisconsin"), so short names count too.
  std::set<std::string> fbsNames;
  for (const SportsTeam& team : fbs) {
    for (const std::optional<std::string>& name : {std::optional<std::string>(team.name), team.shortName, team.location}) {
      if (name) fbsNames.insert(trimmed(normalized(*name)));
    }
    for (const std::string& name : team.alternates) fbsNames.insert(trimmed(normalized(name)));
  }
  auto collegeLeftover = [&](const ProviderEvent& event) {
    if (!event.guessed || event.league != "College Football" || !fbsIndex) return false;
    if (fbsIndex->pair(event.title)) return true;
    if (schoolsIndex) {
      auto pair = schoolsIndex->pair(event.title);
      if (pair && (fbsIDs.count(pair->first.id) || fbsIDs.count(pair->second.id))) return true;
    }
    auto sep = rx::first("\\s+(?:@|at|vs\\.?|v)\\s+", event.title, true);
    if (!sep) return false;
    std::vector<std::string> sides = {event.title.substr(0, sep->begin), event.title.substr(sep->end)};
    return std::any_of(sides.begin(), sides.end(),
                       [&](const std::string& side) { return fbsNames.count(trimmed(normalized(side))) > 0; });
  };
  for (const auto& item : pending) {
    ProviderEvent event = corrected(item.first);
    std::optional<int> chosen;
    // A singles feed (no "/") doesn't go to the same players' doubles.
    std::vector<int> candidates = item.second;
    if (!has(event.title, "/")) {
      std::vector<int> singles;
      for (int g : candidates) {
        const SportsGame& game = games[static_cast<size_t>(g)];
        if (!game.isTennis() || !(has(game.home.name, " / ") || has(game.away.name, " / "))) singles.push_back(g);
      }
      if (!singles.empty()) candidates = singles;
    }
    if (event.start && !event.guessed) {
      std::optional<std::pair<int, Seconds>> best;
      for (int g : candidates) {
        const SportsGame& game = games[static_cast<size_t>(g)];
        Seconds gap = std::llabs(*event.start - game.start);
        // A match over several days (a Test): any of its days.
        if (game.end && *event.start >= game.start - 3 * 3600 && *event.start <= *game.end + 10 * 3600) gap = 0;
        if (!best || gap < best->second) best = std::make_pair(g, gap);
      }
      // Tennis: a later match on a court can start hours after its listed time.
      const SportsGame* bestGame = best ? &games[static_cast<size_t>(best->first)] : nullptr;
      if (best && best->second <= profile(bestGame->league, bestGame->leagueShort).matchWindow) {
        chosen = best->first;
      }
    } else {
      std::vector<int> open;
      for (int g : candidates) {
        const SportsGame& game = games[static_cast<size_t>(g)];
        if (!game.completed && game.start > now - 6 * 3600 && game.start < now + 7 * 86'400) open.push_back(g);
      }
      std::stable_sort(open.begin(), open.end(), [&](int a, int b) {
        return games[static_cast<size_t>(a)].start < games[static_cast<size_t>(b)].start;
      });
      // The matchup's next game; on a doubleheader day, the one the title
      // names ("Game 2"), else the one on now (the latest that started).
      auto next = [&]() -> std::optional<int> {
        if (open.empty()) return std::nullopt;
        const SportsGame& first = games[static_cast<size_t>(open.front())];
        if (!profile(first.league, first.leagueShort).doubleheaders) return open.front();
        std::vector<int> day;
        for (int g : open)
          if (games[static_cast<size_t>(g)].start - first.start < 12 * 3600) day.push_back(g);
        if (day.size() < 2) return open.front();
        if (rx::test("\\b(game|gm)\\s*2\\b", event.title, true)) return day[1];
        if (rx::test("\\b(game|gm)\\s*1\\b", event.title, true)) return day[0];
        int on = day.front();
        for (int g : day)
          if (games[static_cast<size_t>(g)].start <= now) on = g;
        return on;
      };
      if (event.start) {
        auto at = std::find_if(open.begin(), open.end(), [&](int g) {
          return clockGap(games[static_cast<size_t>(g)].start, *event.start) <= 75 * 60;
        });
        if (at != open.end()) {
          chosen = *at;
        } else {
          chosen = next();
        }
      } else {
        chosen = next();
      }
      // Counted per group and sport, for day-less titles only: fresh when it
      // names a game still to come, stale when the game it names was before
      // today (not earlier today).
      std::string familyKey = event.family + "|" + event.league;
      if (chosen) {
        stats.guessedMatched += 1;
        if (event.guessed && !event.family.empty()) freshByFamily[familyKey] += 1;
      } else if (event.guessed && !event.family.empty() &&
                 std::any_of(candidates.begin(), candidates.end(), [&](int g) {
                   return games[static_cast<size_t>(g)].start < startOfToday;
                 })) {
        staleByFamily[familyKey] += 1;
      }
    }
    if (!chosen) {
      // A dated feed of no known sport days away from the game its schools
      // play is another sport's event between them (Thursday's volleyball,
      // Saturday's football), not a replay.
      if (event.start && !event.guessed && (event.league.empty() || event.league == "Other")) {
        unmatched.push_back(item.first);
        continue;
      }
      stats.replays += 1;
      continue;
    }
    size_t g = static_cast<size_t>(*chosen);
    stats.matchedEvents += 1;
    SportsEvent value = entry(g);
    addFeeds(value, event);
    if (!value.family) value.family = event.family;
    byGame[g] = std::move(value);
    if (!firstProviderID[g]) firstProviderID[g] = event.id;
  }
  lap("1 events");
  if (stop()) return std::nullopt;

  // 2. Guide listings -> games (only the game itself, not the shows around
  // it), up to two days ahead. Local and regional listings rarely name the
  // teams ("Live: NBA Basketball"), but their description names the venue,
  // which the schedule has too. Capped at 2 s.
  std::vector<char> relevant(gameCount, 0);
  bool anyRelevant = false;
  for (size_t i = 0; i < gameCount; ++i) {
    const SportsGame& game = games[i];
    if (!game.completed && game.start > now - 5 * 3600 && game.start < now + 48 * 3600) {
      relevant[i] = 1;
      anyRelevant = true;
    }
  }
  auto isRelevant = [&](int g) { return g >= 0 && static_cast<size_t>(g) < gameCount && relevant[static_cast<size_t>(g)]; };
  std::map<std::string, std::vector<int>> byVenue, byCity;
  for (size_t i = 0; i < gameCount; ++i) {
    // A tennis "venue" is a court ("Centre Court"), not a place to find.
    if (!relevant[i] || games[i].isTennis() || !games[i].venue) continue;
    // "Golden 1 Center, Sacramento": the city follows the last comma.
    std::vector<std::string> parts = text::components(*games[i].venue, ", ");
    std::string name;
    if (parts.size() > 1) {
      std::vector<std::string> head(parts.begin(), parts.end() - 1);
      name = trimmed(normalized(str::join(head, ", ")));
    } else {
      name = trimmed(normalized(*games[i].venue));
    }
    byVenue[name].push_back(static_cast<int>(i));
    if (parts.size() > 1) byCity[trimmed(normalized(parts.back()))].push_back(static_cast<int>(i));
  }
  if (anyRelevant) {
    auto started = std::chrono::steady_clock::now();
    for (const GuideListing& listing : in.listings) {
      if (elapsedMs(started) > kListingsBudgetMs) {
        stats.aborted = "listings over 2 s";
        break;
      }
      if (excluded(normalized(listing.title))) continue;
      auto airing = [&](int g) {
        const SportsGame& game = games[static_cast<size_t>(g)];
        return listing.start < game.start + 2 * 3600 && listing.end > game.start - 15 * 60;
      };
      auto filtered = [&](std::vector<int> list, const std::function<bool(int)>& keep) {
        std::vector<int> out;
        for (int g : list) {
          if (keep(g)) out.push_back(g);
        }
        return out;
      };
      std::vector<int> candidates = filtered(index.matches(listing.title, isRelevant), airing);
      std::string how = "title";
      if (candidates.empty() && (listing.venue || listing.detail)) {
        std::string league = catalog::leagueFor("", "", listing.title);
        auto fits = [&](int g) { return airing(g) && compatible(league, games[static_cast<size_t>(g)]); };
        if (listing.venue) {
          // The venue, at that time, in that sport; else the city when only
          // one such game is there.
          auto v = byVenue.find(*listing.venue);
          if (v != byVenue.end()) candidates = filtered(v->second, fits);
          if (candidates.empty() && listing.city) {
            auto c = byCity.find(*listing.city);
            if (c != byCity.end()) {
              std::vector<int> inCity = filtered(c->second, fits);
              if (inCity.size() == 1) candidates = inCity;
            }
          }
          how = "venue";
        } else if (listing.detail) {
          // The description names one game's two teams.
          std::vector<int> found = filtered(
              index.matches(*listing.detail,
                            [&](int g) { return isRelevant(g) && compatible(league, games[static_cast<size_t>(g)]); }),
              airing);
          if (found.size() == 1) candidates = found;
          how = "description";
        }
      }
      if (!candidates.empty()) {
        if (how == "venue") {
          stats.venueListings += 1;
        } else if (how == "description") {
          stats.describedListings += 1;
        }
      }
      for (int gi : candidates) {
        size_t g = static_cast<size_t>(gi);
        stats.matchedListings += 1;
        SportsEvent value = entry(g);
        if (!value.listingEnd || listing.end > *value.listingEnd) value.listingEnd = listing.end;
        if (!containsSource(value.sources, listing.entryID)) {
          int rank = listing.rank.value_or(listing.inLineup ? station_rank::lineup : station_rank::elsewhere);
          EventSource source;
          source.channelID = listing.entryID;
          source.kind = listing.inLineup ? EventSource::Kind::lineup : EventSource::Kind::guide;
          source.spanish = listing.spanish;
          source.detail = stationDetail(rank);
          source.rank = rank;
          value.sources.push_back(std::move(source));
        }
        byGame[g] = std::move(value);
      }
    }
  }
  lap("2 listings");
  if (stop()) return std::nullopt;

  // Feeds no game matched that aren't events at all, set aside before
  // they're grouped:
  // - in a team sport (gamesOnly), a title with no two sides is a show about
  //   the sport, and a matchup marked as a replay, classic or highlights is
  //   an old game (a whip-around's channel stays: it shows live games);
  // - a fight feed marked the same way is an old card, and one for a card
  //   its promotion's schedule doesn't have is a card that's over (last
  //   week's "UFC 333 Prelims").
  std::set<std::string> scheduledOrgs;
  for (const auto& [card, indices] : cards) scheduledOrgs.insert(text::components(card, "|").front());
  auto notAnEvent = [&](const ProviderEvent& event) {
    // "2024 Round 5: Detroit", "Harbor vs Valley 9 Feb": an old race or game
    // shown again.
    if (namesPastYear(event.title, now) || namesPastDate(event.title, now)) {
      stats.replays += 1;
      return true;
    }
    bool old = excluded(normalized(event.title));
    if (profile(event.league).gamesOnly && !board::whipNamed(event.title)) {
      if (old || !looksLikeGame(event.title)) {
        stats.shows += 1;
        return true;
      }
    }
    if (event.league == "Fighting") {
      if (old) {
        stats.shows += 1;
        return true;
      }
      auto key = fightKey(event.title);
      if (key && scheduledOrgs.count(key->org)) {
        stats.pastCards += 1;
        return true;
      }
    }
    return false;
  };

  // 3. Provider events that matched no game: grouped by matchup (either
  // team order, start within 90 minutes) through a key lookup.
  std::vector<SportsEvent> loose;
  std::map<std::string, std::vector<size_t>> byPair;
  // A group whose day-less college football titles are mostly leftovers:
  // its others are too (a misspelt school: "UAlbany at lowa").
  std::map<std::string, int> collegeLeftovers;
  for (const ProviderEvent& event : unmatched) {
    if (collegeLeftover(event)) collegeLeftovers[event.family + "|" + event.league] += 1;
  }
  for (const ProviderEvent& original : unmatched) {
    if (notAnEvent(original)) continue;
    ProviderEvent event = corrected(original);
    // A group whose day-less titles are mostly yesterday's games: its other
    // day-less titles are too.
    std::string familyKey = event.family + "|" + event.league;
    int staleCount = staleByFamily.count(familyKey) ? staleByFamily[familyKey] : 0;
    int freshCount = freshByFamily.count(familyKey) ? freshByFamily[familyKey] : 0;
    int collegeCount = collegeLeftovers.count(familyKey) ? collegeLeftovers[familyKey] : 0;
    bool collegeGroup = event.guessed && event.league == "College Football" && !event.family.empty() &&
                        collegeCount >= 3 && collegeCount > 3 * freshCount;
    if ((event.guessed && !event.family.empty() && staleCount >= 3 && staleCount > 3 * freshCount) ||
        leftover(event) || collegeLeftover(event) || collegeGroup) {
      stats.stale += 1;
      continue;
    }
    std::optional<std::string> key = pairKey(event.title);
    if (key) {
      auto list = byPair.find(*key);
      if (list != byPair.end()) {
        auto at = std::find_if(list->second.begin(), list->second.end(),
                               [&](size_t i) { return closeEnough(loose[i].start, event.start); });
        if (at != list->second.end()) {
          addFeeds(loose[*at], event);
          continue;
        }
      }
    }
    loose.push_back(plainEvent(event));
    if (key) byPair[*key].push_back(loose.size() - 1);
  }
  // Fight feeds no card matched: one tile per card part ("UFC 333 Prelims"
  // in English, Spanish and from another provider). The same feed title
  // from several channel groups at the same time: one tile with every feed.
  std::vector<SportsEvent> plainTiles;
  std::map<std::string, std::vector<size_t>> byCard, byTitle;
  auto sameTime = [](std::optional<Seconds> a, std::optional<Seconds> b) {
    if (!a || !b) return !a && !b;
    return std::llabs(*a - *b) <= 10 * 60;
  };
  for (const ProviderEvent& original : plain) {
    if (notAnEvent(original)) continue;
    if (leftover(original)) {
      stats.stale += 1;
      continue;
    }
    ProviderEvent event = corrected(original);
    std::optional<FightKey> key;
    if (event.league == "Fighting") key = fightKey(event.title);
    if (!key || key->number.empty()) {
      std::string titleKey = normalized(event.title) + "|" + event.league;
      auto list = byTitle.find(titleKey);
      if (list != byTitle.end()) {
        auto at = std::find_if(list->second.begin(), list->second.end(),
                               [&](size_t i) { return sameTime(plainTiles[i].start, event.start); });
        if (at != list->second.end()) {
          addFeeds(plainTiles[*at], event);
          stats.mergedPlain += 1;
          continue;
        }
      }
      plainTiles.push_back(plainEvent(event));
      byTitle[titleKey].push_back(plainTiles.size() - 1);
      continue;
    }
    std::string cardID = key->org + "|" + key->number + "|" + key->part.value_or("");
    auto list = byCard.find(cardID);
    if (list != byCard.end()) {
      auto at = std::find_if(list->second.begin(), list->second.end(),
                             [&](size_t i) { return closeEnough(plainTiles[i].start, event.start); });
      if (at != list->second.end()) {
        SportsEvent& tile = plainTiles[*at];
        // The English feed names the tile.
        bool allSpanish = std::all_of(tile.sources.begin(), tile.sources.end(),
                                      [](const EventSource& s) { return s.spanish; });
        if (!event.spanish && allSpanish) {
          std::vector<EventSource> previous = tile.sources;
          tile = plainEvent(event);
          for (const EventSource& source : previous) {
            if (!containsSource(tile.sources, source.channelID)) tile.sources.push_back(source);
          }
        } else {
          addFeeds(tile, event);
        }
        stats.fightFeeds += 1;
        continue;
      }
    }
    plainTiles.push_back(plainEvent(event));
    byCard[cardID].push_back(plainTiles.size() - 1);
  }
  std::vector<SportsEvent> others = loose;
  others.insert(others.end(), plainTiles.begin(), plainTiles.end());


  // Teams named in titles no game matched: two teams of a complete league
  // with no game between them is a replay or an old slot name (hidden);
  // elsewhere their logos go on the tile.
  std::set<size_t> hidden;
  if (!in.rosters.empty()) {
    std::vector<std::pair<const RosterLeague*, TeamIndex>> indexes;
    for (const RosterLeague& roster : in.rosters) {
      indexes.emplace_back(&roster, TeamIndex::fromTeams(roster.teams, roster.college));
    }
    // College: the schedule has every game between the schools it knows, so
    // two of them with no game is a leftover too. Only while that schedule
    // is loaded; basketball only with both men's and women's.
    std::set<std::string> scheduled;
    for (const SportsGame& game : games) scheduled.insert(game.league);
    std::set<std::string> complete = completeLeagues();
    if (scheduled.count("College Football")) complete.insert("College Football");
    std::set<std::string> shorts;
    for (const RosterLeague& roster : in.rosters) shorts.insert(roster.shortName);
    if (scheduled.count("College Basketball") && shorts.count("NCAAM") && shorts.count("NCAAW")) {
      complete.insert("College Basketball");
    }
    for (size_t position = 0; position < others.size(); ++position) {
      const SportsEvent& event = others[position];
      if (event.game || !looksLikeGame(event.title)) continue;
      bool unknown = event.league.empty() || event.league == "Other";
      std::set<std::string> groups = {event.league};
      auto family = sportFamilies().find(event.league);
      if (family != sportFamilies().end()) groups = family->second;
      struct Found {
        const RosterLeague* league;
        SportsTeam home, away;
      };
      std::vector<Found> found;
      for (const auto& item : indexes) {
        // A title of unknown sport only borrows soccer clubs ("Panthers @
        // Sharks" may be rugby, "Utah vs Arizona" anything).
        const std::string& group = item.first->group;
        if (unknown ? !(group == "Soccer" || group == "MLS") : !groups.count(group)) continue;
        if (auto pair = item.second.pair(event.title)) found.push_back({item.first, pair->first, pair->second});
      }
      if (found.empty()) continue;
      std::set<std::string> foundGroups;
      for (const Found& f : found) foundGroups.insert(f.league->group);
      if (foundGroups.size() != 1) continue;
      const Found& hit = found.front();
      if (complete.count(hit.league->group)) {
        hidden.insert(position);
        stats.stale += 1;
        continue;
      }
      SportsEvent copy = unknown ? event.retitled(hit.league->group, hit.league->shortName) : event;
      copy.teams = std::vector<SportsTeam>{hit.home, hit.away};
      others[position] = std::move(copy);
      stats.namePairs += 1;
    }
  }
  if (!hidden.empty()) {
    std::vector<SportsEvent> kept;
    for (size_t i = 0; i < others.size(); ++i) {
      if (!hidden.count(i)) kept.push_back(std::move(others[i]));
    }
    others = std::move(kept);
  }
  if (!in.schools.empty()) {
    TeamIndex schoolIndex = TeamIndex::fromTeams(in.schools);
    // College sports by name; elsewhere (soccer, plain "Football", hockey,
    // "Other") only with a college sign in the title or feed.
    static const std::set<std::string> kCollege = {"College Football", "College Basketball", "Volleyball",
                                                   "Field Hockey",     "Lacrosse",           "Softball",
                                                   "Wrestling",        "Baseball"};
    static const std::set<std::string> kPro = {"NFL", "NBA", "WNBA", "MLB", "NHL", "MLS"};
    static const char* const kClue =
        "\\b(ncaa\\w*|college|university|univ|women's|men's|espn\\+|accn|acc network|secn|sec network|btn\\+?|big ten)";
    for (SportsEvent& event : others) {
      if (kPro.count(event.league) || event.teams) continue;
      std::string family = event.family.value_or("");
      if (!(kCollege.count(event.league) || rx::test(kClue, event.title + " " + family, true) ||
            rx::test("\\bflo", family, true))) {
        continue;
      }
      if (!looksLikeGame(event.title)) continue;
      auto pair = schoolIndex.pair(event.title);
      if (!pair) continue;
      event.teams = std::vector<SportsTeam>{pair->first, pair->second};
      stats.schoolPairs += 1;
    }
  }
  std::unordered_set<std::string> used;
  std::vector<SportsEvent> result;
  result.reserve(gameCount + others.size());
  for (SportsEvent& event : others) {
    if (used.insert(event.id).second) result.push_back(std::move(event));
  }

  // 4. Games (no channel showing them: dimmed). Ids stay stable: last
  // time's id, else the provider event it was first shown as, unless a
  // provider tile above already uses it.
  for (size_t i = 0; i < gameCount; ++i) {
    SportsEvent value = entry(i);
    const SportsGame& game = games[i];
    std::string id;
    auto previous = in.previousIDs.find(game.id);
    if (previous != in.previousIDs.end()) {
      id = previous->second;
    } else if (firstProviderID[i]) {
      id = *firstProviderID[i];
    } else {
      id = value.id;
    }
    if (used.count(id)) id = firstProviderID[i] ? *firstProviderID[i] : value.id;
    if (used.count(id)) id = value.id;
    if (!used.insert(id).second) continue;
    result.push_back(value.withId(id));
  }
  lap("3 assemble");

  // Source order: My Lineup, other channels, provider feeds, team channels;
  // Spanish after English unless the viewer picked US Spanish. Stations
  // beyond the best few (one game can be on 90 stations) are counted, not
  // listed.
  for (SportsEvent& event : result) {
    if (event.sources.size() <= 1) continue;
    std::vector<std::pair<size_t, EventSource>> numbered;
    for (size_t i = 0; i < event.sources.size(); ++i) numbered.emplace_back(i, event.sources[i]);
    auto lang = [&](const EventSource& s) { return (in.preferSpanish ? !s.spanish : s.spanish) ? 1 : 0; };
    std::sort(numbered.begin(), numbered.end(), [&](const auto& a, const auto& b) {
      if (a.second.kind != b.second.kind) return static_cast<int>(a.second.kind) < static_cast<int>(b.second.kind);
      int ra = a.second.rank.value_or(4), rb = b.second.rank.value_or(4);
      if (ra != rb) return ra < rb;
      int sa = lang(a.second), sb = lang(b.second);
      if (sa != sb) return sa < sb;
      return a.first < b.first;
    });
    std::vector<EventSource> kept;
    int stations = 0, folded = 0;
    for (auto& item : numbered) {
      if (item.second.kind == EventSource::Kind::guide) {
        stations += 1;
        if (stations > 3) {
          folded += 1;
          continue;
        }
      }
      kept.push_back(std::move(item.second));
    }
    event.sources = std::move(kept);
    if (folded > 0) {
      event.moreStations = folded;
      stats.trimmedStations += folded;
    }
  }
  std::sort(result.begin(), result.end(), [](const SportsEvent& a, const SportsEvent& b) {
    if (a.start && b.start) {
      if (*a.start != *b.start) return *a.start < *b.start;
      return a.title != b.title ? a.title < b.title : a.id < b.id;
    }
    if (a.start && !b.start) return true;
    if (!a.start && b.start) return false;
    return a.title != b.title ? a.title < b.title : a.id < b.id;
  });
  lap("4 order");
  BuildResult out;
  out.events = std::move(result);
  out.stats = std::move(stats);
  return out;
}

// MARK: Fight cards

std::optional<FightKey> fightKey(std::string_view title) {
  std::string t = normalized(title);
  static const char* const kAround[] = {" press conference ", " weigh in ", " weigh ins ", " embedded ",
                                        " countdown ",        " 24 7 ",     " preview ",   " replay ",
                                        " classic ",          " highlights ", " post fight ", " pre fight ",
                                        " face off ",         " faceoffs ", " media day "};
  for (const char* w : kAround) {
    if (has(t, w)) return std::nullopt;
  }
  std::string org;
  for (const char* o : {"ufc", "pfl", "bkfc"}) {
    if (has(t, std::string(" ") + o + " ")) {
      org = o;
      break;
    }
  }
  if (org.empty()) return std::nullopt;
  std::string number;
  if (auto m = rx::first("(?<=\\b" + org + " )\\d{1,4}\\b", t)) {
    number = std::to_string(text::toInt(m->group(0)).value_or(0));
  }
  std::optional<std::string> part;
  if (has(t, " early prelim") || has(t, " early preliminar")) {
    part = "early";
  } else if (has(t, " prelim") || has(t, " preliminar")) {
    part = "prelims";
  } else if (has(t, " main card ") || has(t, " main event ") || has(t, " cartelera principal ")) {
    part = "main";
  }
  FightKey key;
  key.org = org;
  key.number = number == "0" ? "" : number;
  key.part = part;
  key.fightPass = has(t, " fight pass ");
  return key;
}

std::vector<int> fightTargets(const FightKey& key, std::optional<Seconds> start,
                              const std::map<std::string, std::vector<int>>& cards,
                              const std::vector<SportsGame>& games) {
  auto found = cards.find(key.org + "|" + key.number);
  if (found == cards.end()) return {};
  std::vector<int> list = found->second;
  auto game = [&](int i) -> const SportsGame& { return games[static_cast<size_t>(i)]; };
  std::stable_sort(list.begin(), list.end(), [&](int a, int b) { return game(a).start < game(b).start; });
  auto part = [&](int i) -> std::string {
    if (!game(i).cardKey) return "";
    std::vector<std::string> parts = text::components(*game(i).cardKey, "|");
    return parts.back();
  };
  auto gap = [&](int i) -> Seconds { return start ? std::llabs(game(i).start - *start) : 0; };
  // "UFC|600061267": one card (Fight Nights share an empty number).
  auto card = [&](int i) -> std::string {
    std::vector<std::string> parts = text::components(game(i).id, "|");
    parts.pop_back();
    return str::join(parts, "|");
  };
  auto nearestOf = [&](const std::vector<int>& from) -> std::optional<int> {
    std::optional<int> best;
    for (int i : from) {
      if (!best || gap(i) < gap(*best)) best = i;
    }
    return best;
  };
  // Several cards under one key (Fight Nights): the one nearest in time, or
  // none when there's no time to tell them apart.
  std::set<std::string> distinct;
  for (int i : list) distinct.insert(card(i));
  if (distinct.size() > 1) {
    auto closest = nearestOf(list);
    if (!start || !closest) return {};
    std::string chosen = card(*closest);
    std::vector<int> kept;
    for (int i : list) {
      if (card(i) == chosen) kept.push_back(i);
    }
    list = kept;
  }
  if (list.empty()) return {};
  int first = list.front();
  std::optional<int> nearest = nearestOf(list);
  // A card that isn't split: one game.
  if (std::all_of(list.begin(), list.end(), [&](int i) { return part(i).empty(); })) {
    if (!start || gap(first) <= 6 * 3600) return {first};
    return {};
  }
  if (key.part) {
    std::string wanted = *key.part;
    if (key.fightPass && wanted == "prelims" &&
        std::any_of(list.begin(), list.end(), [&](int i) { return part(i) == "early"; })) {
      wanted = "early";
    }
    std::vector<int> labeled;
    for (int i : list) {
      if (part(i) == wanted) labeled.push_back(i);
    }
    if (!start) return labeled;
    if (!labeled.empty() && gap(labeled.front()) <= 45 * 60) return {labeled.front()};
    if (nearest && gap(*nearest) <= 30 * 60) return {*nearest};
    std::vector<int> out;
    for (int i : labeled) {
      if (gap(i) <= 3 * 3600) out.push_back(i);
    }
    return out;
  }
  if (!start) return {};
  if (*start <= game(first).start + 30 * 60 && *start >= game(first).start - 3 * 3600) return list;
  if (!nearest || gap(*nearest) > 3 * 3600) return {};
  return {*nearest};
}

// MARK: Names

std::string matchupKey(std::string_view title) {
  if (auto key = pairKey(title)) return *key;
  return normalized(title);
}

std::optional<std::string> tennisLevel(std::string_view tour, std::string_view tournament) {
  // Words of the tournaments' own names (ESPN's "Toray Pan Pacific Open
  // Tennis", "Mutua Madrid Open"), by tour and level.
  struct Level {
    const char* tour;
    const char* level;
    std::vector<const char*> words;
  };
  static const std::vector<Level> kLevels = {
      {"wta", "1000",
       {"qatar", "doha", "dubai", "indian wells", "bnp paribas", "miami", "madrid", "internazionali", "rome",
        "national bank", "canadian", "cincinnati", "china open", "wuhan"}},
      {"wta", "500",
       {"brisbane", "adelaide", "abu dhabi", "charleston", "credit one", "stuttgart", "porsche", "berlin", "queen",
        "bad homburg", "eastbourne", "washington", "mubadala", "korea open", "pan pacific", "ningbo", "linz",
        "strasbourg", "monterrey", "san diego", "san antonio"}},
      {"atp", "1000",
       {"indian wells", "bnp paribas", "miami", "monte carlo", "madrid", "internazionali", "rome",
        "national bank", "canadian", "cincinnati", "shanghai", "paris"}},
      {"atp", "500",
       {"rotterdam", "dallas", "rio open", "acapulco", "abierto mexicano", "dubai", "qatar", "doha", "barcelona",
        "munich", "bmw open", "halle", "terra wortmann", "queen", "hamburg", "washington", "mubadala",
        "china open", "japan open", "kinoshita", "basel", "swiss indoors", "vienna", "erste bank"}},
  };
  std::string name = normalized(tournament);
  std::string onTour = str::lower(tour);
  for (const Level& l : kLevels) {
    if (onTour != l.tour) continue;
    for (const char* w : l.words) {
      if (has(name, std::string(" ") + w + " ")) return std::string(l.level);
    }
  }
  return std::nullopt;
}

std::vector<int> tennisTargets(std::string_view titleIn, std::string_view family, std::optional<Seconds> start,
                               Seconds now, const std::vector<int>& matches, const std::vector<SportsGame>& games) {
  if (matches.empty() || looksLikeGame(titleIn)) return {};
  std::string title = normalized(std::string(titleIn) + " " + std::string(family));
  std::optional<std::string> tour;
  if (has(title, " wta ")) tour = "WTA";
  if (has(title, " atp ")) tour = tour ? std::optional<std::string>() : std::optional<std::string>("ATP");
  std::optional<std::string> level;
  if (auto m = rx::first("\\b(?:wta|atp)\\s*(1000|500|250|125)\\b", title)) level = std::string(m->group(1));
  // The day's play: from a little before the feed's start into the night (a
  // tournament abroad plays overnight here); without a start, today's.
  Seconds from = start ? *start - 6 * 3600 : now - 12 * 3600;
  Seconds to = start ? *start + 16 * 3600 : now + 18 * 3600;
  static const std::set<std::string> kStop = {"open", "tennis", "championships", "championship", "cup",
                                              "international", "internationals", "ladies", "tournament",
                                              "presented", "classic", "masters", "trophy", "the", "and"};
  struct Candidate {
    std::string name;
    int words = 0;
    bool levelMatches = false;
    std::vector<int> today;
  };
  std::map<std::string, Candidate> byTournament;
  for (int index : matches) {
    const SportsGame& g = games[static_cast<size_t>(index)];
    if (!g.isTennis() || !g.tournament) continue;
    if (tour && g.leagueShort != *tour) continue;
    if (g.start < from || g.start > to) continue;
    Candidate& c = byTournament[g.leagueShort + "|" + *g.tournament];
    if (c.name.empty()) {
      c.name = *g.tournament;
      for (const std::string& w : splitWords(normalized(*g.tournament))) {
        if (text::count(w) >= 4 && !kStop.count(w) && has(title, " " + w + " ")) c.words += 1;
      }
      if (g.venue) {
        // The city ("Tokyo, Japan" -> tokyo).
        std::string city = normalized(text::components(*g.venue, ",").front());
        if (city.size() > 4 && has(title, city)) c.words += 1;
      }
      c.levelMatches = level && tennisLevel(g.leagueShort, *g.tournament) == level;
    }
    c.today.push_back(index);
  }
  const Candidate* pick = nullptr;
  int bestWords = 0;
  for (const auto& [key, c] : byTournament) {
    if (c.words > bestWords) {
      bestWords = c.words;
      pick = &c;
    }
  }
  if (!pick && level) {
    // "WTA 500": the only one of that level on.
    for (const auto& [key, c] : byTournament) {
      if (!c.levelMatches) continue;
      if (pick) return {};  // two: can't tell
      pick = &c;
    }
  }
  if (!pick && tour && !level && byTournament.size() == 1) pick = &byTournament.begin()->second;
  if (!pick) return {};
  std::vector<int> out = pick->today;
  std::sort(out.begin(), out.end(), [&](int a, int b) {
    return games[static_cast<size_t>(a)].start < games[static_cast<size_t>(b)].start;
  });
  return out;
}

bool looksLikeGame(std::string_view title) {
  std::string full = " " + str::lower(title) + " ";
  if (has(full, "@")) return true;
  for (const char* sep : {" vs ", " vs. ", " v ", " v. ", " at "}) {
    if (has(full, sep)) return true;
  }
  // " - " only counts after a "Network:" style prefix.
  size_t colon = full.find(':');
  if (colon != std::string::npos && text::count(std::string_view(full).substr(0, colon)) < 30) {
    std::string rest = " " + full.substr(colon + 1);
    return has(rest, " - ");
  }
  return false;
}

std::string normalized(std::string_view input) {
  std::string folded = str::replaceAll(str::lower(str::foldDiacritics(input)), "&", " and ");
  std::vector<std::string> tokens = guide::letterNumberTokens(folded);
  return " " + str::join(tokens, " ") + " ";
}

// Names channels use that the schedule spells another way.
const std::map<std::string, std::vector<std::string>>& teamAliases() {
  static const std::map<std::string, std::vector<std::string>> kAliases = {
      {"athletic club", {"athletic bilbao"}},
      {"internazionale", {"inter milano", "inter milan"}},
      {"hamburg sv", {"hamburger sv"}},
      {"d c united", {"d c"}},
      {"newcastle falcons", {"newcastle red bulls"}},
      // Schools by their short names.
      {"massachusetts", {"umass"}},
      {"boston university", {"boston u"}},
      {"connecticut", {"uconn"}},
      {"mississippi", {"ole miss"}},
      {"pittsburgh", {"pitt"}},
      {"southern california", {"usc"}},
      {"central florida", {"ucf"}},
      {"virginia commonwealth", {"vcu"}},
      {"texas christian", {"tcu"}},
      {"brigham young", {"byu"}},
      {"southern methodist", {"smu"}},
  };
  return kAliases;
}

std::vector<std::string> teamVariants(const SportsTeam& team, bool college) {
  std::vector<std::string> names = {team.name};
  names.insert(names.end(), team.alternates.begin(), team.alternates.end());
  for (const std::optional<std::string>& key : {std::optional<std::string>(team.name), team.location}) {
    if (!key) continue;
    std::string k = trimmed(normalized(*key));
    auto alias = teamAliases().find(k);
    if (alias != teamAliases().end()) names.insert(names.end(), alias->second.begin(), alias->second.end());
    // "Wisconsin-Whitewater" is also "UW-Whitewater".
    if (str::startsWith(k, "wisconsin ") && k.size() > 12) names.push_back("uw " + k.substr(10));
  }
  if (college && team.location) names.push_back(*team.location);
  std::vector<std::string> words = splitWords(normalized(team.name));
  if (words.size() >= 2) {
    const std::string& last = words.back();
    if (text::count(last) >= 4 && !genericWords().count(last)) names.push_back(last);
    std::string lastTwo = words[words.size() - 2] + " " + last;
    if (!genericWords().count(words[words.size() - 2])) names.push_back(lastTwo);
  }
  std::vector<std::string> out;
  std::set<std::string> seen;
  for (const std::string& n : names) {
    std::string v = trimmed(normalized(n));
    if (text::count(v) >= 3 && !genericWords().count(v) && seen.insert(v).second) out.push_back(v);
  }
  return out;
}

const std::map<std::string, std::set<std::string>>& sportFamilies() {
  static const std::map<std::string, std::set<std::string>> kFamilies = {
      {"Football", {"NFL", "College Football"}},
      {"Basketball", {"NBA", "WNBA", "College Basketball"}},
      {"Hockey", {"NHL", "Hockey"}},
      {"Baseball", {"MLB", "MiLB", "Baseball"}},
      {"Soccer", {"Soccer", "MLS"}},
      {"MLS", {"Soccer", "MLS"}},
  };
  return kFamilies;
}

bool isStudioShow(std::string_view title) {
  if (looksLikeGame(title)) return false;
  // Not excluded()'s list: "Classic" is also a tournament's name.
  static const char* const kStudio[] = {
      " pregame ",    " pre game ",   " postgame ",   " post game ",  " countdown ",  " preview ",
      " highlights ", " replay ",     " encore ",     " rewind ",     " recap ",      " press conference ",
      " kickoff ",    " kick off ",   " gameday ",    " game day ",   " sportscenter ", " nfl today ",
      " inside the nfl ", " good morning football ", " first take ", " get up ",   " nfl live ",
      " warm up ",    " warmup ",     " weigh in ",   " weigh ins ",  " media day ",  " embedded ",
      " studio ",     " fantasy ",    " the herd ",   " pti ",        " pre race ",   " post race ",
      " build up ",   " buildup ",    " notebook ",   " paddock ",    " race show ",  " pre show ",
      " post show ",  " prerace ",    " postrace ",   " post fight ", " pre fight ",  " fight conference ",
      " qualifying replay ", " radio ",    " podcast ",    " tonight ",    " talk ",      " hour ",
      " insider ",    " insiders ",   " report ",     " daily ",      " roundtable ", " magazine ",
      " weekly ",     " race day live ", " center ",  " show ",       " auction "};
  std::string t = normalized(title);
  for (const char* w : kStudio) {
    if (has(t, w)) return true;
  }
  return false;
}

bool isTeamSportShow(std::string_view title, std::string_view league) {
  // Sports played by two teams: an event in them names both.
  static const std::set<std::string> kTeamSports = {
      "NFL",    "College Football", "Football", "NBA",     "WNBA",     "College Basketball", "Basketball",
      "MLB",    "MiLB",             "Baseball", "NHL",     "Hockey",   "MLS",                "Soccer",
      "Volleyball", "Field Hockey", "Softball", "Lacrosse"};
  if (!kTeamSports.count(std::string(league)) || looksLikeGame(title)) return false;
  std::string t = normalized(title);
  // Named after a real event, not a team: still an event.
  static const char* const kEvent[] = {
      " bowl ",       " final ",      " finals ",     " championship ", " championships ", " cup ",
      " tournament ", " classic ",    " series ",     " playoff ",    " playoffs ",   " all star ",
      " draft ",      " open ",       " invitational ", " showcase ", " derby ",      " regional ",
      " regionals ",  " jamboree ",   " scrimmage ",  " semifinal ",  " semifinals ", " quarterfinal ",
      " combine ",    " world ",      " olympic ",    " olympics ",   " qualifier ",  " super "};
  for (const char* w : kEvent) {
    if (has(t, w)) return false;
  }
  if (rx::test("\\b(?:game|round|week|matchday|leg|match|event|day)\\s+\\d+\\b", t)) return false;
  return true;
}

bool isNetworkAndSport(std::string_view title) {
  static const std::set<std::string> kNetworks = {
      "espn", "espn2", "espnu", "espnews", "espn+", "fox", "fs1", "fs2", "tnt", "tbs", "cbs", "nbc", "abc",
      "bein", "tudn", "univision", "unimas", "telemundo", "universo", "deportes", "sports", "sport", "tv",
      "network", "channel", "en", "espanol", "plus", "sky", "dazn", "tsn", "sportsnet", "bally", "msg", "nesn",
      "yes", "peacock", "paramount", "prime", "video", "the", "hd"};
  static const std::set<std::string> kSports = {
      "baseball", "beisbol", "basketball", "baloncesto", "basquetbol", "football", "futbol", "soccer", "hockey",
      "tennis", "tenis", "golf", "boxing", "boxeo", "mma", "racing", "nfl", "nba", "mlb", "nhl", "mls",
      "college", "ncaa", "volleyball", "voleibol", "rugby", "cricket", "wrestling", "lucha"};
  std::vector<std::string> words = splitWords(normalized(title));
  if (words.size() < 2 || words.size() > 4) return false;
  bool network = false, sport = false;
  for (const std::string& w : words) {
    if (kSports.count(w)) sport = true;
    else if (kNetworks.count(w)) network = true;
    else return false;
  }
  return network && sport;
}

bool namesPastYear(std::string_view title, Seconds now) {
  int year = time::civilFromSeconds(now).year;
  for (const rx::Match& m : rx::all("\\b((?:19|20)\\d{2})\\b(\\s*[/-]\\s*(?:\\d{2}|\\d{4})\\b)?", title)) {
    int y = std::atoi(std::string(m.group(1)).c_str());
    // "2025/26", "2025-26": a season reaching this year is current.
    bool season = !std::string(m.group(2)).empty();
    if (y < year - (season ? 1 : 0)) return true;
  }
  return false;
}

bool namesPastDate(std::string_view title, Seconds now) {
  time::Civil today = time::civilFromSeconds(now);
  const int64_t day0 = time::daysFromCivil(today.year, today.month, today.day);
  auto month = [](std::string_view name) {
    static const char* const kMonths[] = {"jan", "feb", "mar", "apr", "may", "jun",
                                          "jul", "aug", "sep", "oct", "nov", "dec"};
    std::string lower = str::lower(name.substr(0, 3));
    for (int i = 0; i < 12; ++i) {
      if (lower == kMonths[i]) return i + 1;
    }
    return 0;
  };
  // A day and month: gone by unless it's within the last two days or its
  // next time is in the next two months (late December's "3 Jan" is next
  // week).
  auto pastDay = [&](int m, int d) {
    if (m < 1 || m > 12 || d < 1 || d > 31) return false;
    bool any = false;
    for (int year = today.year - 1; year <= today.year + 1; ++year) {
      if (d > time::daysInMonth(year, m)) continue;
      any = true;
      int64_t diff = time::daysFromCivil(year, m, d) - day0;
      if (diff >= -2 && diff <= 60) return false;
    }
    return any;
  };
  const char* kMonth = "(jan|feb|mar|apr|may|jun|jul|aug|sep|oct|nov|dec)[a-z]*\\.?";
  // "9 Feb", "12th January".
  for (const rx::Match& m : rx::all(std::string("\\b(\\d{1,2})(?:st|nd|rd|th)?\\s+") + kMonth + "\\b", title, true)) {
    if (pastDay(month(m.group(2)), std::atoi(std::string(m.group(1)).c_str()))) return true;
  }
  // "Feb 9", "October 3rd" (not "May 2026").
  for (const rx::Match& m : rx::all(std::string("\\b") + kMonth + "\\s+(\\d{1,2})(?:st|nd|rd|th)?\\b(?!\\d)", title, true)) {
    if (pastDay(month(m.group(1)), std::atoi(std::string(m.group(2)).c_str()))) return true;
  }
  // "Sun 6th", "Sat 10": no such day in the days around now.
  for (const rx::Match& m :
       rx::all("\\b(mon|tue|wed|thu|fri|sat|sun)[a-z]*\\.?\\s+(\\d{1,2})(?:st|nd|rd|th)?\\b(?![:./]\\d)", title, true)) {
    static const std::string kDays = "sunmontuewedthufrisat";
    int wanted = static_cast<int>(kDays.find(str::lower(m.group(1).substr(0, 3)))) / 3;
    int d = std::atoi(std::string(m.group(2)).c_str());
    bool found = false;
    for (int offset = -2; offset <= 10 && !found; ++offset) {
      time::Civil c = time::civilFromSeconds(now + offset * 86'400);
      found = c.day == d && time::weekday(c.year, c.month, c.day) == wanted;
    }
    if (!found) return true;
  }
  // A last piece "5.28" or "10.25" (month and day).
  if (auto m = rx::first("(?:^|\\s)(\\d{1,2})\\.(\\d{1,2})\\s*$", title)) {
    if (pastDay(std::atoi(std::string(m->group(1)).c_str()), std::atoi(std::string(m->group(2)).c_str()))) return true;
  }
  return false;
}

bool excluded(std::string_view t) {
  static const char* const kWords[] = {" pregame ",    " pre game ", " postgame ", " post game ",
                                       " countdown ",  " preview ",  " highlights ", " replay ",
                                       " classic ",    " encore ",   " rewind ",   " recap ",
                                       " press conference ", " kickoff show ", " tonight ", " live from "};
  for (const char* w : kWords) {
    if (has(t, w)) return true;
  }
  return false;
}

std::set<std::string> titleWords(std::string_view title) {
  static const std::set<std::string> kNoise = {"vs",   "the",  "and",  "live", "feed", "event", "events",
                                               "hd",   "fhd",  "uhd",  "4k",   "ppv",  "tv",    "channel",
                                               "main", "card", "full", "replay"};
  std::set<std::string> out;
  for (const std::string& w : splitWords(normalized(title))) {
    bool digits = std::all_of(w.begin(), w.end(), [](char c) { return c >= '0' && c <= '9'; });
    if (digits ? w.size() < 2 : w.size() < 3) continue;
    if (!kNoise.count(w)) out.insert(w);
  }
  return out;
}

std::optional<std::string> pairKey(std::string_view title) {
  std::string cleaned = rx::replace(rx::replace(title, "^[A-Za-z+ ]{2,20}:\\s*", ""), "\\([^)]*\\)?", " ");
  std::vector<std::string> parts = text::components(cleaned, " ");
  size_t split = parts.size();
  for (size_t i = 0; i < parts.size(); ++i) {
    std::string w = str::lower(parts[i]);
    if (w == "vs" || w == "vs." || w == "v" || w == "at" || w == "@" || w == "-") {
      split = i;
      break;
    }
  }
  if (split == parts.size() || split == 0 || split >= parts.size() - 1) return std::nullopt;
  std::vector<std::string> left(parts.begin(), parts.begin() + static_cast<std::ptrdiff_t>(split));
  std::vector<std::string> right(parts.begin() + static_cast<std::ptrdiff_t>(split) + 1, parts.end());
  std::string a = trimmed(normalized(str::join(left, " ")));
  std::string b = trimmed(normalized(str::join(right, " ")));
  if (a.empty() || b.empty()) return std::nullopt;
  return a < b ? a + "|" + b : b + "|" + a;
}

std::string shortLeague(std::string_view league) {
  if (league.empty() || league == "Other") return "EVENT";
  const Profile& p = profile(league);
  return p.chip.empty() ? str::upper(league) : p.chip;
}

// MARK: TeamIndex

TeamIndex::TeamIndex(const std::vector<SportsGame>& games) {
  // Clubs known by their first word ("Montpellier" for Montpellier Hérault,
  // "Castres" for Castres Olympique): soccer and rugby, when no other team
  // of the sport has that first word.
  auto firstWord = [](const SportsTeam& team) {
    std::vector<std::string> words = splitWords(normalized(team.name));
    return words.size() >= 2 && text::count(words.front()) >= 5 && !genericWords().count(words.front())
               ? words.front()
               : std::string();
  };
  std::map<std::pair<std::string, std::string>, std::set<std::string>> clubsByWord;
  for (const SportsGame& game : games) {
    if (game.league != "Soccer" && game.league != "Rugby") continue;
    for (const SportsTeam* team : {&game.home, &game.away}) {
      if (std::string w = firstWord(*team); !w.empty()) clubsByWord[{game.league, w}].insert(team->id);
    }
  }
  for (size_t i = 0; i < games.size(); ++i) {
    const SportsGame& game = games[i];
    if (game.league != "Soccer" && game.league != "Rugby") continue;
    int side = 0;
    for (const SportsTeam* team : {&game.home, &game.away}) {
      std::string w = firstWord(*team);
      if (!w.empty() && clubsByWord[{game.league, w}].size() == 1) {
        byFirstWord_[w].push_back(Name{static_cast<int>(i), side, team->id, {w}, std::nullopt, false});
      }
      side += 1;
    }
  }
  for (size_t i = 0; i < games.size(); ++i) {
    const SportsGame& game = games[i];
    // College games in any sport: the school's name ("Massachusetts") counts.
    const League* league = leagueByShort(game.leagueShort);
    bool college = game.league == "College Football" || game.league == "College Basketball" ||
                   (league && league->everyGame);
    add(game.home, static_cast<int>(i), 0, college, true);
    add(game.away, static_cast<int>(i), 1, college, true);
  }
}

TeamIndex TeamIndex::fromTeams(const std::vector<SportsTeam>& list, bool college) {
  TeamIndex index;
  index.teams_ = list;
  for (size_t i = 0; i < list.size(); ++i) index.add(list[i], static_cast<int>(i), 0, college, false);
  return index;
}

void TeamIndex::add(const SportsTeam& team, int slot, int side, bool college, bool abbreviations) {
  std::set<std::string> seen;
  for (const std::string& variant : teamVariants(team, college)) {
    std::vector<std::string> words = text::words(variant);
    if (words.empty() || !seen.insert(variant).second) continue;
    byFirstWord_[words.front()].push_back(Name{slot, side, team.id, words, std::nullopt, false});
  }
  // Pro teams' city ("Atlanta vs. Chicago"), only when the league is known.
  if (!college && team.location) {
    std::string variant = trimmed(normalized(*team.location));
    std::vector<std::string> words = text::words(variant);
    if (!words.empty() && text::count(variant) >= 4 && seen.insert(variant).second) {
      byFirstWord_[words.front()].push_back(Name{slot, side, team.id, words, std::nullopt, true});
    }
  }
  if (abbreviations && team.shortName) {
    std::string shortName = trimmed(*team.shortName);
    size_t n = text::count(shortName);
    if (n >= 2 && n <= 5) {
      std::string word = str::lower(shortName);
      if (seen.insert(word).second) {
        byFirstWord_[word].push_back(Name{slot, side, team.id, {word}, str::upper(shortName), false});
      }
    }
  }
}

std::vector<TeamIndex::Hit> TeamIndex::hits(std::string_view title, const Filter& allowed,
                                            const Filter& allowLoose, std::set<std::string>* written) const {
  std::vector<std::string> words = splitWords(normalized(title));
  if (words.size() < 2) return {};
  // Words as written, for "abbreviations count only in capitals". In an
  // all-capitals title every word looks like an abbreviation, so none
  // counts on its own there.
  if (written) {
    if (text::hasLowercase(title)) {
      for (std::string& w : guide::letterNumberTokens(title)) written->insert(std::move(w));
    }
  }
  std::vector<Hit> found;
  for (size_t position = 0; position < words.size(); ++position) {
    auto names = byFirstWord_.find(words[position]);
    if (names == byFirstWord_.end()) continue;
    for (const Name& name : names->second) {
      if (allowed && !allowed(name.slot)) continue;
      if (name.loose && !(allowLoose && allowLoose(name.slot))) continue;
      size_t end = position + name.words.size();
      if (end > words.size()) continue;
      if (!std::equal(name.words.begin(), name.words.end(), words.begin() + static_cast<std::ptrdiff_t>(position))) {
        continue;
      }
      found.push_back(Hit{&name, static_cast<int>(position), static_cast<int>(end)});
    }
  }
  // The longest name wins its words: "michigan state" keeps "michigan"
  // (another team) from counting.
  std::vector<Hit> kept;
  for (const Hit& hit : found) {
    bool covered = std::any_of(found.begin(), found.end(), [&](const Hit& other) {
      return other.name->team != hit.name->team && other.length() > hit.length() && other.start <= hit.start &&
             other.end >= hit.end;
    });
    if (!covered) kept.push_back(hit);
  }
  return kept;
}

std::vector<int> TeamIndex::matches(std::string_view title, const Filter& allowed, const Filter& allowLoose) const {
  std::set<std::string> written;
  std::vector<Hit> kept = hits(title, allowed, allowLoose, &written);
  if (kept.size() < 2) return {};
  std::map<int, std::vector<Hit>> byGame;
  for (const Hit& hit : kept) byGame[hit.name->slot].push_back(hit);
  std::vector<int> result;
  for (const auto& [game, list] : byGame) {
    bool loose = allowLoose && allowLoose(game);
    auto strong = [&](const Hit& hit) {
      if (!hit.name->abbreviation) return true;
      if (!written.count(*hit.name->abbreviation)) return false;
      return text::count(*hit.name->abbreviation) >= 3 || loose;
    };
    auto full = [](const Hit& hit) { return !hit.name->abbreviation; };
    bool found = false;
    for (const Hit& h : list) {
      if (found || h.name->side != 0) continue;
      for (const Hit& a : list) {
        if (found || a.name->side != 1) continue;
        if (!(h.end <= a.start || a.end <= h.start)) continue;
        // An abbreviation in lower case only counts next to a full name.
        if ((strong(h) && strong(a)) || (full(h) && a.name->abbreviation) || (full(a) && h.name->abbreviation)) {
          found = true;
        }
      }
    }
    if (found) result.push_back(game);
  }
  return result;  // std::map keeps them sorted
}

std::optional<SportsTeam> TeamIndex::single(std::string_view input, std::string_view leagueWord) const {
  std::vector<Hit> kept = hits(input, nullptr, nullptr, nullptr);
  bool hasLeague = has(normalized(input), " " + str::lower(leagueWord) + " ");
  std::vector<Hit> strong;
  for (const Hit& hit : kept) {
    if (hit.name->slot < 0 || static_cast<size_t>(hit.name->slot) >= teams_.size()) continue;
    std::string words = str::join(hit.name->words, " ");
    std::string full = trimmed(normalized(teams_[static_cast<size_t>(hit.name->slot)].name));
    if (hit.name->words.size() >= 2 || words == full || (hasLeague && text::count(words) >= 4)) strong.push_back(hit);
  }
  if (strong.empty()) return std::nullopt;
  std::set<std::string> distinct;
  for (const Hit& hit : strong) distinct.insert(hit.name->team);
  if (distinct.size() != 1) return std::nullopt;
  return teams_[static_cast<size_t>(strong.front().name->slot)];
}

std::optional<std::pair<SportsTeam, SportsTeam>> TeamIndex::pair(std::string_view title) const {
  std::vector<Hit> kept = hits(title, nullptr, nullptr, nullptr);
  std::vector<Hit> strong;
  for (const Hit& hit : kept) {
    if (text::count(str::join(hit.name->words, " ")) >= 4) strong.push_back(hit);
  }
  std::stable_sort(strong.begin(), strong.end(), [](const Hit& a, const Hit& b) { return a.start < b.start; });
  // Exactly two schools, on separate words; more is ambiguous.
  std::set<std::string> distinct;
  for (const Hit& hit : strong) distinct.insert(hit.name->team);
  if (distinct.size() != 2) return std::nullopt;
  const Hit& first = strong.front();
  auto second = std::find_if(strong.begin(), strong.end(), [&](const Hit& h) {
    return h.name->team != first.name->team && h.start >= first.end;
  });
  if (second == strong.end()) return std::nullopt;
  size_t a = static_cast<size_t>(first.name->slot), b = static_cast<size_t>(second->name->slot);
  if (a >= teams_.size() || b >= teams_.size()) return std::nullopt;
  return std::make_pair(teams_[a], teams_[b]);
}

}  // namespace marigold::sports::matcher
