// The playback log on disk as a journal (Build 120): appends read back to
// what the old whole-log save gave, a line cut short is dropped, memory
// readings alone don't write.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

#include "core/report/log_journal.hpp"
#include "core/report/playback_log.hpp"
#include "doctest.h"

using marigold::report::JournalFile;
using marigold::report::LogJournal;
using marigold::report::PlaybackLog;
namespace mtime = marigold::time;

namespace {

constexpr int64_t kAt = 1793037909120;

// A temp file, removed afterwards.
struct TempFile {
  std::string path;
  TempFile() {
    static int serial = 0;
    path = (std::filesystem::temp_directory_path() /
            ("marigold-journal-" + std::to_string(::getpid()) + "-" + std::to_string(++serial) + ".jsonl"))
               .string();
    std::remove(path.c_str());
  }
  ~TempFile() {
    std::remove(path.c_str());
    std::remove((path + ".tmp").c_str());
  }
  std::string read() const {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
  }
  void write(const std::string& text) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
  }
};

// The app's save: what take() gives, queued and written.
bool save(LogJournal& journal, JournalFile& file, const PlaybackLog& log, bool all = false) {
  auto w = journal.take(log, all);
  if (!w) return false;
  file.queue(std::move(*w));
  REQUIRE(file.flush());
  return true;
}

void addEvents(PlaybackLog& log, int from, int n) {
  for (int i = from; i < from + n; ++i) {
    int s = i % 7 == 0 ? log.newSession() : log.lastSession();
    if (i % 5 == 0)
      log.add(kAt + i, "memory", 0, {}, {}, {}, "rss " + std::to_string(i) + " MB");
    else
      log.add(kAt + i, i % 2 ? "buffering" : "tune", s, "US: ESPN FHD", "US: ESPN HD", "Provider A",
              "stalled, \"quoted\"\nline " + std::to_string(i) + " \xc3\xa9");
  }
}

}  // namespace

TEST_CASE("log journal: appends, compactions and a read back give what the whole save gave") {
  TempFile f;
  PlaybackLog log;
  LogJournal journal;
  JournalFile file(f.path);
  int wholes = 0;
  size_t appended = 0;
  // Ten thousand events, saved in bursts like the app's 10 s saves: the
  // file is rewritten whole only as it passes twice the log's cap.
  for (int burst = 0; burst < 100; ++burst) {
    addEvents(log, burst * 100, 100);
    auto w = journal.take(log);
    REQUIRE(w);
    if (w->whole) ++wholes;
    else appended = std::max(appended, w->text.size());
    file.queue(std::move(*w));
    REQUIRE(file.flush());
    CHECK(journal.lines() <= LogJournal::kCompactAt);
  }
  CHECK(wholes == 1);  // at 8,100 lines
  CHECK(appended < log.toJson().size() / 30);  // a burst writes its own lines only
  CHECK(log.size() == PlaybackLog::kCap);
  std::string text = f.read();
  CHECK(text.back() == '\n');

  PlaybackLog journaled, whole;
  journaled.restoreJournal(text);
  REQUIRE(whole.restore(log.toJson()));
  CHECK(journaled.size() == PlaybackLog::kCap);
  CHECK(journaled.csv(mtime::Zone::utc()) == log.csv(mtime::Zone::utc()));
  CHECK(journaled.csv(mtime::Zone::utc()) == whole.csv(mtime::Zone::utc()));
  CHECK(journaled.text(mtime::Zone::utc()) == whole.text(mtime::Zone::utc()));
  CHECK(journaled.lastSession() == whole.lastSession());
  CHECK(journaled.newSession() == whole.newSession());
}

