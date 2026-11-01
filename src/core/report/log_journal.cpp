#include "core/report/log_journal.hpp"

#include <cstdio>
#include <utility>

namespace marigold::report {

void LogJournal::restored(const PlaybackLog& log, size_t lines, bool whole) {
  added_ = log.added();
  clears_ = log.clears();
  lines_ = lines;
  whole_ = whole;
}

std::optional<LogJournal::Write> LogJournal::take(const PlaybackLog& log, bool all) {
  const auto& entries = log.entries();
  uint64_t fresh = log.added() - added_;
  bool cleared = log.clears() != clears_;
  bool lost = fresh > entries.size();  // more came than the log keeps
  if (!whole_ && !cleared && !lost) {
    if (fresh == 0) return std::nullopt;
    // Memory readings alone wait for the next real event.
    bool real = all;
    for (size_t i = entries.size() - fresh; i < entries.size() && !real; ++i)
      real = entries[i].event != PlaybackLog::kMemoryEvent;
    if (!real) return std::nullopt;
  }
  Write w;
  w.whole = whole_ || cleared || lost || lines_ + fresh > kCompactAt;
  size_t from = w.whole ? 0 : entries.size() - fresh;
  for (size_t i = from; i < entries.size(); ++i) w.text += PlaybackLog::journalLine(entries[i]);
  w.lines = entries.size() - from;
  lines_ = w.whole ? w.lines : lines_ + w.lines;
  added_ = log.added();
  clears_ = log.clears();
  whole_ = false;
  return w;
}

void JournalFile::queue(LogJournal::Write w) {
  std::lock_guard<std::mutex> lock(queued_);
  // A whole file replaces what waits; lines join it (the whole file and
  // the lines after it are still the whole file).
  if (!pending_ || w.whole) {
    pending_ = std::move(w);
    return;
  }
  pending_->text += w.text;
  pending_->lines += w.lines;
}

void JournalFile::replaces(std::string oldPath) {
  std::lock_guard<std::mutex> lock(queued_);
  old_ = std::move(oldPath);
}

bool JournalFile::flush() {
  std::lock_guard<std::mutex> io(io_);
  std::optional<LogJournal::Write> w;
  std::string old;
  {
    std::lock_guard<std::mutex> lock(queued_);
    w.swap(pending_);
    if (w && w->whole) old = old_;
  }
  if (!w) return true;
  if (!write(*w)) {
    broken_ = true;
    return false;
  }
  bytes_ += w->text.size();
  if (w->whole) {
    broken_ = false;
    if (!old.empty()) {
      std::remove(old.c_str());
      std::lock_guard<std::mutex> lock(queued_);
      if (old_ == old) old_.clear();
    }
  }
  return true;
}

bool JournalFile::write(const LogJournal::Write& w) {
  if (w.whole) {
    std::string tmp = path_ + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(w.text.data(), 1, w.text.size(), f) == w.text.size();
    ok = std::fclose(f) == 0 && ok;
    return ok && std::rename(tmp.c_str(), path_.c_str()) == 0;
  }
  if (w.text.empty()) return true;
  std::FILE* f = std::fopen(path_.c_str(), "a+b");
  if (!f) return false;
  // A line cut short (the app stopped while writing it) is ended first, so
  // only it is lost, not the line after it.
  bool ok = true;
  if (std::fseek(f, 0, SEEK_END) == 0 && std::ftell(f) > 0 && std::fseek(f, -1, SEEK_END) == 0 &&
      std::fgetc(f) != '\n') {
    std::fseek(f, 0, SEEK_END);
    ok = std::fputc('\n', f) == '\n';
  }
  std::fseek(f, 0, SEEK_END);
  ok = ok && std::fwrite(w.text.data(), 1, w.text.size(), f) == w.text.size();
  return std::fclose(f) == 0 && ok;
}

}  // namespace marigold::report
