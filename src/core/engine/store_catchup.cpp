// Catch-up (Build 163, replay since Build 165): which copies say they keep
// an archive, the link that asks an Xtream provider for a stretch of it,
// and what the replay's timeline names: the guide's last hours and, on
// demand, a copy's own guide with its past. Links carry the login, like a
// live one: they go to the player only.
#include "core/engine/store.hpp"
#include "core/engine/store_impl.hpp"
#include "core/engine/replay.hpp"
#include "core/engine/source_probe.hpp"
#include "core/sources/xtream.hpp"
#include "core/util/json.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace marigold::engine {
namespace {

constexpr int kMostOffset = 14 * 3600;

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

// ------------------------------------------------- archive clocks (Build 170)

namespace {

// The value counted most; a tie goes to `last`, else the smallest.
std::optional<int> mostCounted(const std::map<int, size_t>& counts, std::optional<int> last) {
  std::optional<int> best;
  size_t top = 0;
  for (const auto& [value, n] : counts)
    if (n > top || (n == top && last == value)) {
      best = value;
      top = n;
    }
  return best;
}

bool noteIn(std::map<std::string, int>& kept, const std::string& streamId, int offset) {
  auto it = kept.find(streamId);
  if (it != kept.end()) {
    bool changed = it->second != offset;
    it->second = offset;
    return changed;
  }
  if (kept.size() >= ArchiveClocks::kStreamsKept) kept.erase(kept.begin());
  kept.emplace(streamId, offset);
  return true;
}

}  // namespace

bool ArchiveClocks::note(const std::string& accountId, const std::string& streamId, int offset) {
  if (accountId.empty() || streamId.empty() || offset < -kMostOffset || offset > kMostOffset) return false;
  if (!accounts.count(accountId) && accounts.size() >= kAccountsKept) return false;
  Account& a = accounts[accountId];
  bool changed = a.last != offset;
  a.last = offset;
  return noteIn(a.streams, streamId, offset) || changed;
}

bool ArchiveClocks::noteMeasured(const std::string& accountId, const std::string& streamId, int offset) {
  if (accountId.empty() || streamId.empty() || offset < -kMostOffset || offset > kMostOffset) return false;
  if (!accounts.count(accountId) && accounts.size() >= kAccountsKept) return false;
  Account& a = accounts[accountId];
  bool changed = a.lastMeasured != offset;
  a.lastMeasured = offset;
  return noteIn(a.measured, streamId, offset) || changed;
}

std::optional<int> ArchiveClocks::measuredStream(const std::string& accountId, const std::string& streamId) const {
  auto a = accounts.find(accountId);
  if (a == accounts.end()) return std::nullopt;
  auto it = a->second.measured.find(streamId);
  if (it == a->second.measured.end()) return std::nullopt;
  return it->second;
}

std::optional<int> ArchiveClocks::measuredFor(const std::string& accountId, std::optional<int> said,
                                              std::optional<int> server, bool any) const {
  auto a = accounts.find(accountId);
  if (a == accounts.end()) return std::nullopt;
  std::map<int, size_t> same, all;
  for (const auto& [id, clock] : a->second.measured) {
    ++all[clock];
    auto table = a->second.streams.find(id);
    std::optional<int> theirs = table != a->second.streams.end() ? std::optional<int>(table->second) : server;
    if (theirs == said) ++same[clock];
  }
  if (std::optional<int> v = mostCounted(same, a->second.lastMeasured)) return v;
  return any ? mostCounted(all, a->second.lastMeasured) : std::nullopt;
}

bool ArchiveClocks::allExact(const std::string& accountId) const {
  auto a = accounts.find(accountId);
  if (a == accounts.end()) return false;
  size_t exact = 0;
  for (const auto& [id, t] : a->second.timings) {
    if (t.kind == archive::Kind::Exact) ++exact;
    else if (t.kind == archive::Kind::Chunked || t.kind == archive::Kind::Programme) return false;
  }
  return exact > 0;
}

bool ArchiveClocks::noteCheck(const std::string& accountId, bool ok, time::Seconds at) {
  if (accountId.empty()) return false;
  auto it = accounts.find(accountId);
  if (ok) {
    if (it == accounts.end() || !it->second.checkFails) return false;
    it->second.checkFails = 0;
    it->second.checkFailedAt = 0;
    return true;
  }
  if (it == accounts.end() && accounts.size() >= kAccountsKept) return false;
  Account& a = accounts[accountId];
  a.checkFails = std::min(a.checkFails + 1, 99);
  a.checkFailedAt = at;
  return true;
}

int ArchiveClocks::checkFails(const std::string& accountId) const {
  auto a = accounts.find(accountId);
  return a == accounts.end() ? 0 : a->second.checkFails;
}

bool ArchiveClocks::anyMeasured(const std::string& accountId) const {
  auto a = accounts.find(accountId);
  return a != accounts.end() && !a->second.measured.empty();
}

bool ArchiveClocks::noteTiming(const std::string& accountId, const std::string& streamId,
                               const archive::Timing& t) {
  if (accountId.empty() || streamId.empty() || !t.told()) return false;
  if (!accounts.count(accountId) && accounts.size() >= kAccountsKept) return false;
  Account& a = accounts[accountId];
  auto it = a.timings.find(streamId);
  if (it == a.timings.end() && a.timings.size() >= kStreamsKept) a.timings.erase(a.timings.begin());
  a.timings[streamId] = t;
  return true;
}

std::optional<archive::Timing> ArchiveClocks::timing(const std::string& accountId, const std::string& streamId) const {
  auto a = accounts.find(accountId);
  if (a == accounts.end()) return std::nullopt;
  auto it = a->second.timings.find(streamId);
  if (it == a->second.timings.end()) return std::nullopt;
  return it->second;
}

std::optional<int> ArchiveClocks::stream(const std::string& accountId, const std::string& streamId) const {
  auto a = accounts.find(accountId);
  if (a == accounts.end()) return std::nullopt;
  auto it = a->second.streams.find(streamId);
  if (it == a->second.streams.end()) return std::nullopt;
  return it->second;
}

std::optional<int> ArchiveClocks::learned(const std::string& accountId) const {
  auto a = accounts.find(accountId);
  if (a == accounts.end()) return std::nullopt;
  std::map<int, size_t> counts;
  for (const auto& [id, off] : a->second.streams) ++counts[off];
  std::optional<int> best = mostCounted(counts, a->second.last);
  return best ? best : a->second.last;
}

std::string ArchiveClocks::toJson() const {
  json::Value root = json::Value::object();
  // Build 173: 2, with the measured clocks apart from the tables' ("streams").
  root.set("v", 2);
  json::Value list = json::Value::object();
  for (const auto& [id, a] : accounts) {
    json::Value x = json::Value::object();
    if (a.last) x.set("last", *a.last);
    json::Value streams = json::Value::object();
    for (const auto& [stream, off] : a.streams) streams.set(stream, off);
    x.set("streams", std::move(streams));
    if (!a.measured.empty()) {
      json::Value measured = json::Value::object();
      for (const auto& [stream, off] : a.measured) measured.set(stream, off);
      x.set("measured", std::move(measured));
    }
    if (a.lastMeasured) x.set("lastMeasured", *a.lastMeasured);
    // Build 172: ids and numbers only ("c30,0,1,625000,<when>").
    if (!a.timings.empty()) {
      json::Value timings = json::Value::object();
      for (const auto& [stream, t] : a.timings) timings.set(stream, archive::encode(t));
      x.set("timing", std::move(timings));
    }
    // Build 174: the clock checks failed in a row, and when the last did.
    if (a.checkFails) {
      x.set("checkFails", a.checkFails);
      x.set("checkFailedAt", static_cast<int64_t>(a.checkFailedAt));
    }
    list.set(id, std::move(x));
  }
  root.set("accounts", std::move(list));
  return json::write(root);
}

ArchiveClocks ArchiveClocks::fromJson(std::string_view text) {
  ArchiveClocks out;
  std::optional<json::Value> root = json::parse(text);
  if (!root || !root->isObject()) return out;
  auto sane = [](const json::Value& v) -> std::optional<int> {
    if (!v.isNumber()) return std::nullopt;
    int64_t n = v.asInt();
    if (n < -kMostOffset || n > kMostOffset) return std::nullopt;
    return static_cast<int>(n);
  };
  for (const auto& [id, x] : (*root)["accounts"].fields()) {
    if (out.accounts.size() >= kAccountsKept || !sources::isSafeItemId(id)) continue;
    Account a;
    a.last = sane(x["last"]);
    // 1.0.13 / 1.0.15 kept only the tables' clocks, here: still the
    // tables', so a measurement replaces them.
    for (const auto& [stream, off] : x["streams"].fields()) {
      if (a.streams.size() >= kStreamsKept) break;
      if (std::optional<int> v = sane(off); v && sources::isSafeItemId(stream)) a.streams.emplace(stream, *v);
    }
    for (const auto& [stream, off] : x["measured"].fields()) {
      if (a.measured.size() >= kStreamsKept) break;
      if (std::optional<int> v = sane(off); v && sources::isSafeItemId(stream)) a.measured.emplace(stream, *v);
    }
    a.lastMeasured = sane(x["lastMeasured"]);
    for (const auto& [stream, v] : x["timing"].fields()) {
      if (a.timings.size() >= kStreamsKept) break;
      if (!v.isString() || !sources::isSafeItemId(stream)) continue;
      if (std::optional<archive::Timing> t = archive::decode(v.asString())) a.timings.emplace(stream, *t);
    }
    if (x["checkFails"].isNumber()) {
      int64_t n = x["checkFails"].asInt();
      a.checkFails = n > 0 ? static_cast<int>(std::min<int64_t>(n, 99)) : 0;
      if (a.checkFails && x["checkFailedAt"].isNumber())
        a.checkFailedAt = static_cast<time::Seconds>(x["checkFailedAt"].asInt());
    }
    if (a.last || !a.streams.empty() || !a.measured.empty() || !a.timings.empty() || a.checkFails)
      out.accounts.emplace(id, std::move(a));
  }
  return out;
}

Store::ArchiveClock Store::archiveClock(const std::string& channelId) const {
  const Impl& m = *impl_;
  ArchiveClock out;
  const Channel* c = nullptr;
  const sources::Account* account = xtreamAccount(m.accounts, *m.channels, m.indexByID, channelId, &c);
  if (!account) return out;
  out.server = account->serverOffset;
  out.offset = account->serverOffset;
  std::optional<int> table = m.archiveClocks.stream(account->id, c->itemId);
  if (std::optional<int> own = m.archiveClocks.measuredStream(account->id, c->itemId)) {
    // Build 173: the copy's own video over anything said.
    out.offset = own;
    out.from = ClockFrom::Measured;
    out.measured = true;
  } else if (std::optional<int> taught = m.archiveClocks.measuredFor(account->id, table ? table : account->serverOffset,
                                                                    account->serverOffset, !table)) {
    // Build 173: what the account's measured copies showed where their
    // table (or server) said what this copy's says; for a copy without a
    // table, what most of them showed.
    out.offset = taught;
    out.from = ClockFrom::Learned;
    out.measured = true;
  } else if (table) {
    out.offset = table;
    out.from = ClockFrom::Table;
  } else if (std::optional<int> learned = m.archiveClocks.learned(account->id)) {
    out.offset = learned;
    out.from = ClockFrom::Learned;
  } else {
    out.settled = m.tablesRead.count(channelId) > 0;
  }
  return out;
}

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
  // Build 170: in the clock the copy's archive is indexed on.
  out.clock = archiveClock(live.channelId);
  out.minutes = sources::xtream::timeshiftMinutes(from, now);
  std::optional<std::string> url =
      client.value().timeshiftURL(c->itemId, from, out.minutes, out.clock.offset.value_or(0));
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
  // Build 179: what the guide shows for the row (the schedule it chose by
  // the channel's identity, Build 154) leads, so Rewind names what the
  // guide does. Before, the copy whose kept guide reached furthest back
  // was taken whole, and a copy whose provider files another channel's
  // guide under it (NBC Sports Bay Area's "49ers Now" on KNTV) could win.
  std::vector<Listing> shown;
  auto pr = m.programs.find(entryId);
  if (pr != m.programs.end() && pr->second)
    for (const Program& p : *pr->second) {
      if (p.end <= from || p.start >= to) continue;
      Listing l;
      l.title = p.title;
      l.episode = p.episodeTitle.value_or("");
      l.overview = p.overview;
      l.start = p.start;
      l.end = p.end;
      l.outside = p.fromOutsideGuide;
      shown.push_back(std::move(l));
    }
  auto asProgrammes = [](const std::vector<Listing>& list) {
    std::vector<replay::Programme> out;
    out.reserve(list.size());
    for (const Listing& l : list) out.push_back(replay::Programme{l.start, l.end, l.title, true});
    return out;
  };
  const std::vector<replay::Programme> shownP = asProgrammes(shown);
  // Then the copy whose kept guide covers the most of the past asked for
  // (its provider's full guide, else the outside listings, else the short
  // guide), among those that are the same schedule where both have
  // listings: it fills only what the guide's list lacks.
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
      auto sp = m.serverPrograms.find(id);
      if (sp != m.serverPrograms.end())
        for (const Program& prg : sp->second) {
          if (prg.end <= from || prg.start >= to) continue;
          Listing l;
          l.title = prg.title;
          l.episode = prg.episodeTitle.value_or("");
          l.overview = prg.overview;
          l.start = prg.start;
          l.end = prg.end;
          list.push_back(std::move(l));
        }
    }
    if (!shown.empty() && !replay::agrees(shownP, asProgrammes(list))) continue;
    time::Seconds cover = coverage(list, from, now);
    if (cover > bestCover) {
      bestCover = cover;
      best = std::move(list);
    }
  }
  if (!shown.empty()) {
    // The guide's own, and from the other only what none of them covers
    // more than half of.
    std::vector<Listing> out = shown;
    for (Listing& l : best) {
      time::Seconds len = std::max<time::Seconds>(1, l.end - l.start);
      bool covered = false;
      for (const Listing& g : shown) {
        time::Seconds overlap = std::min(l.end, g.end) - std::max(l.start, g.start);
        if (overlap * 2 > len) {
          covered = true;
          break;
        }
      }
      if (!covered) out.push_back(std::move(l));
    }
    best = std::move(out);
  }
  std::stable_sort(best.begin(), best.end(), [](const Listing& a, const Listing& b) { return a.start < b.start; });
  return best;
}

