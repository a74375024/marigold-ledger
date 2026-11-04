#include "core/engine/disk.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <utility>

#include "core/util/log.hpp"

namespace marigold::engine::disk {
namespace {

bool allDigits(std::string_view s) {
  return std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

bool startsWith(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.substr(0, p.size()) == p; }
bool endsWith(std::string_view s, std::string_view p) {
  return s.size() >= p.size() && s.substr(s.size() - p.size()) == p;
}

bool removable(Family f) {
  return f == Family::catalog || f == Family::listings || f == Family::provider || f == Family::guide;
}

std::string mbText(uint64_t bytes) {
  uint64_t tenths = (bytes * 10 + kMB / 2) / kMB;
  return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " MB";
}

const char* errorText(int err) {
  switch (err) {
    case ENOSPC: return "the storage is full";
#ifdef EDQUOT
    case EDQUOT: return "over the storage quota";
#endif
    case EROFS: return "the storage is read-only";
    case EACCES:
    case EPERM: return "not allowed";
    case EIO: return "a storage error";
    default: return "an error";
  }
}

// Regular files directly in `dir`, named relative to the data folder with
// `prefix` ("" or "guides/").
void listInto(const std::string& dir, const std::string& prefix, std::vector<Entry>& out) {
  DIR* d = opendir(dir.c_str());
  if (!d) return;
  while (dirent* ent = readdir(d)) {
    if (ent->d_name[0] == '.') continue;
    std::string path = dir + "/" + ent->d_name;
    struct stat st;
    if (lstat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
    Entry e;
    e.name = prefix + ent->d_name;
    e.bytes = static_cast<uint64_t>(st.st_size);
    e.mtime = static_cast<int64_t>(st.st_mtime);
    out.push_back(std::move(e));
  }
  closedir(d);
}

template <class Match>
Swept removeMatching(const std::string& dir, Match match) {
  Swept s;
  std::vector<Entry> files;
  listInto(dir, "", files);
  for (const Entry& e : files) {
    if (!match(e.name)) continue;
    if (unlink((dir + "/" + e.name).c_str()) == 0) {
      ++s.files;
      s.bytes += e.bytes;
    }
  }
  return s;
}

}  // namespace

bool isCoreDump(std::string_view name) {
  if (name == "core") return true;
  return startsWith(name, "core.") && name.size() > 5 && allDigits(name.substr(5));
}

bool isStaleTemp(std::string_view name) {
  if (endsWith(name, ".part")) return name.size() > 5;
  size_t at = name.rfind(".tmp");
  return at != std::string_view::npos && at > 0 && allDigits(name.substr(at + 4));
}

Family familyOf(std::string_view name) {
  size_t slash = name.find('/');
  if (slash != std::string_view::npos) {
    std::string_view folder = name.substr(0, slash), base = name.substr(slash + 1);
    if (base.empty() || base.find('/') != std::string_view::npos || base[0] == '.' || isStaleTemp(base))
      return Family::other;
    if (folder == kGuidesDir) return Family::guide;
    if (folder == kLogosDir) return Family::logo;
    return Family::other;
  }
  if (isStaleTemp(name)) return Family::other;
  if (startsWith(name, "catalog-") && (endsWith(name, "-first.json") || endsWith(name, "-full.json")))
    return Family::catalog;
  if (startsWith(name, "listings-") && endsWith(name, ".json")) return Family::listings;
  if (startsWith(name, "provider-") && endsWith(name, ".json")) return Family::provider;
  return Family::other;
}

bool roomFor(std::optional<uint64_t> freeBytes, uint64_t bytes, const Limits& limits) {
  if (!freeBytes) return true;
  return *freeBytes >= bytes && *freeBytes - bytes >= limits.margin;
}

Plan plan(const std::vector<Entry>& files, const Owners& owners, uint64_t budget) {
  Plan p;
  for (const Entry& e : files) p.before += e.bytes;
  uint64_t total = p.before;
  std::set<std::string> gone;
  auto drop = [&](const Entry& e) {
    p.remove.push_back(e.name);
    gone.insert(e.name);
    total -= std::min(total, e.bytes);
  };
  if (owners.known) {
    for (const Entry& e : files) {
      if (removable(familyOf(e.name)) && !owners.keep.count(e.name)) {
        drop(e);
        ++p.unowned;
      }
    }
  }
  // Over budget: the least recently used of a kind first.
  auto oldestFirst = [&](auto match) {
    std::vector<const Entry*> pick;
    for (const Entry& e : files)
      if (!gone.count(e.name) && match(e)) pick.push_back(&e);
    std::stable_sort(pick.begin(), pick.end(), [](const Entry* a, const Entry* b) { return a->mtime < b->mtime; });
    for (const Entry* e : pick) {
      if (total <= budget) break;
      drop(*e);
    }
  };
  if (total > budget) oldestFirst([](const Entry& e) { return familyOf(e.name) == Family::logo; });
  if (total > budget)
    oldestFirst([&](const Entry& e) { return familyOf(e.name) == Family::guide && owners.spare.count(e.name) > 0; });
  p.after = total;
  p.over = total > budget;
  return p;
}

std::optional<uint64_t> freeBytes(const std::string& dir) {
  struct statvfs st;
  if (dir.empty() || statvfs(dir.c_str(), &st) != 0) return std::nullopt;
  return static_cast<uint64_t>(st.f_bavail) * static_cast<uint64_t>(st.f_frsize);
}

std::vector<Entry> list(const std::string& dir) {
  std::vector<Entry> out;
  if (dir.empty()) return out;
  listInto(dir, "", out);
  listInto(dir + "/" + kGuidesDir, std::string(kGuidesDir) + "/", out);
  listInto(dir + "/" + kLogosDir, std::string(kLogosDir) + "/", out);
  return out;
}

Swept removeCoreDumps(const std::string& dir) {
  if (dir.empty()) return {};
  return removeMatching(dir, [](const std::string& name) { return isCoreDump(name); });
}

Swept removeStaleTemps(const std::string& dir) {
  Swept all;
  if (dir.empty()) return all;
  for (const std::string& d : {dir, dir + "/" + kGuidesDir, dir + "/" + kLogosDir}) {
    Swept s = removeMatching(d, [](const std::string& name) { return isStaleTemp(name); });
    all.files += s.files;
    all.bytes += s.bytes;
  }
  return all;
}

// ------------------------------------------------------------ Keeper

Keeper::Keeper(std::string dir, FreeSpace freeSpace, Limits limits)
    : dir_(std::move(dir)), freeSpace_(std::move(freeSpace)), limits_(limits) {}

bool Keeper::once(const std::string& key) {
  std::lock_guard<std::mutex> lock(logMu_);
  return logged_.insert(key).second;
}

std::string Keeper::nameOf(const std::string& path) const {
  if (!dir_.empty() && path.size() > dir_.size() + 1 && path.compare(0, dir_.size(), dir_) == 0 &&
      path[dir_.size()] == '/')
    return path.substr(dir_.size() + 1);
  size_t slash = path.rfind('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool Keeper::roomFor(const std::string& path, uint64_t bytes) {
  if (dir_.empty() || bytes < limits_.bigWrite) return true;
  std::optional<uint64_t> free = freeSpace_ ? freeSpace_(dir_) : freeBytes(dir_);
  if (disk::roomFor(free, bytes, limits_)) return true;
  std::string name = nameOf(path);
  if (once("room:" + name))
    MLOG_W("disk", "%s not saved: %s free, it needs about %s and %s to spare (kept in memory)", name.c_str(),
           mbText(*free).c_str(), mbText(bytes).c_str(), mbText(limits_.margin).c_str());
  return false;
}

bool Keeper::stillRoom(const std::string& path) {
  if (dir_.empty()) return true;
  std::optional<uint64_t> free = freeSpace_ ? freeSpace_(dir_) : freeBytes(dir_);
  if (disk::roomFor(free, 0, limits_)) return true;
  std::string name = nameOf(path);
  if (once("room:" + name))
    MLOG_W("disk", "%s not saved: the storage is down to %s free (the old copy is kept)", name.c_str(),
           mbText(*free).c_str());
  return false;
}

void Keeper::failed(const std::string& path, int err) {
  std::string name = nameOf(path);
  if (once("failed:" + name)) MLOG_W("disk", "%s not saved: %s (the old copy is kept)", name.c_str(), errorText(err));
}

Plan Keeper::prune(const Owners& owners) {
  std::lock_guard<std::mutex> lock(pruneMu_);
  if (dir_.empty()) return {};
  Plan p = plan(list(dir_), owners, limits_.budget);
  size_t removed = 0;
  for (const std::string& name : p.remove)
    if (unlink((dir_ + "/" + name).c_str()) == 0) ++removed;
  if (removed)
    MLOG_I("disk", "data folder %s, now %s: %zu file(s) removed (%zu with no owner)", mbText(p.before).c_str(),
           mbText(p.after).c_str(), removed, p.unowned);
  if (p.over) {
    bool first;
    {
      std::lock_guard<std::mutex> log(logMu_);
      first = !overLogged_;
      overLogged_ = true;
    }
    if (first)
      MLOG_W("disk", "data folder %s, over its %s budget (what is left is in use)", mbText(p.after).c_str(),
             mbText(limits_.budget).c_str());
  }
  return p;
}

}  // namespace marigold::engine::disk
