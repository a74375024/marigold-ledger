#include "core/engine/search.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <set>
#include <string_view>
#include <unordered_map>
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
bool tokensMatch(const std::vector<std::string>& tokens, const Name& n, bool typos) {
  size_t nw = n.words.size();
  if (tokens.empty() || nw == 0) return false;
  // The words taken: a bit each (a list past 64 words).
  uint64_t taken = 0;
  std::vector<bool> takenMore(nw > 64 ? nw : 0, false);
  auto used = [&](size_t i) { return nw > 64 ? static_cast<bool>(takenMore[i]) : (taken >> i & 1) != 0; };
  bool anyTypo = false;
  for (size_t t = 0; t < tokens.size(); ++t) {
    const std::string& tok = tokens[t];
    bool last = t + 1 == tokens.size();
    bool typoToken = typos && tok.size() >= 3 && allLetters(tok);
    int found = -1;
    // Exact words first, then a start (last token), then a typo.
    for (int pass = 0; pass < 3 && found < 0; ++pass) {
      if (pass == 2 && !typoToken) break;
      if (pass == 1 && !last) continue;
      for (size_t i = 0; i < nw && found < 0; ++i) {
        if (used(i)) continue;
        std::string_view w = n.word(i);
        if (pass == 0) {
          if (w == tok) found = static_cast<int>(i);
        } else if (pass == 1) {
          if (str::startsWith(w, tok)) found = static_cast<int>(i);
        } else {
          int allow = typoAllowance(std::max(tok.size(), w.size()));
          size_t apart = tok.size() > w.size() ? tok.size() - w.size() : w.size() - tok.size();
          if (allow > 0 && apart <= static_cast<size_t>(allow) && editDistance(tok, w, allow) <= allow) {
            found = static_cast<int>(i);
            anyTypo = true;
          }
        }
      }
    }
    if (found < 0) return false;
    if (nw > 64) takenMore[static_cast<size_t>(found)] = true;
    else taken |= uint64_t(1) << found;
  }
  return !typos || anyTypo;
}

// The old, allocating distance: words past the stack rows' size.
int editDistanceLong(std::string_view a, std::string_view b, int cap) {
  int n = static_cast<int>(a.size()), m = static_cast<int>(b.size());
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
  // swap of two neighbours, each one edit. Three rows on the stack, only
  // the cells within `cap` of the diagonal, every value past the cap held
  // at cap + 1 (the cells outside the band are past it).
  constexpr int kMax = 62;
  if (m > kMax || cap >= kMax) return editDistanceLong(a, b, cap);
  const int top = cap + 1;
  int rows[3][kMax + 2];
  int *before = rows[0], *prev = rows[1], *cur = rows[2];
  for (int j = 0; j <= m; ++j) prev[j] = std::min(j, top);
  for (int i = 1; i <= n; ++i) {
    int lo = std::max(1, i - cap), hi = std::min(m, i + cap);
    cur[0] = std::min(i, top);
    if (lo > 1) cur[lo - 1] = top;
    if (hi < m) cur[hi + 1] = top;
    int rowBest = top;
    for (int j = lo; j <= hi; ++j) {
      size_t ui = static_cast<size_t>(i), uj = static_cast<size_t>(j);
      int cost = a[ui - 1] == b[uj - 1] ? 0 : 1;
      int v = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost, top});
      if (i > 1 && j > 1 && a[ui - 1] == b[uj - 2] && a[ui - 2] == b[uj - 1]) v = std::min(v, before[j - 2] + 1);
      cur[j] = v;
      rowBest = std::min(rowBest, v);
    }
    if (rowBest > cap) return top;
    int* spare = before;
    before = prev;
    prev = cur;
    cur = spare;
  }
  return std::min(prev[m], top);
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