void Store::archiveGuide(const std::string& channelId,
                         std::function<void(std::optional<std::vector<PastProgramme>>)> done) {
  archiveTable(channelId, [done = std::move(done)](const ArchiveTableAnswer& a) {
    if (done) done(a.programmes);
  });
}

void Store::archiveTable(const std::string& channelId, std::function<void(const ArchiveTableAnswer&)> done) {
  Impl& m = *impl_;
  const Channel* c = nullptr;
  const sources::Account* account = xtreamAccount(m.accounts, *m.channels, m.indexByID, channelId, &c);
  if (!account) return;
  auto client = sources::xtream::Client::make(*account);
  if (!client) return;
  // Build 170: one request per copy at a time; a second ask waits for it.
  auto out = m.tablesOut.find(channelId);
  if (out != m.tablesOut.end()) {
    out->second.push_back(std::move(done));
    return;
  }
  m.tablesOut[channelId].push_back(std::move(done));
  auto answer = std::make_shared<ArchiveTableAnswer>();
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
  q.afterTransfer = [answer](http::Response& r) {
    if (r.status >= 200 && r.status <= 299) {
      sources::xtream::ArchiveTable table = sources::xtream::parseArchiveTable(r.status, r.body);
      std::vector<PastProgramme> list;
      for (sources::xtream::ArchiveProgramme& p : table.programmes) {
        PastProgramme one;
        one.start = p.start;
        one.end = p.end;
        one.title = std::move(p.title);
        one.archived = p.archived;
        list.push_back(std::move(one));
      }
      // A body that isn't the table at all is a failure; an empty table
      // is an answer.
      if (!list.empty() || r.body.find("epg_listings") != std::string::npos) {
        answer->programmes = std::move(list);
        answer->clock = table.clock;
      }
    }
    if (!answer->programmes) {
      answer->refused = probe::refusal(r.status, r.error, r.body);
      // Never the error text itself: it can carry the link.
      answer->why = r.status ? "http " + std::to_string(r.status)
                             : std::string(answer->refused ? "refused" : "no answer");
    }
    r.body.clear();
  };
  uint64_t gen = m.generation;
  std::string accountId = account->id, streamId = c->itemId;
  host_.pool.submit(std::move(q), [this, gen, answer, channelId, accountId, streamId](http::Response&&) {
    Impl& n = *impl_;
    std::vector<std::function<void(const ArchiveTableAnswer&)>> waiters;
    if (auto waiting = n.tablesOut.find(channelId); waiting != n.tablesOut.end()) {
      waiters = std::move(waiting->second);
      n.tablesOut.erase(waiting);
    }
    if (gen != n.generation) return;
    // A refusal isn't an answer: the table is asked again next time.
    if (!answer->refused) n.tablesRead.insert(channelId);
    if (answer->clock && n.archiveClocks.note(accountId, streamId, *answer->clock)) saveArchiveClocksLater();
    for (auto& d : waiters)
      if (d) d(*answer);
  });
}

