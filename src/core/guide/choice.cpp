#include "core/guide/choice.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

#include "core/catalog/catalog.hpp"
#include "core/catalog/reference.hpp"
#include "core/catalog/work.hpp"
#include "core/guide/genre.hpp"
#include "core/util/rx.hpp"
#include "core/util/strings.hpp"

namespace marigold::guide::choice {
namespace {

// MARK: Identity

bool asciiAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

// "UK" for "GB", "US" for "USA"; other codes as they are.
std::string countryCode(std::string code) {
  code = str::upper(code);
  if (code == "GB") return "UK";
  if (code == "USA") return "US";
  if (code == "CAN") return "CA";
  return code;
}

// The country a guide channel's name is marked with: "US: ...", "UK| ...",
// "|FR| ...", "... (Canada)".
std::string countryOfName(std::string_view name) {
  static const std::unordered_set<std::string> kNotCountries = {"TV", "HD", "SD", "FHD", "UHD", "4K", "PPV",
                                                                "VIP", "RAW", "NEW", "ALL", "THE"};
  if (auto m = rx::first("^\\s*[\\|\\[\\(]?\\s*([A-Z]{2}|USA|CAN)\\s*[\\|:\\]\\)]", name)) {
    std::string code(m->group(1));
    if (!kNotCountries.count(code)) return countryCode(code);
  }
  if (rx::test("\\(canada\\)|\\bcanada$", str::lower(name))) return "CA";
  return std::string();
}

bool hasWord(std::string_view lowered, std::string_view word) {
  size_t at = 0;
  while ((at = lowered.find(word, at)) != std::string_view::npos) {
    bool before = at == 0 || !asciiAlpha(lowered[at - 1]);
    size_t end = at + word.size();
    bool after = end >= lowered.size() || !asciiAlpha(lowered[end]);
    if (before && after) return true;
    at = end;
  }
  return false;
}

const catalog::reference::Index& index() { return catalog::reference::builtInIndex(); }

// A known station's call sign, bare ("WCBS"); empty when it isn't one.
std::string stationSign(std::string_view sign) {
  const catalog::reference::Index& ix = index();
  const catalog::reference::Station* s = ix.station(sign);
  if (!s) s = ix.formerly(sign);
  return s ? catalog::reference::bareCallsign(s->callsign) : std::string();
}

bool sameStation(const std::string& a, const std::string& b) {
  if (a == b) return true;
  const catalog::reference::Index& ix = index();
  const catalog::reference::Station* sa = ix.station(a);
  const catalog::reference::Station* sb = ix.station(b);
  auto simulcasts = [](const catalog::reference::Station* s, const std::string& other) {
    return s && !s->simulcasts.empty() && catalog::reference::bareCallsign(s->simulcasts) == other;
  };
  return simulcasts(sa, b) || simulcasts(sb, a);
}

std::string refOf(std::string_view display) {
  if (display.empty()) return std::string();
  catalog::reference::Match m = index().match(display);
  if (!m.channel) {
    if (auto core = catalog::coreMatch(display)) m = index().match(core->name);
  }
  return m.channel ? m.channel->id : std::string();
}

Identity identifyNameNow(std::string_view name) {
  Identity out;
  std::string lower = str::lower(name);
  if (namesWestFeed(lower)) out.coast = Coast::west;
  else if (hasWord(lower, "east")) out.coast = Coast::east;
  out.country = countryOfName(name);
  if (!out.country.empty() && out.country != "US") return out;
  // A station: a call sign written as one ("WCBS-DT", "CBS (WCBS)"); a
  // sub-channel ("KABC-2") is none. Then a channel by its name ("WAPA
  // America"), then a call sign written alone ("KAGS").
  for (const std::string& sign : callsigns(name, false)) {
    if (sign.find('-') != std::string::npos) continue;
    out.callsign = stationSign(sign);
    if (!out.callsign.empty()) return out;
  }
  out.ref = refOf(catalog::cleanName(name));
  if (!out.ref.empty()) return out;
  if (auto sign = bareCallsign(name)) out.callsign = stationSign(*sign);
  return out;
}

// "DiscoveryChannel" -> "Discovery Channel", "nbc30wvit" -> "nbc 30 wvit".
std::string splitWords(std::string_view base) {
  std::string out;
  for (size_t i = 0; i < base.size(); ++i) {
    char c = base[i];
    if (i > 0) {
      char p = base[i - 1];
      bool lowerToUpper = p >= 'a' && p <= 'z' && c >= 'A' && c <= 'Z';
      bool letterDigit = asciiAlpha(p) != asciiAlpha(c) && (std::isdigit(static_cast<unsigned char>(p)) ||
                                                           std::isdigit(static_cast<unsigned char>(c)));
      if (lowerToUpper || letterDigit) out += ' ';
    }
    out += (c == '_' || c == '-') ? ' ' : c;
  }
  return out;
}

// A guide id's words run together ("historychannel", "cbssportsnetworkusa"):
// the reference names squashed the same way, then without the words ids
// add at the end.
std::string squashedRef(std::string_view base) {
  static const std::unordered_map<std::string, std::string> kByKey = [] {
    std::unordered_map<std::string, std::string> map;
    std::unordered_set<std::string> shared;
    auto squash = [](std::string_view text) {
      std::string out;
      for (char c : text)
        if (std::isalnum(static_cast<unsigned char>(c))) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return out;
    };
    for (const catalog::reference::Channel& c : catalog::reference::builtIn().channels) {
      std::vector<std::string> names = c.aliases;
      names.push_back(c.name);
      for (const catalog::reference::Feed& f : c.feeds) names.insert(names.end(), f.aliases.begin(), f.aliases.end());
      for (const std::string& n : names) {
        std::string key = squash(n);
        if (key.size() < 3) continue;
        auto [it, added] = map.emplace(key, c.id);
        if (!added && it->second != c.id) shared.insert(key);
      }
    }
    for (const std::string& key : shared) map.erase(key);
    return map;
  }();
  std::string key;
  for (char c : base)
    if (std::isalnum(static_cast<unsigned char>(c))) key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  static const char* const kTails[] = {"hd", "tv", "channel", "network", "usa", "us", "east", "west", "pacific"};
  for (int round = 0; round < 4 && key.size() >= 3; ++round) {
    auto it = kByKey.find(key);
    if (it != kByKey.end()) return it->second;
    bool cut = false;
    for (const char* tail : kTails) {
      size_t len = std::char_traits<char>::length(tail);
      if (key.size() > len + 2 && key.compare(key.size() - len, len, tail) == 0) {
        key.resize(key.size() - len);
        cut = true;
        break;
      }
    }
    if (!cut) break;
  }
  return std::string();
}

Identity identifyIdNow(std::string_view guideId) {
  Identity out;
  std::string id(str::trimUnicode(guideId, true));
  size_t at = id.find('@');
  if (at != std::string::npos) id.resize(at);
  std::string base = id;
  size_t dot = id.rfind('.');
  if (dot != std::string::npos && dot > 0) {
    std::string suffix = id.substr(dot + 1);
    base = id.substr(0, dot);
    if (suffix.size() >= 2 && suffix.size() <= 3 &&
        std::all_of(suffix.begin(), suffix.end(), [](char c) { return asciiAlpha(c); }))
      out.country = countryCode(suffix);
  }
  std::string lower = str::lower(base);
  // "HBOWest", "hbowest", "DisneyPacificHD": the coast at the end, not
  // inside a word ("StarzEncoreWesterns").
  std::string end = lower;
  for (const char* drop : {"hd", "tv"})
    if (str::endsWith(end, drop) && end.size() > 6) end.resize(end.size() - 2);
  if (str::endsWith(end, "west") || str::endsWith(end, "pacific") || namesWestFeed(str::lower(splitWords(base))))
    out.coast = Coast::west;
  else if (str::endsWith(end, "east"))
    out.coast = Coast::east;
  if (!out.country.empty() && out.country != "US") return out;
  // A station: the whole id ("WCBS", "KNBC-DT") or its end ("nbc30wvit",
  // "nyclifewnye"), when the reference knows the call sign.
  std::string upper = str::upper(base);
  std::string letters;
  for (char c : upper)
    if (c != '-' && c != ' ' && c != '_') letters += c;
  if (auto m = rx::first("^([KW][A-Z]{2,3})(DT(\\d?)|TV|LD|CD|HD)?$", letters)) {
    // "WKMGDT2" is a sub-channel (Dabl), not the station.
    std::string sub = m->has(3) ? std::string(m->group(3)) : std::string();
    if (sub.empty() || sub == "1") out.callsign = stationSign(m->group(1));
    else return out;
  }
  if (!out.callsign.empty()) return out;
  // A free streaming app's own channel ("plutospiketv.us"): not the
  // network it is named after.
  static const char* const kApps[] = {"pluto", "samsung", "plex", "roku", "tubi", "xumo"};
  for (const char* app : kApps)
    if (str::startsWith(lower, app)) {
      out.ref = "~free:" + lower;
      return out;
    }
  out.ref = refOf(splitWords(base));
  if (out.ref.empty()) out.ref = squashedRef(base);
  if (!out.ref.empty() || letters.size() <= 4) return out;
  // Else a station's call sign at its end ("nbc30wvit", "nyclifewnye").
  std::string tail = letters.substr(letters.size() - 4);
  if ((tail[0] == 'K' || tail[0] == 'W') &&
      std::all_of(tail.begin(), tail.end(), [](char c) { return c >= 'A' && c <= 'Z'; }) && !isCallStopWord(tail))
    out.callsign = stationSign(tail);
  return out;
}

template <typename F>
Identity cached(std::unordered_map<std::string, Identity>& memo, std::mutex& mu, std::string_view key, F make) {
  std::string k(key);
  {
    std::lock_guard<std::mutex> lock(mu);
    auto it = memo.find(k);
    if (it != memo.end()) return it->second;
  }
  Identity made = make(key);
  std::lock_guard<std::mutex> lock(mu);
  // Names only of the channels rows asked about: a few thousand at most.
  if (memo.size() > 20000) memo.clear();
  memo.emplace(std::move(k), made);
  return made;
}

// MARK: Sources

const Source* sourceNamed(std::string_view name) {
  for (const Source& s : sources())
    if (s.name == name) return &s;
  return nullptr;
}

}  // namespace

Identity identifyName(std::string_view name) {
  static std::mutex mu;
  static std::unordered_map<std::string, Identity> memo;
  return cached(memo, mu, name, identifyNameNow);
}

Identity identifyId(std::string_view guideId) {
  static std::mutex mu;
  static std::unordered_map<std::string, Identity> memo;
  return cached(memo, mu, guideId, identifyIdNow);
}

Row rowOf(const std::string& entryId, const std::optional<std::string>& country) {
  Row row;
  row.id = entryId;
  row.west = str::endsWith(entryId, "~west");
  std::string key = str::startsWith(entryId, "e|") ? entryId.substr(2) : entryId;
  if (country) row.country = countryCode(*country);
  if (str::startsWith(key, "r:")) {
    std::string ref = key.substr(2);
    size_t tilde = ref.find('~');
    if (tilde != std::string::npos) ref.resize(tilde);
    row.ref = ref;
    row.known = true;
    const catalog::reference::Channel* c = index().channel(ref);
    row.country = c && !c->country.empty() ? countryCode(c->country) : std::string("US");
  } else if (str::startsWith(key, "cs:")) {
    std::string sign = key.substr(3);
    size_t bar = sign.find('|');
    if (bar != std::string::npos) sign.resize(bar);
    row.callsign = catalog::reference::bareCallsign(sign);
    row.known = true;
    if (row.country.empty()) row.country = "US";
  } else if (str::startsWith(key, "core:")) {
    std::string name = key.substr(5);
    size_t tilde = name.find('~');
    if (tilde != std::string::npos) name.resize(tilde);
    row.ref = refOf(name);
    if (row.country.empty()) row.country = "US";
  }
  return row;
}

// MARK: Notes

std::vector<NotePart> noteParts(std::string_view note) {
  // Parts are joined with "; " and each starts with a source's name.
  std::vector<std::pair<size_t, const Source*>> starts;
  for (size_t at = 0; at < note.size();) {
    const Source* found = nullptr;
    if (at == 0 || (at >= 2 && note.substr(at - 2, 2) == "; ")) {
      for (const Source& s : sources()) {
        if (note.substr(at, s.name.size()) == s.name && note.substr(at + s.name.size(), 2) == ": " &&
            (!found || s.name.size() > found->name.size()))
          found = &s;
      }
    }
    if (found) {
      starts.emplace_back(at, found);
      at += found->name.size();
    } else {
      size_t next = note.find("; ", at);
      if (next == std::string_view::npos) break;
      at = next + 2;
    }
  }
  std::vector<NotePart> out;
  for (size_t i = 0; i < starts.size(); ++i) {
    size_t from = starts[i].first + starts[i].second->name.size() + 2;
    size_t to = i + 1 < starts.size() ? starts[i + 1].first - 2 : note.size();
    std::string_view text = note.substr(from, to > from ? to - from : 0);
    NotePart part;
    part.source = starts[i].second->name;
    if (text.size() > 2 && text.back() == ']') {
      size_t open = text.rfind(" [");
      if (open != std::string_view::npos) {
        part.unused = true;
        text = text.substr(0, open);
      }
    }
    size_t how = text.rfind(" (by ");
    if (how != std::string_view::npos) {
      part.name = std::string(text.substr(0, how));
      std::string_view rest = text.substr(how + 2);
      size_t close = rest.find(')');
      part.how = std::string(rest.substr(0, close));
    } else {
      part.name = std::string(text);
    }
    out.push_back(std::move(part));
  }
  return out;
}

std::string providerGuideName(std::string_view note) {
  static const std::string_view kPrefix = "Provider guide: ";
  if (note.substr(0, kPrefix.size()) != kPrefix) return std::string();
  std::string_view rest = note.substr(kPrefix.size());
  size_t how = rest.rfind(" (by ");
  return std::string(how == std::string_view::npos ? rest : rest.substr(0, how));
}

// MARK: Candidates

std::vector<Candidate> candidates(const std::vector<MemberGuide>& members) {
  std::vector<Candidate> out;
  std::unordered_map<std::string, size_t> byKey;
  auto add = [&](Candidate&& c) {
    auto it = byKey.find(c.key);
    if (it != byKey.end()) {
      Candidate& have = out[it->second];
      if (!have.raw && c.raw) have.raw = c.raw;
      if ((!have.extra || have.extra->empty()) && c.extra && !c.extra->empty()) have.extra = c.extra;
      return;
    }
    if (out.size() >= kMaxCandidates) return;
    byKey.emplace(c.key, out.size());
    out.push_back(std::move(c));
  };
  // The provider's and the server's guides first.
  for (const MemberGuide& m : members) {
    bool shortList = m.server && !m.server->empty();
    if (!m.provider && !shortList) continue;
    Candidate c;
    std::string epg(str::trimUnicode(m.epgId, true));
    if (m.xtream || m.provider) {
      c.kind = "provider guide";
      c.source = "pg:" + m.serverId;
      c.key = epg.empty() ? "sv:" + m.serverId + "|" + m.id : c.source + "|" + str::lower(epg);
      c.name = providerGuideName(m.providerNote);
      c.guideId = epg;
    } else {
      c.kind = "server guide";
      c.source = "sv:" + m.serverId;
      c.key = c.source + "|" + m.id;
    }
    c.channelId = m.id;
    c.raw = m.provider;
    c.extra = shortList ? m.server : nullptr;
    c.label = c.kind;
    if (!c.name.empty()) c.label += " \"" + c.name + "\"";
    if (!c.guideId.empty()) c.label += " [" + c.guideId + "]";
    add(std::move(c));
  }
  auto outside = [&](const MemberGuide& m, const Schedule& list, const NotePart& part) {
    if (!list || list->empty()) return;
    const Source* source = sourceNamed(part.source);
    Candidate c;
    c.outside = true;
    c.kind = "outside " + (part.source.empty() ? std::string("listings") : part.source);
    c.source = "og:" + (source ? source->id() : std::string("?"));
    c.name = part.name;
    c.country = source ? source->country : std::string();
    c.shifted = part.how.find("3 h later") != std::string::npos ? kCoastShift : 0;
    c.key = c.source + "|" + str::lower(part.name) + (c.shifted ? "|+3h" : "");
    c.channelId = m.id;
    c.raw = list;
    c.label = c.kind + (c.name.empty() ? std::string() : " \"" + c.name + "\"");
    add(std::move(c));
  };
  for (const MemberGuide& m : members) {
    if (m.outside) {
      std::vector<NotePart> parts = noteParts(m.outsideNote);
      const NotePart* primary = nullptr;
      for (const NotePart& p : parts)
        if (!p.unused) {
          primary = &p;
          break;
        }
      outside(m, m.outside, primary ? *primary : NotePart());
    }
    for (const auto& [list, note] : m.others) {
      std::vector<NotePart> parts = noteParts(note);
      outside(m, list, parts.empty() ? NotePart() : parts.front());
    }
  }
  return out;
}

// MARK: Agreement

uint32_t titleKey(std::string_view t) {
  // Letters and digits in lower case, the first 12, without a "Live:" or
  // "New:" in front (a cheap form of genre::titleKey).
  auto starts = [&](std::string_view word) {
    if (t.size() <= word.size()) return false;
    for (size_t i = 0; i < word.size(); ++i)
      if (std::tolower(static_cast<unsigned char>(t[i])) != word[i]) return false;
    return t[word.size()] == ':';
  };
  if (starts("live")) t.remove_prefix(5);
  else if (starts("new")) t.remove_prefix(4);
  uint32_t h = 2166136261u;
  int n = 0;
  for (size_t i = 0; i < t.size() && n < 12; ++i) {
    unsigned char c = static_cast<unsigned char>(t[i]);
    if (c < 0x80) {
      if (!std::isalnum(c)) continue;
      c = static_cast<unsigned char>(std::tolower(c));
      ++n;
    } else if ((c & 0xC0) != 0x80) {
      ++n;
    }
    h = (h ^ c) * 16777619u;
  }
  return h;
}

std::vector<Mark> marks(const std::vector<RawProgram>* raw, const std::vector<Program>* extra, int from, int to) {
  std::vector<Mark> out;
  if (raw) {
    for (const RawProgram& p : *raw) {
      if (p.s < from || p.s >= to) continue;
      out.push_back({p.s, titleKey(p.t)});
      if (out.size() >= 3 * kMaxMarks) break;
    }
  }
  if (extra) {
    size_t own = out.size();
    for (const Program& p : *extra) {
      int s = static_cast<int>(p.start / 60);
      if (s < from || s >= to) continue;
      bool have = std::any_of(out.begin(), out.begin() + static_cast<long>(own),
                              [&](const Mark& m) { return std::abs(m.s - s) <= kStartSlack; });
      if (!have) out.push_back({s, titleKey(p.title)});
      if (out.size() >= 3 * kMaxMarks) break;
    }
  }
  std::sort(out.begin(), out.end(), [](const Mark& a, const Mark& b) { return a.s < b.s; });
  return out;
}

bool Agreement::agrees() const {
  if (compared >= kMinCompared) return matched >= kAgree * compared;
  return compared >= 1 && matched == compared;
}

bool Agreement::differs() const { return compared >= kMinCompared && matched * 10 < compared * 3; }

Agreement agreement(const std::vector<Mark>& a, int da, const std::vector<Mark>& b, int db, int from, int minutes) {
  int to = from + minutes;
  auto window = [&](const std::vector<Mark>& v, int d, size_t& first, size_t& count) {
    first = 0;
    while (first < v.size() && v[first].s + d < from) ++first;
    count = 0;
    while (first + count < v.size() && v[first + count].s + d < to && count < kMaxMarks) ++count;
  };
  size_t fa, na, fb, nb;
  window(a, da, fa, na);
  window(b, db, fb, nb);
  Agreement out;
  out.compared = static_cast<int>(std::min(na, nb));
  if (out.compared == 0) return out;
  size_t j = fb;
  int matched = 0;
  for (size_t i = fa; i < fa + na; ++i) {
    int s = a[i].s + da;
    while (j < fb + nb && b[j].s + db < s - kStartSlack) ++j;
    for (size_t k = j; k < fb + nb && b[k].s + db <= s + kStartSlack; ++k) {
      if (b[k].key == a[i].key) {
        ++matched;
        break;
      }
    }
  }
  out.matched = std::min(matched, out.compared);
  return out;
}

// MARK: Deciding

namespace {

struct Info {
  std::string reject;   // set aside, and why
  bool confirmed = false;  // its name or id is the row's channel
  bool empty = false;      // nothing in the guide's window
  Coast label = Coast::unknown;
  int timing = 0;   // 0: East times, 180: West times
  bool proven = false;  // its coast was told by lining it up
  int shift = 0;
  int64_t coverage = 0;
  int described = 0;
  std::vector<Mark> marks;
  int cluster = -1;
};

std::string coastWord(int timing) { return timing ? "West" : "East"; }

int64_t coverageOf(const Candidate& c, int shift, int lo, int hi, int* described, int dLo, int dHi) {
  auto overlap = [](int s, int e, int a, int b) { return std::max(0, std::min(e, b) - std::max(s, a)); };
  int64_t raw = 0, extra = 0;
  int dRaw = 0, dExtra = 0;
  if (c.raw) {
    for (const RawProgram& p : *c.raw) {
      int s = p.s + shift, e = p.end() + shift;
      raw += overlap(s, e, lo, hi);
      if (p.d && e > dLo && s < dHi) ++dRaw;
    }
  }
  if (c.extra) {
    for (const Program& p : *c.extra) {
      int s = static_cast<int>(p.start / 60) + shift, e = static_cast<int>(p.end / 60) + shift;
      extra += overlap(s, e, lo, hi);
      if (!p.overview.empty() && e > dLo && s < dHi) ++dExtra;
    }
  }
  if (described) *described = std::max(dRaw, dExtra);
  return std::max(raw, extra);
}

int findRoot(std::vector<int>& parent, int i) {
  while (parent[static_cast<size_t>(i)] != i) {
    parent[static_cast<size_t>(i)] = parent[static_cast<size_t>(parent[static_cast<size_t>(i)])];
    i = parent[static_cast<size_t>(i)];
  }
  return i;
}

}  // namespace

Decision decide(const Row& row, const std::vector<Candidate>& cands, int now, time::Seconds windowStart,
                const std::string& pick) {
  Decision d;
  const size_t n = std::min(cands.size(), kMaxCandidates);
  if (n == 0) {
    d.pickGone = false;
    return d;
  }
  std::vector<Info> info(n);
  const int rowTiming = row.west ? kCoastShift : 0;
  const int ws = static_cast<int>(windowStart / 60);
  // 1. Identity: another known channel, station or country is set aside.
  for (size_t i = 0; i < n; ++i) {
    const Candidate& c = cands[i];
    Info& x = info[i];
    Identity byName = c.name.empty() ? Identity() : identifyName(c.name);
    Identity byId = c.guideId.empty() ? Identity() : identifyId(c.guideId);
    x.label = byName.coast != Coast::unknown ? byName.coast : byId.coast;
    // A provider names its guide channels after one of its streams with
    // that id ("CA: BRAVO" for Bravo.us): the id picks the listings, so it
    // wins where it tells anything.
    // Its name only sets it aside: a provider's name says which stream
    // carried the id, not whose listings these are ("|US| TLC WEST" over
    // 360 North's), so only the id (or an outside listing's own name)
    // vouches for it.
    bool idTells = !byId.ref.empty() || !byId.callsign.empty();
    const Identity& who = idTells ? byId : byName;
    bool vouches = idTells || c.outside;
    std::string shown = idTells || c.name.empty() ? c.guideId : c.name;
    if (!row.ref.empty()) {
      if (!who.ref.empty() && who.ref != row.ref) x.reject = "other channel: " + shown;
      else if (who.ref.empty() && !who.callsign.empty()) x.reject = "other channel: " + shown;
      else if (who.ref == row.ref) x.confirmed = vouches;
    } else if (!row.callsign.empty()) {
      if (!who.callsign.empty() && !sameStation(who.callsign, row.callsign)) x.reject = "other station: " + shown;
      else if (!who.ref.empty()) x.reject = "other channel: " + shown;
      else if (!who.callsign.empty()) x.confirmed = vouches;
    }
  }
  // Countries: the outside source's, the guide id's ("TheWeatherNetwork.ca"),
  // else the name's.
  std::vector<std::string> country(n);
  for (size_t i = 0; i < n; ++i) {
    const Candidate& c = cands[i];
    if (!c.country.empty()) country[i] = c.country;
    else if (!c.guideId.empty()) country[i] = identifyId(c.guideId).country;
    if (country[i].empty() && !c.name.empty()) country[i] = identifyName(c.name).country;
  }
  if (!row.country.empty()) {
    // A US channel the reference knows is never another country's; any
    // other row only when it has a schedule of its own country.
    bool home = false;
    for (size_t i = 0; i < n; ++i) home = home || (info[i].reject.empty() && country[i] == row.country);
    bool certain = row.known && row.country == "US";
    for (size_t i = 0; i < n; ++i) {
      if (!info[i].reject.empty() || country[i].empty() || country[i] == row.country) continue;
      if (certain || home) info[i].reject = "other country: " + country[i];
    }
  }
  // Programmes compared: the next kCompareMinutes, with room for ±3 h.
  const int from = now - kCoastShift - 30, to = now + kCompareMinutes + kCoastShift + 30;
  for (size_t i = 0; i < n; ++i) {
    const Candidate& c = cands[i];
    info[i].marks = marks(c.raw.get(), c.extra.get(), from, to);
  }
  // 2. Coast. Outside labels are true (an unmarked one is the East feed);
  // a provider's is a claim until it lines up with a trusted one.
  for (size_t i = 0; i < n; ++i) {
    const Candidate& c = cands[i];
    Info& x = info[i];
    x.timing = c.shifted == kCoastShift || x.label == Coast::west ? kCoastShift : 0;
    // A server's own guide (Emby, Jellyfin) is the channel's, at its times.
    if (x.label == Coast::unknown && str::startsWith(c.source, "sv:")) x.timing = rowTiming;
  }
  for (size_t i = 0; i < n; ++i) {
    if (cands[i].outside || info[i].marks.empty()) continue;
    for (size_t r = 0; r < n; ++r) {
      if (!cands[r].outside || !info[r].reject.empty() || info[r].marks.empty()) continue;
      double best = -1, second = -1;
      int bestShift = 0;
      for (int shift : {0, kCoastShift, -kCoastShift}) {
        Agreement a = agreement(info[i].marks, shift, info[r].marks, 0, now);
        if (a.compared < kMinCompared) continue;
        double share = static_cast<double>(a.matched) / a.compared;
        if (share > best) {
          second = best;
          best = share;
          bestShift = shift;
        } else if (share > second) {
          second = share;
        }
      }
      if (best < kAgree || best - std::max(0.0, second) < 0.2) continue;
      int timing = info[r].timing - bestShift;
      if (timing != 0 && timing != kCoastShift) continue;
      info[i].timing = timing;
      info[i].proven = true;
      break;
    }
  }
  for (size_t i = 0; i < n; ++i) {
    Info& x = info[i];
    x.shift = rowTiming - x.timing;
    x.coverage = coverageOf(cands[i], x.shift, ws, ws + static_cast<int>(catalog::kGuideHours * 60), &x.described,
                            ws, ws + 360);
    // Nothing from an hour before the window to 13 h after it.
    int ignored = 0;
    x.empty = coverageOf(cands[i], x.shift, ws - 60, ws + 13 * 60, &ignored, 0, 0) == 0;
  }
  // 3. Distinct schedules: those that agree (once on the row's coast) are one.
  std::vector<int> parent(n);
  std::iota(parent.begin(), parent.end(), 0);
  for (size_t i = 0; i < n; ++i) {
    if (info[i].empty || !info[i].reject.empty()) continue;
    for (size_t j = i + 1; j < n; ++j) {
      if (info[j].empty || !info[j].reject.empty()) continue;
      if (findRoot(parent, static_cast<int>(i)) == findRoot(parent, static_cast<int>(j))) continue;
      if (agreement(info[i].marks, info[i].shift, info[j].marks, info[j].shift, now).agrees())
        parent[static_cast<size_t>(findRoot(parent, static_cast<int>(j)))] = findRoot(parent, static_cast<int>(i));
    }
  }
  struct Cluster {
    std::vector<size_t> members;
    std::map<std::string, int> sources;  // source -> weight (2: confirmed)
    bool trusted = false;  // an outside schedule on the row's coast as it is
    double score = 0;
    size_t lead = 0;  // its fullest list
  };
  std::vector<Cluster> clusters;
  std::unordered_map<int, size_t> clusterOf;
  for (size_t i = 0; i < n; ++i) {
    if (info[i].empty || !info[i].reject.empty()) continue;
    int root = findRoot(parent, static_cast<int>(i));
    auto it = clusterOf.find(root);
    if (it == clusterOf.end()) {
      it = clusterOf.emplace(root, clusters.size()).first;
      clusters.emplace_back();
    }
    Cluster& cl = clusters[it->second];
    cl.members.push_back(i);
    info[i].cluster = static_cast<int>(it->second);
    int& w = cl.sources[cands[i].source];
    w = std::max(w, info[i].confirmed ? 2 : 1);
    if (cands[i].outside && info[i].shift == 0) cl.trusted = true;
  }
  auto fuller = [&](size_t a, size_t b) {
    // bestGuide's rule: more of the next 3 h; about the same (10%): more descriptions.
    const Info& x = info[a];
    const Info& y = info[b];
    bool close = std::llabs(x.coverage - y.coverage) * 10 <= std::max(x.coverage, y.coverage);
    if (close && x.described != y.described) return x.described > y.described;
    if (x.coverage != y.coverage) return x.coverage > y.coverage;
    return a < b;
  };
  for (Cluster& cl : clusters) {
    for (const auto& [source, weight] : cl.sources) cl.score += weight;
    if (cl.trusted) cl.score += 0.5;
    cl.lead = cl.members.front();
    for (size_t m : cl.members)
      if (fuller(m, cl.lead)) cl.lead = m;
  }
  std::vector<size_t> order(clusters.size());
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    if (clusters[a].score != clusters[b].score) return clusters[a].score > clusters[b].score;
    return fuller(clusters[a].lead, clusters[b].lead);
  });
  // 4. The viewer's pick, while it is there.
  int pickedAt = -1;
  if (!pick.empty()) {
    for (size_t i = 0; i < n; ++i)
      if (cands[i].key == pick) pickedAt = static_cast<int>(i);
    d.pickGone = pickedAt < 0;
  }
  if (!order.empty()) d.automatic = static_cast<int>(clusters[order.front()].lead);
  const Cluster* chosenCluster = nullptr;
  if (pickedAt >= 0) {
    d.chosen = pickedAt;
    d.picked = true;
    if (info[static_cast<size_t>(pickedAt)].cluster >= 0)
      chosenCluster = &clusters[static_cast<size_t>(info[static_cast<size_t>(pickedAt)].cluster)];
  } else if (!order.empty()) {
    chosenCluster = &clusters[order.front()];
    d.chosen = static_cast<int>(chosenCluster->lead);
  }
  if (d.chosen >= 0) {
    d.shift = info[static_cast<size_t>(d.chosen)].shift;
    if (chosenCluster) {
      std::vector<size_t> rest = chosenCluster->members;
      std::stable_sort(rest.begin(), rest.end(), fuller);
      for (size_t m : rest)
        if (static_cast<int>(m) != d.chosen) d.fill.emplace_back(m, info[m].shift);
    }
  }
  // Options: the shown schedule, the other distinct ones, then those set aside.
  std::unordered_set<size_t> listed;
  auto option = [&](size_t i, bool rejected) {
    if (listed.count(i) || info[i].empty) return;
    listed.insert(i);
    d.options.push_back({i, info[i].shift, rejected});
  };
  if (d.chosen >= 0) option(static_cast<size_t>(d.chosen), !info[static_cast<size_t>(d.chosen)].reject.empty());
  for (size_t k : order) {
    const Cluster& cl = clusters[k];
    if (&cl == chosenCluster) continue;
    option(cl.lead, false);
  }
  for (size_t i = 0; i < n; ++i)
    if (!info[i].reject.empty()) option(i, true);
  // Why, for the report.
  auto plural = [](size_t count, const char* one, const char* many) {
    return std::to_string(count) + " " + (count == 1 ? one : many);
  };
  if (d.chosen >= 0) {
    const Candidate& c = cands[static_cast<size_t>(d.chosen)];
    const Info& x = info[static_cast<size_t>(d.chosen)];
    std::string why = d.picked ? "viewer's pick: " + c.kind : c.kind;
    size_t others = chosenCluster ? chosenCluster->sources.size() - 1 : 0;
    if (others > 0) why += " (agrees with " + plural(others, "other", "others") + ")";
    else if (clusters.size() > 1) why += " (the others disagree)";
    else why += " (only schedule)";
    if (clusters.size() > 1 && chosenCluster && !d.picked) {
      double next = 0;
      for (size_t k : order)
        if (&clusters[k] != chosenCluster) next = std::max(next, clusters[k].score);
      if (next == chosenCluster->score) why += ", tie broken by coverage";
    }
    if (x.label != Coast::unknown && x.proven && (x.label == Coast::west) != (x.timing == kCoastShift))
      why += ", " + coastWord(x.label == Coast::west ? kCoastShift : 0) + " label but " + coastWord(x.timing) +
             " times";
    if (x.shift > 0) why += ", shifted +3 h for West";
    else if (x.shift < 0) why += ", shifted \xE2\x88\x92" "3 h for East";
    d.why = why;
  }
  std::vector<std::string> rejected;
  for (size_t i = 0; i < n; ++i) {
    if (static_cast<int>(i) == d.chosen) continue;
    const Info& x = info[i];
    std::string reason;
    if (!x.reject.empty()) {
      reason = x.reject;
    } else if (x.empty) {
      continue;
    } else if (chosenCluster && x.cluster >= 0 && &clusters[static_cast<size_t>(x.cluster)] == chosenCluster) {
      continue;  // agrees with the shown one
    } else if (!cands[i].outside && !x.proven && x.label == Coast::west && !row.west) {
      reason = "West label, East row";
    } else if (!cands[i].outside && !x.proven && x.label == Coast::east && row.west) {
      reason = "East label, West row";
    } else if (x.cluster >= 0) {
      const Cluster& own = clusters[static_cast<size_t>(x.cluster)];
      reason = "disagrees (" + plural(own.sources.size(), "source", "sources") +
               (chosenCluster ? " vs " + std::to_string(chosenCluster->sources.size()) : std::string()) + ")";
    }
    if (x.shift > 0) reason += ", +3 h";
    else if (x.shift < 0) reason += ", \xE2\x88\x92" "3 h";
    rejected.push_back(cands[i].label + ": " + reason);
  }
  d.rejected = str::join(rejected, "; ");
  return d;
}

