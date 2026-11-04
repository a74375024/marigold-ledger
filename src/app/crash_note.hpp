// What killed the app, without a core dump.
//
// A core dump holds the whole process (hundreds of MB, with provider logins
// and stream links in it) and on webOS lands in the shared app partition, so
// the app turns them off (RLIMIT_CORE 0) and writes a short note instead: on
// SIGSEGV, SIGBUS, SIGABRT, SIGILL or SIGFPE a handler on its own stack
// writes the signal, the build, the uptime and the raw backtrace (addresses
// and symbols only; core/report/crash_note.hpp has the form) to the log and
// to crash.txt in the data folder, then hands the signal on to the handler
// that was there before (the system's), so the app still ends as it would
// have. An uncaught exception names its type and what() first.
//
// The next launch logs "previous run crashed: <signal>" with the note, keeps
// it one more launch as crash-previous.txt, and puts a one-line summary in
// the playback log (Help's export) and on the Diagnostics screen.
#pragma once

#include <string>

namespace marigold::app::crash {

// First thing in prepareProcess, before any thread starts and before
// Legacy's IME guard (entrada_texto.c) saves the handlers it chains to:
// the fatal signal handlers on an alternate stack, std::terminate's.
void install();

// Once the data folder and its log are open: the note the previous run left
// (logged, then renamed crash-previous.txt; the one before that removed),
// and where this run's goes.
void openNotes(const std::string& dataDir);

// The previous run's crash, for Help and Diagnostics: "" when it ended
// normally (or left no note).
const std::string& previousSignal();
// One line: signal, uptime, build, reason, the first frames.
const std::string& previousSummary();

}  // namespace marigold::app::crash
