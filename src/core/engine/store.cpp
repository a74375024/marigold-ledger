// The store: a port of the parts of LiveTVStore (tvOS) that milestone 1
// needs, on the TV's worker pool. Everything here runs on the UI thread
// except the lambdas handed to pool().post() / afterTransfer / onChunk,
// which only touch what they captured (by value or shared_ptr).
#include "core/engine/store.hpp"
#include "core/engine/viewer_lists.hpp"
#include "core/engine/store_impl.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <unordered_set>

#include "core/catalog/admission.hpp"
#include "core/catalog/cache.hpp"
#include "core/catalog/catalog.hpp"
#include "core/catalog/directory.hpp"
#include "core/catalog/lineup.hpp"
#include "core/catalog/markets.hpp"
#include "core/catalog/reference.hpp"
#include "core/catalog/wiki_data.hpp"
#include "core/catalog/work.hpp"
#include "core/guide/genre.hpp"
#include "core/guide/outside.hpp"
#include "core/guide/names.hpp"
#include "core/guide/provider.hpp"
#include "core/guide/stations.hpp"
#include "core/guide/xmltv.hpp"
#include "core/logos/logos.hpp"
#include "core/sources/account.hpp"
#include "core/sources/emby.hpp"
#include "core/sources/m3u.hpp"
#include "core/sources/xtream.hpp"
#include "core/sports/brands.hpp"
#include "core/util/gunzip.hpp"
#include "core/util/json.hpp"
#include "core/util/json_doc.hpp"
#include "core/util/log.hpp"
#include "core/util/memory.hpp"
#include "core/util/redact.hpp"
#include "core/util/strings.hpp"

namespace marigold::engine {

namespace {

using catalog::CatalogResult;

// The built-in channel directory, when the build has one.
const catalog::directory::Index* builtInDirectory() {
  return catalog::directory::builtIn().empty() ? nullptr : &catalog::directory::builtInIndex();
}

// The built-in lineup reference, when the build has one.
const catalog::reference::Index* builtInReference() {
  return catalog::reference::builtIn().empty() ? nullptr : &catalog::reference::builtInIndex();
}

// The channel names the area suggestion reads: local-looking ones first
// (tvOS).
std::vector<std::string> suggestionNames(const std::vector<Channel>& list) {
  std::vector<std::string> names;
  for (const Channel& c : list) {
    std::string group = str::lowerAscii(c.group.value_or(""));
    if (group.find("local") != std::string::npos || guide::bareCallsign(c.name)) names.push_back(c.name);
  }
  if (names.empty()) {
    for (const Channel& c : list) names.push_back(c.name);
  }
  if (names.size() > 6000) names.resize(6000);
  return names;
}

// A lineup row's channel directory entry (nullptr when none).
const catalog::directory::Entry* directoryEntry(const catalog::EntryInfo* entry) {
  if (!entry || entry->directoryId.empty()) return nullptr;
  const catalog::directory::Index* index = builtInDirectory();
  return index ? index->entryById(entry->directoryId) : nullptr;
}

// Its iptv-org id ("" when none), for its fallback logo.
std::string directoryIptvOrg(const catalog::EntryInfo* entry) {
  const catalog::directory::Entry* listed = directoryEntry(entry);
  return listed ? listed->iptvOrg : std::string();
}

// The directory's Wikidata logo for a row's channel (empty for none).
std::string directoryLogo(const catalog::EntryInfo* entry) {
  const catalog::directory::Entry* listed = directoryEntry(entry);
  return listed ? listed->logo : std::string();
}

using catalog::EntryInfo;

// LiveTVStore constants.
constexpr time::Seconds kChannelLifetime = 30 * 60;  // channel list re-read
// Marigold: that often only while games matter (Live Events open or used
// in the last few hours, a reminder coming up); otherwise every 2 hours.
// Opening Live Events re-reads a list older than kChannelFreshForEvents.
constexpr time::Seconds kChannelLifetimeQuiet = 2 * 3600;
constexpr time::Seconds kGamesMatterFor = 3 * 3600;
constexpr time::Seconds kChannelFreshForEvents = 10 * 60;
// A provider group's starts count from its last one a week ago at most.
constexpr time::Seconds kGroupStartsFor = 7 * 86400;
// A list where only event names changed is saved at most this often (the
// next launch's download renames them again within seconds).
constexpr time::Seconds kRenamedSaveEvery = 6 * 3600;
// After a big job, memory is handed back once nothing has needed the CPU
// for this long (no key, no channel starting), or after the latest wait.
constexpr uint32_t kReleaseIdleMs = 2000;
constexpr uint32_t kReleaseLatestMs = 60000;
constexpr size_t kProgramBatch = 25;
constexpr int kGuideConcurrency = 4;
// Requests per batch at once: with kGuideConcurrency batches, at most 8
// short-EPG requests in flight (it was 24), so a burst of guide rows can't
// take every worker.
constexpr int kShortEpgParallel = 2;
// A row whose listings still run this long past a new half-hour window
// keeps them; only rows about to run out are asked again.
constexpr time::Seconds kListingsAheadEnough = 90 * 60;
constexpr size_t kRecentMax = 30;
constexpr size_t kRecentlyRequestedMax = 300;
// Build 153: rows asked for on screen that are weighed by channel identity
// (guide/choice.hpp) besides My Lineup and favourites.
constexpr size_t kChoiceRowsMax = 1500;
constexpr double kGuideHours = 3.0;
constexpr uint32_t kGuideFlushMs = 120;
constexpr uint32_t kProviderDelayMs = 4000;
constexpr uint32_t kOutsideDelayMs = 8000;
// Whether a guide is due by its age is looked at this often (and on a
// wake); one that started lately waits as long as a stale outside guide
// does before it is tried again.
constexpr uint32_t kGuidesLookMs = 60000;
constexpr time::Seconds kGuideRetryAfter = guide::kStaleRetry;
constexpr uint32_t kLineupRefreshMs = 600;
// Timing lines: at most one a minute.
constexpr uint32_t kTimingLineMs = 60000;
constexpr const char* kOtherGroup = "Other";

constexpr const char* kAccountsFile = "accounts.json";
constexpr const char* kSecretsFile = "accounts-secrets.json";
constexpr const char* kSettingsFile = "settings.json";
constexpr const char* kListsFile = "lists.json";
constexpr const char* kLineupFile = "lineup.json";
constexpr const char* kPicturesFile = "pictures.json";  // measured pictures (Build 136)
constexpr const char* kM3uStreamsFile = "m3u-streams.json";  // secret
constexpr const char* kStationsFile = "stations-v2.json";  // v2: two-city cells, MyNetworkTV
constexpr const char* kOldLogoIndexFile = "logo-index.json";  // downloaded by builds before 89

http::Request toHttp(const sources::Request& r, std::string tag, http::Priority p = http::Priority::Normal) {
  http::Request q;
  q.method = r.method == "GET" ? std::string() : r.method;
  q.url = r.url;
  for (const sources::Header& h : r.headers) q.headers.push_back(h.name + ": " + h.value);
  q.body = r.body;
  q.timeoutMs = static_cast<unsigned>(std::max(1, r.timeoutSeconds)) * 1000u;
  q.personalServer = r.allowInsecureTLS;
  q.priority = p;
  q.tag = std::move(tag);
  return q;
}

std::string randomId() {
  uint64_t seed = static_cast<uint64_t>(time::now()) * 1000003u ^ static_cast<uint64_t>(getpid()) ^
                  reinterpret_cast<uintptr_t>(&seed);
  std::FILE* f = std::fopen("/dev/urandom", "rb");
  if (f) {
    uint64_t r = 0;
    if (std::fread(&r, sizeof r, 1, f) == 1) seed ^= r;
    std::fclose(f);
  }
  return str::hex64(str::fnv1a64(std::to_string(seed))).substr(0, 12);
}

// Waits while full screen video plays (big downloads).
struct GuideReadStateLite {
  std::shared_ptr<std::atomic<bool>> playing, stop;
};

// Counts a foreground job while it runs on a worker (see foregroundJobs).
class Foreground {
 public:
  explicit Foreground(std::shared_ptr<std::atomic<int>> count) : count_(std::move(count)) { ++*count_; }
  ~Foreground() { --*count_; }
  Foreground(const Foreground&) = delete;
  Foreground& operator=(const Foreground&) = delete;

 private:
  std::shared_ptr<std::atomic<int>> count_;
};

// Accounts with a full guide file: Xtream's xmltv.php, or an M3U guide link.
bool hasGuideFile(const sources::Account& a) {
  return a.kind == sources::AccountKind::Xtream || (a.kind == sources::AccountKind::M3U && !a.guideUrl.empty());
}

// A provider with a guide file has none saved yet: its first guide.
bool providerGuideMissing(const Store::Impl& m) {
  for (const sources::Account& a : m.accounts)
    if (hasGuideFile(a) && !m.providerSaved.count(guide::provider::cacheKey("default", a.sourceId()))) return true;
  return false;
}

std::string hostOf(const std::string& url) {
  size_t s = url.find("://");
  size_t start = s == std::string::npos ? 0 : s + 3;
  size_t end = url.find_first_of("/?#", start);
  std::string host = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
  size_t at = host.rfind('@');
  if (at != std::string::npos) host = host.substr(at + 1);
  return host;
}

Listing toListing(const Program& p) {
  Listing l;
  l.title = p.title;
  l.episode = p.episodeTitle.value_or("");
  l.overview = p.overview;
  l.start = p.start;
  l.end = p.end;
  l.genres = p.genres;
  l.live = p.isLive;
  l.premiere = p.isPremiere;
  l.outside = p.fromOutsideGuide;
  return l;
}

// One row's listings in time order with no overlaps. Merged guides (short
// EPG, the provider's file, outside listings) and providers that repeat a
// programme can hold two programmes for the same minutes, which the guide
// drew one over the other. The first one listed for a time wins; one that
// runs on past it keeps only the rest of its time.
std::vector<Listing> cleanListings(const std::vector<Program>& programs) {
  std::vector<Listing> all;
  all.reserve(programs.size());
  for (const Program& p : programs)
    if (p.end > p.start) all.push_back(toListing(p));
  std::stable_sort(all.begin(), all.end(), [](const Listing& a, const Listing& b) { return a.start < b.start; });
  std::vector<Listing> out;
  out.reserve(all.size());
  for (Listing& l : all) {
    if (!out.empty() && l.start < out.back().end) {
      if (l.end <= out.back().end + 60) continue;  // covered by the one before
      l.start = out.back().end;
    }
    out.push_back(std::move(l));
  }
  return out;
}

uint8_t titleGenres(const std::string& title, const std::optional<std::string>& episode) {
  return static_cast<uint8_t>(guide::genre::flagsForTitle(title, episode));
}

bool readFile(const std::string& path, std::string& out) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  out.clear();
  char buf[64 * 1024];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
  std::fclose(f);
  return true;
}

// The size of the file at `path` now (0: none): about what its next
// version needs on top of it while both are on disk.
uint64_t sizeOnDisk(const std::string& path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 ? static_cast<uint64_t>(st.st_size) : 0;
}

// Writes `data` next to `path` and renames it into place. A big one waits
// for room on the partition (`keeper`, disk.hpp); a failed one leaves the
// old file as it was and no temporary file behind.
bool writeFileAtomic(const std::string& path, const std::string& data, bool secret = false,
                     disk::Keeper* keeper = nullptr) {
  if (keeper && !keeper->roomFor(path, data.size())) return false;
  std::string tmp = path + ".tmp";
  bool ok;
  int err = 0;
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    f.close();  // the last of it reaches the disk here, and can fail
    ok = !f.fail();
    if (!ok) err = errno;
  }
  if (ok && secret) chmod(tmp.c_str(), 0600);
  if (!ok || std::rename(tmp.c_str(), path.c_str()) != 0) {
    if (keeper) keeper->failed(path, ok ? errno : err);
    std::remove(tmp.c_str());
    return false;
  }
  return true;
}

// Writes a file a piece at a time (`write` sends its chunks to the sink),
// next to `path`, and renames it into place. With a `keeper` it starts only
// with room for about the old file's size, and stops (the old file kept)
// when the partition gets down to the margin while it writes.
bool writeFileStreaming(const std::string& path, const std::function<bool(const json::Writer::Sink&)>& write,
                        bool gzip = false, disk::Keeper* keeper = nullptr) {
  if (keeper && !keeper->roomFor(path, std::max(sizeOnDisk(path), keeper->limits().bigWrite))) return false;
  std::string tmp = path + ".tmp";
  bool ok, full = false;
  int err = 0;
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    uint64_t written = 0, nextLook = keeper ? keeper->limits().recheckEvery : 0;
    auto toFile = [&](std::string_view chunk) {
      f.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
      written += chunk.size();
      if (keeper && written >= nextLook) {
        nextLook = written + keeper->limits().recheckEvery;
        if (!keeper->stillRoom(path)) full = true;
      }
      return static_cast<bool>(f) && !full;
    };
    if (gzip) {
      // A fraction of the size on flash (JSON packs well), written as made.
      gz::Deflate pack(toFile);
      ok = write([&pack](std::string_view chunk) { return pack.feed(chunk); }) && pack.finish();
    } else {
      ok = write(toFile);
    }
    f.close();  // the last of it reaches the disk here, and can fail
    ok = ok && !f.fail() && !full;
    if (!ok && !full) err = errno;
  }
  if (!ok) {
    if (keeper && !full) keeper->failed(path, err);
    std::remove(tmp.c_str());
    return false;
  }
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    if (keeper) keeper->failed(path, errno);
    std::remove(tmp.c_str());
    return false;
  }
  return true;
}

using SteadyClock = std::chrono::steady_clock;

// Near enough the process's start (static set-up runs before main), for
// the launch timing line.
const SteadyClock::time_point kProcessStart = SteadyClock::now();

double msSince(SteadyClock::time_point t) {
  return std::chrono::duration<double, std::milli>(SteadyClock::now() - t).count();
}

// Lets go of a big snapshot on a worker (thousands of rows' strings take
// milliseconds to free); one a screen still holds goes when it lets go.
template <class T>
void releaseLater(http::Pool& pool, std::shared_ptr<T> held) {
  if (!held) return;
  pool.post([last = std::move(held)]() mutable { last.reset(); }, nullptr, http::Priority::Background);
}

// ---- guide rows

// The rows' list-dependent fields from a lineup and the catalog and list it
// was made from: on the lineup's worker, or on the spot when a publish finds
// none for them. `entryAt` and `channelAt` are the store's own indexes when
// they belong to `cat` and `list` (else made here).
std::shared_ptr<ChannelRows> makeRows(const catalog::LineupOutput& lo, const CatalogResult& cat,
                                      const std::vector<Channel>& list, const logos::IptvOrgIndex* iptvOrg,
                                      const std::unordered_map<std::string, size_t>* entryAt,
                                      const std::unordered_map<std::string, size_t>* channelAt) {
  std::unordered_map<std::string, size_t> ownEntries, ownChannels;
  if (!entryAt) {
    ownEntries.reserve(cat.entries.size());
    for (size_t i = 0; i < cat.entries.size(); ++i) ownEntries.emplace(cat.entries[i].id, i);
    entryAt = &ownEntries;
  }
  if (!channelAt) {
    // Only each row's first version is looked up (the first of an id wins,
    // as in the store's index).
    constexpr size_t kNone = ~size_t(0);
    for (const Channel& e : lo.entries) {
      auto info = entryAt->find(e.id);
      if (info != entryAt->end() && !cat.entries[info->second].members.empty())
        ownChannels.emplace(cat.entries[info->second].members.front(), kNone);
    }
    for (size_t i = 0; i < list.size(); ++i) {
      auto it = ownChannels.find(list[i].id);
      if (it != ownChannels.end() && it->second == kNone) it->second = i;
    }
    for (auto it = ownChannels.begin(); it != ownChannels.end();) {
      if (it->second == kNone) it = ownChannels.erase(it);
      else ++it;
    }
    channelAt = &ownChannels;
  }
  auto out = std::make_shared<ChannelRows>();
  for (const EntryInfo& e : cat.entries) {
    for (const std::string& former : e.formerIds) out->renamed.emplace(former, e.id);
  }
  out->all.reserve(lo.entries.size());
  out->rows.reserve(lo.entries.size());
  out->byId.reserve(lo.entries.size());
  for (const Channel& e : lo.entries) {
    GuideChannel g;
    ChannelRows::Row row;
    g.id = e.id;
    g.radio = e.isRadio;
    auto info = entryAt->find(e.id);
    const EntryInfo* ei = info == entryAt->end() ? nullptr : &cat.entries[info->second];
    catalog::LineupCategory category = ei ? ei->category : catalog::LineupCategory::entertainment;
    row.category = category;
    g.section = catalog::genreTitle(category);
    g.sectionRank = catalog::rank(category);
    // Build 148: a picked area's station under its area, with two or more.
    if (category == catalog::LineupCategory::local)
      if (const std::string* area = lo.areaSection(e.id)) g.section = *area;
    g.name = ei ? ei->name : e.displayName();
    g.group = catalog::groupOf(e);
    if (g.group.empty()) g.group = kOtherGroup;
    g.feeds = ei ? static_cast<int>(ei->members.size()) : 1;
    g.west = catalog::isWestId(e.id);
    if (std::string other = catalog::coastPartnerId(e.id); lo.entryIndex.count(other)) g.partner = other;
    row.serverFavorite = e.isFavorite;
    // Which logo a row tries and how its cell reads (ChannelLogos.spec).
    const Channel* member = nullptr;
    if (ei && !ei->members.empty()) {
      auto ci = channelAt->find(ei->members.front());
      if (ci != channelAt->end()) member = &list[ci->second];
    }
    bool local = category == catalog::LineupCategory::local;
    // A station shows as one in whatever section it sits (a Telemundo
    // station under Spanish).
    logos::LogoSpec spec = logos::spec(g.name, ei ? ei->core : std::nullopt, g.west, ei && ei->callsign,
                                       ei ? ei->callsign : std::nullopt, ei ? ei->virtualNumber : std::nullopt,
                                       e.imageUrl, member ? member->epgId : std::nullopt, category, iptvOrg,
                                       directoryIptvOrg(ei), ei ? logos::referenceIdOf(ei->id) : std::string(),
                                       directoryLogo(ei));
    g.logoUrl = logos::chain(spec.urls);
    g.logoSources = str::join(spec.sources, ", ");
    g.subtitle = spec.subtitle.value_or(catalog::genreTitle(category));
    switch (spec.style) {
      case logos::LogoSpec::Style::local:
        g.style = GuideChannel::Style::Local;
        if (!spec.callsign.empty()) g.name = spec.callsign;
        g.subtitle = spec.line;
        break;
      case logos::LogoSpec::Style::logoOnly:
        g.style = g.logoUrl.empty() ? GuideChannel::Style::Named : GuideChannel::Style::LogoOnly;
        break;
      case logos::LogoSpec::Style::named:
        g.style = GuideChannel::Style::Named;
        break;
    }
    // Locals go by their broadcast number (the rest by blocks, at publish).
    if (ei && local && ei->virtualNumber) row.number = *ei->virtualNumber;
    out->byId.emplace(g.id, static_cast<int>(out->all.size()));
    out->all.push_back(std::move(g));
    out->rows.push_back(row);
  }
  for (const Channel& e : lo.lineupEntries) {
    auto it = out->byId.find(e.id);
    if (it != out->byId.end()) out->lineup.push_back(it->second);
  }
  for (const Channel& e : lo.browseEntries) {
    auto it = out->byId.find(e.id);
    if (it != out->byId.end()) out->browse.push_back(it->second);
  }
  return out;
}

// Build 149: a row's star. The viewer's favourite, or the server's own
// (Emby / Jellyfin) unless removed here. The lineup's build takes the
// viewer's favourites in only for membership, never for the star (they
// stuck until the next build).
bool favoriteNow(const std::string& id, bool serverFavorite, const std::set<std::string>& favorites,
                 const std::set<std::string>& unfavorited) {
  return favorites.count(id) > 0 || (serverFavorite && unfavorited.count(id) == 0);
}

// What a publish sets on the rows: the viewer's lists and the lineup's
// membership. The store's own, or copies for a worker.
struct RowFlags {
  const std::set<std::string>* lineupIDs = nullptr;
  const std::set<std::string>* favorites = nullptr;
  const std::set<std::string>* unfavorited = nullptr;
  const std::set<std::string>* hiddenGroups = nullptr;
  const std::vector<std::string>* recents = nullptr;
  bool configured = false;
  size_t rawChannels = 0;
};

RowFlags rowFlags(const Store::Impl& m) {
  return RowFlags{&m.lineup.lineupIDs, &m.favorites,        &m.unfavorited, &m.hiddenGroups,
                  &m.recents,          m.prefs.configured, m.channels->size()};
}

// The same, copied (a few hundred ids) for a patch made on a worker.
struct RowFlagsCopy {
  std::set<std::string> lineupIDs, favorites, unfavorited, hiddenGroups;
  std::vector<std::string> recents;
  RowFlags flags;
  explicit RowFlagsCopy(const Store::Impl& m)
      : lineupIDs(m.lineup.lineupIDs),
        favorites(m.favorites),
        unfavorited(m.unfavorited),
        hiddenGroups(m.hiddenGroups),
        recents(m.recents),
        flags{&lineupIDs, &favorites, &unfavorited, &hiddenGroups, &recents, m.prefs.configured, m.channels->size()} {}
  RowFlagsCopy(const RowFlagsCopy&) = delete;
  RowFlagsCopy& operator=(const RowFlagsCopy&) = delete;
};

// A publish: the rows' fields copied, then what the viewer's lists and the
// lineup's membership make of them (favourites, recents, numbers, hidden
// groups). Milliseconds (the copy) where working out every row took tens.
std::shared_ptr<Channels> patchRows(const ChannelRows& r, const RowFlags& f) {
  auto out = std::make_shared<Channels>();
  out->rawChannels = f.rawChannels;
  out->lineupConfigured = f.configured;
  out->renamed = r.renamed;
  out->byId = r.byId;
  out->all = r.all;
  std::map<catalog::LineupCategory, int> blockNext;
  for (size_t i = 0; i < out->all.size(); ++i) {
    GuideChannel& g = out->all[i];
    const ChannelRows::Row& row = r.rows[i];
    g.inLineup = f.lineupIDs->count(g.id) > 0;
    g.favorite = favoriteNow(g.id, row.serverFavorite, *f.favorites, *f.unfavorited);
    // Locals by their broadcast number; the rest by cable-style blocks.
    if (row.number) {
      g.number = row.number;
    } else if (g.inLineup || !f.configured) {
      int& next = blockNext[row.category];
      if (next == 0)
        next = catalog::numberBlock(row.category) + (row.category == catalog::LineupCategory::local ? 50 : 0);
      g.number = next++;
    }
    g.browsable = false;
  }
  out->lineup = r.lineup;
  for (int i : r.browse) {
    GuideChannel& g = out->all[static_cast<size_t>(i)];
    g.browsable = true;
    if (f.hiddenGroups->count(g.group)) continue;
    out->browse.push_back(i);
  }
  out->every.reserve(out->all.size());
  for (size_t i = 0; i < out->all.size(); ++i) {
    out->every.push_back(static_cast<int>(i));
    if (out->all[i].favorite) out->favorites.push_back(static_cast<int>(i));
  }
  for (const std::string& id : *f.recents) {
    auto it = out->byId.find(id);
    if (it != out->byId.end()) out->recent.push_back(it->second);
  }
  return out;
}

