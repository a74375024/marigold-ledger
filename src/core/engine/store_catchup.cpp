// Catch-up (Build 163, replay since Build 165): which copies say they keep
// an archive, the link that asks an Xtream provider for a stretch of it,
// and what the replay's timeline names: the guide's last hours and, on
// demand, a copy's own guide with its past. Links carry the login, like a
// live one: they go to the player only.
#include "core/engine/store.hpp"
#include "core/engine/store_impl.hpp"
#include "core/engine/replay.hpp"
#include "core/sources/xtream.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace marigold::engine {
namespace {

// The raw channel and the Xtream account it comes from; null for an M3U or
// Emby channel, or one no longer listed.
const sources::Account* xtreamAccount(const std::vector<sources::Account>& accounts,
                                      const std::vector<Channel>& channels,
                                      const std::unordered_map<std::string, size_t>& index,
                                      const std::string& channelId, const Channel** channel) {
  auto ci = index.find(channelId);
  if (ci == index.end() || ci->second >= channels.size()) return nullptr;
  const Channel& c = channels[ci->second];
  for (const sources::Account& a : accounts) {
    if (a.sourceId() != c.serverId) continue;
    if (a.kind != sources::AccountKind::Xtream) return nullptr;
    *channel = &c;
    return &a;
  }
  return nullptr;
}

// A raw guide list as listings inside [from, to).
void addRaw(const std::vector<guide::RawProgram>& raw, time::Seconds from, time::Seconds to,
            std::vector<Listing>& out) {
  int64_t low = from / 60, high = to / 60;
  for (const guide::RawProgram& p : raw) {
    if (!(p.end() > low && p.s < high)) continue;
    Listing l;
    l.title = p.t;
    // Build 167: the banner during a replay names the episode and says
    // what the programme is about, as on live.
    l.episode = p.st.value_or("");
    l.overview = p.d;
    l.start = static_cast<time::Seconds>(p.s) * 60;
    l.end = static_cast<time::Seconds>(p.end()) * 60;
    out.push_back(std::move(l));
  }
}

// How much of [from, to) a list covers.
time::Seconds coverage(const std::vector<Listing>& list, time::Seconds from, time::Seconds to) {
  time::Seconds total = 0;
  for (const Listing& l : list) total += std::max<time::Seconds>(0, std::min(l.end, to) - std::max(l.start, from));
  return total;
}

}  // namespace

uint16_t Store::catchupDays(const std::string& channelId) const {
  const Impl& m = *impl_;
  const Channel* c = nullptr;
  return xtreamAccount(m.accounts, *m.channels, m.indexByID, channelId, &c) ? c->archiveDays : 0;
}

std::vector<std::pair<std::string, uint16_t>> Store::replayByProvider(const std::string& entryId) const {
  const Impl& m = *impl_;
  std::vector<std::pair<std::string, uint16_t>> out;
  auto info = m.entryInfo.find(entryId);
  if (info == m.entryInfo.end() || info->second >= m.catalog->entries.size()) return out;
  for (const std::string& member : m.catalog->entries[info->second].members) {
    uint16_t claimed = catchupDays(member);
    if (!claimed) continue;
    auto seen = m.archiveSeen->find(member);
    uint16_t days = seen == m.archiveSeen->end() ? replay::usableDays(claimed, -1, 0)
                                                 : replay::usableDays(claimed, seen->second.first, seen->second.second);
    if (!days) continue;
    std::string provider = providerName(member);
    auto it = std::find_if(out.begin(), out.end(), [&](const auto& p) { return p.first == provider; });
    if (it == out.end()) out.emplace_back(std::move(provider), days);
    else it->second = std::max(it->second, days);
  }
  std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
  return out;
}

std::optional<Store::CatchupFeed> Store::catchupFeed(const Feed& live, time::Seconds from, time::Seconds now) const {
  const Impl& m = *impl_;
  const Channel* c = nullptr;
  const sources::Account* account = xtreamAccount(m.accounts, *m.channels, m.indexByID, live.channelId, &c);
  if (!account || c->archiveDays == 0 || from >= now) return std::nullopt;
  auto client = sources::xtream::Client::make(*account);
  if (!client) return std::nullopt;
  CatchupFeed out;
  out.serverOffset = account->serverOffset;
  out.minutes = sources::xtream::timeshiftMinutes(from, now);
  std::optional<std::string> url =
      client.value().timeshiftURL(c->itemId, from, out.minutes, account->serverOffset.value_or(0));
  if (!url) return std::nullopt;
  out.feed = live;
  out.feed.url = std::move(*url);
  out.feed.headers.clear();
  out.feed.needsOpen = false;
  out.feed.replay = true;
  return out;
}

