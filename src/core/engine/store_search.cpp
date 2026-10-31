// What Search looks through (search.hpp's index), made from the store's
// snapshots in two parts (search_build.hpp): the guide's channels with
// their other names and its shows, and every team of the saved team lists
// joined with the board's games, the leagues and sports on the board. Each
// part is made on a worker and put in by tick(); Search keeps the one it
// has until then. Recent searches and following a team from Search.
#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <unordered_map>

#include "core/catalog/core_channels.hpp"
#include "core/catalog/reference.hpp"
#include "core/engine/search_build.hpp"
#include "core/engine/store.hpp"
#include "core/engine/store_impl.hpp"
#include "core/sports/leagues.hpp"
#include "core/sports/network_map.hpp"
#include "core/util/log.hpp"
#include "core/util/strings.hpp"

namespace marigold::engine {
namespace {

namespace se = search;

// A full guide part is made in the background at most this often (only
// while there is none, or after a guide job finished).
constexpr uint32_t kSearchWarmMs = 60000;

void addName(std::vector<std::string>& names, const std::string& name) {
  std::string f = se::fold(name);
  if (!f.empty() && std::find(names.begin(), names.end(), f) == names.end()) names.push_back(f);
}

// A local's network and channel from "NBC 4".
void networkAndChannel(const std::string& subtitle, std::string& network, std::string& channel) {
  std::vector<std::string> w = se::words(se::fold(subtitle));
  if (w.size() != 2 || w[1].empty() || !std::all_of(w[1].begin(), w[1].end(), [](char c) { return c >= '0' && c <= '9'; }))
    return;
  network = w[0];
  channel = w[1];
}

double msSince(std::chrono::steady_clock::time_point t) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

const std::shared_ptr<const se::Index>& emptyIndex() {
  static const std::shared_ptr<const se::Index> kEmpty = [] {
    auto index = std::make_shared<se::Index>();
    index->finish();
    return std::shared_ptr<const se::Index>(index);
  }();
  return kEmpty;
}

}  // namespace

// ---------------------------------------------------------------- the parts

namespace search_build {

std::shared_ptr<se::Index> guide(const GuideInput& in) {
  auto index = std::make_shared<se::Index>();
  const Channels& channels = *in.channels;
  if (in.previous && !in.quick) {
    index->keepChannels(*in.previous);
  } else {
    // ---- channels, with their other names
    const catalog::reference::Index& ref = catalog::reference::builtInIndex();
    std::map<std::string, const catalog::CoreChannel*> cores;
    std::unordered_map<std::string_view, size_t> entryInfo;  // entry id -> catalog entries index
    if (!in.quick) {
      for (const catalog::CoreChannel& c : catalog::coreChannels()) cores[c.name] = &c;
      if (in.catalog) {
        entryInfo.reserve(in.catalog->entries.size());
        for (size_t i = 0; i < in.catalog->entries.size(); ++i) entryInfo.emplace(in.catalog->entries[i].id, i);
      }
    }
    std::vector<se::ChannelEntry>& list = index->channels.edit();
    list.reserve(channels.all.size());
    for (const GuideChannel& g : channels.all) {
      se::ChannelEntry c;
      c.id = g.id;
      c.name = se::fold(g.name);
      if (g.number) c.number = std::to_string(*g.number);
      c.section = se::fold(g.section);
      c.inLineup = g.inLineup;
      c.favorite = g.favorite;
      c.browsable = g.browsable;
      if (in.quick) {
        list.push_back(std::move(c));
        continue;
      }
      c.regional = sports::regional::key(g.name).value_or("");
      if (g.style == GuideChannel::Style::Local) networkAndChannel(g.subtitle, c.network, c.channel);
      auto info = entryInfo.find(g.id);
      if (info != entryInfo.end()) {
        const catalog::EntryInfo& ei = in.catalog->entries[info->second];
        if (ei.core) {
          auto core = cores.find(*ei.core);
          if (core != cores.end())
            for (const std::string& a : core->second->aliases) addName(c.aliases, a);
        }
        if (ei.callsign) {
          std::string sign = catalog::reference::bareCallsign(*ei.callsign);
          addName(c.stations, sign);
          if (const catalog::reference::Station* st = ref.station(sign)) {
            for (const std::string& f : st->former) addName(c.stations, catalog::reference::bareCallsign(f));
            if (c.network.empty()) c.network = se::fold(st->network);
            if (c.channel.empty()) c.channel = st->number.substr(0, st->number.find('.'));
          }
        }
        c.stationRank = sports::stationRank(ei, in.markets, g.inLineup);
      }
      if (const catalog::reference::Channel* rc = ref.match(g.name).channel) {
        addName(c.aliases, rc->name);
        for (const std::string& a : rc->aliases) addName(c.aliases, a);
      }
      c.aliases.erase(std::remove(c.aliases.begin(), c.aliases.end(), c.name), c.aliases.end());
      list.push_back(std::move(c));
    }
    index->finishChannels();
  }
  if (in.quick) return index;

  // ---- shows: every airing still to end, by folded title. Each title is
  // folded once, however often it airs.
  index->listings = in.listings;
  const std::vector<se::ChannelEntry>& list = *index->channels;
  struct Title {
    std::string folded;
    bool standIn = false;
    std::vector<se::Index::Airing>* airings = nullptr;
  };
  std::unordered_map<std::string_view, Title> titles;
  for (const auto& [id, shown] : in.listings->byChannel) {
    auto at = channels.byId.find(id);
    if (!shown || at == channels.byId.end() || channels.all[static_cast<size_t>(at->second)].radio) continue;
    uint32_t channel = static_cast<uint32_t>(at->second);
    const std::string& channelName = list[channel].name;
    for (const Listing& l : *shown) {
      if (l.end <= in.now || l.end - l.start >= 12 * 3600) continue;
      auto [it, fresh] = titles.try_emplace(l.title);
      Title& t = it->second;
      if (fresh) {
        t.folded = se::fold(l.title);
        t.standIn = se::placeholderTitle(t.folded, std::string());
      }
      if (t.standIn || t.folded == channelName) continue;
      if (!t.airings) t.airings = &index->shows[t.folded];
      t.airings->push_back({std::string(), &l, channel});
    }
  }
  for (auto& [title, airings] : index->shows)
    std::sort(airings.begin(), airings.end(), [&](const se::Index::Airing& a, const se::Index::Airing& b) {
      return a.listing->start != b.listing->start ? a.listing->start < b.listing->start
                                                  : list[a.channel].id < list[b.channel].id;
    });
  index->finishShows();
  return index;
}

BoardPart board(const BoardInput& in) {
  BoardPart out;
  auto index = std::make_shared<se::Index>();
  time::Seconds now = in.now;

  // ---- games: the board's tiles, never their score text
  std::set<std::string> leaguesOnBoard, sportsOnBoard;
  std::vector<se::GameEntry>& games = index->games.edit();
  games.reserve(in.tiles.size());
  for (const BoardInput::Tile& x : in.tiles) {
    const EventTile& t = *x.tile;
    se::GameEntry g = se::gameEntry(t);
    for (const std::string& n : x.sideNames) addName(g.names, n);
    g.teams = x.teams;
    addName(g.names, x.customTitle);
    g.league = se::fold(x.leagueShort);
    if (const sports::League* l = sports::leagueByShort(x.leagueShort)) g.leagueName = se::fold(l->name);
    if (!x.leagueShort.empty()) leaguesOnBoard.insert(x.leagueShort);
    if (!t.sport.empty()) sportsOnBoard.insert(t.sport);
    games.push_back(std::move(g));
  }

  // ---- teams: every team of the saved lists (a team on a bye is found
  // too), made once per lists; then each one's games
  const sports::MySportsPrefs& prefs = in.mySports;
  std::shared_ptr<const std::vector<se::TeamEntry>> base = in.teams;
  if (!base) {
    auto made = std::make_shared<std::vector<se::TeamEntry>>();
    for (const BoardInput::League& l : in.leagues) {
      for (const sports::SportsTeam& t : l.teams) {
        se::TeamEntry x;
        x.key = sports::teamKey(l.id, t.id);
        x.name = t.name;
        x.league = l.id;
        x.leagueName = l.name;
        x.names = se::teamNames(t, l.college);
        x.abbreviation = se::fold(t.shortName.value_or(""));
        auto f = std::find(prefs.teams.begin(), prefs.teams.end(), x.key);
        x.followed = f != prefs.teams.end();
        x.followOrder = static_cast<int>(f - prefs.teams.begin());
        x.leaguePicked = l.picked;
        for (const std::string& n : x.names) x.forms.push_back(se::Name::of(n));
        made->push_back(std::move(x));
      }
    }
    base = made;
  }
  out.teams = base;
  std::unordered_map<std::string_view, std::vector<size_t>> gamesOfTeam;
  for (size_t i = 0; i < games.size(); ++i)
    for (const std::string& k : games[i].teams) gamesOfTeam[k].push_back(i);
  std::vector<se::TeamEntry>& teams = index->teams.edit();
  teams = *base;
  for (se::TeamEntry& x : teams) {
    // Its game on now, else the next in the coming week.
    auto of = gamesOfTeam.find(x.key);
    if (of == gamesOfTeam.end()) continue;
    time::Seconds next = 0;
    for (size_t gi : of->second) {
      const se::GameEntry& g = games[gi];
      if (g.state == se::GameEntry::State::Live) x.liveGame = g.id;
      else if (g.state == se::GameEntry::State::Upcoming && g.start > now && g.start < now + 7 * 86400 &&
               (next == 0 || g.start < next)) {
        next = g.start;
        x.nextGame = g.id;
      }
    }
  }

  // ---- leagues and sports with games on the board, or followed
  std::vector<se::LeagueEntry>& leagues = index->leagues.edit();
  for (const sports::League& l : sports::allLeagues()) {
    bool followed = std::find(prefs.leagues.begin(), prefs.leagues.end(), l.shortName) != prefs.leagues.end();
    if (!followed && !leaguesOnBoard.count(l.shortName)) continue;
    if (std::any_of(leagues.begin(), leagues.end(), [&](const se::LeagueEntry& x) { return x.id == l.shortName; }))
      continue;
    se::LeagueEntry x;
    x.id = l.shortName;
    x.title = l.name.empty() ? l.shortName : l.name;
    addName(x.names, l.name);
    addName(x.names, l.shortName);
    for (const std::string& k : l.keywords) addName(x.names, k);
    x.followed = followed;
    leagues.push_back(std::move(x));
  }
  for (const std::string& sport : sportsOnBoard) {
    se::LeagueEntry x;
    std::string word = str::lower(sport);
    x.id = "sport:" + word;
    x.title = word;
    if (!x.title.empty()) x.title[0] = static_cast<char>(x.title[0] - 32 * (x.title[0] >= 'a' && x.title[0] <= 'z'));
    x.sport = true;
    addName(x.names, word);
    leagues.push_back(std::move(x));
  }
  index->finishGames();
  out.index = index;
  return out;
}

}  // namespace search_build

// ---------------------------------------------------------------- the store

std::shared_ptr<const search::Index> Store::searchIndex() const {
  Impl& m = *impl_;
  installSearch();
  startSearchBuilds(true);
  return m.searchIndex ? m.searchIndex : emptyIndex();
}

void Store::startSearchBuilds(bool forSearch) const {
  Impl& m = *impl_;
  const EventsState& e = m.ev;
  auto now = time::now();
  http::Priority priority = forSearch ? http::Priority::Visible : http::Priority::Background;

  // ---- the guide's part: the channels and listings
  Impl::SearchPart& guide = m.searchGuide;
  Impl::SearchKey guideKey{channels_->version, listings_->version, 0};
  if (!guide.busy && (!guide.index || guide.quick || guide.key != guideKey)) {
    sports::StationMarkets markets = sports::stationMarkets(m.prefs);
    // The channels are kept when only the listings changed.
    bool keep = guide.index && !guide.quick && guide.key[0] == guideKey[0] && guide.catalogRevision == m.catalogRevision &&
                guide.markets == markets;
    auto in = std::make_shared<search_build::GuideInput>();
    in->channels = channels_;
    in->listings = listings_;
    in->catalog = m.catalog;
    in->markets = markets;
    in->now = now;
    if (keep) in->previous = guide.index;
    auto result = std::make_shared<Impl::SearchMade>();
    result->key = guideKey;
    result->catalogRevision = m.catalogRevision;
    result->markets = markets;
    // Search opened before any index: the channels' names first, at once.
    if (forSearch && !guide.index && !guide.quickBusy) {
      guide.quickBusy = true;
      auto quickIn = std::make_shared<search_build::GuideInput>(*in);
      quickIn->quick = true;
      auto quick = std::make_shared<Impl::SearchMade>(*result);
      quick->quick = true;
      host_.pool.post(
          [quickIn, quick] {
            auto t = std::chrono::steady_clock::now();
            quick->index = search_build::guide(*quickIn);
            quick->ms = msSince(t);
          },
          [this, quick] {
            impl_->searchGuide.quickBusy = false;
            impl_->searchGuide.quickDone = quick;
          },
          http::Priority::Visible);
    }
    guide.busy = true;
    host_.pool.post(
        [in, result] {
          auto t = std::chrono::steady_clock::now();
          result->index = search_build::guide(*in);
          result->ms = msSince(t);
        },
        [this, result] {
          impl_->searchGuide.busy = false;
          impl_->searchGuide.done = result;
        },
        priority);
  }

  // ---- the board's part: the board, the team lists and My Sports
  Impl::SearchPart& board = m.searchBoard;
  Impl::SearchKey boardKey{events_->version, static_cast<uint64_t>(e.teamsRevision),
                           static_cast<uint64_t>(e.mySportsRevision)};
  if (!board.busy && (!board.index || board.key != boardKey)) {
    auto in = std::make_shared<search_build::BoardInput>();
    in->events = events_;
    in->mySports = e.mySports;
    in->now = now;
    std::set<std::string> seen, leaguesOnBoard;
    for (const EventRow& row : events_->rows) {
      if (row.families) continue;
      for (const EventGroup& g : row.groups) {
        for (const EventTile& t : g.tiles) {
          if (!seen.insert(t.id).second) continue;
          search_build::BoardInput::Tile x;
          x.tile = &t;
          auto tile = e.board.tiles.find(t.id);
          if (tile != e.board.tiles.end()) {
            for (const sports::board::SideLine& s : tile->second.sides) {
              x.sideNames.push_back(s.fullName);
              x.sideNames.push_back(s.name);
            }
          }
          x.leagueShort = t.league;
          auto ev = e.board.events.find(t.id);
          if (ev != e.board.events.end()) {
            x.leagueShort = ev->second.leagueShort;
            if (const auto& game = ev->second.game) {
              x.teams.push_back(sports::teamKey(game->leagueShort, game->home.id));
              x.teams.push_back(sports::teamKey(game->leagueShort, game->away.id));
              if (game->customTitle) x.customTitle = *game->customTitle;
            }
          }
          if (!x.leagueShort.empty()) leaguesOnBoard.insert(x.leagueShort);
          in->tiles.push_back(std::move(x));
        }
      }
    }
    // The teams of every league with a list, kept while the lists are the
    // same.
    const sports::MySportsPrefs& prefs = e.mySports;
    std::set<std::string> leagueIds(leaguesOnBoard.begin(), leaguesOnBoard.end());
    for (const std::string& l : prefs.leagues) leagueIds.insert(l);
    for (const std::string& k : prefs.teams) leagueIds.insert(k.substr(0, k.find('|')));
    for (const auto& [l, teams] : e.leagueTeams) leagueIds.insert(l);
    for (const auto& [l, teams] : e.schedule.saved().rosters) leagueIds.insert(l);
    std::string teamsKey = std::to_string(e.teamsRevision) + "|" + std::to_string(e.scheduleRevision) + "|" +
                           std::to_string(e.mySportsRevision);
    for (const std::string& id : leagueIds) teamsKey += "|" + id;
    if (m.searchTeams && m.searchTeamsKey == teamsKey) {
      in->teams = m.searchTeams;
    } else {
      for (const std::string& id : leagueIds) {
        const sports::League* l = sports::leagueByShort(id);
        if (!l || l->fights || l->racing || l->tennis || l->golf || l->cricket) continue;
        search_build::BoardInput::League x;
        x.id = id;
        x.name = l->name;
        x.college = l->college();
        x.picked = !prefs.configured || std::find(prefs.leagues.begin(), prefs.leagues.end(), id) != prefs.leagues.end();
        x.teams = teamsOf(id);
        in->leagues.push_back(std::move(x));
      }
    }
    auto result = std::make_shared<Impl::SearchMade>();
    result->key = boardKey;
    result->teamsKey = teamsKey;
    board.busy = true;
    host_.pool.post(
        [in, result] {
          auto t = std::chrono::steady_clock::now();
          search_build::BoardPart part = search_build::board(*in);
          result->index = part.index;
          result->teams = part.teams;
          result->ms = msSince(t);
        },
        [this, result] {
          impl_->searchBoard.busy = false;
          impl_->searchBoard.done = result;
        },
        priority);
  }
}

bool Store::installSearch() const {
  Impl& m = *impl_;
  bool changed = false;
  auto put = [&](Impl::SearchPart& p, std::shared_ptr<Impl::SearchMade>& made, const char* part) {
    if (!made) return;
    std::shared_ptr<Impl::SearchMade> r = std::move(made);
    made.reset();
    // A quick part never replaces a full one.
    if (!r->index || (r->quick && p.index && !p.quick)) return;
    p.index = r->index;
    p.key = r->key;
    p.quick = r->quick;
    p.catalogRevision = r->catalogRevision;
    p.markets = r->markets;
    if (r->teams) {
      m.searchTeams = r->teams;
      m.searchTeamsKey = r->teamsKey;
    }
    const se::Index& x = *r->index;
    MLOG_I("search", "index part=%s%s ms=%.0f ch=%zu teams=%zu games=%zu titles=%zu", part, r->quick ? " quick" : "",
           r->ms, x.channels.size(), x.teams.size(), x.games.size(), x.showTitles.size());
    changed = true;
  };
  put(m.searchGuide, m.searchGuide.quickDone, "channels");
  put(m.searchGuide, m.searchGuide.done, "channels");
  put(m.searchBoard, m.searchBoard.done, "games");
  if (!changed) return false;
  const se::Index& guide = m.searchGuide.index ? *m.searchGuide.index : *emptyIndex();
  const se::Index& board = m.searchBoard.index ? *m.searchBoard.index : *emptyIndex();
  m.searchIndex = std::make_shared<const se::Index>(se::Index::join(guide, board));
  return true;
}

bool Store::searchTick(uint32_t ms) {
  Impl& m = *impl_;
  bool changed = installSearch();
  // Made ahead in the background once the guide is in, so the first open
  // doesn't wait: while there is no full index, and after a guide job.
  bool guideJobs = m.providerRunning || m.outsideRunning;
  if (m.searchGuideJobs && !guideJobs) m.searchWarm = true;
  m.searchGuideJobs = guideJobs;
  bool none = !m.searchGuide.index || m.searchGuide.quick || !m.searchBoard.index;
  if ((none || (m.searchWarm && !guideJobs)) && status_.phase == StoreStatus::Phase::Ready && !channels_->all.empty() &&
      !m.loading && *m.foregroundJobs == 0 && !*m.playingFull && ms >= m.searchWarmAt) {
    m.searchWarm = false;
    m.searchWarmAt = ms + kSearchWarmMs;
    startSearchBuilds(false);
  }
  return changed;
}

const std::vector<search::Recent>& Store::recentSearches() const { return impl_->searches; }

void Store::rememberSearch(const search::Recent& r) {
  if (!settings_.recentSearches) return;
  if (search::remember(impl_->searches, r)) saveListsLater();
}

void Store::forgetSearch(const std::string& id) {
  auto& v = impl_->searches;
  size_t before = v.size();
  v.erase(std::remove_if(v.begin(), v.end(), [&](const search::Recent& r) { return r.id == id; }), v.end());
  if (v.size() != before) saveListsLater();
}

void Store::clearSearches() {
  if (impl_->searches.empty()) return;
  impl_->searches.clear();
  saveListsLater();
}

bool Store::followsTeam(const std::string& key) const {
  const std::vector<std::string>& t = impl_->ev.mySports.teams;
  return std::find(t.begin(), t.end(), key) != t.end();
}

bool Store::toggleFollowTeam(const std::string& key) {
  sports::MySportsPrefs p = impl_->ev.mySports;
  auto it = std::find(p.teams.begin(), p.teams.end(), key);
  bool on = it == p.teams.end();
  if (on) {
    p.teams.push_back(key);
    // Its league too, so its games are looked up.
    std::string league = key.substr(0, key.find('|'));
    if (std::find(p.leagues.begin(), p.leagues.end(), league) == p.leagues.end()) p.leagues.push_back(league);
    p.configured = true;
  } else {
    p.teams.erase(it);
  }
  setMySports(p);
  return on;
}

}  // namespace marigold::engine
