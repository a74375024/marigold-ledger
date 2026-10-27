#include "core/util/log.hpp"

#include <cstdio>
#include <ctime>
#include <deque>
#include <mutex>

#include "core/util/redact.hpp"

namespace marigold::log {
namespace {

constexpr size_t kRecentMax = 200;

struct State {
  std::mutex mu;
  std::FILE* file = nullptr;
  std::string path;
  size_t maxBytes = 0;
  size_t written = 0;
  std::deque<std::string> recent;
  Mirror mirror = nullptr;
};

State& state() {
  static State s;
  return s;
}

const char* levelName(Level level) {
  switch (level) {
    case Level::Debug: return "D";
    case Level::Info: return "I";
    case Level::Warn: return "W";
    case Level::Error: return "E";
  }
  return "?";
}

#if defined(__GNUC__)
__attribute__((format(printf, 1, 0)))
#endif
std::string format(const char* fmt, va_list args) {
  va_list copy;
  va_copy(copy, args);
  int n = std::vsnprintf(nullptr, 0, fmt, copy);
  va_end(copy);
  if (n <= 0) return {};
  std::string out(static_cast<size_t>(n) + 1, '\0');
  std::vsnprintf(out.data(), out.size(), fmt, args);
  out.resize(static_cast<size_t>(n));
  return out;
}

std::string timestamp() {
  std::time_t now = std::time(nullptr);
  std::tm tm{};
  localtime_r(&now, &tm);
  char buf[32];
  std::strftime(buf, sizeof buf, "%m-%d %H:%M:%S", &tm);
  return buf;
}

// Caller holds the lock.
void rotateIfNeeded(State& s) {
  if (!s.file || s.maxBytes == 0 || s.written < s.maxBytes) return;
  std::fclose(s.file);
  std::string old = s.path + ".1";
  std::rename(s.path.c_str(), old.c_str());
  s.file = std::fopen(s.path.c_str(), "w");
  s.written = 0;
}

}  // namespace

void open(const std::string& path, size_t maxBytes) {
  State& s = state();
  std::lock_guard<std::mutex> lock(s.mu);
  if (s.file) std::fclose(s.file);
  s.path = path;
  s.maxBytes = maxBytes;
  s.file = std::fopen(path.c_str(), "a");
  s.written = 0;
  if (s.file) {
    std::fseek(s.file, 0, SEEK_END);
    long size = std::ftell(s.file);
    s.written = size > 0 ? static_cast<size_t>(size) : 0;
    for (const std::string& line : s.recent) std::fprintf(s.file, "%s\n", line.c_str());
    std::fflush(s.file);
  }
}

void close() {
  State& s = state();
  std::lock_guard<std::mutex> lock(s.mu);
  if (s.file) std::fclose(s.file);
  s.file = nullptr;
}

void writev(Level level, const char* tag, const char* fmt, va_list args) {
  std::string line = timestamp();
  line += ' ';
  line += levelName(level);
  line += " [";
  line += tag ? tag : "";
  line += "] ";
  line += redact(format(fmt, args));

  State& s = state();
  std::lock_guard<std::mutex> lock(s.mu);
  s.recent.push_back(line);
  if (s.recent.size() > kRecentMax) s.recent.pop_front();
  if (s.mirror) s.mirror(level, line);
  if (s.file) {
    std::fprintf(s.file, "%s\n", line.c_str());
    std::fflush(s.file);
    s.written += line.size() + 1;
    rotateIfNeeded(s);
  }
}

void write(Level level, const char* tag, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  writev(level, tag, fmt, args);
  va_end(args);
}

void setMirror(Mirror mirror) {
  State& s = state();
  std::lock_guard<std::mutex> lock(s.mu);
  s.mirror = mirror;
}

std::vector<std::string> recent(size_t max) {
  State& s = state();
  std::lock_guard<std::mutex> lock(s.mu);
  size_t n = s.recent.size() < max ? s.recent.size() : max;
  return std::vector<std::string>(s.recent.end() - static_cast<std::ptrdiff_t>(n), s.recent.end());
}

}  // namespace marigold::log
