#include "platform/logo_cache.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

#include "core/logos/logos.hpp"
#include "core/util/log.hpp"
#include "core/util/strings.hpp"
#include "core/util/time.hpp"
#include "platform/gfx.hpp"
#include "platform/http.hpp"
#include "platform/image.hpp"

namespace marigold::logos {
namespace {

constexpr int kMaxInFlight = 4;
constexpr uint64_t kWantedFrames = 2;     // asked for in the last two frames
constexpr int64_t kRetryAfterS = 30 * 60;  // a broken logo is not asked again soon
constexpr size_t kMaxLogoBytes = 2u << 20;

bool readFile(const std::string& path, std::string& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return !out.empty();
}

bool writeFileAtomic(const std::string& path, const std::string& data) {
  std::string tmp = path + ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!f) return false;
  }
  return std::rename(tmp.c_str(), path.c_str()) == 0;
}

}  // namespace

struct LogoCache::Result {
  std::optional<px::Image> image;
  px::Tone tone = px::Tone::normal;
  bool onDisk = false;
};

LogoCache::LogoCache(http::Client& pool, std::string dir, size_t textureBudget, size_t diskBudget)
    : pool_(pool), dir_(std::move(dir)), budget_(textureBudget), diskBudget_(diskBudget) {
  mkdir(dir_.c_str(), 0700);
  trimDisk();
}

LogoCache::~LogoCache() {
  for (auto& [url, e] : entries_) gfx::releaseTexture(e.tex.id);
}

std::string LogoCache::fileFor(const std::string& url) const {
  return dir_ + "/" + str::hex64(str::fnv1a64(url));
}

std::string LogoCache::chain(const std::vector<std::string>& urls) { return logos::chain(urls); }

Texture LogoCache::get(const std::string& url) {
  if (url.empty()) return {};
  auto it = entries_.find(url);
  if (it == entries_.end()) {
    if (!str::startsWith(url, "http://") && !str::startsWith(url, "https://")) return {};
    it = entries_.emplace(url, Entry{}).first;
    lru_.push_front(url);
    it->second.lru = lru_.begin();
  }
  Entry& e = it->second;
  e.wantedFrame = frame_;
  lru_.splice(lru_.begin(), lru_, e.lru);
  if (e.state == State::Failed && time::now() >= e.retryAt) e.state = State::Wanted;
  return e.state == State::Ready ? e.tex : Texture{};
}

std::string LogoCache::status(const std::string& url) const {
  auto it = entries_.find(url);
  if (it == entries_.end()) return "not drawn yet";
  switch (it->second.state) {
    case State::Ready: return "shown";
    case State::Failed: return "failed";
    case State::Loading: return "loading";
    case State::Wanted: return "waiting";
  }
  return "";
}

void LogoCache::frame() {
  ++frame_;
  if (inFlight_ >= kMaxInFlight) return;
  // Newest wanted first; LRU order already puts the latest draws in front.
  for (const std::string& url : lru_) {
    if (inFlight_ >= kMaxInFlight) break;
    Entry& e = entries_[url];
    if (e.wantedFrame + kWantedFrames < frame_) break;  // rest were not drawn lately
    if (e.state == State::Wanted) start(url);
  }
}

void LogoCache::abandonLoads() {
  ++epoch_;
  inFlight_ = 0;
  for (auto& [url, e] : entries_) {
    if (e.state == State::Loading) e.state = State::Wanted;
  }
}

void LogoCache::retryFailed() {
  for (auto& [url, e] : entries_) {
    if (e.state == State::Failed) e.state = State::Wanted;
  }
}

void LogoCache::start(const std::string& url) {
  Entry& e = entries_[url];
  e.state = State::Loading;
  ++inFlight_;
  auto r = std::make_shared<Result>();
  std::string path = fileFor(url);
  std::weak_ptr<bool> alive = alive_;
  pool_.post(
      [r, path] {
        std::string bytes;
        if (!readFile(path, bytes)) return;
        r->onDisk = true;
        r->image = image::decode(bytes, kMaxW, kMaxH);
        if (r->image) r->tone = px::toneOf(*r->image);
        if (!r->image) unlink(path.c_str());  // corrupt: fetch it again
        else utime(path.c_str(), nullptr);    // freshen for the disk LRU
      },
      [this, alive, epoch = epoch_, url, r] {
        if (alive.expired() || epoch != epoch_) return;
        if (r->image) finish(url, r);
        else fetch(url, std::make_shared<Result>(), 0);
      },
      http::Priority::Visible);
}

