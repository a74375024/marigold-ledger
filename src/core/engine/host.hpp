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

// How each file is kept where lasting storage is small (an Apple TV keeps
// about 500 KB for good; anything else may be deleted when space runs low).
// Keep: what the user made (accounts, settings, lineup, favourites,
// reminders, My Sports). Secret: passwords and tokens. Cache: everything
// rebuilt from the sources (channels, guides, logos, scores, logs).
// webOS and Android keep every file and don't ask.
enum class FileKind { Cache, Keep, Secret };
FileKind fileKind(const std::string& name);

// Build 148: what installing a checked update came to (Host::installUpdate).
struct UpdateInstall {
  bool ok = false;    // handed to the system's installer, or ready to swap in
  bool quit = false;  // the app must end for it (the Mac's helper waits)
  std::string why;    // for the log when it failed (never a link or a path)
  // On screen in place of the usual "Couldn't update" line (the Mac: a
  // folder it may not change). Empty: the usual.
  std::string message;
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
  // Nothing needs the CPU this moment (no key pressed lately, no channel
  // starting): the time to hand freed memory back. Unset counts as idle.
  std::function<bool()> idle;
  // 1.0.1: ms() of the last touch (a key, the pointer, the wheel, a swipe,
  // or the app shown again). Live Events' scores slow down while it's long
  // ago. Unset: always touched.
  std::function<uint32_t()> lastTouchMs;
  // For the channel report: whether a channel's logo is on screen.
  std::function<std::string(const std::string& logoUrl)> logoStatus;
  // Every request was cancelled (a provider change): image loads in flight
  // won't call back.
  std::function<void()> requestsCancelled;
  // The app's build number, for the report.
  std::string build;
  // Free bytes on the partition holding a folder (disk.hpp); unset: the
  // real statvfs. Tests set it to try a nearly full partition.
  std::function<std::optional<uint64_t>(const std::string& dir)> freeSpace;

  // Build 148, updates (updates.hpp): only a device that installs the app
  // by itself can (Android and Fire TV, the Mac). The release file it
  // installs ("marigold-android.apk", "marigold-mac.dmg"), where its download
  // goes (the app's cache), and, on a worker, the hand-over of the checked
  // file. Unset (webOS, the Apple TV): no check, no request, nothing shown.
  std::string updateAsset;
  std::string updateFile;
  std::function<UpdateInstall(const std::string& path)> installUpdate;
  bool canSelfUpdate() const { return !updateAsset.empty() && !updateFile.empty() && installUpdate != nullptr; }
};

}  // namespace marigold::engine
