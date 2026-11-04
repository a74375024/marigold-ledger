#include "app/crash_note.hpp"

#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <optional>
#include <typeinfo>

// The raw backtrace: glibc's (webOS). Bionic only has it from Android 13 and
// Apple keeps its own crash reports, so there the note has no frames.
#if defined(__GLIBC__)
#include <execinfo.h>
#include <link.h>
#include <ucontext.h>
#define MARIGOLD_CRASH_FRAMES 1
#endif
#if __has_include(<cxxabi.h>)
#include <cxxabi.h>
#define MARIGOLD_CRASH_CXXABI 1
#endif

#include "core/report/crash_note.hpp"
#include "core/util/log.hpp"

#ifndef MARIGOLD_VERSION
#define MARIGOLD_VERSION "0.0.0"
#endif
#ifndef MARIGOLD_BUILD
#define MARIGOLD_BUILD "dev"
#endif

namespace marigold::app::crash {
namespace {

constexpr int kSignals[] = {SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE};
constexpr int kSignalCount = static_cast<int>(std::size(kSignals));
constexpr size_t kNoteMax = 64 * 1024;  // crash.txt, at most
constexpr size_t kAltStackBytes = 64 * 1024;
#if defined(MARIGOLD_CRASH_FRAMES)
constexpr int kFrames = 48;  // a few KB of lines
uintptr_t gBase = 0;         // where the executable is loaded (its frames' offsets are from here)
#endif

// What the handler reads. A signal handler may not allocate or take a lock,
// so these are fixed buffers, filled before it can run (install() and
// openNotes(), at start-up) and only read after.
char gNotePath[1024];
char gBuild[160];
char gReason[400];
volatile sig_atomic_t gNoteReady = 0;
volatile sig_atomic_t gReasonReady = 0;
volatile sig_atomic_t gInHandler = 0;
struct timespec gStart;
struct sigaction gPrevious[kSignalCount];

// The previous run's note (UI thread, after openNotes()).
std::string gPreviousSignal, gPreviousSummary;

// write(2) to the log and the note: async-signal-safe, no allocation.
struct Out {
  int fds[2] = {-1, -1};

  void put(const char* s, size_t n) const {
    for (int fd : fds) {
      if (fd < 0) continue;
      size_t done = 0;
      while (done < n) {
        ssize_t w = ::write(fd, s + done, n - done);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) break;
        done += static_cast<size_t>(w);
      }
    }
  }
  void str(const char* s) const {
    size_t n = 0;
    while (s[n]) ++n;
    put(s, n);
  }
  void dec(uint64_t v) const {
    char b[24];
    size_t i = sizeof b;
    do {
      b[--i] = static_cast<char>('0' + v % 10);
      v /= 10;
    } while (v && i > 0);
    put(b + i, sizeof b - i);
  }
  void sdec(long v) const {
    if (v < 0) {
      str("-");
      dec(static_cast<uint64_t>(-(v + 1)) + 1);
    } else {
      dec(static_cast<uint64_t>(v));
    }
  }
  void hex(uintptr_t v) const {
    char b[2 + 2 * sizeof(uintptr_t)];
    size_t i = sizeof b;
    do {
      b[--i] = "0123456789abcdef"[v & 15];
      v >>= 4;
    } while (v && i > 2);
    b[--i] = 'x';
    b[--i] = '0';
    put(b + i, sizeof b - i);
  }
};

const char* signalName(int sig) {
  switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS: return "SIGBUS";
    case SIGABRT: return "SIGABRT";
    case SIGILL: return "SIGILL";
    case SIGFPE: return "SIGFPE";
    default: return "signal";
  }
}

int slotOf(int sig) {
  for (int i = 0; i < kSignalCount; ++i)
    if (kSignals[i] == sig) return i;
  return -1;
}

// The signal goes on to the handler that was there before (the system's:
// the default, or Android's crash reporter). One sent with kill() or
// abort() is raised again (it is blocked until this handler returns); a
// fault happens again when the faulting instruction runs again.
void handOn(int sig, const siginfo_t* info) {
  int slot = slotOf(sig);
  struct sigaction previous;
  if (slot >= 0) previous = gPrevious[slot];
  else std::memset(&previous, 0, sizeof previous);
  if (slot < 0 || (!(previous.sa_flags & SA_SIGINFO) && previous.sa_handler == SIG_IGN)) {
    std::memset(&previous, 0, sizeof previous);
    previous.sa_handler = SIG_DFL;
  }
  sigaction(sig, &previous, nullptr);
  if (!info || info->si_code <= 0 || sig == SIGABRT) raise(sig);
}