// ------------------------------------------------ archive timing (Build 172)

std::optional<archive::Timing> Store::archiveTiming(const std::string& channelId) const {
  const Impl& m = *impl_;
  const Channel* c = nullptr;
  const sources::Account* account = xtreamAccount(m.accounts, *m.channels, m.indexByID, channelId, &c);
  if (!account) return std::nullopt;
  return m.archiveClocks.timing(account->id, c->itemId);
}

void Store::noteArchiveTiming(const std::string& channelId, const archive::Timing& t) {
  Impl& m = *impl_;
  const Channel* c = nullptr;
  const sources::Account* account = xtreamAccount(m.accounts, *m.channels, m.indexByID, channelId, &c);
  if (!account) return;
  bool changed = m.archiveClocks.noteTiming(account->id, c->itemId, t);
  // Build 173: the clock its video showed is the copy's archive clock, and
  // teaches the account's other copies.
  if (t.clock) {
    changed = m.archiveClocks.noteMeasured(account->id, c->itemId, *t.clock) || changed;
    // Build 174: a clock found: the account's failed checks are over.
    changed = m.archiveClocks.noteCheck(account->id, true, time::now()) || changed;
    m.clockCheckAt.erase(account->id);
  }
  if (changed) saveArchiveClocksLater();
}

