// The store: a port of the parts of LiveTVStore (tvOS) that milestone 1
// needs, on the TV's worker pool. Everything here runs on the UI thread
// except the lambdas handed to pool().post() / afterTransfer / onChunk,
// which only touch what they captured (by value or shared_ptr).
#include "core/engine/store.hpp"
#include "core/engine/store_impl.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <unordered_set>

#include "core/catalog/admission.hpp"
#include "core/catalog/cache.hpp"
#include "core/catalog/catalog.hpp"
#include "core/catalog/lineup.hpp"
#include "core/catalog/markets.hpp"
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
using catalog::EntryInfo;

// LiveTVStore constants.
constexpr time::Seconds kChannelLifetime = 30 * 60;  // channel list re-read
constexpr size_t kProgramBatch = 25;
constexpr int kGuideConcurrency = 4;
constexpr int kShortEpgParallel = 6;
constexpr size_t kRecentMax = 30;
constexpr size_t kRecentlyRequestedMax = 300;
constexpr double kGuideHours = 3.0;
constexpr uint32_t kGuideFlushMs = 120;
constexpr uint32_t kProviderDelayMs = 4000;
constexpr uint32_t kOutsideDelayMs = 8000;
constexpr uint32_t kLineupRefreshMs = 600;
constexpr const char* kOtherGroup = "Other";

constexpr const char* kAccountsFile = "accounts.json";
constexpr const char* kSecretsFile = "accounts-secrets.json";
constexpr const char* kSettingsFile = "settings.json";
constexpr const char* kListsFile = "lists.json";
constexpr const char* kLineupFile = "lineup.json";
constexpr const char* kM3uStreamsFile = "m3u-streams.json";  // secret
constexpr const char* kStationsFile = "stations-v2.json";  // v2: two-city cells, MyNetworkTV
constexpr const char* kLogoIndexFile = "logo-index.json";

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

// One Wikipedia page's wikitext, on a pool thread (station directory, markets).
std::optional<std::string> wikitext(http::Pool& pool, const std::string& title,
                                    http::Priority priority = http::Priority::Visible);

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
  l.overview = p.overview.value_or("");
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

// Writes `data` next to `path` and renames it into place.
bool writeFileAtomic(const std::string& path, const std::string& data, bool secret = false) {
  std::string tmp = path + ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!f) return false;
  }
  if (secret) chmod(tmp.c_str(), 0600);
  return std::rename(tmp.c_str(), path.c_str()) == 0;
}

// Writes a file a piece at a time (`write` sends its chunks to the sink),
// next to `path`, and renames it into place.
bool writeFileStreaming(const std::string& path, const std::function<bool(const json::Writer::Sink&)>& write) {
  std::string tmp = path + ".tmp";
  bool ok;
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    ok = write([&f](std::string_view chunk) {
      f.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
      return static_cast<bool>(f);
    });
    ok = ok && static_cast<bool>(f);
  }
  if (!ok) {
    std::remove(tmp.c_str());
    return false;
  }
  return std::rename(tmp.c_str(), path.c_str()) == 0;
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
  Files& st = host_.files;
  Impl& m = *impl_;

  if (auto main = st.read(kAccountsFile)) {
    auto decoded = sources::decodeAccounts(*main, st.read(kSecretsFile).value_or(""));
    if (decoded) m.accounts = decoded.take();
    else MLOG_W("store", "accounts file unreadable: %s", decoded.error().message.c_str());
  }
  if (auto text = st.read(kSettingsFile)) {
    if (auto v = json::parse(*text)) {
      settings_.previewChannels = (*v)["previewChannels"].asBool(true);
      settings_.allChannelsTab = (*v)["allChannelsTab"].asBool(false);
      settings_.preferTS = (*v)["preferTS"].asBool(true);
      settings_.fillMissingListings = (*v)["fillMissingListings"].asBool(true);
      settings_.scheduleEnabled = (*v)["scheduleEnabled"].asBool(true);
      settings_.showUnwatchable = (*v)["showUnwatchable"].asBool(true);
      settings_.showScores = (*v)["showScores"].asBool(true);
      settings_.clock24 = (*v)["clock24"].asBool(false);
      settings_.playerTipsShown = (*v)["playerTipsShown"].asBool(false);
      settings_.guideShards = stringSet((*v)["guideShards"]);
    }
  }
  if (auto text = st.read(kListsFile)) {
    if (auto v = json::parse(*text)) {
      m.favorites = stringSet((*v)["favorites"]);
      m.hiddenGroups = stringSet((*v)["hiddenGroups"]);
      for (const json::Value& x : (*v)["recents"].items())
        if (x.isString()) m.recents.push_back(x.asString());
      for (const auto& [k, x] : (*v)["pinned"].fields()) m.pinned[k] = x.asString();
      for (const auto& [k, x] : (*v)["reliability"].fields())
        m.reliability[k] = {static_cast<int>(x[size_t(0)].asInt()), static_cast<int>(x[size_t(1)].asInt())};
    }
  }
  if (auto text = st.read(kLineupFile)) {
    if (auto p = catalog::lineupPrefsFromJson(*text)) m.prefs = *p;
  }
  if (auto text = st.read(kM3uStreamsFile)) {
    if (auto v = json::parse(*text)) {
      for (const auto& [id, x] : v->fields()) {
        sources::m3u::StreamInfo info;
        info.url = x["url"].asString();
        for (const json::Value& h : x["headers"].items()) info.headers.push_back(h.asString());
        m.m3uStreams[id] = std::move(info);
      }
    }
  }
  for (sources::Account& a : m.accounts) {
    if (settings_.preferTS) a.preferTS = true;
    m.runs[a.sourceId()];
  }
  readStations();
  readLogoIndex();
  loadEvents();
  MLOG_I("store", "%zu provider(s)", m.accounts.size());
  if (m.accounts.empty()) {
    status_.phase = StoreStatus::Phase::NoAccounts;
  } else {
    status_.phase = StoreStatus::Phase::Loading;
    status_.line = "Loading channels\xe2\x80\xa6";
    load(false);
  }
}

Store::~Store() { *impl_->stopJobs = true; }