// ---- M3U links

// A set of links' fingerprint (only a changed set is written).
uint64_t linksPrint(const StreamLinks::Map& links) {
  uint64_t print = 1469598103934665603ull;
  auto mix = [&print](std::string_view text) {
    for (unsigned char c : text) print = (print ^ c) * 1099511628211ull;
    print = (print ^ 0xff) * 1099511628211ull;
  };
  std::vector<const std::pair<const std::string, sources::m3u::StreamInfo>*> sorted;
  sorted.reserve(links.size());
  for (const auto& link : links) sorted.push_back(&link);
  std::sort(sorted.begin(), sorted.end(), [](const auto* a, const auto* b) { return a->first < b->first; });
  for (const auto* link : sorted) {
    mix(link->first);
    mix(link->second.url);
    for (const std::string& line : link->second.headers) mix(line);
  }
  return print;
}

// Reads the saved links once (the caller holds `work`).
void readLinks(StreamLinks& l, const Files& files) {
  {
    std::lock_guard<std::mutex> lock(l.mu);
    if (l.read) return;
  }
  auto map = std::make_shared<StreamLinks::Map>();
  bool saved = false;
  if (auto text = files.read(kM3uStreamsFile)) {
    if (auto v = json::parse(*text)) {
      saved = true;
      for (const auto& [id, x] : v->fields()) {
        sources::m3u::StreamInfo info;
        info.url = x["url"].asString();
        for (const json::Value& h : x["headers"].items()) info.headers.push_back(h.asString());
        (*map)[id] = std::move(info);
      }
    }
  }
  // The same links downloaded again aren't written again.
  l.savedPrint = saved ? linksPrint(*map) : 0;
  std::lock_guard<std::mutex> lock(l.mu);
  l.map = std::move(map);
  l.read = true;
}

// The links now (read here first, or waited for, when a tune comes before
// the read on its worker is done).
std::shared_ptr<const StreamLinks::Map> currentLinks(StreamLinks& l, const Files& files) {
  {
    std::lock_guard<std::mutex> lock(l.mu);
    if (l.read) return l.map;
  }
  std::lock_guard<std::mutex> work(l.work);
  readLinks(l, files);
  std::lock_guard<std::mutex> lock(l.mu);
  return l.map;
}

// Writes the newest links, unless a job before this one has (the file is
// secret: 0600).
void writeLinks(StreamLinks& l, const std::string& path) {
  std::lock_guard<std::mutex> file(l.fileMu);
  uint64_t asked = l.writes.load();
  if (asked <= l.written) return;
  std::shared_ptr<const StreamLinks::Map> map;
  {
    std::lock_guard<std::mutex> lock(l.mu);
    map = l.map;
  }
  json::Value v = json::Value::object();
  for (const auto& [id, info] : *map) {
    json::Value x = json::Value::object();
    x.set("url", info.url);
    json::Value h = json::Value::array();
    for (const std::string& line : info.headers) h.push(line);
    x.set("headers", h);
    v.set(id, x);
  }
  writeFileAtomic(path, json::write(v), true, l.disk.get());
  l.written = asked;
}

// A download's links, merged on a worker: the accounts that answered
// (`prefixes`, "m3u:..|") lose their old links and take the new ones; the
// rest keep theirs. A changed set is written in the background.
struct LinksUpdate {
  std::vector<std::string> prefixes;
  std::vector<StreamLinks::Map> fresh;
};

void updateLinks(const std::shared_ptr<StreamLinks>& l, LinksUpdate& u, const Files& files, http::Pool& pool,
                 const std::string& path) {
  std::lock_guard<std::mutex> work(l->work);
  readLinks(*l, files);
  std::shared_ptr<const StreamLinks::Map> before;
  {
    std::lock_guard<std::mutex> lock(l->mu);
    before = l->map;
  }
  auto next = std::make_shared<StreamLinks::Map>();
  next->reserve(before->size());
  for (const auto& [id, info] : *before) {
    bool replaced = std::any_of(u.prefixes.begin(), u.prefixes.end(),
                                [&key = id](const std::string& p) { return str::startsWith(key, p); });
    if (!replaced) next->emplace(id, info);
  }
  for (StreamLinks::Map& fresh : u.fresh) {
    for (auto& [id, info] : fresh) (*next)[id] = std::move(info);
    fresh.clear();
  }
  uint64_t print = linksPrint(*next);
  {
    std::lock_guard<std::mutex> lock(l->mu);
    l->map = std::move(next);
  }
  if (print == l->savedPrint) return;
  l->savedPrint = print;
  ++l->writes;
  pool.post([l, path] { writeLinks(*l, path); }, nullptr, http::Priority::Background);
}

}  // namespace


// ------------------------------------------------------------ persistence

namespace {

std::set<std::string> stringSet(const json::Value& v) {
  std::set<std::string> out;
  for (const json::Value& x : v.items())
    if (x.isString()) out.insert(x.asString());
  return out;
}

json::Value stringArray(const std::vector<std::string>& v) {
  json::Value a = json::Value::array();
  for (const std::string& s : v) a.push(s);
  return a;
}

}  // namespace

Store::Store(Host host) : host_(std::move(host)), impl_(std::make_unique<Impl>()) {
  auto started = SteadyClock::now();
  Files& st = host_.files;
  Impl& m = *impl_;
  m.disk = std::make_shared<disk::Keeper>(st.dir(), host_.freeSpace);
  m.links->disk = m.disk;

  if (auto main = st.read(kAccountsFile)) {
    auto decoded = sources::decodeAccounts(*main, st.read(kSecretsFile).value_or(""));
    if (decoded) m.accounts = decoded.take();
    else {
      MLOG_W("store", "accounts file unreadable: %s", decoded.error().message.c_str());
      m.accountsUnreadable = true;
    }
  }
  if (auto text = st.read(kSettingsFile)) {
    if (auto v = json::parse(*text)) {
      settings_.previewChannels = (*v)["previewChannels"].asBool(true);
      settings_.allChannelsTab = (*v)["allChannelsTab"].asBool(false);
      settings_.preferTS = (*v)["preferTS"].asBool(true);
      settings_.embyQuickStart = (*v)["embyQuickStart"].asBool(false);
      settings_.fillMissingListings = (*v)["fillMissingListings"].asBool(true);
      settings_.showOther = (*v)["showOther"].asBool(false);
      settings_.scheduleEnabled = (*v)["scheduleEnabled"].asBool(true);
      settings_.showUnwatchable = (*v)["showUnwatchable"].asBool(true);
      settings_.boardMoreEvents = (*v)["boardMoreEvents"].asBool(false);
      settings_.boardUntimed = (*v)["boardUntimed"].asBool(false);
      settings_.boardEventChannels = (*v)["boardEventChannels"].asBool(false);
      settings_.boardUnscheduled = (*v)["boardUnscheduled"].asBool(false);
      settings_.showScores = (*v)["showScores"].asBool(true);
      settings_.clock24 = (*v)["clock24"].asBool(false);
      settings_.recentSearches = (*v)["recentSearches"].asBool(true);
      settings_.playerTipsShown = (*v)["playerTipsShown"].asBool(false);
      settings_.subtitles = (*v)["subtitles"].asBool(false);
      settings_.subtitleLanguage = (*v)["subtitleLanguage"].asString();
      // Before the two choices there was one, for every channel.
      settings_.subtitlesEs = (*v)["subtitlesEs"].asBool(settings_.subtitles);
      settings_.subtitleLanguageEs = (*v)["subtitleLanguageEs"].asString();
      settings_.subtitleSize = std::clamp(static_cast<int>((*v)["subtitleSize"].asNumber(1)), 0, 3);
      settings_.subtitleLook = std::clamp(static_cast<int>((*v)["subtitleLook"].asNumber(0)), 0, 2);
      settings_.subtitleSentenceCase = (*v)["subtitleSentenceCase"].asBool(false);
      settings_.subtitleTop = (*v)["subtitleTop"].asBool(false);
      settings_.captionTiming = clampCaptionTiming(static_cast<int>((*v)["captionTiming"].asNumber(0)));
      for (const auto& [k, x] : (*v)["captionTimingByChannel"].fields())
        if (!k.empty()) settings_.captionTimingByChannel[k] = clampCaptionTiming(static_cast<int>(x.asNumber(0)));
      settings_.playThroughApp = (*v)["playThroughApp"].asBool(true);
      settings_.upGoesHigher = (*v)["upGoesHigher"].asBool(false);
      settings_.systemCaptions = static_cast<int>((*v)["systemCaptions"].asNumber(-1));
      settings_.guideShards = stringSet((*v)["guideShards"]);
    }
  }
  if (auto text = st.read(kListsFile)) {
    if (auto v = json::parse(*text)) {
      m.favorites = stringSet((*v)["favorites"]);
      m.unfavorited = stringSet((*v)["unfavorited"]);
      m.hiddenGroups = stringSet((*v)["hiddenGroups"]);
      for (const json::Value& x : (*v)["recents"].items())
        if (x.isString()) m.recents.push_back(x.asString());
      for (const json::Value& x : (*v)["searches"].items()) {
        search::Recent r{x["name"].asString(), x["id"].asString()};
        if (m.searches.size() < search::kRecentKept && !r.name.empty() && !r.id.empty()) m.searches.push_back(r);
      }
      for (const auto& [k, x] : (*v)["pinned"].fields()) m.pinned[k] = x.asString();
      for (const auto& [k, x] : (*v)["guidePicks"].fields())
        if (x.isString() && !x.asString().empty()) m.guidePicks[k] = x.asString();
      for (const auto& [k, x] : (*v)["lastFeed"].fields()) m.lastFeed[k] = x.asString();
      for (const auto& [k, x] : (*v)["unplayable"].fields()) m.unplayable[k] = x.asInt();
      for (const auto& [k, x] : (*v)["audioOnly"].fields()) m.audioOnly[k] = x.asInt();
      m.quickStartRefused = stringSet((*v)["quickStartRefused"]);
      m.captionChannels = stringSet((*v)["captionChannels"]);
      for (const auto& [k, x] : (*v)["reliability"].fields())
        m.reliability[k] = {static_cast<int>(x[size_t(0)].asInt()), static_cast<int>(x[size_t(1)].asInt())};
      for (const auto& [k, x] : (*v)["groupStarts"].fields())
        m.groupStarts[k] = {static_cast<int>(x[size_t(0)].asInt()), static_cast<int>(x[size_t(1)].asInt()),
                            x[size_t(2)].asInt()};
    }
  }
  if (auto text = st.read(kLineupFile)) {
    if (auto p = catalog::lineupPrefsFromJson(*text)) m.prefs = *p;
  }
  if (auto text = st.read(kPicturesFile)) m.pictures = quality::Memory::fromJson(*text);
  // The saved M3U links (tens of thousands) are read on a worker once the
  // first frame is up (tick): only a tune needs them.
  for (sources::Account& a : m.accounts) {
    if (settings_.preferTS) a.preferTS = true;
    m.runs[a.sourceId()];
  }
  readStations();
  useBuiltInLogos();
  loadEvents();
  MLOG_I("store", "%zu provider(s)", m.accounts.size());
  if (m.accounts.empty()) {
    status_.phase = StoreStatus::Phase::NoAccounts;
  } else {
    status_.phase = StoreStatus::Phase::Loading;
    status_.line = "Loading channels\xe2\x80\xa6";
    load(false);
  }
  // What an older session left: a removed provider's guide, an old list.
  pruneDisk();
  m.launch.ready = msSince(kProcessStart);
  MLOG_I("store", "timing opened=%.1fms", msSince(started));
}

Store::~Store() {
  *impl_->stopJobs = true;
  // Recent searches not on file yet are written now.
  Impl::ListsWriter& w = *impl_->listsWriter;
  std::string text;
  uint64_t seq = 0;
  {
    std::lock_guard<std::mutex> lock(w.mu);
    text.swap(w.pending);
    std::swap(seq, w.pendingSeq);
  }
  std::lock_guard<std::mutex> lock(w.fileMu);
  if (seq > w.written) {
    host_.files.write(kListsFile, text);
    w.written = seq;
  }
}

void Store::saveAccounts() {
  sources::EncodedAccounts enc = sources::encodeAccounts(impl_->accounts);
  // Rewritten after every download otherwise; only a change goes to flash.
  if (enc.main == impl_->savedAccounts && enc.secrets == impl_->savedSecrets) return;
  host_.files.write(kAccountsFile, enc.main);
  host_.files.write(kSecretsFile, enc.secrets, true);
  impl_->savedAccounts = std::move(enc.main);
  impl_->savedSecrets = std::move(enc.secrets);
  impl_->accountsUnreadable = false;  // the file on disk is ours again
}

void Store::saveLists() {
  std::string text = listsText();
  Impl::ListsWriter& w = *impl_->listsWriter;
  uint64_t seq = 0;
  {
    // Supersedes a write still waiting for a worker.
    std::lock_guard<std::mutex> lock(w.mu);
    seq = ++w.seq;
    w.pending.clear();
    w.pendingSeq = 0;
  }
  std::lock_guard<std::mutex> lock(w.fileMu);
  host_.files.write(kListsFile, text);
  w.written = seq;
}

void Store::saveListsLater() {
  std::shared_ptr<Impl::ListsWriter> w = impl_->listsWriter;
  {
    std::lock_guard<std::mutex> lock(w->mu);
    w->pending = listsText();
    w->pendingSeq = ++w->seq;
  }
  // Each job writes the newest list, if a job before it hasn't: a burst of
  // changes is written once or twice.
  Files* files = &host_.files;
  host_.pool.post(
      [w, files] {
        std::string text;
        uint64_t seq = 0;
        {
          std::lock_guard<std::mutex> lock(w->mu);
          text.swap(w->pending);
          std::swap(seq, w->pendingSeq);
        }
        std::lock_guard<std::mutex> lock(w->fileMu);
        if (seq <= w->written) return;
        files->write(kListsFile, text);
        w->written = seq;
      },
      nullptr, http::Priority::Background);
}

std::string Store::listsText() const {
  const Impl& m = *impl_;
  json::Value v = json::Value::object();
  v.set("favorites", stringArray(std::vector<std::string>(m.favorites.begin(), m.favorites.end())));
  if (!m.unfavorited.empty())
    v.set("unfavorited", stringArray(std::vector<std::string>(m.unfavorited.begin(), m.unfavorited.end())));
  v.set("recents", stringArray(m.recents));
  if (!m.searches.empty()) {
    // The names picked from Search and their ids, never what was typed.
    json::Value searches = json::Value::array();
    for (const search::Recent& r : m.searches) {
      json::Value x = json::Value::object();
      x.set("name", r.name);
      x.set("id", r.id);
      searches.push(x);
    }
    v.set("searches", searches);
  }
  v.set("hiddenGroups", stringArray(std::vector<std::string>(m.hiddenGroups.begin(), m.hiddenGroups.end())));
  json::Value pinned = json::Value::object();
  for (const auto& [k, x] : m.pinned) pinned.set(k, x);
  v.set("pinned", pinned);
  if (!m.guidePicks.empty()) {
    json::Value picks = json::Value::object();
    for (const auto& [k, x] : m.guidePicks) picks.set(k, x);
    v.set("guidePicks", picks);
  }
  json::Value last = json::Value::object();
  for (const auto& [k, x] : m.lastFeed) last.set(k, x);
  v.set("lastFeed", last);
  json::Value unplayable = json::Value::object();
  for (const auto& [k, at] : m.unplayable) unplayable.set(k, static_cast<int64_t>(at));
  v.set("unplayable", unplayable);
  if (!m.audioOnly.empty()) {
    json::Value audio = json::Value::object();
    time::Seconds now = time::now();
    for (const auto& [k, at] : m.audioOnly)
      if (now - at < kAudioOnlyFor) audio.set(k, static_cast<int64_t>(at));
    v.set("audioOnly", audio);
  }
  v.set("quickStartRefused",
        stringArray(std::vector<std::string>(m.quickStartRefused.begin(), m.quickStartRefused.end())));
  if (!m.captionChannels.empty())
    v.set("captionChannels",
          stringArray(std::vector<std::string>(m.captionChannels.begin(), m.captionChannels.end())));
  json::Value rel = json::Value::object();
  for (const auto& [k, x] : m.reliability) {
    json::Value pair = json::Value::array();
    pair.push(x.size() > 0 ? x[0] : 0);
    pair.push(x.size() > 1 ? x[1] : 0);
    rel.set(k, pair);
  }
  v.set("reliability", rel);
  json::Value groups = json::Value::object();
  for (const auto& [k, g] : m.groupStarts) {
    json::Value row = json::Value::array();
    row.push(g.worked);
    row.push(g.failed);
    row.push(g.at);
    groups.set(k, row);
  }
  if (!m.groupStarts.empty()) v.set("groupStarts", groups);
  return json::write(v);
}

void Store::readLinksLater() {
  Impl& m = *impl_;
  m.linksAsked = true;
  auto links = m.links;
  const Files* files = &host_.files;
  host_.pool.post(
      [links, files] {
        std::lock_guard<std::mutex> work(links->work);
        readLinks(*links, *files);
      },
      nullptr, http::Priority::Normal);
}

void Store::dropLinks(std::vector<std::string> prefixes) {
  // Rare (a provider removed, an empty playlist): a job of its own.
  auto u = std::make_shared<LinksUpdate>();
  u->prefixes = std::move(prefixes);
  auto links = impl_->links;
  const Files* files = &host_.files;
  http::Pool* pool = &host_.pool;
  std::string path = host_.files.path(kM3uStreamsFile);
  pool->post([u, links, files, pool, path] { updateLinks(links, *u, *files, *pool, path); }, nullptr,
             http::Priority::Background);
}

SubtitleChoice subtitleChoice(const Settings& s, bool spanishChannel) {
  return spanishChannel ? SubtitleChoice{s.subtitlesEs, s.subtitleLanguageEs}
                        : SubtitleChoice{s.subtitles, s.subtitleLanguage};
}

void setSubtitleChoice(Settings& s, bool spanishChannel, const SubtitleChoice& c) {
  (spanishChannel ? s.subtitlesEs : s.subtitles) = c.on;
  (spanishChannel ? s.subtitleLanguageEs : s.subtitleLanguage) = c.language;
}

int clampCaptionTiming(int ms) {
  int steps = (std::abs(ms) + kCaptionTimingStepMs / 2) / kCaptionTimingStepMs;
  return std::clamp((ms < 0 ? -steps : steps) * kCaptionTimingStepMs, -kCaptionTimingMaxMs, kCaptionTimingMaxMs);
}

int captionTiming(const Settings& s, const std::string& entryId) {
  auto it = s.captionTimingByChannel.find(entryId);
  return it != s.captionTimingByChannel.end() ? it->second : s.captionTiming;
}

bool hasOwnCaptionTiming(const Settings& s, const std::string& entryId) {
  return !entryId.empty() && s.captionTimingByChannel.count(entryId) > 0;
}

void setCaptionTiming(Settings& s, const std::string& entryId, int ms) {
  if (entryId.empty()) s.captionTiming = clampCaptionTiming(ms);
  else s.captionTimingByChannel[entryId] = clampCaptionTiming(ms);
}

void clearOwnCaptionTiming(Settings& s, const std::string& entryId) { s.captionTimingByChannel.erase(entryId); }

void Store::setSettings(const Settings& s) {
  bool tsChanged = s.preferTS != settings_.preferTS;
  bool fillChanged = s.fillMissingListings != settings_.fillMissingListings;
  bool tabChanged = s.allChannelsTab != settings_.allChannelsTab;
  bool otherChanged = s.showOther != settings_.showOther;
  // Subtitle choices don't touch Live Events.
  Settings same = s;
  same.subtitles = settings_.subtitles;
  same.subtitleLanguage = settings_.subtitleLanguage;
  same.subtitlesEs = settings_.subtitlesEs;
  same.subtitleLanguageEs = settings_.subtitleLanguageEs;
  same.subtitleSize = settings_.subtitleSize;
  same.subtitleLook = settings_.subtitleLook;
  same.subtitleSentenceCase = settings_.subtitleSentenceCase;
  same.subtitleTop = settings_.subtitleTop;
  same.captionTiming = settings_.captionTiming;
  same.captionTimingByChannel = settings_.captionTimingByChannel;
  same.playThroughApp = settings_.playThroughApp;
  same.upGoesHigher = settings_.upGoesHigher;
  same.systemCaptions = settings_.systemCaptions;
  bool boardChanged = !(same.previewChannels == settings_.previewChannels && same.scheduleEnabled == settings_.scheduleEnabled &&
                        same.showUnwatchable == settings_.showUnwatchable && same.showScores == settings_.showScores &&
                        same.boardUnscheduled == settings_.boardUnscheduled &&
                        same.clock24 == settings_.clock24 && same.allChannelsTab == settings_.allChannelsTab &&
                        same.preferTS == settings_.preferTS && same.embyQuickStart == settings_.embyQuickStart &&
                        same.fillMissingListings == settings_.fillMissingListings &&
                        same.playerTipsShown == settings_.playerTipsShown && same.guideShards == settings_.guideShards);
  bool searchesOff = settings_.recentSearches && !s.recentSearches;
  settings_ = s;
  if (searchesOff && !impl_->searches.empty()) {
    // Turning Recent searches off forgets them too, on file at once.
    impl_->searches.clear();
    saveLists();
  }
  json::Value v = json::Value::object();
  v.set("previewChannels", s.previewChannels);
  v.set("allChannelsTab", s.allChannelsTab);
  v.set("preferTS", s.preferTS);
  v.set("embyQuickStart", s.embyQuickStart);
  v.set("fillMissingListings", s.fillMissingListings);
  v.set("showOther", s.showOther);
  v.set("scheduleEnabled", s.scheduleEnabled);
  v.set("showUnwatchable", s.showUnwatchable);
  v.set("boardMoreEvents", s.boardMoreEvents);
  v.set("boardUntimed", s.boardUntimed);
  v.set("boardEventChannels", s.boardEventChannels);
  v.set("boardUnscheduled", s.boardUnscheduled);
  v.set("showScores", s.showScores);
  v.set("clock24", s.clock24);
  v.set("recentSearches", s.recentSearches);
  v.set("playerTipsShown", s.playerTipsShown);
  v.set("subtitles", s.subtitles);
  if (!s.subtitleLanguage.empty()) v.set("subtitleLanguage", s.subtitleLanguage);
  v.set("subtitlesEs", s.subtitlesEs);
  if (!s.subtitleLanguageEs.empty()) v.set("subtitleLanguageEs", s.subtitleLanguageEs);
  v.set("subtitleSize", s.subtitleSize);
  v.set("subtitleLook", s.subtitleLook);
  v.set("subtitleSentenceCase", s.subtitleSentenceCase);
  v.set("subtitleTop", s.subtitleTop);
  if (s.captionTiming) v.set("captionTiming", s.captionTiming);
  if (!s.captionTimingByChannel.empty()) {
    json::Value own = json::Value::object();
    for (const auto& [id, ms] : s.captionTimingByChannel) own.set(id, ms);
    v.set("captionTimingByChannel", own);
  }
  v.set("playThroughApp", s.playThroughApp);
  v.set("upGoesHigher", s.upGoesHigher);
  if (s.systemCaptions >= 0) v.set("systemCaptions", s.systemCaptions);
  if (!s.guideShards.empty())
    v.set("guideShards", stringArray(std::vector<std::string>(s.guideShards.begin(), s.guideShards.end())));
  if (boardChanged) impl_->ev.boardDirty = true;
  host_.files.write(kSettingsFile, json::write(v, true));
  if (tsChanged) {
    for (sources::Account& a : impl_->accounts) a.preferTS = s.preferTS;
  }
  if (otherChanged && impl_->catalog) rebuildEntries();
  if (fillChanged) {
    if (s.fillMissingListings) scheduleOutside(true);
    else {
      impl_->outsideMatches.clear();
      rebuildOutsidePrograms();
    }
  }
  if (tabChanged) impl_->channelsDirty = true;
}

