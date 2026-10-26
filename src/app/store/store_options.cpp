// The store behind the Options pages, show reminders and search (tvOS
// LiveTVOptionsPanel, LineupEditView, LiveTVGuideSourcesView, the program
// detail card and LiveTVSearchView). UI thread only.
#include <algorithm>
#include <map>
#include <set>

#include "app/app.hpp"
#include "app/store/store.hpp"
#include "app/store/store_impl.hpp"
#include "core/catalog/catalog.hpp"
#include "core/guide/outside.hpp"
#include "core/guide/names.hpp"
#include "core/guide/provider.hpp"
#include "core/report/channel_report.hpp"
#include "core/sources/account.hpp"
#include "core/sports/network_map.hpp"
#include "core/util/log.hpp"
#include "core/util/redact.hpp"
#include "core/util/strings.hpp"

namespace marigold::app {
namespace {

constexpr size_t kMaxProgramHits = 60;
constexpr size_t kMaxAirings = 4;
constexpr size_t kMaxEventHits = 40;

std::string fold(std::string_view s) { return str::lowerAscii(str::foldDiacritics(std::string(s))); }

// "3 hours ago", "yesterday"...
std::string ago(time::Seconds t, time::Seconds now) {
  time::Seconds d = std::max<time::Seconds>(0, now - t);
  if (d < 90) return "just now";
  if (d < 3600) return std::to_string(d / 60) + " minutes ago";
  if (d < 2 * 3600) return "an hour ago";
  if (d < 24 * 3600) return std::to_string(d / 3600) + " hours ago";
  if (d < 48 * 3600) return "yesterday";
  return std::to_string(d / 86400) + " days ago";
}

std::string count(int n, const char* one, const char* many) {
  return str::groupedThousands(n) + " " + (n == 1 ? one : many);
}

bool matches(const std::string& hay, const std::string& q) { return fold(hay).find(q) != std::string::npos; }

}  // namespace

// ------------------------------------------------------------ provider groups

std::vector<GroupSummary> Store::groups() const {
  std::map<std::string, int> counts;
  for (const GuideChannel& g : channels_->all) ++counts[g.group];
  std::vector<GroupSummary> out;
  for (const auto& [name, n] : counts) {
    if (name.empty()) continue;
    out.push_back({name, n, impl_->hiddenGroups.count(name) > 0});
  }
  return out;
}

void Store::setGroupHidden(const std::string& name, bool hidden) {
  Impl& m = *impl_;
  bool changed = hidden ? m.hiddenGroups.insert(name).second : m.hiddenGroups.erase(name) > 0;
  if (!changed) return;
  saveLists();
  m.channelsDirty = true;
  m.ev.boardDirty = true;
}

// ------------------------------------------------------------ My Lineup

void Store::setLineupCountries(const std::set<std::string>& countries) {
  if (countries.empty()) return;  // keep at least one
  catalog::LineupPrefs p = impl_->prefs;
  if (p.countries == countries) return;
  p.countries = countries;
  setPrefs(p);
  impl_->ev.boardDirty = true;
}

void Store::refreshSuggestions() { buildLineup(impl_->prefs); }

std::string Store::homeMarketName() const {
  const catalog::LineupPrefs& p = impl_->prefs;
  if (p.homeMarket) return *p.homeMarket;
  return p.markets.empty() ? std::string() : p.markets.front().name;
}

void Store::cycleHomeMarket() {
  auto next = sports::nextHomeMarket(impl_->prefs);
  if (!next) return;
  catalog::LineupPrefs p = impl_->prefs;
  p.homeMarket = *next;
  setPrefs(p);
  // Station ranks are worked out when the game listings are read.
  impl_->ev.listingsAt.reset();
  impl_->ev.boardDirty = true;
}

// ------------------------------------------------------------ guide listings

std::set<std::string> Store::guideShards() const {
  return settings_.guideShards.empty() ? guide::defaultShards() : settings_.guideShards;
}

void Store::setGuideShard(const std::string& id, bool on) {
  Settings s = settings_;
  std::set<std::string> shards = guideShards();
  if (on) shards.insert(id);
  else shards.erase(id);
  s.guideShards = shards;
  setSettings(s);
  scheduleOutside(true);
}

void Store::refreshListings() {
  scheduleProvider(true);
  if (settings_.fillMissingListings) scheduleOutside(true);
}

GuideListingsStatus Store::guideListings() const {
  const Impl& m = *impl_;
  GuideListingsStatus out;
  time::Seconds now = time::now();
  out.providerRunning = m.providerRunning;
  for (const sources::Account& a : m.accounts) {
    bool file = a.kind == sources::AccountKind::Xtream || (a.kind == sources::AccountKind::M3U && !a.guideUrl.empty());
    if (!file) continue;
    GuideListingsStatus::Provider p;
    p.name = redact(a.displayName);
    std::string key = guide::provider::cacheKey("default", a.sourceId());
    auto problem = m.providerProblems.find(key);
    auto saved = m.providerSaved.find(key);
    if (m.providerRunning) {
      p.line = "Updating\xe2\x80\xa6 (pauses while you watch)";
    } else if (saved != m.providerSaved.end()) {
      const guide::provider::Saved& s = saved->second;
      p.line = "Listings for " + str::groupedThousands(static_cast<int>(s.programs.size())) + " of " +
               count(s.channelCount, "channel", "channels") + " (the file lists " +
               str::groupedThousands(s.guideChannels) + "). Updated " + ago(s.built, now) + ".";
    } else if (problem != m.providerProblems.end()) {
      p.line = "Couldn't load the guide: " + problem->second;
    } else {
      p.line = "Not loaded yet";
    }
    if (problem != m.providerProblems.end() && saved != m.providerSaved.end() && !m.providerRunning)
      p.line += " Last try failed: " + problem->second;
    out.providers.push_back(std::move(p));
  }

  std::set<std::string> on = guideShards();
  for (const guide::Shard& sh : guide::shards()) out.shards.push_back({sh.id, sh.title, sh.detail, on.count(sh.id) > 0});

  if (!settings_.fillMissingListings) {
    out.outsideLine = "Only your provider's guide is used.";
  } else if (m.outsideRunning) {
    out.outsideLine = "Updating outside listings\xe2\x80\xa6 (runs in the background and pauses while you watch)";
  } else if (m.outsideSaved) {
    const guide::Saved& s = *m.outsideSaved;
    out.outsideLine = "Matched " + str::groupedThousands(static_cast<int>(s.programs.size())) + " of " +
                      count(s.channelCount, "channel", "channels") + ". Listings updated " + ago(s.built, now) + ".";
    if (s.sources) {
      int i = 0, stale = 0;
      for (const auto& [id, st] : *s.sources) {
        ++i;
        std::string line = (st.backup ? "Backup " : "Listings file ") + std::to_string(i) + ": ";
        if (st.problem) line += "couldn't update (" + redact(*st.problem) + ")";
        else if (st.stale.value_or(false)) line += "out of date";
        else if (st.matched) line += count(*st.matched, "channel", "channels");
        else line += "not used";
        if (st.downloadedAt) line += ", downloaded " + ago(*st.downloadedAt, now);
        if (st.stale.value_or(false) && !st.backup) ++stale;
        out.sourceLines.push_back(line);
      }
      if (stale) out.outsideLine += " " + count(stale, "file is", "files are") + " out of date; checking again every 2 hours.";
    }
  } else {
    out.outsideLine = "Loading outside listings\xe2\x80\xa6";
  }
  return out;
}

// ------------------------------------------------------------ show reminders

bool Store::hasShowReminder(const std::string& entryId, time::Seconds start) const {
  return impl_->ev.reminders.isSet(entryId, start);
}

bool Store::toggleShowReminder(const std::string& entryId, const Listing& l) {
  EventsState& e = impl_->ev;
  sports::LiveReminder r;
  r.channelID = entryId;
  const GuideChannel* g = channels_->find(entryId);
  r.channelName = g ? g->name : std::string();
  r.title = l.title;
  r.start = l.start;
  r.end = l.end;
  bool was = e.reminders.isSet(entryId, l.start);
  e.reminders.toggle(r);
  saveEvents();
  return !was;
}

std::vector<ProgramHit> Store::otherAirings(const std::string& entryId, const Listing& l) const {
  std::vector<ProgramHit> out;
  time::Seconds now = time::now();
  std::string title = fold(l.title);
  for (const auto& [id, list] : listings_->byChannel) {
    if (!list || !channels_->find(id)) continue;
    for (const Listing& x : *list) {
      if (x.start <= now || fold(x.title) != title) continue;
      if (id == entryId && x.start == l.start) continue;
      out.push_back({id, x});
    }
  }
  std::sort(out.begin(), out.end(), [](const ProgramHit& a, const ProgramHit& b) {
    return a.listing.start != b.listing.start ? a.listing.start < b.listing.start : a.entryId < b.entryId;
  });
  if (out.size() > kMaxAirings) out.resize(kMaxAirings);
  return out;
}

// ------------------------------------------------------------ search

std::vector<std::string> Store::searchEvents(const std::string& query) const {
  std::string q = fold(str::trim(query));
  std::vector<std::string> out;
  if (q.size() < 2) return out;
  std::set<std::string> seen;
  for (const EventRow& row : events_->rows) {
    if (row.families) continue;
    for (const EventGroup& g : row.groups) {
      for (const EventTile& t : g.tiles) {
        if (seen.count(t.id)) continue;
        bool hit = matches(t.title, q) || matches(t.heroTitle, q) || matches(t.league, q) || matches(t.leagueChip, q) ||
                   matches(t.subtitle, q);
        for (const EventSide& s : t.sides) hit = hit || matches(s.name, q) || fold(s.abbr) == q;
        if (!hit) continue;
        seen.insert(t.id);
        out.push_back(t.id);
        if (out.size() >= kMaxEventHits) return out;
      }
    }
  }
  return out;
}

std::vector<ProgramHit> Store::searchPrograms(const std::string& query) const {
  std::string q = fold(str::trim(query));
  std::vector<ProgramHit> out;
  if (q.size() < 2) return out;
  time::Seconds now = time::now();
  struct Scored {
    int score;
    ProgramHit hit;
  };
  std::vector<Scored> found;
  for (const auto& [id, list] : listings_->byChannel) {
    const GuideChannel* g = channels_->find(id);
    if (!list || !g) continue;
    for (const Listing& l : *list) {
      if (l.end <= now) continue;
      std::string title = fold(l.title);
      int score = 0;
      if (title == q) score = 90;
      else if (title.compare(0, q.size(), q) == 0) score = 75;
      else if (title.find(q) != std::string::npos) score = 55;
      else if (fold(l.episode).find(q) != std::string::npos) score = 35;
      if (!score) continue;
      if (l.isOn(now)) score += 8;
      if (g->inLineup) score += 6;
      if (g->favorite) score += 4;
      found.push_back({score, {id, l}});
    }
  }
  std::sort(found.begin(), found.end(), [](const Scored& a, const Scored& b) {
    if (a.score != b.score) return a.score > b.score;
    if (a.hit.listing.start != b.hit.listing.start) return a.hit.listing.start < b.hit.listing.start;
    return a.hit.entryId < b.hit.entryId;
  });
  for (size_t i = 0; i < found.size() && i < kMaxProgramHits; ++i) out.push_back(std::move(found[i].hit));
  return out;
}

// ------------------------------------------------------------ My Sports prompt

bool Store::shouldPromptMySports() const {
  const sports::MySportsPrefs& p = impl_->ev.mySports;
  return settings_.scheduleEnabled && !p.configured && !p.prompted;
}

void Store::markMySportsPrompted() {
  sports::MySportsPrefs p = impl_->ev.mySports;
  if (p.prompted) return;
  p.prompted = true;
  setMySports(p);
}

}  // namespace marigold::app

