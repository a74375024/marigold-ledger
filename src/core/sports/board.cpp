#include "core/sports/board.hpp"

#include <algorithm>
#include <cstdio>
#include <unordered_map>

#include "core/guide/names.hpp"
#include "core/sports/matcher.hpp"
#include "core/sports/player_photos.hpp"
#include "core/sports/profiles.hpp"
#include "core/sports/text.hpp"

namespace marigold::sports::board {

namespace {

using State = SportsEvent::State;

constexpr const char* kDot = " \xC2\xB7 ";            // " · "
constexpr const char* kWideDot = "  \xC2\xB7  ";      // "  ·  "
constexpr const char* kWiderDot = "   \xC2\xB7   ";   // "   ·   "

const char* const kSportOrder[] = {"NFL",   "College Football", "NBA",      "WNBA",       "College Basketball",
                                   "MLB",   "NHL",              "MLS",      "Soccer",     "Fighting",
                                   "Motorsport", "Golf",        "Tennis"};

int sportRank(const std::string& group) {
  int i = 0;
  for (const char* s : kSportOrder) {
    if (group == s) return i;
    ++i;
  }
  return 99;
}

std::string twoDigits(int v) {
  char buf[8];
  std::snprintf(buf, sizeof buf, "%02d", v);
  return buf;
}

const char* const kLongDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char* const kShortDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

std::string weekdayName(Seconds t, const time::Zone& zone, bool longName) {
  time::Civil c = zone.toLocal(t);
  int day = time::weekday(c.year, c.month, c.day);
  return longName ? kLongDays[day] : kShortDays[day];
}

std::string countText(int n, const char* one, const char* many) {
  return n == 1 ? std::string(one) : std::to_string(n) + " " + many;
}

std::string capitalizeFirst(const std::string& s) { return str::upper(text::prefix(s, 1)) + text::dropPrefix(s, 1); }

// MARK: Card parts

std::string cardKind(const SportsEvent& part) {
  if (!part.game || !part.game->cardKey) return "";
  size_t cut = part.game->cardKey->rfind('|');
  return cut == std::string::npos ? "" : part.game->cardKey->substr(cut + 1);
}

std::pair<std::string, std::string> partNames(const SportsEvent& part) {
  std::string key = part.game && part.game->cardKey ? *part.game->cardKey : "";
  if (part.game && part.game->isRacing() && key.find('|') != std::string::npos) {
    std::string kind = cardKind(part);
    return {race_session::shortName(kind), race_session::name(kind)};
  }
  if (str::endsWith(key, "|early")) return {"Early", "Early Prelims"};
  if (str::endsWith(key, "|prelims")) return {"Prelims", "Prelims"};
  return {"Main", "Main Card"};
}

std::optional<LivePhase> phaseOf(const SportsEvent& part, const scores::Statuses& statuses) {
  if (!part.game) return std::nullopt;
  auto it = statuses.find(part.game->id);
  if (it == statuses.end()) return std::nullopt;
  return it->second.phase;
}

bool isLivePhase(std::optional<LivePhase> p) { return p == LivePhase::live || p == LivePhase::delayed; }

// A race weekend: the session on now, else the next one not over (by its
// status, else its usual length), else the last.
const SportsEvent* raceCurrent(const std::vector<SportsEvent>& parts, Seconds now, const scores::Statuses& statuses) {
  // Past its usual end (plus half an hour): a status nobody refreshed while
  // it ran doesn't hold it.
  auto over = [&](const SportsEvent& p) { return p.start && now > *p.start + p.expectedLength() + 30 * 60; };
  auto stale = [&](const SportsEvent& p) { return p.start && now > *p.start + 12 * 3600; };
  for (const SportsEvent& p : parts) {
    if ((p.live == LivePhase::live || isLivePhase(phaseOf(p, statuses))) && !stale(p)) return &p;
  }
  for (const SportsEvent& p : parts) {
    auto value = phaseOf(p, statuses);
    if (value == LivePhase::final || value == LivePhase::postponed || (p.game && p.game->completed)) continue;
    if ((!value || value == LivePhase::pre) && over(p)) continue;
    if (isLivePhase(value) && stale(p)) continue;
    return &p;
  }
  return parts.empty() ? nullptr : &parts.back();
}

// The part on now by its fights' status; without it, by start times.
const SportsEvent* currentPart(const std::vector<SportsEvent>& parts, Seconds now, const scores::Statuses& statuses) {
  if (parts.empty()) return nullptr;
  const SportsEvent& first = parts.front();
  if (first.game && first.game->isRacing()) return raceCurrent(parts, now, statuses);
  std::vector<std::optional<LivePhase>> phases;
  for (const SportsEvent& p : parts) phases.push_back(phaseOf(p, statuses));
  if (std::any_of(phases.begin(), phases.end(), [](const auto& p) { return p.has_value(); })) {
    for (size_t i = 0; i < phases.size(); ++i) {
      if (isLivePhase(phases[i])) return &parts[i];
    }
    // Between parts: the first one not finished yet.
    for (size_t i = 0; i < phases.size(); ++i) {
      if (phases[i] != LivePhase::final && phases[i] != LivePhase::postponed) {
        if (i > 0 || (parts[i].start && *parts[i].start <= now)) return &parts[i];
        return &first;
      }
    }
    return &parts.back();
  }
  for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
    if (it->start && *it->start <= now) return &*it;
  }
  return &first;
}

// A race weekend's tile: the session on now (or next) decides its time,
// status and where to watch; the weekend is over once its last one is.
SportsEvent raceWeekendTile(const std::string& id, const std::vector<SportsEvent>& parts, const SportsEvent& current) {
  SportsEvent tile;
  tile.id = id;
  tile.title = current.game ? current.game->home.name : current.title;
  tile.league = current.league;
  tile.leagueShort = current.leagueShort;
  tile.start = current.start;
  tile.game = current.game;
  tile.sources = current.sources;
  tile.family = current.family;
  tile.listingEnd = current.listingEnd;
  tile.feedsEndedAt = current.feedsEndedAt;
  tile.moreStations = current.moreStations;
  tile.teams = current.teams;
  if (current.live) {
    switch (*current.live) {
      case LivePhase::live:
      case LivePhase::delayed: tile.live = LivePhase::live; break;
      case LivePhase::final: tile.live = current.id == parts.back().id ? LivePhase::final : LivePhase::pre; break;
      case LivePhase::postponed: tile.live = LivePhase::postponed; break;
      case LivePhase::pre: tile.live = LivePhase::pre; break;
    }
  }
  return tile;
}

// A card's parts become one tile: its id "card|UFC|<event>", starting with
// the first part, the headline bout's sides, and every part's sources (the
// part on now first). Cards that aren't split stay as is.
std::vector<SportsEvent> mergingCards(const std::vector<SportsEvent>& events, Seconds now,
                                      const scores::Statuses& statuses,
                                      std::map<std::string, std::vector<SportsEvent>>& cards) {
  std::map<std::string, std::vector<size_t>> groups;
  for (size_t i = 0; i < events.size(); ++i) {
    const auto& game = events[i].game;
    if (!game || !game->cardKey || str::endsWith(*game->cardKey, "|")) continue;
    size_t cut = game->id.rfind('|');
    if (cut == std::string::npos) continue;
    groups[game->id.substr(0, cut)].push_back(i);
  }
  bool any = std::any_of(groups.begin(), groups.end(), [](const auto& g) { return g.second.size() >= 2; });
  if (!any) return events;
  std::map<size_t, SportsEvent> replaced;
  std::set<size_t> dropped;
  for (const auto& [card, indices] : groups) {
    if (indices.size() < 2) continue;
    std::vector<SportsEvent> parts;
    for (size_t i : indices) parts.push_back(events[i]);
    std::stable_sort(parts.begin(), parts.end(), [](const SportsEvent& a, const SportsEvent& b) {
      Seconds x = a.start.value_or(INT64_MAX), y = b.start.value_or(INT64_MAX);
      return x < y;
    });
    const SportsEvent& first = parts.front();
    const SportsEvent& main = parts.back();
    const SportsEvent* current = currentPart(parts, now, statuses);
    if (!first.start || !main.game || !current) continue;
    std::string id = "card|" + card;
    std::vector<size_t> sortedIndices = indices;
    std::sort(sortedIndices.begin(), sortedIndices.end());
    if (main.game->isRacing()) {
      cards[id] = parts;
      replaced[sortedIndices.front()] = raceWeekendTile(id, parts, *current);
      dropped.insert(sortedIndices.begin() + 1, sortedIndices.end());
      continue;
    }
    SportsGame game = *main.game;
    std::set<std::string> seen;
    std::vector<EventSource> sources;
    std::vector<EventSource> all = current->sources;
    for (const SportsEvent& p : parts) all.insert(all.end(), p.sources.begin(), p.sources.end());
    for (const EventSource& s : all) {
      if (seen.insert(s.channelID).second) sources.push_back(s);
    }
    if (game.cardKey) {
      size_t cut = game.cardKey->rfind('|');
      if (cut != std::string::npos) game.cardKey = game.cardKey->substr(0, cut) + "|card";
    }
    SportsEvent merged;
    merged.id = id;
    merged.title = main.title;
    merged.league = main.league;
    merged.leagueShort = main.leagueShort;
    merged.start = first.start;
    merged.game = game;
    merged.sources = std::move(sources);
    merged.family = current->family;
    // Live until the main card's usual end.
    if (main.start) {
      Seconds end = *main.start + main.expectedLength();
      if (main.listingEnd && *main.listingEnd > end) end = *main.listingEnd;
      merged.listingEnd = end;
    }
    merged.feedsEndedAt = main.feedsEndedAt;
    // The card's phase from its parts' fights.
    std::vector<LivePhase> phases;
    for (const SportsEvent& p : parts) {
      if (p.live) phases.push_back(*p.live);
    }
    if (!phases.empty()) {
      bool anyLive = std::any_of(phases.begin(), phases.end(), [](LivePhase p) { return isLivePhase(p); });
      bool allDone = phases.size() == parts.size() && std::all_of(phases.begin(), phases.end(), [](LivePhase p) {
                       return p == LivePhase::final || p == LivePhase::postponed;
                     });
      bool anyFinal = std::find(phases.begin(), phases.end(), LivePhase::final) != phases.end();
      if (anyLive) {
        merged.live = LivePhase::live;
      } else if (allDone) {
        merged.live = anyFinal ? LivePhase::final : LivePhase::postponed;
      } else if (anyFinal) {
        merged.live = LivePhase::live;  // between parts
      } else {
        merged.live = LivePhase::pre;
      }
    }
    cards[id] = parts;
    replaced[sortedIndices.front()] = std::move(merged);
    dropped.insert(sortedIndices.begin() + 1, sortedIndices.end());
  }
  std::vector<SportsEvent> result;
  result.reserve(events.size());
  for (size_t i = 0; i < events.size(); ++i) {
    if (dropped.count(i)) continue;
    auto r = replaced.find(i);
    result.push_back(r != replaced.end() ? r->second : events[i]);
  }
  return result;
}

// MARK: Cameras and placeholders

const std::set<std::string>& genericNascarRaces() {
  static const std::set<std::string> kRaces = {"nascar", "nascar cup", "nascar cup series"};
  return kRaces;
}

// Camera feeds move to their race's cameras panel. An in-car feed or
// multiview only goes when a tile for its race is on the board; otherwise
// it stays a tile of its own.
std::set<std::string> hiddenCameras(const std::vector<SportsEvent>& events, Seconds now, const Input& in) {
  std::set<std::string> hidden;
  if (!in.isCameraName) return hidden;
  auto isCameraFeed = [&](const SportsEvent& e) { return !e.game && in.isCameraName(e.title); };
  std::map<std::string, std::vector<std::string>> byRace;
  for (const SportsEvent& e : events) {
    if (!isCameraFeed(e)) continue;
    std::optional<std::string> race = in.cameraRace ? in.cameraRace(e.title) : std::nullopt;
    if (race) {
      byRace[*race].push_back(e.id);
    } else {
      hidden.insert(e.id);
    }
  }
  if (byRace.empty()) return hidden;
  // Each race's keys: its distinctive words and number ("yellawood", "500").
  static const std::set<std::string> kStop = {"nascar", "series", "racing", "speedway", "motor", "sports", "grand"};
  std::map<std::string, std::vector<std::string>> keys;
  for (const auto& [race, ids] : byRace) {
    std::vector<std::string> list;
    for (const std::string& token : guide::letterNumberTokens(race)) {
      if ((text::count(token) >= 5 && !kStop.count(token)) || (text::count(token) >= 3 && text::isAllDigits(token))) {
        list.push_back(token);
      }
    }
    keys[race] = list.empty() ? std::vector<std::string>{race} : list;
  }
  std::set<std::string> found;
  for (const SportsEvent& e : events) {
    if (e.sources.empty() || isCameraFeed(e) ||
        !(e.league == "Motorsport" || e.league == "Other" || e.league.empty())) {
      continue;
    }
    std::string title = str::lower(e.title);
    // A NASCAR race on now (or about to start): cameras naming no race are its.
    bool nascarRace = e.game && e.game->leagueShort == "NASCAR" && e.start && *e.start < now + 3 * 3600 &&
                      *e.start > now - 6 * 3600;
    for (const auto& [race, list] : keys) {
      if (found.count(race)) continue;
      bool named = std::any_of(list.begin(), list.end(),
                               [&](const std::string& k) { return title.find(k) != std::string::npos; });
      if (named || (nascarRace && genericNascarRaces().count(race))) found.insert(race);
    }
    if (found.size() == byRace.size()) break;
  }
  for (const std::string& race : found) hidden.insert(byRace[race].begin(), byRace[race].end());
  return hidden;
}

// An empty slot's name: "No Match Scheduled", "No Event Scheduled".
bool isPlaceholder(const SportsEvent& e) {
  if (e.game) return false;
  std::string lower = str::lower(e.title);
  for (const char* s : {"no match scheduled", "no event scheduled", "no events scheduled", "no game scheduled",
                        "no games scheduled"}) {
    if (str::endsWith(lower, s)) return true;
  }
  return false;
}

// MARK: Whip-arounds

struct WhipInfo {
  int live = 0;
  std::vector<std::string> redZone;
  std::string subtitle;
};

std::map<std::string, WhipInfo> addWhipArounds(std::vector<SportsEvent>& all, std::map<std::string, State>& states,
                                               const scores::Statuses& statuses, const std::vector<WhipChannel>& whips,
                                               Seconds now) {
  std::map<std::string, WhipInfo> info;
  if (whips.empty()) return info;
  std::vector<WhipChannel> ordered = whips;
  std::stable_sort(ordered.begin(), ordered.end(),
                   [](const WhipChannel& a, const WhipChannel& b) { return a.inLineup && !b.inLineup; });
  std::map<std::string, std::vector<WhipChannel>> groups;
  for (const WhipChannel& w : ordered) groups[w.whip.group].push_back(w);
  for (const auto& [group, members] : groups) {
    const Whip& whip = members.front().whip;
    if (whip.sundayAfternoon) {
      // RedZone airs from the 1 PM ET kickoffs to the end of the late window.
      time::Civil et = time::Zone::usEastern().toLocal(now);
      if (time::weekday(et.year, et.month, et.day) != 0 || et.hour < 13 || et.hour >= 20) continue;
    }
    std::vector<const SportsEvent*> liveGames;
    for (const SportsEvent& e : all) {
      auto st = states.find(e.id);
      if (e.game && st != states.end() && st->second == State::live &&
          (e.league == group || (group == "Soccer" && e.league == "MLS"))) {
        liveGames.push_back(&e);
      }
    }
    if (static_cast<int>(liveGames.size()) < whip.minimumLive) continue;
    std::string id = "whip|" + group;
    SportsEvent tile;
    tile.id = id;
    tile.title = group == "Soccer" ? members.front().displayName : whip.title;
    tile.league = group;
    tile.leagueShort = whip.shortName;
    std::optional<Seconds> earliest;
    for (const SportsEvent* e : liveGames) {
      if (e->start && (!earliest || *e->start < *earliest)) earliest = e->start;
    }
    tile.start = earliest.value_or(now);
    for (const WhipChannel& member : members) {
      EventSource s;
      s.channelID = member.channelID;
      s.kind = member.inLineup ? EventSource::Kind::lineup : EventSource::Kind::guide;
      s.detail = member.inLineup ? "In your lineup" : "In your guide";
      tile.sources.push_back(std::move(s));
    }
    tile.live = LivePhase::live;
    // Games in the red zone now (the team with the ball).
    WhipInfo w;
    w.live = static_cast<int>(liveGames.size());
    w.subtitle = whip.subtitle;
    for (const SportsEvent* e : liveGames) {
      auto status = statuses.find(e->game->id);
      if (status == statuses.end() || status->second.phase != LivePhase::live || !status->second.redZone) continue;
      const SportsGame& game = *e->game;
      const auto& possession = status->second.possession;
      if (possession == game.home.id) {
        w.redZone.push_back(shortName(game.home, false));
      } else if (possession == game.away.id) {
        w.redZone.push_back(shortName(game.away, false));
      } else {
        w.redZone.push_back(title(game));
      }
    }
    info[id] = w;
    all.insert(all.begin(), std::move(tile));
    states[id] = State::live;
  }
  return info;
}

// MARK: Describe

// "A vs B", "A vs. B", "A at B" in a title with no schedule game.
std::optional<std::tuple<std::string, std::string, bool>> sidesIn(const std::string& eventTitle) {
  std::vector<std::string> pieces = text::components(eventTitle, ": ");
  std::string body = eventTitle;
  if (pieces.size() > 1) {
    std::vector<std::string> rest(pieces.begin() + 1, pieces.end());
    body = str::join(rest, ": ");
  }
  const std::pair<const char*, bool> separators[] = {
      {" vs. ", false}, {" vs ", false}, {" v ", false}, {" at ", true}, {" @ ", true}};
  for (const auto& [separator, homeSecond] : separators) {
    std::vector<std::string> parts = text::components(body, separator);
    if (parts.size() != 2) continue;
    std::string a(str::trimUnicode(parts[0])), b(str::trimUnicode(parts[1]));
    size_t na = text::count(a), nb = text::count(b);
    if (na < 2 || nb < 2 || na > 40 || nb > 40) continue;
    return std::make_tuple(a, b, homeSecond);
  }
  return std::nullopt;
}

SportsTeam namedSide(const std::string& prefix, const std::string& name,
                     const std::optional<std::string>& country = std::nullopt) {
  SportsTeam t;
  t.id = prefix + name;
  t.name = name;
  t.country = country;
  return t;
}

// What a tile says about where its game is on (EventWhere).
struct Place {
  std::optional<ChannelLabel> channel;
  bool inLineup = false;
  int feeds = 0;
  bool favorite = false;
  std::optional<std::string> tag;
  bool tagAlert = false;
  std::optional<std::string> pill;
  bool pillOutlined = false;
  std::string footer;
  std::optional<SportsTeam> top, bottom;
  std::optional<bool> homeIsTop;
  std::string title;
  std::optional<std::string> subtitle;
  std::vector<PartLine> parts;
  std::optional<std::string> partLabel;
  bool boutNow = false;
  std::optional<std::string> clock;
  bool notStarted = false;
  std::optional<Seconds> endedAt;
  std::optional<std::string> topScore, bottomScore;
  std::optional<bool> possessionTop;
  std::optional<std::string> situation;
  bool redZone = false;
  std::optional<std::vector<bool>> bases;
  std::optional<int> topPens, bottomPens;
  std::vector<LiveStatus::Leader> leaders;  // golf
  std::optional<std::string> lastPlay;
  std::optional<double> liveProgress;
  bool studio = false;
  std::vector<std::string> topSets, bottomSets;
  bool flags = false;  // tennis: country flags on the sides
  std::vector<PodiumDriver> podium;
  std::optional<std::string> podiumLabel;
};

struct Context {
  const Input& in;
  Seconds now;
  std::map<std::string, std::string> familyNames;
};

Place describe(const SportsEvent& event, const std::vector<SportsEvent>* parts, const Context& ctx,
               std::optional<Seconds> endedAt, const WhipInfo* whip) {
  const Input& in = ctx.in;
  const Seconds now = ctx.now;
  const bool showsScores = in.settings.showsScores;
  Place place;
  const SportsEvent* current = parts ? currentPart(*parts, now, in.statuses) : nullptr;
  const SportsEvent& basis = current ? *current : event;
  std::vector<EventSource> basisFeeds = basis.feeds();
  std::vector<EventSource> basisChannels = basis.channels();
  place.feeds = static_cast<int>(basisFeeds.size());
  place.favorite = event.game && isFavorite(in.prefs, *event.game);
  if (event.game) place.tag = event.game->note;

  // Sides, or a title.
  if (event.game && (event.game->isRacing() || event.game->isGolf())) {
    // A race weekend (the Grand Prix and its circuit) or a golf tournament.
    place.title = event.game->home.name;
    place.subtitle = event.game->venue;
  } else if (event.game) {
    const SportsGame& game = *event.game;
    place.top = game.homeFirst ? game.home : game.away;
    place.bottom = game.homeFirst ? game.away : game.home;
    if (!game.cardKey && !game.isTennis()) place.homeIsTop = game.homeFirst;
    std::string full = game.customTitle.value_or(event.title);
    place.title = std::string(str::trimUnicode(text::components(full, ":").front()));
  } else if (event.teams && event.teams->size() == 2) {
    place.top = (*event.teams)[0];
    place.bottom = (*event.teams)[1];
    place.title = event.title;
  } else if (auto pair = sidesIn(event.title)) {
    place.top = namedSide("t|", std::get<0>(*pair));
    place.bottom = namedSide("t|", std::get<1>(*pair));
    if (std::get<2>(*pair)) place.homeIsTop = false;
    place.title = event.title;
  } else {
    std::vector<std::string> pieces = text::components(event.title, ": ");
    if (pieces.size() >= 2) {
      std::string head(str::trimUnicode(pieces[0]));
      std::vector<std::string> restParts(pieces.begin() + 1, pieces.end());
      std::string rest(str::trimUnicode(str::join(restParts, ": ")));
      // "F1: Mexico Grand Prix" -> the race first; "Some Show: ..." -> the show first.
      if (text::count(head) <= 12) {
        place.title = rest;
        place.subtitle = capitalizeFirst(head);
      } else {
        place.title = head;
        place.subtitle = rest;
      }
    } else {
      place.title = event.title;
      if (!event.league.empty() && event.league != event.title) place.subtitle = event.league;
    }
  }

  // A fight card's timeline: by its fights' status when known, else by time.
  if (parts && !parts->empty()) {
    for (size_t index = 0; index < parts->size(); ++index) {
      const SportsEvent& part = (*parts)[index];
      Seconds start = part.start.value_or(now);
      PartLine line;
      line.label = partNames(part).first;
      line.start = start;
      line.current = current && part.id == current->id;
      auto status = part.game ? in.statuses.find(part.game->id) : in.statuses.end();
      if (status != in.statuses.end()) {
        switch (status->second.phase) {
          case LivePhase::final:
          case LivePhase::postponed: line.fill = 1; break;
          case LivePhase::live:
          case LivePhase::delayed: {
            int total = std::max(1, status->second.boutsTotal.value_or(1));
            line.fill = std::max(0.05, static_cast<double>(status->second.boutsDone.value_or(0)) / total);
            break;
          }
          case LivePhase::pre: line.fill = 0; break;
        }
      } else {
        Seconds next = index + 1 < parts->size() ? (*parts)[index + 1].start.value_or(start) : start + 3 * 3600;
        double span = static_cast<double>(std::max<Seconds>(60, next - start));
        line.fill = std::min(1.0, std::max(0.0, static_cast<double>(now - start) / span));
      }
      place.parts.push_back(std::move(line));
    }
    if (current) {
      place.partLabel = partNames(*current).second;
      auto status = current->game ? in.statuses.find(current->game->id) : in.statuses.end();
      if (status != in.statuses.end()) {
        const LiveStatus& s = status->second;
        if (isLivePhase(s.phase) && !s.detail.empty()) place.partLabel = partNames(*current).second + kDot + s.detail;
        // The fight on now (or next), never a result.
        if (s.boutA && s.boutB && s.phase != LivePhase::final) {
          place.top = namedSide("b|", *s.boutA, s.boutACountry);
          place.bottom = namedSide("b|", *s.boutB, s.boutBCountry);
          place.boutNow = isLivePhase(s.phase);
        }
      }
    }
  }

  // A race weekend: each session's own fill (they're days apart), the time
  // under the current one only, and the latest finished session's top 3.
  if (parts && !parts->empty() && parts->front().game && parts->front().game->isRacing()) {
    place.parts.clear();
    for (const SportsEvent& part : *parts) {
      Seconds start = part.start.value_or(now);
      double elapsed = static_cast<double>(now - start) / static_cast<double>(part.expectedLength());
      PartLine line;
      auto phase = phaseOf(part, in.statuses);
      if (phase == LivePhase::final || phase == LivePhase::postponed) {
        line.fill = 1;
      } else if (isLivePhase(phase)) {
        line.fill = std::max(0.05, std::min(0.95, elapsed));
      } else if (phase == LivePhase::pre) {
        line.fill = 0;
      } else {
        line.fill = part.game && part.game->completed ? 1 : std::min(1.0, std::max(0.0, elapsed));
      }
      line.label = partNames(part).first;
      line.start = start;
      line.current = current && part.id == current->id;
      line.timed = line.current;
      place.parts.push_back(std::move(line));
    }
    place.top.reset();
    place.bottom.reset();
    const SportsGame& first = *parts->front().game;
    if (showsScores && profile(first.league, first.leagueShort).podium && in.podium) {
      for (auto it = parts->rbegin(); it != parts->rend(); ++it) {
        if (!it->start || *it->start >= now || !it->game || !it->game->cardKey) continue;
        std::string kind = cardKind(*it);
        std::vector<PodiumDriver> top = in.podium(kind, *it->start);
        if (!top.empty()) {
          place.podium = std::move(top);
          place.podiumLabel = race_session::name(kind);
          break;
        }
      }
    }
  }

  // The live scoreboard: clock, and with scores on, the score and situation.
  auto status = !parts && event.game ? in.statuses.find(event.game->id) : in.statuses.end();
  if (status != in.statuses.end()) {
    const SportsGame& game = *event.game;
    const LiveStatus& s = status->second;
    if (!s.detail.empty()) place.clock = s.detail;
    if (s.phase == LivePhase::pre && event.start && now > *event.start) place.notStarted = true;
    if (s.phase == LivePhase::final) place.endedAt = endedAt;
    if (isLivePhase(s.phase)) place.liveProgress = clockProgress(s, game.league, game.leagueShort);
    if (showsScores && game.isGolf()) place.leaders = s.leaders;
    if (showsScores && game.isRacing() && s.top3.size() == 3 && profile(game.league, game.leagueShort).podiumFromScores) {
      for (const std::string& code : s.top3) place.podium.push_back({code, std::nullopt});
      place.podiumLabel = race_session::name("race");
    }
    if (showsScores && s.phase == LivePhase::live && s.redZone) {
      place.tag = std::string("RED ZONE");
      place.tagAlert = true;
    }
    if (game.isTennis()) {
      // Games per set; the match has no single score.
      if (showsScores && s.phase != LivePhase::pre) {
        place.topSets = s.awaySets.value_or(std::vector<std::string>{});
        place.bottomSets = s.homeSets.value_or(std::vector<std::string>{});
      }
    } else if (showsScores && s.phase != LivePhase::pre) {
      bool topIsHome = game.homeFirst;
      place.topScore = topIsHome ? s.homeScore : s.awayScore;
      place.bottomScore = topIsHome ? s.awayScore : s.homeScore;
      if (s.possession) {
        if (place.top && *s.possession == place.top->id) {
          place.possessionTop = true;
        } else if (place.bottom && *s.possession == place.bottom->id) {
          place.possessionTop = false;
        }
      }
      place.situation = s.situation;
      place.redZone = s.redZone;
      place.bases = s.bases;
      place.lastPlay = s.lastPlay;
      place.topPens = topIsHome ? s.homePens : s.awayPens;
      place.bottomPens = topIsHome ? s.awayPens : s.homePens;
    }
  } else if (parts && event.live == LivePhase::final) {
    place.clock = std::string("Final");
    place.endedAt = endedAt;
  }

  // Where to watch.
  bool hasSpanish = std::any_of(basis.sources.begin(), basis.sources.end(), [](const EventSource& s) { return s.spanish; });
  bool hasEnglish = std::any_of(basis.sources.begin(), basis.sources.end(), [](const EventSource& s) { return !s.spanish; });
  bool spanishToo = hasSpanish && hasEnglish;
  std::optional<std::string> streaming;
  if (event.game) {
    for (const std::string& n : event.game->networks) {
      if (isStreaming(n)) {
        streaming = n;
        break;
      }
    }
  }
  // The tile names what plays first: a team (or other "likely") channel only
  // when there's no feed for the game itself.
  const EventSource* named = nullptr;
  for (const EventSource& s : basisChannels) {
    if (s.kind != EventSource::Kind::team) {
      named = &s;
      break;
    }
  }
  if (!named && basisFeeds.empty() && !basisChannels.empty()) named = &basisChannels.front();
  std::optional<ChannelLabel> channel = named && in.channelFor ? in.channelFor(*named) : std::nullopt;
  if (channel) {
    place.channel = channel;
    place.inLineup = named->kind == EventSource::Kind::lineup;
    if (named->rank == station_rank::lineupHome || named->rank == station_rank::home) {
      place.footer = "Your local station";
    } else if (place.inLineup) {
      place.footer = "In your lineup";
    } else if (spanishToo && !named->spanish) {
      place.footer = "Also in Spanish";
    } else if (place.feeds > 0) {
      place.footer = "+ " + std::to_string(place.feeds) + " backup feeds";
    } else if (streaming) {
      place.footer = "Also streaming on " + *streaming;
    }
  } else if (place.feeds > 0) {
    auto family = basis.family ? ctx.familyNames.find(*basis.family) : ctx.familyNames.end();
    if (family != ctx.familyNames.end() && text::count(family->second) <= 16) {
      place.pill = family->second;
    } else {
      place.pill = std::string("Event feed");
    }
    place.footer = spanishToo ? std::string("Also in Spanish") : countText(place.feeds, "1 feed", "feeds");
  } else if (event.game && !event.game->networks.empty()) {
    const std::string& network = event.game->networks.front();
    place.pill = network;
    place.pillOutlined = true;
    place.footer = isStreaming(network) ? "Streaming only" : "Not in your channels";
  } else {
    place.footer = "Not in your channels";
  }
  // Tennis: the court where the match is, and the players' flags.
  if (event.game && profile(event.game->league).tile == Profile::Tile::Players) {
    if (event.game->venue) place.footer = *event.game->venue;
    place.flags = true;
  }
  // A whip-around: what it's showing across the sport.
  if (whip) {
    place.tag = std::string("WHIP-AROUND");
    place.title = event.title;
    if (showsScores && !whip->redZone.empty()) {
      place.subtitle = "Red zone: " + str::join(whip->redZone, ", ");
    } else {
      place.subtitle = whip->subtitle;
    }
    place.footer = countText(whip->live, "1 game live", "games live");
  }
  return place;
}

SideLine sideLine(const SportsTeam& team, bool soccer, bool home, const std::optional<std::string>& score,
                  std::optional<bool> leads, bool ball, const std::vector<std::string>& sets, bool flags) {
  SideLine line;
  line.teamID = team.id;
  line.name = shortName(team, soccer);
  line.fullName = team.name;
  line.abbreviation = team.abbreviation();
  line.rank = team.rank;
  line.country = team.country;
  line.color = team.color;
  line.altColor = team.altColor;
  line.lightColor = isLight(team.color);
  line.logoUrl = team.badge;
  line.home = home;
  line.score = score;
  line.leads = leads;
  line.possession = ball;
  line.sets = sets;
  if (flags) line.flags = team.flags;
  return line;
}

// 0 when the top line leads, 1 when the bottom does, nil when level or unknown.
std::optional<int> leader(const std::optional<std::string>& top, const std::optional<std::string>& bottom) {
  if (!top || !bottom) return std::nullopt;
  auto a = text::toInt(*top), b = text::toInt(*bottom);
  if (!a || !b || *a == *b) return std::nullopt;
  return *a > *b ? 0 : 1;
}

std::string lastName(const SportsTeam& fighter) {
  std::vector<std::string> words = text::words(fighter.name);
  return words.empty() ? fighter.name : words.back();
}

// When it's expected to end: the sport's usual length, or the guide
// listing's end when that's later.
std::optional<Seconds> endTime(const SportsEvent& event) {
  if (!event.start) return std::nullopt;
  Seconds end = *event.start + event.expectedLength();
  if (event.listingEnd && *event.listingEnd > end && *event.listingEnd < *event.start + 8 * 3600) end = *event.listingEnd;
  return end;
}

Tile makeTile(const SportsEvent& event, const Place& place, const std::string& rowID, bool showLeague, State state,
              const Context& ctx) {
  const Settings& settings = ctx.in.settings;
  const time::Zone& zone = ctx.in.zone;
  const Seconds now = ctx.now;
  Tile tile;
  tile.id = event.id;
  tile.key = rowID + "|" + event.id;
  tile.state = state;
  tile.watchable = !event.sources.empty();
  tile.studio = place.studio;
  tile.favorite = place.favorite;
  tile.tag = place.tag;
  tile.tagAlert = place.tagAlert;
  if (!tile.tag && showLeague) tile.leagueChip = event.leagueShort;

  // Status pill.
  switch (state) {
    case State::live:
      if (place.studio) {
        tile.pill = Pill::onNow;
        tile.pillText = "ON NOW";
      } else if (event.live == LivePhase::delayed) {
        tile.pill = Pill::delayed;
        tile.pillText = event.suspended ? "SUSPENDED" : "DELAYED";
      } else {
        tile.pill = tile.watchable ? Pill::live : Pill::liveOutlined;
        tile.pillText = "LIVE";
      }
      break;
    case State::mayHaveEnded:
      tile.pill = Pill::likelyOver;
      tile.pillText = "Likely over";
      break;
    case State::postponed:
      tile.pill = Pill::postponed;
      tile.pillText = "POSTPONED";
      break;
    case State::finished:
      tile.pill = Pill::ended;
      tile.pillText = place.clock.value_or("Ended");
      break;
    case State::unknownTime:
      tile.pill = Pill::timeNotListed;
      tile.pillText = "Time not listed";
      break;
    case State::upcoming:
      if (place.notStarted) {
        tile.pill = Pill::notStarted;
        tile.pillText = "Not started";
      }
      break;
  }
  // Status text.
  switch (state) {
    case State::live:
      if (place.partLabel) {
        tile.statusText = *place.partLabel;
      } else if (place.clock) {
        tile.statusText = *place.clock;
        tile.bases = place.bases;
      } else if (auto end = endTime(event)) {
        tile.statusText = "ends ~" + shortClockText(*end, zone, settings.twelveHour);
      }
      break;
    case State::mayHaveEnded:
      if (event.start) tile.statusText = "started " + shortClockText(*event.start, zone, settings.twelveHour);
      break;
    case State::upcoming:
      if (place.notStarted && event.start) {
        tile.statusText = "due " + shortClockText(*event.start, zone, settings.twelveHour);
      } else if (event.start) {
        tile.statusText = clockText(*event.start, zone, settings.twelveHour);
        tile.statusDetail = untilText(*event.start, now);
      }
      break;
    case State::postponed:
      if (event.start) tile.statusText = "was " + clockText(*event.start, zone, settings.twelveHour);
      break;
    case State::finished:
      if (place.endedAt) tile.statusText = "ended " + shortClockText(*place.endedAt, zone, settings.twelveHour);
      break;
    case State::unknownTime: break;
  }

  // Middle.
  tile.title = place.title;
  tile.subtitle = place.subtitle;
  tile.icon = symbol(event.league);
  bool soccer = event.game ? event.game->homeFirst : true;
  if (!place.parts.empty()) {
    tile.layout = Tile::Layout::card;
    for (PartLine part : place.parts) {
      part.text = part.timed ? part.label + " " + shortClockText(part.start, zone, settings.twelveHour) : part.label;
      tile.parts.push_back(std::move(part));
    }
    tile.partLabel = place.partLabel;
    tile.boutNow = place.boutNow;
    if (place.top && place.bottom) {
      tile.boutLine = std::string(place.boutNow ? "Now: " : "") + lastName(*place.top) + " vs. " + lastName(*place.bottom);
      tile.sides.push_back(sideLine(*place.top, soccer, false, std::nullopt, std::nullopt, false, {}, false));
      tile.sides.push_back(sideLine(*place.bottom, soccer, false, std::nullopt, std::nullopt, false, {}, false));
    }
    tile.podium = place.podium;
    tile.podiumLabel = place.podiumLabel;
  } else if (place.top && place.bottom) {
    tile.layout = Tile::Layout::sides;
    std::optional<int> lead = leader(place.topScore, place.bottomScore);
    // Level after extra time: the shootout decides who leads.
    if (!lead && place.topPens && place.bottomPens && *place.topPens != *place.bottomPens)
      lead = *place.topPens > *place.bottomPens ? 0 : 1;
    std::optional<bool> topLeads, bottomLeads;
    if (lead) {
      topLeads = *lead == 0;
      bottomLeads = *lead == 1;
    }
    tile.sides.push_back(sideLine(*place.top, soccer, place.homeIsTop == true, place.topScore, topLeads,
                                  place.possessionTop == true, place.topSets, place.flags));
    tile.sides.push_back(sideLine(*place.bottom, soccer, place.homeIsTop == false, place.bottomScore, bottomLeads,
                                  place.possessionTop == false, place.bottomSets, place.flags));
    tile.sides[tile.sides.size() - 2].pens = place.topPens;
    tile.sides.back().pens = place.bottomPens;
  } else {
    tile.layout = Tile::Layout::plain;
    tile.podium = place.podium;
    tile.podiumLabel = place.podiumLabel;
    tile.leaders = place.leaders;
    // A golf tournament without its leaderboard: the name is the title, not
    // also the tag.
    if (event.game && event.game->isGolf() && tile.leaders.empty()) tile.tag.reset();
  }

  // Footer.
  if (place.channel) {
    tile.channelName = place.channel->name;
    tile.channelDisplay = place.channel->display;
    tile.channelLogos = place.channel->logoUrls;
  }
  tile.inLineup = place.inLineup;
  tile.footerPill = place.pill;
  tile.footerPillOutlined = place.pillOutlined;
  tile.footer = place.footer;
  tile.situation = place.situation;
  tile.redZone = place.redZone;
  tile.lastPlay = place.lastPlay;
  tile.feeds = place.feeds;

  if (state == State::live && place.parts.empty() && !place.studio) {
    tile.progress = place.liveProgress ? *place.liveProgress : event.progress(now);
  }
  tile.clock = place.clock;
  tile.notStarted = place.notStarted;
  tile.endedAt = place.endedAt;

  tile.canWhereToWatch = tile.watchable;
  tile.canRemind = state == State::upcoming;
  return tile;
}

}  // namespace