TEST_CASE("log journal: a last line cut short is dropped, and the next one still reads") {
  TempFile f;
  PlaybackLog log;
  LogJournal journal;
  JournalFile file(f.path);
  log.add(kAt, "tune", 1, "A", "A HD", "P", "1 feed");
  log.add(kAt + 1, "first frame", 1, "A", "A HD", "P", "0.8 s");
  REQUIRE(save(journal, file, log));
  // The app stopped halfway through writing a line.
  std::string text = f.read();
  std::string third = PlaybackLog::journalLine({kAt + 2, 1, "stall", "A", "A HD", "P", "2 s"});
  f.write(text + third.substr(0, third.size() / 2));

  PlaybackLog back;
  CHECK(back.restoreJournal(f.read()) == 3);
  REQUIRE(back.size() == 2);
  CHECK(back.entries().back().event == "first frame");
  CHECK(PlaybackLog().restoreJournal("") == 0);
  CHECK(PlaybackLog().restoreJournal("{\"at\": 1") == 1);

  // Carrying on from what was read: the cut line is ended first, so the
  // next append reads (only the cut one is lost).
  LogJournal again;
  again.restored(back, 3);
  JournalFile file2(f.path);
  back.add(kAt + 3, "stop", 1, "A", "A HD", "P");
  REQUIRE(save(again, file2, back));
  PlaybackLog after;
  after.restoreJournal(f.read());
  REQUIRE(after.size() == 3);
  CHECK(after.entries().back().event == "stop");
  CHECK(after.csv(mtime::Zone::utc()) == back.csv(mtime::Zone::utc()));
  // A line that doesn't read in the middle is skipped too.
  PlaybackLog skipped;
  skipped.restoreJournal(PlaybackLog::journalLine({kAt, 1, "a", "", "", "", ""}) + "not json\n[1]\n" +
                         PlaybackLog::journalLine({kAt, 2, "b", "", "", "", "http://example.invalid/x"}));
  REQUIRE(skipped.size() == 2);
  CHECK(skipped.entries()[1].detail == "<link>");  // scrubbed on the way in
}

TEST_CASE("log journal: memory readings alone don't write; they go with the next event") {
  TempFile f;
  PlaybackLog log;
  LogJournal journal;
  JournalFile file(f.path);
  log.add(kAt, "tune", 1, "A");
  REQUIRE(save(journal, file, log));
  std::string first = f.read();
  // Every five minutes for an hour: nothing written.
  for (int i = 0; i < 12; ++i) {
    log.add(kAt + i, PlaybackLog::kMemoryEvent, 0, {}, {}, {}, "rss 300 MB");
    CHECK_FALSE(save(journal, file, log));
  }
  CHECK(f.read() == first);
  CHECK(file.bytesWritten() == first.size());
  // The next real event takes them along, in order.
  log.add(kAt + 20, "tune", 2, "B");
  REQUIRE(save(journal, file, log));
  PlaybackLog back;
  CHECK(back.restoreJournal(f.read()) == 14);
  CHECK(back.csv(mtime::Zone::utc()) == log.csv(mtime::Zone::utc()));
  // Leaving the app writes them anyway.
  log.add(kAt + 30, PlaybackLog::kMemoryEvent, 0, {}, {}, {}, "rss 310 MB");
  CHECK_FALSE(save(journal, file, log));
  CHECK(save(journal, file, log, true));
  CHECK_FALSE(save(journal, file, log, true));  // nothing new
  PlaybackLog all;
  all.restoreJournal(f.read());
  CHECK(all.size() == 15);
}

TEST_CASE("log journal: clearing the log empties the file; a lost write is made good whole") {
  TempFile f;
  PlaybackLog log;
  LogJournal journal;
  JournalFile file(f.path);
  addEvents(log, 1, 20);
  REQUIRE(save(journal, file, log));
  log.clear();
  auto w = journal.take(log);
  REQUIRE(w);
  CHECK(w->whole);
  CHECK(w->text.empty());
  file.queue(std::move(*w));
  REQUIRE(file.flush());
  CHECK(f.read().empty());
  CHECK(journal.lines() == 0);

  // A write that fails (no folder): broken until the whole log is written.
  JournalFile nowhere("/nonexistent-dir/marigold/playback-log.jsonl");
  log.add(kAt, "tune", 1, "A");
  nowhere.queue(*journal.take(log));
  CHECK_FALSE(nowhere.flush());
  CHECK(nowhere.broken());
  journal.rewrite();
  log.add(kAt + 1, "stop", 1, "A");
  auto whole = journal.take(log);
  REQUIRE(whole);
  CHECK(whole->whole);
  CHECK(whole->lines == 2);
}

TEST_CASE("log journal: queued saves join up in order") {
  TempFile f;
  JournalFile file(f.path);
  file.queue({false, "a\n", 1});
  file.queue({false, "b\n", 1});
  REQUIRE(file.flush());
  CHECK(f.read() == "a\nb\n");
  file.queue({false, "c\n", 1});
  file.queue({true, "x\n", 1});  // a whole file replaces what waits...
  file.queue({false, "y\n", 1});  // ... and lines after it join it
  REQUIRE(file.flush());
  CHECK(f.read() == "x\ny\n");
  CHECK(file.flush());  // nothing queued
  // The old whole-log file goes once the journal is written whole.
  TempFile old;
  old.write("[]");
  file.replaces(old.path);
  file.queue({false, "z\n", 1});
  REQUIRE(file.flush());
  CHECK(std::filesystem::exists(old.path));
  file.queue({true, "w\n", 1});
  REQUIRE(file.flush());
  CHECK_FALSE(std::filesystem::exists(old.path));
}