Name Name::of(std::string_view folded) {
  Name n;
  n.text = std::string(folded);
  for (size_t i = 0; i < folded.size();) {
    size_t end = folded.find(' ', i);
    if (end == std::string_view::npos) end = folded.size();
    if (end > i) n.words.push_back({static_cast<uint32_t>(i), static_cast<uint32_t>(end - i)});
    i = end + 1;
  }
  n.the = str::startsWith(folded, "the ");
  for (size_t i = 0; i < n.words.size(); ++i) {
    std::string_view w = n.word(i);
    if (allDigits(w)) continue;
    n.initials += w[0];
    if (i > 0 || !n.the) n.initialsBare += w[0];
  }
  return n;
}

namespace {

// match(), with `typos` false where a typo would count for nothing (a
// channel's other names).
Hit matchName(const Query& q, const Name& n, bool typos) {
  std::string_view name = n.text;
  if (q.text.empty() || name.empty()) return Hit::None;
  std::string_view t = q.text, tb = bare(t), nb = bare(name);
  if (name == t || nb == tb || nb == t) return Hit::Exact;
  size_t len = q.length();
  if (str::startsWith(name, t) || str::startsWith(nb, tb)) return Hit::Start;
  if (len == 1) return Hit::None;
  if (wordStart(name, t)) return Hit::Word;
  if (n.words.size() >= 2) {
    // "nb" = Northbridge Bulls (without "the" too); "sn 2" = Sports Net 2.
    if (q.tokens.size() == 1 && allLetters(t) && t.size() <= 6 && (t == n.initials || t == n.initialsBare))
      return Hit::Initials;
    if (q.tokens.size() == 2 && allLetters(q.tokens[0]) && allDigits(q.tokens[1]) && q.tokens[0].size() >= 2 &&
        q.tokens[0] == n.initialsBare) {
      for (size_t i = n.the ? 1 : 0; i < n.words.size(); ++i)
        if (n.word(i) == q.tokens[1]) return Hit::Initials;
    }
  }
  if (len == 2) return Hit::None;
  if (q.tokens.size() >= 2 && tokensMatch(q.tokens, n, false)) return Hit::Tokens;
  if (name.find(t) != std::string_view::npos) return Hit::Inside;
  if (typos && !q.exactOnly && tokensMatch(q.tokens, n, true)) return Hit::Typo;
  return Hit::None;
}

}  // namespace

Hit match(const Query& q, const Name& name) { return matchName(q, name, true); }

Hit match(const Query& q, std::string_view folded) {
  if (q.text.empty() || folded.empty()) return Hit::None;
  return matchName(q, Name::of(folded), true);
}

// ---------------------------------------------------------------- index

void Index::finishChannels() {
  std::vector<ChannelEntry>& list = channels.edit();
  auto& byId = channelById.edit();
  auto& names = titles.edit();
  auto& byNumber = numbers.edit();
  auto& byDigits = digitNames.edit();
  auto& exact = strict.edit();
  byId.clear();
  names.clear();
  byNumber.clear();
  byDigits.clear();
  exact.clear();
  byId.reserve(list.size());
  names.reserve(list.size());
  for (size_t i = 0; i < list.size(); ++i) {
    ChannelEntry& c = list[i];
    uint32_t at = static_cast<uint32_t>(i);
    byId[c.id] = i;
    names.insert(c.name);
    c.forms.clear();
    if (!c.browsable) {
      // Only named exactly: looked up, never matched.
      auto key = [&](char kind, std::string_view text) {
        if (text.empty()) return;
        std::vector<uint32_t>& v = exact[std::string(1, kind) + std::string(text)];
        if (v.empty() || v.back() != at) v.push_back(at);
      };
      key('n', bare(c.name));
      for (const std::string& a : c.aliases) key('n', bare(a));
      key('#', c.number);
      key('r', c.regional);
      for (const std::string& s : c.stations) key('s', s);
      continue;
    }
    c.forms.reserve(1 + c.aliases.size());
    c.forms.push_back(Name::of(c.name));
    for (const std::string& a : c.aliases) c.forms.push_back(Name::of(a));
    if (c.number.empty()) continue;
    byNumber[c.number].push_back(at);
    const Name& n = c.forms.front();
    // "49 Network": its first word, then a space.
    if (!n.words.empty() && n.words[0].at == 0 && n.words[0].size < n.text.size() && allDigits(n.word(0)))
      byDigits[std::string(n.word(0))].push_back(at);
  }
}