void Store::setPrefs(const catalog::LineupPrefs& p) {
  bool countriesChanged = p.countries != impl_->prefs.countries;
  auto freeOn = [](const catalog::LineupPrefs& prefs) {
    return prefs.categories.count(catalog::LineupCategory::free) > 0;
  };
  bool freeChanged = freeOn(p) != freeOn(impl_->prefs);
  impl_->prefs = p;
  host_.files.write(kLineupFile, catalog::toJson(p));
  if (countriesChanged && !impl_->ev.mySports.configured) {
    // Build 127: until My Sports is set up, Live Events follows the
    // defaults for the lineup's countries (Español adds Liga MX...).
    impl_->ev.schedule.leaguesChanged(scheduleContext());
    impl_->ev.lastFingerprint.clear();
    impl_->ev.boardDirty = true;
  }
  if ((countriesChanged || freeChanged) && impl_->admissionCut) {
    // Which groups are kept depends on the countries and on whether free
    // channels are on: read the channel lists again, and sort them even if
    // they haven't changed.
    impl_->admissionChanged = true;
    refreshAll();
    return;
  }
  if (countriesChanged) scheduleProvider(true);
  rebuildEntries();
}

// ------------------------------------------------------------ accounts

std::vector<AccountSummary> Store::accounts() const {
  std::vector<AccountSummary> out;
  for (const sources::Account& a : impl_->accounts) {
    AccountSummary s;
    s.id = a.id;
    s.name = redact(a.displayName);
    s.kind = a.kind == sources::AccountKind::Xtream ? "Xtream"
             : a.kind == sources::AccountKind::Emby ? "Emby / Jellyfin"
                                                   : "M3U playlist";
    auto it = impl_->runs.find(a.sourceId());
    s.state = it == impl_->runs.end() ? "" : it->second.state;
    s.signInFailed = s.state == "Couldn't sign in";
    s.membership.ends = a.expires;
    s.membership.trial = a.trial;
    s.membership.streams = a.maxConnections;
    s.membership.endedStatus = it != impl_->runs.end() && it->second.endedStatus;
    out.push_back(std::move(s));
  }
  return out;
}

void Store::addAccount(const AccountForm& form, std::function<void(std::string)> done) {
  sources::Account a;
  a.id = randomId();
  std::string server(str::trim(form.server));
  a.displayName = std::string(str::trim(form.name));
  if (a.displayName.empty()) a.displayName = hostOf(server).empty() ? "My provider" : hostOf(server);
  a.preferTS = settings_.preferTS;
  http::Pool& pool = host_.pool;
  uint64_t gen = impl_->generation;
  auto finish = [this, gen, done](sources::Account account, std::string problem) {
    if (!problem.empty()) {
      done(redact(problem));
      return;
    }
    (void)gen;
    // A provider added to a list on screen: the list stays while the new
    // channels are merged in ("Adding channels from <name>…").
    impl_->accountChange = "Adding channels from " + redact(account.displayName) + "\xe2\x80\xa6";
    impl_->accountChangeDone = "Channels added";
    impl_->accounts.push_back(std::move(account));
    impl_->runs[impl_->accounts.back().sourceId()];
    saveAccounts();
    done("");
    refreshAll();
  };

  switch (form.kind) {
    case AccountForm::Kind::Xtream: {
      a.kind = sources::AccountKind::Xtream;
      a.serverUrl = sources::stripServerSecrets(server);
      a.userName = std::string(str::trim(form.user));
      a.password = form.password;
      auto client = sources::xtream::Client::make(a);
      if (!client) {
        done(client.error().message);
        return;
      }
      auto shared = std::make_shared<sources::xtream::Client>(client.take());
      pool.submit(toHttp(shared->accountInfoRequest(), "xtream.signin", http::Priority::Visible),
                        [shared, a, finish](http::Response&& r) mutable {
        if (!r.status) {
          finish(a, "Couldn't reach the server (" + r.error + ")");
          return;
        }
        auto info = shared->parseAccountInfo(r.status, r.body);
        if (!info) {
          finish(a, info.error().message);
          return;
        }
        sources::xtream::applyAccountInfo(a, info.value());
        finish(a, "");
      });
      return;
    }
    case AccountForm::Kind::Emby: {
      a.kind = sources::AccountKind::Emby;
      a.serverUrl = sources::stripServerSecrets(server);
      a.userName = std::string(str::trim(form.user));
      sources::Request req = sources::emby::authenticateRequest(a.serverUrl, a.userName, form.password, a.allowInsecureTLS, a.id);
      pool.submit(toHttp(req, "emby.signin", http::Priority::Visible), [a, finish](http::Response&& r) mutable {
        if (!r.status) {
          finish(a, "Couldn't reach the server (" + r.error + ")");
          return;
        }
        auto auth = sources::emby::parseAuthenticate(r.status, r.body);
        if (!auth) {
          finish(a, auth.error().message);
          return;
        }
        a.token = auth.value().first;
        a.userId = auth.value().second;
        finish(a, "");
      });
      return;
    }
    case AccountForm::Kind::M3U: {
      a.kind = sources::AccountKind::M3U;
      a.playlistUrl = server;
      a.guideUrl = std::string(str::trim(form.guideUrl));
      // Read just the start of the playlist to check it is one.
      auto parser = std::make_shared<sources::m3u::Parser>(a.id, [](Channel&&, sources::m3u::StreamInfo&&) { return true; });
      auto bytes = std::make_shared<size_t>(0);
      http::Request q;
      q.url = server;
      q.tag = "m3u.check";
      q.gunzip = true;
      q.priority = http::Priority::Visible;
      q.timeoutMs = 30000;
      q.onChunk = [parser, bytes](std::string_view c) {
        *bytes += c.size();
        return parser->feed(c) && *bytes < (256u << 10);
      };
      pool.submit(std::move(q), [a, parser, finish](http::Response&& r) mutable {
        if (!r.status) {
          finish(a, "Couldn't download the playlist (" + r.error + ")");
          return;
        }
        if (r.status < 200 || r.status >= 300) {
          finish(a, "The playlist link answered with error " + std::to_string(r.status));
          return;
        }
        if (!parser->header().sawExtM3u && parser->channels() == 0) {
          finish(a, "That link isn't an M3U playlist");
          return;
        }
        if (a.guideUrl.empty() && parser->header().guideUrl) {
          a.guideUrl = *parser->header().guideUrl;
          a.guideFromPlaylist = true;
        }
        finish(a, "");
      });
      return;
    }
  }
}

void Store::removeAccount(const std::string& id) {
  Impl& m = *impl_;
  auto it = std::find_if(m.accounts.begin(), m.accounts.end(), [&](const sources::Account& a) { return a.id == id; });
  if (it == m.accounts.end()) return;
  std::string source = it->sourceId();
  m.accounts.erase(it);
  m.runs.erase(source);
  m.accountChange = "Updating your channel list\xe2\x80\xa6";
  m.accountChangeDone = "Channel list updated";
  saveAccounts();
  refreshAll();
  // After refreshAll: it drops the jobs queued before it.
  dropLinks({source + "|"});
  pruneDisk();  // its saved guide and channel list
}

void Store::membershipRead(const std::string& accountId, const sources::xtream::AccountInfo& info) {
  Impl& m = *impl_;
  auto it = std::find_if(m.accounts.begin(), m.accounts.end(),
                         [&](const sources::Account& a) { return a.id == accountId; });
  if (it == m.accounts.end()) return;
  sources::xtream::applyAccountInfo(*it, info);
  m.runs[it->sourceId()].endedStatus = sources::xtream::endedStatus(info);
  saveAccounts();  // only a change is written
}

void Store::guideOpened() {
  Impl& m = *impl_;
  if (!m.headsUpLine.empty()) return;  // one waiting or showing
  time::Seconds now = time::now();
  const time::Zone& zone = host_.zone();
  for (const AccountSummary& a : accounts()) {
    auto acc = std::find_if(m.accounts.begin(), m.accounts.end(),
                            [&](const sources::Account& x) { return x.id == a.id; });
    if (acc == m.accounts.end() || !membership::headsUpDue(acc->noticeDay, now, zone)) continue;
    std::string line = membership::headsUp(a.name, a.membership, now, zone);
    if (line.empty()) continue;
    m.headsUpLine = std::move(line);
    m.headsUpAccount = a.id;
    m.headsUpUntil = 0;
    m.headsUpAskedAt = now;
    return;
  }
}

// ------------------------------------------------------------ loading

std::string Store::currentSignature() const {
  std::string s = "default";
  for (const sources::Account& a : impl_->accounts) s += "|" + a.sourceId();
  return s;
}

disk::Owners Store::diskOwners() const {
  const Impl& m = *impl_;
  disk::Owners o;
  o.known = !m.accountsUnreadable;
  std::string signature = currentSignature();
  o.keep.insert(catalog::cache::fileName(signature, catalog::cache::Part::first));
  o.keep.insert(catalog::cache::fileName(signature, catalog::cache::Part::full));
  o.keep.insert(guide::savedFileName(signature));
  for (const sources::Account& a : m.accounts)
    if (hasGuideFile(a)) o.keep.insert(guide::provider::savedFileName(guide::provider::cacheKey("default", a.sourceId())));
  // The outside guide files of the areas chosen (none while missing
  // listings aren't filled in).
  if (settings_.fillMissingListings) {
    std::set<std::string> shards = guideShards();
    for (const guide::Source& source : guide::sources()) {
      if (!source.appliesTo(shards)) continue;
      std::string name = std::string(disk::kGuidesDir) + "/" + source.id();
      o.keep.insert(name);
      if (source.backup) o.spare.insert(name);
    }
  }
  return o;
}

void Store::pruneDisk() {
  std::shared_ptr<disk::Keeper> keeper = impl_->disk;
  if (!keeper) return;
  host_.pool.post([keeper, owners = diskOwners()] { keeper->prune(owners); }, nullptr, http::Priority::Background);
}

void Store::refreshAll() {
  Impl& m = *impl_;
  ++m.generation;
  host_.pool.cancelAll();
  if (host_.requestsCancelled) host_.requestsCancelled();
  // A patch on its way was dropped with the rest: published again.
  if (m.patching) {
    m.patching.reset();
    m.channelsDirty = true;
  }
  m.loading = false;
  m.activeGuideFetches = 0;
  m.savedListingsReading = false;  // dropped with the rest
  // The guide builds were cancelled with the rest: each that was running
  // starts again from the top once the list is in (tick), whether the list
  // changed, only renamed event slots or came back the same.
  if (m.providerRunning) m.providerForce = true;
  if (m.outsideRunning) m.outsideForce = true;
  m.providerRunning = m.outsideRunning = false;
  m.guidesAfterLoad = true;
  // Search's builds were dropped with the rest.
  m.searchGuide.busy = m.searchGuide.quickBusy = m.searchBoard.busy = false;
  // So were Live Events' (one may still run: the next board has an index of
  // its own).
  m.ev.rebuilding = m.ev.restoring = m.ev.boardBusy = false;
  m.ev.boardCameras = std::make_shared<sports::cameras::CameraIndex>();
  m.ev.boardTitles = sports::board::newTitleMemo();
  m.pendingGuide.clear();
  m.pendingGuideSet.clear();
  for (auto& [id, st] : m.guideState) {
    if (st == Impl::GuideState::Loading) st = Impl::GuideState::None;
  }
  if (m.accounts.empty()) {
    reset();
    m.signature.clear();
    return;
  }
  load(true);
}

void Store::reset() {
  Impl& m = *impl_;
  m.channels = std::make_shared<std::vector<Channel>>();
  m.indexByID.clear();
  m.catalog = std::make_shared<CatalogResult>();
  m.listFetchedAt = 0;
  m.entryInfo.clear();
  m.familyAt.clear();
  m.lineup = catalog::LineupOutput{};
  ++m.lineupSerial;
  releaseLater(host_.pool, std::move(m.rows));
  m.patching.reset();
  m.programs.clear();
  m.serverPrograms.clear();
  m.providerMatches.clear();
  m.outsideMatches.clear();
  m.guideState.clear();
  m.rowGuides.clear();
  m.choiceRows.clear();
  m.choiceAsked.clear();
  m.channelsDirty = m.allListingsDirty = m.listingsDirty = true;
  m.readyOnPublish = m.saveOnRows = false;
  status_ = StoreStatus{};
  status_.phase = m.accounts.empty() ? StoreStatus::Phase::NoAccounts : StoreStatus::Phase::Loading;
}

void Store::load(bool force) {
  Impl& m = *impl_;
  std::string signature = currentSignature();
  if (signature != m.signature) {
    // Providers added or removed: a list on screen stays until the new one
    // is sorted (it replaces it in place); with none, from scratch.
    if (m.channels->empty() || m.partial || m.signature.empty()) {
      reset();
      m.accountChange.clear();
    } else {
      m.accountsChanged = true;
    }
    m.signature = signature;
    m.savedListingsRead = m.savedListingsReading = false;
    m.providerSaved.clear();
    m.outsideSaved.reset();
  }
  if (m.loading) return;
  m.loading = true;
  if (m.channels->empty()) {
    status_.phase = StoreStatus::Phase::Loading;
    status_.line = "Loading channels\xe2\x80\xa6";
  }
  uint64_t gen = m.generation;
  if (m.channels->empty() && !force) {
    // 1. The list saved on this TV: on screen right away.
    std::string path = host_.files.path(catalog::cache::fileName(signature, catalog::cache::Part::first));
    std::string fullPath = host_.files.path(catalog::cache::fileName(signature, catalog::cache::Part::full));
    auto saved = std::make_shared<std::optional<catalog::cache::Saved>>();
    auto foreground = m.foregroundJobs;
    // A saved list to open (the launch says "opening", not "getting").
    m.openingSaved = std::ifstream(path).good();
    host_.pool.post(
        [path, saved, signature, foreground] {
          Foreground busy(foreground);
          std::ifstream in(path, std::ios::binary);
          if (in) *saved = catalog::cache::readSaved(in, signature);
        },
        [this, gen, saved, fullPath, signature] {
          if (gen != impl_->generation) return;
          if (!*saved) impl_->openingSaved = false;  // unreadable: a download after all
          if (*saved) {
            MLOG_I("store", "first screen from the saved list: %zu channels", (*saved)->channels.size());
            installSaved(std::move(**saved), true);
            // Then the full saved list.
            auto full = std::make_shared<std::optional<catalog::cache::Saved>>();
            host_.pool.post(
                [fullPath, full, signature, foreground = impl_->foregroundJobs] {
                  Foreground busy(foreground);
                  std::ifstream in(fullPath, std::ios::binary);
                  if (in) *full = catalog::cache::readSaved(in, signature);
                },
                [this, gen, full] {
                  if (gen != impl_->generation || !*full || !impl_->partial) return;
                  installSaved(std::move(**full), false);
                },
                http::Priority::Normal);
          }
          downloadChannels(gen);
        },
        http::Priority::Visible);
    return;
  }
  downloadChannels(gen);
}

void Store::installSaved(catalog::cache::Saved saved, bool partial) {
  Impl& m = *impl_;
  for (const auto& [source, formats] : saved.xtreamFormats) m.runs[source].formats = formats;
  m.channelsLoadedAt = saved.savedAt;
  // An older version's list is shown until this one sorts it again (no
  // fingerprint, so the download is sorted whatever it holds).
  bool older = saved.version != catalog::cache::kVersion;
  if (older && partial) MLOG_I("store", "showing the list version %d saved; sorting it again", saved.version);
  if (!partial) m.listFingerprint = older ? 0 : saved.fingerprint;
  installCatalog(std::make_shared<std::vector<Channel>>(std::move(saved.channels)),
                 std::make_shared<CatalogResult>(std::move(saved.catalog)), partial);
  readyWhenShown();
  startGuides(true);
}



void Store::downloadChannels(uint64_t gen) {
  Impl& m = *impl_;
  size_t n = m.accounts.size();
  auto parts = std::make_shared<std::vector<Fetched>>(n);
  auto remaining = std::make_shared<size_t>(n);
  status_.line = "Loading channels from " + std::to_string(n) + (n == 1 ? " provider\xe2\x80\xa6" : " providers\xe2\x80\xa6");
  status_.received = 0;
  auto finished = [this, gen, parts, remaining](size_t /*index*/) {
    if (gen != impl_->generation) return;
    if (--*remaining > 0) return;
    channelsDownloaded(gen, parts);
  };
  for (size_t i = 0; i < n; ++i) {
    const sources::Account& a = m.accounts[i];
    Fetched* slot = &(*parts)[i];
    std::string name = redact(a.displayName);
    switch (a.kind) {
      case sources::AccountKind::Xtream: {
        auto client = sources::xtream::Client::make(a);
        if (!client) {
          slot->problem = name + ": " + client.error().message;
          finished(i);
          break;
        }
        auto c = std::make_shared<sources::xtream::Client>(client.take());
        // Account info (formats, end date, streams at once) alongside the
        // list: at every launch and every re-read, so a renewal shows by
        // itself. (`parts` keeps the slot alive should it answer last.)
        std::string accountId = a.id;
        host_.pool.submit(toHttp(c->accountInfoRequest(), "xtream.info"),
                          [c, parts, slot, this, gen, accountId](http::Response&& r) {
          if (gen != impl_->generation) return;
          auto info = c->parseAccountInfo(r.status, r.body);
          if (!info) return;
          slot->formats = info.value().formats;
          membershipRead(accountId, info.value());
        });
        host_.pool.submit(toHttp(c->categoriesRequest(), "xtream.categories"),
                           [this, gen, c, slot, name, finished, i](http::Response&& r) {
          if (gen != impl_->generation) return;
          auto categories = sources::xtream::parseCategories(r.status, r.body);
          auto reader = std::make_shared<sources::xtream::StreamsReader>(
              200, c->sourceId(), std::move(categories), [slot](Channel&& ch) {
                slot->channels.push_back(std::move(ch));
                return true;
              });
          http::Request q = toHttp(c->streamsRequest(), "xtream.streams");
          q.onChunk = [reader](std::string_view chunk) { return reader->feed(chunk); };
          q.afterTransfer = [reader, slot, name](http::Response& resp) {
            if (!resp.ok) {
              auto err = sources::xtream::checkStatus(resp.status);
              slot->problem = name + ": " + (err ? err->message : "couldn't load channels (" + resp.error + ")");
              slot->channels.clear();
              return;
            }
            if (auto err = reader->finish()) slot->problem = name + ": " + err->message;
          };
          host_.pool.submit(std::move(q), [finished, i](http::Response&&) { finished(i); });
        });
        break;
      }
      case sources::AccountKind::Emby: {
        auto client = sources::emby::Client::make(a);
        if (!client) {
          slot->problem = name + ": " + client.error().message;
          finished(i);
          break;
        }
        auto c = std::make_shared<sources::emby::Client>(client.take());
        auto reader = std::make_shared<sources::emby::ChannelsReader>(*c, 200, [slot](Channel&& ch) {
          slot->channels.push_back(std::move(ch));
          return true;
        });
        http::Request q = toHttp(c->channelsRequest(), "emby.channels");
        q.onChunk = [reader](std::string_view chunk) { return reader->feed(chunk); };
        q.afterTransfer = [reader, slot, name](http::Response& resp) {
          if (!resp.ok) {
            auto err = sources::emby::checkStatus(resp.status);
            slot->problem = name + ": " + (err ? err->message : "couldn't load channels (" + resp.error + ")");
            slot->channels.clear();
            return;
          }
          if (auto err = reader->finish()) slot->problem = name + ": " + err->message;
        };
        host_.pool.submit(std::move(q), [finished, i](http::Response&&) { finished(i); });
        break;
      }
      case sources::AccountKind::M3U: {
        // A playlist from an Xtream panel: the panel's account info (its
        // end date) for the same login, one small request per read.
        if (std::optional<sources::Account> panel = sources::xtream::panelAccount(a.playlistUrl)) {
          panel->id = a.id;
          panel->allowInsecureTLS = a.allowInsecureTLS;
          if (auto client = sources::xtream::Client::make(*panel)) {
            auto c = std::make_shared<sources::xtream::Client>(client.take());
            std::string accountId = a.id;
            host_.pool.submit(toHttp(c->accountInfoRequest(), "m3u.membership"),
                              [c, this, gen, accountId](http::Response&& r) {
              if (gen != impl_->generation) return;
              auto info = c->parseAccountInfo(r.status, r.body);
              if (info) membershipRead(accountId, info.value());
            });
          }
        }
        auto parser = std::make_shared<sources::m3u::Parser>(a.id, [slot](Channel&& ch, sources::m3u::StreamInfo&& info) {
          slot->streams[ch.id] = std::move(info);
          slot->channels.push_back(std::move(ch));
          return true;
        });
        http::Request q;
        q.url = a.playlistUrl;
        q.tag = "m3u.playlist";
        q.gunzip = true;
        q.timeoutMs = 120000;
        q.onChunk = [parser](std::string_view chunk) { return parser->feed(chunk); };
        q.afterTransfer = [parser, slot, name](http::Response& resp) {
          if (!resp.ok) {
            slot->problem = name + ": couldn't download the playlist (" + resp.error + ")";
            slot->channels.clear();
            slot->streams.clear();
            return;
          }
          if (auto err = parser->finish()) slot->problem = name + ": " + err->message;
          slot->guideUrl = parser->header().guideUrl;
        };
        host_.pool.submit(std::move(q), [finished, i](http::Response&&) { finished(i); });
        break;
      }
    }
  }
  if (n == 0) impl_->loading = false;
}

