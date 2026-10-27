#include "core/util/memory.hpp"

#include <fstream>
#include <sstream>

#if defined(__GLIBC__)
#include <malloc.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <malloc/malloc.h>
#endif

namespace marigold::memory {

Reading read() {
  Reading r;
#if defined(__APPLE__)
  // The footprint is what the system counts against an app's memory limit.
  task_vm_info_data_t vm{};
  mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&vm), &count) == KERN_SUCCESS) {
    r.rssKB = static_cast<int64_t>(vm.phys_footprint / 1024);
    if (count >= TASK_VM_INFO_REV4_COUNT) r.peakKB = static_cast<int64_t>(vm.ledger_phys_footprint_peak / 1024);
  }
  return r;
#endif
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
#elif defined(__APPLE__)
  malloc_zone_pressure_relief(nullptr, 0);
#endif
}

}  // namespace marigold::memory