void registers(const Out& out, void* context) {
#if defined(MARIGOLD_CRASH_FRAMES) && (defined(__arm__) || defined(__aarch64__) || defined(__x86_64__))
  if (!context) return;
  const ucontext_t* uc = static_cast<const ucontext_t*>(context);
  // Where it stopped and (ARM) where that was called from; with the
  // executable's base, addr2line finds the lines.
#if defined(__arm__)
  uintptr_t pc = uc->uc_mcontext.arm_pc, lr = uc->uc_mcontext.arm_lr;
#elif defined(__aarch64__)
  uintptr_t pc = uc->uc_mcontext.pc, lr = uc->uc_mcontext.regs[30];
#else
  uintptr_t pc = static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RIP]);
#endif
  out.str("pc: ");
  out.hex(pc);
#if defined(__arm__) || defined(__aarch64__)
  out.str(" lr: ");
  out.hex(lr);
#endif
  out.str(" base: ");
  out.hex(gBase);
  out.str("\n");
#else
  (void)out;
  (void)context;
#endif
}

void onSignal(int sig, siginfo_t* info, void* context) {
  if (gInHandler) {  // a fault while the note was written: no second one
    handOn(sig, info);
    return;
  }
  gInHandler = 1;
  Out out;
  out.fds[0] = log::fileDescriptor();
  if (gNoteReady) out.fds[1] = ::open(gNotePath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);

  out.put(report::kCrashNoteHead.data(), report::kCrashNoteHead.size());
  out.str(signalName(sig));
  if (info) {
    out.str(" (code ");
    out.sdec(info->si_code);
    if ((sig == SIGSEGV || sig == SIGBUS) && info->si_code > 0) {  // a real fault's address
      out.str(", address ");
      out.hex(reinterpret_cast<uintptr_t>(info->si_addr));
    }
    out.str(")");
  }
  out.str("\nbuild: ");
  out.str(gBuild);
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) == 0) {
    out.str("\nuptime: ");
    out.dec(static_cast<uint64_t>(now.tv_sec > gStart.tv_sec ? now.tv_sec - gStart.tv_sec : 0));
    out.str(" s");
  }
  out.str("\n");
  if (gReasonReady) {
    out.str("reason: ");
    out.str(gReason);
    out.str("\n");
  }
  registers(out, context);
  out.str("backtrace:\n");
#if defined(MARIGOLD_CRASH_FRAMES)
  // From the signal's frame on (this handler's own left out).
  void* frames[kFrames];
  int n = backtrace(frames, kFrames);
  for (int fd : out.fds)
    if (fd >= 0 && n > 1) backtrace_symbols_fd(frames + 1, n - 1, fd);
#else
  out.str("(not on this platform)\n");
#endif

  int note = out.fds[1];
  if (note >= 0) {
    off_t size = lseek(note, 0, SEEK_END);
    if (size > static_cast<off_t>(kNoteMax)) {
      int cut = ftruncate(note, static_cast<off_t>(kNoteMax));
      (void)cut;
    }
    fsync(note);
    ::close(note);
  }
  handOn(sig, info);
}

// Appends to a fixed buffer, always ended with a NUL.
struct Fixed {
  char* buf;
  size_t cap;
  size_t len = 0;

  void add(const char* s, size_t n) {
    size_t room = cap - 1 - len;
    if (n > room) n = room;
    std::memcpy(buf + len, s, n);
    len += n;
    buf[len] = '\0';
  }
  void add(const char* s) { add(s, std::strlen(s)); }
  // A message as words; a word that could be a link or a login
  // ("://", "@", "=") becomes "[link]".
  void addScrubbed(const char* s) {
    size_t i = 0;
    while (s[i]) {
      size_t start = i;
      while (s[i] && s[i] != ' ' && s[i] != '\n' && s[i] != '\t') ++i;
      bool secret = false;
      for (size_t k = start; k < i && !secret; ++k)
        secret = s[k] == '@' || s[k] == '=' || (s[k] == ':' && s[k + 1] == '/' && k + 2 < i && s[k + 2] == '/');
      if (secret) add("[link]");
      else add(s + start, i - start);
      while (s[i] == ' ' || s[i] == '\n' || s[i] == '\t') {
        add(" ", 1);
        ++i;
      }
    }
  }
};

