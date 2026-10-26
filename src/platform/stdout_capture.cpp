#include "platform/stdout_capture.hpp"

#include <unistd.h>

#include <cstdio>
#include <string>
#include <thread>

#include "core/util/log.hpp"

namespace marigold::platform {
namespace {

int gReadFd = -1;
std::thread gReader;

void readLines(int fd) {
  std::string pending;
  char buf[1024];
  while (true) {
    ssize_t n = read(fd, buf, sizeof buf);
    if (n <= 0) break;
    pending.append(buf, static_cast<size_t>(n));
    size_t start = 0, nl;
    while ((nl = pending.find('\n', start)) != std::string::npos) {
      std::string line = pending.substr(start, nl - start);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (!line.empty()) MLOG_I("legacy", "%s", line.c_str());
      start = nl + 1;
    }
    pending.erase(0, start);
    // A line with no newline in sight is logged in pieces rather than kept.
    if (pending.size() > 4096) {
      MLOG_I("legacy", "%s", pending.c_str());
      pending.clear();
    }
  }
  if (!pending.empty()) MLOG_I("legacy", "%s", pending.c_str());
}

}  // namespace

bool captureStdout() {
  int fds[2];
  if (pipe(fds) != 0) return false;
  std::fflush(stdout);
  std::fflush(stderr);
  if (dup2(fds[1], STDOUT_FILENO) < 0 || dup2(fds[1], STDERR_FILENO) < 0) {
    close(fds[0]);
    close(fds[1]);
    return false;
  }
  close(fds[1]);
  setvbuf(stdout, nullptr, _IOLBF, 0);
  gReadFd = fds[0];
  gReader = std::thread(readLines, gReadFd);
  return true;
}

void releaseStdout() {
  if (gReadFd < 0) return;
  std::fflush(stdout);
  std::fflush(stderr);
  // Closing the write ends ends the reader's read() with EOF.
  close(STDOUT_FILENO);
  close(STDERR_FILENO);
  if (gReader.joinable()) gReader.join();
  close(gReadFd);
  gReadFd = -1;
}

}  // namespace marigold::platform
