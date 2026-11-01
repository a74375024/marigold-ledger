// The playback log on disk as a journal (Build 120). Saving the whole log
// (4,000 events, about 850 KB) every few seconds cost the UI thread tens of
// ms on a TV and wrote hundreds of MB a day while idle; now a save appends
// only the events added since the last one, on a worker, and the file is
// rewritten whole only when it passes kCompactAt lines or the log was
// cleared. Memory readings on their own don't save: they go out with the
// next real event, or when the app is left (Help and the export read the
// log in memory, so they show them all the same). Read back, a last line
// cut short by a crash is dropped (PlaybackLog::restoreJournal).
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include "core/report/playback_log.hpp"

namespace marigold::report {

class LogJournal {
 public:
  // Rewritten whole (the newest kCap) once the file holds this many lines.
  static constexpr size_t kCompactAt = 2 * PlaybackLog::kCap;

  // What a save writes: lines to add at the end, or the whole file.
  struct Write {
    bool whole = false;
    std::string text;
    size_t lines = 0;
  };

  // The log was read back from a journal of `lines` lines. `whole`: the
  // next save writes it whole (it came from an older form of the file).
  void restored(const PlaybackLog& log, size_t lines, bool whole = false);
  // The next save writes the whole log (a write was lost).
  void rewrite() { whole_ = true; }
  bool wantsWhole() const { return whole_; }

  // What to write for `log` now, if anything: the events added since the
  // last save, or the whole log. Only memory readings new: nothing (they
  // wait for the next save), unless `all` (the app is being left).
  std::optional<Write> take(const PlaybackLog& log, bool all = false);

  // The file's lines, as written so far.
  size_t lines() const { return lines_; }

 private:
  uint64_t added_ = 0, clears_ = 0;
  size_t lines_ = 0;
  bool whole_ = false;
};

// The journal's file: saves queued on the UI thread, written in order, one
// at a time, by whichever thread calls flush() (a worker; the UI thread
// only as the app closes).
class JournalFile {
 public:
  explicit JournalFile(std::string path) : path_(std::move(path)) {}

  void queue(LogJournal::Write w);
  // Writes what is queued. False when a write failed (the save is lost:
  // broken() until the next whole write).
  bool flush();
  bool broken() const { return broken_; }
  // The file the journal replaces, removed once the journal is written whole.
  void replaces(std::string oldPath);

  // Bytes written, for the log's timing line.
  uint64_t bytesWritten() const { return bytes_; }

 private:
  bool write(const LogJournal::Write& w);

  const std::string path_;
  std::mutex io_;  // held while writing: one write at a time, in order
  std::mutex queued_;
  std::optional<LogJournal::Write> pending_;
  std::string old_;
  std::atomic<bool> broken_{false};
  std::atomic<uint64_t> bytes_{0};
};

}  // namespace marigold::report