// std::terminate: the exception's type and what() into the note's reason
// and the log, then abort() (the SIGABRT note follows). No allocation of our
// own: this may be a std::bad_alloc.
[[noreturn]] void onTerminate() {
  const char* type = nullptr;
  const char* what = nullptr;
#if defined(MARIGOLD_CRASH_CXXABI)
  if (const std::type_info* t = abi::__cxa_current_exception_type()) type = t->name();
#endif
  if (std::exception_ptr e = std::current_exception()) {
    try {
      std::rethrow_exception(e);
    } catch (const std::exception& x) {
      what = x.what();
    } catch (...) {
    }
  }
  Fixed reason{gReason, sizeof gReason};
  if (type || what) {
    reason.add("uncaught exception ");
    reason.add(type ? type : "(of unknown type)");
    if (what) {
      reason.add(": ");
      reason.addScrubbed(what);
    }
  } else {
    reason.add("std::terminate with no exception");
  }
  gReasonReady = 1;
  Out out;
  out.fds[0] = log::fileDescriptor();
  out.str("E [crash] ");
  out.str(gReason);
  out.str("\n");
  std::abort();
}

#if defined(MARIGOLD_CRASH_FRAMES)
int firstObject(struct dl_phdr_info* info, size_t, void*) {
  gBase = static_cast<uintptr_t>(info->dlpi_addr);
  return 1;  // the first is the executable
}
#endif

std::optional<std::string> readSmall(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::string text(kNoteMax, '\0');
  in.read(text.data(), static_cast<std::streamsize>(text.size()));
  text.resize(static_cast<size_t>(std::max<std::streamsize>(0, in.gcount())));
  return text;
}

}  // namespace

void install() {
  static bool installed = false;  // Android starts the app thread again in one process
  if (installed) return;
  installed = true;
  clock_gettime(CLOCK_MONOTONIC, &gStart);
  std::snprintf(gBuild, sizeof gBuild, "Marigold %s build %s", MARIGOLD_VERSION, MARIGOLD_BUILD);
#if defined(MARIGOLD_CRASH_FRAMES)
  // The first backtrace() loads the unwinder (dlopen, malloc): now, not in
  // the handler.
  void* warm[2];
  backtrace(warm, 2);
  dl_iterate_phdr(firstObject, nullptr);
#endif
  // Its own stack: a stack overflow still gets its note (this thread's; the
  // workers' handlers run on their own stacks).
  size_t stackBytes = std::max<size_t>(kAltStackBytes, static_cast<size_t>(SIGSTKSZ));
  static char* altStack = new char[stackBytes];
  stack_t ss;
  std::memset(&ss, 0, sizeof ss);
  ss.ss_sp = altStack;
  ss.ss_size = stackBytes;
  sigaltstack(&ss, nullptr);

  struct sigaction sa;
  std::memset(&sa, 0, sizeof sa);
  sa.sa_sigaction = onSignal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
  for (int i = 0; i < kSignalCount; ++i) sigaction(kSignals[i], &sa, &gPrevious[i]);
  std::set_terminate(onTerminate);
}

void openNotes(const std::string& dataDir) {
  if (dataDir.empty()) return;
  std::string note = dataDir + "/crash.txt", kept = dataDir + "/crash-previous.txt";
  std::remove(kept.c_str());  // two launches old
  if (std::optional<std::string> text = readSmall(note)) {
    if (std::optional<report::CrashNote> parsed = report::parseCrashNote(*text)) {
      gPreviousSignal = parsed->signal;
      gPreviousSummary = report::crashSummary(*parsed);
      MLOG_E("crash", "previous run crashed: %s", parsed->signal.c_str());
      // The note as written (addresses and symbols; lines go through
      // redact() like every log line).
      size_t start = 0;
      int lines = 0;
      while (start < text->size() && lines < 80) {
        size_t nl = text->find('\n', start);
        if (nl == std::string::npos) nl = text->size();
        if (nl > start) MLOG_I("crash", "  %s", text->substr(start, std::min<size_t>(nl - start, 300)).c_str());
        start = nl + 1;
        ++lines;
      }
    } else if (!text->empty()) {
      MLOG_W("crash", "previous run left a crash note that doesn't read");
    }
    std::rename(note.c_str(), kept.c_str());
  }
  if (note.size() < sizeof gNotePath) {
    std::memcpy(gNotePath, note.c_str(), note.size() + 1);
    gNoteReady = 1;
  }
}

const std::string& previousSignal() { return gPreviousSignal; }
const std::string& previousSummary() { return gPreviousSummary; }

}  // namespace marigold::app::crash
