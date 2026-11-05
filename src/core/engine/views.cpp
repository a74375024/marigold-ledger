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

namespace {

std::vector<std::string> idsOf(const Channels& ch, const std::vector<int>& rows) {
  std::vector<std::string> out;
  out.reserve(rows.size());
  for (int i : rows)
    if (const GuideChannel* g = at(ch, i)) out.push_back(g->id);
  return out;
}

bool holds(const std::vector<std::string>& ids, const std::string& id) {
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}

}  // namespace

std::vector<std::string> lineupList(const Channels& ch, const std::string& playing) {
  std::vector<std::string> lineup = idsOf(ch, ch.lineupConfigured ? ch.lineup : ch.browse);
  if (!ch.lineupConfigured || playing.empty() || holds(lineup, playing)) return lineup;
  std::vector<std::string> all = idsOf(ch, ch.browse);
  return holds(all, playing) ? all : lineup;
}

std::vector<std::string> playerList(const Channels& ch, const std::vector<std::string>& flip,
                                    const std::string& playing) {
  // Nothing playing: the list as long as it has a channel in it.
  bool usable = playing.empty()
                    ? std::any_of(flip.begin(), flip.end(), [&ch](const std::string& id) { return ch.byId.count(id) > 0; })
                    : ch.byId.count(playing) > 0 && holds(flip, playing);
  return usable ? flip : lineupList(ch, playing);
}

PlayerList playerOpen(const Channels& ch, const std::vector<std::string>& flip, const std::string& playing, Tab picked,
                      const std::vector<std::string>& pickedFlip) {
  std::vector<std::string> ids = playerList(ch, flip, playing);
  if (ids == flip && !flip.empty() && flip == pickedFlip && picked != Tab::Events) return {picked, std::move(ids)};
  // Played without a remembered list (Search, the main guide before):
  // named by what it holds.
  Tab tab = Tab::Lineup;
  if (!ids.empty() && ids == idsOf(ch, ch.favorites)) tab = Tab::Favorites;
  else if (ch.lineupConfigured && ids == idsOf(ch, ch.browse)) tab = Tab::All;
  return {tab, std::move(ids)};
}

std::vector<std::string> playerTab(const Channels& ch, Tab tab) {
  return tab == Tab::Events ? std::vector<std::string>{} : idsOf(ch, tabRows(ch, tab, {}));
}