void Store::channelsDownloaded(uint64_t gen, std::shared_ptr<std::vector<Fetched>> parts) {
  Impl& m = *impl_;
  std::vector<std::string> problems;
  size_t total = 0;
  auto links = std::make_shared<LinksUpdate>();
  for (size_t i = 0; i < parts->size() && i < m.accounts.size(); ++i) {
    Fetched& f = (*parts)[i];
    sources::Account& a = m.accounts[i];
    Impl::AccountRun& run = m.runs[a.sourceId()];
    if (!f.formats.empty()) run.formats = f.formats;
    if (!f.problem.empty()) {
      problems.push_back(f.problem);
      run.state = "Couldn't sign in";
    } else {
      // Never a channel count on screen.
      run.state = "Signed in";
      a.lastChannelCount = static_cast<int>(f.channels.size());
    }
    total += f.channels.size();
    if (a.kind == sources::AccountKind::M3U && f.problem.empty()) {
      // Its links take the place of its old ones (on the sort's worker).
      links->prefixes.push_back(a.sourceId() + "|");
      links->fresh.push_back(std::move(f.streams));
      f.streams.clear();
      // The playlist's own guide link, unless one was typed in.
      if (f.guideUrl && (a.guideUrl.empty() || a.guideFromPlaylist) && a.guideUrl != *f.guideUrl) {
        a.guideUrl = *f.guideUrl;
        a.guideFromPlaylist = true;
        scheduleProvider(true);
      }
    }
  }
  for (const std::string& p : problems) MLOG_W("store", "%s", p.c_str());
  saveAccounts();

  if (total == 0) {
    // A playlist that answered with no channels: its links go.
    if (!links->prefixes.empty()) dropLinks(std::move(links->prefixes));
    m.loading = false;
    m.channelsLoadedAt = time::now();
    if (!m.channels->empty() && !problems.empty()) m.loadFailed = true;  // the list on screen stays
    m.accountChange.clear();
    m.accountsChanged = false;
    // Nothing to show: no list, or only an empty first-screen file.
    if (m.channels->empty() || (m.readyOnPublish && m.catalog->entries.empty())) {
      m.readyOnPublish = false;
      status_.phase = problems.empty() ? StoreStatus::Phase::Ready : StoreStatus::Phase::Failed;
      status_.problem = str::join(problems, "\n");
      status_.line.clear();
    }
    return;
  }
  // Merge, admit whole groups up to the cap, then build the catalog on a
  // worker (or refresh only Live Events when just event slots were renamed).
  // The providers' lists are put together there too, but for a first list:
  // the area suggestion reads its names while it sorts.
  auto merged = std::make_shared<std::vector<Channel>>();
  auto mergeParts = [](std::vector<Fetched>& from, std::vector<Channel>& into, size_t count) {
    into.reserve(count);
    for (Fetched& f : from) {
      for (Channel& c : f.channels) into.push_back(std::move(c));
      f.channels.clear();
      f.channels.shrink_to_fit();
    }
  };
  if (m.channels->empty()) mergeParts(*parts, *merged, total);
  // Local favourites (Xtream and M3U have no favorites API).
  bool showing = !m.channels->empty() && !m.partial;
  auto old = showing ? m.channels : nullptr;
  auto oldCatalog = showing ? m.catalog : nullptr;
  auto result = std::make_shared<std::pair<std::shared_ptr<std::vector<Channel>>, std::shared_ptr<CatalogResult>>>();
  auto renamedCount = std::make_shared<long>(-1);
  status_.line = "Sorting your channels\xe2\x80\xa6";
  status_.received = total;
  // The names the area suggestion reads while the list sorts (first run:
  // setup carries on meanwhile).
  if (m.channels->empty()) m.receivedNames = std::make_shared<const std::vector<std::string>>(suggestionNames(*merged));
  auto progress = std::make_shared<catalog::BuildProgress>();
  m.sortProgress = progress;
  time::Seconds now = time::now();
  auto stations = m.stations;
  std::set<std::string> wanted = m.prefs.guideCountries();  // "US" for US English and US Spanish
  bool freeOn = m.prefs.categories.count(catalog::LineupCategory::free) > 0;
  auto cut = std::make_shared<bool>(false);
  uint64_t lastPrint = showing && !m.admissionChanged ? m.listFingerprint : 0;
  auto print = std::make_shared<uint64_t>(0);
  auto unchanged = std::make_shared<bool>(false);
  bool mergeHere = merged->empty();
  const Files* files = &host_.files;
  http::Pool* pool = &host_.pool;
  std::string linksPath = host_.files.path(kM3uStreamsFile);
  host_.pool.post(
      [merged, old, oldCatalog, result, renamedCount, now, stations, wanted, freeOn, cut, lastPrint, print, unchanged,
       progress,
       foreground = m.foregroundJobs, parts, total, mergeHere, mergeParts, links, allLinks = m.links, files, pool,
       linksPath] {
        Foreground busy(foreground);
        auto started = std::chrono::steady_clock::now();
        // The links first (tens of thousands for a big playlist): before
        // any row of this list can be tuned to.
        if (!links->prefixes.empty()) updateLinks(allLinks, *links, *files, *pool, linksPath);
        if (mergeHere) mergeParts(*parts, *merged, total);
        std::vector<Channel> list = std::move(*merged);
        // The same download (and countries) as the list on screen, from
        // this run or saved by the last one: nothing to sort again.
        // (The built-in channel directory too: a new one sorts again.)
        std::set<std::string> also = wanted;
        also.insert(freeOn ? "free:on" : "free:off");
        also.insert("directory:" + catalog::directory::builtIn().made);
        also.insert("reference:" + catalog::reference::builtIn().made);
        *print = catalog::fingerprint(list, also);
        if (lastPrint && *print == lastPrint) {
          *unchanged = true;
          return;
        }
        // With countries chosen, another country's groups are kept only for
        // sports and events (Live Events finds games on them); the rest is
        // left out. Over the cap: the viewer's countries first, then
        // unmarked groups ("United States", "4K|", event groups), then those
        // sports groups (the emptiest last), then free streaming and 24/7
        // groups unless free channels are on. Provider order alone kept
        // Europe and cut every US group.
        if (!wanted.empty() || list.size() > catalog::kMaxChannels) {
          catalog::Admission adm =
              catalog::admitGroups(list, catalog::kMaxChannels, catalog::countryRank(wanted, freeOn));
          MLOG_I("store", "admitted %zu channels in %zu groups; %zu groups cut for room, %zu other countries' left out",
                 adm.channels.size(), adm.admittedGroups, adm.cutGroups, adm.leftOut);
          *cut = adm.cutGroups > 0 || adm.leftOut > 0;
          list = std::move(adm.channels);
        }
        auto annotated = std::make_shared<std::vector<Channel>>(catalog::annotated(list));
        if (old && oldCatalog) {
          if (auto renamed = catalog::renamedEventSlots(*old, *annotated, oldCatalog->traits)) {
            auto r = std::make_shared<CatalogResult>(*oldCatalog);
            if (!renamed->empty()) catalog::refreshEvents(*r, *annotated, *renamed, now);
            *renamedCount = static_cast<long>(renamed->size());
            *result = {annotated, r};
            return;
          }
        }
        *result = {annotated, std::make_shared<CatalogResult>(
                                  catalog::build(*annotated, now, *stations, builtInDirectory(), progress.get(),
                                                 builtInReference()))};
        // How long a full sort takes on this device (the log, for tuning).
        long ms = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now() - started).count());
        MLOG_I("store", "sorted %zu channels in %ld ms", annotated->size(), ms);
      },
      [this, gen, result, renamedCount, showing, problems, cut, print, unchanged] {
        if (gen != impl_->generation) return;
        Impl& mm = *impl_;
        mm.loading = false;
        mm.sortProgress.reset();
        mm.receivedNames.reset();
        status_.received = 0;
        mm.channelsLoadedAt = mm.listFetchedAt = time::now();
        mm.listFingerprint = *print;
        if (*unchanged) {
          mm.accountChange.clear();
          MLOG_I("store", "refresh: the channel list is unchanged");
          status_.problem = str::join(problems, "\n");
          status_.line.clear();
          afterBigJob("channel list checked");
          return;
        }
        mm.admissionChanged = false;
        mm.admissionCut = *cut;
        bool wasEmpty = mm.channels->empty();
        // Only event slots were renamed: the rows, their listings and the
        // lineup can't have changed (event feeds are never rows), so only
        // the names and Live Events are new. No lineup rebuild, no listings
        // re-merge, and the saved list waits (kRenamedSaveEvery).
        if (*renamedCount >= 0 && !wasEmpty && showing && !mm.accountsChanged && !mm.partial) {
          installRenamed(result->first, result->second);
          status_.problem = str::join(problems, "\n");
          status_.line.clear();
          MLOG_I("store", "refresh: only %ld event names changed", *renamedCount);
          if (time::now() - mm.catalogSavedAt >= kRenamedSaveEvery) saveCatalog();
          else mm.catalogUnsaved = true;
          afterBigJob("event names refreshed");
          return;
        }
        // A list was on screen and is now sorted again: the header says so.
        if (!wasEmpty && *renamedCount < 0) mm.loadUpdated = true;
        installCatalog(result->first, result->second, false);
        readyWhenShown();
        status_.problem = str::join(problems, "\n");
        status_.line.clear();
        if (*renamedCount >= 0) MLOG_I("store", "refresh: only %ld event names changed", *renamedCount);
        else MLOG_I("store", "built the channel list: %zu channels, %zu entries", mm.channels->size(), mm.catalog->entries.size());
        if (showing && !mm.accountsChanged) {
          scheduleProvider(false);
          rebuildOutsidePrograms();
        } else {
          startGuides(wasEmpty);
        }
        mm.accountsChanged = false;
        saveCatalog();
        afterBigJob("channels sorted");
      },
      showing ? http::Priority::Background : http::Priority::Visible);
}

bool Store::openingSaved() const { return impl_->openingSaved; }

LoadProgress Store::loadProgress() const {
  const Impl& m = *impl_;
  LoadProgress p;
  p.channels = status_.received;
  if (!m.loading) {
    p.stage = LoadProgress::Stage::Done;
    p.within = 1;
    return p;
  }
  if (!m.sortProgress) return p;  // downloading
  int pass = m.sortProgress->pass.load(std::memory_order_relaxed);
  size_t done = m.sortProgress->done.load(std::memory_order_relaxed);
  size_t total = m.sortProgress->total.load(std::memory_order_relaxed);
  float through = total ? std::min(1.0f, static_cast<float>(done) / static_cast<float>(total)) : 0.0f;
  // Recognizing is the channels' traits, then their rows; then games.
  switch (pass) {
    case catalog::BuildProgress::traits:
      p.stage = LoadProgress::Stage::Recognize;
      p.within = 0.05f + 0.6f * through;
      break;
    case catalog::BuildProgress::entries:
      p.stage = LoadProgress::Stage::Recognize;
      p.within = 0.65f + 0.35f * through;
      break;
    case catalog::BuildProgress::events:
      p.stage = LoadProgress::Stage::Games;
      p.within = 0.2f;
      break;
    case catalog::BuildProgress::finished:
      p.stage = LoadProgress::Stage::Games;
      p.within = 0.9f;
      break;
    default:
      p.stage = LoadProgress::Stage::Recognize;
      p.within = 0.02f;
      break;
  }
  return p;
}

// ------------------------------------------------------------ station directory

void Store::readStations() {
  // Built into the app (the build reads Wikipedia's affiliate lists; the TV
  // never does). A directory an older version saved is used only when this
  // build has none.
  const catalog::wiki::Data& built = catalog::wiki::builtIn();
  if (!built.stations.empty()) {
    impl_->stations = std::make_shared<std::vector<guide::Station>>(built.stations);
    MLOG_I("store", "station directory: %zu stations (built in, %s)", built.stations.size(), built.made.c_str());
    return;
  }
  Files& st = host_.files;
  auto text = st.read(kStationsFile);
  if (!text) return;
  auto list = guide::stations::fromJson(*text);
  if (!list) return;
  impl_->stations = std::make_shared<std::vector<guide::Station>>(std::move(*list));
  MLOG_I("store", "station directory: %zu stations (saved)", impl_->stations->size());
}

// ------------------------------------------------------------ fallback logos

// The fallback logos the channel directory carries (CI makes them); an
// older build's downloaded copy is removed.
void Store::useBuiltInLogos() {
  std::remove(host_.files.path(kOldLogoIndexFile).c_str());
  const catalog::directory::Data& data = catalog::directory::builtIn();
  if (data.logos.empty() && data.logosByName.empty() && data.logosAbroad.empty()) return;
  logos::IptvOrgIndex::Saved saved;
  saved.byID.insert(data.logos.begin(), data.logos.end());
  saved.byName.insert(data.logosByName.begin(), data.logosByName.end());
  // Other countries' by guide id: read in place from the built-in
  // directory, which lives as long as the app.
  saved.abroadPrefixes = data.logoPrefixes;
  saved.abroadWords = data.logosAbroad;
  impl_->iptvOrg = std::make_shared<logos::IptvOrgIndex>(std::move(saved));
  MLOG_I("store", "fallback logos: %zu by id, %zu by name, %zu abroad (%zu KB) (built in)", data.logos.size(),
         data.logosByName.size(), impl_->iptvOrg->abroadCount(), data.logosAbroad.size() / 1024);
}

// Classifies the current list again (the station directory changed).
void Store::rebuildCatalog() {
  Impl& m = *impl_;
  if (m.partial || m.channels->empty() || m.loading) return;
  auto list = m.channels;
  auto stations = m.stations;
  auto result = std::make_shared<std::shared_ptr<CatalogResult>>();
  uint64_t gen = m.generation;
  time::Seconds now = time::now();
  host_.pool.post([list, stations, result, now] {
    *result = std::make_shared<CatalogResult>(catalog::build(*list, now, *stations, builtInDirectory(), nullptr,
                                                                   builtInReference()));
  },
                   [this, gen, list, result] {
                     if (gen != impl_->generation || list != impl_->channels || impl_->loading) return;
                     installCatalog(list, *result, false);
                     rebuildOutsidePrograms();
                     saveCatalog();
                   },
                   http::Priority::Background);
}


void Store::installCatalog(std::shared_ptr<const std::vector<Channel>> list, std::shared_ptr<const CatalogResult> cat,
                           bool partial) {
  Impl& m = *impl_;
  m.channels = std::move(list);
  m.catalog = std::move(cat);
  m.partial = partial;
  m.indexByID.clear();
  m.indexByID.reserve(m.channels->size());
  for (size_t i = 0; i < m.channels->size(); ++i) m.indexByID.emplace((*m.channels)[i].id, i);
  m.entryInfo.clear();
  for (size_t i = 0; i < m.catalog->entries.size(); ++i) m.entryInfo.emplace(m.catalog->entries[i].id, i);
  m.familyAt.clear();
  for (size_t i = 0; i < m.catalog->families.size(); ++i) m.familyAt.emplace(m.catalog->families[i].id, i);
  if (!partial && carryFormerIds(m.catalog->entries, m.entryInfo, m.favorites, m.unfavorited, m.recents))
    saveListsLater();
  ++m.catalogRevision;
  rebuildEntries();
}

void Store::installRenamed(std::shared_ptr<const std::vector<Channel>> list,
                           std::shared_ptr<const CatalogResult> cat) {
  Impl& m = *impl_;
  // Only event slots' names changed (catalog::renamedEventSlots: the same
  // channels, guide ids and regular names; the catalog's rows copied): no
  // row can have, so the rows made for the old list hold for this one, and
  // the publish below is a patch.
  bool rowsHold = rowsCurrent();
  m.channels = std::move(list);
  m.catalog = std::move(cat);
  if (rowsHold) m.rowsFrom = {m.channels, m.catalog, m.lineupSerial};
  m.indexByID.clear();
  m.indexByID.reserve(m.channels->size());
  for (size_t i = 0; i < m.channels->size(); ++i) m.indexByID.emplace((*m.channels)[i].id, i);
  m.entryInfo.clear();
  for (size_t i = 0; i < m.catalog->entries.size(); ++i) m.entryInfo.emplace(m.catalog->entries[i].id, i);
  m.familyAt.clear();
  for (size_t i = 0; i < m.catalog->families.size(); ++i) m.familyAt.emplace(m.catalog->families[i].id, i);
  ++m.catalogRevision;  // Live Events rebuild from the new names
  m.channelsDirty = true;
}

void Store::saveCatalog() {
  Impl& m = *impl_;
  if (m.partial || m.channels->empty()) return;
  // The first-screen file is the rows on screen: wait for them.
  if (!m.rowsBuilt) {
    m.saveOnRows = true;
    return;
  }
  m.saveOnRows = false;
  m.catalogUnsaved = false;
  m.catalogSavedAt = time::now();
  // The live list and catalog are shared with the writer, not copied, and
  // written a piece at a time.
  struct Job {
    std::shared_ptr<const std::vector<Channel>> channels;
    std::shared_ptr<const CatalogResult> catalog;
    std::map<std::string, std::vector<std::string>> formats;
    catalog::cache::SavedRef ref;
  };
  auto job = std::make_shared<Job>();
  job->channels = m.channels;
  job->catalog = m.catalog;
  for (const auto& [source, run] : m.runs)
    if (!run.formats.empty()) job->formats[source] = run.formats;
  job->ref.signature = m.signature;
  job->ref.savedAt = m.channelsLoadedAt ? m.channelsLoadedAt : time::now();
  job->ref.fingerprint = m.listFingerprint;
  std::set<std::string> first(m.lineup.lineupIDs.begin(), m.lineup.lineupIDs.end());
  for (const Channel& e : m.lineup.browseEntries) {
    if (first.size() > 400) break;
    first.insert(e.id);
  }
  first.insert(m.favorites.begin(), m.favorites.end());
  for (const std::string& r : m.recents) first.insert(r);
  std::set<std::string> families;
  for (const auto& f : m.catalog->families) families.insert(f.id);
  std::string firstPath = host_.files.path(catalog::cache::fileName(m.signature, catalog::cache::Part::first));
  std::string fullPath = host_.files.path(catalog::cache::fileName(m.signature, catalog::cache::Part::full));
  host_.pool.post(
      [job, first, families, firstPath, fullPath, keeper = m.disk, owners = diskOwners()] {
        job->ref.channels = job->channels.get();
        job->ref.catalog = job->catalog.get();
        job->ref.xtreamFormats = &job->formats;
        int64_t partBytes = 0, fullBytes = 0;
        {
          // Games on now or still to come (a game started up to 6 h ago may
          // still be on); the full file has the rest.
          catalog::cache::Saved part = catalog::cache::firstPart(job->ref, first, families, time::now() - 6 * 3600);
          part.fingerprint = 0;  // only the full list can skip a sort
          writeFileStreaming(
              firstPath,
              [&](const json::Writer::Sink& sink) {
                return catalog::cache::writeSaved(catalog::cache::ref(part), [&](std::string_view c) {
                  partBytes += static_cast<int64_t>(c.size());
                  return sink(c);
                });
              },
              false, keeper.get());
        }
        // The full list gzipped (read after the first screen is up); the
        // first-screen file stays plain, so launch never waits to inflate.
        writeFileStreaming(
            fullPath,
            [&](const json::Writer::Sink& sink) {
              return catalog::cache::writeSaved(job->ref, [&](std::string_view c) {
                fullBytes += static_cast<int64_t>(c.size());
                return sink(c);
              });
            },
            true, keeper.get());
        MLOG_I("store", "saved channel list: first screen %s, full %s", catalog::sizeText(partBytes).c_str(),
               catalog::sizeText(fullBytes).c_str());
        // An older list of other providers goes now (it has no owner).
        if (keeper) keeper->prune(owners);
      },
      nullptr, http::Priority::Background);
}

// ------------------------------------------------------------ My Lineup

void Store::rebuildEntries() {
  Impl& m = *impl_;
  uint64_t generation = ++m.rebuildGeneration;
  m.rowsBuilt = false;
  auto input = std::make_shared<catalog::LineupInput>();
  // Shared, not copied: the list and rows are immutable once installed.
  input->sharedInfos = std::shared_ptr<const std::vector<catalog::EntryInfo>>(m.catalog, &m.catalog->entries);
  input->sharedChannels = m.channels;
  input->prefs = m.prefs;
  input->coast = catalog::resolveCoast(m.prefs);
  input->otherGroup = kOtherGroup;
  input->showOther = settings_.showOther;
  for (const auto& [id, list] : m.outsideMatches) input->guideIDs.insert(id);
  for (const auto& [id, list] : m.providerMatches) input->guideIDs.insert(id);
  for (const auto& [id, list] : m.serverPrograms)
    if (!list.empty()) input->guideIDs.insert(id);
  // Local favourites count as favourites for the lineup rules, with the
  // server's own (Emby / Jellyfin) ones.
  std::unordered_set<std::string> favorites;
  for (const Channel& c : *m.channels)
    if (c.isFavorite && str::startsWith(c.serverId, "jf:")) favorites.insert(c.id);
  // Server favourites removed here (Build 149): out, and forgotten once the
  // server's list no longer has them.
  for (auto u = m.unfavorited.begin(); u != m.unfavorited.end();) {
    auto it = m.entryInfo.find(*u);
    bool still = false;
    if (it != m.entryInfo.end())
      for (const std::string& member : m.catalog->entries[it->second].members)
        still = favorites.erase(member) > 0 || still;
    u = still || it == m.entryInfo.end() ? std::next(u) : m.unfavorited.erase(u);
  }
  for (const std::string& fav : m.favorites) {
    auto it = m.entryInfo.find(fav);
    if (it == m.entryInfo.end()) continue;
    for (const std::string& member : m.catalog->entries[it->second].members)
      if (m.indexByID.count(member)) favorites.insert(member);
  }
  input->favoriteIDs = std::move(favorites);
  auto out = std::make_shared<catalog::LineupOutput>();
  // The rows' names, logos and order with it, on the same worker: a publish
  // then only copies them (a tune or a favourite no longer works out every
  // row's logo on the UI thread).
  auto rows = std::make_shared<std::shared_ptr<const ChannelRows>>();
  auto rowsMs = std::make_shared<double>(0);
  auto lineupMs = std::make_shared<double>(0);
  std::shared_ptr<const CatalogResult> cat = m.catalog;
  std::shared_ptr<const std::vector<Channel>> list = m.channels;
  uint64_t gen = m.generation;
  host_.pool.post([input, out, rows, rowsMs, lineupMs, cat, list, iptvOrg = m.iptvOrg] {
                     auto began = SteadyClock::now();
                     *out = catalog::lineup(*input);
                     *lineupMs = msSince(began);
                     auto started = SteadyClock::now();
                     *rows = makeRows(*out, *cat, *list, iptvOrg.get(), nullptr, nullptr);
                     *rowsMs = msSince(started);
                   },
                   [this, gen, generation, out, rows, rowsMs, lineupMs, cat, list] {
                     if (gen != impl_->generation || generation != impl_->rebuildGeneration) return;
                     impl_->lineup = std::move(*out);
                     ++impl_->lineupSerial;
                     releaseLater(host_.pool, std::move(impl_->rows));
                     impl_->rows = std::move(*rows);
                     impl_->rowsFrom = {list, cat, impl_->lineupSerial};
                     Impl::PublishTiming& t = impl_->publishTiming;
                     ++t.made;
                     t.madeMax = std::max(t.madeMax, *rowsMs);
                     t.lineupMax = std::max(t.lineupMax, *lineupMs);
                     ++impl_->lineupRevision;
                     impl_->channelsDirty = true;
                     impl_->rowsBuilt = true;
                     if (impl_->saveOnRows) saveCatalog();
                     // Entry guides from whatever member data is already here.
                     std::vector<std::string> ids;
                     ids.reserve(impl_->entryInfo.size());
                     for (const auto& [id, idx] : impl_->entryInfo) ids.push_back(id);
                     applyEntryGuides(ids);
                     // The launch's rows, asked for before there was a lineup.
                     if (impl_->listingsOnLineup) {
                       impl_->listingsOnLineup = false;
                       wantFirstRows();
                     }
                   },
                   http::Priority::Visible);
}

