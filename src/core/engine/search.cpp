#include "core/engine/search.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <utility>

#include "core/sports/matcher.hpp"
#include "core/sports/network_map.hpp"
#include "core/sports/types.hpp"
#include "core/util/strings.hpp"

namespace marigold::engine::search {

namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isLetter(char c) { return (c >= 'a' && c <= 'z') || static_cast<unsigned char>(c) >= 0x80; }

bool allDigits(std::string_view s) {
  return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return isDigit(c); });
}
bool allLetters(std::string_view s) {
  return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return isLetter(c); });
}

std::string_view bare(std::string_view s) { return str::startsWith(s, "the ") ? s.substr(4) : s; }

// `q` starts at a word in `name` (not the first: that's a start).
bool wordStart(std::string_view name, std::string_view q) {
  for (size_t i = 1; i + q.size() <= name.size(); ++i)
    if (name[i - 1] == ' ' && name.compare(i, q.size(), q) == 0) return true;
  return false;
}

// Every token on a word of its own, in any order; the last may be the
// start of one. With `typos`, a token may also be a few edits off a word.
bool tokensMatch(const std::vector<std::string>& tokens, const std::vector<std::string>& w, bool typos) {
  if (tokens.empty() || w.empty()) return false;
  std::vector<bool> used(w.size(), false);
  bool anyTypo = false;
  for (size_t t = 0; t < tokens.size(); ++t) {
    const std::string& tok = tokens[t];
    bool last = t + 1 == tokens.size();
    int found = -1;
    // Exact words first, then a start (last token), then a typo.
    for (int pass = 0; pass < 3 && found < 0; ++pass) {
      if (pass == 2 && !typos) break;
      for (size_t i = 0; i < w.size() && found < 0; ++i) {
        if (used[i]) continue;
        if (pass == 0 && w[i] == tok) found = static_cast<int>(i);
        else if (pass == 1 && last && str::startsWith(w[i], tok)) found = static_cast<int>(i);
        else if (pass == 2 && allLetters(tok) && tok.size() >= 3) {
          int allow = typoAllowance(std::max(tok.size(), w[i].size()));
          if (allow > 0 && editDistance(tok, w[i], allow) <= allow) {
            found = static_cast<int>(i);
            anyTypo = true;
          }
        }
      }
    }
    if (found < 0) return false;
    used[static_cast<size_t>(found)] = true;
  }
  return !typos || anyTypo;
}

// A word anywhere in `names` starting with `tok`.
bool tokenIn(const std::string& tok, const std::vector<std::string>& names) {
  for (const std::string& n : names)
    if (str::startsWith(n, tok) || wordStart(n, tok)) return true;
  return false;
}

// The day boundaries in the viewer's zone.
time::Seconds midnightAfter(time::Seconds t, const time::Zone& zone, int days) {
  time::Civil c = zone.toLocal(t);
  time::Civil next = time::civilFromSeconds(time::secondsFromCivil({c.year, c.month, c.day, 0, 0, 0}) + 86400 * days);
  return zone.fromLocal(next);
}

bool inWhen(When when, bool live, time::Seconds start, time::Seconds end, const Options& o) {
  switch (when) {
    case When::Any: return true;
    case When::Now: return live || (start <= o.now + 1800 && end > o.now);  // on now, or starting within the half hour
    case When::Today:
    case When::Tonight: return (live || start >= o.now) && start < midnightAfter(o.now, o.zone, 1);
    case When::Tomorrow:
      return start >= midnightAfter(o.now, o.zone, 1) && start < midnightAfter(o.now, o.zone, 2);
  }
  return true;
}

int strength(Hit h) {
  switch (h) {
    case Hit::Exact: return 7;
    case Hit::Start: return 6;
    case Hit::Word: return 5;
    case Hit::Tokens: return 4;
    case Hit::Initials: return 3;
    case Hit::Inside: return 2;
    case Hit::Typo: return 1;
    case Hit::None: return 0;
  }
  return 0;
}

Hit best(Hit a, Hit b) { return strength(a) >= strength(b) ? a : b; }

Hit bestOf(const Query& q, const std::vector<std::string>& names) {
  Hit h = Hit::None;
  for (const std::string& n : names) {
    h = best(h, match(q, n));
    if (h == Hit::Exact) break;
  }
  return h;
}

// The tier of a name match in the merged list (word starts, initials, typos).
int tierOf(Hit h) {
  switch (h) {
    case Hit::Exact:
    case Hit::Start:
    case Hit::Word:
    case Hit::Tokens: return 6;
    case Hit::Initials:
    case Hit::Inside: return 7;
    case Hit::Typo: return 8;
    case Hit::None: return 9;
  }
  return 9;
}