void Store::saveAccounts() {
  sources::EncodedAccounts enc = sources::encodeAccounts(impl_->accounts);
  host_.files.write(kAccountsFile, enc.main);
  host_.files.write(kSecretsFile, enc.secrets, true);
}

void Store::saveLists() {
  const Impl& m = *impl_;
  json::Value v = json::Value::object();
  v.set("favorites", stringArray(std::vector<std::string>(m.favorites.begin(), m.favorites.end())));
  v.set("recents", stringArray(m.recents));
  v.set("hiddenGroups", stringArray(std::vector<std::string>(m.hiddenGroups.begin(), m.hiddenGroups.end())));
  json::Value pinned = json::Value::object();
  for (const auto& [k, x] : m.pinned) pinned.set(k, x);
  v.set("pinned", pinned);
  json::Value rel = json::Value::object();
  for (const auto& [k, x] : m.reliability) {
    json::Value pair = json::Value::array();
    pair.push(x.size() > 0 ? x[0] : 0);
    pair.push(x.size() > 1 ? x[1] : 0);
    rel.set(k, pair);
  }
  v.set("reliability", rel);
  host_.files.write(kListsFile, json::write(v));
}

void Store::saveM3uStreams() {
  json::Value v = json::Value::object();
  for (const auto& [id, info] : impl_->m3uStreams) {
    json::Value x = json::Value::object();
    x.set("url", info.url);
    json::Value h = json::Value::array();
    for (const std::string& line : info.headers) h.push(line);
    x.set("headers", h);
    v.set(id, x);
  }
  std::string text = json::write(v);
  std::string path = host_.files.path(kM3uStreamsFile);
  host_.pool.post([path, text] { writeFileAtomic(path, text, true); }, nullptr, http::Priority::Background);
}