void Store::readyWhenShown() {
  Impl& m = *impl_;
  // A first-screen file with nothing in it (older versions wrote it before
  // the rows existed) waits for the full one.
  if ((channels_ && !channels_->all.empty()) || (m.catalog->entries.empty() && !m.partial)) {
    status_.phase = StoreStatus::Phase::Ready;
    m.readyOnPublish = false;
    return;
  }
  // Still the progress screen until the rows are published (tick).
  m.readyOnPublish = true;
}

catalog::LineupPrefs Store::lineupPrefs() const { return impl_->prefs; }


void Store::loadMarkets(std::function<void(TvMarkets)> done) {
  Impl& m = *impl_;
  std::string path = host_.files.path("markets-v2.json");  // v2: US markets only
  // Names the market suggestion reads: the sorted list's, or while the
  // first one sorts, the downloaded list's.
  auto names = std::make_shared<std::vector<std::string>>(
      m.channels->empty() && m.receivedNames ? *m.receivedNames : suggestionNames(*m.channels));
  auto result = std::make_shared<TvMarkets>();
  auto list = std::make_shared<std::vector<catalog::markets::Market>>();
  // The list built into the app (read from Wikipedia by the build); else
  // one an older version saved, else the built-in names. Never fetched.
  host_.pool.post(
      [path, names, result, list] {
        const catalog::wiki::Data& built = catalog::wiki::builtIn();
        std::string text;
        if (!built.markets.empty()) {
          *list = built.markets;
        } else if (readFile(path, text)) {
          if (auto saved = catalog::markets::fromJson(text)) *list = std::move(*saved);
        }
        if (list->empty()) *list = catalog::markets::builtinMarkets();
        for (const auto& mk : *list) result->all.push_back(mk.name);
        for (const auto& mk : catalog::markets::suggested(*list, *names)) result->suggested.push_back(mk.name);
      },
      [this, result, list, done] {
        impl_->tvMarkets = *list;
        done(*result);
      },
      http::Priority::Visible);
}

void Store::resolveMarkets(const std::vector<std::string>& names, const catalog::LineupPrefs& draft,
                           std::function<void(std::vector<catalog::MarketChoice>)> done) {
  // Each picked market's call signs: from the data built into the app (its
  // Wikipedia station list), else station-directory stations in its cities.
  const catalog::wiki::Data& built = catalog::wiki::builtIn();
  std::vector<catalog::MarketChoice> out;
  for (const std::string& name : names) {
    auto existing = std::find_if(draft.markets.begin(), draft.markets.end(),
                                 [&](const catalog::MarketChoice& c) { return c.name == name; });
    if (existing != draft.markets.end() && !existing->callsigns.empty()) {
      out.push_back(*existing);
      continue;
    }
    auto mk = std::find_if(impl_->tvMarkets.begin(), impl_->tvMarkets.end(),
                           [&](const catalog::markets::Market& x) { return x.name == name; });
    catalog::markets::Market pick = mk != impl_->tvMarkets.end() ? *mk : catalog::markets::Market{name, std::nullopt};
    catalog::MarketChoice choice;
    choice.name = pick.name;
    choice.templateName = pick.templateName;
    if (const std::vector<std::string>* calls = built.callsignsOf(name)) choice.callsigns = *calls;
    if (choice.callsigns.empty()) choice.callsigns = catalog::markets::fallbackCallsigns(pick, *impl_->stations);
    out.push_back(std::move(choice));
  }
  done(std::move(out));
}

std::vector<MarketSummary> Store::countries(const std::set<std::string>& selected) const {
  std::map<std::string, int> counts = {{"US", 0}, {"US-ES", 0}};
  for (const EntryInfo& info : impl_->catalog->entries) {
    if (info.kind == catalog::ChannelKind::channel && info.market) ++counts[*info.market];
  }
  std::vector<MarketSummary> out;
  for (const auto& [code, n] : counts) out.push_back({code, marketName(code), n, selected.count(code) > 0});
  std::map<std::string, int> rank = {{"US", 0}, {"US-ES", 1}};
  std::sort(out.begin(), out.end(), [&](const MarketSummary& a, const MarketSummary& b) {
    int ra = rank.count(a.code) ? rank[a.code] : 2, rb = rank.count(b.code) ? rank[b.code] : 2;
    return ra != rb ? ra < rb : a.count > b.count;
  });
  // Codes are logged (never channel names) so a missing country can be traced.
  std::string line;
  for (size_t i = 0; i < out.size() && i < 12; ++i) line += out[i].code + "=" + std::to_string(out[i].count) + " ";
  MLOG_I("lineup", "countries: %s", line.c_str());
  return out;
}

std::string Store::marketName(const std::string& code) {
  static const std::map<std::string, std::string> kNames = {
      {"US", "United States (English)"}, {"US-ES", "United States (Spanish)"}, {"UK", "United Kingdom"},
      {"CA", "Canada"}, {"MX", "Mexico"}, {"AU", "Australia"}, {"NZ", "New Zealand"}, {"IE", "Ireland"},
      {"FR", "France"}, {"DE", "Germany"}, {"ES", "Spain"}, {"IT", "Italy"}, {"PT", "Portugal"},
      {"BR", "Brazil"}, {"AR", "Argentina"}, {"NL", "Netherlands"}, {"BE", "Belgium"}, {"IN", "India"},
      {"PK", "Pakistan"}, {"PH", "Philippines"}, {"TR", "Turkey"}, {"PL", "Poland"}, {"SE", "Sweden"},
      {"NO", "Norway"}, {"DK", "Denmark"}, {"FI", "Finland"}, {"GR", "Greece"}, {"RO", "Romania"},
      {"ASIA", "Asia"}, {"AFR", "Africa"}, {"EXYU", "Ex-Yugoslavia"}, {"LA", "Latin America"},
      {"KU", "Kurdish"}, {"CO", "Colombia"}, {"CL", "Chile"}, {"PE", "Peru"}, {"VE", "Venezuela"},
      {"DO", "Dominican Republic"}, {"PR", "Puerto Rico"}, {"JP", "Japan"}, {"KR", "South Korea"},
      {"CN", "China"}, {"VN", "Vietnam"}, {"TH", "Thailand"}, {"ID", "Indonesia"}, {"RU", "Russia"},
      {"UA", "Ukraine"}, {"IL", "Israel"}, {"AE", "United Arab Emirates"}, {"SA", "Saudi Arabia"},
      {"EG", "Egypt"}, {"ZA", "South Africa"}, {"NG", "Nigeria"}, {"CH", "Switzerland"}, {"AT", "Austria"},
      {"AL", "Albania"}, {"IR", "Iran"}, {"HU", "Hungary"}, {"CZ", "Czechia"}};
  auto it = kNames.find(code);
  return it == kNames.end() ? code : it->second;
}

std::vector<CategorySummary> Store::categories(const catalog::LineupPrefs& draft) const {
  const Impl& m = *impl_;
  // Counts: (in the suggested lineup with every category on, total).
  catalog::LineupPrefs everything = draft;
  everything.categories = std::set<catalog::LineupCategory>(catalog::kAllCategories.begin(), catalog::kAllCategories.end());
  std::map<catalog::LineupCategory, std::pair<int, int>> counts;
  auto coasts = catalog::CoastFeeds::of(m.catalog->entries, catalog::resolveCoast(draft));
  for (const EntryInfo& info : m.catalog->entries) {
    auto& c = counts[info.category];
    ++c.second;
    if (catalog::suggests(info, everything, entryHasGuide(info.id), info.name, coasts)) ++c.first;
  }
  std::vector<CategorySummary> out;
  for (catalog::LineupCategory cat : catalog::kAllCategories) {
    CategorySummary s;
    s.key = catalog::rawValue(cat);
    s.title = catalog::title(cat);
    s.detail = catalog::detail(cat);
    s.suggested = counts[cat].first;
    s.total = counts[cat].second;
    s.on = draft.categories.count(cat) > 0;
    out.push_back(std::move(s));
  }
  return out;
}

void Store::buildLineup(catalog::LineupPrefs draft) {
  Impl& m = *impl_;
  if (draft.countries.empty()) draft.countries = catalog::LineupPrefs::defaultCountries();
  draft.suggested.clear();
  auto coasts = catalog::CoastFeeds::of(m.catalog->entries, catalog::resolveCoast(draft));
  for (const EntryInfo& info : m.catalog->entries) {
    if (catalog::suggests(info, draft, entryHasGuide(info.id), info.name, coasts)) draft.suggested.insert(info.id);
  }
  draft.configured = true;
  draft.builtAt = time::now();
  setPrefs(draft);
}


void Store::toggleLineup(const std::string& entryId) {
  Impl& m = *impl_;
  catalog::LineupPrefs p = m.prefs;
  bool included = m.lineup.lineupIDs.count(entryId) > 0;
  if (!included) {
    p.removed.erase(entryId);
    p.added.insert(entryId);  // a hand-added channel stays as suggestions change
    m.lineup.lineupIDs.insert(entryId);
  } else {
    p.added.erase(entryId);
    p.removed.insert(entryId);
    m.lineup.lineupIDs.erase(entryId);
  }
  m.channelsDirty = true;
  setPrefs(p);
}

void Store::addToLineup(const std::vector<std::string>& entryIds) {
  Impl& m = *impl_;
  catalog::LineupPrefs p = m.prefs;
  for (const std::string& id : entryIds) {
    p.removed.erase(id);
    p.added.insert(id);
    m.lineup.lineupIDs.insert(id);
  }
  m.channelsDirty = true;
  setPrefs(p);
}

bool Store::isFavorite(const std::string& entryId) const {
  const Impl& m = *impl_;
  auto at = m.lineup.entryIndex.find(entryId);
  bool server = at != m.lineup.entryIndex.end() && m.lineup.entries[at->second].isFavorite;
  return favoriteNow(entryId, server, m.favorites, m.unfavorited);
}

void Store::toggleFavorite(const std::string& entryId) {
  Impl& m = *impl_;
  // From what the row shows: a server favourite (Emby / Jellyfin) is
  // removed too, not made a local one.
  auto at = m.lineup.entryIndex.find(entryId);
  bool server = at != m.lineup.entryIndex.end() && m.lineup.entries[at->second].isFavorite;
  bool fav = !isFavorite(entryId);
  if (fav) {
    m.favorites.insert(entryId);
    m.unfavorited.erase(entryId);
  } else {
    m.favorites.erase(entryId);
    if (server) m.unfavorited.insert(entryId);
  }
  saveListsLater();
  m.channelsDirty = true;
  // Emby favourites also go to the server.
  auto it = m.entryInfo.find(entryId);
  if (it == m.entryInfo.end()) return;
  for (const std::string& member : m.catalog->entries[it->second].members) {
    auto ci = m.indexByID.find(member);
    if (ci == m.indexByID.end()) continue;
    const Channel& c = (*m.channels)[ci->second];
    if (!str::startsWith(c.serverId, "jf:")) continue;
    for (const sources::Account& a : m.accounts) {
      if (a.sourceId() != c.serverId) continue;
      if (auto client = sources::emby::Client::make(a)) {
        host_.pool.submit(toHttp(client.value().setFavoriteRequest(fav, c.itemId), "emby.favorite"), nullptr);
      }
    }
  }
}

void Store::noteWatched(const std::string& entryId) {
  Impl& m = *impl_;
  m.recents.erase(std::remove(m.recents.begin(), m.recents.end(), entryId), m.recents.end());
  m.recents.insert(m.recents.begin(), entryId);
  if (m.recents.size() > kRecentMax) m.recents.resize(kRecentMax);
  saveListsLater();
  m.channelsDirty = true;
}

// ------------------------------------------------------------ guide

time::Seconds Store::windowStart() const { return catalog::windowStart(time::now(), host_.zone()); }

bool Store::entryHasGuide(const std::string& id) const {
  const Impl& m = *impl_;
  auto it = m.entryInfo.find(id);
  if (it == m.entryInfo.end()) return false;
  for (const std::string& member : m.catalog->entries[it->second].members) {
    if (m.outsideMatches.count(member) || m.providerMatches.count(member)) return true;
    auto s = m.serverPrograms.find(member);
    if (s != m.serverPrograms.end() && !s->second.empty()) return true;
  }
  return false;
}

void Store::wantListings(const std::vector<std::string>& ids) {
  Impl& m = *impl_;
  if (ids.empty() || m.channels->empty()) return;
  time::Seconds start = windowStart();
  if (!m.guideWindowStart) m.guideWindowStart = start;
  bool added = false;
  for (const std::string& id : ids) {
    std::vector<std::string> raw;
    auto info = m.entryInfo.find(id);
    if (info != m.entryInfo.end()) {
      const auto& members = m.catalog->entries[info->second].members;
      for (size_t k = 0; k < members.size() && k < 2; ++k) raw.push_back(members[k]);
    } else {
      raw.push_back(id);
    }
    for (const std::string& r : raw) {
      if (!m.indexByID.count(r)) continue;
      auto t = m.catalog->traits.find(r);
      if (t != m.catalog->traits.end() && t->second.familyKey) continue;  // event feeds: names, not guides
      Impl::GuideState st = m.guideState[r];
      if (st == Impl::GuideState::Loading || m.pendingGuideSet.count(r)) continue;
      if (st == Impl::GuideState::Loaded && m.guideWindowByChannel[r] == start) continue;
      if (st == Impl::GuideState::Loaded) {
        auto have = m.serverPrograms.find(r);
        time::Seconds until = 0;
        if (have != m.serverPrograms.end())
          for (const Program& p : have->second) until = std::max(until, p.end);
        if (until >= start + kListingsAheadEnough) {
          m.guideWindowByChannel[r] = start;
          continue;
        }
      }
      m.pendingGuide.push_back(r);
      m.pendingGuideSet.insert(r);
      added = true;
    }
  }
  // Build 153: rows asked for on screen are weighed by channel identity;
  // one with listings already in and never weighed is weighed now.
  std::vector<std::string> weigh;
  // Rows seen on screen lately: browsing every row of a big list starts over
  // (those on screen now are asked for again here).
  if (m.choiceRows.size() > kChoiceRowsMax) m.choiceRows.clear();
  for (const std::string& id : ids) {
    if (!m.entryInfo.count(id) || !m.choiceRows.insert(id).second) continue;
    if (!m.rowGuides.count(id) && !m.choiceAsked.count(id) && entryHasGuide(id)) weigh.push_back(id);
  }
  decideRows(weigh);
  // Most recently requested last (they are fetched first).
  for (const std::string& id : ids) {
    m.recentlyRequested.erase(std::remove(m.recentlyRequested.begin(), m.recentlyRequested.end(), id),
                              m.recentlyRequested.end());
    m.recentlyRequested.push_back(id);
  }
  if (m.recentlyRequested.size() > kRecentlyRequestedMax)
    m.recentlyRequested.erase(m.recentlyRequested.begin(),
                              m.recentlyRequested.begin() + static_cast<long>(m.recentlyRequested.size() - kRecentlyRequestedMax));
  if (added && !m.guideFlushAt) m.guideFlushAt = host_.ms() + kGuideFlushMs;
}

void Store::startGuideFetches() {
  Impl& m = *impl_;
  while (m.activeGuideFetches < kGuideConcurrency && !m.pendingGuide.empty()) {
    // One batch from one server, most recently requested first.
    const std::string lastID = m.pendingGuide.back();
    auto li = m.indexByID.find(lastID);
    if (li == m.indexByID.end()) {
      m.pendingGuide.pop_back();
      m.pendingGuideSet.erase(lastID);
      continue;
    }
    std::string serverID = (*m.channels)[li->second].serverId;
    std::vector<std::string> batch;
    for (long i = static_cast<long>(m.pendingGuide.size()) - 1; i >= 0 && batch.size() < kProgramBatch; --i) {
      const std::string id = m.pendingGuide[static_cast<size_t>(i)];
      auto ci = m.indexByID.find(id);
      if (ci != m.indexByID.end() && (*m.channels)[ci->second].serverId == serverID) {
        batch.push_back(id);
        m.pendingGuide.erase(m.pendingGuide.begin() + i);
        m.pendingGuideSet.erase(id);
      }
    }
    if (batch.empty()) break;
    const sources::Account* account = nullptr;
    for (const sources::Account& a : m.accounts)
      if (a.sourceId() == serverID) account = &a;
    if (!account || account->kind == sources::AccountKind::M3U) {
      // M3U has no per-channel guide: the playlist's XMLTV and outside
      // guides cover it.
      for (const std::string& id : batch) m.guideState[id] = Impl::GuideState::Loaded;
      applyEntryGuides(entriesOf(batch));
      continue;
    }
    if (account->kind == sources::AccountKind::Xtream) fetchXtreamGuide(batch, *account);
    else fetchEmbyGuide(batch, *account);
  }
}

std::vector<std::string> Store::entriesOf(const std::vector<std::string>& rawIds) const {
  std::set<std::string> out;
  for (const std::string& id : rawIds) {
    auto it = impl_->catalog->entryOf.find(id);
    if (it != impl_->catalog->entryOf.end()) out.insert(it->second);
  }
  return std::vector<std::string>(out.begin(), out.end());
}

bool Store::providerCovers(const std::string& id, time::Seconds start) const {
  auto it = impl_->providerMatches.find(id);
  if (it == impl_->providerMatches.end()) return false;
  int low = static_cast<int>(start / 60);
  int high = low + static_cast<int>(kGuideHours * 60);
  for (const guide::RawProgram& p : *it->second)
    if (p.s < high && p.s + p.m > low) return true;
  return false;
}

void Store::landed(const std::vector<std::string>& ids, const std::unordered_map<std::string, std::vector<Program>>* results,
                   time::Seconds start) {
  Impl& m = *impl_;
  for (const std::string& id : ids) {
    if (results) {
      auto it = results->find(id);
      std::vector<Program> server = it == results->end() ? std::vector<Program>() : it->second;
      std::sort(server.begin(), server.end(), [](const Program& a, const Program& b) { return a.start < b.start; });
      m.serverPrograms[id] = std::move(server);
    }
    m.touchedListings.insert(id);
    std::vector<Program> best = bestListing(id);
    if (best.empty()) m.programs.erase(id);
    else m.programs[id] = std::make_shared<const std::vector<Program>>(std::move(best));
    m.guideState[id] = Impl::GuideState::Loaded;
    m.guideWindowByChannel[id] = start;
  }
  applyEntryGuides(entriesOf(ids));
}

void Store::fetchXtreamGuide(const std::vector<std::string>& allIDs, const sources::Account& account) {
  Impl& m = *impl_;
  time::Seconds start = windowStart();
  std::vector<std::string> covered, ids;
  for (const std::string& id : allIDs) (providerCovers(id, start) ? covered : ids).push_back(id);
  if (!covered.empty()) landed(covered, nullptr, start);
  if (ids.empty()) return;
  auto client = sources::xtream::Client::make(account);
  if (!client) {
    for (const std::string& id : ids) m.guideState[id] = Impl::GuideState::Failed;
    return;
  }
  auto c = std::make_shared<sources::xtream::Client>(client.take());
  for (const std::string& id : ids) m.guideState[id] = Impl::GuideState::Loading;
  ++m.activeGuideFetches;
  // Short EPG is one request per channel: six at a time for this batch.
  struct Batch {
    std::vector<std::pair<std::string, std::string>> items;  // channel id, stream id
    size_t next = 0;
    int running = 0;
    std::unordered_map<std::string, std::vector<Program>> results;
  };
  auto b = std::make_shared<Batch>();
  for (const std::string& id : ids) {
    auto ci = m.indexByID.find(id);
    if (ci != m.indexByID.end()) b->items.push_back({id, (*m.channels)[ci->second].itemId});
  }
  uint64_t gen = m.generation;
  // Each request's callback holds the pump; the last one lets it go.
  auto pump = std::make_shared<std::function<void()>>();
  std::weak_ptr<std::function<void()>> weakPump = pump;
  *pump = [this, gen, b, c, ids, start, weakPump] {
    while (b->running < kShortEpgParallel && b->next < b->items.size()) {
      auto item = b->items[b->next++];
      ++b->running;
      auto list = std::make_shared<std::vector<Program>>();
      http::Request q = toHttp(c->shortEpgRequest(item.second), "xtream.epg", http::Priority::Visible);
      std::string channelId = item.first;
      q.afterTransfer = [c, list, channelId](http::Response& r) {
        *list = c->parseShortEpg(r.status, r.body, channelId, titleGenres);
        r.body.clear();
      };
      std::shared_ptr<std::function<void()>> strong = weakPump.lock();
      host_.pool.submit(std::move(q), [this, gen, b, list, channelId, strong](http::Response&&) {
        if (gen != impl_->generation) return;
        b->results[channelId] = std::move(*list);
        --b->running;
        if (strong) (*strong)();
      });
    }
    if (b->running == 0 && b->next >= b->items.size()) {
      --impl_->activeGuideFetches;
      landed(ids, &b->results, start);
      startGuideFetches();
    }
  };
  (*pump)();
}

