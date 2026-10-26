// Channel logos as GL textures. Each logo is fetched at most once (and kept
// on disk, so a restart costs no requests), decoded and shrunk on a worker,
// and uploaded on the UI thread. Only logos asked for in the last couple of
// frames are loaded, newest first, a few at a time: scrolling past 300
// channels does not start 300 downloads. Textures live in an LRU with a
// memory budget.
#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/util/pixels.hpp"

namespace marigold::http {
class Client;
}

namespace marigold::logos {

struct Texture {
  unsigned id = 0;  // 0 = not ready
  int w = 0, h = 0;
  px::Tone tone = px::Tone::normal;  // how it reads on a light background
};

class LogoCache {
 public:
  // `dir` is the disk cache folder (created if missing).
  LogoCache(http::Client& pool, std::string dir, size_t textureBudget = 32u << 20,
            size_t diskBudget = 48u << 20);
  ~LogoCache();  // GL thread: deletes the textures
  LogoCache(const LogoCache&) = delete;
  LogoCache& operator=(const LogoCache&) = delete;

  // UI thread. The texture when ready; otherwise marks it wanted. `url` may
  // list several links, one per line, tried in order until one loads (see
  // chain()).
  Texture get(const std::string& url);
  static std::string chain(const std::vector<std::string>& urls);
  // UI thread, once per frame, after drawing: starts the wanted loads.
  void frame();
  // Forgets failures (e.g. after the network comes back).
  void retryFailed();
  // After http::Client::cancelAll(): the dropped loads will never call
  // back, so they go back to wanted.
  void abandonLoads();

  size_t textureBytes() const { return bytes_; }
  // For the channel report: "shown", "failed", "loading", "waiting" (asked
  // for, not started) or "not drawn yet".
  std::string status(const std::string& url) const;

  // Largest logo kept, in pixels (fits the hero; rows draw it smaller).
  static constexpr int kMaxW = 320, kMaxH = 180;

 private:
  enum class State { Wanted, Loading, Ready, Failed };
  struct Entry {
    State state = State::Wanted;
    Texture tex;
    size_t bytes = 0;
    uint64_t wantedFrame = 0;
    int64_t retryAt = 0;  // seconds; for Failed
    std::list<std::string>::iterator lru;
  };
  struct Result;

  void start(const std::string& url);
  void fetch(const std::string& key, std::shared_ptr<Result> r, size_t index);
  void finish(const std::string& url, const std::shared_ptr<Result>& r);
  void upload(Entry& e, const px::Image& im, px::Tone tone);
  void evict();
  void trimDisk();
  std::string fileFor(const std::string& url) const;

  http::Client& pool_;
  std::string dir_;
  size_t budget_, diskBudget_;
  size_t bytes_ = 0;
  std::unordered_map<std::string, Entry> entries_;
  std::list<std::string> lru_;  // front = most recently drawn
  uint64_t frame_ = 0;
  int inFlight_ = 0;
  size_t writtenSinceTrim_ = 0;
  uint64_t epoch_ = 0;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace marigold::logos