List playerGrid(const Channels& ch, Tab tab, const std::vector<std::string>& ids) {
  if (tab != Tab::Recent) return grouped(ch, ids);
  List out;
  for (const std::string& id : ids) {
    auto it = ch.byId.find(id);
    if (it != ch.byId.end() && at(ch, it->second)) out.lines.push_back({Line::Kind::Channel, it->second, {}});
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

GuideTop guideTop(const std::vector<Listing>& v, int focused, time::Seconds now) {
  GuideTop out;
  if (focused < 0 || static_cast<size_t>(focused) >= v.size()) return out;
  const Listing& l = v[static_cast<size_t>(focused)];
  out.when = l.isOn(now) ? GuideTop::When::On : l.start > now ? GuideTop::When::Later : GuideTop::When::Ended;
  if (static_cast<size_t>(focused) + 1 < v.size()) out.next = &v[static_cast<size_t>(focused) + 1];
  if (out.when != GuideTop::When::On) {
    auto it = std::upper_bound(v.begin(), v.end(), now, [](time::Seconds x, const Listing& s) { return x < s.end; });
    if (it != v.end() && it->isOn(now)) out.playing = &*it;
  }
  return out;
}

std::string minutesLeft(const Listing& l, time::Seconds now) {
  if (!l.isOn(now)) return {};
  return std::to_string(static_cast<long>((l.end - now + 59) / 60)) + " min left";
}

std::string startsIn(const Listing& l, time::Seconds now) {
  if (l.start <= now) return {};
  long m = static_cast<long>((l.start - now + 59) / 60);
  if (m < 60) return "Starts in " + std::to_string(m) + " min";
  std::string out = "Starts in " + std::to_string(m / 60) + " h";
  return m % 60 ? out + " " + std::to_string(m % 60) + " min" : out;
}

std::string titleAndEpisode(const Listing& l) {
  return l.episode.empty() ? l.title : l.title + " \xc2\xb7 " + l.episode;
}

std::string nowPlaying(const Listing& playing, time::Seconds now) {
  std::string left = minutesLeft(playing, now);
  return left.empty() ? playing.title : playing.title + " \xc2\xb7 " + left;
}

EmptyRow emptyRow(bool later, bool listed, bool askedEmpty, bool onTheWay, bool savedReading) {
  if (later) return EmptyRow::NoneForThisTime;
  // Listings, just none now: they came, and say nothing about this hour.
  if (listed) return EmptyRow::NoGuideInfo;
  // A relaunch reads the saved guide in a moment: nothing is said yet.
  if (savedReading && !askedEmpty) return EmptyRow::Placeholders;
  return onTheWay ? EmptyRow::OnTheWay : EmptyRow::NoGuideInfo;
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

bool startsSoonOnChannel(const EventTile& t, time::Seconds now) {
  // Past its start but not begun (the scoreboard says so) counts too.
  return t.watchable && t.upcoming && t.channelFirst && !t.finished && t.start > 0 &&
         t.start - now <= kPlaysBeforeStart;
}

bool okPlays(const EventTile& t, time::Seconds now) {
  return (t.watchable && !t.upcoming && !t.finished) || startsSoonOnChannel(t, now);
}

const WatchSource* playingSource(const WhereToWatch& w, const std::string& playing, const std::string& picked) {
  if (playing.empty()) return nullptr;
  const WatchSource* first = nullptr;
  for (const auto& section : w.sections) {
    for (const WatchSource& s : section.second) {
      if (std::find(s.channels.begin(), s.channels.end(), playing) == s.channels.end()) continue;
      const std::string& via = s.entryId.empty() ? s.channelId : s.entryId;
      if (!picked.empty() && via == picked) return &s;
      if (!first) first = &s;
    }
  }
  return first;
}

namespace {

// boardFocus() over any rows: `n` of them, each one's id, tile count and
// tile ids.
template <class RowId, class Count, class TileId>
BoardFocus focusIn(size_t n, RowId rowIdOf, Count count, TileId tileIdOf, const std::string& rowId,
                   const std::string& tileId, int row, int tile) {
  auto find = [&](size_t r) {
    for (int i = 0, end = count(r); i < end; ++i)
      if (tileIdOf(r, i) == tileId) return i;
    return -1;
  };
  BoardFocus out;
  if (n == 0) return out;
  int sameRow = -1;
  for (size_t r = 0; r < n && sameRow < 0; ++r)
    if (!rowId.empty() && rowIdOf(r) == rowId) sameRow = static_cast<int>(r);
  if (sameRow >= 0) {
    int at = tileId.empty() ? -1 : find(static_cast<size_t>(sameRow));
    out.row = sameRow;
    out.tile = at >= 0 ? at : std::clamp(tile, 0, std::max(0, count(static_cast<size_t>(sameRow)) - 1));
    return out;
  }
  if (!tileId.empty()) {
    for (size_t r = 0; r < n; ++r) {
      int at = find(r);
      if (at >= 0) return {static_cast<int>(r), at};
    }
  }
  out.row = std::clamp(row, 0, static_cast<int>(n) - 1);
  out.tile = std::clamp(tile, 0, std::max(0, count(static_cast<size_t>(out.row)) - 1));
  return out;
}

}  // namespace

BoardFocus boardFocus(const std::vector<EventRow>& rows, const std::string& rowId, const std::string& tileId, int row,
                      int tile) {
  std::vector<ShownRow> shown;
  shown.reserve(rows.size());
  for (const EventRow& r : rows) shown.push_back(shownRow(r));
  return boardFocusShown(shown, rowId, tileId, row, tile);
}

ShownRow shownRow(const EventRow& row) {
  ShownRow out;
  out.row = &row;
  out.count = row.count;
  out.live = row.live;
  size_t n = 0;
  for (const EventGroup& g : row.groups) n += g.tiles.size();
  out.tiles.reserve(n);
  out.groups.reserve(row.groups.size());
  for (const EventGroup& g : row.groups) {
    size_t begin = out.tiles.size();
    for (const EventTile& t : g.tiles) out.tiles.push_back(&t);
    out.groups.push_back({&g, begin, out.tiles.size()});
  }
  return out;
}

ShownRow shownRow(const EventRow& row, const std::function<bool(const EventTile&)>& keep,
                  const std::function<bool(const EventTile&)>& live) {
  ShownRow out;
  out.row = &row;
  for (const EventGroup& g : row.groups) {
    size_t begin = out.tiles.size();
    for (const EventTile& t : g.tiles) {
      if (!keep(t)) continue;
      out.tiles.push_back(&t);
      if (live(t)) ++out.live;
    }
    if (out.tiles.size() > begin) out.groups.push_back({&g, begin, out.tiles.size()});
  }
  out.count = static_cast<int>(out.tiles.size());
  return out;
}

BoardFocus boardFocusShown(const std::vector<ShownRow>& rows, const std::string& rowId, const std::string& tileId,
                           int row, int tile) {
  return focusIn(
      rows.size(), [&](size_t r) -> const std::string& { return rows[r].row->id; },
      [&](size_t r) { return static_cast<int>(rows[r].tiles.size()); },
      [&](size_t r, int i) -> const std::string& { return rows[r].tiles[static_cast<size_t>(i)]->id; }, rowId, tileId,
      row, tile);
}

bool boardRowShown(const EventRow& row, const BoardShows& shows, const std::string& keep) {
  bool on = row.alwaysShown            ? true
            : row.families             ? shows.families
            : row.id == "more"          ? shows.more
            : row.id == "untimed"       ? shows.untimed
            : row.id == "unscheduled"   ? shows.unscheduled
                                        : true;
  if (on || keep.empty() || row.families) return on;
  for (const EventGroup& g : row.groups)
    for (const EventTile& t : g.tiles)
      if (t.id == keep) return true;
  return false;
}

bool boardHides(const std::vector<EventRow>& rows, const BoardShows& shows, const std::string& keep) {
  return std::any_of(rows.begin(), rows.end(), [&](const EventRow& r) { return !boardRowShown(r, shows, keep); });
}

std::vector<size_t> boardShownRows(const std::vector<EventRow>& rows, const BoardShows& shows,
                                   const std::string& keep) {
  std::vector<size_t> out;
  out.reserve(rows.size());
  for (size_t r = 0; r < rows.size(); ++r)
    if (boardRowShown(rows[r], shows, keep)) out.push_back(r);
  return out;
}

std::vector<EventRow> boardShown(const std::vector<EventRow>& rows, const BoardShows& shows, const std::string& keep) {
  std::vector<EventRow> out;
  for (size_t r : boardShownRows(rows, shows, keep)) out.push_back(rows[r]);
  return out;
}

std::vector<std::string> boardHidden(const std::vector<EventRow>& rows, const BoardShows& shows) {
  std::vector<std::string> out;
  for (const EventRow& r : rows)
    if (!boardRowShown(r, shows)) out.push_back(r.title);
  return out;
}

std::string hiddenLine(const std::vector<std::string>& titles) {
  if (titles.empty()) return {};
  if (titles.size() == 1) return "The " + titles.front() + " row is hidden.";
  std::string out;
  for (size_t i = 0; i < titles.size(); ++i)
    out += (i == 0 ? "" : i + 1 == titles.size() ? " and " : ", ") + titles[i];
  return out + " are hidden.";
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
