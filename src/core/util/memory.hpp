// The app's memory use, for the playback log and Help (webOS restarts an app
// that uses too much), and handing freed memory back after a big job.
#pragma once

#include <cstdint>
#include <string>

namespace marigold::memory {

struct Reading {
  int64_t rssKB = 0;   // in use now (resident)
  int64_t peakKB = 0;  // most since the app started
};

// From /proc/self/status; zeros where it can't be read.
Reading read();
// "212 MB (peak 340 MB)"; empty for a zero reading.
std::string describe(const Reading& r);
// Gives memory freed by a big job (a channel sort, a guide build) back to
// the system; without it the app keeps its high-water mark.
void release();

}  // namespace marigold::memory