// MARK: Names

std::string shortName(const SportsTeam& team, bool soccer) {
  // A doubles pair: "Bucsa / Melichar-Martinez" as is.
  if (team.name.find(" / ") != std::string::npos) return team.name;
  if (!team.alternates.empty() && text::count(team.alternates.front()) <= 16) return team.alternates.front();
  if (soccer) return team.name;
  std::vector<std::string> words = text::words(team.name);
  if (words.size() < 2) return team.name;
  static const std::set<std::string> kTwoWord = {"red sox",      "white sox",     "blue jays",    "maple leafs",
                                                 "red wings",    "golden knights", "blue jackets", "trail blazers",
                                                 "black hawks",  "st louis",      "golden state"};
  std::string lastTwo = words[words.size() - 2] + " " + words.back();
  if (kTwoWord.count(str::lower(lastTwo))) return lastTwo;
  return words.back();
}

std::string title(const SportsGame& game) {
  // A fight card part: "UFC 333 Prelims".
  if (game.customTitle) return *game.customTitle;
  std::string away = shortName(game.away, game.homeFirst), home = shortName(game.home, game.homeFirst);
  return game.homeFirst ? home + " vs " + away : away + " at " + home;
}

std::string symbol(std::string_view league) { return profile(league).symbol; }

bool isLight(const std::optional<std::string>& hex) {
  if (!hex || hex->size() != 6) return false;
  unsigned value = 0;
  for (char c : *hex) {
    int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
    if (d < 0) return false;
    value = value * 16 + static_cast<unsigned>(d);
  }
  double r = (value >> 16) & 0xFF, g = (value >> 8) & 0xFF, b = value & 0xFF;
  return (0.299 * r + 0.587 * g + 0.114 * b) / 255 > 0.62;
}