void Store::fetchEmbyGuide(const std::vector<std::string>& ids, const sources::Account& account) {
  Impl& m = *impl_;
  auto client = sources::emby::Client::make(account);
  if (!client) {
    for (const std::string& id : ids) m.guideState[id] = Impl::GuideState::Failed;
    return;
  }
  auto c = std::make_shared<sources::emby::Client>(client.take());
  time::Seconds start = windowStart();
  time::Seconds end = start + static_cast<time::Seconds>(kGuideHours * 3600) + 1800;
  std::vector<std::string> items;
  for (const std::string& id : ids) {
    auto ci = m.indexByID.find(id);
    if (ci != m.indexByID.end()) items.push_back((*m.channels)[ci->second].itemId);
  }
  auto req = c->programsRequest(items, start, end);
  if (!req) return;
  for (const std::string& id : ids) m.guideState[id] = Impl::GuideState::Loading;
  ++m.activeGuideFetches;
  auto list = std::make_shared<std::optional<std::vector<Program>>>();
  http::Request q = toHttp(*req, "emby.programs", http::Priority::Visible);
  q.afterTransfer = [c, list](http::Response& r) {
    auto parsed = c->parsePrograms(r.status, r.body, titleGenres);
    if (parsed) *list = parsed.take();
    r.body.clear();
  };
  uint64_t gen = m.generation;
  host_.pool.submit(std::move(q), [this, gen, ids, list, start](http::Response&&) {
    if (gen != impl_->generation) return;
    --impl_->activeGuideFetches;
    if (!*list) {
      for (const std::string& id : ids) impl_->guideState[id] = Impl::GuideState::Failed;
      applyEntryGuides(entriesOf(ids));
    } else {
      std::unordered_map<std::string, std::vector<Program>> grouped;
      for (Program& p : **list) grouped[p.channelId].push_back(std::move(p));
      landed(ids, &grouped, start);
    }
    startGuideFetches();
  });
}

std::vector<Program> Store::providerListing(const std::string& id) const {
  auto it = impl_->providerMatches.find(id);
  if (it == impl_->providerMatches.end()) return {};
  time::Seconds start = windowStart() - 3600;
  return guide::programs(*it->second, id, start, start + 14 * 3600, "pg", false);
}

std::vector<Program> Store::outsideListing(const std::string& id) const {
  auto it = impl_->outsideMatches.find(id);
  if (it == impl_->outsideMatches.end()) return {};
  time::Seconds start = windowStart() - 3600;
  return guide::programs(*it->second, id, start, start + 14 * 3600);
}

std::vector<Program> Store::bestListing(const std::string& id) const {
  // The provider's own listing (full guide topped up by the short guide),
  // then outside listings for gaps.
  auto s = impl_->serverPrograms.find(id);
  std::vector<Program> shortGuide = s == impl_->serverPrograms.end() ? std::vector<Program>() : s->second;
  std::vector<Program> full = providerListing(id);
  time::Seconds ws = windowStart();
  std::vector<Program> server = full.empty() ? shortGuide : catalog::bestListing(&full, &shortGuide, ws);
  std::vector<Program> outside = outsideListing(id);
  return catalog::bestListing(server.empty() ? nullptr : &server, outside.empty() ? nullptr : &outside, ws);
}

void Store::applyEntryGuides(const std::vector<std::string>& entryIDs) {
  // An entry's guide is the best of its versions' guides (catalog::bestGuide).
  // Build 153: rows on screen or in My Lineup are weighed by channel
  // identity on a worker (decideRows); one weighed before keeps its listing
  // until the new weighing lands, one not yet weighed shows this meanwhile.
  Impl& m = *impl_;
  time::Seconds start = windowStart();
  std::vector<const std::vector<Program>*> lists;
  std::vector<std::pair<const catalog::ProgramList*, const std::string*>> found;
  std::vector<std::string> weigh;
  for (const std::string& id : entryIDs) {
    auto info = m.entryInfo.find(id);
    if (info == m.entryInfo.end()) continue;
    bool weighed = false;
    if (weighsGuide(id)) {
      weigh.push_back(id);
      weighed = m.rowGuides.count(id) > 0;
    }
    lists.clear();
    found.clear();
    bool anyLoading = false, anyLoaded = false, anyFailed = false;
    for (const std::string& member : m.catalog->entries[info->second].members) {
      auto p = m.programs.find(member);
      if (p != m.programs.end() && p->second && !p->second->empty()) {
        lists.push_back(p->second.get());
        found.emplace_back(&p->second, &member);
      }
      auto st = m.guideState.find(member);
      if (st != m.guideState.end()) {
        anyLoading = anyLoading || st->second == Impl::GuideState::Loading;
        anyLoaded = anyLoaded || st->second == Impl::GuideState::Loaded;
        anyFailed = anyFailed || st->second == Impl::GuideState::Failed;
      }
    }
    int described = 0;
    int pick = weighed ? -1 : catalog::bestGuide(lists, start, &described);
    const catalog::ProgramList* best = pick < 0 ? nullptr : found[static_cast<size_t>(pick)].first;
    std::string bestMember = pick < 0 ? std::string() : *found[static_cast<size_t>(pick)].second;
    if (weighed) {
      auto own = m.programs.find(id);
      Impl::GuideState st = (own != m.programs.end() && own->second) || anyLoaded ? Impl::GuideState::Loaded
                            : anyLoading ? Impl::GuideState::Loading
                            : anyFailed  ? Impl::GuideState::Failed
                                         : Impl::GuideState::None;
      if (m.guideState[id] != st) m.dirtyEntries.insert(id);
      m.guideState[id] = st;
      continue;
    }
    // Unchanged since last time (same version, same programmes and tags): keep it.
    std::string key;
    if (best) {
      int tagged = 0;
      for (const Program& p : **best) tagged += p.genres ? 1 : 0;
      key = bestMember + "|" + std::to_string((*best)->size()) + "|" + (*best)->front().id + "|" + (*best)->back().id +
            "|" + std::to_string(tagged) + "|" + std::to_string(described);
    }
    if (key.empty() || m.entryGuideKeys[id] != key || !m.programs.count(id)) {
      m.entryGuideKeys[id] = key;
      if (best) m.programs[id] = *best;  // the member's list, shared
      else m.programs.erase(id);
      m.dirtyEntries.insert(id);
    }
    Impl::GuideState st = best || anyLoaded ? Impl::GuideState::Loaded
                          : anyLoading     ? Impl::GuideState::Loading
                          : anyFailed      ? Impl::GuideState::Failed
                                           : Impl::GuideState::None;
    if (m.guideState[id] != st) m.dirtyEntries.insert(id);
    m.guideState[id] = st;
  }
  if (!m.dirtyEntries.empty()) m.listingsDirty = true;
  decideRows(weigh);
}

void Store::rebuildOutsidePrograms() {
  // Re-merges every channel's listing (after a guide source loads or the
  // window moves on), on a worker; fetches that land meanwhile are redone.
  Impl& m = *impl_;
  uint64_t generation = ++m.listingsGeneration;
  auto server = std::make_shared<std::unordered_map<std::string, std::vector<Program>>>(m.serverPrograms);
  auto provider = std::make_shared<const guide::SharedPrograms>(m.providerMatches);
  auto outside = std::make_shared<const guide::SharedPrograms>(m.outsideMatches);
  std::set<std::string> ids;
  for (const auto& [id, v] : m.outsideMatches) ids.insert(id);
  for (const auto& [id, v] : m.providerMatches) ids.insert(id);
  for (const auto& [id, v] : m.programs)
    if (!str::startsWith(id, "e|")) ids.insert(id);
  time::Seconds window = windowStart();
  m.touchedListings.clear();
  auto merged = std::make_shared<catalog::ProgramLists>();
  uint64_t gen = m.generation;
  host_.pool.post(
      [server, provider, outside, ids, window, merged] {
        *merged = catalog::mergedListings(ids, *server, *provider, *outside, window);
      },
      [this, gen, generation, merged] {
        Impl& mm = *impl_;
        if (gen != mm.generation || generation != mm.listingsGeneration) return;
        for (const std::string& id : mm.touchedListings) {
          std::vector<Program> list = bestListing(id);
          if (list.empty()) merged->erase(id);
          else (*merged)[id] = std::make_shared<const std::vector<Program>>(std::move(list));
        }
        // Entry lists stay until applyEntryGuides decides they changed.
        for (auto& [key, value] : mm.programs)
          if (str::startsWith(key, "e|")) (*merged)[key] = std::move(value);
        mm.programs = std::move(*merged);
        std::vector<std::string> ids2;
        for (const auto& [id, idx] : mm.entryInfo) ids2.push_back(id);
        // Build 153: weighings of rows no longer weighed (gone, or out of
        // My Lineup and off screen) go, with the lists they hold.
        for (auto g = mm.rowGuides.begin(); g != mm.rowGuides.end();)
          g = mm.entryInfo.count(g->first) && weighsGuide(g->first) ? std::next(g) : mm.rowGuides.erase(g);
        applyEntryGuides(ids2);
        afterBigJob("listings merged");
      },
      http::Priority::Normal);
}

void Store::startGuides(bool requestRows) {
  Impl& m = *impl_;
  readSavedListings();
  scheduleProvider(false);
  scheduleOutside(false);
  if (requestRows) {
    // The lineup is still being made on its worker (installCatalog just
    // started it): its first rows are asked for once it is published.
    if (!m.rowsBuilt) m.listingsOnLineup = true;
    wantFirstRows();
  }
}

void Store::wantFirstRows() {
  Impl& m = *impl_;
  std::vector<std::string> ids;
  for (const std::string& f : m.favorites) {
    if (ids.size() >= 40) break;
    ids.push_back(f);
  }
  for (size_t i = 0; i < m.recents.size() && i < 20; ++i) ids.push_back(m.recents[i]);
  const auto& visible = m.prefs.configured ? m.lineup.lineupEntries : m.lineup.browseEntries;
  for (size_t i = 0; i < visible.size() && i < 60; ++i) ids.push_back(visible[i].id);
  wantListings(ids);
}

void Store::readSavedListings() {
  Impl& m = *impl_;
  if (m.savedListingsRead) return;
  m.savedListingsRead = true;
  m.savedListingsReading = true;
  struct Read {
    std::map<std::string, guide::provider::Saved> provider;
    std::optional<guide::Saved> outside;
  };
  auto box = std::make_shared<Read>();
  std::vector<std::pair<std::string, std::string>> keys;  // key, path
  for (const sources::Account& a : m.accounts) {
    if (!hasGuideFile(a)) continue;
    std::string key = guide::provider::cacheKey("default", a.sourceId());
    keys.push_back({key, host_.files.path(guide::provider::savedFileName(key))});
  }
  std::string outsidePath = host_.files.path(guide::savedFileName(m.signature));
  uint64_t gen = m.generation;
  host_.pool.post(
      [box, keys, outsidePath, foreground = m.foregroundJobs] {
        Foreground busy(foreground);
        // Read a piece at a time: the files run to several MB.
        for (const auto& [key, path] : keys) {
          std::ifstream in(path, std::ios::binary);
          if (!in) continue;
          if (auto s = guide::provider::readSaved(in)) box->provider[key] = std::move(*s);
        }
        std::ifstream in(outsidePath, std::ios::binary);
        if (in) box->outside = guide::readSaved(in);
      },
      [this, gen, box] {
        Impl& mm = *impl_;
        if (gen != mm.generation) return;
        mm.savedListingsReading = false;
        bool any = false;
        for (auto& [key, saved] : box->provider) {
          if (!mm.providerSaved.count(key)) {
            mm.providerSaved[key] = std::move(saved);
            any = true;
          }
        }
        if (box->outside && !mm.outsideSaved) {
          mm.outsideSaved = std::move(box->outside);
          any = true;
        }
        if (!any) return;
        installProviderSaved();
        if (mm.outsideSaved && settings_.fillMissingListings) {
          mm.outsideMatches = mm.outsideSaved->programs;
        }
        rebuildOutsidePrograms();
        scheduleLineupRefresh();
      },
      http::Priority::Visible);
}

void Store::installProviderSaved() {
  Impl& m = *impl_;
  for (const sources::Account& a : m.accounts) {
    if (!hasGuideFile(a)) continue;
    std::string key = guide::provider::cacheKey("default", a.sourceId());
    auto it = m.providerSaved.find(key);
    if (it == m.providerSaved.end()) continue;
    std::string prefix = a.sourceId() + "|";
    for (auto p = m.providerMatches.begin(); p != m.providerMatches.end();) {
      if (str::startsWith(p->first, prefix)) p = m.providerMatches.erase(p);
      else ++p;
    }
    for (const auto& [id, list] : it->second.programs) m.providerMatches[id] = list;
  }
}

void Store::scheduleProvider(bool force) {
  Impl& m = *impl_;
  m.providerForce = m.providerForce || force;
  if (!m.providerAt) m.providerAt = host_.ms() + (force ? 0 : kProviderDelayMs);
}

void Store::scheduleOutside(bool force) {
  Impl& m = *impl_;
  m.outsideForce = m.outsideForce || force;
  if (!m.outsideAt) m.outsideAt = host_.ms() + (force ? 0 : kOutsideDelayMs);
}

void Store::scheduleLineupRefresh() {
  if (!impl_->prefs.configured) return;
  impl_->lineupRefreshAt = host_.ms() + kLineupRefreshMs;
}

void Store::lookAtGuides(uint32_t ms) {
  // Guides were started only at launch, by a sorted list and by settings: a
  // TV left on with the same channel list (or only event names changing)
  // kept listings until they ran out. A few lookups a minute.
  Impl& m = *impl_;
  time::Seconds now = time::now();
  bool woke = m.lastTickAt && now - m.lastTickAt >= events_build::kAsleepFor;
  m.lastTickAt = now;
  if (!woke && ms < m.guidesLookAt) return;
  m.guidesLookAt = ms + kGuidesLookMs;
  if (m.partial || m.channels->empty() || m.loading) return;
  // Due by its own age; none yet is due only once one was tried (launch
  // starts the first, and may still be reading the saved one). A guide that
  // started lately waits (a failed download is tried again that often).
  if (!m.providerRunning && !m.providerAt && now - m.providerTriedAt >= kGuideRetryAfter) {
    bool due = false;
    for (const sources::Account& a : m.accounts) {
      if (!hasGuideFile(a)) continue;
      auto saved = m.providerSaved.find(guide::provider::cacheKey("default", a.sourceId()));
      if (saved == m.providerSaved.end() ? m.providerTriedAt != 0
                                         : now - saved->second.built > guide::provider::kRefreshAge)
        due = true;
    }
    if (due) {
      m.providerTriedAt = now;
      scheduleProvider(false);
    }
  }
  if (settings_.fillMissingListings && !m.outsideRunning && !m.outsideAt &&
      now - m.outsideTriedAt >= kGuideRetryAfter) {
    const guide::Saved* saved = m.outsideSaved ? &*m.outsideSaved : nullptr;
    bool due = saved ? now - saved->built > guide::kRefreshAge || (saved->nextCheck && now >= saved->nextCheck)
                     : m.outsideTriedAt != 0;
    if (due) {
      m.outsideTriedAt = now;
      scheduleOutside(false);
    }
  }
}

namespace {

// Streams a guide download through gunzip into an XMLTV reader, on the
// calling (pool) thread. Big files wait while full screen video plays.
struct GuideReadState {
  std::shared_ptr<std::atomic<bool>> playing, stop;
  std::shared_ptr<std::atomic<int>> foreground;
  // Half seconds waited for playback so far in this build (shared by every
  // wait in it), and whether it waits at all: a first guide doesn't.
  std::shared_ptr<std::atomic<int>> waited = std::make_shared<std::atomic<int>>(0);
  bool pauses = true;
  std::shared_ptr<std::atomic<bool>> waiting;  // set while it waits, for the status line
};

// Guide reads wait while video plays full screen (it needs the CPU), up to
// 5 minutes in all for one build, and while a foreground job holds a lot of
// memory (the saved lists loading, the channel sort), up to 3 minutes.
void waitForPlayback(const GuideReadState& st) {
  if (st.pauses && st.playing->load()) {
    if (st.waiting) *st.waiting = true;
    while (*st.waited < 600 && st.playing->load() && !st.stop->load()) {
      usleep(500000);
      ++*st.waited;
    }
    if (st.waiting) *st.waiting = false;
  }
  for (int i = 0; i < 360 && st.foreground->load() > 0 && !st.stop->load(); ++i) usleep(500000);
}

}  // namespace

void Store::runProviderGuide() {
  Impl& m = *impl_;
  // One already running (or only the first part of the list in): a forced
  // build stays asked for, and runs when that one ends (or the full list
  // is in). Countries changed during setup while the first guide was
  // downloading lost theirs for 12 hours.
  if (m.providerRunning || m.partial || m.channels->empty()) return;
  bool force = m.providerForce;
  m.providerForce = false;
  struct Job {
    std::string sourceId, name, url, key;
    bool insecure = false;
    std::vector<Channel> channels;
    const guide::provider::Saved* saved = nullptr;
    // Build 132: those read for Live Events only.
    std::unordered_set<std::string> eventsOnly;
  };
  std::vector<Job> jobs;
  std::set<std::string> countries = m.prefs.guideCountries();
  // Build 132: other countries' sports channels (not in a hidden group) are
  // read too, for Live Events only; told by their row, once per row.
  std::unordered_map<std::string, bool> sportsRows;
  std::unordered_set<std::string>* eventsOnly = nullptr;  // the job's
  auto forEvents = [&m, &sportsRows, &eventsOnly](const Channel& c) {
    if (m.hiddenGroups.count(c.group.value_or(kOtherGroup))) return false;
    auto entry = m.catalog->entryOf.find(c.id);
    if (entry == m.catalog->entryOf.end()) return false;
    auto [row, fresh] = sportsRows.emplace(entry->second, false);
    if (fresh) {
      auto at = m.entryInfo.find(entry->second);
      if (at != m.entryInfo.end()) {
        const EntryInfo& info = m.catalog->entries[at->second];
        row->second = info.kind == catalog::ChannelKind::channel && info.market && info.closed.empty() &&
                      (info.category == catalog::LineupCategory::international ||
                       info.category == catalog::LineupCategory::sports) &&
                      sports::brands::isSportsBrand(info.name);
      }
    }
    if (row->second && eventsOnly) eventsOnly->insert(c.id);
    return row->second;
  };
  for (const sources::Account& a : m.accounts) {
    if (!hasGuideFile(a)) continue;
    Job j;
    if (a.kind == sources::AccountKind::Xtream) {
      auto client = sources::xtream::Client::make(a);
      if (!client) continue;
      j.url = client.value().guideURL();
    } else {
      j.url = a.guideUrl;
      j.insecure = a.allowInsecureTLS;
    }
    j.sourceId = a.sourceId();
    j.name = redact(a.displayName);
    j.key = guide::provider::cacheKey("default", j.sourceId);
    eventsOnly = &j.eventsOnly;
    for (const Channel& c : *m.channels) {
      if (c.serverId != j.sourceId) continue;
      auto t = m.catalog->traits.find(c.id);
      if (t != m.catalog->traits.end() && t->second.familyKey) continue;
      if (guide::provider::inScope(c, countries, forEvents)) j.channels.push_back(c);
    }
    eventsOnly = nullptr;
    if (j.channels.empty()) continue;
    auto saved = m.providerSaved.find(j.key);
    const guide::provider::Saved* s = saved == m.providerSaved.end() ? nullptr : &saved->second;
    if (!force && !guide::provider::needsRebuild(s, static_cast<int>(j.channels.size()), time::now())) continue;
    j.saved = s;
    jobs.push_back(std::move(j));
  }
  if (jobs.empty()) return;
  m.providerRunning = true;
  m.providerTriedAt = time::now();
  auto results = std::make_shared<std::vector<std::pair<std::string, guide::provider::Outcome>>>();
  // No guide yet: it loads while you watch too (there is nothing to show
  // in its place).
  bool first = std::all_of(jobs.begin(), jobs.end(), [](const Job& j) { return !j.saved; });
  auto shared = std::make_shared<std::vector<Job>>(std::move(jobs));
  GuideReadState st;
  st.playing = m.playingFull;
  st.stop = m.stopJobs;
  st.foreground = m.foregroundJobs;
  st.pauses = !first;
  st.waiting = m.providerWaiting;
  http::Pool& pool = host_.pool;
  std::string dir = host_.files.dir();
  Files* storage = &host_.files;
  uint64_t gen = m.generation;
  std::shared_ptr<std::atomic<int>> progress = m.providerProgress;
  *progress = -1;
  host_.pool.post(
      [shared, results, st, &pool, storage, progress, keeper = m.disk] {
        size_t count = shared->size();
        for (size_t index = 0; index < count; ++index) {
          Job& j = (*shared)[index];
          if (st.stop->load()) return;
          *progress = -1;
          std::string url = j.url;
          bool insecure = j.insecure;
          guide::provider::ReadExactFn read = [&pool, url, insecure, st, progress, index, count](
                                                  const auto& ids, guide::Window window,
                                                  guide::provider::ReadFailure& failure)
              -> std::optional<guide::provider::ExactRead> {
            waitForPlayback(st);
            auto reader = std::make_shared<guide::XmltvReader>(guide::XmltvReader::exact(ids, window));
            reader->setPauseHook([st] { waitForPlayback(st); });
            http::Request q;
            q.url = url;
            q.tag = "provider.guide";
            q.gunzip = true;
            q.follow = true;
            q.personalServer = insecure;
            q.timeoutMs = 300000;
            q.priority = http::Priority::Background;
            q.onChunk = [reader](std::string_view c) {
              reader->feed(c);
              return true;
            };
            // Setup's "Getting your guide ready" bar: each file its share,
            // by its bytes against the size the server gave (none: -1).
            q.onProgress = [progress, index, count](uint64_t received, uint64_t total) {
              if (!total) return;
              double within = std::min(1.0, static_cast<double>(received) / static_cast<double>(total));
              double all = (static_cast<double>(index) + within) / static_cast<double>(count);
              *progress = static_cast<int>(all * 1000.0);
            };
            http::Response r = pool.fetchNow(std::move(q));
            if (!r.ok) {
              failure.httpStatus = r.status;
              failure.message = r.status ? "The provider's guide answered with error " + std::to_string(r.status)
                                         : "Couldn't download the provider's guide (" + r.error + ")";
              return std::nullopt;
            }
            reader->finish();
            guide::provider::ExactRead out;
            out.programs = reader->result();
            out.guideChannels = reader->guideChannelCount();
            out.programmes = reader->programmeCount();
            out.bytes = static_cast<int64_t>(r.bytes);
            return out;
          };
          guide::provider::Outcome o = guide::provider::build(j.channels, time::now(), read, nullptr, &j.eventsOnly);
          if (o.kind == guide::provider::Outcome::Kind::built) {
            // Not saved (no room): the guide is used from memory all the same.
            writeFileStreaming(
                storage->path(guide::provider::savedFileName(j.key)),
                [&o](const json::Writer::Sink& sink) { return guide::provider::writeSaved(o.saved, sink); }, true,
                keeper.get());
          }
          results->push_back({j.key, std::move(o)});
        }
      },
      [this, gen, results] {
        Impl& mm = *impl_;
        if (gen != mm.generation) return;
        mm.providerRunning = false;
        *mm.providerProgress = -1;
        // Forced while this one ran: the next one now.
        if (mm.providerForce) {
          mm.providerAt = 0;
          scheduleProvider(true);
        }
        for (auto& [key, outcome] : *results) {
          if (outcome.kind == guide::provider::Outcome::Kind::built) {
            MLOG_I("guide", "provider guide: %zu channels matched", outcome.saved.programs.size());
            mm.providerSaved[key] = std::move(outcome.saved);
            mm.providerProblems.erase(key);
          } else {
            MLOG_W("guide", "provider guide: %s", redact(outcome.message).c_str());
            mm.providerProblems[key] = redact(outcome.message);
          }
        }
        installProviderSaved();
        rebuildOutsidePrograms();
        scheduleLineupRefresh();
        afterBigJob("provider guide built");
        pruneDisk();
      },
      http::Priority::LongFirst);
  (void)dir;
}

