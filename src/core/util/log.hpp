// App log: one file in the data directory plus the last lines in memory.
//
// webOS sends a native app's stdout to /dev/null, so the file is the only log
// we get back from a TV. Every line is passed through redact() first, so a
// caller cannot leak a stream link by accident.
#pragma once

#include <cstdarg>
#include <cstddef>
#include <string>
#include <vector>

namespace marigold::log {

enum class Level { Debug, Info, Warn, Error };

// Opens (or creates) the log file. Lines written before open() stay in memory.
// The file is rotated to "<path>.1" when it passes maxBytes.
void open(const std::string& path, size_t maxBytes = 1u << 20);
void close();

void write(Level level, const char* tag, const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;
void writev(Level level, const char* tag, const char* fmt, va_list args)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 0)))
#endif
    ;

// Also hands every line (already redacted) to `mirror`, e.g. Android's
// logcat. Called under the log's lock; nullptr stops it.
using Mirror = void (*)(Level level, const std::string& line);
void setMirror(Mirror mirror);

// The newest lines (already redacted), oldest first.
std::vector<std::string> recent(size_t max = 50);

}  // namespace marigold::log

#define MLOG_D(tag, ...) ::marigold::log::write(::marigold::log::Level::Debug, tag, __VA_ARGS__)
#define MLOG_I(tag, ...) ::marigold::log::write(::marigold::log::Level::Info, tag, __VA_ARGS__)
#define MLOG_W(tag, ...) ::marigold::log::write(::marigold::log::Level::Warn, tag, __VA_ARGS__)
#define MLOG_E(tag, ...) ::marigold::log::write(::marigold::log::Level::Error, tag, __VA_ARGS__)
