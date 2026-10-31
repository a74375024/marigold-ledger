// Search made faster (Build 114) finds exactly what it found before: the
// matcher on names made ready once against the Build 113 matcher, the
// allocation-free edit distance against the old one, run() in steps
// against run(), the number keys' lookups against the old scan, and the
// store's index parts made again only for what they're made from.
#include <doctest.h>

#include <algorithm>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "core/engine/search.hpp"
#include "core/engine/store.hpp"
#include "core/sports/my_sports.hpp"
#include "core/sports/types.hpp"
#include "core/util/strings.hpp"
#include "engine/loaded_store.hpp"
#include "sources/mock_provider.hpp"

using namespace marigold;
using namespace marigold::engine;
namespace se = marigold::engine::search;

namespace {

// ---------------------------------------------------------------- Build 113

namespace old {

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isLetter(char c) { return (c >= 'a' && c <= 'z') || static_cast<unsigned char>(c) >= 0x80; }
bool allDigits(std::string_view s) {
  return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return isDigit(c); });
}
bool allLetters(std::string_view s) {
  return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return isLetter(c); });
}
std::string_view bare(std::string_view s) { return str::startsWith(s, "the ") ? s.substr(4) : s; }
bool wordStart(std::string_view name, std::string_view q) {
  for (size_t i = 1; i + q.size() <= name.size(); ++i)
    if (name[i - 1] == ' ' && name.compare(i, q.size(), q) == 0) return true;
  return false;
}