bool isStreaming(std::string_view network) {
  static const char* const kServices[] = {
      "espn+",        "disney+",         "peacock",         "paramount+", "prime video",   "amazon",
      "netflix",      "apple tv",        "apple tv+",       "max",        "dazn",          "fubo",
      "youtube",      "nfl+",            "fox one",         "mls season pass", "nba league pass",
      "nhl power play", "mlb.tv",        "victory+",        "flosports",  "espn app",      "hulu"};
  std::string lowered = str::lower(network);
  for (const char* s : kServices) {
    if (lowered == s || str::startsWith(lowered, std::string(s) + " ")) return true;
  }
  return false;
}

std::optional<Whip> whipNamed(std::string_view name) {
  std::string lower = str::lower(name);
  auto has = [&](const char* w) { return lower.find(w) != std::string::npos; };
  if (has("redzone") || has("red zone")) return Whip{"NFL", "NFL", "NFL RedZone", "Every game, every score", 1, true};
  if (has("strike zone")) return Whip{"MLB", "MLB", "MLB Strike Zone", "Every big moment", 2, false};
  if (has("big inning")) return Whip{"MLB", "MLB", "MLB Big Inning", "Every big moment", 2, false};
  if (has("konferenz") || has("goal zone") || has("goal rush") || has("multiplex")) {
    return Whip{"Soccer", "SOC", "Conference feed", "Every match, one feed", 2, false};
  }
  return std::nullopt;
}

