// What Search looks through (search.hpp's index), made from the store's
// snapshots: the guide's channels with their other names, every team of
// the saved team lists joined with the board's games, the leagues and
// sports on the board, and the guide's shows. Recent searches and
// following a team from Search. UI thread only.
#include <algorithm>
#include <map>
#include <set>

#include "core/catalog/core_channels.hpp"
#include "core/catalog/reference.hpp"
#include "core/engine/store.hpp"
#include "core/engine/store_impl.hpp"
#include "core/sports/leagues.hpp"
#include "core/sports/network_map.hpp"
#include "core/util/log.hpp"
#include "core/util/strings.hpp"

namespace marigold::engine {
namespace {

namespace se = search;

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

}  // namespace

std::shared_ptr<const search::Index> Store::searchIndex() const {
  Impl& m = *impl_;
  const EventsState& e = m.ev;
  // Made again only when what it is made from has changed.
  std::string key = std::to_string(channels_->version) + "|" + std::to_string(listings_->version) + "|" +
                    std::to_string(events_->version) + "|" + std::to_string(e.teamsRevision) + "|" +
                    sports::toJson(e.mySports);
  if (m.searchIndex && m.searchKey == key) return m.searchIndex;
  auto index = std::make_shared<search::Index>();
  time::Seconds now = time::now();

  // ---- channels, with their other names
  sports::StationMarkets markets = sports::stationMarkets(m.prefs);
  const catalog::reference::Index& ref = catalog::reference::builtInIndex();
  std::map<std::string, const catalog::CoreChannel*> cores;
  for (const catalog::CoreChannel& c : catalog::coreChannels()) cores[c.name] = &c;
  index->channels.reserve(channels_->all.size());
  for (const GuideChannel& g : channels_->all) {
    se::ChannelEntry c;
    c.id = g.id;
    c.name = se::fold(g.name);
    if (g.number) c.number = std::to_string(*g.number);
    c.section = se::fold(g.section);
    c.inLineup = g.inLineup;
    c.favorite = g.favorite;
    c.browsable = g.browsable;
    c.regional = sports::regional::key(g.name).value_or("");
    if (g.style == GuideChannel::Style::Local) networkAndChannel(g.subtitle, c.network, c.channel);
    auto info = m.entryInfo.find(g.id);
    if (info != m.entryInfo.end() && info->second < m.catalog->entries.size()) {
      const catalog::EntryInfo& ei = m.catalog->entries[info->second];
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
      c.stationRank = sports::stationRank(ei, markets, g.inLineup);
    }
    if (const catalog::reference::Channel* rc = ref.match(g.name).channel) {
      addName(c.aliases, rc->name);
      for (const std::string& a : rc->aliases) addName(c.aliases, a);
    }
    c.aliases.erase(std::remove(c.aliases.begin(), c.aliases.end(), c.name), c.aliases.end());
    index->channels.push_back(std::move(c));
  }

  // ---- games: the board's tiles, never their score text
  std::set<std::string> seen;
  std::set<std::string> leaguesOnBoard, sportsOnBoard;
  for (const EventRow& row : events_->rows) {
    if (row.families) continue;
    for (const EventGroup& g : row.groups) {
      for (const EventTile& t : g.tiles) {
        if (!seen.insert(t.id).second) continue;
        se::GameEntry x = se::gameEntry(t);
        auto tile = e.board.tiles.find(t.id);
        if (tile != e.board.tiles.end()) {
          for (const sports::board::SideLine& s : tile->second.sides) {
            addName(x.names, s.fullName);
            addName(x.names, s.name);
          }
        }
        auto ev = e.board.events.find(t.id);
        std::string leagueShort = t.league;
        if (ev != e.board.events.end()) {
          leagueShort = ev->second.leagueShort;
          if (const auto& game = ev->second.game) {
            x.teams.push_back(sports::teamKey(game->leagueShort, game->home.id));
            x.teams.push_back(sports::teamKey(game->leagueShort, game->away.id));
            if (game->customTitle) addName(x.names, *game->customTitle);
          }
        }
        x.league = se::fold(leagueShort);
        if (const sports::League* l = sports::leagueByShort(leagueShort)) x.leagueName = se::fold(l->name);
        if (!leagueShort.empty()) leaguesOnBoard.insert(leagueShort);
        if (!t.sport.empty()) sportsOnBoard.insert(t.sport);
        index->games.push_back(std::move(x));
      }
    }
  }

  // ---- teams: every team of the saved lists (a team on a bye is found too)
  const sports::MySportsPrefs& prefs = e.mySports;
  std::set<std::string> leagueIds(leaguesOnBoard.begin(), leaguesOnBoard.end());
  for (const std::string& l : prefs.leagues) leagueIds.insert(l);
  for (const std::string& k : prefs.teams) leagueIds.insert(k.substr(0, k.find('|')));
  for (const auto& [l, teams] : e.leagueTeams) leagueIds.insert(l);
  for (const auto& [l, teams] : e.schedule.saved().rosters) leagueIds.insert(l);
  std::map<std::string, std::vector<size_t>> gamesOfTeam;
  for (size_t i = 0; i < index->games.size(); ++i)
    for (const std::string& k : index->games[i].teams) gamesOfTeam[k].push_back(i);
  for (const std::string& id : leagueIds) {
    const sports::League* l = sports::leagueByShort(id);
    if (!l || l->fights || l->racing || l->tennis || l->golf || l->cricket) continue;
    bool picked = !prefs.configured || std::find(prefs.leagues.begin(), prefs.leagues.end(), id) != prefs.leagues.end();
    for (const sports::SportsTeam& t : teamsOf(id)) {
      se::TeamEntry x;
      x.key = sports::teamKey(id, t.id);
      x.name = t.name;
      x.league = id;
      x.leagueName = l->name;
      x.names = se::teamNames(t, l->college());
      x.abbreviation = se::fold(t.shortName.value_or(""));
      auto f = std::find(prefs.teams.begin(), prefs.teams.end(), x.key);
      x.followed = f != prefs.teams.end();
      x.followOrder = static_cast<int>(f - prefs.teams.begin());
      x.leaguePicked = picked;
      // Its game on now, else the next in the coming week.
      time::Seconds next = 0;
      for (size_t gi : gamesOfTeam[x.key]) {
        const se::GameEntry& g = index->games[gi];
        if (g.state == se::GameEntry::State::Live) x.liveGame = g.id;
        else if (g.state == se::GameEntry::State::Upcoming && g.start > now && g.start < now + 7 * 86400 &&
                 (next == 0 || g.start < next)) {
          next = g.start;
          x.nextGame = g.id;
        }
      }
      index->teams.push_back(std::move(x));
    }
  }

  // ---- leagues and sports with games on the board, or followed
  for (const sports::League& l : sports::allLeagues()) {
    bool followed = std::find(prefs.leagues.begin(), prefs.leagues.end(), l.shortName) != prefs.leagues.end();
    if (!followed && !leaguesOnBoard.count(l.shortName)) continue;
    if (std::any_of(index->leagues.begin(), index->leagues.end(), [&](const se::LeagueEntry& x) { return x.id == l.shortName; }))
      continue;
    se::LeagueEntry x;
    x.id = l.shortName;
    x.title = l.name.empty() ? l.shortName : l.name;
    addName(x.names, l.name);
    addName(x.names, l.shortName);
    for (const std::string& k : l.keywords) addName(x.names, k);
    x.followed = followed;
    index->leagues.push_back(std::move(x));
  }
  for (const std::string& sport : sportsOnBoard) {
    se::LeagueEntry x;
    std::string word = str::lower(sport);
    x.id = "sport:" + word;
    x.title = word;
    if (!x.title.empty()) x.title[0] = static_cast<char>(x.title[0] - 32 * (x.title[0] >= 'a' && x.title[0] <= 'z'));
    x.sport = true;
    addName(x.names, word);
    index->leagues.push_back(std::move(x));
  }

  // ---- shows: every airing still to end, by folded title
  index->listings = listings_;
  for (const auto& [id, list] : listings_->byChannel) {
    const GuideChannel* g = channels_->find(id);
    if (!list || !g || g->radio) continue;
    for (const Listing& l : *list) {
      if (l.end <= now || se::placeholderShow(l, g->name)) continue;
      index->shows[se::fold(l.title)].push_back({id, &l});
    }
  }
  for (auto& [title, airings] : index->shows)
    std::sort(airings.begin(), airings.end(), [](const se::Index::Airing& a, const se::Index::Airing& b) {
      return a.listing->start != b.listing->start ? a.listing->start < b.listing->start : a.entryId < b.entryId;
    });
  index->finish();
  MLOG_I("search", "index: %zu channels, %zu teams, %zu games, %zu titles", index->channels.size(),
         index->teams.size(), index->games.size(), index->shows.size());
  m.searchIndex = index;
  m.searchKey = key;
  return index;
}

const std::vector<search::Recent>& Store::recentSearches() const { return impl_->searches; }

void Store::rememberSearch(const search::Recent& r) {
  if (!settings_.recentSearches) return;
  if (search::remember(impl_->searches, r)) saveLists();
}

void Store::forgetSearch(const std::string& id) {
  auto& v = impl_->searches;
  size_t before = v.size();
  v.erase(std::remove_if(v.begin(), v.end(), [&](const search::Recent& r) { return r.id == id; }), v.end());
  if (v.size() != before) saveLists();
}

void Store::clearSearches() {
  if (impl_->searches.empty()) return;
  impl_->searches.clear();
  saveLists();
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
