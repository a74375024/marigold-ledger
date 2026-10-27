// Android: the data folder is "data" in the app's own files directory
// (Context.getFilesDir(), private to the app). Writes are atomic.
#include "platform/storage.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>

#include "platform/files_posix.hpp"

namespace marigold::platform {

void Storage::init(const std::string& appDir) {
  std::string dir = appDir + "/data";
  if (mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) return;
  dir_ = dir;
}

std::string Storage::path(const std::string& name) const {
  return dir_.empty() ? std::string() : dir_ + "/" + name;
}

std::optional<std::string> Storage::read(const std::string& name) const {
  if (dir_.empty()) return std::nullopt;
  return files::read(path(name));
}

bool Storage::write(const std::string& name, const std::string& content, bool secret) {
  return !dir_.empty() && files::writeAtomic(path(name), content, secret);
}

bool Storage::remove(const std::string& name) { return !dir_.empty() && unlink(path(name).c_str()) == 0; }

}  // namespace marigold::platform
