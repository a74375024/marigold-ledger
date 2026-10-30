#include "core/engine/views.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>

#include "core/model/channel.hpp"
#include "core/util/strings.hpp"

namespace marigold::engine::views {

namespace {

bool wordPrefix(const std::string& hay, const std::string& q) {
  for (size_t i = 0; i < hay.size(); ++i) {
    if ((i == 0 || hay[i - 1] == ' ' || hay[i - 1] == '-' || hay[i - 1] == '(') && hay.compare(i, q.size(), q) == 0)
      return true;
  }
  return false;
}

const GuideChannel* at(const Channels& ch, int idx) {
  if (idx < 0 || static_cast<size_t>(idx) >= ch.all.size()) return nullptr;
  return &ch.all[static_cast<size_t>(idx)];
}

// Adds a channel line, with a header first when the neighborhood changes.
void addChannel(List& out, const GuideChannel& g, int idx, bool headers) {
  if (headers && (out.sections.empty() || g.section != out.sections.back().title)) {
    out.sections.push_back({g.section, static_cast<int>(out.lines.size()), 0});
    out.lines.push_back({Line::Kind::Header, -1, g.section});
  }
  if (!out.sections.empty()) ++out.sections.back().count;
  out.lines.push_back({Line::Kind::Channel, idx, {}});
}

}  // namespace

const std::vector<int>& tabRows(const Channels& ch, Tab tab, const Filter& filter) {
  static const std::vector<int> kNone;
  switch (tab) {
    case Tab::Lineup: return ch.lineupConfigured ? ch.lineup : ch.browse;
    case Tab::Favorites: return ch.favorites;
    case Tab::Recent: return ch.recent;
    case Tab::Events: return kNone;  // the board is its own screen
    case Tab::All: return filter.kind == Filter::Kind::Group ? ch.every : ch.browse;
  }
  return kNone;
}

bool passes(const GuideChannel& g, const Filter& filter, const Listings* li, time::Seconds now) {
  using K = Filter::Kind;
  if (filter.kind == K::None) return true;
  if (filter.kind == K::Section) return g.section == filter.name;
  if (filter.kind == K::Group) return g.group == filter.name;
  uint8_t want = filter.kind == K::SportsNow ? kGenreSports
                 : filter.kind == K::NewsNow ? kGenreNews
                 : filter.kind == K::MoviesNow ? kGenreMovie
                                               : kGenreKids;
  const std::vector<Listing>* v = li ? li->forChannel(g.id) : nullptr;
  bool tagged = false;
  if (v) {
    for (const Listing& l : *v) {
      if (l.end <= now) continue;
      if (l.start > now + 1800) break;
      if (l.genres & want) return true;
      if (l.genres) tagged = true;
    }
  }
  if (tagged) return false;
  const char* section = want == kGenreSports ? "Sports"
                        : want == kGenreNews ? "News"
                        : want == kGenreMovie ? "Movies"
                                              : "Kids";
  return g.section.rfind(section, 0) == 0;
}

List guide(const Channels& ch, const Listings* li, Tab tab, const Filter& filter, time::Seconds now) {
  List out;
  if (tab == Tab::Lineup && !ch.lineupConfigured && !ch.all.empty()) out.lines.push_back({Line::Kind::Prompt, -1, {}});
  bool headers = tab != Tab::Recent;
  for (int idx : tabRows(ch, tab, filter)) {
    const GuideChannel* g = at(ch, idx);
    if (!g || !passes(*g, filter, li, now)) continue;
    addChannel(out, *g, idx, headers);
  }
  return out;
}

List grouped(const Channels& ch, const std::vector<std::string>& ids) {
  List out;
  for (const std::string& id : ids) {
    auto it = ch.byId.find(id);
    if (it == ch.byId.end()) continue;
    if (const GuideChannel* g = at(ch, it->second)) addChannel(out, *g, it->second, true);
  }
  return out;
}

std::vector<std::string> flipList(const Channels& ch, const List& list) {
  std::vector<std::string> out;
  for (const Line& l : list.lines) {
    if (l.kind != Line::Kind::Channel) continue;
    if (const GuideChannel* g = at(ch, l.channel)) out.push_back(g->id);
  }
  return out;
}

std::vector<Count> onNowCounts(const Channels& ch, const std::vector<int>& rows, const Listings* li, time::Seconds now) {
  using K = Filter::Kind;
  std::vector<Count> out;
  for (auto [title, kind] : {std::pair{"Sports on now", K::SportsNow}, std::pair{"News on now", K::NewsNow},
                             std::pair{"Movies on now", K::MoviesNow}, std::pair{"Kids on now", K::KidsNow}}) {
    Count c{title, Filter{kind, {}}, 0};
    for (int i : rows) {
      if (const GuideChannel* g = at(ch, i); g && passes(*g, c.filter, li, now)) ++c.count;
    }
    out.push_back(std::move(c));
  }
  return out;
}

std::vector<Count> sectionCounts(const Channels& ch, const std::vector<int>& rows) {
  // One count per section name, in the order they first come (a row out of
  // its section's block still counts with it).
  std::vector<Count> out;
  std::unordered_map<std::string, size_t> placeOf;
  for (int i : rows) {
    const GuideChannel* g = at(ch, i);
    if (!g) continue;
    auto [it, added] = placeOf.emplace(g->section, out.size());
    if (added) out.push_back({g->section, Filter{Filter::Kind::Section, g->section}, 0});
    ++out[it->second].count;
  }
  return out;
}

int programAt(const std::vector<Listing>& v, time::Seconds t, time::Seconds windowStart, time::Seconds windowEnd) {
  auto it = std::upper_bound(v.begin(), v.end(), t, [](time::Seconds x, const Listing& l) { return x < l.end; });
  if (it != v.end() && it->start < windowEnd) return static_cast<int>(it - v.begin());
  if (it != v.begin()) {
    auto prev = it - 1;
    if (prev->end > windowStart) return static_cast<int>(prev - v.begin());
  }
  return -1;
}

NowNext nowAndNext(const Listings& li, const std::string& entryId, time::Seconds now) {
  NowNext out;
  const std::vector<Listing>* v = li.forChannel(entryId);
  if (!v) return out;
  // Sorted, no overlaps: the first show still running at `now`.
  auto it = std::upper_bound(v->begin(), v->end(), now, [](time::Seconds x, const Listing& l) { return x < l.end; });
  if (it == v->end()) return out;
  if (it->isOn(now)) {
    out.now = &*it;
    if (it + 1 != v->end()) out.next = &*(it + 1);
  } else {
    out.next = &*it;
  }
  return out;
}

std::string folded(const std::string& s) { return str::lowerAscii(str::foldDiacritics(s)); }

std::vector<std::string> searchChannels(const Channels& ch, const std::string& query, size_t max) {
  std::vector<std::string> out;
  std::string q = folded(std::string(str::trim(query)));
  if (q.empty()) return out;
  std::vector<std::pair<int, size_t>> scored;  // score, index
  for (size_t i = 0; i < ch.all.size(); ++i) {
    const GuideChannel& g = ch.all[i];
    if (!g.browsable) continue;  // another country's, kept for Live Events
    std::string name = folded(g.name);
    int score = 0;
    if (name == q) score = 95;
    else if (g.number && std::to_string(*g.number) == q) score = 90;
    else if (name.compare(0, q.size(), q) == 0) score = 80;
    else if (g.style == GuideChannel::Style::Local && folded(g.subtitle).compare(0, q.size(), q) == 0) score = 75;
    else if (wordPrefix(name, q)) score = 70;
    else if (name.find(q) != std::string::npos) score = 50;
    else if (folded(g.section).find(q) != std::string::npos) score = 20;
    if (!score) continue;
    if (g.favorite) score += 5;
    if (g.inLineup) score += 10;
    scored.push_back({score, i});
  }
  std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  for (size_t i = 0; i < scored.size() && i < max; ++i) out.push_back(ch.all[scored[i].second].id);
  return out;
}

std::vector<EventLine> eventGuide(const EventsBoard& board, size_t row) {
  std::vector<EventLine> out;
  if (row >= board.rows.size()) return out;
  for (const EventGroup& g : board.rows[row].groups) {
    bool header = false;
    for (const EventTile& t : g.tiles) {
      if (!t.watchable) continue;  // the guide lists games you can watch
      if (!header && !g.title.empty()) {
        out.push_back({g.title, nullptr});
        header = true;
      }
      out.push_back({"", &t});
    }
  }
  return out;
}

bool digitsSettled(const std::string& typed, const Channels& ch) {
  if (typed.empty()) return false;
  if (typed.size() >= 4) return true;
  for (const GuideChannel& g : ch.all) {
    if (!g.number || *g.number < 0) continue;
    std::string n = std::to_string(*g.number);
    // A longer number starts with what was typed: wait for the next digit.
    if (n.size() > typed.size() && n.compare(0, typed.size(), typed) == 0) return false;
  }
  return true;
}

}  // namespace marigold::engine::views