// MARK: Listing

std::vector<Program> programsOf(const Candidate& c, int shift, time::Seconds windowStart) {
  time::Seconds move = static_cast<time::Seconds>(shift) * 60;
  time::Seconds from = windowStart - 3600 - move;
  time::Seconds to = from + 14 * 3600;
  const char* prefix = c.outside ? "og" : "pg";
  std::vector<Program> list;
  if (c.raw) list = guide::programs(*c.raw, c.channelId, from, to, prefix, c.outside);
  if (c.extra && !c.extra->empty()) {
    std::vector<Program> shortList;
    for (const Program& p : *c.extra)
      if (p.end > from && p.start < to) shortList.push_back(p);
    if (list.empty()) list = std::move(shortList);
    else if (!shortList.empty()) list = catalog::bestListing(&list, &shortList, windowStart - move);
  }
  if (shift != 0) {
    std::string tag = "|" + std::to_string(shift);
    for (Program& p : list) {
      p.start += move;
      p.end += move;
      p.id += tag;
    }
  }
  return list;
}

namespace {

// genre::mergeDetails with the cheap title key: episode titles and
// descriptions from the same show covering more than half of a programme.
void fillDetails(std::vector<Program>& list, const std::vector<Program>& other) {
  auto has = [](const std::optional<std::string>& text) { return text && !text->empty(); };
  auto missing = [&](const Program& p) { return p.overview.empty() || !has(p.episodeTitle); };
  std::vector<std::pair<uint32_t, const Program*>> described;
  for (const Program& p : other)
    if (!p.overview.empty() || has(p.episodeTitle)) described.emplace_back(titleKey(p.title), &p);
  if (described.empty()) return;
  for (Program& program : list) {
    time::Seconds length = program.end - program.start;
    if (!missing(program) || length <= 0) continue;
    uint32_t key = titleKey(program.title);
    for (const auto& [otherKey, candidate] : described) {
      time::Seconds overlap = std::min(candidate->end, program.end) - std::max(candidate->start, program.start);
      if (otherKey != key || overlap * 2 <= length) continue;
      if (program.overview.empty()) program.overview = candidate->overview;
      if (!has(program.episodeTitle) && has(candidate->episodeTitle)) program.episodeTitle = candidate->episodeTitle;
      if (!missing(program)) break;
    }
  }
}

}  // namespace

