// The crash note read back on the next launch (crash_note.hpp).
#include <doctest.h>

#include <string>

#include "core/report/crash_note.hpp"

using namespace marigold::report;

namespace {

const char* kNote =
    "Marigold crashed: SIGSEGV (code 1, address 0x2c)\n"
    "build: Marigold 0.1.0 build 131\n"
    "uptime: 5423 s\n"
    "pc: 0x4a5b6c lr: 0x4a5b00 base: 0x10000\n"
    "backtrace:\n"
    "/media/developer/apps/usr/palm/applications/org.example.app/marigold(+0x1a2b3c)[0x1b2b3c]\n"
    "/media/developer/apps/usr/palm/applications/org.example.app/marigold(_ZN8marigold5guide4readEv+0x20)[0x1b0000]\n"
    "/lib/libc.so.6(__libc_start_main+0x114)[0xb6e01234]\n";

}  // namespace

TEST_CASE("crash note: read back") {
  auto n = parseCrashNote(kNote);
  REQUIRE(n);
  CHECK(n->signal == "SIGSEGV");
  CHECK(n->detail == "code 1, address 0x2c");
  CHECK(n->build == "Marigold 0.1.0 build 131");
  CHECK(n->uptime == "5423 s");
  CHECK(n->reason.empty());
  CHECK(n->registers == "0x4a5b6c lr: 0x4a5b00 base: 0x10000");
  REQUIRE(n->frames.size() == 3);
  CHECK(n->frames[2] == "/lib/libc.so.6(__libc_start_main+0x114)[0xb6e01234]");
}

TEST_CASE("crash note: the summary is one short line with the first frames, no folders") {
  auto n = parseCrashNote(kNote);
  REQUIRE(n);
  std::string s = crashSummary(*n);
  CHECK(s.rfind("SIGSEGV after 5423 s (Marigold 0.1.0 build 131); marigold(+0x1a2b3c) < ", 0) == 0);
  CHECK(s.find("/media") == std::string::npos);
  CHECK(s.find("[0x") == std::string::npos);
  CHECK(s.find("libc.so.6(__libc_start_main+0x114)") != std::string::npos);
  std::string shortOne = crashSummary(*n, 60);
  CHECK(shortOne.size() <= 60);
  CHECK(shortOne.rfind("SIGSEGV after 5423 s", 0) == 0);
}

TEST_CASE("crash note: an uncaught exception's reason, a link in it scrubbed") {
  std::string note =
      "Marigold crashed: SIGABRT (code -6)\n"
      "build: Marigold 0.1.0 build 131\n"
      "uptime: 12 s\n"
      "reason: uncaught exception St9bad_alloc: std::bad_alloc\n"
      "backtrace:\n"
      "(not on this platform)\n";
  auto n = parseCrashNote(note);
  REQUIRE(n);
  CHECK(n->signal == "SIGABRT");
  CHECK(n->reason == "uncaught exception St9bad_alloc: std::bad_alloc");
  CHECK(crashSummary(*n).find("St9bad_alloc") != std::string::npos);

  auto linked = parseCrashNote(
      "Marigold crashed: SIGABRT (code -6)\nreason: uncaught exception x: open http://example.invalid/live/u/p/1.ts\n");
  REQUIRE(linked);
  CHECK(linked->reason.find("example.invalid") == std::string::npos);
}

TEST_CASE("crash note: anything else is not one") {
  CHECK_FALSE(parseCrashNote(""));
  CHECK_FALSE(parseCrashNote("hello\n"));
  CHECK_FALSE(parseCrashNote("Marigold crashed: \n"));
  // Cut short by the crash itself: what is there still reads.
  auto cut = parseCrashNote("Marigold crashed: SIGBUS (code 2, addr");
  REQUIRE(cut);
  CHECK(cut->signal == "SIGBUS");
}
