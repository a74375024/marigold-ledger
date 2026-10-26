// Event titles: start times in the many formats IPTV providers write, the
// deduplication key, and the league a title belongs to.
#include <cstdlib>
#include <unordered_map>

#include "core/catalog/catalog.hpp"
#include "core/catalog/text.hpp"
#include "core/util/rx.hpp"
#include "core/util/strings.hpp"

namespace marigold::catalog {

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

}  // namespace

std::optional<Zone> eventZone(const std::optional<std::string>& market) {
  if (!market) return std::nullopt;
  std::string m = str::upper(*market);
  const char* id = nullptr;
  if (m == "AU")
    id = "Australia/Sydney";
  else if (m == "NZ")
    id = "Pacific/Auckland";
  else if (m == "UK" || m == "GB" || m == "IE")
    id = "Europe/London";
  else if (m == "MX")
    id = "America/Mexico_City";
  else if (m == "CA")
    id = "America/Toronto";
  else if (m == "ES")
    id = "Europe/Madrid";
  else if (m == "FR")
    id = "Europe/Paris";
  else if (m == "DE")
    id = "Europe/Berlin";
  else if (m == "IT")
    id = "Europe/Rome";
  else if (m == "NL")
    id = "Europe/Amsterdam";
  else if (m == "PT")
    id = "Europe/Lisbon";
  else if (m == "BR")
    id = "America/Sao_Paulo";
  if (!id) return std::nullopt;
  return Zone::named(id);
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
  // Unmarked times follow the provider's country when it has one; otherwise
  // US formats are Eastern and European formats UK time.
  std::optional<Zone> providerZone = eventZone(market);
  const Zone eastern = providerZone ? *providerZone : usEastern;
  const Zone london = providerZone ? *providerZone : Zone::london();
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
  // AM/PM times are a US habit (a UK provider writes "Oct 22 08:15 PM" for
  // an 8:15 PM ET kickoff), so they're US Eastern. Providers that differ are
  // corrected by the offset learned from scheduled games.
  const Zone& amPmZone = usEastern;

  // Provider decorations: "NEXT |" / "LIVE |" status, "| 8K EXCLUSIVE | US:
  // ESPN+ PPV 951" tail, "LIVE EVENT 01 -" / "NFL | 06" slot heads,
  // ":Paramount+  07" service tails.
  title = rx::replace(title, "^\\W*(next|live|now|upcoming)\\s*\\|\\s*", "", true);
  title = rx::replace(title, "\\|\\s*8K EXCLUSIV.*$", "", true);
  title = rx::replace(title, "^\\s*live event\\s*\\d{1,3}\\s*-?\\s*", "", true);
  title = rx::replace(title, "^\\s*[A-Za-z+]{2,10}\\s*\\|\\s*\\d{1,3}\\s*-?\\s*", "");
  title = rx::replace(title, "\\s:\\s?[A-Za-z][\\w+ ]{0,24}?\\s+\\d{1,3}\\s*$", "");

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
  // (2026 10 21 19:30:55), (2026-10-22 15:00:25) or "start:2026 10 17 20:25:00 stop:..."
  if (!start) {
    if (auto f =
            groups("(?:\\(|start:\\s*)(\\d{4})[ -](\\d{2})[ -](\\d{2})[ T](\\d{2}):(\\d{2})(?::"
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
  // Wed 21st Oct 8:00PM ET
  if (!start) {
    if (auto f =
            groups("(?:[A-Za-z]{3}\\s+)?(\\d{1,2})(?:st|nd|rd|th)\\s+([A-Za-z]{3})[a-z]*\\s+(\\d{1,"
                   "2}):(\\d{2})\\s*(AM|PM)(?:\\s*ET)?",
                   title)) {
      auto d = num(f->p[0]);
      int mo = monthOf(f->p[1]);
      auto h = num(f->p[2]), mi = num(f->p[3]);
      if (d && mo && h && mi) start = nearestYear(mo, *d, hour24(*h, f->p[4]), *mi, amPmZone);
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
    if (auto f = groups("\\b(\\d{1,2})(?:[:.]?(\\d{2}))?\\s*(AM|PM)(?:\\s*ET)?\\b", title)) {
      removeRange(title, *f);
      if (auto h = num(f->p[0])) {
        int mi = num(f->p[1]).value_or(0);
        const Zone& zone = amPmZone;
        bool done = false;
        if (auto dp =
                groups("\\(?\\b(jan(?:uary)?|feb(?:ruary)?|mar(?:ch)?|apr(?:il)?|may|june?|july?|"
                       "aug(?:ust)?|sep(?:t|tember)?|oct(?:ober)?|nov(?:ember)?|dec(?:ember)?)"
                       "\\.?\\s+(\\d{1,2})\\b\\)?",
                       title)) {
          int mo = monthOf(dp->p[0].substr(0, 3));
          auto d = num(dp->p[1]);
          if (mo && d && *d >= 1 && *d <= 31) {
            Seconds named = nearestYear(mo, *d, hour24(*h, f->p[2]), mi, zone);
            // Only a day near today ("MAR 28" may be Spanish for Tuesday the 28th).
            if (named > now - 3 * 86400 && named < now + 7 * 86400) {
              start = named;
              removeRange(title, *dp);
              done = true;
            }
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

  // Tidy: drop a trailing "(X)" that repeats the title, leading "ESPN+:"
  // style tags, separators and doubled spaces.
  static const char* const kTidy = "|-–:,";
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
  title = rx::replace(title, "\\s*\\|\\s*", " · ");
  title = rx::replace(title, "^[A-Za-z]{3}, \\d{1,2}/\\d{1,2}\\s+", "");
  title = std::string(str::trimUnicode(title, false, kTidy));
  return {title, start, guessed};
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
  auto has = [&](std::string_view w) { return text::contains(all, w); };
  // 1. A sport named at the start of the title ("Football: ...",
  // "volleyball:  ...") says more than the channel's group.
  if (auto m = text::firstMatch("^\\s*[a-z][a-z ]{1,20}(?=:)", titleText)) {
    std::string sport(str::trimUnicode(*m));
    // Level unknown -> the plain sport, which matches pro and college games.
    if (sport == "football" || sport == "american football") {
      if (has("nfl")) return "NFL";
      return rx::test("\\b(ncaa|ncaaf|college|fbs|fcs)\\b", all) ? "College Football" : "Football";
    }
    if (sport == "basketball") {
      if (has("wnba")) return "WNBA";
      if (rx::test("\\bnba\\b", all)) return "NBA";
      return rx::test("\\b(ncaa|ncaab|ncaam|ncaaw|college|men's|women's)\\b", all)
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
        "a-league", "concacaf", "nwsl"}},
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
