#include "core/sports/matcher.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <unordered_set>

#include "core/catalog/catalog.hpp"
#include "core/guide/names.hpp"
#include "core/sports/board.hpp"
#include "core/sports/languages.hpp"
#include "core/sports/network_map.hpp"
#include "core/sports/profiles.hpp"
#include "core/sports/text.hpp"
#include "core/sports/titles.hpp"

namespace marigold::sports::matcher {

namespace {

constexpr const char* kProviderDetail = "Provider event channel";
constexpr const char* kCoverageDetail = "Tournament coverage";

bool has(std::string_view hay, std::string_view needle) { return hay.find(needle) != std::string_view::npos; }

std::vector<std::string> splitWords(const std::string& normalizedText) { return text::words(normalizedText); }

std::string trimmed(const std::string& s) { return std::string(str::trimUnicode(s)); }

EventSource providerSource(const std::string& feed, const std::string& lang) {
  EventSource s;
  s.channelID = feed;
  s.kind = EventSource::Kind::provider;
  s.lang = lang;
  s.detail = kProviderDetail;
  return s;
}

bool containsSource(const std::vector<EventSource>& list, const std::string& id) {
  return std::any_of(list.begin(), list.end(), [&](const EventSource& s) { return s.channelID == id; });
}

void addFeeds(SportsEvent& value, const ProviderEvent& event) {
  for (const std::string& feed : event.feeds) {
    if (!containsSource(value.sources, feed)) value.sources.push_back(providerSource(feed, event.langOf(feed)));
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
  // "live: A vs B _ Cricket" shows as "A vs B".
  e.title = titles::tidy(event.title, event.league);
  e.league = event.league;
  e.leagueShort = shortLeague(event.league);
  e.start = event.start;
  for (const std::string& feed : event.feeds) e.sources.push_back(providerSource(feed, event.langOf(feed)));
  e.family = event.family;
  e.boards = event.boards;
  e.highSchool = event.highSchool;
  return e;
}

// A feed's time inside a cricket match's span, from three hours before it.
// One day: until its end. Several days (a Test; `end` is the last day's
// start): at about one of its days' start, since each day's play starts at
// the same time and runs into the evening; not overnight between two days,
// nor days after its last.
bool onAnyDay(const SportsGame& game, Seconds at) {
  if (!game.end || *game.end <= game.start || at < game.start - 3 * 3600) return false;
  if (*game.end - game.start < 20 * 3600) return at <= *game.end;
  if (at > *game.end + 8 * 3600) return false;
  // From three hours before that day's start.
  Seconds into = (at - game.start + 3 * 3600) % 86'400 - 3 * 3600;
  return into <= 8 * 3600;
}

bool closeEnough(std::optional<Seconds> a, std::optional<Seconds> b) {
  if (!a || !b) return true;
  return std::llabs(*a - *b) <= 90 * 60;
}

// How far a feed's time is from a game's start; none for a game listed by
// its day only (start: that day's midnight) when the feed is that day.
Seconds timeApart(const SportsGame& game, Seconds at) {
  if (game.isDayOnly() && at >= game.start && at < game.start + 86'400) return 0;
  return std::llabs(at - game.start);
}

// A placeholder time on a dated feed: before 7 in the morning US Eastern,
// when no college game is played ("(2026-11-06 05:56:00)" for a game whose
// time isn't set). Its day still counts.
const time::Zone& eastern() {
  static const time::Zone kEastern = time::Zone::usEastern();
  return kEastern;
}

bool placeholderHour(Seconds at) { return eastern().toLocal(at).hour < 7; }

bool sameEasternDay(Seconds a, Seconds b) {
  time::Civil x = eastern().toLocal(a), y = eastern().toLocal(b);
  return x.year == y.year && x.month == y.month && x.day == y.day;
}

// The side of a sport a title says: 'w' for "Women's" ("_ Women`s",
// "Womens", "(Women)"), 'm' for "Men's", 0 for neither or both.
char sideSaid(std::string_view title) {
  // Cheap test first: most titles say neither.
  if (!str::icontains(title, "men")) return 0;
  std::string norm = normalized(title);
  bool women = has(norm, " women s ") || has(norm, " womens ") || has(norm, " women ");
  bool men = has(norm, " men s ") || has(norm, " mens ") || has(norm, " men ");
  return women == men ? 0 : women ? 'w' : 'm';
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
  v.set("spanish", l.lang == "es");
  if (!l.lang.empty()) v.set("lang", l.lang);
  v.set("title", l.title);
  v.set("start", static_cast<int64_t>(l.start));
  v.set("end", static_cast<int64_t>(l.end));
  if (l.detail) v.set("detail", *l.detail);
  if (l.venue) v.set("venue", *l.venue);
  if (l.city) v.set("city", *l.city);
  if (l.rank) v.set("rank", *l.rank);
  if (l.tennisCoverage) v.set("tennisCoverage", true);
  if (l.cricket) v.set("cricket", true);
  if (l.abroad) v.set("abroad", true);
  return v;
}

std::optional<GuideListing> listingFromJson(const json::Value& v) {
  if (!v["entryID"].isString() || !v["title"].isString() || !v["start"].isNumber() || !v["end"].isNumber()) {
    return std::nullopt;
  }
  GuideListing l;
  l.entryID = v["entryID"].asString();
  l.inLineup = v["inLineup"].asBool();
  // Saved before Build 132: "spanish" only.
  l.lang = v["lang"].isString() ? v["lang"].asString() : v["spanish"].asBool() ? std::string("es") : std::string();
  l.title = v["title"].asString();
  l.start = v["start"].asInt();
  l.end = v["end"].asInt();
  if (v["detail"].isString()) l.detail = v["detail"].asString();
  if (v["venue"].isString()) l.venue = v["venue"].asString();
  if (v["city"].isString()) l.city = v["city"].asString();
  if (v["rank"].isNumber()) l.rank = static_cast<int>(v["rank"].asInt());
  l.tennisCoverage = v["tennisCoverage"].asBool();
  l.cricket = v["cricket"].asBool();
  l.abroad = v["abroad"].asBool();
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

bool isLiveCricket(std::string_view title) {
  std::string t = normalized(title);
  // "Live: ..." first, or "LIVE" in capitals anywhere.
  if (!str::startsWith(t, " live ") && !rx::test("\\bLIVE\\b", title)) return false;
  if (!has(t, " cricket ") && !has(t, " t20 ") && !has(t, " t20i ") && !has(t, " odi ") && !has(t, " test match ")) {
    return false;
  }
  // Shows around the cricket aren't a match.
  return !excluded(t) && !isStudioShow(title) && !has(t, " magazine ");
}

namespace {

// A cricket format named in normalized text: "t20", "odi", "test"; empty
// when none is.
std::string cricketFormat(const std::string& norm) {
  if (has(norm, " t20i ") || has(norm, " t20 ") || has(norm, " t20is ") || has(norm, " twenty20 ")) return "t20";
  if (has(norm, " odi ") || has(norm, " odis ") || has(norm, " one day ")) return "odi";
  if (has(norm, " test ") || has(norm, " tests ")) return "test";
  return "";
}

// "3rd T20I" -> "3 t20": a numbered match of a series; nullopt when none is
// named.
std::optional<std::string> cricketNumber(const std::string& norm) {
  auto m = rx::first("\\b(\\d{1,2})(?:st|nd|rd|th) (t20i?|odi|test|twenty20)\\b", norm);
  if (!m) return std::nullopt;
  return std::to_string(text::toInt(std::string(m->group(1))).value_or(0)) + " " +
         cricketFormat(" " + std::string(m->group(2)) + " ");
}

// A match's format; a match over several days of unknown format is "multi"
// (a Test or a first-class match: never a T20 or an ODI).
std::string cricketFormatOf(const SportsGame& game) {
  std::string format = cricketFormat(normalized(game.matchFormat.value_or("") + " " + game.note.value_or("")));
  if (format.empty() && game.end && *game.end - game.start >= 20 * 3600) format = "multi";
  return format;
}

bool formatsAgree(const std::string& said, const std::string& game) {
  if (said.empty() || game.empty()) return true;
  if (game == "multi") return said == "test";
  return said == game;
}

// A cricket match being played (or about to start) at some point from
// `from` to `to`: a one-day match for its usual length; one over several
// days on each day's play.
bool cricketInPlay(const SportsGame& game, Seconds from, Seconds to) {
  if (game.end && *game.end - game.start >= 20 * 3600) {
    for (Seconds day = game.start; day <= *game.end + 3600; day += 86'400) {
      if (from < day + 8 * 3600 && to > day - 30 * 60) return true;
    }
    return false;
  }
  Seconds end = game.start + lengthOf(game.league, &game);
  if (game.end) end = std::max(end, *game.end);
  return from < end && to > game.start - 30 * 60;
}

}  // namespace

std::optional<int> cricketTarget(const GuideListing& listing, const std::vector<SportsGame>& games) {
  std::string said = normalized(listing.title + " " + listing.detail.value_or(""));
  std::string format = cricketFormat(said);
  std::optional<std::string> number = cricketNumber(said);
  std::vector<int> playing, named;
  for (size_t i = 0; i < games.size(); ++i) {
    const SportsGame& game = games[i];
    if (!game.isCricket() || game.completed || game.postponed) continue;
    if (!cricketInPlay(game, listing.start, listing.end) || !formatsAgree(format, cricketFormatOf(game))) continue;
    std::optional<std::string> its = game.note ? cricketNumber(normalized(*game.note)) : std::nullopt;
    if (number && its && *its != *number) continue;
    playing.push_back(static_cast<int>(i));
    if (number && its) named.push_back(static_cast<int>(i));
  }
  if (playing.size() == 1) return playing.front();
  if (named.size() == 1) return named.front();
  return std::nullopt;
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
                    n(describedListings) + " by description, " + n(cricketListings) + " on cricket channels), " +
                    n(trimmedStations) +
                    " extra stations folded, school pairs " + n(schoolPairs) + ", name pairs " +
                    n(namePairs) + ", fight feeds " + n(fightFeeds) + ", shows " + n(shows) +
                    " and past cards " + n(pastCards) + " hidden, day-guessed slots seen before " +
                    n(seenBefore) + " hidden and " + n(guessedAnySport) + " matched in another sport, " +
                    n(byTeams) + " joined to a game by their teams";
  if (!offsets.empty()) out += ", offsets " + str::join(offsets, "; ");
  out += "; ms: " + str::join(steps, ", ");
  if (aborted) out += "; ABORTED: " + *aborted;
  return out;
}

// MARK: Slots seen

uint64_t slotKey(std::string_view title) { return str::fnv1a64(trimmed(normalized(title))); }

SlotsSeen noteSlotsSeen(const SlotsSeen& before, const std::vector<std::string>& guessedTitles, Seconds now,
                        const time::Zone& zone) {
  const Seconds today = text::startOfDay(now, zone);
  SlotsSeen out;
  out.reserve(before.size() + guessedTitles.size());
  for (const auto& [key, seen] : before) {
    if (seen.last > now - kSlotsSeenFor) out.emplace(key, seen);
  }
  for (const std::string& title : guessedTitles) {
    auto [it, added] = out.try_emplace(slotKey(title), SlotSeen{today, today});
    if (!added) it->second.last = std::max(it->second.last, today);
  }
  if (out.size() > kSlotsSeenMost) {
    // The least recently seen go first (then the newest first seen, then by key).
    std::vector<std::pair<uint64_t, SlotSeen>> list(out.begin(), out.end());
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
      if (a.second.last != b.second.last) return a.second.last > b.second.last;
      if (a.second.first != b.second.first) return a.second.first < b.second.first;
      return a.first < b.first;
    });
    list.resize(kSlotsSeenMost);
    out = SlotsSeen(list.begin(), list.end());
  }
  return out;
}

json::Value toJson(const SlotsSeen& seen) {
  // An object's keys are written sorted: the same slots, the same text.
  json::Value v = json::Value::object();
  for (const auto& [key, s] : seen) {
    json::Value days = json::Value::array();
    days.push(static_cast<int64_t>(s.first));
    days.push(static_cast<int64_t>(s.last));
    v.set(str::hex64(key), days);
  }
  return v;
}

SlotsSeen slotsSeenFromJson(const json::Value& v) {
  SlotsSeen out;
  if (!v.isObject()) return out;
  for (const auto& [key, days] : v.fields()) {
    if (key.empty() || key.size() > 16 || !days.isArray() || days.items().size() != 2) continue;
    if (!std::all_of(key.begin(), key.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); }))
      continue;
    const json::Value& first = days.items()[0];
    const json::Value& last = days.items()[1];
    if (!first.isNumber() || !last.isNumber()) continue;
    out[std::strtoull(key.c_str(), nullptr, 16)] = SlotSeen{first.asInt(), last.asInt()};
    if (out.size() >= kSlotsSeenMost) break;
  }
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
  auto hide = [&](const ProviderEvent& event, const char* why) {
    for (const std::string& feed : event.feeds) stats.hidden.emplace(feed, why);
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
  const size_t gameCount = games.size();
  // Worked out once per league: each game's side of its sport ('w' women's,
  // 'm' men's, 0 neither, from its league's name: a feed saying "Men's"
  // never goes to a women's game), whether it's a college game, and whether
  // it's from the small colleges' scoreboard pages (a game the other
  // scoreboards also have goes to theirs on a tie).
  std::vector<char> gameSide(gameCount, 0), collegeGame(gameCount, 0), pageGame(gameCount, 0);
  {
    struct Kind {
      char side = 0;
      bool college = false, page = false;
    };
    std::unordered_map<std::string, Kind> byLeague;
    for (size_t i = 0; i < gameCount; ++i) {
      const SportsGame& game = games[i];
      auto it = byLeague.find(game.leagueShort);
      if (it == byLeague.end()) {
        Kind kind;
        if (const League* league = leagueByShort(game.leagueShort)) {
          if (str::startsWith(league->name, "Women's")) kind.side = 'w';
          if (str::startsWith(league->name, "Men's")) kind.side = 'm';
          kind.college = league->everyGame;
          kind.page = league->ncaaPage;
        }
        it = byLeague.emplace(game.leagueShort, kind).first;
      }
      gameSide[i] = it->second.side;
      collegeGame[i] = it->second.college || game.league == "College Football" || game.league == "College Basketball";
      pageGame[i] = it->second.page;
    }
  }
  lap("0 index");
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
          EventSource source = providerSource(feed, event.langOf(feed));
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
          if (!containsSource(value.sources, feed)) value.sources.push_back(providerSource(feed, event.langOf(feed)));
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
    // The side the title says ("_ Women`s"): never a game of the other.
    const char side = sideSaid(event.title);
    auto sideFits = [&](int g) {
      return !side || !gameSide[static_cast<size_t>(g)] || gameSide[static_cast<size_t>(g)] == side;
    };
    // Schools play several sports at once (volleyball and field hockey at
    // noon): a school game of another sport than the feed says isn't it,
    // even with both schools named at that time.
    auto sportFits = [&](int g) {
      const size_t i = static_cast<size_t>(g);
      return sideFits(g) && (!collegeGame[i] || compatible(event.league, games[i]));
    };
    std::vector<int> candidates = index.matches(
        event.title,
        [&](int g) { return compatible(event.league, games[static_cast<size_t>(g)]) && sideFits(g); },
        [&](int g) { return sameLeague(event.league, games[static_cast<size_t>(g)]); });
    if (candidates.empty() && event.start && !event.guessed) {
      // One game of any league at that time with both teams named, even by
      // its own abbreviations ("NJ vs. DAL" from a channel listing several
      // events under one sport's name): the time settles it.
      auto atTime = [&](int g) {
        return std::llabs(games[static_cast<size_t>(g)].start - *event.start) <= 45 * 60 && sportFits(g);
      };
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
          if (timeApart(games[static_cast<size_t>(g)], *event.start) > 90 * 60) continue;
          near.push_back(g);
          nearLeagues.insert(games[static_cast<size_t>(g)].league);
        }
        if (nearLeagues.size() == 1) {
          candidates = std::move(near);
          ambiguous = false;
        }
      }
    }
    if (candidates.empty() && event.guessed && event.start) {
      // Build 125: a day-guessed slot naming a scheduled game's two teams
      // under another sport's label ("MNF 8:15pm Bears at Seahawks" in a
      // soccer group, posted the day before): that game when it's the only
      // one to come at about that clock time. It then takes the game's day
      // and time.
      const Seconds slotAt = *event.start;
      std::vector<int> found = scheduledFor(index, event.title, [&](int g) {
        const SportsGame& game = games[static_cast<size_t>(g)];
        Seconds onClock = std::llabs(game.start - slotAt) % 86'400;
        return !game.completed && game.start > now - 6 * 3600 && game.start < now + 7 * 86'400 &&
               std::min(onClock, 86'400 - onClock) <= 75 * 60 && sportFits(g);
      });
      if (found.size() == 1) {
        stats.guessedAnySport += 1;
        candidates = std::move(found);
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
    // Tennis times are a court's order of play: no clock to learn from; nor
    // a college game with no time set, or one its feed gives a placeholder
    // time.
    const bool placeholder = placeholderHour(*event.start);
    for (int g : item.second) {
      const SportsGame& game = games[static_cast<size_t>(g)];
      if (game.isTennis() || game.isDayOnly() || (placeholder && collegeGame[static_cast<size_t>(g)])) continue;
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
  // Build 125: a day-guessed slot no game took (no schedule has it) that
  // was there on an earlier day is a leftover too (BuildInput::seen). Not
  // high-school games or pro wrestling: no schedule has them, and they show
  // only when picked in My Sports.
  auto seenBefore = [&](const ProviderEvent& event) {
    if (!event.guessed || !in.seen || event.highSchool || event.league == catalog::kProWrestling) return false;
    auto it = in.seen->find(slotKey(event.title));
    return it != in.seen->end() && it->second.first < startOfToday;
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
        Seconds gap = timeApart(game, *event.start);
        if (onAnyDay(game, *event.start)) gap = 0;
        if (!best || gap < best->second ||
            (gap == best->second && pageGame[static_cast<size_t>(best->first)] && !pageGame[static_cast<size_t>(g)])) {
          best = std::make_pair(g, gap);
        }
      }
      // Tennis: a later match on a court can start hours after its listed time.
      const SportsGame* bestGame = best ? &games[static_cast<size_t>(best->first)] : nullptr;
      if (best && best->second <= profile(bestGame->league, bestGame->leagueShort).matchWindow) {
        chosen = best->first;
      } else if (!event.league.empty() && event.league != "Other" && placeholderHour(*event.start)) {
        // A college feed of a known sport dated with a placeholder time
        // ("(2026-11-06 05:56:00)"): the one game of its two schools later
        // that day.
        std::optional<int> only;
        int count = 0;
        for (int g : candidates) {
          const SportsGame& game = games[static_cast<size_t>(g)];
          if (!collegeGame[static_cast<size_t>(g)] || (game.start < *event.start && !game.isDayOnly()) ||
              !sameEasternDay(game.start, *event.start)) {
            continue;
          }
          only = g;
          count += 1;
        }
        if (count == 1) chosen = only;
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
      hide(event, "a replay: its game is on another day");
      continue;
    }
    size_t g = static_cast<size_t>(*chosen);
    stats.matchedEvents += 1;
    SportsEvent value = entry(g);
    addFeeds(value, event);
    if (!value.family) value.family = event.family;
    // A game listed by its day only: the time from its first feed with one.
    if (games[g].isDayOnly() && value.start == games[g].start && event.start && !event.guessed &&
        !placeholderHour(*event.start)) {
      value.start = event.start;
    }
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
  // A tennis channel's coverage of named tournaments, once the listings
  // naming players have had their say.
  std::vector<const GuideListing*> coverage;
  // A cricket channel's live match ("Live: T20 Cricket") goes by the match
  // in play, which may have started days ago (a Test).
  const bool anyCricket = std::any_of(games.begin(), games.end(), [](const SportsGame& g) {
    return g.isCricket() && !g.completed;
  });
  if (anyRelevant || anyCricket) {
    auto started = std::chrono::steady_clock::now();
    // Build 132: a title on several channels (or on one again later) is
    // read once: whether it's left out, and the games it names.
    struct TitleRead {
      bool excluded = false;
      std::optional<std::vector<int>> games;
    };
    std::unordered_map<std::string, TitleRead> titleReads;
    for (const GuideListing& listing : in.listings) {
      if (elapsedMs(started) > kListingsBudgetMs) {
        stats.aborted = "listings over 2 s";
        break;
      }
      auto [titleRead, fresh] = titleReads.try_emplace(listing.title);
      if (fresh) titleRead->second.excluded = excluded(normalized(listing.title));
      if (titleRead->second.excluded) continue;
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
      std::vector<int> candidates;
      std::string how = "title";
      if (listing.cricket) {
        // The match in play then (or the one its number names), else the
        // one its description names ("Harbor v Valley, 3rd T20I").
        how = "cricket";
        if (auto target = cricketTarget(listing, games)) {
          candidates = {*target};
        } else if (listing.detail) {
          std::vector<int> found = index.matches(*listing.detail, [&](int g) {
            const SportsGame& game = games[static_cast<size_t>(g)];
            return game.isCricket() && !game.completed && cricketInPlay(game, listing.start, listing.end);
          });
          if (found.size() == 1) candidates = found;
        }
      } else if (anyRelevant) {
        if (!titleRead->second.games) titleRead->second.games = index.matches(listing.title, isRelevant);
        candidates = filtered(*titleRead->second.games, airing);
      }
      if (candidates.empty() && listing.tennisCoverage) {
        // Another country's channel is a guide source only (never "likely").
        if (!tennisMatches.empty() && !listing.abroad) coverage.push_back(&listing);
        continue;
      }
      if (candidates.empty() && !listing.cricket && (listing.venue || listing.detail)) {
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
        } else if (how == "cricket") {
          stats.cricketListings += 1;
        }
      }
      for (int gi : candidates) {
        size_t g = static_cast<size_t>(gi);
        stats.matchedListings += 1;
        SportsEvent value = entry(g);
        if (!value.listingEnd || listing.end > *value.listingEnd) value.listingEnd = listing.end;
        if (!containsSource(value.sources, listing.entryID)) {
          int rank = listing.abroad ? station_rank::abroad
                                    : listing.rank.value_or(listing.inLineup ? station_rank::lineup
                                                                             : station_rank::elsewhere);
          EventSource source;
          source.channelID = listing.entryID;
          source.kind = listing.inLineup && !listing.abroad ? EventSource::Kind::lineup : EventSource::Kind::guide;
          source.lang = listing.lang;
          source.detail = stationDetail(rank);
          source.rank = rank;
          value.sources.push_back(std::move(source));
        }
        byGame[g] = std::move(value);
      }
    }
    // Each tournament it names ("Vienna-ATP, Basel-ATP & Tokyo-WTA
    // Quarterfinals"): that tournament's matches on during the listing, or
    // starting in it. A likely way to watch them, tried after their own
    // feeds; a listing naming none ("Center Court Live") gives nothing.
    for (const GuideListing* listing : coverage) {
      std::string parts = rx::replace(listing->title, "\\s*(?:,|&|\\band\\b)\\s*", "|", true);
      std::set<int> targets;
      for (const std::string& part : text::components(parts, "|")) {
        if (trimmed(part).empty()) continue;
        for (int g : tennisTargets(part, "", listing->start, now, tennisMatches, games, true)) targets.insert(g);
      }
      bool onNow = listing->start <= now && now < listing->end;
      for (int gi : targets) {
        if (!isRelevant(gi)) continue;
        size_t g = static_cast<size_t>(gi);
        const SportsGame& game = games[g];
        bool startsDuring = game.start >= listing->start && game.start < listing->end;
        bool liveDuring = onNow && game.start < listing->start;
        if (!startsDuring && !liveDuring) continue;
        SportsEvent value = entry(g);
        if (containsSource(value.sources, listing->entryID)) continue;
        stats.matchedListings += 1;
        EventSource source;
        source.channelID = listing->entryID;
        source.kind = EventSource::Kind::team;
        source.lang = listing->lang;
        source.detail = kCoverageDetail;
        source.rank = listing->rank.value_or(listing->inLineup ? station_rank::lineup : station_rank::elsewhere);
        value.sources.push_back(std::move(source));
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
  // The schedule's F1 weekends by place ("mexico"), with the race's start: a
  // feed naming a Grand Prix whose race has run, or one that isn't this
  // weekend's or the next ("Storbritanniens Grand Prix ... 2026" in
  // November), shows it again.
  std::map<std::string, std::optional<Seconds>> f1Weekends;
  for (const SportsGame& game : games) {
    if (game.leagueShort != "F1" || !game.isRacing()) continue;
    auto place = titles::racePlace(game.home.name);
    if (!place) continue;
    std::optional<Seconds>& race = f1Weekends[*place];
    if (game.cardKey && str::endsWith(*game.cardKey, "|race")) race = game.start;
  }
  auto raceRan = [&](const ProviderEvent& event) {
    if (f1Weekends.empty()) return false;
    std::string raw = event.title + " " + event.family;
    if (!str::icontains(raw, "f1") && !str::icontains(raw, "formula") && !str::icontains(raw, "formel")) return false;
    auto place = titles::grandPrixPlace(event.title);
    if (!place) return false;
    const Seconds when = event.start && !event.guessed ? *event.start : now;
    auto weekend = f1Weekends.find(*place);
    if (weekend == f1Weekends.end()) return when < now + 2 * 86'400;
    return weekend->second && when > *weekend->second + 3 * 3600;
  };
  auto notAnEvent = [&](const ProviderEvent& event) {
    // "LIVE | CARROLLTON | Fri 09:30": a school's name alone, no sport or
    // opponent (a high-school feed with nothing to show for it).
    if (event.highSchool && !looksLikeGame(event.title) && isTeamSportShow(event.title, "Football") &&
        str::split(str::trim(event.title), ' ').size() <= 3) {
      hide(event, "a school's name alone: no sport or opponent");
      return true;
    }
    // "2024 Round 5: Detroit", "Harbor vs Valley 9 Feb": an old race or game
    // shown again.
    if (namesPastYear(event.title, now) || namesPastDate(event.title, now)) {
      stats.replays += 1;
      hide(event, "a replay: names a past year or date");
      return true;
    }
    if (raceRan(event)) {
      stats.replays += 1;
      hide(event, "a replay: the race already ran");
      return true;
    }
    bool old = excluded(normalized(event.title));
    if (profile(event.league).gamesOnly && !board::whipNamed(event.title)) {
      if (old || !looksLikeGame(event.title)) {
        stats.shows += 1;
        hide(event, old ? "a replay, classic or highlights" : "a show: a team sport with no matchup");
        return true;
      }
    }
    if (event.league == "Fighting") {
      if (old) {
        stats.shows += 1;
        hide(event, "a replay, classic or highlights");
        return true;
      }
      auto key = fightKey(event.title);
      if (key && scheduledOrgs.count(key->org)) {
        stats.pastCards += 1;
        hide(event, "a fight card that's over");
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
      hide(event, "a leftover: yesterday's game or an old slot name");
      continue;
    }
    if (seenBefore(event)) {
      stats.seenBefore += 1;
      hide(event, kLeftoverNotScheduled);
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
      hide(original, "a leftover: yesterday's game or an old slot name");
      continue;
    }
    if (seenBefore(original)) {
      stats.seenBefore += 1;
      hide(original, kLeftoverNotScheduled);
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
                                      [](const EventSource& s) { return s.spanish(); });
        if (event.lang != "es" && allSpanish) {
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
    // Build 125: scheduled games by their two teams (either order), so a
    // feed whose title the rosters read as two teams joins their game when
    // it's at about its time: one tile per game, its feeds the sources.
    std::unordered_map<std::string, std::vector<int>> byTeams;
    auto teamsKey = [](const std::string& a, const std::string& b) { return a < b ? a + "|" + b : b + "|" + a; };
    for (size_t i = 0; i < gameCount; ++i) {
      const SportsGame& game = games[i];
      if (game.home.id.empty() || game.away.id.empty() || game.home.id == game.away.id) continue;
      byTeams[teamsKey(game.home.id, game.away.id)].push_back(static_cast<int>(i));
    }
    std::unordered_set<std::string> guessedIDs;
    for (const ProviderEvent& event : in.events)
      if (event.guessed) guessedIDs.insert(event.id);
    auto gameBetween = [&](const SportsEvent& event, const SportsTeam& a, const SportsTeam& b,
                           const std::string& group) -> std::optional<int> {
      auto list = byTeams.find(teamsKey(a.id, b.id));
      if (list == byTeams.end() || !event.start) return std::nullopt;
      const bool guessed = guessedIDs.count(event.id) > 0;
      const char side = sideSaid(event.title);
      std::optional<std::pair<int, Seconds>> best;
      for (int g : list->second) {
        const SportsGame& game = games[static_cast<size_t>(g)];
        if (!compatible(group, game)) continue;
        if (side && gameSide[static_cast<size_t>(g)] && gameSide[static_cast<size_t>(g)] != side) continue;
        Seconds gap = 0;
        if (guessed) {
          // Only the clock was given: the matchup's game still to come at that time.
          Seconds apart = std::llabs(game.start - *event.start) % 86'400;
          if (game.completed || game.start <= now - 6 * 3600 || game.start >= now + 7 * 86'400 ||
              std::min(apart, 86'400 - apart) > 75 * 60)
            continue;
          gap = game.start - now;
        } else {
          gap = onAnyDay(game, *event.start) ? 0 : timeApart(game, *event.start);
          if (gap > profile(game.league, game.leagueShort).matchWindow) continue;
        }
        if (!best || gap < best->second) best = std::make_pair(g, gap);
      }
      if (!best) return std::nullopt;
      return best->first;
    };
    // Tiles given two teams so far, by those teams (positions in `others`).
    std::unordered_map<std::string, std::vector<size_t>> sameTeams;
    auto nearTime = [](std::optional<Seconds> a, std::optional<Seconds> b) {
      return a && b ? std::llabs(*a - *b) <= 90 * 60 : !a && !b;
    };
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
      if (auto at = gameBetween(event, hit.home, hit.away, hit.league->group)) {
        size_t g = static_cast<size_t>(*at);
        SportsEvent value = entry(g);
        for (const EventSource& source : event.sources)
          if (!containsSource(value.sources, source.channelID)) value.sources.push_back(source);
        if (!value.family) value.family = event.family;
        byGame[g] = std::move(value);
        if (!firstProviderID[g]) firstProviderID[g] = event.id;
        hidden.insert(position);
        stats.matchedEvents += 1;
        stats.byTeams += 1;
        continue;
      }
      if (complete.count(hit.league->group)) {
        hidden.insert(position);
        stats.stale += 1;
        for (const EventSource& source : event.sources)
          if (source.kind == EventSource::Kind::provider)
            stats.hidden.emplace(source.channelID, "two teams with no game between them: a replay or old slot name");
        continue;
      }
      // Build 125: another feed's tile for the same two teams at about the
      // same time (titles written another way): one tile with both.
      std::string pairKeyOfTeams = teamsKey(hit.home.id, hit.away.id);
      auto same = sameTeams.find(pairKeyOfTeams);
      if (same != sameTeams.end()) {
        auto at = std::find_if(same->second.begin(), same->second.end(),
                               [&](size_t i) { return nearTime(others[i].start, event.start); });
        if (at != same->second.end()) {
          SportsEvent& kept = others[*at];
          for (const EventSource& source : event.sources)
            if (!containsSource(kept.sources, source.channelID)) kept.sources.push_back(source);
          hidden.insert(position);
          stats.mergedPlain += 1;
          continue;
        }
      }
      SportsEvent copy = unknown ? event.retitled(hit.league->group, hit.league->shortName) : event;
      copy.teams = std::vector<SportsTeam>{hit.home, hit.away};
      others[position] = std::move(copy);
      sameTeams[pairKeyOfTeams].push_back(position);
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

  // Source order (Build 132): My Lineup first, whatever its language (you
  // chose it); then by language: yours, not known, another; then other
  // channels, provider feeds, team channels; then station rank and the
  // order found. Stations beyond the best few of a language (one game can
  // be on 90 stations) are counted, not listed: yours and unknown ones
  // together, each other language apart, so another language's stations
  // never push yours out. Other countries' sports channels past two go.
  for (SportsEvent& event : result) {
    if (event.sources.size() <= 1) continue;
    struct Keyed {
      size_t index;
      int lineup, fit;
      EventSource source;
    };
    std::vector<Keyed> keyed;
    keyed.reserve(event.sources.size());
    for (size_t i = 0; i < event.sources.size(); ++i) {
      EventSource& s = event.sources[i];
      keyed.push_back({i, s.kind == EventSource::Kind::lineup ? 0 : 1,
                       static_cast<int>(languages::fit(s.lang, in.languages)), std::move(s)});
    }
    std::sort(keyed.begin(), keyed.end(), [](const Keyed& a, const Keyed& b) {
      if (a.lineup != b.lineup) return a.lineup < b.lineup;
      if (a.fit != b.fit) return a.fit < b.fit;
      if (a.source.kind != b.source.kind) return static_cast<int>(a.source.kind) < static_cast<int>(b.source.kind);
      int ra = a.source.rank.value_or(4), rb = b.source.rank.value_or(4);
      if (ra != rb) return ra < rb;
      return a.index < b.index;
    });
    std::vector<EventSource> kept;
    kept.reserve(keyed.size());
    std::map<std::string, int> stations;  // "" yours and unknown; else the language
    int folded = 0, abroad = 0;
    for (Keyed& item : keyed) {
      EventSource& source = item.source;
      if (source.rank == station_rank::abroad && ++abroad > kAbroadPerGame) {
        stats.abroadDropped += 1;
        continue;
      }
      if (source.kind == EventSource::Kind::guide) {
        int& count = stations[item.fit == static_cast<int>(languages::Fit::other) ? source.lang : std::string()];
        if (++count > 3) {
          folded += 1;
          continue;
        }
      }
      kept.push_back(std::move(source));
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
  // The day-guessed slots seen today, noted for the days after.
  std::vector<std::string> guessedTitles;
  for (const ProviderEvent& event : in.events)
    if (event.guessed) guessedTitles.push_back(event.title);
  SlotsSeen seen = noteSlotsSeen(in.seen ? *in.seen : SlotsSeen{}, guessedTitles, now, in.zone);
  if (in.seen && *in.seen == seen) {
    out.seen = in.seen;
  } else {
    out.seen = std::make_shared<const SlotsSeen>(std::move(seen));
  }
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

namespace {

// The place a tournament is played when its name doesn't say ("Erste Bank
// Open" is Vienna's), by words of the name.
const std::vector<std::pair<const char*, const char*>>& tennisHomes() {
  static const std::vector<std::pair<const char*, const char*>> kHomes = {
      {"erste bank", "vienna"},       {"swiss indoors", "basel"},      {"pan pacific", "tokyo"},
      {"japan open", "tokyo"},        {"kinoshita", "tokyo"},          {"china open", "beijing"},
      {"korea open", "seoul"},        {"bnp paribas", "indian wells"}, {"internazionali", "rome"},
      {"abierto mexicano", "acapulco"}, {"terra wortmann", "halle"},   {"credit one", "charleston"},
      {"porsche", "stuttgart"},       {"citi dc", "washington"},       {"european open", "antwerp"},
      {"moselle open", "metz"},       {"hellenic", "athens"},          {"roland garros", "paris"},
      {"french open", "paris"},       {"wimbledon", "london"},         {"us open", "new york"},
      {"australian open", "melbourne"}, {"atp finals", "turin"},       {"generali open", "kitzbuhel"},
      {"nordea open", "bastad"},      {"plava laguna", "umag"},        {"western and southern", "cincinnati"},
  };
  return kHomes;
}

// Cities as other countries' feeds write them ("ATP Tennis: Wien").
const std::vector<std::pair<const char*, const char*>>& tennisCityNames() {
  static const std::vector<std::pair<const char*, const char*>> kNames = {
      {"wien", "vienna"},    {"viena", "vienna"},     {"vienne", "vienna"},   {"wenen", "vienna"},
      {"basilea", "basel"},  {"basileia", "basel"},   {"bale", "basel"},      {"roma", "rome"},
      {"rom", "rome"},       {"munchen", "munich"},   {"muenchen", "munich"}, {"tokio", "tokyo"},
      {"peking", "beijing"}, {"pekin", "beijing"},    {"torino", "turin"},    {"genf", "geneva"},
      {"geneve", "geneva"},  {"antwerpen", "antwerp"}, {"anvers", "antwerp"}, {"parigi", "paris"},
      {"londres", "london"}, {"londra", "london"},    {"atenas", "athens"},   {"athen", "athens"},
  };
  return kNames;
}

// A round or stage on its own ("Semifinals", "R16", "QF"): what a feed puts
// where the second player would go ("Vienna vs Semifinals").
bool isTennisRound(const std::string& normalizedText) {
  static const char* const kRound =
      "^ (?:semi ?finals?|semis|quarter ?finals?|finals?|round of \\d+|r\\d+|qf|sf|f|q\\d?|qualifying|qualifiers|"
      "(?:first|second|third|fourth|1st|2nd|3rd|4th) round|round \\d|day \\d+|early rounds) $";
  return rx::test(kRound, normalizedText);
}

}  // namespace

bool namesTennisEvent(std::string_view titleIn) {
  std::string title = normalized(titleIn);
  if (!(has(title, " atp ") || has(title, " wta ") || has(title, " tennis "))) return false;
  // Words of the show's name, the tours and the rounds don't name an event.
  static const std::set<std::string> kShow = {
      "tennis", "atp",      "wta",     "channel", "live",    "center",   "centre",   "court",       "today",
      "coverage", "tour",   "show",    "studio",  "news",    "highlights", "classic", "replay",     "encore",
      "semifinals", "semifinal", "semis", "quarterfinals", "quarterfinal", "finals", "final", "round", "rounds",
      "early",  "late",     "match",   "matches", "day",     "night",    "session",  "action",      "world",
      "from",   "the",      "and",     "with",    "presents", "plus",    "best",     "week",        "doubles",
      "singles", "mens",    "womens",  "men",     "women",   "qualifying"};
  for (const std::string& w : splitWords(title)) {
    if (text::count(w) >= 4 && !text::isAllDigits(w) && !kShow.count(w)) return true;
  }
  return false;
}

std::vector<int> tennisTargets(std::string_view titleIn, std::string_view family, std::optional<Seconds> start,
                               Seconds now, const std::vector<int>& matches, const std::vector<SportsGame>& games,
                               bool namedOnly) {
  if (matches.empty()) return {};
  std::string given(titleIn);
  if (looksLikeGame(given)) {
    // "ATP 500 Tennis: Vienna vs Semifinals": the tournament and its round,
    // not two players.
    auto sep = rx::first("\\s+(?:vs\\.?|v\\.?)\\s+", given, true);
    if (!sep || has(given, "@") || !isTennisRound(normalized(given.substr(sep->end)))) return {};
    given = given.substr(0, sep->begin);
  }
  std::string title = normalized(given + " " + std::string(family));
  for (const auto& [local, city] : tennisCityNames()) {
    if (has(title, std::string(" ") + local + " ")) title += std::string(city) + " ";
  }
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
      std::string name = normalized(*g.tournament);
      std::set<std::string> places;
      if (g.venue) {
        // The city ("Tokyo, Japan" -> tokyo); a court ("Center Court") is
        // no place to name.
        std::string city = normalized(text::components(*g.venue, ",").front());
        if (!has(city, " court ") && !has(city, " arena ") && !has(city, " stadium ")) places.insert(city);
      }
      for (const auto& [words, home] : tennisHomes()) {
        if (has(name, std::string(" ") + words + " ")) places.insert(std::string(" ") + home + " ");
      }
      for (const std::string& place : places) {
        if (place.size() > 4 && !has(name, place) && has(title, place)) {
          c.words += 1;
          break;
        }
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
  if (!pick && namedOnly) return {};
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
      // Clubs by the names European feeds use ("Paris SaintGermain",
      // "Olympiakos", "Olympique Lyonnais", "Deportivo A Coruña").
      {"paris saint germain", {"paris saintgermain", "paris sg"}},
      {"olympiacos", {"olympiakos", "olympiakos piraeus", "olympiacos piraeus"}},
      {"lyon", {"olympique lyonnais", "olympique lyon"}},
      {"marseille", {"olympique marseille", "olympique de marseille"}},
      {"deportivo", {"deportivo a coruna", "deportivo la coruna", "deportivo de la coruna"}},
      {"lask linz", {"lask"}},
      {"stade de reims", {"reims"}},
      {"bayern munich", {"bayern munchen", "fc bayern munchen"}},
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

namespace {

// "u19" (as read from the end of a name, or after a team in a title).
bool ageWord(const std::string& w) { return w.size() == 3 && w[0] == 'u' && text::isAllDigits(w.substr(1)); }

// Words after a first team's name that make it another squad: "XI",
// "Emerging", "JV", "U19" / "U-19" / "Under 19". "A" and "B" (written in
// capitals) and "Women" are the caller's to check.
bool squadWord(const std::string& w, const std::string& next) {
  static const std::set<std::string> kWords = {"xi",    "emerging", "development", "academy",
                                               "reserves", "youth", "jv"};
  return kWords.count(w) > 0 || ageWord(w) || ((w == "u" || w == "under") && next.size() == 2 && text::isAllDigits(next));
}

// A squad as its first team is named ("Australia" for Australia Women), and
// its qualifier.
std::optional<std::pair<SportsTeam, std::string>> squadCore(const SportsTeam& team) {
  auto squad = squadOf(team.name);
  if (!squad) return std::nullopt;
  SportsTeam core = team;
  core.name = squad->first;
  core.alternates.clear();
  for (const std::string& alternate : team.alternates) {
    auto other = squadOf(alternate);
    core.alternates.push_back(other ? other->first : alternate);
  }
  return std::make_pair(core, squad->second);
}

// Whether a squad's bare name may count: the title says its qualifier
// somewhere ("Brisbane Heat vs Sydney Thunder _ Women`s Cricket", "India vs
// Australia, U19 World Cup"). Women's and age sides only: an "A" side, an
// XI or a reserve side is never named without its qualifier.
bool saysQualifier(const std::string& normalizedTitle, const std::string& qualifier) {
  for (const std::string& w : text::words(qualifier)) {
    if (w == "women") {
      if (!has(normalizedTitle, " women ") && !has(normalizedTitle, " womens ")) return false;
    } else if (ageWord(w)) {
      std::string age = w.substr(1);
      if (!has(normalizedTitle, " " + w + " ") && !has(normalizedTitle, " u " + age + " ") &&
          !has(normalizedTitle, " under " + age + " "))
        return false;
    } else {
      return false;
    }
  }
  return true;
}

bool bareNameCounts(const std::string& qualifier) {
  for (const std::string& w : text::words(qualifier))
    if (w != "women" && !ageWord(w)) return false;
  return true;
}

}  // namespace

std::optional<std::pair<std::string, std::string>> squadOf(std::string_view teamName) {
  std::vector<std::string> words = splitWords(normalized(teamName));
  std::vector<std::string> tail;
  while (words.size() >= 2) {
    size_t n = words.size();
    const std::string& w = words.back();
    if (w == "s" && n >= 3 && words[n - 2] == "women") {
      // "Women's"
      tail.insert(tail.begin(), "women");
      words.resize(n - 2);
    } else if (w == "women" || w == "womens") {
      tail.insert(tail.begin(), "women");
      words.pop_back();
    } else if (n >= 3 && w.size() == 2 && text::isAllDigits(w) && (words[n - 2] == "u" || words[n - 2] == "under")) {
      tail.insert(tail.begin(), "u" + w);
      words.resize(n - 2);
    } else if (w == "a" || w == "b" || squadWord(w, "")) {
      tail.insert(tail.begin(), w);
      words.pop_back();
    } else {
      break;
    }
  }
  if (tail.empty() || words.empty()) return std::nullopt;
  return std::make_pair(str::join(words, " "), str::join(tail, " "));
}

std::vector<std::string> teamVariants(const SportsTeam& team, bool college) {
  // A squad ("New Zealand A", "Brisbane Heat Women"): its first team's ways,
  // each with the qualifier ("new zealand a", "heat women"), never the
  // qualifier alone.
  if (auto squad = squadCore(team)) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (const std::string& v : teamVariants(squad->first, college)) {
      std::string named = v + " " + squad->second;
      if (seen.insert(named).second) out.push_back(named);
    }
    return out;
  }
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
  // "Central Córdoba (Santiago del Estero)": feeds leave out the bracket.
  if (size_t open = team.name.find(" ("); open != std::string::npos && !college) {
    std::string bare = trimmed(normalized(team.name.substr(0, open)));
    if (text::count(bare) >= 6 && !genericWords().count(bare)) names.push_back(bare);
  }
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
      " weekly ",     " race day live ", " center ",  " show ",       " auction ",    " sportscentre ",
      // Talk and analysis around a race weekend ("Código F1", "DAZN F1
      // Debrief", "Análise ao GP", "GP de México - Previo Carrera").
      " codigo ",     " debrief ",    " analise ",    " analyse ",    " analysis ",   " previo "};
  std::string t = normalized(title);
  for (const char* w : kStudio) {
    if (has(t, w)) return true;
  }
  // TSN's SportsCentre: "SC with Jay Onrait", "SC: Top Characters in Sports".
  return rx::test("^\\W*SC(?:\\s*:|\\s+with\\b)", title);
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
  // The competition after " · " isn't a side ("A vs B · DFB-Pokal Frauen").
  std::string_view matchup = title.substr(0, title.find("\xC2\xB7"));
  std::string cleaned = rx::replace(rx::replace(matchup, "^[A-Za-z+ ]{2,20}:\\s*", ""), "\\([^)]*\\)?", " ");
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
  // A club's initials and founding year aren't how every feed names it
  // ("VfL Wolfsburg v TSG 1899 Hoffenheim" is "Wolfsburg vs Hoffenheim").
  auto side = [](const std::vector<std::string>& words) {
    static const std::set<std::string> kClubWords = {"fc",  "cf", "sc", "afc", "vfl", "vfb", "tsg", "sv", "ac",
                                                     "as",  "ss", "cd", "ud",  "rcd", "rc",  "ca",  "if", "ik",
                                                     "aif", "bk", "ff", "fk",  "nk",  "kf",  "sk"};
    std::vector<std::string> kept;
    for (const std::string& w : splitWords(normalized(str::join(words, " ")))) {
      if (!kClubWords.count(w) && !(w.size() == 4 && text::isAllDigits(w))) kept.push_back(w);
    }
    return kept.empty() ? trimmed(normalized(str::join(words, " "))) : str::join(kept, " ");
  };
  std::string a = side(left);
  std::string b = side(right);
  if (a.empty() || b.empty()) return std::nullopt;
  return a < b ? a + "|" + b : b + "|" + a;
}

std::string shortLeague(std::string_view league) {
  if (league.empty() || league == "Other") return "EVENT";
  const Profile& p = profile(league);
  return p.chip.empty() ? str::upper(league) : p.chip;
}

// MARK: TeamIndex

std::vector<int> scheduledFor(const TeamIndex& index, std::string_view title, const std::function<bool(int)>& keep) {
  // Both teams named as the schedule names them (no city names or
  // lower-case abbreviations: the sport isn't known).
  return index.matches(title, keep);
}

TeamIndex::TeamIndex(const std::vector<SportsGame>& games) {
  // Clubs known by their first word ("Montpellier" for Montpellier Hérault,
  // "Castres" for Castres Olympique): soccer and rugby, when no other team
  // of the sport has that first word.
  auto firstWord = [](const SportsTeam& team) {
    // Never a squad's ("Harbor B" isn't "Harbor").
    if (squadOf(team.name)) return std::string();
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
        byFirstWord_[w].push_back(
            Name{static_cast<int>(i), side, team->id, {w}, std::nullopt, false, std::nullopt, true, false, false});
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
    add(game.home, static_cast<int>(i), 0, college, true, &game);
    add(game.away, static_cast<int>(i), 1, college, true, &game);
  }
}

TeamIndex TeamIndex::fromTeams(const std::vector<SportsTeam>& list, bool college) {
  TeamIndex index;
  index.teams_ = list;
  for (size_t i = 0; i < list.size(); ++i) index.add(list[i], static_cast<int>(i), 0, college, false);
  return index;
}

void TeamIndex::add(const SportsTeam& team, int slot, int side, bool college, bool abbreviations,
                    const SportsGame* game) {
  std::set<std::string> seen;
  auto squad = squadCore(team);
  // A first team in a game between two teams (not players, a race or a
  // tournament); cricket writes its women's sides "Australia Women".
  const bool firstTeam = game && !squad && !game->isTennis() && !game->isRacing() && !game->isGolf() && !game->cardKey;
  const bool womenSides = game && game->isCricket();
  auto put = [&](std::vector<std::string> words, std::optional<std::string> abbreviation, bool loose,
                 std::optional<std::string> needs) {
    std::string first = words.front();
    byFirstWord_[first].push_back(Name{slot, side, team.id, std::move(words), std::move(abbreviation), loose,
                                       std::move(needs), firstTeam, womenSides, college});
  };
  for (const std::string& variant : teamVariants(team, college)) {
    std::vector<std::string> words = text::words(variant);
    if (words.empty() || !seen.insert(variant).second) continue;
    put(std::move(words), std::nullopt, false, std::nullopt);
  }
  // A women's or age side by its first team's name: only when the title
  // says which side elsewhere ("_ Women`s Cricket").
  const bool bare = squad && bareNameCounts(squad->second);
  if (bare) {
    for (const std::string& variant : teamVariants(squad->first, college)) {
      std::vector<std::string> words = text::words(variant);
      if (words.empty() || !seen.insert(variant).second) continue;
      put(std::move(words), std::nullopt, false, squad->second);
    }
  }
  // Pro teams' city ("Atlanta vs. Chicago"), only when the league is known;
  // a squad's ("New Zealand" for New Zealand A) only as its bare name.
  if (!college && team.location && (!squad || bare)) {
    std::string variant = trimmed(normalized(*team.location));
    std::vector<std::string> words = text::words(variant);
    if (!words.empty() && text::count(variant) >= 4 && seen.insert(variant).second) {
      put(std::move(words), std::nullopt, true, squad ? std::optional<std::string>(squad->second) : std::nullopt);
    }
  }
  if (abbreviations && team.shortName) {
    std::string shortName = trimmed(*team.shortName);
    size_t n = text::count(shortName);
    if (n >= 2 && n <= 5) {
      std::string word = str::lower(shortName);
      if (seen.insert(word).second) put({word}, str::upper(shortName), false, std::nullopt);
    }
  }
}

std::vector<TeamIndex::Hit> TeamIndex::hits(std::string_view title, const Filter& allowed,
                                            const Filter& allowLoose, std::set<std::string>* written) const {
  const std::string norm = normalized(title);
  std::vector<std::string> words = splitWords(norm);
  if (words.size() < 2) return {};
  // Words as written, for "abbreviations count only in capitals". In an
  // all-capitals title every word looks like an abbreviation, so none
  // counts on its own there.
  if (written) {
    if (text::hasLowercase(title)) {
      for (std::string& w : guide::letterNumberTokens(title)) written->insert(std::move(w));
    }
  }
  // The same words as written, for an "A" or "B" side (read when needed).
  std::optional<std::vector<std::string>> cased;
  // The word after a first team's name makes it another squad: "India A",
  // "Harbor FC U19", "Australia XI". Where the sport names its women's sides
  // "Australia Women" (cricket), a title saying "Women" anywhere is theirs.
  const bool saysWomen = saysQualifier(norm, "women");
  auto anotherSquad = [&](const Name& name, size_t at) {
    if (name.womenSides && saysWomen) return true;
    if (at >= words.size()) return false;
    const std::string& w = words[at];
    if (name.school && w == "academy") return false;
    if (squadWord(w, at + 1 < words.size() ? words[at + 1] : std::string())) return true;
    // "Harbor Women vs Valley Women" (a feed's "Harbor (F)"): the club's
    // women's side. Not "Valley Women's Volleyball" or "(Women's)", a
    // college sport's tag.
    if (w == "women" && (at + 1 >= words.size() || words[at + 1] != "s")) return true;
    // "A-League" is a league, not an A side.
    if ((w != "a" && w != "b") || (at + 1 < words.size() && words[at + 1] == "league")) return false;
    if (!cased) cased = guide::letterNumberTokens(str::replaceAll(str::foldDiacritics(title), "&", " and "));
    return cased->size() == words.size() && ((*cased)[at] == "A" || (*cased)[at] == "B");
  };
  std::vector<Hit> found, squads;
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
      if (name.needs && !saysQualifier(norm, *name.needs)) continue;
      if (name.firstTeam && anotherSquad(name, end)) {
        squads.push_back(Hit{&name, static_cast<int>(position), static_cast<int>(end)});
        continue;
      }
      found.push_back(Hit{&name, static_cast<int>(position), static_cast<int>(end)});
    }
  }
  // Nor the team's shorter names inside that squad's ("Harbor" in "Harbor
  // FC U19").
  if (!squads.empty()) {
    found.erase(std::remove_if(found.begin(), found.end(),
                               [&](const Hit& hit) {
                                 return std::any_of(squads.begin(), squads.end(), [&](const Hit& squad) {
                                   return squad.name->team == hit.name->team && squad.start <= hit.start &&
                                          squad.end >= hit.end;
                                 });
                               }),
                found.end());
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