bool Store::presumedExact(const std::string& channelId) const {
  const Impl& m = *impl_;
  const Channel* c = nullptr;
  const sources::Account* account = xtreamAccount(m.accounts, *m.channels, m.indexByID, channelId, &c);
  return account && m.archiveClocks.allExact(account->id);
}

std::optional<archive::Request> Store::timingRequest(const std::string& channelId) const {
  const Impl& m = *impl_;
  const Channel* c = nullptr;
  const sources::Account* account = xtreamAccount(m.accounts, *m.channels, m.indexByID, channelId, &c);
  if (!account || c->archiveDays == 0) return std::nullopt;
  auto made = sources::xtream::Client::make(*account);
  if (!made) return std::nullopt;
  sources::xtream::Client client = made.value();
  archive::Request req;
  // Live as MPEG-TS: its clock is read from the packets.
  std::optional<std::string> live = client.streamURL(c->itemId, {}, true);
  if (!live) return std::nullopt;
  req.live = std::move(*live);
  if (std::optional<Feed> f = copyFeed(channelId)) {
    size_t at = 0;
    while (at < f->headers.size()) {
      size_t end = f->headers.find('\n', at);
      if (end == std::string::npos) end = f->headers.size();
      std::string line = f->headers.substr(at, end - at);
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
      if (!line.empty()) req.headers.push_back(std::move(line));
      at = end + 1;
    }
  }
  req.clock = archiveClock(channelId).offset.value_or(0);
  int clock = req.clock;
  std::string item = c->itemId;
  req.linkFor = [client, item, clock](time::Seconds at) { return client.timeshiftURL(item, at, 60, clock); };
  time::Seconds now = time::now();
  std::string entry = entryOfChannel(channelId);
  for (const Listing& l : pastListing(entry.empty() ? channelId : entry, now - 8 * 3600))
    if (l.start < now) req.programmeStarts.push_back(l.start);
  return req;
}

