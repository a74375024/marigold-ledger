#include "core/engine/updates.hpp"

#include <cstdio>
#include <utility>

#include "core/engine/membership.hpp"
#include "core/util/json.hpp"
#include "core/util/log.hpp"
#include "core/util/sha256.hpp"

namespace marigold::engine::updates {
namespace {

// The release's answer is a few KB; anything far bigger isn't it.
constexpr size_t kReleaseMaxBytes = 1u << 20;
// A file whose size the release doesn't give: at most this much.
constexpr uint64_t kFileMaxBytes = 400ull << 20;

std::optional<int> number(std::string_view s) {
  if (s.empty() || s.size() > 6) return std::nullopt;
  int v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return std::nullopt;
    v = v * 10 + (c - '0');
  }
  return v;
}

bool isHex64(const std::string& s) {
  if (s.size() != 64) return false;
  for (char c : s)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  return true;
}

std::string lower(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return s;
}

http::Request releaseRequest(const std::string& asset, std::shared_ptr<std::optional<Release>> out,
                             http::Priority priority) {
  http::Request q;
  q.url = kLatestUrl;
  q.headers = {"Accept: application/vnd.github+json"};
  q.maxBytes = kReleaseMaxBytes;
  q.priority = priority;
  q.tag = "update.check";
  q.afterTransfer = [asset, out](http::Response& r) {
    if (r.status == 200) *out = parseRelease(r.body, asset);
    r.body.clear();
  };
  return q;
}

// A download in progress: the file and its digest, on the worker. Closed
// however the transfer ends (a dropped request included).
struct Download {
  std::string path;
  std::FILE* file = nullptr;
  sha256::Hasher hasher;
  uint64_t bytes = 0;
  bool writeFailed = false;
  std::string digest;

  ~Download() {
    if (file) std::fclose(file);
  }
  bool write(std::string_view piece) {
    if (!file) file = std::fopen(path.c_str(), "wb");
    if (!file || std::fwrite(piece.data(), 1, piece.size(), file) != piece.size()) {
      writeFailed = true;
      return false;
    }
    hasher.update(piece);
    bytes += piece.size();
    return true;
  }
  void finish() {
    if (file && std::fclose(file) != 0) writeFailed = true;
    file = nullptr;
    digest = hasher.hex();
  }
};

}  // namespace

std::optional<Version> parseVersion(std::string_view s) {
  if (!s.empty() && (s.front() == 'v' || s.front() == 'V')) s.remove_prefix(1);
  Version v;
  int* parts[] = {&v.major, &v.minor, &v.patch};
  for (int i = 0; i < 3; ++i) {
    size_t dot = s.find('.');
    if ((i < 2) != (dot != std::string_view::npos)) return std::nullopt;
    std::string_view piece = i < 2 ? s.substr(0, dot) : s;
    std::optional<int> n = number(piece);
    if (!n) return std::nullopt;
    *parts[i] = *n;
    if (i < 2) s.remove_prefix(dot + 1);
  }
  return v;
}

bool newer(std::string_view candidate, std::string_view current) {
  std::optional<Version> a = parseVersion(candidate), b = parseVersion(current);
  if (!a || !b) return false;
  if (a->major != b->major) return a->major > b->major;
  if (a->minor != b->minor) return a->minor > b->minor;
  return a->patch > b->patch;
}

std::optional<Release> parseRelease(std::string_view body, const std::string& asset) {
  std::optional<json::Value> v = json::parse(body);
  if (!v || !v->isObject()) return std::nullopt;
  if ((*v)["draft"].asBool(false) || (*v)["prerelease"].asBool(false)) return std::nullopt;
  std::string tag = (*v)["tag_name"].asString();
  std::optional<Version> version = parseVersion(tag);
  if (!version) return std::nullopt;
  for (const json::Value& a : (*v)["assets"].items()) {
    if (a["name"].asString() != asset) continue;
    std::string url = a["browser_download_url"].asString();
    if (url.compare(0, 8, "https://") != 0 || url.size() <= 8) return std::nullopt;
    Release r;
    r.version = std::to_string(version->major) + "." + std::to_string(version->minor) + "." +
                std::to_string(version->patch);
    r.url = std::move(url);
    std::string digest = a["digest"].asString();
    if (digest.compare(0, 7, "sha256:") == 0) {
      std::string hex = lower(digest.substr(7));
      if (isHex64(hex)) r.sha256 = std::move(hex);
    }
    int64_t size = a["size"].asInt(0);
    r.size = size > 0 ? static_cast<uint64_t>(size) : 0;
    return r;
  }
  return std::nullopt;
}

