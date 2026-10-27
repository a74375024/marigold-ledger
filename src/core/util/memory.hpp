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

// From /proc/self/status (Apple: the task's memory footprint); zeros where
// it can't be read.
Reading read();
// "212 MB (peak 340 MB)"; empty for a zero reading.
std::string describe(const Reading& r);
// At startup, before any thread: at most two malloc arenas. With one per
// thread (the default, up to 8 per core), long-lived listings end up pinned
// among freed pages in many arenas and the app keeps its peak; one arena
// mixes them worse still. Measured on the guide build plus a channel-list
// save on three threads: 135 MB resident after release() with the default,
// 88-96 MB with two, 150-163 MB with one.
void configure();
// Gives memory freed by a big job (a channel sort, a guide build) back to
// the system; without it the app keeps its high-water mark.
void release();

}  // namespace marigold::memory