bool Store::spareConnection(const std::string& channelId) const {
  const Impl& m = *impl_;
  const Channel* c = nullptr;
  const sources::Account* account = xtreamAccount(m.accounts, *m.channels, m.indexByID, channelId, &c);
  return account && account->maxConnections.value_or(1) >= 2;
}

std::shared_ptr<std::atomic<bool>> Store::measureArchive(const std::string& channelId, uint32_t budgetMs,
                                                         std::function<void(const archive::Outcome&)> done,
                                                         archive::Clock clock, std::optional<time::Seconds> picked,
                                                         uint32_t quickMs, http::Priority priority) {
  std::optional<archive::Request> req = timingRequest(channelId);
  if (!req) return nullptr;
  req->picked = picked;
  req->quickMs = quickMs;
  auto abort = std::make_shared<std::atomic<bool>>(false);
  auto result = std::make_shared<archive::Outcome>();
  if (!clock) clock = probe::steadyClock();
  http::Pool& pool = host_.pool;
  uint64_t gen = impl_->generation;
  time::Seconds now = time::now();
  pool.post(
      [&pool, req = std::move(*req), clock, abort, result, budgetMs, now] {
        *result = archive::measure(pool, req, clock, abort.get(), budgetMs, now);
      },
      [this, gen, channelId, result, abort, done = std::move(done)] {
        if (gen != impl_->generation) return;
        if (result->timing) noteArchiveTiming(channelId, *result->timing);
        if (done) done(*result);
      },
      priority);
  return abort;
}