std::vector<WhipChannel> whipChannels(const std::vector<Channel>& entries, const std::set<std::string>& lineup) {
  std::vector<WhipChannel> out;
  for (const Channel& entry : entries) {
    auto whip = whipNamed(entry.name);
    if (!whip) continue;
    out.push_back({*whip, entry.id, entry.displayName(), lineup.count(entry.id) > 0});
  }
  std::stable_sort(out.begin(), out.end(), [](const WhipChannel& a, const WhipChannel& b) { return a.inLineup && !b.inLineup; });
  return out;
}

// MARK: Formatting

std::string clockText(Seconds t, const time::Zone& zone, bool twelveHour) {
  time::Civil c = zone.toLocal(t);
  if (!twelveHour) return twoDigits(c.hour) + ":" + twoDigits(c.minute);
  int h12 = c.hour % 12 == 0 ? 12 : c.hour % 12;
  return std::to_string(h12) + ":" + twoDigits(c.minute) + (c.hour < 12 ? " AM" : " PM");
}

std::string shortClockText(Seconds t, const time::Zone& zone, bool twelveHour) {
  time::Civil c = zone.toLocal(t);
  int h = twelveHour ? (c.hour % 12 == 0 ? 12 : c.hour % 12) : c.hour;
  return std::to_string(h) + ":" + twoDigits(c.minute);
}