int editDistance(std::string_view a, std::string_view b, int cap) {
  int n = static_cast<int>(a.size()), m = static_cast<int>(b.size());
  if (std::abs(n - m) > cap) return cap + 1;
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

bool tokensMatch(const std::vector<std::string>& tokens, const std::vector<std::string>& w, bool typos) {
  if (tokens.empty() || w.empty()) return false;
  std::vector<bool> used(w.size(), false);
  bool anyTypo = false;
  for (size_t t = 0; t < tokens.size(); ++t) {
    const std::string& tok = tokens[t];
    bool last = t + 1 == tokens.size();
    int found = -1;
    for (int pass = 0; pass < 3 && found < 0; ++pass) {
      if (pass == 2 && !typos) break;
      for (size_t i = 0; i < w.size() && found < 0; ++i) {
        if (used[i]) continue;
        if (pass == 0 && w[i] == tok) found = static_cast<int>(i);
        else if (pass == 1 && last && str::startsWith(w[i], tok)) found = static_cast<int>(i);
        else if (pass == 2 && allLetters(tok) && tok.size() >= 3) {
          int allow = se::typoAllowance(std::max(tok.size(), w[i].size()));
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

se::Hit match(const se::Query& q, std::string_view name) {
  using se::Hit;
  if (q.text.empty() || name.empty()) return Hit::None;
  std::string_view t = q.text, tb = bare(t), nb = bare(name);
  if (name == t || nb == tb || nb == t) return Hit::Exact;
  size_t len = q.length();
  if (str::startsWith(name, t) || str::startsWith(nb, tb)) return Hit::Start;
  if (len == 1) return Hit::None;
  if (wordStart(name, t)) return Hit::Word;
  std::vector<std::string> w = se::words(name);
  if (w.size() >= 2) {
    std::vector<std::string> wb = str::startsWith(name, "the ") ? std::vector<std::string>(w.begin() + 1, w.end()) : w;
    if (q.tokens.size() == 1 && allLetters(t) && t.size() <= 6 && (t == se::initials(w) || t == se::initials(wb)))
      return Hit::Initials;
    if (q.tokens.size() == 2 && allLetters(q.tokens[0]) && allDigits(q.tokens[1]) && q.tokens[0].size() >= 2 &&
        q.tokens[0] == se::initials(wb) && std::find(wb.begin(), wb.end(), q.tokens[1]) != wb.end())
      return Hit::Initials;
  }
  if (len == 2) return Hit::None;
  if (q.tokens.size() >= 2 && tokensMatch(q.tokens, w, false)) return Hit::Tokens;
  if (name.find(t) != std::string_view::npos) return Hit::Inside;
  if (!q.exactOnly && tokensMatch(q.tokens, w, true)) return Hit::Typo;
  return Hit::None;
}

se::Digits digits(const se::Index& index, const std::string& typed) {
  se::Digits d;
  if (!allDigits(typed)) return d;
  std::vector<std::pair<std::string, std::string>> longer;
  for (const se::ChannelEntry& c : index.channels) {
    if (c.number.empty() || !c.browsable) continue;
    if (c.number == typed) {
      if (d.channel.empty()) d.channel = c.id;
    } else if (str::startsWith(c.number, typed)) {
      longer.push_back({c.number, c.id});
    }
    if (c.number != typed && str::startsWith(c.name, typed + " ")) d.waits = true;
  }
  std::sort(longer.begin(), longer.end(), [](const auto& a, const auto& b) {
    return a.first.size() != b.first.size() ? a.first.size() < b.first.size() : a.first < b.first;
  });
  for (const auto& [n, id] : longer) d.also.push_back(id);
  for (const se::TeamEntry& t : index.teams) {
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

}  // namespace old

// ---------------------------------------------------------------- made-up data

constexpr time::Seconds kNow = 1'800'000'000;

std::mt19937 rng(7);
template <class T>
const T& pick(const std::vector<T>& v) {
  return v[rng() % v.size()];
}

const std::vector<std::string> kWords = {
    "law", "and", "order", "special", "victims", "unit", "jeopardy", "wheel", "of", "fortune", "the", "late",
    "show", "news", "tonight", "morning", "report", "live", "sports", "center", "nba", "basketball", "hockey",
    "premier", "league", "highlights", "classic", "kitchen", "house", "hunters", "family", "feud", "chicago",
    "fire", "med", "ncis", "criminal", "minds", "survivor", "amazing", "race", "dateline", "today", "world",
    "planet", "earth", "wild", "deadliest", "catch", "net", "sn", "2", "4", "49", "206", "nbc", "replay", "tv",
    "café", "northbridge", "bulls", "harbor", "kings", "49ers", "76ers", "golden", "girls", "a", "b", "x"};

std::string phrase(int maxWords) {
  int n = 1 + static_cast<int>(rng() % static_cast<unsigned>(maxWords));
  std::string out;
  for (int i = 0; i < n; ++i) out += (i ? " " : "") + pick(kWords);
  return out;
}

// A typed query near a name: as is, a start, a word, initials, a typo
// (a letter dropped, swapped or changed), extra words, "the".
std::string nearby(const std::string& name) {
  std::string s = name;
  switch (rng() % 9) {
    case 0: return s;
    case 1: return s.substr(0, 1 + rng() % std::max<size_t>(1, s.size()));
    case 2: {
      std::vector<std::string> w = se::words(se::fold(s));
      return w.empty() ? s : pick(w);
    }
    case 3: {
      std::string ini;
      for (const std::string& w : se::words(se::fold(s))) ini += w[0];
      return ini;
    }
    case 4:
      if (s.size() > 3) s.erase(rng() % s.size(), 1);
      return s;
    case 5:
      if (s.size() > 3) {
        size_t i = rng() % (s.size() - 1);
        std::swap(s[i], s[i + 1]);
      }
      return s;
    case 6:
      if (!s.empty()) s[rng() % s.size()] = static_cast<char>('a' + rng() % 26);
      return s;
    case 7: return "the " + s;
    default: return s + " " + pick(kWords);
  }
}

struct Data {
  se::Index index;
  std::shared_ptr<Listings> listings = std::make_shared<Listings>();
  std::vector<std::string> names;  // to make queries from
};

Data makeData() {
  Data d;
  using S = se::GameEntry::State;
  for (int i = 0; i < 400; ++i) {
    se::ChannelEntry c;
    c.id = "e|" + std::to_string(i);
    std::string raw = phrase(3) + (i % 3 ? "" : " " + std::to_string(i % 50));
    c.name = se::fold(raw);
    if (i % 5) c.number = std::to_string(i % 7 == 0 ? 2000 + i : 2 + i);
    c.section = se::fold(pick(std::vector<std::string>{"Sports", "News", "Movies", "Local"}));
    c.inLineup = i % 4 == 0;
    c.favorite = i % 11 == 0;
    c.browsable = i % 3 != 0;
    if (i % 4 == 1) c.aliases = {se::fold(phrase(3))};
    if (i % 9 == 0) c.aliases.push_back(se::fold("the " + phrase(2)));
    if (i % 13 == 0) {
      c.stations = {"w" + std::string(1, static_cast<char>('a' + i % 26)) + "bc"};
      c.network = "nbc";
      c.channel = std::to_string(i % 13);
    }
    if (i % 17 == 0) c.regional = "rsn" + std::to_string(i % 3);
    c.stationRank = static_cast<int>(rng() % 4);
    d.names.push_back(raw);
    d.index.channels.push_back(std::move(c));
  }
  for (int i = 0; i < 150; ++i) {
    sports::SportsTeam t;
    t.id = std::to_string(i);
    t.name = phrase(2) + " " + pick(std::vector<std::string>{"Bulls", "Kings", "49ers", "United", "Harbor 49s"});
    se::TeamEntry x;
    x.league = pick(std::vector<std::string>{"PBL", "NFL", "EPL"});
    x.key = x.league + "|" + t.id;
    x.name = t.name;
    x.names = se::teamNames(t, i % 7 == 0);
    x.abbreviation = se::fold(t.name.substr(0, 3));
    x.followed = i % 10 == 0;
    x.followOrder = i;
    d.names.push_back(t.name);
    d.index.teams.push_back(std::move(x));
  }
  for (int i = 0; i < 60; ++i) {
    const se::TeamEntry& a = d.index.teams[rng() % 150];
    const se::TeamEntry& b = d.index.teams[rng() % 150];
    se::GameEntry g;
    g.id = "g" + std::to_string(i);
    g.title = a.name + " at " + b.name;
    g.names = {se::fold(g.title), a.names.front(), b.names.front()};
    g.teams = {a.key, b.key};
    g.league = se::fold(a.league);
    g.sport = "basketball";
    g.state = i % 4 == 0 ? S::Live : i % 9 == 0 ? S::Ended : S::Upcoming;
    g.start = kNow + static_cast<time::Seconds>(i * 900) - 3600;
    g.watchable = i % 3 != 0;
    g.favorite = i % 8 == 0;
    d.names.push_back(g.title);
    d.index.games.push_back(std::move(g));
  }
  for (size_t i = 0; i < d.index.teams.size(); ++i) {
    for (const se::GameEntry& g : d.index.games) {
      if (std::find(g.teams.begin(), g.teams.end(), d.index.teams[i].key) == g.teams.end()) continue;
      if (g.state == S::Live) d.index.teams[i].liveGame = g.id;
      else if (g.state == S::Upcoming && d.index.teams[i].nextGame.empty()) d.index.teams[i].nextGame = g.id;
    }
  }
  se::LeagueEntry pbl;
  pbl.id = "PBL";
  pbl.title = "PBL";
  pbl.names = {"pbl", "pro basketball league"};
  pbl.followed = true;
  d.index.leagues = {pbl};
  std::vector<std::string> titles;
  for (int i = 0; i < 120; ++i) titles.push_back(i == 0 ? "Jeopardy!" : i == 1 ? "Law & Order" : phrase(4));
  for (int c = 0; c < 400; c += 2) {
    std::vector<Listing> v;
    time::Seconds t = kNow - 3600;
    for (int k = 0; k < 12; ++k) {
      Listing l;
      l.title = pick(titles);
      l.start = t;
      l.end = t + 1800 + static_cast<time::Seconds>(rng() % 3) * 900;
      t = l.end;
      v.push_back(l);
    }
    d.listings->byChannel["e|" + std::to_string(c)] = std::make_shared<const std::vector<Listing>>(std::move(v));
  }
  d.index.listings = d.listings;
  for (const auto& [id, list] : d.listings->byChannel)
    for (const Listing& l : *list) d.index.shows[se::fold(l.title)].push_back({id, &l});
  for (auto& [title, airings] : d.index.shows)
    std::sort(airings.begin(), airings.end(), [](const se::Index::Airing& a, const se::Index::Airing& b) {
      return a.listing->start != b.listing->start ? a.listing->start < b.listing->start : a.entryId < b.entryId;
    });
  for (const std::string& t : titles) d.names.push_back(t);
  d.index.finish();
  return d;
}

std::vector<std::string> queries(const Data& d, size_t n) {
  std::vector<std::string> out = {"n", "nb", "nba", "jepordy", "law and ordr", "49", "206", "sn2", "nbc 4",
                                  "news tonight", "the office", "bulls kings", "rsn", "2", "wabc", "x"};
  while (out.size() < n) out.push_back(nearby(pick(d.names)));
  return out;
}

bool sameItem(const se::Item& a, const se::Item& b) {
  return a.kind == b.kind && a.id == b.id && a.tier == b.tier && a.score == b.score && a.hit == b.hit &&
         a.name == b.name && a.entryId == b.entryId && a.listing == b.listing && a.also == b.also &&
         a.live == b.live && a.playable == b.playable;
}

bool sameItems(const std::vector<se::Item>& a, const std::vector<se::Item>& b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), sameItem);
}

bool sameResults(const se::Results& a, const se::Results& b) {
  if (a.top.has_value() != b.top.has_value() || (a.top && !sameItem(*a.top, *b.top))) return false;
  return sameItems(a.channels, b.channels) && sameItems(a.teams, b.teams) && sameItems(a.games, b.games) &&
         sameItems(a.shows, b.shows) && sameItems(a.genres, b.genres) && a.showing == b.showing &&
         a.nothing == b.nothing && a.query.text == b.query.text;
}

}  // namespace

TEST_CASE("search fast: the distance without allocating is the old one, for any pair and cap") {
  std::mt19937 r(11);
  const std::string letters = "abcdeab";
  for (int i = 0; i < 40000; ++i) {
    std::string a, b;
    size_t na = r() % 12, nb = r() % 12;
    if (i % 50 == 0) na += 60;  // past the stack rows
    for (size_t k = 0; k < na; ++k) a += letters[r() % letters.size()];
    b = a.substr(0, std::min(a.size(), nb));
    for (size_t k = 0; k < r() % 4; ++k) {
      if (b.empty() || r() % 2) b.insert(b.begin() + static_cast<std::ptrdiff_t>(r() % (b.size() + 1)), letters[r() % 7]);
      else b[r() % b.size()] = letters[r() % 7];
    }
    if (i % 3 == 0) b = std::string(r() % 8, 'a');
    int cap = static_cast<int>(r() % 5) - (i % 97 == 0 ? 1 : 0);
    REQUIRE_MESSAGE(se::editDistance(a, b, cap) == old::editDistance(a, b, cap), a << " / " << b << " cap " << cap);
    REQUIRE(se::editDistance(b, a, cap) == old::editDistance(b, a, cap));
  }
  CHECK(se::editDistance("jepordy", "jeopardy", 2) == 2);
  CHECK(se::editDistance("ab", "", 2) == old::editDistance("ab", "", 2));
}

TEST_CASE("search fast: names made ready once match as the Build 113 matcher did") {
  Data d = makeData();
  std::vector<std::string> names;
  for (const se::ChannelEntry& c : d.index.channels) {
    names.push_back(c.name);
    for (const std::string& a : c.aliases) names.push_back(a);
  }
  for (const se::TeamEntry& t : d.index.teams)
    for (const std::string& n : t.names) names.push_back(n);
  for (const se::Index::Show& s : d.index.showTitles) names.push_back(s.title.text);
  // Odd ones: "the" alone and leading, digits only, many words, spaces.
  for (const char* x : {"the", "the the x", "49", "2 4 6", "a b c d e f g h", "sports net 2", "the office",
                        "x y z w v u t s r q p o n m l k j i h g f e d c b a z y x w v u t s r q p o n m l k j i h g "
                        "f e d c b a z y x w v u t s r q",
                        " 49 lead", "trail 49 ", "a  b"})
    names.push_back(x);
  std::vector<se::Name> ready;
  for (const std::string& n : names) ready.push_back(se::Name::of(n));
  int compared = 0, hits = 0;
  for (const std::string& text : queries(d, 400)) {
    for (bool exact : {false, true}) {
      se::Query q = se::parse(text);
      q.exactOnly = exact;
      for (size_t i = 0; i < names.size(); ++i) {
        se::Hit want = old::match(q, names[i]);
        REQUIRE_MESSAGE(se::match(q, ready[i]) == want, "\"" << text << "\" on \"" << names[i] << "\"");
        REQUIRE(se::match(q, names[i]) == want);
        ++compared;
        hits += want != se::Hit::None;
      }
    }
  }
  CHECK(compared > 100000);
  CHECK(hits > 1000);
}

TEST_CASE("search fast: run() in steps gives what run() gives") {
  Data d = makeData();
  int found = 0;
  for (const std::string& text : queries(d, 600)) {
    for (bool exact : {false, true}) {
      se::Query q = se::parse(text, [&](const std::string& f) { return d.index.isTitle(f); });
      q.exactOnly = exact;
      se::Options o;
      o.now = kNow;
      o.showScores = text.size() % 2 == 0;
      o.shows = true;
      se::Results all = se::run(d.index, q, o);
      // Each letter: everything but the shows; after the pause, the shows.
      se::Options letter = o;
      letter.shows = false;
      se::Found f = se::find(d.index, q, letter);
      se::Results without = se::merge(d.index, f, {});
      CHECK(sameResults(without, se::run(d.index, q, letter)));
      se::Results with = se::merge(d.index, f, se::findShows(d.index, q, o));
      REQUIRE_MESSAGE(sameResults(with, all), "\"" << text << "\"");
      found += !all.empty();
    }
  }
  CHECK(found > 300);
}

TEST_CASE("search fast: the number keys find what the scan found, worked out once per digit") {
  Data d = makeData();
  for (int n = 0; n < 3000; ++n) {
    std::string typed = std::to_string(n % 1000);
    if (n >= 1000) typed = std::to_string(2000 + n % 500);
    if (n % 7 == 0) typed = typed.substr(0, 1);
    se::Digits a = se::digits(d.index, typed), b = old::digits(d.index, typed);
    REQUIRE_MESSAGE(a.channel == b.channel, typed);
    REQUIRE(a.also == b.also);
    REQUIRE(a.waits == b.waits);
    REQUIRE(a.team == b.team);
  }
  auto index = std::make_shared<const se::Index>(d.index);
  se::DigitsCache cache;
  const se::Digits& first = cache.of(index, "4");
  CHECK(cache.made() == 1);
  for (int frame = 0; frame < 60; ++frame) CHECK(&cache.of(index, "4") == &first);
  CHECK(cache.made() == 1);  // every frame reads what the digit made
  cache.of(index, "49");
  CHECK(cache.made() == 2);  // the next digit
  cache.of(std::make_shared<const se::Index>(d.index), "49");
  CHECK(cache.made() == 3);  // a new index
}

TEST_CASE("search fast: copies of an index share its lists, and a change copies only the one changed") {
  Data d = makeData();
  se::Index copy = d.index;
  CHECK(copy.channels.get() == d.index.channels.get());
  se::Index joined = se::Index::join(d.index, copy);
  CHECK(joined.showTitles.get() == d.index.showTitles.get());
  copy.games[0].favorite = !copy.games[0].favorite;
  CHECK(copy.games.get() != d.index.games.get());
  CHECK(copy.channels.get() == d.index.channels.get());
  CHECK(d.index.games[0].favorite != copy.games[0].favorite);
}

TEST_CASE("engine: Search's index parts are made again only for what they're made from") {
  fake::Tv tv;
  mock::Provider provider(200);
  tv.pool.answer(fake::kPlaylist, provider.m3u());
  Store store(tv.host());
  REQUIRE(fake::addPlaylist(store, tv).empty());
  // Search asks: what changed since the index made ahead is made again.
  store.searchIndex();
  fake::settle(store, tv);
  auto first = store.searchIndex();
  REQUIRE(first->channels.size() == store.channels()->all.size());
  // My Sports changes: the board's part is made again, the guide's kept.
  sports::MySportsPrefs prefs = store.mySports();
  prefs.leagues.push_back("NFL");
  prefs.configured = true;
  store.setMySports(prefs);
  store.searchIndex();  // Search asks: the part starts on a worker
  fake::settle(store, tv);
  auto second = store.searchIndex();
  CHECK(second != first);
  CHECK(second->channels.get() == first->channels.get());
  CHECK(second->showTitles.get() == first->showTitles.get());
  CHECK(second->leagues.get() != first->leagues.get());
  // A channel's favorite changes: the guide's part is made again; the
  // board's stays when the board didn't change.
  uint64_t board = store.events()->version;
  store.toggleFavorite(store.channels()->all.front().id);
  fake::settle(store, tv);
  store.searchIndex();
  fake::settle(store, tv);
  auto third = store.searchIndex();
  CHECK(third->channels.get() != second->channels.get());
  CHECK(third->channels.front().favorite != second->channels.front().favorite);
  CHECK(store.events()->version == board);
  CHECK(third->teams.get() == second->teams.get());
  // Nothing changed: the same index, nothing made.
  fake::settle(store, tv);
  CHECK(store.searchIndex() == third);
}