std::vector<Listing> Store::pastListing(const std::string& entryId, time::Seconds from) const {
  const Impl& m = *impl_;
  time::Seconds now = time::now();
  time::Seconds to = now + 4 * 3600;
  std::vector<std::string> members;
  auto info = m.entryInfo.find(entryId);
  if (info != m.entryInfo.end() && info->second < m.catalog->entries.size())
    members = m.catalog->entries[info->second].members;
  else
    members.push_back(entryId);
  // The copy whose kept guide covers the most of the past asked for: its
  // provider's full guide, else the outside listings, else the short guide.
  std::vector<Listing> best;
  time::Seconds bestCover = -1;
  for (const std::string& id : members) {
    std::vector<Listing> list;
    auto p = m.providerMatches.find(id);
    if (p != m.providerMatches.end() && p->second) addRaw(*p->second, from, to, list);
    if (list.empty()) {
      auto o = m.outsideMatches.find(id);
      if (o != m.outsideMatches.end() && o->second) addRaw(*o->second, from, to, list);
    }
    if (list.empty()) {
      auto s = m.serverPrograms.find(id);
      if (s != m.serverPrograms.end())
        for (const Program& pr : s->second) {
          if (pr.end <= from || pr.start >= to) continue;
          Listing l;
          l.title = pr.title;
          l.episode = pr.episodeTitle.value_or("");
          l.overview = pr.overview;
          l.start = pr.start;
          l.end = pr.end;
          list.push_back(std::move(l));
        }
    }
    time::Seconds cover = coverage(list, from, now);
    if (cover > bestCover) {
      bestCover = cover;
      best = std::move(list);
    }
  }
  std::stable_sort(best.begin(), best.end(), [](const Listing& a, const Listing& b) { return a.start < b.start; });
  return best;
}

void Store::archiveGuide(const std::string& channelId,
                         std::function<void(std::optional<std::vector<PastProgramme>>)> done) {
  Impl& m = *impl_;
  const Channel* c = nullptr;
  const sources::Account* account = xtreamAccount(m.accounts, *m.channels, m.indexByID, channelId, &c);
  if (!account) return;
  auto client = sources::xtream::Client::make(*account);
  if (!client) return;
  auto list = std::make_shared<std::optional<std::vector<PastProgramme>>>();
  // Like the short guide: the same client and plain headers, at background
  // priority (it waits for nothing on screen).
  http::Request q;
  {
    sources::Request r = client.value().simpleDataTableRequest(c->itemId);
    q.url = r.url;
    for (const sources::Header& h : r.headers) q.headers.push_back(h.name + ": " + h.value);
    q.timeoutMs = static_cast<unsigned>(std::max(1, r.timeoutSeconds)) * 1000u;
    q.personalServer = r.allowInsecureTLS;
  }
  q.priority = http::Priority::Background;
  q.tag = "xtream.archive";
  q.afterTransfer = [list](http::Response& r) {
    if (r.status >= 200 && r.status <= 299) {
      std::vector<PastProgramme> out;
      for (sources::xtream::ArchiveProgramme& p : sources::xtream::parseSimpleDataTable(r.status, r.body)) {
        PastProgramme one;
        one.start = p.start;
        one.end = p.end;
        one.title = std::move(p.title);
        one.archived = p.archived;
        out.push_back(std::move(one));
      }
      // A body that isn't the table at all is a failure; an empty table
      // is an answer.
      if (!out.empty() || r.body.find("epg_listings") != std::string::npos) *list = std::move(out);
    }
    r.body.clear();
  };
  uint64_t gen = m.generation;
  host_.pool.submit(std::move(q), [this, gen, list, done = std::move(done)](http::Response&&) {
    if (gen != impl_->generation) return;
    if (done) done(std::move(*list));
  });
}

std::vector<std::string> Store::takeGuideLog() {
  std::vector<std::string> out;
  out.swap(impl_->guideNotes);
  return out;
}

}  // namespace marigold::engine