std::string untilText(Seconds start, Seconds now) {
  // Swift's Int(seconds / 60) truncates toward zero.
  int64_t minutes = (start - now) / 60;
  if (minutes < 0 || minutes >= 6 * 60) return "";
  if (minutes < 60) return "in " + std::to_string(std::max<int64_t>(1, minutes)) + " m";
  return "in " + std::to_string((minutes + 30) / 60) + " h";
}

std::optional<double> clockProgress(const LiveStatus& status, std::string_view league, std::string_view leagueShort) {
  const Profile& p = profile(league, leagueShort);
  if (p.clock == Profile::Clock::None) return std::nullopt;
  const std::string& detail = status.detail;
  if (detail == "Halftime") return 0.5;
  std::vector<std::string> pieces = text::words(detail);
  if (pieces.empty()) return std::nullopt;
  const std::string& head = pieces.front();
  std::string digitText;
  for (char c : head) {
    if (c >= '0' && c <= '9') digitText += c;
  }
  std::optional<int> digits = text::toInt(digitText);
  if (p.clock == Profile::Clock::Minutes) {
    // "67'", "90'+3'".
    if (!digits) return std::nullopt;
    return std::min(1.0, *digits / static_cast<double>(p.clockMinutes));
  }
  if (p.clock == Profile::Clock::Innings) {
    // "Top 6th", "Mid 5th", "Bot 8th", "End 5th".
    if (pieces.size() < 2) return std::nullopt;
    std::string inningText;
    for (char c : pieces[1]) {
      if (c >= '0' && c <= '9') inningText += c;
    }
    auto inning = text::toInt(inningText);
    if (!inning) return std::nullopt;
    std::string h = str::lower(head);
    double half = h == "top" ? 0 : (h == "mid" || h == "bot") ? 0.5 : 1;
    return std::min(1.0, std::max(0.0, (*inning - 1 + half + 0.25) / p.innings));
  }
  if (str::endsWith(head, "OT") || head == "SO") return 1.0;
  if (pieces.size() < 2) return std::nullopt;
  std::vector<std::string> clockParts = text::components(pieces[1], ":");
  if (clockParts.size() != 2) return std::nullopt;
  auto minutes = text::toInt(clockParts[0]);
  auto seconds = text::toInt(clockParts[1]);
  if (!minutes || !seconds) return std::nullopt;
  double remaining = *minutes * 60.0 + *seconds;
  double count = p.periods, length = p.periodSeconds;
  if (str::startsWith(head, "Q")) {
    count = 4;
    length = p.quarterSeconds;
  }
  int period = std::min(std::max(1, digits.value_or(1)), static_cast<int>(count));
  return std::min(1.0, std::max(0.0, (period - 1 + 1 - std::min(1.0, remaining / length)) / count));
}

// MARK: Build

