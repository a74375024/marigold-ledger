// The data folder on disk: what may be deleted, when a big write is skipped
// for lack of space, and how much the folder may hold.
//
// On webOS every developer-mode app shares one small partition (2.7 GB on an
// LG C1). When it fills, the TV can no longer install or update any app, so
// the app keeps its folder in a budget, never writes core dumps there (see
// app/crash_note.hpp), cleans up what a crash or an older build left behind,
// and skips a big write that would leave the partition nearly full (the data
// stays in memory and the old file stays as it was). Android and Apple TV
// have sandboxes of their own and get the same rules with their own numbers.
//
// The decisions (names, room, the prune plan) are plain functions for the
// tests; the I/O around them is small and below them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace marigold::engine::disk {

constexpr uint64_t kMB = 1024 * 1024;

struct Limits {
  // The whole data folder (caches, guides, logos, logs).
  uint64_t budget;
  // Free space a big write must leave on the partition, besides itself.
  uint64_t margin;
  // Writes from this size up look at the free space first.
  uint64_t bigWrite;
  // A streamed write (its size unknown up front) looks again this often.
  uint64_t recheckEvery;
};

// The numbers for each platform, in one place.
#if defined(__ANDROID__)
inline constexpr Limits kLimits{400 * kMB, 50 * kMB, 1 * kMB, 4 * kMB};
#elif defined(__APPLE__)
inline constexpr Limits kLimits{300 * kMB, 50 * kMB, 1 * kMB, 4 * kMB};
#else  // webOS (and the host tests)
inline constexpr Limits kLimits{400 * kMB, 100 * kMB, 1 * kMB, 4 * kMB};
#endif
// The app log while running: rotated to "<name>.1" past this (log::open).
inline constexpr size_t kLogBytes = 1u << 20;
// The folders inside the data folder that hold cache files.
inline constexpr const char* kGuidesDir = "guides";
inline constexpr const char* kLogosDir = "logos";

// MARK: Names

// A core dump: "core" or "core.<pid>" ("corefile", "core.txt" are not).
bool isCoreDump(std::string_view name);
// A write that never finished: "<name>.tmp", "<name>.tmp<n>", "<name>.part".
bool isStaleTemp(std::string_view name);

// What a file in the data folder is, by its name ("guides/us-locals" for a
// file in a subfolder). Only these families are ever removed by the prune.
enum class Family {
  other,     // settings, accounts, lists, logs, small caches: never removed
  catalog,   // "catalog-<hash>-first.json", "catalog-<hash>-full.json"
  listings,  // "listings-<signature>.json": outside listings matched
  provider,  // "provider-<key>.json": a provider's guide, matched
  guide,     // "guides/<source>": a downloaded outside guide file
  logo,      // "logos/<hash>": a logo as downloaded
};
Family familyOf(std::string_view name);

// MARK: Decisions

// Whether a write of `bytes` may start when the partition has `freeBytes`
// free: it must leave `margin` free after it. Unknown free space allows it.
bool roomFor(std::optional<uint64_t> freeBytes, uint64_t bytes, const Limits& limits = kLimits);

struct Entry {
  std::string name;  // relative to the data folder ("guides/us", "logos/ab12")
  uint64_t bytes = 0;
  int64_t mtime = 0;  // seconds
};

// The cache files that still have a use (the store knows them).
struct Owners {
  // False when the owners can't be told (an unreadable accounts file):
  // nothing is removed for having no owner then.
  bool known = false;
  // The catalog, listings, provider and guide files in use.
  std::set<std::string> keep;
  // Of those, the ones only used as a fallback (backup guide sources): the
  // first owned files to go when the folder is over budget, after logos.
  std::set<std::string> spare;
};

struct Plan {
  std::vector<std::string> remove;  // in this order
  uint64_t before = 0;              // the folder's size now
  uint64_t after = 0;               // once `remove` is gone
  size_t unowned = 0;               // of `remove`, the files with no owner
  bool over = false;                // still over budget after it
};

// What to remove: first every catalog, listings, provider and guide file
// with no owner (a removed provider's guide, a guide area no longer chosen,
// an old channel list); then, while the folder is over `budget`, logos
// least recently used first, then spare guide files oldest first. Files in
// use and everything else (settings, accounts, logs) stay.
Plan plan(const std::vector<Entry>& files, const Owners& owners, uint64_t budget);

// MARK: I/O

// Free bytes for the app on the partition holding `dir` (statvfs).
std::optional<uint64_t> freeBytes(const std::string& dir);
// The regular files in `dir` and its guides/ and logos/ folders (dot files
// left out).
std::vector<Entry> list(const std::string& dir);

struct Swept {
  size_t files = 0;
  uint64_t bytes = 0;
};
// Removes core dumps directly in `dir`.
Swept removeCoreDumps(const std::string& dir);
// Removes unfinished writes in `dir` and its guides/ and logos/ folders.
// Only at launch, before anything writes.
Swept removeStaleTemps(const std::string& dir);

// The store's keeper of the data folder, shared with its workers.
class Keeper {
 public:
  using FreeSpace = std::function<std::optional<uint64_t>(const std::string& dir)>;
  // `freeSpace` unset: freeBytes(). `dir` empty: nothing on disk, every
  // call allows and removes nothing.
  explicit Keeper(std::string dir, FreeSpace freeSpace = {}, Limits limits = kLimits);

  const Limits& limits() const { return limits_; }

  // A write of about `bytes` to `path` may start. Under limits().bigWrite
  // it always may; above, it may when roomFor() says so (else it is
  // skipped and logged, once per file per session).
  bool roomFor(const std::string& path, uint64_t bytes);
  // During a streamed write to `path`: false (logged once) when the
  // partition is down to the margin. Call it every limits().recheckEvery.
  bool stillRoom(const std::string& path);
  // A write to `path` failed with `err` (errno): logged once per file per
  // session.
  void failed(const std::string& path, int err);

  // Removes what plan() says. Logs what went, and once a session when the
  // folder stays over budget.
  Plan prune(const Owners& owners);

 private:
  bool once(const std::string& key);
  std::string nameOf(const std::string& path) const;

  std::string dir_;
  FreeSpace freeSpace_;
  Limits limits_;
  std::mutex logMu_;    // logged_, overLogged_
  std::mutex pruneMu_;  // one prune at a time
  std::set<std::string> logged_;
  bool overLogged_ = false;
};

}  // namespace marigold::engine::disk
