#include "core/sports/special_feeds.hpp"

#include <algorithm>
#include <map>

#include "core/sports/matcher.hpp"
#include "core/sports/text.hpp"

namespace marigold::sports::special_feeds {

namespace {

using Kind = CameraFeed::Kind;

std::string trimmed(std::string_view s) { return std::string(str::trimUnicode(s)); }

// Swift's folding(options: [.diacriticInsensitive, .caseInsensitive]).lowercased().
std::string fold(std::string_view s) { return str::lower(str::foldDiacritics(s)); }

bool has(std::string_view hay, std::string_view needle) { return hay.find(needle) != std::string_view::npos; }

// Byte offset of an ASCII needle, case-insensitive (lowerAscii keeps the
// byte offsets, so the offset is valid in `hay`).
size_t findAscii(std::string_view hay, std::string_view needle) {
  return str::lowerAscii(hay).find(str::lowerAscii(needle));
}

CameraFeed makeFeed(const Channel& channel, std::string title, std::string detail, Kind kind,
                    std::optional<std::string> team = std::nullopt) {
  CameraFeed feed;
  feed.channelID = channel.id;
  feed.title = std::move(title);
  feed.detail = std::move(detail);
  feed.kind = kind;
  feed.team = std::move(team);
  return feed;
}

const F1Team* teamNamed(std::string_view name) {
  std::string wanted = str::lower(name);
  for (const F1Team& team : f1Teams()) {
    if (str::lower(team.name) == wanted) return &team;
  }
  return nullptr;
}

struct PipeDriver {
  std::string driver;
  std::optional<std::string> team;
};

// "F1: DRIVER ALPHA | TEAM ALPHA | DRA" -> ("Driver Alpha", the team).
std::optional<PipeDriver> pipeDriver(std::string_view name) {
  if (!has(name, " | ")) return std::nullopt;
  std::string value = trimmed(name);
  if (str::startsWith(str::upper(value), "F1: ")) value = text::dropPrefix(value, 4);
  std::vector<std::string> parts = text::components(value, " | ");
  for (std::string& part : parts) part = trimmed(part);
  if (parts.size() != 3 || text::count(parts[2]) != 3 || !text::isAllLetters(parts[2]) ||
      parts[2] != str::upper(parts[2]) || !has(parts[0], " ")) {
    return std::nullopt;
  }
  std::string driver = parts[0] == str::upper(parts[0]) ? catalog::text::capitalized(parts[0]) : parts[0];
  // Only an F1 team ("Channel One | UK | ENG" isn't a driver).
  const F1Team* team = teamNamed(parts[1]);
  if (!team) return std::nullopt;
  return PipeDriver{driver, std::string(team->name)};
}

// One F1 camera channel as a camera, in any of the naming sets.
std::optional<CameraFeed> f1Camera(const Channel& channel) {
  if (auto pipe = pipeDriver(channel.name)) {
    return makeFeed(channel, pipe->driver, pipe->team.value_or("Onboard"), Kind::driver, pipe->team);
  }
  std::string label;
  size_t close = channel.name.find(')');
  if (close != std::string::npos) {
    label = trimmed(std::string_view(channel.name).substr(close + 1));
  } else {
    // "F1 DATA", "F1: F1-TRACKER".
    label = trimmed(str::replaceAll(str::replaceAll(str::replaceAll(channel.name, "F1:", ""), "F1-", ""), "F1 ", ""));
  }
  if (label.empty()) return std::nullopt;
  std::string lower = str::lower(label);
  if (has(lower, "mixed")) return makeFeed(channel, "Mixed onboard", "Switches cars", Kind::mixed);
  if (has(lower, "tracker")) return makeFeed(channel, "Driver Tracker", "Bird's-eye map", Kind::tracker);
  if (has(lower, "data")) return makeFeed(channel, "Driver Data", "Live telemetry", Kind::data);
  if (auto podium = rx::first("^p[1-3]\\b", lower)) {
    std::string place = str::upper(podium->group(0));
    return makeFeed(channel, place + " driver", "Follows " + place, Kind::podium);
  }
  const F1Team* team = nullptr;
  for (const F1Team& t : f1Teams()) {
    if (str::startsWith(lower, str::lower(t.name) + " ")) {
      team = &t;
      break;
    }
  }
  std::string driver = team ? trimmed(text::dropPrefix(label, text::count(team->name))) : label;
  return makeFeed(channel, driver, team ? team->name : "Onboard", Kind::driver,
                  team ? std::optional<std::string>(team->name) : std::nullopt);
}

// The race's own feeds: the first (or "main broadcast") is the race feed, a
// Sky Sports feed its commentary, the rest numbered.
std::vector<CameraFeed> ownFeeds(const std::vector<Channel>& eventFeeds) {
  std::vector<CameraFeed> result;
  for (size_t index = 0; index < eventFeeds.size(); ++index) {
    const Channel& channel = eventFeeds[index];
    std::string lower = str::lower(channel.name);
    if (has(lower, "sky sports")) {
      result.push_back(makeFeed(channel, "Sky Sports", "Sky commentary", Kind::alt));
    } else if (has(lower, "main broadcast") || index == 0) {
      result.push_back(makeFeed(channel, "Main broadcast", "The race feed", Kind::main));
    } else {
      result.push_back(makeFeed(channel, "Feed " + std::to_string(index + 1), "Another feed", Kind::alt));
    }
  }
  return result;
}

// One entry per camera (the same driver on several feeds keeps the others as
// backups), in kind order.
std::vector<CameraFeed> sorted(const std::vector<CameraFeed>& feeds, bool f1) {
  std::vector<CameraFeed> merged;
  std::map<std::string, size_t> index;
  for (const CameraFeed& feed : feeds) {
    // F1 drivers by surname (one per surname on the grid): "Driver Alpha
    // Jr." and "Driver Alpha", "Kimi Beta" and "Andrea Kimi Beta" are one
    // camera.
    std::string name = fold(feed.title);
    if (f1 && feed.kind == Kind::driver) {
      name = str::replaceAll(str::replaceAll(name, " jr.", ""), " jr", "");
      std::vector<std::string> words = text::words(name);
      if (!words.empty()) name = words.back();
    }
    std::string key = std::to_string(static_cast<int>(feed.kind)) + "|" + name;
    auto at = index.find(key);
    if (at != index.end()) {
      merged[at->second].alternates.push_back(feed.channelID);
    } else {
      index[key] = merged.size();
      merged.push_back(feed);
    }
  }
  std::stable_sort(merged.begin(), merged.end(), [](const CameraFeed& a, const CameraFeed& b) {
    return static_cast<int>(a.kind) < static_cast<int>(b.kind);
  });
  return merged;
}

std::vector<CameraFeed> f1Cameras(const std::vector<Channel>& eventFeeds, const std::vector<Channel>& candidates) {
  // The provider family of the race feed first ("(Apple) (F1) ..." ->
  // "(Apple F1 CAM) ..."); every other set of the same cameras (F1 TV cams,
  // "Driver | Team | CODE" channels) joins as automatic backups.
  std::vector<const Channel*> apple, f1tv, others;
  for (const Channel& channel : candidates) {
    std::string lower = str::lower(channel.name);
    bool isApple = has(lower, "apple f1 cam");
    bool isF1tv = has(lower, "f1 tv cam");
    if (isApple) apple.push_back(&channel);
    if (isF1tv) f1tv.push_back(&channel);
    if (!isApple && !isF1tv && isF1CameraName(channel.name)) others.push_back(&channel);
  }
  bool prefersApple = std::any_of(eventFeeds.begin(), eventFeeds.end(),
                                  [](const Channel& c) { return has(str::lower(c.name), "apple"); });
  std::vector<const Channel*> order = prefersApple ? apple : f1tv;
  const std::vector<const Channel*>& second = prefersApple ? f1tv : apple;
  order.insert(order.end(), second.begin(), second.end());
  order.insert(order.end(), others.begin(), others.end());
  std::vector<CameraFeed> result = ownFeeds(eventFeeds);
  for (const Channel* channel : order) {
    if (auto feed = f1Camera(*channel)) result.push_back(std::move(*feed));
  }
  return sorted(result, true);
}

const std::set<std::string>& stopWords() {
  static const std::set<std::string> kWords = {
      "nascar",  "series", "motor", "sports",    "racing",   "race",    "cup",     "xfinity",
      "truck",   "trucks", "driver", "drivers",  "multiview", "featured", "chase",  "leaders",
      "live",    "practice", "qualifying", "round", "stage",  "formula"};
  return kWords;
}

// Distinctive words of a title ("Race 500 at Speedway Alpha" -> speedway, alpha... of 5+ letters).
std::set<std::string> distinctiveWords(std::string_view title) {
  std::set<std::string> out;
  std::string lower = str::lower(title);
  for (const rx::Match& m : rx::all("\\p{L}+", lower)) {
    std::string word(m.group(0));
    if (text::count(word) >= 5 && !stopWords().count(word)) out.insert(word);
  }
  return out;
}

bool intersects(const std::set<std::string>& a, const std::set<std::string>& b) {
  for (const std::string& w : a) {
    if (b.count(w)) return true;
  }
  return false;
}

std::vector<CameraFeed> nascarCameras(const SportsEvent& event, const std::vector<Channel>& eventFeeds,
                                      const std::vector<Channel>& candidates) {
  std::set<std::string> key = distinctiveWords(event.title);
  if (key.empty()) return {};
  std::vector<CameraFeed> result = ownFeeds(eventFeeds);
  std::set<std::string> seen;
  for (const Channel& c : eventFeeds) seen.insert(c.id);
  for (const Channel& channel : candidates) {
    if (!seen.insert(channel.id).second) continue;
    std::string title = text::components(channel.name, "|").back();
    std::string lower = str::lower(title);
    // Its own name, or a NASCAR feed that names no race ("NASCAR: No 5 Driver Alpha").
    std::optional<std::string> race = cameraRace(title);
    if (!intersects(distinctiveWords(title), key) && !genericNascarRaces().count(race.value_or(""))) continue;
    if (auto car = carCamera(title)) {
      // "Race 500: No. 5 Driver Alpha" (in-car feeds by car).
      CameraFeed feed = makeFeed(channel, car->driver, "No. " + std::to_string(car->number), Kind::driver);
      feed.number = car->number;
      result.push_back(std::move(feed));
    } else if (has(lower, "multiview")) {
      // "Race 500: NASCAR Chase Leaders Multiview (10.25 1:40PM ET)" -> "Chase Leaders Multiview".
      std::string bare = text::components(title, "(").front();
      std::string label = trimmed(text::components(bare, ":").back());
      if (str::startsWith(str::lower(label), "nascar ")) label = text::dropPrefix(label, 7);
      result.push_back(makeFeed(channel, label, "Several cars at once", Kind::multiview));
    } else {
      size_t cam = findAscii(title, "driver cam");
      if (cam == std::string::npos) continue;
      // "Motor Sports: Driver Alpha Speedway Driver Cam (...)" -> "Driver Alpha".
      std::string before = title.substr(0, cam);
      std::string tail = text::components(before, ":").back();
      std::string last = text::components(tail, "\xE2\x80\xA2").back();  // "•"
      std::vector<std::string> kept;
      for (const std::string& word : text::words(last)) {
        if (!key.count(str::lower(word))) kept.push_back(word);
      }
      std::string name = trimmed(str::join(kept, " "));
      result.push_back(makeFeed(channel, name.empty() ? "Driver cam" : name, "In-car camera", Kind::driver));
    }
  }
  // Drivers by car number.
  std::vector<CameraFeed> drivers, rest;
  for (const CameraFeed& feed : result) (feed.kind == Kind::driver ? drivers : rest).push_back(feed);
  std::stable_sort(drivers.begin(), drivers.end(), [](const CameraFeed& a, const CameraFeed& b) {
    return a.number.value_or(999) < b.number.value_or(999);
  });
  rest.insert(rest.end(), drivers.begin(), drivers.end());
  return sorted(rest, false);
}

}  // namespace

// MARK: Names

bool isCameraName(std::string_view name) {
  std::string lower = str::lower(name);
  return has(lower, "f1 cam") || has(lower, "f1 tv cam") || has(lower, "driver cam") ||
         (has(lower, "multiview") && (has(lower, "nascar") || has(lower, "driver"))) || carCamera(lower) ||
         isF1CameraName(name);
}

std::optional<CarCamera> carCamera(std::string_view title) {
  // Not a ranked game ("No. 5 Team Alpha vs No. 8 Team Beta").
  if (findAscii(title, ": no") == std::string::npos || matcher::looksLikeGame(title)) return std::nullopt;
  auto match = rx::first(": ?No\\.? ?[0-9]{1,3} ", title, true);
  if (!match) return std::nullopt;
  std::string digits;
  for (char c : match->group(0)) {
    if (c >= '0' && c <= '9') digits += c;
  }
  int number = text::toInt(digits).value_or(0);
  std::string_view rest = title.substr(match->end);
  std::string name = trimmed(text::components(rest, "(").front());
  // A person's name: two to four words, no digits or dashes, not another
  // ranking ("No. 5 Team Alpha - No. 8 Team Beta").
  size_t words = text::words(name).size();
  if (number <= 0 || words < 2 || words > 4 || text::hasDigit(name) || has(name, " - ") ||
      findAscii(name, "no.") != std::string::npos) {
    return std::nullopt;
  }
  // Not a team sport's show ("College GameDay: No. 3 Team Alpha").
  std::string head = str::lower(title.substr(0, match->begin));
  static const char* const kTeamWords[] = {"football", "basketball", "volleyball", "soccer",   "tennis",
                                           "golf",     "court",      "gameday",    "network",  "baseball",
                                           "hockey",   "softball",   "wimbledon"};
  for (const char* word : kTeamWords) {
    if (has(head, word)) return std::nullopt;
  }
  return CarCamera{number, name};
}

const std::set<std::string>& genericNascarRaces() {
  static const std::set<std::string> kRaces = {"nascar", "nascar cup", "nascar cup series"};
  return kRaces;
}

std::optional<std::string> cameraRace(std::string_view title) {
  std::string lower = str::lower(title);
  if (carCamera(title)) {
    size_t cut = lower.find(": no");
    if (cut != std::string::npos) {
      std::string race = trimmed(std::string_view(lower).substr(0, cut));
      return text::count(race) >= 4 ? std::optional<std::string>(race) : std::nullopt;
    }
  }
  // The time in brackets ("(10.25 1:40PM ET)") isn't part of the name.
  std::string base = text::components(lower, "(").front();
  if (has(base, "multiview")) {
    size_t cut = base.rfind(':');
    if (cut != std::string::npos) {
      std::string race = trimmed(std::string_view(base).substr(0, cut));
      return text::count(race) >= 4 ? std::optional<std::string>(race) : std::nullopt;
    }
  }
  return std::nullopt;
}

bool isF1CameraName(std::string_view name) {
  std::string lower = trimmed(str::lower(name));
  if (lower == "f1 data" || lower == "f1 tracker" || lower == "f1: f1-data" || lower == "f1: f1-tracker") return true;
  return pipeDriver(name).has_value();
}

// MARK: Teams and series

const std::vector<F1Team>& f1Teams() {
  static const std::vector<F1Team> kTeams = {
      {"Haas F1 Team", "B6BABD"}, {"Red Bull Racing", "1E41FF"}, {"Racing Bulls", "6692FF"},
      {"Aston Martin", "00665E"}, {"Kick Sauber", "52E252"},     {"McLaren", "FF8000"},
      {"Ferrari", "DC0000"},      {"Mercedes", "00D2BE"},        {"Williams", "00A0DE"},
      {"Alpine", "FF87BC"},       {"Audi", "E00000"},            {"Cadillac", "C9A227"},
      {"Sauber", "52E252"},       {"Haas", "B6BABD"}};
  return kTeams;
}

std::optional<std::string> teamColor(const std::optional<std::string>& team) {
  if (!team) return std::nullopt;
  for (const F1Team& t : f1Teams()) {
    if (*team == t.name) return std::string(t.color);
  }
  return std::nullopt;
}

bool isF1(const SportsEvent& event) {
  if (event.leagueShort == "NASCAR" || (event.game && event.game->leagueShort == "NASCAR")) return false;
  if (event.leagueShort == "F1") return true;
  std::string value = str::lower(event.title + " " + event.league + " " + event.family.value_or(""));
  return rx::test("\\b(formula ?1|formula one|f1)\\b", value);
}

bool isNASCAR(const SportsEvent& event) {
  if (event.leagueShort == "NASCAR" || (event.game && event.game->leagueShort == "NASCAR")) return true;
  std::string value = str::lower(event.title + " " + event.league);
  if (has(value, "nascar") || has(value, "cup series") || has(value, "xfinity") || has(value, "truck series")) {
    return true;
  }
  // A race named only by its sponsor ("Race 500", league Motorsport or
  // unknown): its cameras still have to share its name.
  return (event.league == "Motorsport" || event.league == "Other" || event.league.empty()) && !event.game &&
         !isF1(event);
}

// MARK: Cameras

std::vector<CameraFeed> cameras(const SportsEvent& event, const std::vector<Channel>& eventFeeds,
                                const std::vector<Channel>& candidates) {
  std::vector<CameraFeed> feeds;
  if (isF1(event)) {
    feeds = f1Cameras(eventFeeds, candidates);
  } else if (isNASCAR(event)) {
    feeds = nascarCameras(event, eventFeeds, candidates);
  }
  // Only the race's own feeds isn't a cameras panel.
  bool any = std::any_of(feeds.begin(), feeds.end(),
                         [](const CameraFeed& f) { return f.kind != Kind::main && f.kind != Kind::alt; });
  return any ? feeds : std::vector<CameraFeed>{};
}

std::vector<Channel> cameraCandidates(const std::vector<Channel>& channels) {
  std::vector<Channel> out;
  for (const Channel& channel : channels) {
    if (isCameraName(channel.name)) out.push_back(channel);
  }
  return out;
}

// MARK: F1 channels

std::vector<EventSource> f1ChannelSources(const std::vector<Channel>& entries,
                                          const std::unordered_map<std::string, catalog::EntryInfo>& entryInfo,
                                          const std::set<std::string>& hiddenGroups, const std::string& otherGroup,
                                          const std::set<std::string>& lineup) {
  std::vector<EventSource> mine, english, others;
  for (const Channel& entry : entries) {
    const std::string& name = entry.name;
    if (hiddenGroups.count(entry.group.value_or(otherGroup))) continue;
    auto info = entryInfo.find(entry.id);
    if (info == entryInfo.end() || info->second.kind != catalog::ChannelKind::channel) continue;
    if (!str::icontains(name, "F1") && !str::icontains(name, "formula")) continue;
    std::string lower = str::lower(name);
    bool dedicated = has(lower, "sky sport f1") || has(lower, "sky sports f1") || has(lower, "dazn f1") ||
                     has(lower, "canal+ f1") || has(lower, "canal+ formula") ||
                     (has(lower, "viaplay") && has(lower, "f1")) || has(lower, "f1 tv main") ||
                     (has(lower, "ziggo") && has(lower, "f1")) || lower == "f1 tv" || lower == "f1: f1-tv";
    if (!dedicated || isCameraName(name) || has(lower, "tracker") || has(lower, "data") || has(lower, "pit lane")) {
      continue;
    }
    bool inLineup = lineup.count(entry.id) > 0;
    bool isEnglish = has(lower, "sky sports f1") || has(lower, "english") || has(lower, "f1 tv main") ||
                     lower == "f1 tv" || lower == "f1: f1-tv" ||
                     (has(lower, "sky sport f1") && (str::startsWith(lower, "uk") || str::startsWith(lower, "vip") ||
                                                     str::startsWith(lower, "now") || str::startsWith(lower, "4k")));
    bool spanish = has(lower, "dazn f1") || has(lower, "spanish") || str::startsWith(lower, "(es)") ||
                   str::startsWith(lower, "es:");
    // "Likely, not certain" (like a team's own channel): tried after the
    // session's own feeds, never folded away, and it doesn't stop the feeds'
    // "moved on" signal from ending the session.
    EventSource source;
    source.channelID = entry.id;
    source.kind = EventSource::Kind::team;
    source.spanish = spanish;
    source.detail = kF1ChannelDetail;
    if (inLineup) {
      mine.push_back(std::move(source));
    } else if (isEnglish) {
      english.push_back(std::move(source));
    } else {
      others.push_back(std::move(source));
    }
  }
  // Yours first, then a few English and a few others (one per language is plenty).
  std::vector<EventSource> sources = std::move(mine);
  auto firstFew = [&sources](const std::vector<EventSource>& list, size_t n) {
    for (size_t i = 0; i < list.size() && i < n; ++i) sources.push_back(list[i]);
  };
  firstFew(english, 3);
  firstFew(others, 4);
  return sources;
}

std::string f1ChannelsKey(int entriesRevision, size_t entryCount, const std::set<std::string>& lineup,
                          const std::set<std::string>& hiddenGroups) {
  std::string joined;
  for (const std::string& id : lineup) joined += id + "\n";
  joined += "\x1f";
  for (const std::string& group : hiddenGroups) joined += group + "\n";
  return std::to_string(entriesRevision) + "|" + std::to_string(entryCount) + "|" + str::hex64(str::fnv1a64(joined));
}

bool wantsF1Sources(const std::vector<SportsGame>& games) {
  return std::any_of(games.begin(), games.end(),
                     [](const SportsGame& g) { return g.leagueShort == "F1" && !g.completed; });
}

}  // namespace marigold::sports::special_feeds