Board build(const Input& in) {
  Board board;
  const Seconds now = in.now;
  board.now = now;
  // Driver-cam feeds belong to their race (its cameras panel), not the rows.
  std::set<std::string> cameras = hiddenCameras(in.events, now, in);
  // Placeholder slots ("No Match Scheduled") aren't events.
  std::vector<SportsEvent> withLive;
  for (const SportsEvent& e : in.events) {
    if (cameras.count(e.id) || isPlaceholder(e)) continue;
    SportsEvent copy = e;
    // Where each game stands, from the live scoreboard.
    if (e.game) {
      auto status = in.statuses.find(e.game->id);
      if (status != in.statuses.end()) {
        copy.live = status->second.phase;
        copy.suspended = status->second.suspended;
      }
    }
    withLive.push_back(std::move(copy));
  }
  std::vector<SportsEvent> all = mergingCards(withLive, now, in.statuses, board.cards);
  std::map<std::string, State> states;
  for (const SportsEvent& e : all) states[e.id] = e.state(now);
  std::map<std::string, WhipInfo> whipInfo = addWhipArounds(all, states, in.statuses, in.whips, now);
  // Studio shows (pregame, countdown, weigh-ins) aren't games: never Live
  // now, at the end of their sport's row.
  for (const SportsEvent& e : all) {
    if (!e.game && !str::startsWith(e.id, "whip|") && matcher::isStudioShow(e.title)) board.studio.insert(e.id);
  }
  auto stateOf = [&](const SportsEvent& e) {
    auto it = states.find(e.id);
    return it == states.end() ? State::unknownTime : it->second;
  };
  // Live now is only what's surely on; "likely over" stays in its sport's row.
  auto isLive = [&](const SportsEvent& e) { return stateOf(e) == State::live && !board.studio.count(e.id); };
  const Seconds horizon = now + 8 * 24 * 3600;
  // A game the scoreboard says is over stays as Final for 30 minutes.
  auto endedAt = [&](const SportsEvent& e) -> std::optional<Seconds> {
    if (e.live != LivePhase::final || !e.game) return std::nullopt;
    auto it = in.finalSeen.find(e.game->id);
    if (it == in.finalSeen.end()) return std::nullopt;
    return it->second;
  };
  auto recentFinal = [&](const SportsEvent& e) {
    auto ended = endedAt(e);
    return ended && now - *ended < 30 * 60;
  };
  auto shown = [&](const SportsEvent& e) {
    State state = stateOf(e);
    bool timely = state == State::live || state == State::mayHaveEnded || state == State::upcoming ||
                  state == State::postponed || recentFinal(e);
    if (!timely || !e.start || *e.start >= horizon) return false;
    // Tennis lists every match on every court: only the ones you can watch.
    if (e.game && e.game->isTennis()) return !e.sources.empty();
    // Cricket too: the listing has every domestic match as well.
    if (e.game && e.game->isCricket()) return !e.sources.empty();
    return !e.sources.empty() || in.settings.showsUnwatchable;
  };

  // Groups within a row: live, likely over, starting soon, later today,
  // then by day; postponed; then games not in your channels.
  struct Bucket {
    int order;
    std::string label;
  };
  const Seconds today = text::startOfDay(now, in.zone);
  auto bucket = [&](const SportsEvent& e) -> Bucket {
    if (str::startsWith(e.id, "whip|")) return {-1, "Whip-around"};
    if (e.sources.empty()) return {90, "Not in your channels"};
    if (board.studio.count(e.id)) return {70, "Shows"};
    switch (stateOf(e)) {
      case State::live: return {0, "Live"};
      case State::finished: return {1, "Final"};
      case State::mayHaveEnded: return {2, "Earlier"};
      case State::postponed: return {80, "Postponed"};
      default: break;
    }
    if (!e.start) return {85, ""};
    if (*e.start - now <= 3600) return {3, "Starting Soon"};
    int days = text::dayDifference(today, *e.start, in.zone);
    if (days <= 0) return {4, "Later Today"};
    if (days == 1) return {5, "Tomorrow"};
    return {5 + days, weekdayName(*e.start, in.zone, true)};
  };
  struct Grouped {
    std::vector<SportsEvent> events;
    std::vector<Bucket> buckets;
  };
  auto grouped = [&](const std::vector<SportsEvent>& list) {
    std::vector<std::pair<Bucket, SportsEvent>> keyed;
    for (const SportsEvent& e : list) keyed.emplace_back(bucket(e), e);
    std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) {
      if (a.first.order != b.first.order) return a.first.order < b.first.order;
      Seconds x = a.second.start.value_or(INT64_MAX), y = b.second.start.value_or(INT64_MAX);
      return x != y ? x < y : a.second.id < b.second.id;
    });
    Grouped g;
    for (auto& [b, e] : keyed) {
      g.buckets.push_back(b);
      g.events.push_back(std::move(e));
    }
    return g;
  };
  auto mixed = [](const std::vector<SportsEvent>& list) {
    if (list.empty()) return false;
    const std::string& first = list.front().leagueShort;
    return std::any_of(list.begin(), list.end(), [&](const SportsEvent& e) { return e.leagueShort != first; });
  };
  struct RowDraft {
    std::string id, title;
    std::vector<SportsEvent> events;
    std::vector<Bucket> buckets;  // empty: not grouped
    int live = 0;
    bool mixed = false;
  };
  std::vector<RowDraft> drafts;
  std::optional<std::set<std::string>> picked;
  std::vector<std::string> pickedOrder;
  if (in.prefs.configured) {
    pickedOrder = pickedGroups(in.prefs);
    picked = std::set<std::string>(pickedOrder.begin(), pickedOrder.end());
  }
  auto liveWatchable = [&](const std::vector<SportsEvent>& list) {
    return static_cast<int>(std::count_if(list.begin(), list.end(),
                                          [&](const SportsEvent& e) { return isLive(e) && !e.sources.empty(); }));
  };

  // My Sports: your teams first.
  if (picked) {
    std::vector<SportsEvent> mine;
    for (const SportsEvent& e : all) {
      if (e.game && isFavorite(in.prefs, *e.game) && shown(e)) mine.push_back(e);
    }
    if (!mine.empty()) {
      Grouped g = grouped(mine);
      drafts.push_back({"teams", "Your teams", g.events, g.buckets, liveWatchable(mine), true});
    }
  }
  std::vector<SportsEvent> live;
  for (const SportsEvent& e : all) {
    if (isLive(e) && !e.sources.empty() && (!picked || picked->count(e.league))) live.push_back(e);
  }
  std::stable_sort(live.begin(), live.end(), [](const SportsEvent& a, const SportsEvent& b) {
    bool wa = str::startsWith(a.id, "whip|"), wb = str::startsWith(b.id, "whip|");
    if (wa != wb) return wa;
    Seconds x = a.start.value_or(INT64_MAX), y = b.start.value_or(INT64_MAX);
    return x != y ? x < y : a.id < b.id;
  });
  if (!live.empty()) {
    drafts.push_back({"live", "Live now", live, {}, static_cast<int>(live.size()), mixed(live)});
  }
  std::map<std::string, std::vector<SportsEvent>> bySport;
  std::vector<SportsEvent> more;
  for (const SportsEvent& e : all) {
    if (!shown(e)) continue;
    std::string group = e.league.empty() ? "Other" : e.league;
    if (picked && !picked->count(group)) {
      // Other sports fold into "More events" (only what you can watch).
      if (!e.sources.empty()) more.push_back(e);
      continue;
    }
    bySport[group].push_back(e);
  }
  std::vector<std::string> sports;
  if (picked) {
    for (const std::string& g : pickedOrder) {
      if (bySport.count(g)) sports.push_back(g);
    }
  } else {
    for (const auto& [g, list] : bySport) sports.push_back(g);
    std::stable_sort(sports.begin(), sports.end(), [](const std::string& a, const std::string& b) {
      int ia = sportRank(a), ib = sportRank(b);
      return ia != ib ? ia < ib : a < b;
    });
  }
  for (const std::string& sport : sports) {
    const std::vector<SportsEvent>& source = bySport[sport];
    Grouped g = grouped(source);
    drafts.push_back({"sport|" + sport, sport, g.events, g.buckets, liveWatchable(source), mixed(source)});
  }
  if (!more.empty()) {
    Grouped g = grouped(more);
    int moreLive = static_cast<int>(std::count_if(more.begin(), more.end(), isLive));
    drafts.push_back({"more", "More events", g.events, g.buckets, moreLive, true});
  }
  std::vector<SportsEvent> untimed;
  for (const SportsEvent& e : all) {
    if (stateOf(e) == State::unknownTime && !e.sources.empty()) untimed.push_back(e);
  }
  if (!untimed.empty()) drafts.push_back({"untimed", "Time not listed", untimed, {}, 0, true});

  // What each shown tile says.
  Context ctx{in, now, {}};
  for (const catalog::EventFamily& f : in.families) ctx.familyNames.emplace(f.id, f.name);
  std::map<std::string, Place> places;
  for (const RowDraft& row : drafts) {
    for (const SportsEvent& e : row.events) {
      if (places.count(e.id)) continue;
      auto card = board.cards.find(e.id);
      auto whip = whipInfo.find(e.id);
      Place place = describe(e, card != board.cards.end() ? &card->second : nullptr, ctx, endedAt(e),
                             whip != whipInfo.end() ? &whip->second : nullptr);
      if (board.studio.count(e.id)) {
        place.studio = true;
        place.tag = std::string("STUDIO");
      }
      places.emplace(e.id, std::move(place));
      board.events.emplace(e.id, e);
    }
  }
  for (RowDraft& draft : drafts) {
    Row row;
    row.id = draft.id;
    row.title = draft.title;
    row.live = draft.live;
    row.mixed = draft.mixed;
    row.grouped = !draft.buckets.empty();
    row.count = static_cast<int>(draft.events.size());
    std::optional<int> lastOrder;
    for (size_t i = 0; i < draft.events.size(); ++i) {
      const SportsEvent& e = draft.events[i];
      int order = row.grouped ? draft.buckets[i].order : 0;
      if (row.groups.empty() || !lastOrder || order != *lastOrder) {
        Group group;
        group.order = order;
        if (row.grouped) group.label = draft.buckets[i].label;
        row.groups.push_back(std::move(group));
        lastOrder = order;
      }
      Tile tile = makeTile(e, places.at(e.id), row.id, row.mixed, stateOf(e), ctx);
      // Select: a card plays the part that's on now.
      State state = tile.state;
      if (state == State::live || state == State::mayHaveEnded || state == State::unknownTime) {
        std::string target = e.id;
        auto card = board.cards.find(e.id);
        if (card != board.cards.end()) {
          const SportsEvent* part = currentPart(card->second, now, in.statuses);
          if (part && !part->sources.empty()) target = part->id;
        }
        bool targetWatchable = target != e.id || !e.sources.empty();
        tile.selectWatches = targetWatchable;
        tile.playID = target;
      } else {
        tile.playID = e.id;
      }
      board.tiles.emplace(e.id, tile);
      row.groups.back().tiles.push_back(std::move(tile));
      row.eventIDs.push_back(e.id);
    }
    board.rows.push_back(std::move(row));
  }
  if (!in.families.empty()) {
    Row row;
    row.id = "families";
    row.title = "Event channels";
    for (const catalog::EventFamily& f : in.families) {
      FamilyTile tile;
      tile.id = f.id;
      tile.key = row.id + "|" + f.id;
      tile.name = f.name;
      tile.countText = std::to_string(f.feeds.size()) + " feeds";
      tile.leagueChip = f.league.empty() ? "" : matcher::shortLeague(f.league);
      tile.hint = "Up/Down to surf";
      row.families.push_back(std::move(tile));
    }
    row.count = static_cast<int>(row.families.size());
    board.rows.push_back(std::move(row));
  }
  for (Row& row : board.rows) {
    row.summary = std::string("\xC2\xB7  ") + std::to_string(row.count);
    if (row.live > 0 && row.id != "live") row.summary += kWideDot + std::to_string(row.live) + " live";
  }
  if (board.rows.empty()) board.emptyMessage = "No upcoming events found in your channels right now.";

  // Race weekends and tennis players you can watch (for later milestones).
  for (const auto& [id, parts] : board.cards) {
    auto e = board.events.find(id);
    if (!parts.empty() && parts.front().game && parts.front().game->leagueShort == "F1" && e != board.events.end() &&
        !e->second.sources.empty()) {
      board.watchableRaceWeekends.push_back(id);
    }
  }


  // The top of the screen shows a game, not a whip-around.
  const SportsEvent* hero = nullptr;
  const SportsEvent* upcoming = nullptr;
  for (const SportsEvent& e : all) {
    if (e.sources.empty() || str::startsWith(e.id, "whip|") || board.studio.count(e.id)) continue;
    if (isLive(e)) {
      hero = &e;
      break;
    }
    if (!upcoming && stateOf(e) == State::upcoming) upcoming = &e;
  }
  if (!hero) hero = upcoming;
  board.hero.clockText = clockText(now, in.zone, in.settings.twelveHour);
  if (hero) {
    board.heroID = hero->id;
    Hero& h = board.hero;
    h.empty = false;
    h.id = hero->id;
    auto placeIt = places.find(hero->id);
    Place place = placeIt != places.end() ? placeIt->second
                                          : describe(*hero, nullptr, ctx, endedAt(*hero), nullptr);
    std::vector<std::string> leagueParts = {hero->leagueShort};
    if (hero->game && hero->game->weekLabel) leagueParts.push_back(*hero->game->weekLabel);
    h.leagueLine = str::join(leagueParts, kWideDot);
    if (hero->game) {
      const SportsGame& g = *hero->game;
      h.badges.push_back(sideLine(g.homeFirst ? g.home : g.away, g.homeFirst, false, std::nullopt, std::nullopt, false, {}, false));
      h.badges.push_back(sideLine(g.homeFirst ? g.away : g.home, g.homeFirst, false, std::nullopt, std::nullopt, false, {}, false));
    } else if (hero->teams && hero->teams->size() == 2) {
      for (const SportsTeam& t : *hero->teams) h.badges.push_back(sideLine(t, true, false, std::nullopt, std::nullopt, false, {}, false));
    }
    // "Indiana 24  –  17 Michigan" with scores; the game's title without.
    if (place.topScore && place.bottomScore && place.top && place.bottom && place.parts.empty()) {
      bool soccer = hero->game ? hero->game->homeFirst : true;
      h.title = shortName(*place.top, soccer) + " " + *place.topScore + "  \xE2\x80\x93  " + *place.bottomScore + " " +
                shortName(*place.bottom, soccer);
    } else {
      h.title = hero->game ? title(*hero->game) : hero->title;
    }
    State state = stateOf(*hero);
    switch (state) {
      case State::live:
        h.chip = hero->live == LivePhase::delayed ? HeroChip::delayed : HeroChip::live;
        h.chipText = hero->live != LivePhase::delayed ? "LIVE" : hero->suspended ? "SUSPENDED" : "DELAYED";
        break;
      case State::upcoming:
        h.chip = HeroChip::later;
        h.chipText = "LATER";
        break;
      case State::finished:
        h.chip = HeroChip::ended;
        h.chipText = place.clock ? str::upper(*place.clock) : "ENDED";
        break;
      case State::postponed:
        h.chip = HeroChip::postponed;
        h.chipText = "POSTPONED";
        break;
      case State::mayHaveEnded:
        h.chip = HeroChip::mayHaveEnded;
        h.chipText = "MAY HAVE ENDED";
        break;
      case State::unknownTime: break;
    }
    std::vector<std::string> status;
    bool done = false;
    if (state == State::live && (place.partLabel || place.clock)) {
      // The hero has room for one status line: clock, then the situation.
      status.push_back(place.partLabel ? *place.partLabel : *place.clock);
      if (place.situation) status.push_back(*place.situation);
      done = true;
    } else if ((state == State::live || state == State::mayHaveEnded) && hero->start) {
      status.push_back("Started " + clockText(*hero->start, in.zone, in.settings.twelveHour));
    } else if (state == State::upcoming && hero->start) {
      int days = text::dayDifference(now, *hero->start, in.zone);
      std::string clock = clockText(*hero->start, in.zone, in.settings.twelveHour);
      status.push_back(days == 0 ? clock : days == 1 ? "Tomorrow " + clock : weekdayName(*hero->start, in.zone, false) + " " + clock);
      int64_t minutes = (*hero->start - now) / 60;
      if (minutes < 180) {
        status.push_back("Starts in " + (minutes >= 60 ? std::to_string(minutes / 60) + " h " + std::to_string(minutes % 60) + " m"
                                                       : std::to_string(std::max<int64_t>(1, minutes)) + " m"));
      }
    }
    if (!done && hero->game && hero->game->venue) status.push_back(*hero->game->venue);
    h.statusText = str::join(status, kWiderDot);
    if (state == State::live) h.progress = place.liveProgress ? *place.liveProgress : hero->progress(now);
    if (place.channel) {
      h.channelDisplay = place.channel->display;
      h.channelLogos = place.channel->logoUrls;
      std::vector<std::string> parts = {place.inLineup ? "On " + place.channel->name + ", in your lineup"
                                                       : "On " + place.channel->name};
      if (place.feeds > 0) parts.push_back(std::to_string(place.feeds) + " backup feeds from your provider");
      h.whereText = str::join(parts, kWiderDot);
    } else if (place.feeds > 0) {
      h.whereText = std::to_string(place.feeds) + " feeds from your provider";
    } else if (hero->game && !hero->game->networks.empty()) {
      h.whereText = "On " + hero->game->networks.front() + kDot + "not in your channels";
    } else {
      h.whereText = "Not in your channels";
    }
  }
  return board;
}