constexpr int kNeverTop = 99;

struct Genre {
  const char* key;
  const char* title;
  std::array<const char*, 4> words;
};
const Genre kGenres[] = {
    {"genre:news", "News", {"news", "", "", ""}},
    {"genre:movies", "Movies", {"movies", "movie", "films", "film"}},
    {"genre:kids", "Kids", {"kids", "children", "cartoons", "kid"}},
    {"genre:sports", "Sports", {"sports", "sport", "", ""}},
};

}  // namespace

// ---------------------------------------------------------------- text

std::string fold(std::string_view s) {
  std::string lowered = str::lower(str::foldDiacritics(s));
  std::string out;
  out.reserve(lowered.size() + 8);
  auto space = [&] {
    if (!out.empty() && out.back() != ' ') out += ' ';
  };
  char prev = ' ';
  for (size_t i = 0; i < lowered.size(); ++i) {
    char c = lowered[i];
    unsigned char u = static_cast<unsigned char>(c);
    // Curly apostrophes (U+2019) join like straight ones.
    if (u == 0xE2 && i + 2 < lowered.size() && static_cast<unsigned char>(lowered[i + 1]) == 0x80 &&
        static_cast<unsigned char>(lowered[i + 2]) == 0x99) {
      i += 2;
      continue;
    }
    if (c == '\'') continue;
    if (c == '&') {
      space();
      out += "and ";
      prev = ' ';
      continue;
    }
    bool letter = isLetter(c), digit = isDigit(c);
    if (!letter && !digit) {
      space();
      prev = ' ';
      continue;
    }
    // Letters and digits apart ("sn2" -> "sn 2"); a UTF-8 tail byte stays.
    if ((digit && isLetter(prev) && u < 0x80) || (letter && u < 0x80 && isDigit(prev))) space();
    out += c;
    prev = c;
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

std::vector<std::string> words(std::string_view folded) {
  std::vector<std::string> out;
  for (std::string_view w : str::split(folded, ' '))
    if (!w.empty()) out.emplace_back(w);
  return out;
}

std::string initials(const std::vector<std::string>& w) {
  std::string out;
  for (const std::string& x : w)
    if (!x.empty() && !allDigits(x)) out += x[0];
  return out;
}

int editDistance(std::string_view a, std::string_view b, int cap) {
  int n = static_cast<int>(a.size()), m = static_cast<int>(b.size());
  if (std::abs(n - m) > cap) return cap + 1;
  // Optimal string alignment: insertions, deletions, substitutions and a
  // swap of two neighbours, each one edit.
  std::vector<std::vector<int>> d(static_cast<size_t>(n + 1), std::vector<int>(static_cast<size_t>(m + 1), 0));
  for (int i = 0; i <= n; ++i) d[static_cast<size_t>(i)][0] = i;
  for (int j = 0; j <= m; ++j) d[0][static_cast<size_t>(j)] = j;
  for (int i = 1; i <= n; ++i) {
    int rowBest = cap + 1;
    for (int j = 1; j <= m; ++j) {
      size_t ui = static_cast<size_t>(i), uj = static_cast<size_t>(j);
      int cost = a[ui - 1] == b[uj - 1] ? 0 : 1;
      int v = std::min({d[ui - 1][uj] + 1, d[ui][uj - 1] + 1, d[ui - 1][uj - 1] + cost});
      if (i > 1 && j > 1 && a[ui - 1] == b[uj - 2] && a[ui - 2] == b[uj - 1]) v = std::min(v, d[ui - 2][uj - 2] + 1);
      d[ui][uj] = v;
      rowBest = std::min(rowBest, v);
    }
    if (rowBest > cap) return cap + 1;
  }
  return std::min(d[static_cast<size_t>(n)][static_cast<size_t>(m)], cap + 1);
}

int typoAllowance(size_t letters) { return letters >= 8 ? 2 : letters >= 5 ? 1 : 0; }

// ---------------------------------------------------------------- query

size_t Query::length() const {
  return static_cast<size_t>(std::count_if(text.begin(), text.end(), [](char c) { return c != ' '; }));
}

namespace {

// Number words from dictation ("two oh six" -> "206"); "" when any word
// isn't one.
std::string numberWords(const std::vector<std::string>& w) {
  static const std::map<std::string, char> kDigits = {
      {"zero", '0'}, {"oh", '0'}, {"o", '0'}, {"one", '1'}, {"two", '2'}, {"three", '3'}, {"four", '4'},
      {"five", '5'}, {"six", '6'}, {"seven", '7'}, {"eight", '8'}, {"nine", '9'}};
  std::string out;
  for (const std::string& x : w) {
    if (allDigits(x)) {
      out += x;
      continue;
    }
    auto it = kDigits.find(x);
    if (it == kDigits.end()) return {};
    out += it->second;
  }
  return out;
}

}  // namespace

Query parse(std::string_view raw, const std::function<bool(const std::string&)>& isTitle) {
  Query q;
  q.raw = std::string(raw);
  std::string folded = fold(raw);
  std::vector<std::string> w = words(folded);
  // A title as typed ("Lakers Tonight", "NFL Live") is never cleaned.
  bool title = !folded.empty() && isTitle && isTitle(folded);
  if (!title && w.size() > 1) {
    static const std::set<std::string> kFiller = {"channel", "ch", "watch", "that", "show", "game", "match",
                                                  "the",     "a",  "on",    "vs",   "v",    "at",   "tv"};
    static const std::set<std::string> kJoin = {"vs", "v", "at"};
    auto timeWord = [](const std::string& x) -> std::optional<When> {
      if (x == "now") return When::Now;
      if (x == "tonight") return When::Tonight;
      if (x == "today") return When::Today;
      if (x == "tomorrow") return When::Tomorrow;
      return std::nullopt;
    };
    bool changed = true;
    while (changed && w.size() > 1) {
      changed = false;
      // Time words at either end ("on now" is "on" and "now").
      for (bool front : {false, true}) {
        if (w.size() < 2) break;
        const std::string& x = front ? w.front() : w.back();
        if (auto when = timeWord(x)) {
          q.when = *when;
          front ? (void)w.erase(w.begin()) : w.pop_back();
          changed = true;
        }
      }
      if (w.size() > 1 && kFiller.count(w.back())) {
        w.pop_back();
        changed = true;
      }
      if (w.size() > 1 && kFiller.count(w.front())) {
        w.erase(w.begin());
        changed = true;
      }
    }
    // "bulls vs kings", "rams at bulls": the joining word goes.
    if (w.size() > 2) {
      std::vector<std::string> kept;
      for (size_t i = 0; i < w.size(); ++i)
        if (i == 0 || i + 1 == w.size() || !kJoin.count(w[i])) kept.push_back(w[i]);
      w.swap(kept);
    }
    q.cleaned = str::join(w, " ") != folded;
  }
  // Spoken numbers, and a number on its own.
  std::string number = numberWords(w);
  if (!number.empty() && !title) {
    q.digits = true;
    q.cleaned = q.cleaned || w.size() > 1 || !allDigits(w.front());
    w = {number};
  }
  q.text = str::join(w, " ");
  q.tokens = w;
  if (q.length() >= 3 && !q.digits) q.regional = sports::regional::key(raw).value_or("");
  return q;
}

Hit match(const Query& q, std::string_view name) {
  if (q.text.empty() || name.empty()) return Hit::None;
  std::string_view t = q.text, tb = bare(t), nb = bare(name);
  if (name == t || nb == tb || nb == t) return Hit::Exact;
  size_t len = q.length();
  if (str::startsWith(name, t) || str::startsWith(nb, tb)) return Hit::Start;
  if (len == 1) return Hit::None;
  if (wordStart(name, t)) return Hit::Word;
  std::vector<std::string> w = words(name);
  if (w.size() >= 2) {
    // "nb" = Northbridge Bulls (without "the" too); "sn 2" = Sports Net 2.
    std::vector<std::string> wb = str::startsWith(name, "the ") ? std::vector<std::string>(w.begin() + 1, w.end()) : w;
    if (q.tokens.size() == 1 && allLetters(t) && t.size() <= 6 && (t == initials(w) || t == initials(wb)))
      return Hit::Initials;
    if (q.tokens.size() == 2 && allLetters(q.tokens[0]) && allDigits(q.tokens[1]) && q.tokens[0].size() >= 2 &&
        q.tokens[0] == initials(wb) && std::find(wb.begin(), wb.end(), q.tokens[1]) != wb.end())
      return Hit::Initials;
  }
  if (len == 2) return Hit::None;
  if (q.tokens.size() >= 2 && tokensMatch(q.tokens, w, false)) return Hit::Tokens;
  if (name.find(t) != std::string_view::npos) return Hit::Inside;
  if (!q.exactOnly && tokensMatch(q.tokens, w, true)) return Hit::Typo;
  return Hit::None;
}

// ---------------------------------------------------------------- index

void Index::finish() {
  channelById.clear();
  gameById.clear();
  teamByKey.clear();
  for (size_t i = 0; i < channels.size(); ++i) {
    channelById[channels[i].id] = i;
    titles.insert(channels[i].name);
  }
  for (size_t i = 0; i < games.size(); ++i) gameById[games[i].id] = i;
  for (size_t i = 0; i < teams.size(); ++i) {
    teamByKey[teams[i].key] = i;
    if (!teams[i].names.empty()) titles.insert(teams[i].names.front());
  }
  for (const auto& [title, airings] : shows) titles.insert(title);
}

const std::unordered_map<std::string, std::vector<std::string>>& nicknames() {
  // Team nicknames only (channel names come from the catalog and the
  // reference). Folded, as typed.
  static const std::unordered_map<std::string, std::vector<std::string>> kNicknames = {
      {"niners", {"san francisco 49 ers"}},
      {"sixers", {"philadelphia 76 ers"}},
      {"man utd", {"manchester united"}},
      {"man u", {"manchester united"}},
      {"man united", {"manchester united"}},
      {"man city", {"manchester city"}},
      {"spurs", {"tottenham hotspur", "san antonio spurs"}},
      {"habs", {"montreal canadiens"}},
      {"bucs", {"tampa bay buccaneers"}},
      {"pens", {"pittsburgh penguins"}},
      {"caps", {"washington capitals"}},
      {"canes", {"carolina hurricanes"}},
      {"leafs", {"toronto maple leafs"}},
      {"bolts", {"tampa bay lightning"}},
      {"nats", {"washington nationals"}},
      {"barca", {"barcelona"}},
      {"gunners", {"arsenal"}},
      {"wolves", {"wolverhampton wanderers", "minnesota timberwolves"}},
      {"pats", {"new england patriots"}},
      {"phins", {"miami dolphins"}},
      {"cavs", {"cleveland cavaliers"}},
      {"mavs", {"dallas mavericks"}},
  };
  return kNicknames;
}

std::vector<std::string> teamNames(const sports::SportsTeam& team, bool college) {
  std::vector<std::string> out;
  auto add = [&](const std::string& n) {
    std::string f = fold(n);
    if (!f.empty() && std::find(out.begin(), out.end(), f) == out.end()) out.push_back(f);
  };
  add(team.name);
  for (const std::string& v : sports::matcher::teamVariants(team, college)) add(v);
  for (const std::string& a : team.alternates) add(a);
  for (const std::string& n : nicknamesOf(fold(team.name))) add(n);
  return out;
}

std::vector<std::string> nicknamesOf(const std::string& foldedName) {
  std::vector<std::string> out;
  for (const auto& [nick, teams] : nicknames())
    if (std::find(teams.begin(), teams.end(), foldedName) != teams.end()) out.push_back(nick);
  std::sort(out.begin(), out.end());
  return out;
}

GameEntry gameEntry(const EventTile& t) {
  GameEntry g;
  g.id = t.id;
  g.title = matchup(t);
  auto add = [&](const std::string& n) {
    std::string f = fold(n);
    if (!f.empty() && std::find(g.names.begin(), g.names.end(), f) == g.names.end()) g.names.push_back(f);
  };
  add(g.title);
  for (const EventSide& s : t.sides) add(s.name);
  for (const EventPart& p : t.parts) add(p.label);
  g.sport = fold(t.sport);
  using P = EventTile::Pill;
  bool live = !t.upcoming && !t.finished &&
              (t.pill == P::Live || t.pill == P::LiveOutline || t.pill == P::OnNow || t.pill == P::Delayed);
  g.state = t.finished                  ? GameEntry::State::Ended
            : live                      ? GameEntry::State::Live
            : t.upcoming && t.start > 0 ? GameEntry::State::Upcoming
                                        : GameEntry::State::Unknown;
  g.start = t.start;
  g.watchable = t.watchable;
  g.streamingOnly = !t.watchable && t.footer == "Streaming only";
  g.favorite = t.favorite;
  return g;
}

bool placeholderShow(const Listing& l, const std::string& channelName) {
  if (l.end - l.start >= 12 * 3600) return true;
  std::string t = fold(l.title);
  if (t.empty() || t == fold(channelName)) return true;
  static const std::set<std::string> kStandIns = {"to be announced", "tba", "tbd", "no information", "off air",
                                                   "programming", "paid programming", "no guide info"};
  return kStandIns.count(t) > 0;
}

std::string showTag(const std::string& foldedTitle) {
  std::vector<std::string> w = words(foldedTitle);
  auto has = [&](const char* x) { return std::find(w.begin(), w.end(), x) != w.end(); };
  if (has("highlights") || has("recap")) return "HIGHLIGHTS";
  if (has("replay") || has("encore") || has("rewind") || has("classic") || has("classics")) return "REPLAY";
  return {};
}

// ---------------------------------------------------------------- run

namespace {

int channelScore(const Query& q, const ChannelEntry& c, Hit& hit, int& tier) {
  hit = match(q, c.name);
  int score = 0;
  tier = 9;
  auto set = [&](int s, int t) {
    if (s > score) {
      score = s;
      tier = t;
    }
  };
  switch (hit) {
    case Hit::Exact: set(95, 1); break;
    case Hit::Start: set(80, 6); break;
    case Hit::Initials: set(72, 7); break;
    case Hit::Word: set(70, 6); break;
    case Hit::Tokens: set(68, 6); break;
    case Hit::Inside: set(50, 7); break;
    case Hit::Typo: set(40, 8); break;
    case Hit::None: break;
  }
  if (q.digits && !c.number.empty()) {
    if (c.number == q.text) set(90, 1);
    else if (str::startsWith(c.number, q.text)) set(60, 6);
  }
  for (const std::string& a : c.aliases) {
    Hit h = match(q, a);
    if (h == Hit::Exact) set(85, 1);
    else if (h == Hit::Start || h == Hit::Word) set(66, 7);
    if (strength(h) > strength(hit) && h != Hit::Typo) hit = h;
  }
  if (!c.regional.empty() && !q.regional.empty()) {
    // "Bally Sports Sun" finds FanDuel Sports Sun: the same network's key.
    if (c.regional == q.regional) set(85, 1);
  }
  if (!c.network.empty() && q.tokens.size() == 2 && !c.channel.empty() && q.tokens[0] == c.network &&
      q.tokens[1] == c.channel)
    set(78, 6);  // "nbc 4"
  if (q.length() >= 2) {
    for (const std::string& s : c.stations) {
      if (s == q.text) set(85, 1);
      else if (q.length() >= 3 && str::startsWith(s, q.text)) set(75, 6);
    }
  }
  if (!score && q.length() >= 3 && c.section.find(q.text) != std::string::npos) set(20, 9);
  if (!score) return 0;
  // Another country's sports channel only when named exactly.
  if (!c.browsable && tier != 1) return 0;
  if (c.inLineup) score += 10;
  if (c.favorite) score += 5;
  return score;
}

// The order games are listed in: live and watchable, soon, later; your
// teams first; not in your channels after those; ended last.
int64_t gameOrder(const GameEntry& g, const Options& o) {
  int64_t band = 0;
  if (g.state == GameEntry::State::Ended) band = 4;
  else if (!g.watchable) band = 3;
  else if (g.state == GameEntry::State::Live) band = 0;
  else band = 1;
  int64_t when = g.state == GameEntry::State::Live ? 0 : std::max<int64_t>(0, g.start - o.now);
  return band * 100'000'000'000LL + (g.favorite ? 0 : 50'000'000'000LL) + std::min<int64_t>(when, 40'000'000'000LL);
}

bool gameLive(const GameEntry& g) { return g.state == GameEntry::State::Live; }

}  // namespace

Results run(const Index& index, const Query& q, const Options& o) {
  Results r;
  r.query = q;
  size_t len = q.length();
  if (len == 0) return r;

  // ---- channels
  struct Scored {
    Item item;
    int64_t order = 0;
  };
  std::vector<Scored> channels;
  for (const ChannelEntry& c : index.channels) {
    Hit hit = Hit::None;
    int tier = 9;
    int score = channelScore(q, c, hit, tier);
    if (!score) continue;
    Item it;
    it.kind = Kind::Channel;
    it.id = c.id;
    it.entryId = c.id;
    it.hit = hit;
    it.tier = tier;
    it.score = score;
    it.playable = true;
    channels.push_back({it, -static_cast<int64_t>(score) * 1000 + c.stationRank});
  }
  std::stable_sort(channels.begin(), channels.end(), [](const Scored& a, const Scored& b) { return a.order < b.order; });

  // ---- genres, sports and leagues ("News on now", "Basketball", "PBL")
  std::vector<Item> genres;
  auto genreHit = [&](const std::string& word) {
    if (word.empty()) return Hit::None;
    if (q.text == word) return Hit::Exact;
    if (len >= 3 && q.tokens.size() == 1 && str::startsWith(word, q.text)) return Hit::Start;
    return Hit::None;
  };
  for (const Genre& g : kGenres) {
    Hit h = Hit::None;
    for (const char* wd : g.words) h = best(h, genreHit(wd));
    if (h == Hit::None) continue;
    Item it;
    it.kind = Kind::Genre;
    it.id = g.key;
    it.name = g.title;
    it.hit = h;
    it.tier = h == Hit::Exact ? 5 : 6;
    it.score = h == Hit::Exact ? 60 : 40;
    genres.push_back(it);
  }
  for (const LeagueEntry& l : index.leagues) {
    Hit h = Hit::None;
    for (const std::string& n : l.names) {
      if (n == q.text) h = Hit::Exact;
      else if (len >= 3 && str::startsWith(n, q.text)) h = best(h, Hit::Start);
    }
    if (h == Hit::None) continue;
    Item it;
    it.kind = Kind::Genre;
    it.id = l.id;
    it.name = l.title;
    it.hit = h;
    it.tier = h == Hit::Exact ? 5 : 6;
    it.score = (h == Hit::Exact ? 60 : 40) + (l.followed ? 5 : 0) + (l.sport ? 1 : 0);
    genres.push_back(it);
  }
  std::stable_sort(genres.begin(), genres.end(), [](const Item& a, const Item& b) { return a.score > b.score; });
  std::set<std::string> genreLeagues;  // folded league words a genre result named
  for (const Item& g : genres)
    if (g.hit == Hit::Exact || g.hit == Hit::Start) genreLeagues.insert(fold(g.name));

  // ---- teams (two characters, or one for a team you follow)
  struct TeamHit {
    const TeamEntry* team;
    Hit hit;
  };
  std::vector<TeamHit> teams;
  for (const TeamEntry& t : index.teams) {
    if (len < 2 && !t.followed) continue;
    Hit h = bestOf(q, t.names);
    if (len >= 2 && !t.abbreviation.empty() && t.abbreviation == q.text) h = best(h, Hit::Initials);
    if (h == Hit::None || (len < 2 && h != Hit::Start && h != Hit::Exact)) continue;
    // A nickname is said in full ("spurs"), not as a start.
    teams.push_back({&t, h});
  }
  auto gameOf = [&](const std::string& id) -> const GameEntry* {
    auto it = index.gameById.find(id);
    return it == index.gameById.end() ? nullptr : &index.games[it->second];
  };
  auto teamRank = [&](const TeamHit& t) {
    const GameEntry* live = gameOf(t.team->liveGame);
    const GameEntry* next = gameOf(t.team->nextGame);
    int64_t band = strength(t.hit) >= strength(Hit::Initials) ? 0 : 1;
    int64_t follow = t.team->followed ? t.team->followOrder : 1'000'000;
    int64_t when = live ? 0 : next ? std::max<int64_t>(1, next->start - o.now) : 9'000'000'000LL;
    return std::make_tuple(band, follow, when, t.team->name);
  };
  std::stable_sort(teams.begin(), teams.end(), [&](const TeamHit& a, const TeamHit& b) { return teamRank(a) < teamRank(b); });

  // ---- games: by their teams, the matchup, the league and sport
  std::set<std::string> teamGames;  // games shown on a team row
  for (size_t i = 0; i < teams.size() && i < o.maxTeams; ++i) {
    if (!teams[i].team->liveGame.empty()) teamGames.insert(teams[i].team->liveGame);
    else if (!teams[i].team->nextGame.empty()) teamGames.insert(teams[i].team->nextGame);
  }
  std::map<std::string, Hit> teamHits;
  for (const TeamHit& t : teams) teamHits[t.team->key] = t.hit;
  struct GameHit {
    const GameEntry* game;
    Hit hit;
    int tier;
  };
  std::vector<GameHit> games;
  if (len >= 2) {
    for (const GameEntry& g : index.games) {
      if (g.state == GameEntry::State::Ended && !o.showScores) continue;  // finished games only with scores on
      if (!inWhen(q.when, gameLive(g), g.start, g.start + 3 * 3600, o)) continue;
      Hit h = bestOf(q, g.names);
      int tier = tierOf(h);
      int both = 0;
      for (const std::string& key : g.teams) {
        auto it = teamHits.find(key);
        if (it != teamHits.end() && strength(it->second) > strength(h)) {
          h = it->second;
          tier = tierOf(h);
        }
      }
      // Two team words ("bulls kings") put their game first.
      if (q.tokens.size() >= 2 && g.teams.size() == 2) {
        for (const std::string& key : g.teams) {
          auto it = index.teamByKey.find(key);
          if (it == index.teamByKey.end()) continue;
          for (const std::string& tok : q.tokens)
            if (tok.size() >= 2 && tokenIn(tok, index.teams[it->second].names)) {
              ++both;
              break;
            }
        }
        if (both == 2) {
          h = best(h, Hit::Tokens);
          tier = 3;
        }
      }
      if (h == Hit::None) {
        // A league or sport word lists its games.
        bool named = genreLeagues.count(g.league) || genreLeagues.count(g.leagueName) || genreLeagues.count(g.sport);
        if (!named) continue;
        h = Hit::Word;
        tier = 9;
      }
      if (g.state == GameEntry::State::Ended) tier = kNeverTop;
      games.push_back({&g, h, tier});
    }
    std::stable_sort(games.begin(), games.end(), [&](const GameHit& a, const GameHit& b) {
      bool sa = strength(a.hit) >= strength(Hit::Initials), sb = strength(b.hit) >= strength(Hit::Initials);
      if (sa != sb && (a.game->state != GameEntry::State::Ended) == (b.game->state != GameEntry::State::Ended)) return sa;
      return gameOrder(*a.game, o) < gameOrder(*b.game, o);
    });
  }

  // ---- shows: one row per title per channel
  std::vector<Scored> shows;
  if (o.shows && len >= 2) {
    for (const auto& [title, airings] : index.shows) {
      Hit h = match(q, title);
      if (h == Hit::Initials || h == Hit::Inside || h == Hit::None) continue;
      std::set<std::string> seen;
      for (size_t i = 0; i < airings.size(); ++i) {
        const Index::Airing& a = airings[i];
        if (!a.listing || a.listing->end <= o.now || seen.count(a.entryId)) continue;
        bool on = a.listing->isOn(o.now);
        if (!inWhen(q.when, on, a.listing->start, a.listing->end, o)) continue;
        auto ch = index.channelById.find(a.entryId);
        if (ch == index.channelById.end()) continue;
        const ChannelEntry& c = index.channels[ch->second];
        if (!c.browsable) continue;
        seen.insert(a.entryId);
        Item it;
        it.kind = Kind::Show;
        it.entryId = a.entryId;
        it.listing = a.listing;
        it.id = a.entryId + "|" + std::to_string(a.listing->start);
        it.name = a.listing->title;
        it.hit = h;
        it.live = on;
        it.playable = on;
        it.tier = h == Hit::Exact && on ? 4 : tierOf(h);
        for (size_t k = i + 1; k < airings.size(); ++k)
          if (airings[k].entryId == a.entryId && airings[k].listing && airings[k].listing->start > a.listing->start) {
            it.also = airings[k].listing->start;
            break;
          }
        int score = strength(h) * 100 + (on ? 30 : 0) + (c.inLineup ? 12 : 0) + (c.favorite ? 6 : 0);
        it.score = score;
        int64_t start = on ? 0 : a.listing->start - o.now;
        shows.push_back({it, -static_cast<int64_t>(score) * 10'000'000LL + std::min<int64_t>(start, 9'999'999)});
      }
    }
    std::sort(shows.begin(), shows.end(), [](const Scored& a, const Scored& b) {
      return a.order != b.order ? a.order < b.order : a.item.id < b.item.id;
    });
  }

  // ---- the top result: the best of every group
  std::optional<Item> top;
  auto consider = [&](const Item& it) {
    if (it.tier >= kNeverTop) return;
    if (!top || it.tier < top->tier || (it.tier == top->tier && it.score > top->score)) top = it;
  };
  auto gameItem = [&](const GameEntry& g, Hit h, int tier) {
    Item it;
    it.kind = Kind::Game;
    it.id = g.id;
    it.name = g.title;
    it.hit = h;
    it.tier = tier;
    it.live = gameLive(g);
    it.playable = g.watchable && it.live;
    int64_t order = gameOrder(g, o);
    it.score = 1000 - static_cast<int>(std::min<int64_t>(order / 100'000'000'000LL, 9) * 100) + (g.favorite ? 50 : 0) +
               (it.live ? 20 : 0);
    // Only something you can watch, or a later game in your channels, leads.
    if (!g.watchable || g.state == GameEntry::State::Ended || g.state == GameEntry::State::Unknown) it.tier = kNeverTop;
    return it;
  };
  for (const Scored& c : channels) consider(c.item);
  for (const Item& g : genres) consider(g);
  if (o.shows || len < 2)
    for (const Scored& s : shows) consider(s.item);
  for (const GameHit& g : games)
    if (!teamGames.count(g.game->id)) consider(gameItem(*g.game, g.hit, g.tier));
  for (size_t i = 0; i < teams.size() && i < o.maxTeams; ++i) {
    const TeamHit& t = teams[i];
    if (len < 2 && !t.team->followed) continue;
    // A team leads with its first item you can watch: its live game, else
    // its next one in your channels; else the team itself.
    const GameEntry* live = gameOf(t.team->liveGame);
    const GameEntry* next = gameOf(t.team->nextGame);
    int tier = tierOf(t.hit);
    if (live && live->watchable) {
      int liveTier = t.team->followed && t.hit != Hit::Typo ? 2 : t.hit == Hit::Exact ? 3 : tier;
      consider(gameItem(*live, t.hit, std::min(liveTier, tier)));
    } else if (next && next->watchable && next->state != GameEntry::State::Ended) {
      consider(gameItem(*next, t.hit, tier));
    } else {
      Item it;
      it.kind = Kind::Team;
      it.id = t.team->key;
      it.name = t.team->name;
      it.hit = t.hit;
      it.tier = tier;
      it.score = 10;
      consider(it);
    }
  }
  // Shows join the top result only once they're in (the band never swaps
  // after it is read): the caller waits for them past one character.
  r.top = top;

  // ---- the groups, without the top result
  auto notTop = [&](Kind k, const std::string& id) { return !(r.top && r.top->kind == k && r.top->id == id); };
  for (const Scored& c : channels) {
    if (r.channels.size() >= o.maxChannels) break;
    if (notTop(Kind::Channel, c.item.id)) r.channels.push_back(c.item);
  }
  for (const TeamHit& t : teams) {
    if (r.teams.size() >= o.maxTeams) break;
    if (!notTop(Kind::Team, t.team->key)) continue;
    Item it;
    it.kind = Kind::Team;
    it.id = t.team->key;
    it.name = t.team->name;
    it.hit = t.hit;
    it.tier = tierOf(t.hit);
    const GameEntry* live = gameOf(t.team->liveGame);
    it.live = live != nullptr;
    it.playable = live && live->watchable;
    r.teams.push_back(it);
  }
  for (const GameHit& g : games) {
    if (r.games.size() >= o.maxGames) break;
    if (teamGames.count(g.game->id) || !notTop(Kind::Game, g.game->id)) continue;
    Item it = gameItem(*g.game, g.hit, g.tier);
    it.tier = g.tier;
    r.games.push_back(it);
  }
  for (const Scored& s : shows) {
    if (r.shows.size() >= o.maxShows) break;
    if (notTop(Kind::Show, s.item.id)) r.shows.push_back(s.item);
  }
  for (const Item& g : genres) {
    if (r.genres.size() >= o.maxGenres) break;
    if (notTop(Kind::Genre, g.id)) r.genres.push_back(g);
  }
  // Names for "Showing …" and recent searches.
  if (r.top && r.top->name.empty() && r.top->kind == Kind::Channel) {
    auto it = index.channelById.find(r.top->id);
    if (it != index.channelById.end()) r.top->name = index.channels[it->second].name;
  }
  // "Showing Northbridge Bulls": only typos matched.
  if (r.top && r.top->hit == Hit::Typo && !q.exactOnly) {
    bool better = false;
    auto strong = [](const Item& i) { return strength(i.hit) > strength(Hit::Typo); };
    for (const auto* group : {&r.channels, &r.teams, &r.games, &r.shows, &r.genres})
      better = better || std::any_of(group->begin(), group->end(), strong);
    if (!better) {
      // The name the typo matched: a team's full name rather than its game.
      std::string name = r.top->name;
      for (const TeamHit& t : teams)
        if (t.hit == Hit::Typo && (t.team->liveGame == r.top->id || t.team->nextGame == r.top->id || t.team->key == r.top->id)) {
          name = t.team->name;
          break;
        }
      r.showing = name;
    }
  }
  r.nothing = len >= 2 && r.empty();
  return r;
}

// ---------------------------------------------------------------- numbers

Digits digits(const Index& index, const std::string& typed) {
  Digits d;
  if (!allDigits(typed)) return d;
  std::vector<std::pair<std::string, std::string>> longer;  // number, id
  for (const ChannelEntry& c : index.channels) {
    if (c.number.empty() || !c.browsable) continue;
    if (c.number == typed) {
      if (d.channel.empty()) d.channel = c.id;
    } else if (str::startsWith(c.number, typed)) {
      longer.push_back({c.number, c.id});
    }
    // A channel named with the digits first ("49 Network"), not the one
    // with that number.
    if (c.number != typed && str::startsWith(c.name, typed + " ")) d.waits = true;
  }
  std::sort(longer.begin(), longer.end(), [](const auto& a, const auto& b) {
    return a.first.size() != b.first.size() ? a.first.size() < b.first.size() : a.first < b.first;
  });
  for (const auto& [n, id] : longer) d.also.push_back(id);
  // The digits start a team's name ("49" -> the 49ers): wait for OK.
  for (const TeamEntry& t : index.teams) {
    for (const std::string& n : t.names) {
      if (str::startsWith(n, typed + " ") || wordStart(n, typed + " ")) {
        d.waits = true;
        if (d.team.empty()) d.team = t.name;
        break;
      }
    }
  }
  return d;
}

// ---------------------------------------------------------------- recent

bool remember(std::vector<Recent>& list, const Recent& r) {
  if (str::trim(r.name).empty() || r.id.empty()) return false;
  list.erase(std::remove_if(list.begin(), list.end(),
                            [&](const Recent& x) { return x.id == r.id || fold(x.name) == fold(r.name); }),
             list.end());
  list.insert(list.begin(), r);
  if (list.size() > kRecentKept) list.resize(kRecentKept);
  return true;
}

}  // namespace marigold::engine::search