void Store::setSettings(const Settings& s) {
  bool tsChanged = s.preferTS != settings_.preferTS;
  bool fillChanged = s.fillMissingListings != settings_.fillMissingListings;
  bool tabChanged = s.allChannelsTab != settings_.allChannelsTab;
  settings_ = s;
  json::Value v = json::Value::object();
  v.set("previewChannels", s.previewChannels);
  v.set("allChannelsTab", s.allChannelsTab);
  v.set("preferTS", s.preferTS);
  v.set("fillMissingListings", s.fillMissingListings);
  v.set("scheduleEnabled", s.scheduleEnabled);
  v.set("showUnwatchable", s.showUnwatchable);
  v.set("showScores", s.showScores);
  v.set("clock24", s.clock24);
  v.set("playerTipsShown", s.playerTipsShown);
  if (!s.guideShards.empty())
    v.set("guideShards", stringArray(std::vector<std::string>(s.guideShards.begin(), s.guideShards.end())));
  impl_->ev.boardDirty = true;
  host_.files.write(kSettingsFile, json::write(v, true));
  if (tsChanged) {
    for (sources::Account& a : impl_->accounts) a.preferTS = s.preferTS;
  }
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
  impl_->prefs = p;
  host_.files.write(kLineupFile, catalog::toJson(p));
  if (countriesChanged && impl_->admissionCut) {
    // Which groups are kept depends on the countries: read the channel
    // lists again, and sort them even if they haven't changed.
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
      sources::Request req = sources::emby::authenticateRequest(a.serverUrl, a.userName, form.password, a.allowInsecureTLS);
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
  for (auto s = m.m3uStreams.begin(); s != m.m3uStreams.end();) {
    if (str::startsWith(s->first, source + "|")) s = m.m3uStreams.erase(s);
    else ++s;
  }
  saveAccounts();
  saveM3uStreams();
  refreshAll();
}

// ------------------------------------------------------------ loading

std::string Store::currentSignature() const {
  std::string s = "default";
  for (const sources::Account& a : impl_->accounts) s += "|" + a.sourceId();
  return s;
}

void Store::refreshAll() {
  Impl& m = *impl_;
  ++m.generation;
  host_.pool.cancelAll();
  if (host_.requestsCancelled) host_.requestsCancelled();
  m.loading = false;
  m.activeGuideFetches = 0;
  m.providerRunning = m.outsideRunning = false;
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
  m.entryInfo.clear();
  m.lineup = catalog::LineupOutput{};
  m.programs.clear();
  m.serverPrograms.clear();
  m.providerMatches.clear();
  m.outsideMatches.clear();
  m.guideState.clear();
  m.channelsDirty = m.allListingsDirty = m.listingsDirty = true;
  status_ = StoreStatus{};
  status_.phase = m.accounts.empty() ? StoreStatus::Phase::NoAccounts : StoreStatus::Phase::Loading;
}

void Store::load(bool force) {
  Impl& m = *impl_;
  std::string signature = currentSignature();
  if (signature != m.signature) {
    reset();
    m.signature = signature;
    m.savedListingsRead = false;
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
    host_.pool.post(
        [path, saved, signature, foreground] {
          Foreground busy(foreground);
          std::ifstream in(path, std::ios::binary);
          if (in) *saved = catalog::cache::readSaved(in, signature);
        },
        [this, gen, saved, fullPath, signature] {
          if (gen != impl_->generation) return;
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
  if (!partial) m.listFingerprint = saved.fingerprint;
  installCatalog(std::make_shared<std::vector<Channel>>(std::move(saved.channels)),
                 std::make_shared<CatalogResult>(std::move(saved.catalog)), partial);
  status_.phase = StoreStatus::Phase::Ready;
  startGuides(true);
}



void Store::downloadChannels(uint64_t gen) {
  Impl& m = *impl_;
  size_t n = m.accounts.size();
  auto parts = std::make_shared<std::vector<Fetched>>(n);
  auto remaining = std::make_shared<size_t>(n);
  status_.line = "Loading channels from " + std::to_string(n) + (n == 1 ? " provider\xe2\x80\xa6" : " providers\xe2\x80\xa6");
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
        // Account info (formats, expiry) alongside the list.
        auto infoBox = std::make_shared<std::optional<sources::xtream::AccountInfo>>();
        host_.pool.submit(toHttp(c->accountInfoRequest(), "xtream.info"), [c, infoBox, slot, this, gen](http::Response&& r) {
          if (gen != impl_->generation) return;
          auto info = c->parseAccountInfo(r.status, r.body);
          if (info) slot->formats = info.value().formats;
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
  for (size_t i = 0; i < parts->size() && i < m.accounts.size(); ++i) {
    Fetched& f = (*parts)[i];
    sources::Account& a = m.accounts[i];
    Impl::AccountRun& run = m.runs[a.sourceId()];
    if (!f.formats.empty()) run.formats = f.formats;
    if (!f.problem.empty()) {
      problems.push_back(f.problem);
      run.state = "Couldn't load channels";
    } else {
      run.state = std::to_string(f.channels.size()) + " channels";
      a.lastChannelCount = static_cast<int>(f.channels.size());
    }
    total += f.channels.size();
    if (a.kind == sources::AccountKind::M3U && f.problem.empty()) {
      std::string prefix = a.sourceId() + "|";
      for (auto s = m.m3uStreams.begin(); s != m.m3uStreams.end();) {
        if (str::startsWith(s->first, prefix)) s = m.m3uStreams.erase(s);
        else ++s;
      }
      for (auto& [id, info] : f.streams) m.m3uStreams[id] = std::move(info);
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
  if (std::any_of(m.accounts.begin(), m.accounts.end(), [](const sources::Account& a) { return a.kind == sources::AccountKind::M3U; }))
    saveM3uStreams();

  if (total == 0) {
    m.loading = false;
    m.channelsLoadedAt = time::now();
    if (m.channels->empty()) {
      status_.phase = problems.empty() ? StoreStatus::Phase::Ready : StoreStatus::Phase::Failed;
      status_.problem = str::join(problems, "\n");
      status_.line.clear();
    }
    return;
  }
  // Merge, admit whole groups up to the cap, then build the catalog on a
  // worker (or refresh only Live Events when just event slots were renamed).
  auto merged = std::make_shared<std::vector<Channel>>();
  merged->reserve(total);
  for (Fetched& f : *parts) {
    for (Channel& c : f.channels) merged->push_back(std::move(c));
    f.channels.clear();
    f.channels.shrink_to_fit();
  }
  // Local favourites (Xtream and M3U have no favorites API).
  bool showing = !m.channels->empty() && !m.partial;
  auto old = showing ? m.channels : nullptr;
  auto oldCatalog = showing ? m.catalog : nullptr;
  auto result = std::make_shared<std::pair<std::shared_ptr<std::vector<Channel>>, std::shared_ptr<CatalogResult>>>();
  auto renamedCount = std::make_shared<long>(-1);
  status_.line = "Sorting " + std::to_string(total) + " channels\xe2\x80\xa6";
  time::Seconds now = time::now();
  auto stations = m.stations;
  std::set<std::string> wanted = m.prefs.guideCountries();  // "US" for US English and US Spanish
  auto cut = std::make_shared<bool>(false);
  uint64_t lastPrint = showing && !m.admissionChanged ? m.listFingerprint : 0;
  auto print = std::make_shared<uint64_t>(0);
  auto unchanged = std::make_shared<bool>(false);
  host_.pool.post(
      [merged, old, oldCatalog, result, renamedCount, now, stations, wanted, cut, lastPrint, print, unchanged,
       foreground = m.foregroundJobs] {
        Foreground busy(foreground);
        std::vector<Channel> list = std::move(*merged);
        // The same download (and countries) as the list on screen, from
        // this run or saved by the last one: nothing to sort again.
        *print = catalog::fingerprint(list, wanted);
        if (lastPrint && *print == lastPrint) {
          *unchanged = true;
          return;
        }
        // With countries chosen, another country's groups are kept only for
        // sports and events (Live Events finds games on them); the rest is
        // left out. Over the cap: the viewer's countries first, then
        // unmarked groups ("United States", "4K|", event groups), then those
        // sports groups. Provider order alone kept Europe and cut every US
        // group.
        if (!wanted.empty() || list.size() > catalog::kMaxChannels) {
          catalog::Admission adm = catalog::admitGroups(list, catalog::kMaxChannels, catalog::countryRank(wanted));
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
        *result = {annotated, std::make_shared<CatalogResult>(catalog::build(*annotated, now, *stations))};
      },
      [this, gen, result, renamedCount, showing, problems, cut, print, unchanged] {
        if (gen != impl_->generation) return;
        Impl& mm = *impl_;
        mm.loading = false;
        mm.channelsLoadedAt = time::now();
        mm.listFingerprint = *print;
        if (*unchanged) {
          MLOG_I("store", "refresh: the channel list is unchanged");
          status_.problem = str::join(problems, "\n");
          status_.line.clear();
          if (!mm.stationsFresh) fetchStations();
          fetchLogoIndex();
          afterBigJob("channel list checked");
          return;
        }
        mm.admissionChanged = false;
        mm.admissionCut = *cut;
        bool wasEmpty = mm.channels->empty();
        installCatalog(result->first, result->second, false);
        status_.phase = StoreStatus::Phase::Ready;
        status_.problem = str::join(problems, "\n");
        status_.line.clear();
        if (*renamedCount >= 0) MLOG_I("store", "refresh: only %ld event names changed", *renamedCount);
        else MLOG_I("store", "built the channel list: %zu channels, %zu entries", mm.channels->size(), mm.catalog->entries.size());
        if (showing) {
          scheduleProvider(false);
          rebuildOutsidePrograms();
        } else {
          startGuides(wasEmpty);
        }
        saveCatalog();
        if (!mm.stationsFresh) fetchStations();
        fetchLogoIndex();
        afterBigJob("channels sorted");
      },
      showing ? http::Priority::Background : http::Priority::Visible);
}

// ------------------------------------------------------------ station directory

void Store::readStations() {
  // Small (a few thousand stations): read before the first catalog build.
  Files& st = host_.files;
  std::string path = st.path(kStationsFile);
  struct stat sb;
  if (stat(path.c_str(), &sb) != 0) return;
  auto text = st.read(kStationsFile);
  if (!text) return;
  auto list = guide::stations::fromJson(*text);
  if (!list) return;
  impl_->stationsFresh = guide::stations::isFresh(sb.st_mtime, time::now(), list->empty());
  impl_->stations = std::make_shared<std::vector<guide::Station>>(std::move(*list));
  MLOG_I("store", "station directory: %zu stations%s", impl_->stations->size(), impl_->stationsFresh ? "" : " (old)");
}

void Store::fetchStations() {
  Impl& m = *impl_;
  time::Seconds now = time::now();
  if (m.stationsFetching || m.stationsFresh || now - m.stationsTriedAt < 6 * 3600) return;
  m.stationsFetching = true;
  m.stationsTriedAt = now;
  auto out = std::make_shared<std::vector<guide::Station>>();
  http::Pool& pool = host_.pool;
  std::string path = host_.files.path(kStationsFile);
  auto stop = m.stopJobs;
  host_.pool.post(
      [out, &pool, path, stop] {
        std::vector<std::pair<std::string, std::string>> texts;
        bool first = true;
        for (const guide::stations::Page& page : guide::stations::pages()) {
          for (const std::string& title : page.titles) {
            if (stop->load()) return;
            if (!first) usleep(guide::stations::kRequestSpacingMs * 1000);
            first = false;
            if (auto text = wikitext(pool, title, http::Priority::Background)) {
              texts.push_back({page.network, std::move(*text)});
              break;
            }
          }
        }
        *out = guide::stations::collect(texts);
        if (!out->empty()) writeFileAtomic(path, guide::stations::toJson(*out));
      },
      [this, out] {
        Impl& mm = *impl_;
        mm.stationsFetching = false;
        if (out->empty()) {
          MLOG_W("store", "station directory: couldn't read the affiliate lists");
          return;
        }
        bool changed = *out != *mm.stations;
        mm.stationsFresh = true;
        mm.stations = out;
        MLOG_I("store", "station directory: %zu stations", out->size());
        if (!changed) return;
        rebuildCatalog();
        if (guide::needsStationDirectory(guideShards())) scheduleOutside(true);
      },
      http::Priority::Long);
}

// ------------------------------------------------------------ fallback logos

void Store::readLogoIndex() {
  std::string path = host_.files.path(kLogoIndexFile);
  auto box = std::make_shared<std::optional<logos::IptvOrgIndex::Saved>>();
  host_.pool.post(
      [path, box] {
        std::string text;
        if (readFile(path, text)) *box = logos::IptvOrgIndex::fromJson(text);
      },
      [this, box] {
        if (!*box || impl_->iptvOrg) return;
        impl_->iptvOrg = std::make_shared<logos::IptvOrgIndex>(std::move(**box));
        impl_->channelsDirty = true;
      },
      http::Priority::Normal);
}

void Store::fetchLogoIndex() {
  Impl& m = *impl_;
  time::Seconds now = time::now();
  if (m.iptvOrgFetching || now - m.iptvOrgTriedAt < 6 * 3600) return;
  if (m.iptvOrg && !m.iptvOrg->needsRefresh(now)) return;
  m.iptvOrgFetching = true;
  m.iptvOrgTriedAt = now;
  auto out = std::make_shared<std::optional<logos::IptvOrgIndex::Saved>>();
  http::Pool& pool = host_.pool;
  std::string path = host_.files.path(kLogoIndexFile);
  GuideReadStateLite st{m.playingFull, m.stopJobs};
  host_.pool.post(
      [out, &pool, path, st] {
        auto get = [&pool, st](const char* file) -> std::optional<std::string> {
          for (int i = 0; i < 600 && st.playing->load() && !st.stop->load(); ++i) usleep(500000);
          http::Request q;
          q.url = std::string(logos::IptvOrgIndex::kBase) + file;
          q.tag = "logo.index";
          q.timeoutMs = 120000;
          q.maxBytes = 48u << 20;
          q.follow = true;
          q.priority = http::Priority::Background;
          http::Response r = pool.fetchNow(std::move(q));
          if (!r.ok || r.status != 200) return std::nullopt;
          return std::move(r.body);
        };
        auto channels = get(logos::IptvOrgIndex::kChannelsFile);
        if (!channels || st.stop->load()) return;
        auto logoList = get(logos::IptvOrgIndex::kLogosFile);
        if (!logoList) return;
        *out = logos::IptvOrgIndex::build(*channels, *logoList, time::now());
        if (*out) writeFileAtomic(path, logos::IptvOrgIndex::toJson(**out));
      },
      [this, out] {
        Impl& mm = *impl_;
        mm.iptvOrgFetching = false;
        if (!*out) {
          MLOG_W("store", "fallback logos: couldn't load the index");
          return;
        }
        MLOG_I("store", "fallback logos: %zu by id, %zu by name", (*out)->byID.size(), (*out)->byName.size());
        mm.iptvOrg = std::make_shared<logos::IptvOrgIndex>(std::move(**out));
        mm.channelsDirty = true;
      },
      http::Priority::Long);
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
  host_.pool.post([list, stations, result, now] { *result = std::make_shared<CatalogResult>(catalog::build(*list, now, *stations)); },
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
  ++m.catalogRevision;
  rebuildEntries();
}

void Store::saveCatalog() {
  Impl& m = *impl_;
  if (m.partial || m.channels->empty()) return;
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
      [job, first, families, firstPath, fullPath] {
        job->ref.channels = job->channels.get();
        job->ref.catalog = job->catalog.get();
        job->ref.xtreamFormats = &job->formats;
        int64_t partBytes = 0, fullBytes = 0;
        {
          catalog::cache::Saved part = catalog::cache::firstPart(job->ref, first, families);
          part.fingerprint = 0;  // only the full list can skip a sort
          writeFileStreaming(firstPath, [&](const json::Writer::Sink& sink) {
            return catalog::cache::writeSaved(catalog::cache::ref(part), [&](std::string_view c) {
              partBytes += static_cast<int64_t>(c.size());
              return sink(c);
            });
          });
        }
        writeFileStreaming(fullPath, [&](const json::Writer::Sink& sink) {
          return catalog::cache::writeSaved(job->ref, [&](std::string_view c) {
            fullBytes += static_cast<int64_t>(c.size());
            return sink(c);
          });
        });
        MLOG_I("store", "saved channel list: first screen %s, full %s", catalog::sizeText(partBytes).c_str(),
               catalog::sizeText(fullBytes).c_str());
      },
      nullptr, http::Priority::Background);
}

// ------------------------------------------------------------ My Lineup

void Store::rebuildEntries() {
  Impl& m = *impl_;
  uint64_t generation = ++m.rebuildGeneration;
  auto input = std::make_shared<catalog::LineupInput>();
  input->infos = m.catalog->entries;
  input->channels = *m.channels;
  input->indexByID = m.indexByID;
  input->prefs = m.prefs;
  input->otherGroup = kOtherGroup;
  for (const auto& [id, list] : m.outsideMatches) input->guideIDs.insert(id);
  for (const auto& [id, list] : m.providerMatches) input->guideIDs.insert(id);
  for (const auto& [id, list] : m.serverPrograms)
    if (!list.empty()) input->guideIDs.insert(id);
  // Local favourites count as favourites for the lineup rules.
  for (Channel& c : input->channels) {
    if (!str::startsWith(c.serverId, "jf:")) c.isFavorite = false;
  }
  for (const std::string& fav : m.favorites) {
    auto it = m.entryInfo.find(fav);
    if (it == m.entryInfo.end()) continue;
    for (const std::string& member : m.catalog->entries[it->second].members) {
      auto ci = m.indexByID.find(member);
      if (ci != m.indexByID.end()) input->channels[ci->second].isFavorite = true;
    }
  }
  auto out = std::make_shared<catalog::LineupOutput>();
  uint64_t gen = m.generation;
  host_.pool.post([input, out] { *out = catalog::lineup(*input); },
                   [this, gen, generation, out] {
                     if (gen != impl_->generation || generation != impl_->rebuildGeneration) return;
                     impl_->lineup = std::move(*out);
                     ++impl_->lineupRevision;
                     impl_->channelsDirty = true;
                     // Entry guides from whatever member data is already here.
                     std::vector<std::string> ids;
                     ids.reserve(impl_->entryInfo.size());
                     for (const auto& [id, idx] : impl_->entryInfo) ids.push_back(id);
                     applyEntryGuides(ids);
                   },
                   http::Priority::Visible);
}

catalog::LineupPrefs Store::lineupPrefs() const { return impl_->prefs; }

namespace {

// One Wikipedia page's wikitext, on a pool thread. Wikimedia answers 429 to
// clients whose User-Agent has no contact address (a URL or an email), and
// to bursts: the agent names the project's page, and a 429 waits as long as
// the server asks (or 3, 8, then 15 s) before trying again.
std::optional<std::string> wikitext(http::Pool& pool, const std::string& title, http::Priority priority) {
  http::Request q;
  q.url = guide::stations::wikiRequestUrl(title);
  q.headers.push_back("User-Agent: " + std::string(guide::stations::kUserAgent));
  q.tag = "wiki";
  q.timeoutMs = 60000;
  q.maxBytes = 8u << 20;
  q.priority = priority;
  static const unsigned kBackoffS[] = {3, 8, 15};
  for (int attempt = 0; attempt < 4; ++attempt) {
    http::Response r = pool.fetchNow(q);
    if (r.status == 429 || r.status == 503) {
      if (attempt == 3) {
        MLOG_W("wiki", "page request rate limited (status %d)", r.status);
        break;
      }
      unsigned wait = std::max<unsigned>(kBackoffS[attempt], static_cast<unsigned>(std::clamp(r.retryAfterS, 0, 30)));
      usleep(wait * 1000000u);
      continue;
    }
    if (!r.ok) {
      MLOG_W("wiki", "page request failed (status %d, %s)", r.status, r.error.c_str());
      if (attempt < 2) {
        usleep(2000000u);
        continue;
      }
      break;
    }
    auto text = guide::stations::wikitextFromResponse(r.body);
    if (!text) MLOG_W("wiki", "page answer unreadable (status %d, %zu bytes)", r.status, r.body.size());
    return text;
  }
  return std::nullopt;
}

}  // namespace

void Store::loadMarkets(std::function<void(TvMarkets)> done) {
  Impl& m = *impl_;
  std::string path = host_.files.path("markets-v2.json");  // v2: US markets only
  // Names the market suggestion reads: local-looking channels first (tvOS).
  auto names = std::make_shared<std::vector<std::string>>();
  for (const Channel& c : *m.channels) {
    std::string group = str::lowerAscii(c.group.value_or(""));
    if (group.find("local") != std::string::npos || guide::bareCallsign(c.name)) names->push_back(c.name);
  }
  if (names->empty()) {
    for (const Channel& c : *m.channels) names->push_back(c.name);
  }
  if (names->size() > 6000) names->resize(6000);
  auto result = std::make_shared<TvMarkets>();
  auto list = std::make_shared<std::vector<catalog::markets::Market>>();
  http::Pool& pool = host_.pool;
  host_.pool.post(
      [path, names, result, list, &pool, stations = m.stations] {
        struct stat st;
        std::string text;
        time::Seconds now = time::now();
        if (stat(path.c_str(), &st) == 0 && readFile(path, text)) {
          if (auto saved = catalog::markets::fromJson(text)) {
            if (catalog::markets::cacheFresh(st.st_mtime, now, saved->empty())) *list = std::move(*saved);
          }
        }
        if (list->empty()) {
          if (auto page = wikitext(pool, catalog::markets::kListPage)) *list = catalog::markets::parseMarkets(*page);
          if (!list->empty()) writeFileAtomic(path, catalog::markets::toJson(*list));
        }
        if (list->empty() && readFile(path, text)) {
          // An old saved list beats none.
          if (auto saved = catalog::markets::fromJson(text)) *list = std::move(*saved);
        }
        if (list->empty()) {
          // Offline, or the page changed: the built-in list of every market.
          MLOG_W("lineup", "market list page unavailable; using the built-in list");
          *list = catalog::markets::builtinMarkets();
        }
        (void)stations;
        if (list->empty()) {
          result->problem = "Couldn't load the list of TV markets. Check the connection and try again, or skip local stations.";
          return;
        }
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
  std::vector<catalog::MarketChoice> known;
  std::vector<catalog::markets::Market> todo;
  for (const std::string& name : names) {
    auto existing = std::find_if(draft.markets.begin(), draft.markets.end(),
                                 [&](const catalog::MarketChoice& c) { return c.name == name; });
    if (existing != draft.markets.end() && !existing->callsigns.empty()) continue;
    auto mk = std::find_if(impl_->tvMarkets.begin(), impl_->tvMarkets.end(),
                           [&](const catalog::markets::Market& x) { return x.name == name; });
    todo.push_back(mk != impl_->tvMarkets.end() ? *mk : catalog::markets::Market{name, std::nullopt});
  }
  auto fetched = std::make_shared<std::map<std::string, catalog::MarketChoice>>();
  http::Pool& pool = host_.pool;
  host_.pool.post(
      [todo, fetched, &pool, stations = impl_->stations] {
        bool first = true;
        for (const auto& mk : todo) {
          catalog::MarketChoice choice;
          choice.name = mk.name;
          choice.templateName = mk.templateName;
          if (mk.templateName) {
            // Wikipedia rate-limits bursts.
            if (!first) usleep(guide::stations::kRequestSpacingMs * 1000);
            first = false;
            if (auto page = wikitext(pool, *mk.templateName))
              choice.callsigns = catalog::markets::callsignsInWikitext(*page);
          }
          if (choice.callsigns.empty()) choice.callsigns = catalog::markets::fallbackCallsigns(mk, *stations);
          (*fetched)[mk.name] = std::move(choice);
        }
      },
      [names, draft, fetched, done] {
        std::vector<catalog::MarketChoice> out;
        for (const std::string& name : names) {
          auto f = fetched->find(name);
          if (f != fetched->end()) {
            out.push_back(f->second);
            continue;
          }
          for (const catalog::MarketChoice& c : draft.markets)
            if (c.name == name) out.push_back(c);
        }
        done(std::move(out));
      },
      http::Priority::Visible);
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
  for (const EntryInfo& info : m.catalog->entries) {
    auto& c = counts[info.category];
    ++c.second;
    if (catalog::suggests(info, everything, entryHasGuide(info.id), info.name)) ++c.first;
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
  for (const EntryInfo& info : m.catalog->entries) {
    if (catalog::suggests(info, draft, entryHasGuide(info.id), info.name)) draft.suggested.insert(info.id);
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

void Store::toggleFavorite(const std::string& entryId) {
  Impl& m = *impl_;
  if (!m.favorites.erase(entryId)) m.favorites.insert(entryId);
  saveLists();
  m.channelsDirty = true;
  // Emby favourites also go to the server.
  auto it = m.entryInfo.find(entryId);
  if (it == m.entryInfo.end()) return;
  bool fav = m.favorites.count(entryId) > 0;
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
  saveLists();
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
      m.pendingGuide.push_back(r);
      m.pendingGuideSet.insert(r);
      added = true;
    }
  }
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
  // An entry's guide is the best of its versions' guides.
  Impl& m = *impl_;
  time::Seconds start = windowStart();
  time::Seconds end = start + static_cast<time::Seconds>(kGuideHours * 3600);
  auto coverage = [&](const std::vector<Program>& list) {
    time::Seconds total = 0;
    for (const Program& p : list) total += std::max<time::Seconds>(0, std::min(p.end, end) - std::max(p.start, start));
    return total;
  };
  for (const std::string& id : entryIDs) {
    auto info = m.entryInfo.find(id);
    if (info == m.entryInfo.end()) continue;
    const catalog::ProgramList* best = nullptr;
    std::string bestMember;
    time::Seconds bestCoverage = -1;
    bool anyLoading = false, anyLoaded = false, anyFailed = false;
    for (const std::string& member : m.catalog->entries[info->second].members) {
      auto p = m.programs.find(member);
      if (p != m.programs.end() && p->second && !p->second->empty()) {
        time::Seconds value = coverage(*p->second);
        if (value > bestCoverage) {
          best = &p->second;
          bestCoverage = value;
          bestMember = member;
        }
      }
      auto st = m.guideState.find(member);
      if (st != m.guideState.end()) {
        anyLoading = anyLoading || st->second == Impl::GuideState::Loading;
        anyLoaded = anyLoaded || st->second == Impl::GuideState::Loaded;
        anyFailed = anyFailed || st->second == Impl::GuideState::Failed;
      }
    }
    // Unchanged since last time (same version, same programmes and tags): keep it.
    std::string key;
    if (best) {
      int tagged = 0;
      for (const Program& p : **best) tagged += p.genres ? 1 : 0;
      key = bestMember + "|" + std::to_string((*best)->size()) + "|" + (*best)->front().id + "|" + (*best)->back().id +
            "|" + std::to_string(tagged);
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
}

void Store::readSavedListings() {
  Impl& m = *impl_;
  if (m.savedListingsRead) return;
  m.savedListingsRead = true;
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

namespace {

// Streams a guide download through gunzip into an XMLTV reader, on the
// calling (pool) thread. Big files wait while full screen video plays.
struct GuideReadState {
  std::shared_ptr<std::atomic<bool>> playing, stop;
  std::shared_ptr<std::atomic<int>> foreground;
};

// Guide reads wait while video plays full screen (it needs the CPU), up to
// 5 minutes, and while a foreground job holds a lot of memory (the saved
// lists loading, the channel sort), up to 3 minutes.
void waitForPlayback(const GuideReadState& st) {
  for (int i = 0; i < 600 && st.playing->load() && !st.stop->load(); ++i) usleep(500000);
  for (int i = 0; i < 360 && st.foreground->load() > 0 && !st.stop->load(); ++i) usleep(500000);
}

}  // namespace

void Store::runProviderGuide() {
  Impl& m = *impl_;
  bool force = m.providerForce;
  m.providerForce = false;
  if (m.providerRunning || m.partial || m.channels->empty()) return;
  struct Job {
    std::string sourceId, name, url, key;
    bool insecure = false;
    std::vector<Channel> channels;
    const guide::provider::Saved* saved = nullptr;
  };
  std::vector<Job> jobs;
  std::set<std::string> countries = m.prefs.guideCountries();
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
    for (const Channel& c : *m.channels) {
      if (c.serverId != j.sourceId) continue;
      auto t = m.catalog->traits.find(c.id);
      if (t != m.catalog->traits.end() && t->second.familyKey) continue;
      if (guide::provider::inScope(c, countries)) j.channels.push_back(c);
    }
    if (j.channels.empty()) continue;
    auto saved = m.providerSaved.find(j.key);
    const guide::provider::Saved* s = saved == m.providerSaved.end() ? nullptr : &saved->second;
    if (!force && !guide::provider::needsRebuild(s, static_cast<int>(j.channels.size()), time::now())) continue;
    jobs.push_back(std::move(j));
  }
  if (jobs.empty()) return;
  m.providerRunning = true;
  auto results = std::make_shared<std::vector<std::pair<std::string, guide::provider::Outcome>>>();
  auto shared = std::make_shared<std::vector<Job>>(std::move(jobs));
  GuideReadState st{m.playingFull, m.stopJobs, m.foregroundJobs};
  http::Pool& pool = host_.pool;
  std::string dir = host_.files.dir();
  Files* storage = &host_.files;
  uint64_t gen = m.generation;
  host_.pool.post(
      [shared, results, st, &pool, storage] {
        for (Job& j : *shared) {
          if (st.stop->load()) return;
          std::string url = j.url;
          bool insecure = j.insecure;
          guide::provider::ReadExactFn read = [&pool, url, insecure, st](const auto& ids, guide::Window window,
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
          guide::provider::Outcome o = guide::provider::build(j.channels, time::now(), read);
          if (o.kind == guide::provider::Outcome::Kind::built) {
            writeFileStreaming(storage->path(guide::provider::savedFileName(j.key)),
                               [&o](const json::Writer::Sink& sink) { return guide::provider::writeSaved(o.saved, sink); });
          }
          results->push_back({j.key, std::move(o)});
        }
      },
      [this, gen, results] {
        Impl& mm = *impl_;
        if (gen != mm.generation) return;
        mm.providerRunning = false;
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
      },
      http::Priority::Long);
  (void)dir;
}

void Store::runOutsideGuide() {
  Impl& m = *impl_;
  bool force = m.outsideForce;
  m.outsideForce = false;
  if (!settings_.fillMissingListings || m.outsideRunning || m.partial || m.channels->empty()) return;
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
  auto previous = std::make_shared<std::optional<guide::Saved>>(m.outsideSaved);
  auto built = std::make_shared<guide::BuildResult>();
  std::string guideDir = host_.files.path("guides");
  mkdir(guideDir.c_str(), 0700);
  std::string savedPath = host_.files.path(guide::savedFileName(m.signature));
  GuideReadState st{m.playingFull, m.stopJobs, m.foregroundJobs};
  http::Pool& pool = host_.pool;
  uint64_t gen = m.generation;
  auto newSaved = std::make_shared<std::optional<guide::Saved>>();
  host_.pool.post(
      [snapshot, shards, previous, built, guideDir, savedPath, st, &pool, newSaved, stations = m.stations] {
        waitForPlayback(st);
        guide::BuildIO io;
        io.cancelled = [st] { return st.stop->load(); };
        io.fetch = [&pool, guideDir, st](const guide::Source& source, const std::optional<std::string>& etag,
                                          const std::optional<time::Seconds>& lastModified, bool waitPlayback) {
          guide::BuildIO::FetchOutcome out;
          if (waitPlayback) waitForPlayback(st);
          std::string file = guideDir + "/" + source.id();
          std::string tmp = file + ".part";
          auto f = std::make_shared<std::ofstream>(tmp, std::ios::binary | std::ios::trunc);
          http::Request q;
          q.url = source.url;
          q.tag = "outside.guide";
          q.timeoutMs = 300000;
          q.priority = http::Priority::Background;
          q.follow = true;
          struct stat sb;
          bool have = stat(file.c_str(), &sb) == 0;
          if (have && etag) q.headers.push_back("If-None-Match: " + *etag);
          (void)lastModified;
          // Raw bytes to disk (inflated later, while reading).
          q.onChunk = [f](std::string_view c) {
            f->write(c.data(), static_cast<std::streamsize>(c.size()));
            return static_cast<bool>(*f);
          };
          http::Response r = pool.fetchNow(std::move(q));
          f->close();
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
          if (r.ok && r.status == 200) {
            std::rename(tmp.c_str(), file.c_str());
            fetched.status = 200;
            if (!r.etag.empty()) fetched.etag = r.etag;
            fetched.bytes = static_cast<int64_t>(r.bytes);
            out.fetched = fetched;
            return out;
          }
          unlink(tmp.c_str());
          std::string problem = r.status ? "error " + std::to_string(r.status) : r.error;
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
          s.sources = built->states;
          s.nextCheck = built->nextCheck;
          s.targetsKey = built->targetsKey;
          s.detailVersion = guide::kDetailVersion;
          writeFileStreaming(savedPath, [&s](const json::Writer::Sink& sink) { return guide::writeSaved(s, sink); });
          *newSaved = std::move(s);
        }
      },
      [this, gen, built, newSaved] {
        Impl& mm = *impl_;
        if (gen != mm.generation) return;
        mm.outsideRunning = false;
        for (const std::string& line : built->log) MLOG_I("guide", "%s", redact(line).c_str());
        for (const std::string& p : built->problems) MLOG_W("guide", "%s", redact(p).c_str());
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
  memory::release();
  if (host_.memoryNote) host_.memoryNote(what);
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

double Store::reliabilityOf(const std::string& channelId) const {
  const Impl& m = *impl_;
  auto t = m.catalog->traits.find(channelId);
  std::string tag = t == m.catalog->traits.end() ? "" : t->second.sourceTag;
  auto r = m.reliability.find(tag);
  int ok = r == m.reliability.end() || r->second.size() < 2 ? 0 : r->second[0];
  int bad = r == m.reliability.end() || r->second.size() < 2 ? 0 : r->second[1];
  return static_cast<double>(ok + 1) / static_cast<double>(ok + bad + 2);
}

std::vector<std::string> Store::versionsOf(const std::string& id) const {
  // An entry's own versions, best first, the pinned one first; a raw
  // channel alone (sports::store::baseVersions, LiveTVStore.baseVersions).
  const Impl& m = *impl_;
  auto info = m.entryInfo.find(id);
  const EntryInfo* ei = info == m.entryInfo.end() ? nullptr : &m.catalog->entries[info->second];
  auto pin = m.pinned.find(id);
  std::optional<std::string> pinned;
  if (pin != m.pinned.end()) pinned = pin->second;
  return sports::store::baseVersions(
      id, ei, pinned, [this](const std::string& c) { return reliabilityOf(c); },
      [&m](const std::string& c) { return m.indexByID.count(c) > 0; });
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
    if (t != m.catalog->traits.end()) {
      static const char* kQuality[] = {"SD", "HD", "FHD", "4K"};
      int q = std::clamp(t->second.quality, 0, 3);
      parts.push_back(kQuality[q]);
      f.quality = kQuality[q];
      if (t->second.isWest) parts.push_back("West feed");
    }
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
        auto s = m.m3uStreams.find(id);
        if (s == m.m3uStreams.end() || s->second.url.empty()) continue;
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
  http::Request q = toHttp(cl->openStreamRequest(item), "emby.open", http::Priority::Visible);
  q.afterTransfer = [cl, item, stream](http::Response& r) {
    auto s = cl->parseOpenStream(r.status, r.body, item);
    if (s) *stream = s.take();
    else MLOG_W("play", "open failed: %s", s.error().message.c_str());
    r.body.clear();
  };
  Feed base = feed;
  closeStream();
  host_.pool.submit(std::move(q), [this, cl, stream, base, done](http::Response&&) {
    if (!*stream) {
      done(std::nullopt);
      return;
    }
    Feed f = base;
    f.url = (*stream)->url;
    f.needsOpen = false;
    openStream_ = std::make_shared<OpenStream>(OpenStream{cl, **stream});
    host_.pool.submit(toHttp(cl->reportPlayingRequest(**stream), "emby.playing"), nullptr);
    done(f);
  });
}

void Store::closeStream() {
  if (!openStream_) return;
  // Each step best-effort, in order: report stopped, stop transcode, close.
  auto os = openStream_;
  openStream_.reset();
  std::vector<sources::Request> steps = os->client->closeStreamRequests(os->stream);
  http::Pool& pool = host_.pool;
  pool.post(
      [steps, &pool] {
        for (const sources::Request& r : steps) pool.fetchNow(toHttp(r, "emby.close", http::Priority::Background));
      },
      nullptr, http::Priority::Background);
}

void Store::recordFeed(const std::string& channelId, bool worked) {
  Impl& m = *impl_;
  auto t = m.catalog->traits.find(channelId);
  std::string tag = t == m.catalog->traits.end() ? "" : t->second.sourceTag;
  auto& counts = m.reliability[tag];
  if (counts.size() < 2) counts = {0, 0};
  ++counts[worked ? 0 : 1];
  saveLists();
}

void Store::pinFeed(const std::string& entryId, const std::string& channelId) {
  if (channelId.empty()) impl_->pinned.erase(entryId);
  else impl_->pinned[entryId] = channelId;
  saveLists();
}

std::string Store::pinnedFeed(const std::string& entryId) const {
  auto it = impl_->pinned.find(entryId);
  return it == impl_->pinned.end() ? std::string() : it->second;
}

// ------------------------------------------------------------ snapshots

void Store::publishChannels() {
  Impl& m = *impl_;
  auto out = std::make_shared<Channels>();
  out->version = ++m.publishVersion;
  out->rawChannels = m.channels->size();
  out->lineupConfigured = m.prefs.configured;
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
    g.name = ei ? ei->name : e.displayName();
    g.group = catalog::groupOf(e);
    if (g.group.empty()) g.group = kOtherGroup;
    g.feeds = ei ? static_cast<int>(ei->members.size()) : 1;
    g.west = str::endsWith(e.id, "~west");
    g.inLineup = lo.lineupIDs.count(e.id) > 0;
    g.favorite = m.favorites.count(e.id) > 0 || e.isFavorite;
    // Which logo a row tries and how its cell reads (ChannelLogos.spec).
    const Channel* member = nullptr;
    if (ei && !ei->members.empty()) {
      auto ci = m.indexByID.find(ei->members.front());
      if (ci != m.indexByID.end()) member = &(*m.channels)[ci->second];
    }
    logos::LogoSpec spec = logos::spec(
        g.name, ei ? ei->core : std::nullopt, g.west, ei && cat == catalog::LineupCategory::local && ei->callsign,
        ei ? ei->callsign : std::nullopt, ei ? ei->virtualNumber : std::nullopt, e.imageUrl,
        member ? member->epgId : std::nullopt, cat, m.iptvOrg.get());
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
  channels_ = out;
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

bool Store::tick(uint32_t ms) {
  Impl& m = *impl_;
  uint64_t eventsVersion = events_->version;
  eventsTick(ms);
  *m.playingFull = host_.playingFull();
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
      wantListings(m.recentlyRequested);
    }
    if (m.channelsLoadedAt && time::now() - m.channelsLoadedAt > kChannelLifetime && !m.loading && !*m.playingFull) {
      m.channelsLoadedAt = time::now();  // one attempt per lifetime
      load(true);
    }
  }
  bool changed = events_->version != eventsVersion;
  if (m.channelsDirty) {
    m.channelsDirty = false;
    publishChannels();
    changed = true;
  }
  if (m.listingsDirty || m.allListingsDirty) {
    m.listingsDirty = false;
    publishListings();
    changed = true;
  }
  // Status line while guides build.
  std::string line;
  if (m.loading && !m.channels->empty()) line = "Updating channels\xe2\x80\xa6";
  else if (m.providerRunning) line = "Updating the provider's guide\xe2\x80\xa6";
  else if (m.outsideRunning) line = "Filling in missing listings\xe2\x80\xa6";
  if (status_.phase == StoreStatus::Phase::Ready && status_.line != line) {
    status_.line = line;
    changed = true;
  }
  return changed;
}

}  // namespace marigold::engine
