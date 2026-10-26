#include "core/util/memory.hpp"

#include <fstream>
#include <sstream>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

namespace marigold::memory {

Reading read() {
  Reading r;
  std::ifstream f("/proc/self/status");
  std::string line;
  while (std::getline(f, line)) {
    std::istringstream in(line);
    std::string key;
    int64_t kb = 0;
    in >> key >> kb;
    if (key == "VmRSS:") r.rssKB = kb;
    else if (key == "VmHWM:") r.peakKB = kb;
  }
  return r;
}

std::string describe(const Reading& r) {
  if (r.rssKB <= 0) return "";
  std::string out = std::to_string((r.rssKB + 512) / 1024) + " MB";
  if (r.peakKB > 0) out += " (peak " + std::to_string((r.peakKB + 512) / 1024) + " MB)";
  return out;
}

void configure() {
#if defined(__GLIBC__)
  mallopt(M_ARENA_MAX, 2);
#endif
}

void release() {
#if defined(__GLIBC__)
  malloc_trim(0);
#endif
}

}  // namespace marigold::memory