void LogoCache::fetch(const std::string& key, std::shared_ptr<Result> r, size_t index) {
  std::vector<std::string_view> links = str::split(key, '\n');
  if (index >= links.size()) {
    finish(key, r);
    return;
  }
  http::Request q;
  q.url = std::string(links[index]);
  q.timeoutMs = 15000;
  q.maxBytes = kMaxLogoBytes;
  q.tag = "logo";
  q.priority = http::Priority::Normal;
  std::string path = fileFor(key);
  q.afterTransfer = [r, path](http::Response& resp) {
    if (!resp.ok || resp.status != 200) return;
    r->image = image::decode(resp.body, kMaxW, kMaxH);
    if (r->image) r->tone = px::toneOf(*r->image);
    if (r->image) writeFileAtomic(path, resp.body);
    resp.body.clear();
    resp.body.shrink_to_fit();
  };
  std::weak_ptr<bool> alive = alive_;
  bool more = index + 1 < links.size();
  pool_.submit(std::move(q), [this, alive, epoch = epoch_, key, r, index, more](http::Response&& resp) {
    if (alive.expired() || epoch != epoch_) return;
    if (r->image) writtenSinceTrim_ += resp.bytes;
    if (!r->image && more && !resp.cancelled) {
      fetch(key, r, index + 1);  // the next logo in the list
      return;
    }
    finish(key, r);
  });
}

void LogoCache::finish(const std::string& url, const std::shared_ptr<Result>& r) {
  --inFlight_;
  auto it = entries_.find(url);
  if (it == entries_.end()) return;
  Entry& e = it->second;
  if (!r->image) {
    e.state = State::Failed;
    e.retryAt = time::now() + kRetryAfterS;
    return;
  }
  upload(e, *r->image, r->tone);
  e.state = State::Ready;
  evict();
  if (writtenSinceTrim_ > diskBudget_ / 4) {
    writtenSinceTrim_ = 0;
    trimDisk();
  }
}

void LogoCache::upload(Entry& e, const px::Image& im, px::Tone tone) {
  e.tex.id = gfx::uploadTexture(im, e.tex.id);
  e.tex.w = im.w;
  e.tex.h = im.h;
  e.tex.tone = tone;
  bytes_ -= e.bytes;
  e.bytes = im.rgba.size();
  bytes_ += e.bytes;
}

void LogoCache::evict() {
  // Drop least recently drawn textures; keep the entry so it reloads from disk.
  for (auto it = lru_.rbegin(); it != lru_.rend() && bytes_ > budget_; ++it) {
    Entry& e = entries_[*it];
    if (e.state != State::Ready || e.wantedFrame + kWantedFrames >= frame_) continue;
    gfx::releaseTexture(e.tex.id);
    e.tex = {};
    bytes_ -= e.bytes;
    e.bytes = 0;
    e.state = State::Wanted;
  }
}

void LogoCache::trimDisk() {
  std::string dir = dir_;
  size_t budget = diskBudget_;
  pool_.post(
      [dir, budget] {
        struct File {
          std::string path;
          time_t mtime;
          size_t size;
        };
        std::vector<File> files;
        size_t total = 0;
        if (DIR* d = opendir(dir.c_str())) {
          while (dirent* ent = readdir(d)) {
            if (ent->d_name[0] == '.') continue;
            std::string p = dir + "/" + ent->d_name;
            struct stat st;
            if (stat(p.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
            files.push_back({p, st.st_mtime, static_cast<size_t>(st.st_size)});
            total += static_cast<size_t>(st.st_size);
          }
          closedir(d);
        }
        if (total <= budget) return;
        std::sort(files.begin(), files.end(),
                  [](const File& a, const File& b) { return a.mtime < b.mtime; });
        size_t removed = 0;
        for (const File& f : files) {
          if (total <= budget * 3 / 4) break;
          if (unlink(f.path.c_str()) == 0) {
            total -= f.size;
            ++removed;
          }
        }
        MLOG_I("logos", "disk cache trimmed: %zu files removed", removed);
      },
      nullptr, http::Priority::Background);
}

}  // namespace marigold::logos
