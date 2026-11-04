// The crash note: what the app's fatal-signal handler (app/crash_note.cpp)
// writes to crash.txt as the app dies, read back on the next launch. Only
// the signal, the build, the uptime, the reason std::terminate gave, a few
// registers and the raw backtrace (addresses and symbol names): never a
// link, a name or a password.
//
//   Marigold crashed: SIGSEGV (code 1, address 0x2c)
//   build: Marigold 0.1.0 build 131
//   uptime: 5423 s
//   reason: uncaught exception St9bad_alloc: std::bad_alloc
//   pc: 0x4a5b6c lr: 0x4a5b00 base: 0x10000
//   backtrace:
//   /media/developer/apps/.../marigold(+0x1a2b3c)[0x2a2b3c]
//   ...
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace marigold::report {

struct CrashNote {
  std::string signal;  // "SIGSEGV"
  std::string detail;  // "code 1, address 0x2c"
  std::string build;   // "Marigold 0.1.0 build 131"
  std::string uptime;  // "5423 s"
  std::string reason;  // std::terminate's, if any
  std::string registers;
  std::vector<std::string> frames;
};

// The first line's prefix.
inline constexpr std::string_view kCrashNoteHead = "Marigold crashed: ";

// Reads a note; nullopt when the text is not one.
std::optional<CrashNote> parseCrashNote(std::string_view text);
// One line for the playback log and Diagnostics, at most `maxBytes`:
// "SIGSEGV after 5423 s (Marigold 0.1.0 build 131); uncaught exception ...;
// marigold(+0x1a2b3c) < marigold(+0x1a0000) < ..." (frames without their
// folder, as many as fit).
std::string crashSummary(const CrashNote& note, size_t maxBytes = 480);

}  // namespace marigold::report