// ------------------------------------------------ clock checks (Build 174)

Store::ClockTrust Store::clockTrust(const std::string& channelId) const {
  const Impl& m = *impl_;
  const Channel* c = nullptr;
  const sources::Account* account = xtreamAccount(m.accounts, *m.channels, m.indexByID, channelId, &c);
  if (!account) return ClockTrust::Unchecked;
  if (archiveClock(channelId).measured) return ClockTrust::Measured;
  return m.archiveClocks.checkFails(account->id) > 0 ? ClockTrust::Failed : ClockTrust::Unchecked;
}

bool Store::clockCheckReady(const std::string& channelId, uint32_t ms) const {
  const Impl& m = *impl_;
  const Channel* c = nullptr;
  const sources::Account* account = xtreamAccount(m.accounts, *m.channels, m.indexByID, channelId, &c);
  if (!account || m.clockCantTell.count(account->id)) return false;
  auto at = m.clockCheckAt.find(account->id);
  return at == m.clockCheckAt.end() || static_cast<int32_t>(ms - at->second) >= 0;
}

uint32_t Store::clockCheckBackoff(int fails) {
  if (fails <= 1) return 2 * 60000;
  if (fails == 2) return 10 * 60000;
  if (fails == 3) return 30 * 60000;
  return 60 * 60000;
}