std::vector<Program> listing(const Decision& d, const std::vector<Candidate>& cands, time::Seconds windowStart) {
  if (d.chosen < 0 || static_cast<size_t>(d.chosen) >= cands.size()) return {};
  std::vector<Program> result = programsOf(cands[static_cast<size_t>(d.chosen)], d.shift, windowStart);
  // Gaps (or programmes without a description) from the two fullest that
  // agree: the rest add nothing the first two don't.
  size_t filled = 0;
  for (const auto& [index, shift] : d.fill) {
    if (index >= cands.size() || filled >= 2) continue;
    bool gaps = result.empty();
    time::Seconds reached = windowStart - 3600;
    for (const Program& p : result) {
      if (p.start > reached + 300 || p.overview.empty()) gaps = true;
      reached = std::max(reached, p.end);
    }
    if (reached < windowStart + 12 * 3600) gaps = true;
    if (!gaps) break;
    ++filled;
    std::vector<Program> other = programsOf(cands[index], shift, windowStart);
    if (other.empty()) continue;
    if (result.empty()) {
      result = std::move(other);
      continue;
    }
    // Gaps only (an overlap of 5 minutes or less doesn't count); genre tags,
    // episode titles and descriptions where the shown one has none.
    result = genre::mergeFlags(result, other);
    fillDetails(result, other);
    size_t own = result.size();
    for (const Program& p : other) {
      bool clashes = std::any_of(result.begin(), result.begin() + static_cast<long>(own), [&](const Program& q) {
        return std::min(p.end, q.end) - std::max(p.start, q.start) > 300;
      });
      if (!clashes) result.push_back(p);
    }
    std::stable_sort(result.begin(), result.end(),
                     [](const Program& a, const Program& b) { return a.start < b.start; });
  }
  return result;
}

}  // namespace marigold::guide::choice
