// What the engine needs from the platform it runs on, and nothing more:
// a pool for requests and background jobs, a folder for its files, a clock,
// the time zone, and a few hooks into the app around it. webOS fills it in
// from the App; a test fills it in with fakes. Everything else the engine
// does (loading channels, sorting, guides, Live Events, saving) is shared.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "core/net/http.hpp"
#include "core/util/time.hpp"

namespace marigold::engine {

// The engine's folder. Names are plain file names ("accounts.json"); path()
// gives the full path for files read and written a piece at a time.
class Files {
 public:
  virtual ~Files() = default;
  virtual const std::string& dir() const = 0;
  virtual std::string path(const std::string& name) const = 0;
  virtual std::optional<std::string> read(const std::string& name) const = 0;
  // Atomic replace. `secret` files are readable by the app user only.
  virtual bool write(const std::string& name, const std::string& content, bool secret = false) = 0;
};

struct Host {
  Host(http::Pool& requests, Files& folder) : pool(requests), files(folder) {}

  http::Pool& pool;
  Files& files;
  // Milliseconds since the app started (monotonic).
  std::function<uint32_t()> ms;
  // The device's time zone (guide times, "Tonight").
  std::function<const time::Zone&()> zone;
  // Video playing full screen: big background jobs wait for it.
  std::function<bool()> playingFull;
  // A memory reading after a big job ("channels sorted"), for the playback log.
  std::function<void(const char* what)> memoryNote;
  // For the channel report: whether a channel's logo is on screen.
  std::function<std::string(const std::string& logoUrl)> logoStatus;
  // Every request was cancelled (a provider change): image loads in flight
  // won't call back.
  std::function<void()> requestsCancelled;
  // The app's build number, for the report.
  std::string build;
};

}  // namespace marigold::engine