void Store::runOutsideGuide() {
  Impl& m = *impl_;
  // As the provider guide: a forced build waits for the one running.
  if (m.outsideRunning || m.partial || m.channels->empty()) return;
  bool force = m.outsideForce;
  m.outsideForce = false;
  if (!settings_.fillMissingListings) return;
  // Event feeds are named per event; only the chosen countries' channels
  // (or ones of unknown country) take part.
  std::set<std::string> countries = m.prefs.guideCountries();
  auto snapshot = std::make_shared<std::vector<Channel>>();
  for (const Channel& c : *m.channels) {
    auto t = m.catalog->traits.find(c.id);
    if (t == m.catalog->traits.end() || t->second.familyKey || t->second.kind == catalog::ChannelKind::placeholder ||
        t->second.kind == catalog::ChannelKind::separator)
      continue;
    if (t->second.market) {
      std::string market = *t->second.market;
      std::string country = market.substr(0, market.find('-'));
      if (!countries.count(country)) continue;
    }
    snapshot->push_back(c);
  }
  std::set<std::string> shards = guideShards();
  const guide::Saved* saved = m.outsideSaved ? &*m.outsideSaved : nullptr;
  if (saved) {
    std::set<std::string> savedShards(saved->shards.begin(), saved->shards.end());
    if (savedShards == shards) {
      m.outsideMatches = saved->programs;
    }
  }
  if (!force && !guide::needsRebuild(saved, shards, static_cast<int>(snapshot->size()), time::now())) return;
  m.outsideRunning = true;
  m.outsideTriedAt = time::now();
  auto previous = std::make_shared<std::optional<guide::Saved>>(m.outsideSaved);
  auto built = std::make_shared<guide::BuildResult>();
  std::string guideDir = host_.files.path("guides");
  mkdir(guideDir.c_str(), 0700);
  std::string savedPath = host_.files.path(guide::savedFileName(m.signature));
  GuideReadState st;
  st.playing = m.playingFull;
  st.stop = m.stopJobs;
  st.foreground = m.foregroundJobs;
  st.waiting = m.outsideWaiting;
  http::Pool& pool = host_.pool;
  uint64_t gen = m.generation;
  auto newSaved = std::make_shared<std::optional<guide::Saved>>();
  host_.pool.post(
      [snapshot, shards, previous, built, guideDir, savedPath, st, &pool, newSaved, stations = m.stations,
       keeper = m.disk] {
        waitForPlayback(st);
        guide::BuildIO io;
        io.cancelled = [st] { return st.stop->load(); };
        io.fetch = [&pool, guideDir, st, keeper](const guide::Source& source, const std::optional<std::string>& etag,
                                                  const std::optional<time::Seconds>& lastModified, bool waitPlayback) {
          guide::BuildIO::FetchOutcome out;
          if (waitPlayback) waitForPlayback(st);
          std::string file = guideDir + "/" + source.id();
          std::string tmp = file + ".part";
          struct stat sb;
          bool have = stat(file.c_str(), &sb) == 0;
          // The new copy lands next to the old one: room for one more of
          // the size it had, and the margin (a first one: the margin, looked
          // at again as it downloads).
          uint64_t expected = have ? static_cast<uint64_t>(sb.st_size) : 0;
          if (keeper && !keeper->roomFor(file, std::max(expected, keeper->limits().bigWrite))) {
            if (have) {
              guide::Fetched kept;
              kept.file = file;
              kept.status = 0;
              kept.etag = etag;
              kept.bytes = sb.st_size;
              kept.problem = "not enough free space to update it";
              out.fetched = kept;
            } else {
              out.error = "not enough free space";
            }
            return out;
          }
          auto f = std::make_shared<std::ofstream>(tmp, std::ios::binary | std::ios::trunc);
          auto full = std::make_shared<bool>(false);
          http::Request q;
          q.url = source.url;
          q.tag = "outside.guide";
          q.timeoutMs = 300000;
          q.priority = http::Priority::Background;
          q.follow = true;
          if (have && etag) q.headers.push_back("If-None-Match: " + *etag);
          (void)lastModified;
          // Raw bytes to disk (inflated later, while reading). The free
          // space is looked at again every few MB: the download stops while
          // the partition still has its margin.
          auto written = std::make_shared<uint64_t>(0);
          auto nextLook = std::make_shared<uint64_t>(keeper ? keeper->limits().recheckEvery : 0);
          q.onChunk = [f, full, written, nextLook, keeper, file](std::string_view c) {
            f->write(c.data(), static_cast<std::streamsize>(c.size()));
            *written += c.size();
            if (keeper && *written >= *nextLook) {
              *nextLook = *written + keeper->limits().recheckEvery;
              if (!keeper->stillRoom(file)) *full = true;
            }
            return static_cast<bool>(*f) && !*full;
          };
          http::Response r = pool.fetchNow(std::move(q));
          bool wroteBad = !*f;
          int err = wroteBad ? errno : 0;
          f->close();  // the last of it reaches the disk here, and can fail
          bool savedOk = !wroteBad && !f->fail() && !*full;
          if (!savedOk && !*full && keeper && r.status == 200) keeper->failed(file, err ? err : errno);
          guide::Fetched fetched;
          fetched.file = file;
          if (r.status == 304 && have) {
            unlink(tmp.c_str());
            fetched.status = 304;
            fetched.etag = etag;
            fetched.bytes = sb.st_size;
            out.fetched = fetched;
            return out;
          }
          if (r.ok && r.status == 200 && savedOk && std::rename(tmp.c_str(), file.c_str()) == 0) {
            fetched.status = 200;
            if (!r.etag.empty()) fetched.etag = r.etag;
            fetched.bytes = static_cast<int64_t>(r.bytes);
            out.fetched = fetched;
            return out;
          }
          unlink(tmp.c_str());
          std::string problem = *full                       ? std::string("not enough free space to update it")
                                : r.status == 200 && !savedOk ? std::string("couldn't save it")
                                : r.status                 ? "error " + std::to_string(r.status)
                                                           : r.error;
          if (have) {
            // The saved copy after a failure.
            fetched.status = 0;
            fetched.etag = etag;
            fetched.bytes = sb.st_size;
            fetched.problem = problem;
            out.fetched = fetched;
          } else {
            out.error = problem;
          }
          return out;
        };
        io.read = [st](const guide::Source& source, const std::string& file,
                       const std::shared_ptr<const guide::Targets>& targets, guide::Window window) {
          guide::BuildIO::ReadOutcome out;
          std::ifstream in(file, std::ios::binary);
          if (!in) {
            out.error = "the saved file is missing";
            return out;
          }
          guide::XmltvReader reader = guide::XmltvReader::forSource(source, targets, window);
          reader.setPauseHook([st] { waitForPlayback(st); });
          gz::Gunzip inflate([&reader](std::string_view c) {
            reader.feed(c);
            return true;
          });
          std::vector<char> buf(64 * 1024);
          while (in && !st.stop->load()) {
            in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
            std::streamsize got = in.gcount();
            if (got <= 0) break;
            if (!inflate.feed(std::string_view(buf.data(), static_cast<size_t>(got)))) {
              out.error = "the file is damaged";
              return out;
            }
          }
          if (!inflate.finish()) {
            out.error = "the file is cut short";
            return out;
          }
          reader.finish();
          out.result = reader.sourceResult();
          return out;
        };
        const guide::Saved* prev = *previous ? &**previous : nullptr;
        *built = guide::build(*snapshot, shards, prev, *stations, time::now(), io);
        if (!built->programs.empty()) {
          guide::Saved s;
          s.built = time::now();
          s.shards.assign(shards.begin(), shards.end());
          s.channelCount = static_cast<int>(snapshot->size());
          s.programs = built->programs;
          s.notes = built->notes;
          s.others = built->others;
          s.sources = built->states;
          s.nextCheck = built->nextCheck;
          s.targetsKey = built->targetsKey;
          s.detailVersion = guide::kDetailVersion;
          s.readAt = built->readAt;
          // Not saved (no room): the listings are used from memory all the same.
          writeFileStreaming(
              savedPath, [&s](const json::Writer::Sink& sink) { return guide::writeSaved(s, sink); }, true,
              keeper.get());
          *newSaved = std::move(s);
        }
      },
      [this, gen, built, newSaved] {
        Impl& mm = *impl_;
        if (gen != mm.generation) return;
        mm.outsideRunning = false;
        if (mm.outsideForce) {
          mm.outsideAt = 0;
          scheduleOutside(true);
        }
        for (const std::string& line : built->log) MLOG_I("guide", "%s", redact(line).c_str());
        for (const std::string& p : built->problems) MLOG_W("guide", "%s", redact(p).c_str());
        pruneDisk();  // guide files of areas no longer chosen, and the budget
        if (!*newSaved) return;
        mm.outsideSaved = std::move(*newSaved);
        mm.outsideMatches = mm.outsideSaved->programs;
        rebuildOutsidePrograms();
        scheduleLineupRefresh();
        afterBigJob("outside listings built");
      },
      http::Priority::Long);
}

// ------------------------------------------------------------ memory

void Store::afterBigJob(const char* what) {
  // Handing memory back walks the whole heap (a freeze of a few hundred ms
  // on a TV): it waits for an idle moment, at most a minute, and runs on a
  // worker. The playback log notes it then, with how long it took.
  Impl& m = *impl_;
  if (m.releaseWhat.find(what) == std::string::npos)
    m.releaseWhat += (m.releaseWhat.empty() ? "" : ", ") + std::string(what);
  if (!m.releaseAfter) m.releaseAfter = std::max<uint32_t>(1, host_.ms() + kReleaseIdleMs);
}

void Store::releaseMemoryWhenIdle(uint32_t ms) {
  Impl& m = *impl_;
  if (!m.releaseAfter || ms < m.releaseAfter || m.releasing) return;
  bool idle = !host_.idle || host_.idle();
  if (!idle && ms < m.releaseAfter + kReleaseLatestMs) return;
  m.releaseAfter = 0;
  m.releasing = true;
  auto what = std::make_shared<std::string>(std::move(m.releaseWhat));
  m.releaseWhat.clear();
  auto took = std::make_shared<long>(0);
  host_.pool.post(
      [took] {
        auto started = std::chrono::steady_clock::now();
        memory::release();
        *took = static_cast<long>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
      },
      [this, what, took] {
        impl_->releasing = false;
        std::string note = *what + " (memory handed back in " + std::to_string(*took) + " ms)";
        if (host_.memoryNote) host_.memoryNote(note.c_str());
      },
      http::Priority::Background);
}

// ------------------------------------------------------------ playback

std::string Store::providerName(const std::string& channelId) const {
  for (const sources::Account& a : impl_->accounts)
    if (str::startsWith(channelId, a.sourceId() + "|")) return redact(a.displayName);
  return {};
}

std::string Store::entryOfChannel(const std::string& channelId) const {
  auto it = impl_->catalog->entryOf.find(channelId);
  return it == impl_->catalog->entryOf.end() ? std::string() : it->second;
}

std::string Store::qualityText(const std::string& channelId) const {
  const Impl& m = *impl_;
  if (std::optional<quality::Picture> p = m.pictures.find(channelId)) {
    std::string measured = quality::text(*p);
    if (!measured.empty()) return measured;
  }
  auto ci = m.indexByID.find(channelId);
  if (ci == m.indexByID.end()) return {};
  const Channel& c = (*m.channels)[ci->second];
  return quality::claimed(c.name, c.group ? std::string_view(*c.group) : std::string_view());
}

void Store::notePicture(const std::string& channelId, const quality::Picture& picture) {
  if (impl_->pictures.remember(channelId, picture)) savePicturesLater();
}

void Store::savePicturesLater() {
  Impl& m = *impl_;
  std::shared_ptr<Impl::PicturesWriter> w = m.picturesWriter;
  {
    std::lock_guard<std::mutex> lock(w->mu);
    w->pending = m.pictures.toJson();
    w->pendingSeq = ++w->seq;
  }
  // Atomic, within the folder's budget (a full partition leaves the old
  // file); a burst of measurements is written once or twice.
  std::string path = host_.files.path(kPicturesFile);
  std::shared_ptr<disk::Keeper> keeper = m.disk;
  host_.pool.post(
      [w, path, keeper] {
        std::string text;
        uint64_t seq = 0;
        {
          std::lock_guard<std::mutex> lock(w->mu);
          text.swap(w->pending);
          std::swap(seq, w->pendingSeq);
        }
        std::lock_guard<std::mutex> lock(w->fileMu);
        if (seq <= w->written) return;
        if (writeFileAtomic(path, text, false, keeper.get())) w->written = seq;
      },
      nullptr, http::Priority::Background);
}

double Store::feedReliability(const std::string& channelId) const {
  // A game's provider feeds: one that failed lately (this run) goes last;
  // else its provider group's starts this week, when there are any; else
  // its source's record.
  const Impl& m = *impl_;
  time::Seconds now = time::now();
  auto failed = m.failedAt.find(channelId);
  if (failed != m.failedAt.end() && now - failed->second < kDirectFailedFor) return 0;
  auto t = m.catalog->traits.find(channelId);
  if (t != m.catalog->traits.end() && t->second.familyKey) {
    auto g = m.groupStarts.find(*t->second.familyKey);
    if (g != m.groupStarts.end() && now - g->second.at <= kGroupStartsFor && g->second.worked + g->second.failed > 0)
      return static_cast<double>(g->second.worked + 1) / static_cast<double>(g->second.worked + g->second.failed + 2);
  }
  return reliabilityOf(channelId);
}

namespace {
// How copies of one kind start: by the provider's source tag ("(s)", "us")
// within one account, since two providers' tags (or none) say nothing
// about each other.
std::string reliabilityKey(const std::string& channelId, const std::string& tag) {
  size_t bar = channelId.find('|');
  return bar == std::string::npos ? tag : tag + "@" + channelId.substr(0, bar);
}
}  // namespace

double Store::reliabilityOf(const std::string& channelId) const {
  const Impl& m = *impl_;
  auto t = m.catalog->traits.find(channelId);
  std::string tag = t == m.catalog->traits.end() ? "" : t->second.sourceTag;
  auto r = m.reliability.find(reliabilityKey(channelId, tag));
  int ok = r == m.reliability.end() || r->second.size() < 2 ? 0 : r->second[0];
  int bad = r == m.reliability.end() || r->second.size() < 2 ? 0 : r->second[1];
  return static_cast<double>(ok + 1) / static_cast<double>(ok + bad + 2);
}

std::vector<std::string> Store::versionsOf(const std::string& id) const {
  // An entry's own versions, best first, the pinned (or last played) one
  // first; a raw
  // channel alone (sports::store::baseVersions, LiveTVStore.baseVersions).
  const Impl& m = *impl_;
  auto info = m.entryInfo.find(id);
  const EntryInfo* ei = info == m.entryInfo.end() ? nullptr : &m.catalog->entries[info->second];
  // First: the pinned version, else the one that played last. Marigold: a
  // server-opened version (Emby / Jellyfin, about 10 s to a picture) that
  // played last goes first only when the direct versions (about 2.6 s)
  // failed lately; otherwise it leads the server-opened ones.
  auto pin = m.pinned.find(id);
  auto last = m.lastFeed.find(id);
  std::optional<std::string> pinned;
  std::optional<std::string> leadServer;
  time::Seconds now = time::now();
  auto healthyDirect = [&] {
    if (!ei) return false;
    for (const std::string& c : ei->members) {
      if (!m.indexByID.count(c) || serverOpened(c)) continue;
      auto failed = m.failedAt.find(c);
      if (failed == m.failedAt.end() || now - failed->second >= kDirectFailedFor) return true;
    }
    return false;
  };
  if (pin != m.pinned.end()) pinned = pin->second;
  else if (last != m.lastFeed.end() && serverOpened(last->second) && healthyDirect()) leadServer = last->second;
  else if (last != m.lastFeed.end()) pinned = last->second;
  std::vector<std::string> out = sports::store::baseVersions(
      id, ei, pinned, [this](const std::string& c) { return reliabilityOf(c); },
      [&m](const std::string& c) { return m.indexByID.count(c) > 0; });
  // Marigold (Build 136): the catalog lists direct versions first, but the
  // record of a source can move a server-opened one ahead of them; while a
  // direct version hasn't failed lately they stay ahead (the pinned or last
  // played version still leads).
  if (healthyDirect()) {
    size_t keep = pinned && !out.empty() && out.front() == *pinned ? 1 : 0;
    sports::store::directVersionsFirst(out, keep, [this](const std::string& c) { return serverOpened(c); });
  }
  if (leadServer)
    sports::store::leadServerVersions(out, *leadServer, [this](const std::string& c) { return serverOpened(c); });
  lastIfUnplayable(out, pin != m.pinned.end() ? std::optional<std::string>(pin->second) : std::nullopt);
  return out;
}

bool Store::serverOpened(const std::string& channelId) const {
  const Impl& m = *impl_;
  auto ci = m.indexByID.find(channelId);
  if (ci == m.indexByID.end()) return false;
  const std::string& server = (*m.channels)[ci->second].serverId;
  for (const sources::Account& a : m.accounts)
    if (a.sourceId() == server) return a.kind == sources::AccountKind::Emby;
  return false;
}

std::vector<Feed> Store::feeds(const std::string& entryId) const {
  if (isEvent(entryId)) return eventFeeds(entryId, "");
  std::vector<std::string> members = versionsOf(entryId);
  // A game on now on this channel: its provider feeds back it up.
  for (const std::string& f : eventBackups(entryId))
    if (std::find(members.begin(), members.end(), f) == members.end()) members.push_back(f);
  return feedsFor(members);
}

std::vector<Feed> Store::feedsFor(const std::vector<std::string>& members) const {
  const Impl& m = *impl_;
  std::vector<Feed> out;
  std::shared_ptr<const StreamLinks::Map> links;  // M3U links, once one is needed
  for (const std::string& id : members) {
    auto ci = m.indexByID.find(id);
    if (ci == m.indexByID.end()) continue;
    const Channel& c = (*m.channels)[ci->second];
    const sources::Account* account = nullptr;
    for (const sources::Account& a : m.accounts)
      if (a.sourceId() == c.serverId) account = &a;
    if (!account) continue;
    Feed f;
    f.channelId = id;
    // "FHD · West feed · provider · group" (feedSummary).
    std::vector<std::string> parts;
    auto t = m.catalog->traits.find(id);
    // Build 136: the picture measured on it, else what its name or group
    // claims; nothing for an unmarked feed (it used to say "HD").
    f.quality = qualityText(id);
    if (!f.quality.empty()) parts.push_back(f.quality);
    if (t != m.catalog->traits.end() && t->second.isWest) parts.push_back("West feed");
    parts.push_back(redact(account->displayName));
    if (c.group) parts.push_back(redact(*c.group));
    f.label = str::join(parts, " \xc2\xb7 ");
    switch (account->kind) {
      case sources::AccountKind::Xtream: {
        auto client = sources::xtream::Client::make(*account);
        if (!client) continue;
        auto run = m.runs.find(account->sourceId());
        std::vector<std::string> formats = run == m.runs.end() ? std::vector<std::string>() : run->second.formats;
        auto url = client.value().streamURL(c.itemId, formats, settings_.preferTS);
        if (!url) continue;
        f.url = *url;
        f.loadMode = 2;  // lean live payload (Legacy #158)
        break;
      }
      case sources::AccountKind::M3U: {
        if (!links) links = currentLinks(*m.links, host_.files);
        auto s = links->find(id);
        if (s == links->end() || s->second.url.empty()) continue;
        f.url = s->second.url;
        f.headers = str::join(s->second.headers, "\r\n");
        f.loadMode = 2;
        break;
      }
      case sources::AccountKind::Emby:
        f.needsOpen = true;
        break;
    }
    out.push_back(std::move(f));
  }
  return out;
}

void Store::open(const Feed& feed, std::function<void(std::optional<Feed>)> done) {
  // Emby / Jellyfin: the server opens the stream (PlaybackInfo) first.
  const Impl& m = *impl_;
  auto ci = m.indexByID.find(feed.channelId);
  if (ci == m.indexByID.end()) {
    done(std::nullopt);
    return;
  }
  const Channel& c = (*m.channels)[ci->second];
  const sources::Account* account = nullptr;
  for (const sources::Account& a : m.accounts)
    if (a.sourceId() == c.serverId) account = &a;
  if (!account) {
    done(std::nullopt);
    return;
  }
  auto client = sources::emby::Client::make(*account);
  if (!client) {
    done(std::nullopt);
    return;
  }
  auto cl = std::make_shared<sources::emby::Client>(client.take());
  std::string item = c.itemId;
  auto stream = std::make_shared<std::optional<sources::emby::Stream>>();
  http::Request q = toHttp(cl->openStreamRequest(item), "emby.open", http::Priority::Playback);
  q.afterTransfer = [cl, item, stream](http::Response& r) {
    auto s = cl->parseOpenStream(r.status, r.body, item);
    if (s) *stream = s.take();
    else MLOG_W("play", "open failed: %s", s.error().message.c_str());
    r.body.clear();
  };
  Feed base = feed;
  closeStream(true);
  host_.pool.submit(std::move(q), [this, cl, stream, base, done](http::Response&&) {
    if (!*stream) {
      done(std::nullopt);
      return;
    }
    Feed f = base;
    f.url = (*stream)->url;
    f.needsOpen = false;
    if (settings_.embyQuickStart) f.loadMode = 2;  // the lean live load (webOS)
    openStream_ = std::make_shared<OpenStream>(OpenStream{cl, **stream});
    host_.pool.submit(toHttp(cl->reportPlayingRequest(**stream), "emby.playing"), nullptr);
    done(f);
  });
}

