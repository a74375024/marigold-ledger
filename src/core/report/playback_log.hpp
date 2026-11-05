// What happened each time Live TV tuned a channel (LiveTVPlaybackLog in
// tvOS): which version was tried, how long the picture took, stalls,
// reconnects, switches to backup feeds and why. The last kCap events are
// kept (the app saves them with toJson() so they survive restarts) and
// exported from Help as a CSV. No links, passwords or tokens: every field
// is scrubbed when it is added.
//
// tvOS kept one shared instance and saved it itself 2 s after a change; here
// the app owns one PlaybackLog (UI thread) and keeps it on disk as a journal
// (log_journal.hpp): each save appends only the events added since the last.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>

#include "core/playback/recovery.hpp"
#include "core/util/time.hpp"

namespace marigold::report {

class PlaybackLog {
 public:
  // Swift `cap`: the newest events kept.
  static constexpr size_t kCap = 4000;
  // Longest field kept, in bytes (cut at a character boundary). Swift had no
  // limit; this bounds the log's memory on the TV (4000 x 6 fields).
  static constexpr size_t kFieldCap = 512;
  // A memory reading's event (Session::logMemory).
  static constexpr std::string_view kMemoryEvent = "memory";
  // Marigold (Build 136): the first line of every run (session 0): the
  // build, version and system it runs on, so an export that spans several
  // builds or devices can be told apart. No account or user names.
  static constexpr std::string_view kLaunchEvent = "launch";

  struct Entry {
    int64_t atMs = 0;  // Unix time in milliseconds
    // Tune session number: groups the events of one channel start (0 for
    // channel-list load timings).
    int session = 0;
    std::string event;
    std::string channel;
    std::string feed;
    std::string provider;
    std::string detail;
  };

  // Starts a new tune session and returns its number.
  int newSession();
  int lastSession() const { return lastSession_; }

  // Player errors can quote the stream link, and Xtream links carry the user
  // name and password: links become "<link>" (Swift's pattern) and every
  // field then goes through redact().
  void add(int64_t atMs, std::string_view event, int session, std::string_view channel = {},
           std::string_view feed = {}, std::string_view provider = {},
           std::string_view detail = {});
  // A playback::Action of kind Log from Recovery: its text is the event,
  // its detail the detail. `feed` / `provider` name the action's feed (the
  // caller resolves action.feed to names). Other kinds are ignored.
  void add(int64_t atMs, const playback::Action& action, int session, std::string_view channel,
           std::string_view feed = {}, std::string_view provider = {});
  // A channel-list load timing (Swift logLoad): event "load: <event>",
  // session 0.
  void addLoad(int64_t atMs, std::string_view event, std::string_view detail);
  // The app started (kLaunchEvent, session 0); `detail` from launchDetail().
  void addLaunch(int64_t atMs, std::string_view detail);
  void clear();

  const std::deque<Entry>& entries() const { return entries_; }
  size_t size() const { return entries_.size(); }
  // Bumps on every change, for saving.
  uint64_t revision() const { return revision_; }
  // Events ever added and clear()s, for the journal (log_journal.hpp): the
  // newest added() - n entries are the ones added since it was n.
  uint64_t added() const { return added_; }
  uint64_t clears() const { return clears_; }

  // A spreadsheet (CSV) of the log, newest last:
  // time,session,event,channel,feed,provider,detail. Times are ISO 8601
  // with milliseconds and the offset of `zone` ("2026-10-26T14:05:09.120-04:00",
  // "Z" for UTC), like ISO8601DateFormatter with the device's zone.
  std::string csv(const time::Zone& zone = time::Zone::device()) const;
  // The same as plain text, one event per line:
  // "<time>  #<session>  <event>  <channel> / <feed> (<provider>)  <detail>".
  std::string text(const time::Zone& zone = time::Zone::device()) const;

  // Saved form: [{"at": ms, "session": n, "event": ..., "channel": ...,
  // "feed": ..., "provider": ..., "detail": ...}, ...].
  std::string toJson() const;
  // Restores a saved log (fields scrubbed again, newest kCap kept). False,
  // leaving the log as it was, when the text is not a saved log.
  bool restore(std::string_view json);

  // One event as a journal line: its saved form (an element of toJson())
  // and "\n".
  static std::string journalLine(const Entry& e);
  // Restores a journal (one saved event per line, oldest first) as
  // restore() does a saved log: the newest kCap, fields scrubbed again. A
  // last line cut short (no "\n": the app stopped while writing it) and
  // lines that don't read are left out. Returns the file's lines.
  size_t restoreJournal(std::string_view text);

 private:
  void push(Entry entry);

  void restored(std::deque<Entry> entries);

  std::deque<Entry> entries_;
  int lastSession_ = 0;
  uint64_t revision_ = 0;
  uint64_t added_ = 0, clears_ = 0;
};

// A launch line's detail: "build 136, version 1.2.0, webOS 6, <device>"
// (the empty parts left out; each part cut to a short length).
std::string launchDetail(std::string_view build, std::string_view version, std::string_view system,
                         std::string_view device = {});

// "2026-10-26T14:05:09.120-04:00" (ISO 8601, milliseconds, `zone`'s offset;
// "Z" when it is 0).
std::string isoTimeMs(int64_t atMs, const time::Zone& zone);

}  // namespace marigold::report