std::unique_ptr<Updates> Updates::make(const Host& host, std::string version, std::function<time::Seconds()> now) {
  if (!host.canSelfUpdate()) return nullptr;
  return std::make_unique<Updates>(host, std::move(version), std::move(now));
}

Updates::Updates(const Host& host, std::string version, std::function<time::Seconds()> now)
    : pool_(host.pool),
      files_(host.files),
      ms_(host.ms),
      zone_(host.zone),
      now_(now ? std::move(now) : std::function<time::Seconds()>(time::now)),
      version_(std::move(version)) {
  // Without the means, it stays idle: nothing is read, asked or shown.
  if (!host.canSelfUpdate()) {
    nextCheckMs_ = UINT32_MAX;
    return;
  }
  install_ = host.installUpdate;
  asset_ = host.updateAsset;
  file_ = host.updateFile;
  load();
  // An earlier download: installed by now, or given up.
  std::remove(file_.c_str());
}

void Updates::load() {
  std::optional<std::string> text = files_.read(kStateFile);
  if (!text) return;
  std::optional<json::Value> v = json::parse(*text);
  if (!v) return;
  checkDay_ = (*v)["checkDay"].asInt(0);
  latest_ = (*v)["latest"].asString();
  cardDay_ = (*v)["cardDay"].asInt(0);
  cardVersion_ = (*v)["cardVersion"].asString();
  // Updated since (or a version that was never newer): nothing waits.
  if (!newer(latest_, version_)) latest_.clear();
}

void Updates::save() const {
  json::Value v = json::Value::object();
  v.set("checkDay", checkDay_);
  if (!latest_.empty()) v.set("latest", latest_);
  if (cardDay_) v.set("cardDay", cardDay_);
  if (!cardVersion_.empty()) v.set("cardVersion", cardVersion_);
  files_.write(kStateFile, json::write(v));
}

int64_t Updates::today() const {
  static const time::Zone kUtc = time::Zone::utc();
  return membership::localDay(now_(), zone_ ? zone_() : kUtc);
}

bool Updates::tick(uint32_t ms, bool quiet) {
  bool changed = false;
  if (phase_ == Phase::Failed && static_cast<int32_t>(ms - failedUntil_) >= 0) {
    phase_ = Phase::Idle;
    changed = true;
  }
  if (checking_ || phase_ == Phase::Updating || ms < nextCheckMs_ || !quiet) return changed;
  if (checkDay_ == today()) {
    nextCheckMs_ = ms + 60000;  // the day is looked at again in a minute
    return changed;
  }
  check(ms);
  return changed;
}

void Updates::check(uint32_t ms) {
  checking_ = true;
  nextCheckMs_ = ms + kRetryMs;  // unless an answer comes
  auto rel = std::make_shared<std::optional<Release>>();
  uint64_t generation = generation_;
  pool_.submit(releaseRequest(asset_, rel, http::Priority::Background),
               [this, generation, rel](http::Response&& r) {
                 if (generation != generation_) return;
                 checking_ = false;
                 checked(ms_ ? ms_() : 0, r.status, r.error, *rel);
               });
}

void Updates::checked(uint32_t ms, int status, const std::string& error, const std::optional<Release>& rel) {
  if (status == 200 || status == 404) {
    // 404: no release at all yet.
    checkDay_ = today();
    nextCheckMs_ = ms + 60000;
    std::string before = latest_;
    latest_ = rel && !rel->sha256.empty() && newer(rel->version, version_) ? rel->version : std::string();
    if (latest_ != before || status == 200) save();
    if (!latest_.empty()) MLOG_I("update", "checked: %s is available (running %s)", latest_.c_str(), version_.c_str());
    else MLOG_I("update", "checked: up to date (%s)", version_.c_str());
    return;
  }
  if (status == 403 || status == 429) {
    // Turned away for asking too often (shared addresses): tomorrow.
    checkDay_ = today();
    nextCheckMs_ = ms + 60000;
    save();
  }
  MLOG_W("update", "check failed (status %d%s%s)", status, error.empty() ? "" : ", ", error.c_str());
}

