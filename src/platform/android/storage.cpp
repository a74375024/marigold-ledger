// Android: the data folder is "data" in the app's own files directory
// (Context.getFilesDir(), private to the app). Writes are atomic: a
// temporary file, fsync, then rename over the old one.
#include "platform/storage.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <fstream>
#include <sstream>

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
  std::ifstream in(path(name), std::ios::binary);
  if (!in) return std::nullopt;
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool Storage::write(const std::string& name, const std::string& content, bool secret) {
  if (dir_.empty()) return false;
  std::string target = path(name);
  std::string temp = target + ".tmp";
  int fd = open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, secret ? 0600 : 0644);
  if (fd < 0) return false;
  size_t done = 0;
  while (done < content.size()) {
    ssize_t n = ::write(fd, content.data() + done, content.size() - done);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) {
      close(fd);
      unlink(temp.c_str());
      return false;
    }
    done += static_cast<size_t>(n);
  }
  bool ok = fsync(fd) == 0;
  ok = close(fd) == 0 && ok;
  if (!ok || std::rename(temp.c_str(), target.c_str()) != 0) {
    unlink(temp.c_str());
    return false;
  }
  return true;
}

bool Storage::remove(const std::string& name) { return !dir_.empty() && unlink(path(name).c_str()) == 0; }

}  // namespace marigold::platform