bool containsKey(const Board& board, const std::string& key) {
  for (const Row& row : board.rows) {
    if (!str::startsWith(key, row.id + "|")) continue;
    for (const std::string& id : row.eventIDs) {
      if (key == row.id + "|" + id) return true;
    }
    for (const FamilyTile& f : row.families) {
      if (key == f.key) return true;
    }
  }
  return false;
}

bool matches(const Board& board, const std::string& tileID, const std::string& eventID) {
  if (tileID == eventID) return true;
  auto card = board.cards.find(tileID);
  if (card == board.cards.end()) return false;
  return std::any_of(card->second.begin(), card->second.end(), [&](const SportsEvent& p) { return p.id == eventID; });
}

std::optional<SportsEvent> cardCurrent(const Board& board, const std::string& tileID, const scores::Statuses& statuses,
                                       Seconds now) {
  auto card = board.cards.find(tileID);
  if (card == board.cards.end()) return std::nullopt;
  const SportsEvent* part = currentPart(card->second, now, statuses);
  if (!part) return std::nullopt;
  return *part;
}

// MARK: Sports column

std::vector<ColumnItem> sportsColumn(const Board& board) {
  std::vector<ColumnItem> out;
  for (const Row& row : board.rows) {
    ColumnItem item;
    item.rowID = row.id;
    item.title = row.title;
    if (row.live > 0 && row.id != "live") item.liveText = std::to_string(row.live) + " live";
    item.count = row.count;
    out.push_back(std::move(item));
  }
  return out;
}

// MARK: Event guide

std::vector<const Row*> guideRows(const Board& board) {
  std::vector<const Row*> out;
  for (const Row& row : board.rows) {
    if (row.id != "families" && row.id != "untimed" && !row.eventIDs.empty()) out.push_back(&row);
  }
  return out;
}

std::optional<std::string> guideRowID(const Board& board, const std::optional<std::string>& eventID) {
  std::vector<const Row*> rows = guideRows(board);
  if (eventID) {
    auto has = [&](const Row* row) {
      return std::any_of(row->eventIDs.begin(), row->eventIDs.end(),
                         [&](const std::string& id) { return matches(board, id, *eventID); });
    };
    for (const Row* row : rows) {
      if (str::startsWith(row->id, "sport|") && has(row)) return row->id;
    }
    for (const Row* row : rows) {
      if (has(row)) return row->id;
    }
  }
  if (rows.empty()) return std::nullopt;
  return rows.front()->id;
}

std::vector<std::string> guideEvents(const Board& board, const std::string& rowID) {
  std::vector<std::string> out;
  for (const Row* row : guideRows(board)) {
    if (row->id != rowID) continue;
    for (const std::string& id : row->eventIDs) {
      if (!board.events.at(id).sources.empty()) out.push_back(id);
    }
    break;
  }
  return out;
}

std::vector<std::string> flipEvents(const Board& board, const std::string& eventID, Seconds now) {
  std::optional<std::string> rowID = guideRowID(board, eventID);
  if (!rowID) return {};
  std::vector<std::string> out;
  for (const std::string& id : guideEvents(board, *rowID)) {
    const SportsEvent& e = board.events.at(id);
    State state = e.state(now);
    if (matches(board, id, eventID) ||
        (!board.studio.count(id) && (state == State::live || state == State::mayHaveEnded))) {
      out.push_back(id);
    }
  }
  return out;
}

