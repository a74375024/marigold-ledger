#include "platform/files_posix.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace marigold::platform::files {

std::optional<std::string> read(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool writeAtomic(const std::string& target, const std::string& content, bool secret) {
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

}  // namespace marigold::platform::files