void Index::finishShows() {
  std::vector<Show>& shown = showTitles.edit();
  shown.reserve(shown.size() + shows.size());
  for (auto& [title, airings] : shows) shown.push_back({Name::of(title), std::move(airings)});
  shows.clear();
  auto& names = showNames.edit();
  names.clear();
  names.reserve(shown.size());
  const auto& byId = *channelById;
  for (Show& s : shown) {
    names.insert(s.title.text);
    for (Airing& a : s.airings) {
      if (a.entryId.empty()) continue;  // placed already
      auto it = byId.find(a.entryId);
      a.channel = it == byId.end() ? kNone : static_cast<uint32_t>(it->second);
    }
  }
}

void Index::keepChannels(const Index& from) {
  channels = from.channels;
  titles = from.titles;
  channelById = from.channelById;
  numbers = from.numbers;
  digitNames = from.digitNames;
  strict = from.strict;
}

void Index::finishGames() {
  auto& byGame = gameById.edit();
  auto& byKey = teamByKey.edit();
  auto& names = teamTitles.edit();
  auto& byDigits = digitTeams.edit();
  byGame.clear();
  byKey.clear();
  names.clear();
  byDigits.clear();
  std::vector<GameEntry>& gameList = games.edit();
  for (size_t i = 0; i < gameList.size(); ++i) {
    GameEntry& g = gameList[i];
    byGame[g.id] = i;
    g.forms.clear();
    g.forms.reserve(g.names.size());
    for (const std::string& n : g.names) g.forms.push_back(Name::of(n));
  }
  std::vector<TeamEntry>& teamList = teams.edit();
  for (size_t i = 0; i < teamList.size(); ++i) {
    TeamEntry& t = teamList[i];
    byKey[t.key] = i;
    if (!t.names.empty()) names.insert(t.names.front());
    // A team's names made ready once (the store keeps its teams while the
    // lists are the same).
    if (t.forms.size() != t.names.size()) {
      t.forms.clear();
      for (const std::string& n : t.names) t.forms.push_back(Name::of(n));
    }
    for (const Name& f : t.forms) {
      // A word of digits with a space after it ("harbor 49 s"): the number
      // keys wait for OK.
      for (size_t w = 0; w < f.words.size(); ++w)
        if (f.words[w].at + f.words[w].size < f.text.size() && allDigits(f.word(w)))
          byDigits.emplace(std::string(f.word(w)), static_cast<uint32_t>(i));
    }
  }
}

Index Index::join(const Index& guide, const Index& board) {
  Index j;
  j.keepChannels(guide);
  j.showTitles = guide.showTitles;
  j.showNames = guide.showNames;
  j.listings = guide.listings;
  j.teams = board.teams;
  j.games = board.games;
  j.leagues = board.leagues;
  j.teamTitles = board.teamTitles;
  j.gameById = board.gameById;
  j.teamByKey = board.teamByKey;
  j.digitTeams = board.digitTeams;
  return j;
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
  return placeholderTitle(fold(l.title), fold(channelName));
}