void Store::quickStartRefused(const std::string& channelId) {
  Impl& m = *impl_;
  auto ci = m.indexByID.find(channelId);
  if (ci == m.indexByID.end()) return;
  if (m.quickStartRefused.insert((*m.channels)[ci->second].serverId).second) saveListsLater();
}

void Store::captionsSeen(const std::string& channelId) {
  if (!channelId.empty() && impl_->captionChannels.insert(channelId).second) saveListsLater();
}

bool Store::captionsKnown(const std::string& channelId) const { return impl_->captionChannels.count(channelId) > 0; }

bool Store::quickStartRefusedBy(const std::string& channelId) const {
  const Impl& m = *impl_;
  auto ci = m.indexByID.find(channelId);
  return ci != m.indexByID.end() && m.quickStartRefused.count((*m.channels)[ci->second].serverId) > 0;
}

std::optional<Feed> Store::quickOpen(const Feed& feed) {
  const Impl& m = *impl_;
  auto ci = m.indexByID.find(feed.channelId);
  if (ci == m.indexByID.end()) return std::nullopt;
  const Channel& c = (*m.channels)[ci->second];
  for (const sources::Account& a : m.accounts) {
    if (a.sourceId() != c.serverId) continue;
    auto client = sources::emby::Client::make(a);
    if (!client) return std::nullopt;
    closeStream(true);
    Feed f = feed;
    f.url = client.value().quickStreamURL(c.itemId);
    f.needsOpen = false;
    f.loadMode = 2;  // the lean live load (webOS)
    return f;
  }
  return std::nullopt;
}

void Store::closeStream(bool first) {
  if (!openStream_) return;
  // Each step best-effort, in order: report stopped, stop transcode, close.
  auto os = openStream_;
  openStream_.reset();
  std::vector<sources::Request> steps = os->client->closeStreamRequests(os->stream);
  http::Pool& pool = host_.pool;
  if (first && os->stream.liveStreamId && !steps.empty()) {
    // The live stream itself first, on the playback lane (it runs before the
    // next open), briefly: a provider allowing one connection can then hand
    // it to the next channel. The reports follow in the background.
    http::Request close = toHttp(steps.back(), "emby.close", http::Priority::Playback);
    close.timeoutMs = 1500;
    pool.submit(std::move(close), nullptr);
    steps.pop_back();
  }
  pool.post(
      [steps, &pool] {
        for (const sources::Request& r : steps) pool.fetchNow(toHttp(r, "emby.close", http::Priority::Background));
      },
      nullptr, http::Priority::Background);
}

void Store::recordFeed(const std::string& channelId, bool worked) {
  Impl& m = *impl_;
  if (worked) {
    m.unplayable.erase(channelId);
    m.failedAt.erase(channelId);
  } else {
    m.failedAt[channelId] = time::now();
  }
  auto t = m.catalog->traits.find(channelId);
  std::string tag = t == m.catalog->traits.end() ? "" : t->second.sourceTag;
  auto& counts = m.reliability[reliabilityKey(channelId, tag)];
  if (counts.size() < 2) counts = {0, 0};
  ++counts[worked ? 0 : 1];
  if (t != m.catalog->traits.end() && t->second.familyKey) {
    Impl::GroupStarts& g = m.groupStarts[*t->second.familyKey];
    time::Seconds now = time::now();
    if (now - g.at > kGroupStartsFor) g = {};
    ++(worked ? g.worked : g.failed);
    g.at = now;
  }
  saveListsLater();
}

bool Store::groupKeepsFailing(const std::string& familyKey) const {
  auto it = impl_->groupStarts.find(familyKey);
  if (it == impl_->groupStarts.end() || time::now() - it->second.at > kGroupStartsFor) return false;
  return it->second.failed >= 3 && it->second.failed > 2 * it->second.worked;
}

std::string Store::groupStartsText(const std::string& familyKey) const {
  auto it = impl_->groupStarts.find(familyKey);
  if (it == impl_->groupStarts.end() || time::now() - it->second.at > kGroupStartsFor) return {};
  const Impl::GroupStarts& g = it->second;
  return std::to_string(g.worked) + " of " + std::to_string(g.worked + g.failed) + " started";
}

void Store::markUnplayable(const std::string& channelId) {
  impl_->unplayable[channelId] = time::now();
  saveListsLater();
}

void Store::markAudioOnly(const std::string& id) {
  if (id.empty()) return;
  impl_->audioOnly[id] = time::now();
  saveListsLater();
}

bool Store::audioOnly(const std::string& id) const {
  auto it = impl_->audioOnly.find(id);
  return it != impl_->audioOnly.end() && time::now() - it->second < kAudioOnlyFor;
}

void Store::lastIfUnplayable(std::vector<std::string>& ids, const std::optional<std::string>& pinned) const {
  const Impl& m = *impl_;
  if (m.unplayable.empty()) return;
  time::Seconds now = time::now();
  auto playable = [&](const std::string& id) {
    if (pinned && id == *pinned) return true;  // the viewer's pick stays first
    auto it = m.unplayable.find(id);
    return it == m.unplayable.end() || now - it->second >= kUnplayableFor;
  };
  std::stable_partition(ids.begin(), ids.end(), playable);
}

void Store::rememberFeed(const std::string& entryId, const std::string& channelId) {
  Impl& m = *impl_;
  // Channels only: a game keeps its own memory (rememberGame).
  if (!m.entryInfo.count(entryId)) return;
  auto it = m.lastFeed.find(entryId);
  if (it != m.lastFeed.end() && it->second == channelId) return;
  m.lastFeed[entryId] = channelId;
  saveListsLater();
}

void Store::pinFeed(const std::string& entryId, const std::string& channelId) {
  if (channelId.empty()) impl_->pinned.erase(entryId);
  else impl_->pinned[entryId] = channelId;
  saveListsLater();
}

std::string Store::pinnedFeed(const std::string& entryId) const {
  auto it = impl_->pinned.find(entryId);
  if (it != impl_->pinned.end()) return it->second;
  // A feed pinned under the row's earlier id.
  for (const catalog::EntryInfo& e : impl_->catalog->entries) {
    if (e.id != entryId) continue;
    for (const std::string& former : e.formerIds) {
      if (auto old = impl_->pinned.find(former); old != impl_->pinned.end()) return old->second;
    }
    break;
  }
  return std::string();
}

// ------------------------------------------------------------ snapshots

bool Store::rowsCurrent() const {
  const Impl& m = *impl_;
  return m.rows && m.rowsFrom.lineup == m.lineupSerial && m.rowsFrom.catalog.lock() == m.catalog &&
         m.rowsFrom.channels.lock() == m.channels;
}

void Store::publishChannels() {
  Impl& m = *impl_;
  auto started = SteadyClock::now();
  // Rows made for another list or lineup (published while the new lineup
  // is on its worker, or after a reset): worked out here, as every publish
  // once was.
  bool full = !rowsCurrent();
  if (full) {
    releaseLater(host_.pool, std::move(m.rows));
    m.rows = makeRows(m.lineup, *m.catalog, *m.channels, m.iptvOrg.get(), &m.entryInfo, &m.indexByID);
    m.rowsFrom = {m.channels, m.catalog, m.lineupSerial};
  }
  m.patching.reset();  // one on its way is out of date
  // Nothing waits for these rows (not the launch's first, nor the progress
  // screen's): the copy and patch go to a worker and tick() puts them in.
  if (!full && !m.readyOnPublish && !channels_->all.empty()) {
    auto job = std::make_shared<Impl::PatchJob>();
    m.patching = job;
    auto flags = std::make_shared<const RowFlagsCopy>(m);
    host_.pool.post(
        [job, flags, rows = m.rows] {
          auto began = SteadyClock::now();
          job->out = patchRows(*rows, flags->flags);
          job->ms = msSince(began);
        },
        [job] { job->done = true; }, http::Priority::Visible);
    return;
  }
  putChannels(patchRows(*m.rows, rowFlags(m)));
  Impl::PublishTiming& t = m.publishTiming;
  ++(full ? t.full : t.patched);
  double& most = full ? t.fullMax : t.patchMax;
  most = std::max(most, msSince(started));
}

void Store::putChannels(std::shared_ptr<Channels> out) {
  Impl& m = *impl_;
  out->version = ++m.publishVersion;
  std::shared_ptr<const Channels> before = std::move(channels_);
  channels_ = out;
  releaseLater(host_.pool, std::move(before));
  // The launch's steps: the first rows on screen (the saved first screen,
  // or a download), then the whole list.
  if (!m.launch.said && !out->all.empty()) {
    if (m.launch.firstRows < 0) m.launch.firstRows = msSince(kProcessStart);
    if (!m.partial) {
      m.launch.said = true;
      MLOG_I("store", "timing launch ready=%.0fms firstRows=%.0fms fullList=%.0fms rows=%zu",
             m.launch.ready, m.launch.firstRows, msSince(kProcessStart), out->all.size());
    }
  }
}

void Store::noteChannelsTiming(uint32_t ms) {
  Impl::PublishTiming& t = impl_->publishTiming;
  if (!t.since) t.since = ms;
  if (ms - t.since < kTimingLineMs) return;
  if (t.full || t.patched || t.patchedLater || t.made)
    MLOG_I("store",
           "timing publishes full=%d fullMax=%.1fms patched=%d patchMax=%.1fms onWorker=%d workerMax=%.1fms "
           "rowsMade=%d rowsMax=%.0fms lineupMax=%.0fms",
           t.full, t.fullMax, t.patched, t.patchMax, t.patchedLater, t.laterMax, t.made, t.madeMax, t.lineupMax);
  t = Impl::PublishTiming{};
  t.since = ms;
}

std::shared_ptr<const Channels> Store::channelsNow() const {
  const Impl& m = *impl_;
  auto out = std::make_shared<Channels>();
  out->version = channels_->version;
  out->rawChannels = m.channels->size();
  out->lineupConfigured = m.prefs.configured;
  for (const EntryInfo& e : m.catalog->entries) {
    for (const std::string& former : e.formerIds) out->renamed.emplace(former, e.id);
  }
  const catalog::LineupOutput& lo = m.lineup;
  out->all.reserve(lo.entries.size());
  std::map<catalog::LineupCategory, int> blockNext;
  for (const Channel& e : lo.entries) {
    GuideChannel g;
    g.id = e.id;
    g.logoUrl = e.imageUrl;
    g.radio = e.isRadio;
    auto info = m.entryInfo.find(e.id);
    const EntryInfo* ei = info == m.entryInfo.end() ? nullptr : &m.catalog->entries[info->second];
    catalog::LineupCategory cat = ei ? ei->category : catalog::LineupCategory::entertainment;
    g.section = catalog::genreTitle(cat);
    g.sectionRank = catalog::rank(cat);
    if (cat == catalog::LineupCategory::local)
      if (const std::string* area = lo.areaSection(e.id)) g.section = *area;
    g.name = ei ? ei->name : e.displayName();
    g.group = catalog::groupOf(e);
    if (g.group.empty()) g.group = kOtherGroup;
    g.feeds = ei ? static_cast<int>(ei->members.size()) : 1;
    g.west = catalog::isWestId(e.id);
    if (std::string other = catalog::coastPartnerId(e.id); lo.entryIndex.count(other)) g.partner = other;
    g.inLineup = lo.lineupIDs.count(e.id) > 0;
    g.favorite = favoriteNow(e.id, e.isFavorite, m.favorites, m.unfavorited);
    // Which logo a row tries and how its cell reads (ChannelLogos.spec).
    const Channel* member = nullptr;
    if (ei && !ei->members.empty()) {
      auto ci = m.indexByID.find(ei->members.front());
      if (ci != m.indexByID.end()) member = &(*m.channels)[ci->second];
    }
    logos::LogoSpec spec = logos::spec(
        g.name, ei ? ei->core : std::nullopt, g.west, ei && ei->callsign, ei ? ei->callsign : std::nullopt,
        ei ? ei->virtualNumber : std::nullopt, e.imageUrl, member ? member->epgId : std::nullopt, cat,
        m.iptvOrg.get(), directoryIptvOrg(ei), ei ? logos::referenceIdOf(ei->id) : std::string(),
        directoryLogo(ei));
    g.logoUrl = logos::chain(spec.urls);
    g.logoSources = str::join(spec.sources, ", ");
    g.subtitle = spec.subtitle.value_or(catalog::genreTitle(cat));
    switch (spec.style) {
      case logos::LogoSpec::Style::local:
        g.style = GuideChannel::Style::Local;
        if (!spec.callsign.empty()) g.name = spec.callsign;
        g.subtitle = spec.line;
        break;
      case logos::LogoSpec::Style::logoOnly:
        g.style = g.logoUrl.empty() ? GuideChannel::Style::Named : GuideChannel::Style::LogoOnly;
        break;
      case logos::LogoSpec::Style::named:
        g.style = GuideChannel::Style::Named;
        break;
    }
    // Locals by their broadcast number; the rest by cable-style blocks.
    if (ei && cat == catalog::LineupCategory::local && ei->virtualNumber) {
      g.number = *ei->virtualNumber;
    } else if (g.inLineup || !m.prefs.configured) {
      int& next = blockNext[cat];
      if (next == 0) next = catalog::numberBlock(cat) + (cat == catalog::LineupCategory::local ? 50 : 0);
      g.number = next++;
    }
    out->byId.emplace(g.id, static_cast<int>(out->all.size()));
    out->all.push_back(std::move(g));
  }
  for (const Channel& e : lo.lineupEntries) {
    auto it = out->byId.find(e.id);
    if (it != out->byId.end()) out->lineup.push_back(it->second);
  }
  for (GuideChannel& g : out->all) g.browsable = false;
  for (const Channel& e : lo.browseEntries) {
    auto it = out->byId.find(e.id);
    if (it == out->byId.end()) continue;
    out->all[static_cast<size_t>(it->second)].browsable = true;
    if (m.hiddenGroups.count(out->all[static_cast<size_t>(it->second)].group)) continue;
    out->browse.push_back(it->second);
  }
  out->every.reserve(out->all.size());
  for (size_t i = 0; i < out->all.size(); ++i) {
    out->every.push_back(static_cast<int>(i));
    if (out->all[i].favorite) out->favorites.push_back(static_cast<int>(i));
  }
  for (const std::string& r : m.recents) {
    auto it = out->byId.find(r);
    if (it != out->byId.end()) out->recent.push_back(it->second);
  }
  return out;
}

void Store::publishListings() {
  Impl& m = *impl_;
  auto out = std::make_shared<Listings>();
  bool all = m.allListingsDirty || !listings_;
  if (!all) *out = *listings_;
  out->version = ++m.publishVersion;
  auto convert = [&](const std::string& id) {
    auto p = m.programs.find(id);
    if (p == m.programs.end() || !p->second || p->second->empty()) {
      out->byChannel.erase(id);
    } else {
      auto v = std::make_shared<std::vector<Listing>>(cleanListings(*p->second));
      if (v->empty()) out->byChannel.erase(id);
      else out->byChannel[id] = v;
    }
    auto st = m.guideState.find(id);
    if (st != m.guideState.end() && (st->second == Impl::GuideState::Loaded || st->second == Impl::GuideState::Failed) &&
        !out->byChannel.count(id))
      out->empty[id] = true;
    else
      out->empty.erase(id);
  };
  if (all) {
    out->byChannel.clear();
    out->empty.clear();
    for (const auto& [id, idx] : m.entryInfo) convert(id);
  } else {
    for (const std::string& id : m.dirtyEntries) convert(id);
  }
  m.dirtyEntries.clear();
  m.allListingsDirty = false;
  listings_ = out;
}

time::Seconds Store::channelLifetime() const {
  const Impl& m = *impl_;
  time::Seconds now = time::now();
  if (eventsOpen_ > 0) return kChannelLifetime;
  if (m.eventsUsedAt && now - m.eventsUsedAt < kGamesMatterFor) return kChannelLifetime;
  for (const sports::LiveReminder& r : m.ev.reminders.items())
    if (r.start > now - kGamesMatterFor && r.start < now + kGamesMatterFor) return kChannelLifetime;
  return kChannelLifetimeQuiet;
}

void Store::liveEventsOpened() {
  Impl& m = *impl_;
  m.eventsUsedAt = time::now();
  // New games show up as renamed event slots: a list older than a few
  // minutes is read again (cheap when only names changed).
  if (m.channelsLoadedAt && time::now() - m.channelsLoadedAt > kChannelFreshForEvents && !m.loading &&
      !*m.playingFull && !m.channels->empty())
    load(false);
}

bool Store::tick(uint32_t ms) {
  Impl& m = *impl_;
  releaseMemoryWhenIdle(ms);
  uint64_t eventsVersion = events_->version;
  eventsTick(ms);
  *m.playingFull = host_.playingFull();
  // The saved M3U links, once the first frame is up (the second tick).
  if (!m.linksAsked && ++m.ticks > 1) readLinksLater();
  lookAtGuides(ms);
  // A reload is done (its list sorted, unchanged, renamed or failed): the
  // guides it stopped go on. Each is built only if due or forced.
  if (m.guidesAfterLoad && !m.loading) {
    m.guidesAfterLoad = false;
    if (!m.channels->empty()) {
      scheduleProvider(false);
      scheduleOutside(false);
    }
  }
  if (m.guideFlushAt && ms >= m.guideFlushAt) {
    m.guideFlushAt = 0;
    startGuideFetches();
  }
  if (m.providerAt && ms >= m.providerAt) {
    m.providerAt = 0;
    runProviderGuide();
  }
  if (m.outsideAt && ms >= m.outsideAt && !m.providerRunning) {
    m.outsideAt = 0;
    runOutsideGuide();
  }
  if (m.lineupRefreshAt && ms >= m.lineupRefreshAt) {
    m.lineupRefreshAt = 0;
    rebuildEntries();
  }
  // The half-hour window moved on: re-merge and re-ask the rows looked at
  // recently. The channel list (event names) is re-read every half hour.
  if (!m.channels->empty()) {
    time::Seconds start = windowStart();
    if (m.guideWindowStart && m.guideWindowStart != start) {
      m.guideWindowStart = start;
      rebuildOutsidePrograms();
      // A copy: wantListings reorders recentlyRequested while it reads the
      // ids it was given.
      std::vector<std::string> again = m.recentlyRequested;
      wantListings(again);
    }
    if (m.catalogUnsaved && !m.loading && time::now() - m.catalogSavedAt >= kRenamedSaveEvery) saveCatalog();
    if (m.channelsLoadedAt && time::now() - m.channelsLoadedAt > channelLifetime() && !m.loading && !*m.playingFull) {
      m.channelsLoadedAt = time::now();  // one attempt per lifetime
      load(true);
    }
  }
  bool changed = events_->version != eventsVersion;
  // A patch made on a worker goes in.
  if (m.patching && m.patching->done) {
    std::shared_ptr<Impl::PatchJob> job = std::move(m.patching);
    putChannels(std::move(job->out));
    Impl::PublishTiming& t = m.publishTiming;
    ++t.patchedLater;
    t.laterMax = std::max(t.laterMax, job->ms);
    changed = true;
  }
  if (m.channelsDirty) {
    m.channelsDirty = false;
    publishChannels();
    changed = true;
    if (m.readyOnPublish && m.rowsBuilt && (!m.partial || (channels_ && !channels_->all.empty()))) {
      m.readyOnPublish = false;
      status_.phase = StoreStatus::Phase::Ready;
    }
  }
  noteChannelsTiming(ms);
  if (m.listingsDirty || m.allListingsDirty) {
    m.listingsDirty = false;
    publishListings();
    changed = true;
  }
  // Search's index parts made on a worker go in.
  if (searchTick(ms)) changed = true;
  // Status line while the list updates and guides build: updating, then
  // updated (or couldn't) for a few seconds.
  m.lastTick = ms;
  if (m.loadUpdated) {
    m.loadUpdated = false;
    m.updatedUntil = ms + 3000;
    m.failedUntil = 0;
    m.updatedLine = m.accountChange.empty() ? "Channel list updated" : m.accountChangeDone;
    m.accountChange.clear();
  }
  if (m.loadFailed) {
    m.loadFailed = false;
    m.failedUntil = ms + 4000;
    m.updatedUntil = 0;
  }
  std::string line;
  StoreStatus::Mark mark = StoreStatus::Mark::None;
  if (m.loading && !m.channels->empty()) {
    line = m.accountChange.empty() ? "Updating your channel list\xe2\x80\xa6" : m.accountChange;
    mark = StoreStatus::Mark::Busy;
  } else if (m.updatedUntil && ms < m.updatedUntil) {
    line = m.updatedLine.empty() ? "Channel list updated" : m.updatedLine;
    mark = StoreStatus::Mark::Done;
  } else if (m.failedUntil && ms < m.failedUntil) {
    line = "Couldn't update the channel list \xc2\xb7 tries again later";
    mark = StoreStatus::Mark::Failed;
  } else if (!m.headsUpLine.empty() && status_.phase == StoreStatus::Phase::Ready &&
             (m.headsUpUntil || time::now() - m.headsUpAskedAt < 60)) {
    // A membership's end, once a day: its seconds start once it's up, and
    // the day is kept then (not when it only waited).
    if (!m.headsUpUntil) {
      m.headsUpUntil = std::max<uint32_t>(ms + membership::kHeadsUpMs, 1);
      for (sources::Account& a : m.accounts)
        if (a.id == m.headsUpAccount) a.noticeDay = membership::localDay(time::now(), host_.zone());
      saveAccounts();
    }
    line = m.headsUpLine;
    mark = StoreStatus::Mark::Failed;
  } else if (m.providerRunning || m.outsideRunning) {
    GuideBuildLine g = guideBuildLine(m.providerRunning, m.providerRunning && providerGuideMissing(m),
                                      m.outsideRunning, m.outsideWaiting->load());
    line = std::move(g.line);
    mark = g.mark;
  }
  if (m.updatedUntil && ms >= m.updatedUntil) m.updatedUntil = 0;
  if (m.failedUntil && ms >= m.failedUntil) m.failedUntil = 0;
  // The heads-up's seconds are up (or it waited too long): gone until the
  // next day.
  if (!m.headsUpLine.empty() && ((m.headsUpUntil && ms >= m.headsUpUntil) ||
                                 (!m.headsUpUntil && time::now() - m.headsUpAskedAt >= 60))) {
    m.headsUpLine.clear();
    m.headsUpAccount.clear();
    m.headsUpUntil = 0;
  }
  if (status_.phase == StoreStatus::Phase::Ready && (status_.line != line || status_.mark != mark)) {
    status_.line = line;
    status_.mark = mark;
    changed = true;
  }
  return changed;
}

}  // namespace marigold::engine