EventGuide eventGuide(const Board& board, const std::optional<std::string>& rowID,
                      const std::optional<std::string>& watchingEventID, const Settings& settings,
                      const time::Zone& zone) {
  EventGuide guide;
  const Seconds now = board.now;
  guide.rowID = rowID ? *rowID : guideRowID(board, watchingEventID).value_or("");
  const Row* row = nullptr;
  for (const Row* r : guideRows(board)) {
    if (r->id == guide.rowID) row = r;
  }
  guide.heading = std::string("Live Events") + kWideDot + (row ? row->title : "") + kWideDot +
                  clockText(now, zone, settings.twelveHour);
  guide.hint = "\xE2\x96\xB2\xE2\x96\xBC browse games   \xC2\xB7   Select: switch   \xC2\xB7   \xE2\x96\xB6 more ways to watch   "
               "\xC2\xB7   \xE2\x97\x80 sports   \xC2\xB7   Back: close";
  guide.sportsHint = "\xE2\x96\xB2\xE2\x96\xBC sports   \xC2\xB7   \xE2\x96\xB6 or Select: pick   \xC2\xB7   Back: close";
  guide.sourcesHint = "\xE2\x96\xB2\xE2\x96\xBC choose   \xC2\xB7   Select: watch this one   \xC2\xB7   Back: games";
  std::optional<std::string> firstGame;
  if (row) {
    // The row's games you can watch, under its group labels.
    std::optional<std::string> label, shownLabel;
    for (const Group& group : row->groups) {
      for (const Tile& tile : group.tiles) {
        if (&tile == &group.tiles.front() && !group.label.empty()) label = group.label;
        const SportsEvent& e = board.events.at(tile.id);
        if (e.sources.empty()) continue;
        if (label && label != shownLabel) {
          guide.items.push_back({label, std::nullopt});
          shownLabel = label;
        }
        GuideGame game;
        game.id = tile.id;
        if (tile.layout == Tile::Layout::sides && tile.sides.size() == 2) {
          game.sidesLayout = true;
          game.sides = tile.sides;
        } else {
          game.title = tile.title.empty() ? e.title : tile.title;
          if (tile.sides.size() == 2) {
            game.subtitle = tile.sides[0].fullName + "  vs.  " + tile.sides[1].fullName;
          } else {
            game.subtitle = tile.subtitle;
          }
        }
        State state = e.state(now);
        switch (state) {
          case State::live:
            game.pill = e.live == LivePhase::delayed ? Pill::delayed : Pill::live;
            game.pillText = e.live != LivePhase::delayed ? "LIVE" : e.suspended ? "SUSPENDED" : "DELAYED";
            game.statusText = tile.partLabel ? *tile.partLabel : tile.clock.value_or("");
            break;
          case State::mayHaveEnded: game.statusText = "Likely over"; break;
          case State::finished: game.statusText = tile.clock.value_or("Ended"); break;
          case State::upcoming:
            if (e.start) {
              std::string until = untilText(*e.start, now);
              game.statusText = clockText(*e.start, zone, settings.twelveHour) + (until.empty() ? "" : kWideDot + until);
            }
            break;
          case State::postponed: game.statusText = "POSTPONED"; break;
          case State::unknownTime: game.statusText = "Time not listed"; break;
        }
        std::vector<std::string> where;
        if (tile.channelName) {
          where.push_back(*tile.channelName);
        } else if (tile.footerPill) {
          where.push_back(*tile.footerPill);
        }
        if (tile.situation) {
          where.push_back(*tile.situation);
        } else if (!tile.footer.empty()) {
          where.push_back(tile.footer);
        }
        game.whereText = str::join(where, kWideDot);
        game.watching = watchingEventID && matches(board, tile.id, *watchingEventID);
        game.dimmed = state == State::finished;
        game.playID = tile.playID.empty() ? tile.id : tile.playID;
        if (!firstGame) firstGame = tile.id;
        if (game.watching && !guide.focusID) guide.focusID = tile.id;
        guide.items.push_back({std::nullopt, std::move(game)});
      }
    }
  }
  if (!guide.focusID) guide.focusID = firstGame;
  if (guide.items.empty()) guide.emptyMessage = "Nothing else on in your channels right now.";
  for (const Row* r : guideRows(board)) {
    GuideColumnItem item;
    item.rowID = r->id;
    item.title = r->title;
    if (r->live > 0) item.liveText = std::to_string(r->live) + " live";
    for (const std::string& id : r->eventIDs) {
      if (!board.events.at(id).sources.empty()) item.count += 1;
    }
    item.origin = r->id == guide.rowID;
    guide.sports.push_back(std::move(item));
  }
  return guide;
}

std::vector<SourceItem> guideSources(const Board& board, const std::string& tileID, const scores::Statuses& statuses,
                                     const ChannelFor& channelFor) {
  std::vector<SourceItem> out;
  auto e = board.events.find(tileID);
  if (e == board.events.end()) return out;
  std::optional<SportsEvent> current = cardCurrent(board, tileID, statuses, board.now);
  const SportsEvent& basis = current ? *current : e->second;
  for (const EventSource& source : basis.sources) {
    auto channel = channelFor ? channelFor(source) : std::nullopt;
    if (!channel) continue;
    out.push_back({source.channelID, channel->name, source.detail, source.spanish});
  }
  return out;
}

// MARK: Where to Watch and the details card

namespace {

std::string notInChannels(const SportsEvent& event) {
  if (event.game && !event.game->networks.empty()) {
    return "On " + event.game->networks.front() + kDot + "not in your channels";
  }
  return "Not in your channels";
}

std::string leagueLine(const SportsEvent& event) {
  std::vector<std::string> parts = {event.leagueShort};
  if (event.game && event.game->weekLabel) parts.push_back(*event.game->weekLabel);
  return str::join(parts, kWideDot);
}

}  // namespace

WherePanel wherePanel(const Board& board, const SportsEvent& event, const ChannelFor& channelFor,
                      const std::function<std::string(const std::string&)>& feedSummary) {
  WherePanel panel;
  panel.title = event.game ? title(*event.game) : event.title;
  panel.hint = "Whichever you pick, the others stay as automatic backups.";
  auto section = [&](const std::string& heading, const std::vector<EventSource>& sources, const std::string& playID) {
    WhereSection s;
    s.title = heading;
    s.playEventID = playID;
    for (const EventSource& source : sources) {
      auto channel = channelFor ? channelFor(source) : std::nullopt;
      if (!channel) continue;
      std::string detail = source.detail;
      if (source.kind == EventSource::Kind::provider && feedSummary) {
        std::string extra = feedSummary(source.channelID);
        std::vector<std::string> parts;
        if (!detail.empty()) parts.push_back(detail);
        if (!extra.empty()) parts.push_back(extra);
        detail = str::join(parts, kDot);
      }
      s.items.push_back({source.channelID, channel->name, detail, source.spanish});
    }
    if (!s.items.empty()) panel.sections.push_back(std::move(s));
  };
  auto card = board.cards.find(event.id);
  if (card != board.cards.end()) {
    // A fight card: each part's channels and feeds.
    for (const SportsEvent& part : card->second) {
      if (!part.sources.empty()) section(partNames(part).second, part.sources, part.id);
    }
    return panel;
  }
  std::vector<EventSource> english, spanish, feeds;
  for (const EventSource& s : event.channels()) {
    if (!s.spanish) english.push_back(s);
  }
  for (const EventSource& s : event.sources) {
    if (s.spanish) spanish.push_back(s);
  }
  for (const EventSource& s : event.feeds()) {
    if (!s.spanish) feeds.push_back(s);
  }
  if (!english.empty()) section("Your channels", english, event.id);
  if (event.moreStations && *event.moreStations > 0) {
    int more = *event.moreStations;
    panel.moreStations = more == 1 ? std::string("1 more station in your channels is showing it")
                                   : std::to_string(more) + " more stations in your channels are showing it";
  }
  if (!spanish.empty()) section("Spanish", spanish, event.id);
  if (!feeds.empty()) section("Event feeds from your provider", feeds, event.id);
  return panel;
}

DetailCard detailCard(const SportsEvent& event, Seconds now, const time::Zone& zone, const Settings& settings,
                      const ChannelFor& channelFor) {
  DetailCard card;
  card.leagueLine = leagueLine(event);
  if (event.game) {
    const SportsGame& g = *event.game;
    card.badges.push_back(sideLine(g.homeFirst ? g.home : g.away, g.homeFirst, false, std::nullopt, std::nullopt, false, {}, false));
    card.badges.push_back(sideLine(g.homeFirst ? g.away : g.home, g.homeFirst, false, std::nullopt, std::nullopt, false, {}, false));
  }
  card.title = event.game ? title(*event.game) : event.title;
  std::vector<std::string> parts;
  if (event.start) {
    int days = text::dayDifference(now, *event.start, zone);
    std::string day;
    if (days == 0) {
      day = zone.toLocal(*event.start).hour >= 17 ? "Tonight" : "Today";
    } else if (days == 1) {
      day = "Tomorrow";
    } else {
      day = weekdayName(*event.start, zone, true);
    }
    parts.push_back(day + " " + clockText(*event.start, zone, settings.twelveHour));
  }
  if (event.game && event.game->venue) parts.push_back(*event.game->venue);
  std::vector<EventSource> channels = event.channels();
  std::vector<EventSource> feeds = event.feeds();
  std::optional<ChannelLabel> channel =
      !channels.empty() && channelFor ? channelFor(channels.front()) : std::nullopt;
  if (channel) {
    parts.push_back("On " + channel->name);
    card.watchLabel = "Watch " + channel->name + " Now";
    card.watchSourceID = channels.front().channelID;
  } else if (feeds.empty() && event.game && !event.game->networks.empty()) {
    parts.push_back("On " + event.game->networks.front());
  }
  if (!feeds.empty()) parts.push_back(std::to_string(feeds.size()) + " feeds from your provider");
  card.summary = str::join(parts, kWideDot);
  card.whereToWatch = !event.sources.empty();
  card.canRemind = event.state(now) == State::upcoming;
  if (event.sources.empty()) {
    card.notInChannels = notInChannels(event) + ". If one of your channels or event feeds picks it up, it'll show here.";
  }
  return card;
}

}  // namespace marigold::sports::board