// ------------------------------------------------------------ channel report

namespace marigold::app {

void Store::buildChannelReport(std::function<void(std::shared_ptr<std::string>)> done) {
  const Impl& m = *impl_;
  // Copies of everything the report reads; the worker owns them.
  struct Job {
    std::shared_ptr<const std::vector<Channel>> channels;
    std::shared_ptr<const CatalogResult> catalog;
    std::set<std::string> lineup, hidden, countries;
    std::unordered_map<std::string, report::ServerGuideState> guideState;
    std::unordered_map<std::string, std::vector<Program>> serverPrograms;
    catalog::ProgramLists programs;
    guide::SharedPrograms outside, provider;
    std::map<std::string, std::string> outsideNotes, providerNotes, names;
    std::map<std::string, guide::provider::Status> providerStates;
    guide::StationsByNetwork stations;
    std::unordered_map<std::string, std::pair<std::string, std::string>> logos;
    std::vector<std::string> privateWords;
    time::Seconds now = 0, windowStart = 0;
    time::Zone zone = time::Zone::device();
  };
  auto job = std::make_shared<Job>();
  job->channels = m.channels;
  job->catalog = m.catalog;
  job->lineup.insert(m.lineup.lineupIDs.begin(), m.lineup.lineupIDs.end());
  job->hidden = m.hiddenGroups;
  job->countries = m.prefs.guideCountries();
  for (const auto& [id, st] : m.guideState) {
    report::ServerGuideState s = report::ServerGuideState::NotChecked;
    if (st == Impl::GuideState::Loading) s = report::ServerGuideState::Loading;
    else if (st == Impl::GuideState::Loaded) s = report::ServerGuideState::Loaded;
    else if (st == Impl::GuideState::Failed) s = report::ServerGuideState::Failed;
    job->guideState[id] = s;
  }
  job->serverPrograms = m.serverPrograms;
  job->programs = m.programs;
  job->outside = m.outsideMatches;
  if (m.outsideSaved && m.outsideSaved->notes) job->outsideNotes = *m.outsideSaved->notes;
  job->provider = m.providerMatches;
  time::Seconds now = time::now();
  for (const sources::Account& a : m.accounts) {
    std::string source = a.sourceId();
    job->names[source] = redact(a.displayName);
    // Hosts and user names never reach a cell.
    for (const std::string& w : {a.userName, sources::stripServerSecrets(a.serverUrl),
                                 sources::stripServerSecrets(a.playlistUrl), sources::stripServerSecrets(a.guideUrl)}) {
      std::string host = w;
      size_t scheme = host.find("://");
      if (scheme != std::string::npos) host = host.substr(scheme + 3);
      host = host.substr(0, host.find_first_of(":/"));
      if (host.size() >= 4) job->privateWords.push_back(host);
    }
    std::string key = guide::provider::cacheKey("default", source);
    auto saved = m.providerSaved.find(key);
    guide::provider::Status st;
    if (saved != m.providerSaved.end()) {
      for (const auto& [id, note] : saved->second.notes) job->providerNotes[id] = note;
      st.kind = guide::provider::Status::Kind::ready;
      st.matched = static_cast<int>(saved->second.programs.size());
      st.total = saved->second.channelCount;
      st.guideChannels = saved->second.guideChannels;
      st.updated = saved->second.built;
    } else if (auto p = m.providerProblems.find(key); p != m.providerProblems.end()) {
      st.kind = guide::provider::Status::Kind::failed;
      st.message = p->second;
    } else {
      st.kind = m.providerRunning ? guide::provider::Status::Kind::updating : guide::provider::Status::Kind::unavailable;
    }
    job->providerStates[source] = st;
  }
  job->stations = guide::groupByNetwork(*m.stations);
  for (const GuideChannel& g : channels_->all) {
    std::string status = g.logoUrl.empty() ? "no logo" : app_.logos().status(g.logoUrl);
    job->logos[g.id] = {g.logoSources.empty() ? "none" : g.logoSources, status};
  }
  job->now = now;
  job->windowStart = windowStart();
  job->zone = app_.zone();
  auto out = std::make_shared<std::string>();
  app_.pool().post(
      [job, out] {
        report::ChannelReportInput in;
        in.channels = job->channels.get();
        in.catalog = job->catalog.get();
        in.lineupIds = &job->lineup;
        in.hiddenGroups = &job->hidden;
        in.guideState = &job->guideState;
        in.serverPrograms = &job->serverPrograms;
        in.programs = &job->programs;
        in.outsideMatches = &job->outside;
        in.outsideNotes = &job->outsideNotes;
        in.providerMatches = &job->provider;
        in.providerNotes = &job->providerNotes;
        in.providerStates = &job->providerStates;
        in.providerCountries = &job->countries;
        in.providerNames = &job->names;
        in.stations = &job->stations;
        in.logos = &job->logos;
        in.now = job->now;
        in.windowStart = job->windowStart;
        in.zone = job->zone;
        in.privateWords = job->privateWords;
        *out = report::channelReportCSV(in);
      },
      [out, done] { done(out->empty() ? nullptr : out); }, http::Priority::Normal);
}

}  // namespace marigold::app