bool placeholderTitle(const std::string& foldedTitle, const std::string& foldedChannel) {
  if (foldedTitle.empty() || foldedTitle == foldedChannel) return true;
  static const std::set<std::string> kStandIns = {"to be announced", "tba", "tbd", "no information", "off air",
                                                   "programming", "paid programming", "no guide info"};
  return kStandIns.count(foldedTitle) > 0;
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
  hit = c.forms.empty() ? match(q, c.name) : match(q, c.forms.front());
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
  for (size_t k = 0; k < c.aliases.size(); ++k) {
    // A typo in another name counts for nothing: none looked for.
    Hit h = k + 1 < c.forms.size() ? matchName(q, c.forms[k + 1], false) : matchName(q, Name::of(c.aliases[k]), false);
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

// Another country's channels the query names exactly (the only way they
// are found: a tier 1 hit), in guide order.
std::vector<uint32_t> strictChannels(const Index& index, const Query& q) {
  std::vector<uint32_t> out;
  auto add = [&](char kind, std::string_view text) {
    if (text.empty()) return;
    auto it = index.strict.find(std::string(1, kind) + std::string(text));
    if (it != index.strict.end()) out.insert(out.end(), it->second.begin(), it->second.end());
  };
  // An exact name: the name without "the" is the query, with or without
  // its own "the".
  add('n', bare(q.text));
  if (bare(q.text) != q.text) add('n', q.text);
  if (q.digits) add('#', q.text);
  add('r', q.regional);
  if (q.length() >= 2) add('s', q.text);
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
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

Hit bestOf(const Query& q, const std::vector<std::string>& names, const std::vector<Name>& forms) {
  if (forms.size() != names.size()) return bestOf(q, names);
  Hit h = Hit::None;
  for (const Name& n : forms) {
    h = best(h, match(q, n));
    if (h == Hit::Exact) break;
  }
  return h;
}

// The top result's rule: a lower tier, then a higher score; the first of
// equals.
bool beats(const Item& a, const std::optional<Item>& b) {
  return !b || a.tier < b->tier || (a.tier == b->tier && a.score > b->score);
}

// "entry|start", the id a show row has, made in `out` (kept between
// comparisons, so a sort allocates nothing).
std::string_view airingId(const std::string& entryId, time::Seconds start, std::string& out) {
  out.assign(entryId);
  out += '|';
  char digits[24];
  int n = std::snprintf(digits, sizeof digits, "%lld", static_cast<long long>(start));
  out.append(digits, static_cast<size_t>(std::max(0, n)));
  return out;
}

}  // namespace

Found find(const Index& index, const Query& q, const Options& o) {
  Found f;
  f.query = q;
  f.options = o;
  size_t len = q.length();
  if (len == 0) return f;

  // ---- channels: every browsable one matched, the others looked up
  struct ChannelHit {
    uint32_t at;
    Hit hit;
    int tier, score;
    int64_t order;
  };
  std::vector<ChannelHit> channels;
  std::vector<uint32_t> strict = strictChannels(index, q);
  size_t nextStrict = 0;
  const std::vector<ChannelEntry>& list = *index.channels;
  for (size_t i = 0; i < list.size(); ++i) {
    const ChannelEntry& c = list[i];
    if (!c.browsable) {
      while (nextStrict < strict.size() && strict[nextStrict] < i) ++nextStrict;
      if (nextStrict == strict.size() || strict[nextStrict] != i) continue;
    }
    Hit hit = Hit::None;
    int tier = 9;
    int score = channelScore(q, c, hit, tier);
    if (!score) continue;
    channels.push_back({static_cast<uint32_t>(i), hit, tier, score, -static_cast<int64_t>(score) * 1000 + c.stationRank});
  }
  // In order (a stable sort: guide order among equals), the first few; the
  // best by the top result's rule among all of them.
  auto listed = [](const ChannelHit& a, const ChannelHit& b) { return a.order != b.order ? a.order < b.order : a.at < b.at; };
  auto channelItem = [&](const ChannelHit& h) {
    Item it;
    it.kind = Kind::Channel;
    it.id = list[h.at].id;
    it.entryId = it.id;
    it.hit = h.hit;
    it.tier = h.tier;
    it.score = h.score;
    it.playable = true;
    return it;
  };
  if (!channels.empty()) {
    const ChannelHit* top = &channels.front();
    for (const ChannelHit& h : channels)
      if (h.tier < top->tier || (h.tier == top->tier && (h.score > top->score || (h.score == top->score && listed(h, *top)))))
        top = &h;
    f.bestChannel = channelItem(*top);
    size_t head = std::min(channels.size(), o.maxChannels + 1);
    std::partial_sort(channels.begin(), channels.begin() + static_cast<std::ptrdiff_t>(head), channels.end(), listed);
    for (size_t i = 0; i < head; ++i) f.channels.push_back(channelItem(channels[i]));
  }

  // ---- genres, sports and leagues ("News on now", "Basketball", "PBL")
  std::vector<Item>& genres = f.genres;
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
  std::vector<Found::TeamHit>& teams = f.teams;
  for (const TeamEntry& t : index.teams) {
    if (len < 2 && !t.followed) continue;
    Hit h = bestOf(q, t.names, t.forms);
    if (len >= 2 && !t.abbreviation.empty() && t.abbreviation == q.text) h = best(h, Hit::Initials);
    if (h == Hit::None || (len < 2 && h != Hit::Start && h != Hit::Exact)) continue;
    // A nickname is said in full ("spurs"), not as a start.
    teams.push_back({&t, h});
  }
  auto gameOf = [&](const std::string& id) -> const GameEntry* {
    auto it = index.gameById.find(id);
    return it == index.gameById.end() ? nullptr : &index.games[it->second];
  };
  auto teamRank = [&](const Found::TeamHit& t) {
    const GameEntry* live = gameOf(t.team->liveGame);
    const GameEntry* next = gameOf(t.team->nextGame);
    int64_t band = strength(t.hit) >= strength(Hit::Initials) ? 0 : 1;
    int64_t follow = t.team->followed ? t.team->followOrder : 1'000'000;
    int64_t when = live ? 0 : next ? std::max<int64_t>(1, next->start - o.now) : 9'000'000'000LL;
    return std::make_tuple(band, follow, when, std::string_view(t.team->name));
  };
  std::stable_sort(teams.begin(), teams.end(),
                   [&](const Found::TeamHit& a, const Found::TeamHit& b) { return teamRank(a) < teamRank(b); });

  // ---- games: by their teams, the matchup, the league and sport
  std::unordered_map<std::string_view, Hit> teamHits;
  for (const Found::TeamHit& t : teams) teamHits[t.team->key] = t.hit;
  std::vector<Found::GameHit>& games = f.games;
  if (len >= 2) {
    for (const GameEntry& g : index.games) {
      if (g.state == GameEntry::State::Ended && !o.showScores) continue;  // finished games only with scores on
      if (!inWhen(q.when, gameLive(g), g.start, g.start + 3 * 3600, o)) continue;
      Hit h = bestOf(q, g.names, g.forms);
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
    std::stable_sort(games.begin(), games.end(), [&](const Found::GameHit& a, const Found::GameHit& b) {
      bool sa = strength(a.hit) >= strength(Hit::Initials), sb = strength(b.hit) >= strength(Hit::Initials);
      if (sa != sb && (a.game->state != GameEntry::State::Ended) == (b.game->state != GameEntry::State::Ended)) return sa;
      return gameOrder(*a.game, o) < gameOrder(*b.game, o);
    });
  }
  return f;
}

FoundShows findShows(const Index& index, const Query& q, const Options& o) {
  FoundShows out;
  if (q.length() < 2) return out;
  // ---- shows: one row per title per channel
  struct ShowHit {
    uint32_t show, airing;
    Hit hit;
    bool on;
    int tier, score;
    int64_t order;
  };
  std::vector<ShowHit> hits;
  const std::vector<Index::Show>& titles = *index.showTitles;
  const std::vector<ChannelEntry>& list = *index.channels;
  // The channels a title already has a row on (the title's number + 1).
  std::vector<uint32_t> seen(list.size(), 0);
  for (size_t s = 0; s < titles.size(); ++s) {
    const Index::Show& show = titles[s];
    Hit h = match(q, show.title);
    if (h == Hit::Initials || h == Hit::Inside || h == Hit::None) continue;
    uint32_t mark = static_cast<uint32_t>(s + 1);
    for (size_t i = 0; i < show.airings.size(); ++i) {
      const Index::Airing& a = show.airings[i];
      if (!a.listing || a.listing->end <= o.now || a.channel >= list.size() || seen[a.channel] == mark) continue;
      bool on = a.listing->isOn(o.now);
      if (!inWhen(q.when, on, a.listing->start, a.listing->end, o)) continue;
      const ChannelEntry& c = list[a.channel];
      if (!c.browsable) continue;
      seen[a.channel] = mark;
      int score = strength(h) * 100 + (on ? 30 : 0) + (c.inLineup ? 12 : 0) + (c.favorite ? 6 : 0);
      int64_t start = on ? 0 : a.listing->start - o.now;
      hits.push_back({static_cast<uint32_t>(s), static_cast<uint32_t>(i), h, on, h == Hit::Exact && on ? 4 : tierOf(h),
                      score, -static_cast<int64_t>(score) * 10'000'000LL + std::min<int64_t>(start, 9'999'999)});
    }
  }
  if (hits.empty()) return out;
  auto airing = [&](const ShowHit& h) -> const Index::Airing& { return titles[h.show].airings[h.airing]; };
  // By order, then by id ("entry|start", as the rows' ids compare).
  std::string ia, ib;
  auto listed = [&](const ShowHit& a, const ShowHit& b) {
    if (a.order != b.order) return a.order < b.order;
    const Index::Airing &x = airing(a), &y = airing(b);
    return airingId(list[x.channel].id, x.listing->start, ia) < airingId(list[y.channel].id, y.listing->start, ib);
  };
  auto item = [&](const ShowHit& h) {
    const std::vector<Index::Airing>& airings = titles[h.show].airings;
    const Index::Airing& a = airings[h.airing];
    Item it;
    it.kind = Kind::Show;
    it.entryId = list[a.channel].id;
    it.listing = a.listing;
    it.id = it.entryId + "|" + std::to_string(a.listing->start);
    it.name = a.listing->title;
    it.hit = h.hit;
    it.live = h.on;
    it.playable = h.on;
    it.tier = h.tier;
    // The next airing there (same channel, the same id).
    for (size_t k = h.airing + 1; k < airings.size(); ++k)
      if (airings[k].channel == a.channel && airings[k].listing && airings[k].listing->start > a.listing->start) {
        it.also = airings[k].listing->start;
        break;
      }
    it.score = h.score;
    return it;
  };
  const ShowHit* top = &hits.front();
  for (const ShowHit& h : hits)
    if (h.tier < top->tier || (h.tier == top->tier && (h.score > top->score || (h.score == top->score && listed(h, *top)))))
      top = &h;
  out.best = item(*top);
  size_t head = std::min(hits.size(), o.maxShows + 1);
  std::partial_sort(hits.begin(), hits.begin() + static_cast<std::ptrdiff_t>(head), hits.end(), listed);
  for (size_t i = 0; i < head; ++i) out.shows.push_back(item(hits[i]));
  return out;
}

Results merge(const Index& index, const Found& found, const FoundShows& shows) {
  Results r;
  const Query& q = found.query;
  const Options& o = found.options;
  r.query = q;
  size_t len = q.length();
  if (len == 0) return r;
  const std::vector<Found::TeamHit>& teams = found.teams;
  const std::vector<Found::GameHit>& games = found.games;
  auto gameOf = [&](const std::string& id) -> const GameEntry* {
    auto it = index.gameById.find(id);
    return it == index.gameById.end() ? nullptr : &index.games[it->second];
  };
  std::set<std::string> teamGames;  // games shown on a team row
  for (size_t i = 0; i < teams.size() && i < o.maxTeams; ++i) {
    if (!teams[i].team->liveGame.empty()) teamGames.insert(teams[i].team->liveGame);
    else if (!teams[i].team->nextGame.empty()) teamGames.insert(teams[i].team->nextGame);
  }

  // ---- the top result: the best of every group
  std::optional<Item> top;
  auto consider = [&](const Item& it) {
    if (it.tier >= kNeverTop) return;
    if (beats(it, top)) top = it;
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
  if (found.bestChannel) consider(*found.bestChannel);
  for (const Item& g : found.genres) consider(g);
  if (shows.best) consider(*shows.best);
  for (const Found::GameHit& g : games)
    if (!teamGames.count(g.game->id)) consider(gameItem(*g.game, g.hit, g.tier));
  for (size_t i = 0; i < teams.size() && i < o.maxTeams; ++i) {
    const Found::TeamHit& t = teams[i];
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
  for (const Item& c : found.channels) {
    if (r.channels.size() >= o.maxChannels) break;
    if (notTop(Kind::Channel, c.id)) r.channels.push_back(c);
  }
  for (const Found::TeamHit& t : teams) {
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
  for (const Found::GameHit& g : games) {
    if (r.games.size() >= o.maxGames) break;
    if (teamGames.count(g.game->id) || !notTop(Kind::Game, g.game->id)) continue;
    Item it = gameItem(*g.game, g.hit, g.tier);
    it.tier = g.tier;
    r.games.push_back(it);
  }
  for (const Item& s : shows.shows) {
    if (r.shows.size() >= o.maxShows) break;
    if (notTop(Kind::Show, s.id)) r.shows.push_back(s);
  }
  for (const Item& g : found.genres) {
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
      for (const Found::TeamHit& t : teams)
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

Results run(const Index& index, const Query& q, const Options& o) {
  Found found = find(index, q, o);
  FoundShows shows;
  if (o.shows) shows = findShows(index, q, o);
  return merge(index, found, shows);
}

// ---------------------------------------------------------------- numbers

Digits digits(const Index& index, const std::string& typed) {
  Digits d;
  if (!allDigits(typed)) return d;
  const std::vector<ChannelEntry>& list = *index.channels;
  // Browsable channels with a number: that number, and the longer ones
  // starting with it (in guide order, then by length and number).
  const std::map<std::string, std::vector<uint32_t>>& numbers = *index.numbers;
  auto exact = numbers.find(typed);
  if (exact != numbers.end() && !exact->second.empty()) d.channel = list[exact->second.front()].id;
  std::vector<uint32_t> at;
  for (auto it = numbers.lower_bound(typed); it != numbers.end() && str::startsWith(it->first, typed); ++it)
    if (it->first != typed) at.insert(at.end(), it->second.begin(), it->second.end());
  std::sort(at.begin(), at.end());
  std::vector<std::pair<std::string, std::string>> longer;  // number, id
  longer.reserve(at.size());
  for (uint32_t i : at) longer.push_back({list[i].number, list[i].id});
  std::sort(longer.begin(), longer.end(), [](const auto& a, const auto& b) {
    return a.first.size() != b.first.size() ? a.first.size() < b.first.size() : a.first < b.first;
  });
  for (const auto& [n, id] : longer) d.also.push_back(id);
  // A channel named with the digits first ("49 Network"), not the one
  // with that number.
  auto named = index.digitNames.find(typed);
  if (named != index.digitNames.end())
    for (uint32_t i : named->second)
      if (list[i].number != typed) d.waits = true;
  // The digits start a team's name ("49" -> the 49ers): wait for OK.
  auto team = index.digitTeams.find(typed);
  if (team != index.digitTeams.end()) {
    d.waits = true;
    d.team = index.teams[team->second].name;
  }
  return d;
}

const Digits& DigitsCache::of(const std::shared_ptr<const Index>& index, const std::string& typed) {
  if (made_ == 0 || index != index_ || typed != typed_) {
    index_ = index;
    typed_ = typed;
    digits_ = index ? digits(*index, typed) : Digits{};
    ++made_;
  }
  return digits_;
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