bool Updates::available() const { return !latest_.empty(); }

bool Updates::cardDue() const {
  return available() && phase_ == Phase::Idle && !(cardDay_ == today() && cardVersion_ == latest_);
}

void Updates::cardShown() {
  cardDay_ = today();
  cardVersion_ = latest_;
  save();
}

void Updates::update() {
  if (phase_ == Phase::Updating || !available()) return;
  phase_ = Phase::Updating;
  quit_ = false;
  // Chosen (on the card or in Settings): no card about it again today.
  if (cardDay_ != today() || cardVersion_ != latest_) cardShown();
  MLOG_I("update", "updating to %s", latest_.c_str());
  auto rel = std::make_shared<std::optional<Release>>();
  uint64_t generation = generation_;
  pool_.submit(releaseRequest(asset_, rel, http::Priority::Visible), [this, generation, rel](http::Response&& r) {
    if (generation != generation_) return;
    fetched(r.status, *rel);
  });
}

void Updates::fetched(int status, const std::optional<Release>& rel) {
  if (status != 200) return fail("the release couldn't be read (status " + std::to_string(status) + ")");
  if (!rel) return fail("the release has no file for this device");
  if (!newer(rel->version, version_)) {
    latest_.clear();
    save();
    return fail("the release isn't newer");
  }
  if (rel->sha256.empty()) return fail("the release gives no digest");
  download(*rel);
}

void Updates::download(const Release& rel) {
  auto dl = std::make_shared<Download>();
  dl->path = file_;
  http::Request q;
  q.url = rel.url;
  q.priority = http::Priority::LongFirst;
  q.timeoutMs = 30000;
  q.maxBytes = rel.size ? rel.size + 1 : kFileMaxBytes;
  q.tag = "update.download";
  q.onChunk = [dl](std::string_view piece) { return dl->write(piece); };
  q.afterTransfer = [dl](http::Response&) { dl->finish(); };
  uint64_t generation = generation_;
  std::string want = rel.sha256;
  uint64_t size = rel.size;
  pool_.submit(std::move(q), [this, generation, dl, want, size](http::Response&& r) {
    if (generation != generation_) return;
    if (!r.ok || r.status != 200) return fail("download failed (status " + std::to_string(r.status) + ")");
    if (dl->writeFailed) return fail("the download couldn't be saved");
    if (size && dl->bytes != size) return fail("the download is cut short");
    if (dl->digest != want) return fail("the download doesn't match the release's digest");
    MLOG_I("update", "downloaded and checked (%llu KB)", static_cast<unsigned long long>(dl->bytes / 1024));
    install();
  });
}

void Updates::install() {
  auto out = std::make_shared<UpdateInstall>();
  auto installer = install_;
  std::string path = file_;
  uint64_t generation = generation_;
  pool_.post([installer, path, out] { *out = installer(path); },
             [this, generation, out] {
               if (generation != generation_) return;
               if (!out->ok) return fail(out->why.empty() ? "the installer refused it" : out->why, out->message);
               MLOG_I("update", "handed over%s", out->quit ? "; the app quits for it" : "");
               phase_ = Phase::Idle;
               quit_ = out->quit;
             },
             http::Priority::Visible);
}

void Updates::fail(const std::string& why, const std::string& message) {
  MLOG_W("update", "couldn't update: %s", why.c_str());
  std::remove(file_.c_str());
  phase_ = Phase::Failed;
  failText_ = message.empty() ? kFailedText : message;
  uint32_t ms = ms_ ? ms_() : 0;
  failedUntil_ = ms + kFailedMs;
}

void Updates::abandoned() {
  if (!checking_ && phase_ != Phase::Updating) return;
  ++generation_;
  if (checking_) {
    checking_ = false;
    nextCheckMs_ = (ms_ ? ms_() : 0) + kRetryMs;
  }
  if (phase_ == Phase::Updating) fail("stopped with every other request");
}

}  // namespace marigold::engine::updates