uint32_t Store::noteClockCheck(const std::string& channelId, CheckEnd end, uint32_t ms) {
  Impl& m = *impl_;
  const Channel* c = nullptr;
  const sources::Account* account = xtreamAccount(m.accounts, *m.channels, m.indexByID, channelId, &c);
  if (!account) return 0;
  uint32_t wait = 0;
  switch (end) {
    case CheckEnd::Failed:
      if (m.archiveClocks.noteCheck(account->id, false, time::now())) saveArchiveClocksLater();
      wait = clockCheckBackoff(std::max(1, m.archiveClocks.checkFails(account->id)));
      break;
    case CheckEnd::CantTell:
      // Its timestamps aren't the broadcast's: nothing a read can tell, so
      // not read again this run (replays go by its table or server).
      m.clockCantTell.insert(account->id);
      m.clockCheckAt.erase(account->id);
      return 0;
    case CheckEnd::Stopped: wait = 60000; break;
  }
  m.clockCheckAt[account->id] = ms + wait;
  return wait;
}

std::optional<std::string> Store::clockCheckDue(uint32_t ms, const std::string& busyChannel) {
  Impl& m = *impl_;
  if (m.loading || !m.channels || m.channels->empty()) return std::nullopt;
  if (!m.clockReadySince) {
    m.clockReadySince = ms ? ms : 1;
    return std::nullopt;
  }
  if (ms - m.clockReadySince < kClockCheckFirstMs || m.sourceTestRunning || m.clockCheckOut) return std::nullopt;
  const Channel* bc = nullptr;
  const sources::Account* busy =
      busyChannel.empty() ? nullptr : xtreamAccount(m.accounts, *m.channels, m.indexByID, busyChannel, &bc);
  for (const sources::Account& a : m.accounts) {
    if (a.kind != sources::AccountKind::Xtream || m.clockCantTell.count(a.id)) continue;
    int fails = m.archiveClocks.checkFails(a.id);
    // Its clock shown by the video already, and no check failed since.
    if (m.archiveClocks.anyMeasured(a.id) && !fails) continue;
    auto at = m.clockCheckAt.find(a.id);
    if (at != m.clockCheckAt.end() && static_cast<int32_t>(ms - at->second) < 0) continue;
    // Its only connection playing: not now.
    if (busy && busy->id == a.id && a.maxConnections.value_or(1) < 2) continue;
    // Build 178: which of its copies. One Test all sources saw replay (its
    // archive played) first, then those in the lineup, then the rest; never
    // one whose archive it saw fail (an empty or missing archive tells no
    // clock), and a different copy after each try this run, round again
    // once all were tried.
    std::string source = a.sourceId();
    std::string pick;
    int best = 3;
    bool any = false;
    std::set<std::string>& tried = m.clockTried[a.id];
    for (int pass = 0; pass < 2 && pick.empty(); ++pass) {
      for (const Channel& c : *m.channels) {
        if (c.serverId != source || c.archiveDays == 0) continue;
        const scores::Record* rec = m.scores.find(c.id);
        if (rec && rec->archiveOk == 0) continue;
        any = true;
        if (tried.count(c.id)) continue;
        int rank = rec && rec->archiveOk == 1 ? 0 : (m.catalog && m.catalog->entryOf.count(c.id)) ? 1 : 2;
        if (rank < best) {
          best = rank;
          pick = c.id;
          if (rank == 0) break;
        }
      }
      // All tried: round again.
      if (pick.empty() && any) tried.clear();
    }
    if (pick.empty()) {
      // No replay here (none kept, or every one tested broken): looked at
      // again in an hour.
      m.clockCheckAt[a.id] = ms + 60 * 60000;
      continue;
    }
    tried.insert(pick);
    return pick;
  }
  return std::nullopt;
}

void Store::clockCheckOut(bool out) { impl_->clockCheckOut = out; }

void Store::noteSourceTest(bool running) { impl_->sourceTestRunning = running; }

std::vector<std::string> Store::takeGuideLog() {
  std::vector<std::string> out;
  out.swap(impl_->guideNotes);
  return out;
}

}  // namespace marigold::engine
