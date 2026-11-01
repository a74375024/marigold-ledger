#include "platform/logo_cache.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>
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

// Loads under way (most of a download is waiting); of those, at most
// PrepareGate::kMax are decoded and prepared at once.
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
  px::Image small;
  px::Shape shape;
  Treatment treatment;
  uint32_t field = 0;
  bool onDisk = false;
  std::string bytes;  // downloaded, until decoded
  double ms = 0;      // the decode and preparation, on the worker

  // On the worker, once per load: decoded, then made ready for the dark
  // surfaces. The disk keeps the file as it came (a restart decodes and
  // prepares it again), so there is nothing old to throw away.
  void decode(std::string_view in, Art art) {
    auto start = std::chrono::steady_clock::now();
    image = image::decode(in, kMaxW, kMaxH, &shape);
    if (image) {
      treatment = prepare(*image, art);
      if (art == Art::logo) small = smallCopy(*image);
      if (art == Art::flag) field = mainColour(*image);
    }
    ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  }
};

LogoCache::LogoCache(http::Client& pool, std::string dir, size_t textureBudget, size_t diskBudget)
    : pool_(pool), dir_(std::move(dir)), budget_(textureBudget), diskBudget_(diskBudget) {
  mkdir(dir_.c_str(), 0700);
  trimDisk();
}

LogoCache::~LogoCache() {
  for (auto& [url, e] : entries_) release(e);
}

std::string LogoCache::fileFor(const std::string& url) const {
  return dir_ + "/" + str::hex64(str::fnv1a64(url));
}

std::string LogoCache::chain(const std::vector<std::string>& urls) { return logos::chain(urls); }

Texture LogoCache::get(const std::string& url, Art art) {
  if (url.empty()) return {};
  auto it = entries_.find(url);
  if (it == entries_.end()) {
    if (!str::startsWith(url, "http://") && !str::startsWith(url, "https://")) return {};
    Entry fresh;
    fresh.art = art;
    it = entries_.emplace(url, std::move(fresh)).first;
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
    case State::Ready: {
      const Texture& t = it->second.tex;
      std::string s = fillsCard(t.shape) ? "shown" : "shown, low quality";
      if (t.treatment.boxRemoved) s += ", box removed";
      if (t.treatment.tuned) s += ", colours tuned";
      return s;
    }
    case State::Failed: return "failed";
    case State::Loading: return "loading";
    case State::Wanted: return "waiting";
  }
  return "";
}

void LogoCache::frame() {
  ++frame_;
  // A new load starts with a decode (from the disk): only when one can run
  // now, after the downloads waiting for theirs.
  auto room = [this] { return inFlight_ < kMaxInFlight && !gate_.full() && gate_.waiting() == 0; };
  if (!room()) return;
  // Newest wanted first; LRU order already puts the latest draws in front.
  for (const std::string& url : lru_) {
    if (!room()) break;
    Entry& e = entries_[url];
    if (e.wantedFrame + kWantedFrames < frame_) break;  // rest were not drawn lately
    if (e.state == State::Wanted) start(url);
  }
}

void LogoCache::abandonLoads() {
  ++epoch_;
  inFlight_ = 0;
  gate_.reset();
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
  maxInFlight_ = std::max(maxInFlight_, inFlight_);
  auto r = std::make_shared<Result>();
  std::string path = fileFor(url);
  std::weak_ptr<bool> alive = alive_;
  Art art = e.art;
  gate_.run([this, r, path, art, alive, epoch = epoch_, url] {
    pool_.post(
        [r, path, art] {
          std::string bytes;
          if (!readFile(path, bytes)) return;
          r->onDisk = true;
          r->decode(bytes, art);
          if (!r->image) unlink(path.c_str());  // corrupt: fetch it again
          else utime(path.c_str(), nullptr);    // freshen for the disk LRU
        },
        [this, alive, epoch, url, r] {
          if (alive.expired() || epoch != epoch_) return;
          gate_.ended();
          if (r->onDisk) gate_.took(r->ms);
          if (r->image) finish(url, r);
          else fetch(url, std::make_shared<Result>(), 0);
        },
        http::Priority::Visible);
  });
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
  auto found = entries_.find(key);
  Art art = found == entries_.end() ? Art::logo : found->second.art;
  // The bytes are kept as they came; the decode waits its turn (gate_).
  q.afterTransfer = [r](http::Response& resp) {
    if (!resp.ok || resp.status != 200) return;
    r->bytes = std::move(resp.body);
    resp.body = std::string();
  };
  std::weak_ptr<bool> alive = alive_;
  bool more = index + 1 < links.size();
  pool_.submit(std::move(q), [this, alive, epoch = epoch_, key, r, index, more, path, art](http::Response&& resp) {
    if (alive.expired() || epoch != epoch_) return;
    if (r->bytes.empty()) {
      if (more && !resp.cancelled) fetch(key, r, index + 1);  // the next logo in the list
      else finish(key, r);
      return;
    }
    bool cancelled = resp.cancelled;
    gate_.run([this, alive, epoch, key, r, index, more, path, art, cancelled] {
      pool_.post(
          [r, path, art] {
            r->decode(r->bytes, art);
            if (r->image) writeFileAtomic(path, r->bytes);
            r->bytes = std::string();
          },
          [this, alive, epoch, key, r, index, more, cancelled, size = r->bytes.size()] {
            if (alive.expired() || epoch != epoch_) return;
            gate_.ended();
            gate_.took(r->ms);
            if (r->image) writtenSinceTrim_ += size;
            if (!r->image && more && !cancelled) {
              fetch(key, r, index + 1);  // the next logo in the list
              return;
            }
            finish(key, r);
          },
          http::Priority::Visible);
    });
  });
}

LogoCache::Stats LogoCache::takeStats() {
  PrepareGate::Stats g = gate_.take();
  Stats out;
  out.prepared = g.prepared;
  out.prepareMaxMs = g.maxMs;
  out.maxPreparing = g.maxRunning;
  out.maxInFlight = maxInFlight_;
  maxInFlight_ = inFlight_;
  return out;
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
  upload(e, *r);
  e.state = State::Ready;
  evict();
  if (writtenSinceTrim_ > diskBudget_ / 4) {
    writtenSinceTrim_ = 0;
    trimDisk();
  }
}

void LogoCache::upload(Entry& e, const Result& r) {
  const px::Image& im = *r.image;
  e.tex.id = gfx::uploadTexture(im, e.tex.id);
  e.tex.w = im.w;
  e.tex.h = im.h;
  if (!r.small.empty()) {
    e.tex.small = gfx::uploadTexture(r.small, e.tex.small);
    e.tex.smallW = r.small.w;
    e.tex.smallH = r.small.h;
  } else {
    gfx::releaseTexture(e.tex.small);
    e.tex.smallW = e.tex.smallH = 0;
  }
  e.tex.field = r.field;
  e.tex.treatment = r.treatment;
  e.tex.shape = r.shape;
  bytes_ -= e.bytes;
  e.bytes = im.rgba.size() + r.small.rgba.size();
  bytes_ += e.bytes;
}

void LogoCache::release(Entry& e) {
  gfx::releaseTexture(e.tex.id);
  gfx::releaseTexture(e.tex.small);
}

void LogoCache::evict() {
  // Drop least recently drawn textures; keep the entry so it reloads from disk.
  for (auto it = lru_.rbegin(); it != lru_.rend() && bytes_ > budget_; ++it) {
    Entry& e = entries_[*it];
    if (e.state != State::Ready || e.wantedFrame + kWantedFrames >= frame_) continue;
    release(e);
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
