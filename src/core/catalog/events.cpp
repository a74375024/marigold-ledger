// Event titles: start times in the many formats IPTV providers write, the
// deduplication key, and the league a title belongs to.
#include <algorithm>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>

#include "core/catalog/catalog.hpp"
#include "core/catalog/text.hpp"
#include "core/util/rx.hpp"
#include "core/util/strings.hpp"

namespace marigold::catalog {

const char* const kProWrestling = "Pro Wrestling";
const char* const kProWrestlingWords =
    "\\b(wwe|aew|nxt|tna|roh)\\b|ring of honor|impact wrestling|smackdown|monday night raw|\\bwwe raw\\b|"
    "aew (dynamite|collision|rampage)|lucha libre|\\bnjpw\\b|new japan pro";

const char* const kFightWords =
    "\\b(ufc|bkfc|pfl)(\\b|_|\\d)|\\b(mma|bellator|wwe|aew|kickboxing|muay "
    "thai)\\b|\\bboxing\\b(?! "
    "day)|fight night|one fight|combat fc|fight pass|fightpass";

namespace {

using time::Seconds;
using time::Zone;

int monthOf(std::string_view abbreviation) {
  static const std::unordered_map<std::string, int> kMonths = {
      {"jan", 1}, {"feb", 2}, {"mar", 3}, {"apr", 4},  {"may", 5},  {"jun", 6},
      {"jul", 7}, {"aug", 8}, {"sep", 9}, {"oct", 10}, {"nov", 11}, {"dec", 12}};
  auto it = kMonths.find(str::lower(abbreviation));
  return it == kMonths.end() ? 0 : it->second;
}

// Calendar.date(from:) in `zone`. Foundation is lenient: month 13 is next
// January, day 32 rolls into the next month, hour 25 into the next day. The
// civil arithmetic below is linear in day / hour / minute, and months are
// carried by hand.
Seconds dateIn(int y, int mo, int d, int h, int mi, const Zone& zone) {
  int m0 = mo - 1;
  y += m0 >= 0 ? m0 / 12 : -((-m0 + 11) / 12);
  m0 = ((m0 % 12) + 12) % 12;
  time::Civil first;
  first.year = y;
  first.month = m0 + 1;
  // The wall-clock reading, then that reading in the zone.
  Seconds wall = time::secondsFromCivil(first) + static_cast<Seconds>(d - 1) * 86400 +
                 static_cast<Seconds>(h) * 3600 + static_cast<Seconds>(mi) * 60;
  return zone.fromLocal(time::civilFromSeconds(wall));
}

int hour24(int h, std::string_view ampm) {
  bool pm = str::lower(ampm) == "pm";
  return h % 12 + (pm ? 12 : 0);
}

struct Found {
  std::vector<std::string> p;  // groups 1..n, "" when unset
  size_t begin = 0, end = 0;
};

std::optional<Found> groups(std::string_view pattern, const std::string& title) {
  auto m = rx::first(pattern, title, true);
  if (!m) return std::nullopt;
  Found f;
  for (size_t i = 1; i < m->groups.size(); ++i) f.p.emplace_back(m->group(i));
  f.begin = m->begin;
  f.end = m->end;
  return f;
}

void removeRange(std::string& title, const Found& f) { title.erase(f.begin, f.end - f.begin); }

std::optional<int> num(const std::string& s) { return text::toInt(s); }

// The country in a slot's service tag (":Viaplay DK  03", ":Tv4 Play SE  06",
// ":MAX ES  01"; ":VG  01" is Norway's VG): the zone its times are written
// in, AM/PM ones too.
std::optional<Zone> serviceZone(std::string_view service) {
  std::string_view s = str::trimUnicode(service);
  size_t space = s.find_last_of(' ');
  std::string_view last = space == std::string_view::npos ? s : s.substr(space + 1);
  if (last.size() == 2 && last[0] >= 'A' && last[0] <= 'Z' && last[1] >= 'A' && last[1] <= 'Z') {
    if (last == "US") return Zone::usEastern();
    if (auto zone = eventZone(std::string(last))) return zone;
  }
  std::string lower = str::lowerAscii(s);
  if (lower == "vg" || lower == "vgtv") return Zone::named("Europe/Oslo");
  // Build 141: the tennis service (":Tennis  13", "Fils, Arthur vs Fritz,
  // Taylor @ Nov 5 19:00 PM - ATP Paris") writes Central European time,
  // whatever the tournament: its Paris night sessions at 19:00 and 20:30
  // are the schedule's 18:00 and 19:30 UTC, and its Chennai and Hong Kong
  // matches are an hour ahead of their UTC starts too. Read as Eastern
  // they were six hours late (a doubles watched at 6-4 4-4 at 8:36 AM
  // Pacific was listed for 12:55 PM).
  if (lower == "tennis") return Zone::named("Europe/Paris");
  return std::nullopt;
}

bool asciiLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

// Words that make a side of "A - B" a stage, session or programme rather
// than a team or player ("Paris Masters - Court Central", "Formula 1 -
// Free Practice 1").
bool stageSide(const std::string& lowerSide) {
  static const std::unordered_set<std::string> kWords = {
      "round",    "day",        "final",     "finals",     "semifinal",     "semi",       "quarter",
      "session",  "qualifying", "practice",  "race",       "sprint",        "stage",      "heat",
      "heats",    "highlights", "replay",    "preview",    "studio",        "show",       "court",
      "group",    "night",      "game",      "match",      "week",          "leg",        "part",
      "episode",  "season",     "championship", "championships", "tournament", "camp",    "draft",
      "awards",   "ceremony",   "gala",      "weigh",      "press",         "conference", "magazine",
      "news",     "talk",       "live",      "open",       "masters",       "grand",      "prix",
      "gp",       "cup",        "trophy",    "rally",      "classic",       "series",     "tour",
      "world",    "olympic",    "olympics",  "games",      "event",         "special",    "program",
      "programme", "studie",    "timing",    "onboard",    "pitlane",       "kvalifikation", "debrief",
      "analysis", "highlight",  "recap",     "review",     "interview",     "documentary", "film"};
  for (std::string_view w : str::split(lowerSide, ' ')) {
    if (kWords.count(std::string(w))) return true;
  }
  return false;
}

// Competitions written into the matchup ("LIGUE 2 REIMS - SOCHAUX",
// "MJÄLLBY - KALMAR ALLSVENSKAN"), lower case without accents; a longer
// name before one it starts with.
const std::vector<std::string>& gluedCompetitions() {
  static const std::vector<std::string> kNames = {
      "uefa champions league", "uefa europa league", "uefa conference league", "champions league",
      "europa league",         "conference league",  "dfb-pokal frauen",       "dfb pokal frauen",
      "dfb-pokal",             "dfb pokal",          "2. bundesliga",          "bundesliga",
      "ligue 1",               "ligue 2",            "serie a",                "serie b",
      "laliga hypermotion",    "laliga",             "la liga",                "premier league",
      "allsvenskan",           "superettan",         "eliteserien",            "veikkausliiga",
      "superligaen",           "eredivisie",         "liga portugal",          "primeira liga",
      "ekstraklasa",           "coppa italia",       "copa del rey",           "coupe de france",
      "scottish premiership",  "liga mx",            "nations league"};
  return kNames;
}

// "LIGUE 2 REIMS - SOCHAUX" -> "REIMS - SOCHAUX | LIGUE 2": a competition
// glued to the start or end of the matchup goes after it, the way the
// provider's other slots write it.
std::string unglueCompetition(const std::string& segment) {
  // Byte positions of the folded text must be the original's (ASCII).
  if (std::any_of(segment.begin(), segment.end(), [](char c) { return static_cast<unsigned char>(c) >= 0x80; })) {
    std::string folded = str::foldDiacritics(segment);
    if (folded.size() != segment.size()) {
      // Accents before the competition only shift its start: look at the end.
      std::string lower = str::lower(folded);
      for (const std::string& name : gluedCompetitions()) {
        if (lower.size() > name.size() + 6 && lower.compare(lower.size() - name.size(), name.size(), name) == 0 &&
            lower[lower.size() - name.size() - 1] == ' ') {
          std::string rest(str::trimUnicode(std::string_view(segment).substr(0, segment.size() - name.size())));
          return rest + " | " + segment.substr(segment.size() - name.size());
        }
      }
      return segment;
    }
  }
  std::string lower = str::lowerAscii(segment);
  for (const std::string& name : gluedCompetitions()) {
    if (lower.size() <= name.size() + 6) continue;
    if (lower.compare(0, name.size(), name) == 0 && lower[name.size()] == ' ') {
      std::string rest(str::trimUnicode(std::string_view(segment).substr(name.size())));
      return rest + " | " + segment.substr(0, name.size());
    }
    if (lower.compare(lower.size() - name.size(), name.size(), name) == 0 &&
        lower[lower.size() - name.size() - 1] == ' ') {
      std::string rest(str::trimUnicode(std::string_view(segment).substr(0, segment.size() - name.size())));
      return rest + " | " + segment.substr(segment.size() - name.size());
    }
  }
  return segment;
}

// "Sinner, Jannik" -> "Jannik Sinner". A doubles pair ("Peric I,
// Stojiljkovic P") or anything else stays as it is.
std::string reversedName(const std::string& side) {
  size_t comma = side.find(',');
  if (comma == std::string::npos || side.find(',', comma + 1) != std::string::npos) return side;
  std::string surname(str::trimUnicode(std::string_view(side).substr(0, comma)));
  std::string first(str::trimUnicode(std::string_view(side).substr(comma + 1)));
  auto namesOnly = [](const std::string& part, size_t most) {
    // Particles of a surname ("Van De Zandschulp", "Da Silva").
    static const std::unordered_set<std::string> kParticles = {"de", "da", "di", "du", "la", "le",
                                                                "van", "von", "der", "den", "dos", "del"};
    size_t words = 0;
    for (std::string_view w : str::split(part, ' ')) {
      if (w.empty()) continue;
      ++words;
      if (kParticles.count(str::lowerAscii(w))) continue;
      // An initial ("I", "J.") makes it a short name, not a first name.
      if (w.size() <= 2 || !(asciiLetter(w[0]) || static_cast<unsigned char>(w[0]) >= 0x80)) return false;
    }
    return words >= 1 && words <= most;
  };
  if (!namesOnly(surname, 4) || !namesOnly(first, 2)) return side;
  return first + " " + surname;
}

// The matchup leading a slot's title as a plain "A vs B": "A - B" (how
// several providers write a match: "MJÄLLBY - KALMAR", "SINNER - ALCARAZ")
// and "Surname, First vs Surname, First"; a competition glued to it goes
// after it.
std::string tidyMatchup(const std::string& title) {
  size_t pipe = title.find(" | ");
  std::string segment = title.substr(0, pipe);
  std::string rest = pipe == std::string::npos ? std::string() : title.substr(pipe);
  // "Network: A - B" already reads as a game; "Serie A: Harbor-Valley" (a
  // league's name, then two clubs joined by a hyphen) is made one.
  if (segment.find(':') != std::string::npos) {
    size_t colon = segment.find(": ");
    if (colon == std::string::npos) return title;
    std::string league = str::lowerAscii(segment.substr(0, colon));
    std::string body = segment.substr(colon + 2);
    size_t hyphen = body.find('-');
    static const char* const kLeagueWords[] = {"liga", "league", "serie", "ligue", "bundesliga", "pokal",
                                               "allsvenskan", "eliteserien", "eredivisie", "cup"};
    bool leagueName = std::any_of(std::begin(kLeagueWords), std::end(kLeagueWords),
                                  [&](const char* w) { return league.find(w) != std::string::npos; });
    if (leagueName && hyphen != std::string::npos && hyphen >= 4 && body.size() - hyphen > 4 &&
        body.find('-', hyphen + 1) == std::string::npos && body.find(" vs") == std::string::npos &&
        body[hyphen - 1] != ' ' && body[hyphen + 1] != ' ' && body[hyphen + 1] >= 'A' && body[hyphen + 1] <= 'Z') {
      return segment.substr(0, colon + 2) + body.substr(0, hyphen) + " vs " + body.substr(hyphen + 1) + rest;
    }
    return title;
  }
  bool dashed = segment.find(" - ") != std::string::npos || segment.find(" x ") != std::string::npos;
  bool comma = segment.find(',') != std::string::npos;
  if (!dashed && !comma && segment.find(" v") == std::string::npos) return title;
  segment = unglueCompetition(segment);
  size_t gluedPipe = segment.find(" | ");
  if (gluedPipe != std::string::npos) {
    rest = segment.substr(gluedPipe) + rest;
    segment = segment.substr(0, gluedPipe);
  }
  std::string lower = " " + str::lower(segment) + " ";
  bool hasVs = lower.find(" vs ") != std::string::npos || lower.find(" vs. ") != std::string::npos ||
               lower.find(" v ") != std::string::npos || lower.find(" @ ") != std::string::npos;
  // "Wolfsburg x Hoffenheim" (Portuguese) the same way.
  const char* sepText = segment.find(" - ") != std::string::npos ? " - " : " x ";
  size_t dash = segment.find(sepText);
  if (dash != std::string::npos && !hasVs && segment.find(sepText, dash + 3) == std::string::npos) {
    std::string left(str::trimUnicode(std::string_view(segment).substr(0, dash)));
    std::string right(str::trimUnicode(std::string_view(segment).substr(dash + 3)));
    auto sideOk = [](const std::string& side) {
      // Not a date or a list ("MON, 11/2 - ESPN FC").
      if (side.find(',') != std::string::npos || side.find('/') != std::string::npos) return false;
      size_t words = 0;
      bool letters = false;
      for (std::string_view w : str::split(side, ' ')) {
        if (w.empty()) continue;
        ++words;
        for (char c : w) letters = letters || asciiLetter(c) || static_cast<unsigned char>(c) >= 0x80;
      }
      return words >= 1 && words <= 5 && letters && !stageSide(str::lower(side));
    };
    if (sideOk(left) && sideOk(right)) segment = left + " vs " + right;
  } else if (hasVs && comma) {
    for (const char* sep : {" vs. ", " vs ", " v "}) {
      size_t at = segment.find(sep);
      if (at == std::string::npos) continue;
      size_t len = std::char_traits<char>::length(sep);
      segment = reversedName(segment.substr(0, at)) + sep + reversedName(segment.substr(at + len));
      break;
    }
  }
  return segment + rest;
}

}  // namespace

std::optional<Zone> eventZone(const std::optional<std::string>& market) {
  if (!market) return std::nullopt;
  // US markets stay out: their European-style listings are UK time.
  static const std::unordered_map<std::string, const char*> kZones = {
      {"AU", "Australia/Sydney"},    {"NZ", "Pacific/Auckland"},   {"UK", "Europe/London"},
      {"GB", "Europe/London"},       {"IE", "Europe/London"},      {"MX", "America/Mexico_City"},
      {"CA", "America/Toronto"},     {"ES", "Europe/Madrid"},      {"FR", "Europe/Paris"},
      {"DE", "Europe/Berlin"},       {"IT", "Europe/Rome"},        {"NL", "Europe/Amsterdam"},
      {"PT", "Europe/Lisbon"},       {"BR", "America/Sao_Paulo"},  {"DK", "Europe/Copenhagen"},
      {"SE", "Europe/Stockholm"},    {"NO", "Europe/Oslo"},        {"FI", "Europe/Helsinki"},
      {"IS", "Atlantic/Reykjavik"},  {"AT", "Europe/Vienna"},      {"CH", "Europe/Zurich"},
      {"BE", "Europe/Brussels"},     {"PL", "Europe/Warsaw"},      {"CZ", "Europe/Prague"},
      {"HU", "Europe/Budapest"},     {"GR", "Europe/Athens"},      {"AR", "America/Argentina/Buenos_Aires"},
      {"PE", "America/Lima"},        {"CO", "America/Bogota"}};
  auto it = kZones.find(str::upper(*market));
  if (it == kZones.end()) return std::nullopt;
  return Zone::named(it->second);
}

std::optional<std::string> familyMarket(std::string_view familyKey) {
  // Fanatiz lists South American football in Lima / Bogota time (UTC-5),
  // whatever group a copy is filed in.
  if (str::icontains(familyKey, "fanatiz")) return std::string("PE");
  // Vix's slots ("(MX) (VIX 04)") are on US Eastern time: its "Lo mejor de
  // la jornada (... 15:00:00)" is the 14:00 CST of the same show elsewhere.
  if (str::icontains(familyKey, "(vix")) return std::string("US");
  // "au (stan #)", "(mx) (vix #)", "ca | cfl #": the family's own country,
  // also for its copies filed in another country's group ("US| STAN PPV").
  if (familyKey.size() < 5 || str::startsWith(familyKey, "grp:")) return std::nullopt;
  size_t at = familyKey[0] == '(' ? 1 : 0;
  auto letter = [](char c) { return c >= 'a' && c <= 'z'; };
  if (!letter(familyKey[at]) || !letter(familyKey[at + 1])) return std::nullopt;
  size_t after = at + 2;
  if (at == 1) {
    if (familyKey[after] != ')') return std::nullopt;
    ++after;
  }
  size_t space = after;
  while (after < familyKey.size() && familyKey[after] == ' ') ++after;
  if (after == space || after >= familyKey.size() || (familyKey[after] != '(' && familyKey[after] != '|')) {
    return std::nullopt;
  }
  std::string code = str::upper(familyKey.substr(at, 2));
  if (code != "US" && !eventZone(code)) return std::nullopt;
  return code;
}

std::optional<std::string> slotMarket(const ChannelTraits& traits) {
  if (traits.familyKey) {
    if (auto market = familyMarket(*traits.familyKey)) return market;
  }
  return traits.market;
}

std::pair<std::string, std::optional<Seconds>> parseEvent(
    std::string_view text, Seconds now, const std::optional<std::string>& market) {
  ParsedEvent parsed = parseEventDetailed(text, now, market);
  return {parsed.title, parsed.start};
}

ParsedEvent parseEventDetailed(std::string_view input, Seconds now,
                               const std::optional<std::string>& market) {
  std::string title(input);
  std::optional<Seconds> start;
  bool guessed = false;
  const Zone usEastern = Zone::usEastern();
  // Calendar(identifier: .gregorian) uses the device's zone for the year.
  const int calendarYear = Zone::device().toLocal(now).year;

  auto date = [&](int y, int mo, int d, int h, int mi, const Zone& zone) {
    return dateIn(y, mo, d, h, mi, zone);
  };
  // The reading nearest to now among last year, this year and next year
  // (the first wins a tie, like Swift's min(by:)).
  auto nearestYear = [&](int mo, int d, int h, int mi, const Zone& zone) {
    Seconds best = 0;
    bool have = false;
    for (int y = calendarYear - 1; y <= calendarYear + 1; ++y) {
      Seconds t = date(y, mo, d, h, mi, zone);
      if (!have || std::llabs(t - now) < std::llabs(best - now)) {
        best = t;
        have = true;
      }
    }
    return best;
  };

  // Provider decorations: "NEXT |" / "LIVE |" status, "| 8K EXCLUSIVE | US:
  // ESPN+ PPV 951" tail, "LIVE EVENT 01 -" / "NFL | 06" slot heads,
  // ":Paramount+  07" service tails.
  title = rx::replace(title, "^\\W*(next|live|now|upcoming)\\s*\\|\\s*", "", true);
  title = rx::replace(title, "\\|\\s*8K EXCLUSIV.*$", "", true);
  title = rx::replace(title, "^\\s*live event\\s*\\d{1,3}\\s*-?\\s*", "", true);
  title = rx::replace(title, "^\\s*[A-Za-z+]{2,10}\\s+(?:event|board)\\s*\\d{1,3}\\s*:\\s+", "", true);  // "PDC event 01 : "
  // Build 136: the UEFA slots ("UEFA | 08 - RB Leipzig vs Manchester City
  // 20:00pm", "UEFA | 01 - Galatasaray vs Stuttgart 5:45pm") give the
  // kickoff in UK time, whatever their group's country: 20:00 is the 8 PM
  // GMT kickoff (21:00 CET, noon Pacific), not 8 PM Eastern.
  const bool ukSlot = rx::test("^\\s*UEFA\\s*\\|\\s*\\d{1,3}\\b", title, true);
  title = rx::replace(title, "^\\s*[A-Za-z+]{2,10}\\s*\\|\\s*\\d{1,3}\\s*-?\\s*", "");
  // A service tail naming its country (":Viaplay DK  03", ":VG  01") says
  // which clock the slot's time is on, AM/PM included.
  std::optional<Zone> slotZone;
  // Build 141: a slot that is nothing but the tail (":Golf  02") is empty.
  if (auto tail = rx::first("(?:^|\\s):\\s?([A-Za-z][\\w+ ]{0,24}?)\\s+\\d{1,3}\\s*$", title)) {
    slotZone = serviceZone(tail->group(1));
    title.erase(tail->begin, tail->end - tail->begin);
  }
  if (!slotZone && ukSlot) slotZone = Zone::london();

  // Unmarked times follow the slot's or the provider's country when it has
  // one; otherwise US formats are Eastern and European formats UK time.
  std::optional<Zone> providerZone = slotZone ? slotZone : eventZone(market);
  const Zone eastern = providerZone ? *providerZone : usEastern;
  const Zone london = providerZone ? *providerZone : Zone::london();
  // AM/PM times are a US habit (a UK provider writes "Oct 22 08:15 PM" for
  // an 8:15 PM ET kickoff), so they're US Eastern unless the slot names its
  // country. Providers that differ are corrected by the offset learned from
  // scheduled games.
  const Zone amPmZone = slotZone ? *slotZone : usEastern;

  // "Home vs Away // UK Tue 27 Oct 7:45pm // ET Tue 27 Oct 3:45pm": both
  // times, UK first; the Eastern one is the one to read.
  if (auto both = rx::first("^(.*?)\\s*//\\s*UK\\b[^/]*//\\s*ET\\b\\s*(.*)$", title, true)) {
    std::string etPart = rx::replace(both->group(2), "^(mon|tue|wed|thu|fri|sat|sun)[a-z]*\\.?\\s+", "", true);
    title = std::string(both->group(1)) + " " + etPart + " ET";
  }

  // Fri 23 Oct 18:00 EDT (US) / Fri 23 Oct 02:05 CEST (SE) / ... 19:40 +03 (SA)
  if (auto f =
          groups("(?:[A-Za-z]{3}\\s+)?(\\d{1,2})\\s+([A-Za-z]{3})\\s+(\\d{1,2}):(\\d{2})\\s+(?!"
                 "AM\\b|PM\\b)([A-Z]{2,5}|[+-]\\d{2})(?:\\s*\\([A-Z]{2}\\))?",
                 title)) {
    static const std::unordered_map<std::string, int> kOffsets = {
        {"EDT", -4}, {"EST", -5}, {"CDT", -5}, {"CST", -6}, {"MDT", -6},  {"MST", -7},
        {"PDT", -7}, {"PST", -8}, {"GMT", 0},  {"UTC", 0},  {"BST", 1},   {"CET", 1},
        {"CEST", 2}, {"EET", 2},  {"EEST", 3}, {"MSK", 3},  {"AEST", 10}, {"AEDT", 11}};
    std::string zoneText = str::upper(f->p[4]);
    std::optional<int> hours;
    if (auto it = kOffsets.find(zoneText); it != kOffsets.end()) {
      hours = it->second;
    } else {
      hours = num(zoneText);
    }
    auto d = num(f->p[0]);
    int mo = monthOf(f->p[1]);
    auto h = num(f->p[2]);
    auto mi = num(f->p[3]);
    // TimeZone(secondsFromGMT:) only takes offsets up to 18 hours.
    if (d && mo && h && mi && hours && std::abs(*hours) <= 18) {
      start = nearestYear(mo, *d, *h, *mi, Zone::fixed(*hours * 3600));
      removeRange(title, *f);
    }
  }
  // ... | 2026-10-18 | 15:30 (GMT)  /  22-10-2026 | 00:00 (GMT)
  if (!start) {
    if (auto f =
            groups("(\\d{4})-(\\d{2})-(\\d{2})\\s*\\|?\\s*(\\d{1,2}):(\\d{2})\\s*\\((?:GMT|UTC)\\)",
                   title)) {
      auto y = num(f->p[0]), mo = num(f->p[1]), d = num(f->p[2]), h = num(f->p[3]),
           mi = num(f->p[4]);
      if (y && mo && d && h && mi) start = date(*y, *mo, *d, *h, *mi, Zone::utc());
      removeRange(title, *f);
    }
  }
  if (!start) {
    if (auto f =
            groups("(\\d{2})-(\\d{2})-(\\d{4})\\s*\\|?\\s*(\\d{1,2}):(\\d{2})\\s*\\((?:GMT|UTC)\\)",
                   title)) {
      auto d = num(f->p[0]), mo = num(f->p[1]), y = num(f->p[2]), h = num(f->p[3]),
           mi = num(f->p[4]);
      if (y && mo && d && h && mi) start = date(*y, *mo, *d, *h, *mi, Zone::utc());
      removeRange(title, *f);
    }
  }
  // Oct 22 20:45 (24-hour, no AM/PM): provider time, taken as UK.
  if (!start) {
    if (auto f =
            groups("\\b([A-Za-z]{3}) (\\d{1,2}) (\\d{1,2}):(\\d{2})\\b(?!\\s*(?:AM|PM))", title)) {
      int mo = monthOf(f->p[0]);
      auto d = num(f->p[1]), h = num(f->p[2]), mi = num(f->p[3]);
      if (mo && d && h && mi) start = nearestYear(mo, *d, *h, *mi, london);
      removeRange(title, *f);
    }
  }
  // "@ 2026-11-02T17:50:00.000Z": UTC.
  if (!start && title.find("Z") != std::string::npos) {
    if (auto f = groups("(\\d{4})-(\\d{2})-(\\d{2})T(\\d{2}):(\\d{2})(?::\\d{2}(?:\\.\\d+)?)?Z", title)) {
      auto y = num(f->p[0]), mo = num(f->p[1]), d = num(f->p[2]), h = num(f->p[3]), mi = num(f->p[4]);
      if (y && mo && d && h && mi) start = date(*y, *mo, *d, *h, *mi, Zone::utc());
      removeRange(title, *f);
    }
  }
  // (2026 10 21 19:30:55), (2026-10-22 15:00:25), "start:2026 10 17
  // 20:25:00 stop:..." or "@ 2026-11-02 18:15:00"
  if (!start) {
    if (auto f =
            groups("(?:\\(|start:\\s*|@\\s*)(\\d{4})[ -](\\d{2})[ -](\\d{2})[ T](\\d{2}):(\\d{2})(?::"
                   "\\d{2})?\\)?(?:\\s*stop:\\s*[\\d :-]+)?",
                   title)) {
      auto y = num(f->p[0]), mo = num(f->p[1]), d = num(f->p[2]), h = num(f->p[3]),
           mi = num(f->p[4]);
      if (y && *y < 2090 && mo && d && h && mi) start = date(*y, *mo, *d, *h, *mi, eastern);
      removeRange(title, *f);
    }
  }
  // "- 22/10 11:30" (24-hour): day/month, or month/day when only that reads
  // as a date; when both do, the one nearer today.
  if (!start) {
    if (auto f = groups(
            "(?:^|\\s)-?\\s*(\\d{1,2})/(\\d{1,2})\\s+(\\d{1,2}):(\\d{2})\\b(?!\\s*(?:AM|PM))",
            title)) {
      auto a = num(f->p[0]), b = num(f->p[1]), h = num(f->p[2]), mi = num(f->p[3]);
      if (a && b && h && mi && *h < 24 && *mi < 60) {
        std::vector<Seconds> options;
        if (*b >= 1 && *b <= 12 && *a >= 1 && *a <= 31) {
          options.push_back(nearestYear(*b, *a, *h, *mi, eastern));
        }
        if (*a >= 1 && *a <= 12 && *b >= 1 && *b <= 31) {
          options.push_back(nearestYear(*a, *b, *h, *mi, eastern));
        }
        for (Seconds t : options) {
          if (!start || std::llabs(t - now) < std::llabs(*start - now)) start = t;
        }
      }
      removeRange(title, *f);
    }
  }
  // (10.21 7:30PM ET)
  if (!start) {
    if (auto f =
            groups("\\((\\d{1,2})\\.(\\d{1,2}) (\\d{1,2}):(\\d{2})\\s*(AM|PM)(?: ET)?\\)", title)) {
      auto mo = num(f->p[0]), d = num(f->p[1]), h = num(f->p[2]), mi = num(f->p[3]);
      if (mo && d && h && mi) start = nearestYear(*mo, *d, hour24(*h, f->p[4]), *mi, amPmZone);
      removeRange(title, *f);
    }
  }
  // [23.09 16:04] (day.month, European)
  if (!start) {
    if (auto f = groups("\\[(\\d{1,2})\\.(\\d{1,2}) (\\d{1,2}):(\\d{2})\\]", title)) {
      auto d = num(f->p[0]), mo = num(f->p[1]), h = num(f->p[2]), mi = num(f->p[3]);
      if (d && mo && h && mi) start = nearestYear(*mo, *d, *h, *mi, london);
      removeRange(title, *f);
    }
  }
  // Thursday, 22 October 2026 17:45
  if (!start) {
    if (auto f = groups(
            "(?:[A-Za-z]+,?\\s+)?(\\d{1,2})\\s+([A-Za-z]{3})[a-z]*\\s+(\\d{4})\\s+(\\d{1,2}):"
            "(\\d{2})",
            title)) {
      auto d = num(f->p[0]);
      int mo = monthOf(f->p[1]);
      auto y = num(f->p[2]), h = num(f->p[3]), mi = num(f->p[4]);
      if (d && mo && y && h && mi) start = date(*y, mo, *d, *h, *mi, london);
      removeRange(title, *f);
    }
  }
  // Wed 21st Oct 8:00PM ET. Without the ET it's a UK listing (the same
  // provider's darts: "Thu 29th Oct 12:00PM" is noon in the UK).
  if (!start) {
    if (auto f =
            groups("(?:[A-Za-z]{3}\\s+)?(\\d{1,2})(?:st|nd|rd|th)\\s+([A-Za-z]{3})[a-z]*\\s+(\\d{1,"
                   "2}):(\\d{2})\\s*(AM|PM)(\\s*ET)?",
                   title)) {
      auto d = num(f->p[0]);
      int mo = monthOf(f->p[1]);
      auto h = num(f->p[2]), mi = num(f->p[3]);
      const Zone& zone = f->p[5].empty() ? london : amPmZone;
      if (d && mo && h && mi) start = nearestYear(mo, *d, hour24(*h, f->p[4]), *mi, zone);
      removeRange(title, *f);
    }
  }
  // Oct 21 2:30AM ET (or, once a start is known, just dropped from the title)
  {
    std::optional<Found> f;
    if (!start) {
      f = groups(
          "\\b([A-Za-z]{3})[a-z]*\\.? (\\d{1,2}),? (\\d{1,2}):(\\d{2})\\s*(AM|PM)(?:\\s*ET)?",
          title);
    }
    if (f) {
      int mo = monthOf(f->p[0]);
      auto d = num(f->p[1]), h = num(f->p[2]), mi = num(f->p[3]);
      if (mo && d && h && mi) start = nearestYear(mo, *d, hour24(*h, f->p[4]), *mi, amPmZone);
      removeRange(title, *f);
    } else if (auto g = groups(
                   "\\b[A-Za-z]{3}[a-z]*\\.? \\d{1,2},? \\d{1,2}:\\d{2}\\s*(AM|PM)(?:\\s*ET)?",
                   title)) {
      removeRange(title, *g);
    }
  }
  // "12PM" / "730PM" / "7:30 PM ET" / "8.30 PM ET" -> that day when the title
  // names one ("02:00PM UFC 333 ... (OCT 24)"), else today (Eastern).
  if (!start) {
    if (auto f = groups("\\b(\\d{1,2})(?:[:.]?(\\d{2}))?\\s*(AM|PM)(?:\\s*(ET|UK))?\\b", title)) {
      removeRange(title, *f);
      if (auto h = num(f->p[0])) {
        int mi = num(f->p[1]).value_or(0);
        // "8:00 pm uk": UK time.
        const Zone& zone = str::lower(f->p[3]) == "uk" ? london : amPmZone;
        bool done = false;
        static const char* const kMonth =
            "(jan(?:uary)?|feb(?:ruary)?|mar(?:ch)?|apr(?:il)?|may|june?|july?|aug(?:ust)?|sep(?:t|tember)?|"
            "oct(?:ober)?|nov(?:ember)?|dec(?:ember)?)";
        // "(OCT 24)" or "Tue 27 Oct": the month's name and the day, either
        // way round (not "Oct 3:45pm", whose 3 is the hour).
        auto tryDay = [&](const std::string& monthText, std::optional<int> d, const Found& found) {
          int mo = monthOf(str::lower(monthText).substr(0, 3));
          if (!mo || !d || *d < 1 || *d > 31) return;
          Seconds named = nearestYear(mo, *d, hour24(*h, f->p[2]), mi, zone);
          // Only a day near today: a past one ("(OCT 24)" on the 27th) is a
          // card that's over; "MAR 28" may be Spanish for Tuesday the 28th,
          // so March only within three days.
          bool march = str::lower(monthText).rfind("mar", 0) == 0;
          Seconds back = (march ? 3 : 30) * 86400;
          if (named > now - back && named < now + 7 * 86400) {
            start = named;
            removeRange(title, found);
            done = true;
          }
        };
        if (auto dp = groups(std::string("\\(?\\b") + kMonth + "\\.?\\s+(\\d{1,2})\\b(?![:.]\\d)\\)?", title)) {
          tryDay(dp->p[0], num(dp->p[1]), *dp);
        }
        if (!done) {
          if (auto dp = groups(std::string("\\b(\\d{1,2})(?:st|nd|rd|th)?\\s+") + kMonth + "\\b", title)) {
            tryDay(dp->p[1], num(dp->p[0]), *dp);
          }
        }
        if (!done) {
          time::Civil today = zone.toLocal(now);
          start = date(today.year, today.month, today.day, hour24(*h, f->p[2]), mi, zone);
          guessed = true;
        }
      }
    }
  }

  // A channel running several events in a row lists them with "•" and names
  // the one on now in parentheses ("NCAA Football: BOS vs. CAR • WWE NXT •
  // SHSU vs. MOST (BOS vs. CAR)"): that one is the event. The sport in front
  // belongs to one of the others, so it goes.
  static const std::string kBullet = "\xE2\x80\xA2";
  if (title.find(kBullet) != std::string::npos) {
    std::string body(str::trimUnicode(title));
    std::string named;
    if (!body.empty() && body.back() == ')') {
      int depth = 0;
      for (size_t i = body.size(); i-- > 0;) {
        if (body[i] == ')') {
          ++depth;
        } else if (body[i] == '(' && --depth == 0) {
          named = str::lower(std::string(str::trimUnicode(body.substr(i + 1, body.size() - i - 2))));
          body = body.substr(0, i);
          break;
        }
      }
    }
    size_t bullet = body.find(kBullet);
    size_t colon = body.find(": ");
    if (colon != std::string::npos && colon < bullet) body = body.substr(colon + 2);
    std::vector<std::string> parts;
    for (size_t from = 0;;) {
      size_t at = body.find(kBullet, from);
      std::string part(str::trimUnicode(body.substr(from, at == std::string::npos ? std::string::npos : at - from)));
      if (!part.empty()) parts.push_back(part);
      if (at == std::string::npos) break;
      from = at + kBullet.size();
    }
    if (!parts.empty()) {
      title = parts.front();
      for (const std::string& part : parts) {
        if (!named.empty() && str::lower(part).rfind(named, 0) == 0) {
          title = part;
          break;
        }
      }
    }
  }

  // Darts' boards are feeds of one event ("... Maasport Board 3"), and a
  // description after the venue isn't its name ("Maasport The Players
  // Championship continues in Maaspoort, ...").
  title = rx::replace(title, "\\s+board\\s+\\d{1,2}\\b(?=[\\s|]*$)", "", true);
  title = rx::replace(title, "\\s+The\\s[^|]{6,}?\\s(continues|takes place|returns|heads)\\b[^|]*$", "");

  // "Villarreal v Paris SaintGermain  UEFA Champions League Matchday 4
  // 2026/2027": two spaces after the matchup, then its competition (the
  // season goes when it's the current one).
  static const char* const kTidy = "|-–:,";
  title = std::string(str::trimUnicode(title, false, kTidy));
  if (size_t gap = title.find("  "); gap != std::string::npos && title.find('|') == std::string::npos) {
    std::string left = title.substr(0, gap);
    std::string right(str::trimUnicode(std::string_view(title).substr(gap)));
    std::string lowerLeft = " " + str::lowerAscii(left) + " ";
    bool matchup = lowerLeft.find(" v ") != std::string::npos || lowerLeft.find(" vs ") != std::string::npos ||
                   lowerLeft.find(" vs. ") != std::string::npos;
    // Only a competition with its season ("... 2026/2027", "... Round 4
    // 2026"): two spaces before a day or a country ("Sun", "UK") aren't one.
    auto season = matchup && !right.empty() && asciiLetter(right[0])
                      ? rx::first("\\s+(20\\d\\d)(?:/(?:20)?\\d\\d)?$", right)
                      : std::nullopt;
    if (season) {
      int year = num(std::string(season->group(1))).value_or(0);
      if (year == calendarYear || year == calendarYear - 1) right.erase(season->begin);
      title = left + " | " + right;
    }
  }
  // "Pumas (F) vs. Atlas (F)": women's sides, named so ("Pumas Women") they
  // never meet the men's game of the same clubs.
  if (title.find("(F)") != std::string::npos || title.find("(W)") != std::string::npos) {
    title = rx::replace(title, "\\s\\((?:F|W)\\)(?=\\s+(?:vs\\.?|v|x|-)\\s|\\s*$|\\s*\\|)", " Women");
  }

  // Tidy: drop a trailing "(X)" that repeats the title, leading "ESPN+:"
  // style tags, separators and doubled spaces.
  title = std::string(str::trimUnicode(rx::replace(title, "\\s{2,}", " "), false, kTidy));
  size_t open = title.rfind('(');
  if (open != std::string::npos && str::endsWith(title, ")")) {
    std::string inner = title.substr(open + 1, title.size() - open - 2);
    std::string outer = title.substr(0, open);
    std::string innerTrim(str::trimUnicode(str::lower(inner)));
    bool repeats = !innerTrim.empty() && text::contains(str::lower(outer), innerTrim);
    if (repeats || str::trimUnicode(inner).empty()) title = std::string(str::trimUnicode(outer));
  }
  title = rx::replace(title, "^(ESPN\\+|ESPN PLUS):\\s*", "", true);
  title = rx::replace(title, "\\s*@\\s*$", "");
  // "Bellucci, Mattia vs Sonego, Lorenzo @ <time> - ATP Paris": the
  // competition after the time.
  if (size_t at = title.find(" @ - "); at != std::string::npos) title.replace(at, 5, " | ");
  // "Bears vs. Seahawks | all | ...": "all" is no competition, and the
  // slot's group ("SOCCER PPV") then says nothing about the sport.
  bool noCompetition = false;
  if (title.find('|') != std::string::npos && str::icontains(title, "all")) {
    if (auto all = rx::first("\\s*\\|\\s*all\\s*(?=\\||$)", title, true)) {
      title.erase(all->begin, all->end - all->begin);
      noCompetition = true;
    }
  }
  title = tidyMatchup(title);
  title = rx::replace(title, "\\s*\\|\\s*", " · ");
  title = rx::replace(title, "^[A-Za-z]{3}, \\d{1,2}/\\d{1,2}\\s+", "");
  title = std::string(str::trimUnicode(title, false, kTidy));
  ParsedEvent parsed{title, start, guessed};
  parsed.noCompetition = noCompetition;
  return parsed;
}

std::string eventKey(std::string_view title, const std::optional<Seconds>& start) {
  std::string normalized = str::lower(title);
  normalized = rx::replace(normalized, "^[a-z+ ]{2,20}:\\s*", "");
  normalized = rx::replace(normalized, "\\([^)]*\\)?", "");
  normalized = rx::replace(normalized, "\\b(vs|v|at)\\b\\.?|@", " ");
  normalized = rx::replace(normalized, "\\(spanish\\)|\\(english\\)", "");
  normalized = rx::replace(normalized, "[^a-z0-9]+", "");
  Seconds slot = start ? *start / 1800 : 0;  // Int() truncates toward zero, as here
  return normalized + "|" + std::to_string(slot);
}

std::string leagueFor(std::string_view family, std::string_view group, std::string_view title) {
  std::string titleText = str::lower(title);
  std::string all =
      str::lower(std::string(family) + " " + std::string(group) + " " + std::string(title));
  auto has = [&](std::string_view w) { return text::hasWord(all, w); };
  // Pro wrestling first (shown in Live Events only when My Sports says so):
  // "Wrestling: WWE NXT" is entertainment, not the sport.
  if (rx::test(kProWrestlingWords, all)) return kProWrestling;
  // 1. A sport named at the start of the title ("Football: ...",
  // "volleyball:  ...") says more than the channel's group.
  if (auto m = text::firstMatch("^\\s*[a-z][a-z ]{1,20}(?=:)", titleText)) {
    std::string sport(str::trimUnicode(*m));
    // Level unknown -> the plain sport, which matches pro and college games.
    if (sport == "football" || sport == "american football") {
      if (has("nfl")) return "NFL";
      return rx::test("\\b(ncaa|ncaaf|college|fbs|fcs)\\b", all) ? "College Football" : "Football";
    }
    // "hoops:  Harbor vs Valley _ Men`s": basketball too.
    if (sport == "basketball" || sport == "hoops") {
      if (has("wnba")) return "WNBA";
      if (rx::test("\\bnba\\b", all)) return "NBA";
      // "UCF vs. Kentucky (EXH)": college exhibitions say only that.
      return rx::test("\\b(ncaa|ncaab|ncaam|ncaaw|college|men['`]s|women['`]s|exh|exhibition)\\b", all)
                 ? "College Basketball"
                 : "Basketball";
    }
    if (sport == "hockey" || sport == "ice hockey") return has("nhl") ? "NHL" : "Hockey";
    if (sport == "field hockey") return "Field Hockey";
    if (sport == "volleyball" || sport == "beach volleyball") return "Volleyball";
    if (sport == "soccer" || sport == "fc" || sport == "football soccer") return "Soccer";
    if (sport == "baseball") return has("mlb") ? "MLB" : "Baseball";
    if (sport == "softball") return "Softball";
    if (sport == "lacrosse") return "Lacrosse";
    if (sport == "wrestling") return "Wrestling";
  }
  struct Rule {
    const char* league;
    std::vector<const char*> words;
  };
  // 2. Words anywhere. A "PPV" group says nothing about the sport, so it no
  // longer makes everything in it "Fighting".
  static const std::vector<Rule> kRules = {
      {"NFL", {"nfl", "football: good morning football"}},
      {"College Football", {"ncaaf", "college football", "fbs", "fcs"}},
      {"WNBA", {"wnba"}},
      {"NBA", {"nba", "basketball: nba"}},
      {"College Basketball",
       {"ncaab", "college basketball", "men's basketball", "women's basketball"}},
      {"MiLB", {"milb"}},
      {"MLB", {"mlb"}},
      {"Field Hockey", {"field hockey"}},
      {"NHL", {"nhl"}},
      {"Hockey", {"hockey"}},
      {"Baseball", {"baseball"}},
      {"MLS", {"mls"}},
  };
  for (const Rule& rule : kRules) {
    for (const char* w : rule.words) {
      if (has(w)) return rule.league;
    }
  }
  // Fights before soccer, as whole words: soccer's "fc " also matches the end
  // of "ufc " (UFC cards were landing in Soccer).
  if (rx::test(kFightWords, all)) return "Fighting";
  static const std::vector<Rule> kLaterRules = {
      {"Soccer",
       {"uefa", "premier league", "champions league", "la liga", "laliga", "serie a", "bundesliga",
        "ligue1", "ligue 1", "football event", "soccer", "fc ", " united", "liga", "eredivisie",
        "a-league", "concacaf", "nwsl",
        // Club initials of the Balkans and the north ("KF Vllaznia", "Vora FK", "NK Osijek").
        "kf", "ks", "fk", "nk", "afc"}},
      {"Motorsport",
       {"f1", "formula", "nascar", "indycar", "motogp", "rally", "wrc", "supercross", "motocross"}},
      {"Tennis", {"wta", "atp", "tennis"}},
      {"Golf", {"pga", "lpga", "golf", "liv "}},
      {"Darts", {"pdc", "darts"}},
      {"Cricket", {"cricket", "ipl"}},
      {"Rugby", {"rugby", "nrl"}},
      {"Volleyball", {"volleyball"}},
  };
  for (const Rule& rule : kLaterRules) {
    for (const char* w : rule.words) {
      if (has(w)) return rule.league;
    }
  }
  return "Other";
}

}  // namespace marigold::catalog
