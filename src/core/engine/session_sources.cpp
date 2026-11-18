// Build 182, Sources during a replay (session.hpp, Session::replaySources):
// the channel's copies as they stand for the moment the replay shows, and
// carrying the replay on on another copy from that moment. Kept apart from
// session.cpp: the rows are what Sources draws, the switch goes through the
// replay's own steps (playReplay, replayFailed).
#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "core/engine/feed_scores.hpp"
#include "core/engine/session.hpp"
#include "core/engine/source_test.hpp"
#include "core/engine/store.hpp"
#include "core/sources/xtream.hpp"

namespace marigold::engine {
namespace {

constexpr const char* kDot = " \xc2\xb7 ";

std::string daysWords(uint16_t days) { return std::to_string(days) + (days == 1 ? " day" : " days"); }

}  // namespace

int Session::feedNumber(size_t index) const {
  if (index >= feeds_.size() || entry_.empty()) return 0;
  return store_.feedNumbers(entry_, feeds_)[index];
}

Session::ReplaySources Session::replaySources() const {
  ReplaySources out;
  if (!replay_ || camera_ || feeds_.empty() || entry_.empty()) return out;
  const Replay& r = *replay_;
  time::Seconds now = time::now();
  out.at = r.at();
  time::Seconds at = replay::minuteOf(r.at());
  std::vector<int> numbers = store_.feedNumbers(entry_, feeds_);
  auto row = [&](size_t i) {
    ReplaySource s;
    s.index = i;
    s.number = numbers[i];
    s.label = setaside::feedName(s.number);
    const std::string& id = feeds_[i].channelId;
    s.provider = store_.providerName(id);
    s.days = store_.catchupDays(id);
    s.playing = i == r.feed;
    if (const scores::Record* rec = store_.feedScores().find(id)) {
      scores::Record d = rec->at(now);
      if (d.starts() > 0 && !d.mostlyFails()) s.picture = sourcetest::pictureText(d.height, d.fps);
    }
    return s;
  };
  // The order the replay would pick them in (archiveCopyFor, the copy
  // playing as the one it plays on), one after another.
  std::set<size_t> taken;
  while (std::optional<size_t> next = archiveCopyFor(at, replayLive_, r.feed, &taken)) {
    if (!taken.insert(*next).second) break;
    if (numbers[*next] == 0) continue;  // set aside for the channel (Build 181)
    ReplaySource s = row(*next);
    s.reaches = true;
    out.reaching.push_back(std::move(s));
  }
  // Their replay figures: a note where there are enough to tell, in the
  // margins archiveCopyFor's tie-break uses.
  std::vector<std::optional<double>> bad;
  for (const ReplaySource& s : out.reaching) bad.push_back(store_.replayBadness(feeds_[s.index].channelId));
  for (size_t k = 0; k < out.reaching.size(); ++k) {
    if (!bad[k]) continue;
    std::optional<double> best;
    for (size_t j = 0; j < bad.size(); ++j)
      if (j != k && bad[j] && (!best || *bad[j] < *best)) best = bad[j];
    // Clearly worse than the best of the others (with no other to weigh,
    // than a copy without trouble): stalled lately; within the margin of
    // no trouble at all: held up well.
    if (*bad[k] > best.value_or(0.0) + scores::kReplayClearlyWorse)
      out.reaching[k].note = ReplaySource::Note::Stalled;
    else if (*bad[k] <= scores::kReplayClearlyWorse)
      out.reaching[k].note = ReplaySource::Note::HeldUp;
  }
  for (size_t i = 0; i < feeds_.size(); ++i) {
    if (taken.count(i) || numbers[i] == 0) continue;
    ReplaySource s = row(i);
    if (replayAside(i)) s.why = ReplaySource::Why::SetAside;
    else if (!ownArchive(i, replayLive_)) s.why = ReplaySource::Why::LiveOnly;
    else s.why = ReplaySource::Why::TooShort;
    out.cannot.push_back(std::move(s));
  }
  return out;
}

std::string Session::replayNoteText(ReplaySource::Note n) {
  switch (n) {
    case ReplaySource::Note::HeldUp: return "replays held up well";
    case ReplaySource::Note::Stalled: return "stalled in a replay lately";
    case ReplaySource::Note::None: break;
  }
  return "";
}

std::string Session::replaySourceLine(const ReplaySource& s, const std::string& when) {
  std::vector<std::string> parts;
  if (s.playing) parts.push_back("Playing now");
  if (s.reaches) {
    if (!s.picture.empty()) parts.push_back(s.picture);
    if (s.days > 0) parts.push_back("replay " + daysWords(s.days));
    if (std::string note = replayNoteText(s.note); !note.empty()) parts.push_back(note);
  } else {
    switch (s.why) {
      case ReplaySource::Why::LiveOnly: parts.push_back("Live only"); break;
      case ReplaySource::Why::TooShort: parts.push_back("Its replay doesn't reach " + when); break;
      case ReplaySource::Why::SetAside: parts.push_back("You set its replay aside"); break;
      case ReplaySource::Why::None: break;
    }
  }
  if (!s.provider.empty()) parts.push_back(s.provider);
  std::string out;
  for (size_t i = 0; i < parts.size(); ++i) out += (i ? kDot : "") + parts[i];
  return out;
}

bool Session::replayOn(size_t index) { return switchReplay(index, SwitchWhy::Picked); }

bool Session::switchReplay(size_t index, SwitchWhy why) {
  if (!replay_ || paused_ || camera_ || index >= feeds_.size()) return false;
  Replay& r = *replay_;
  // The copy already replaying: nothing to do.
  if (index == r.feed) return false;
  time::Seconds now = time::now();
  time::Seconds at = replay::minuteOf(r.at());
  if (!keepsArchive(index, replayLive_) || !replay::covers(store_.catchupDays(feeds_[index].channelId), at, now))
    return false;
  size_t from = r.feed;
  // What went on ends here: a stall (counted where it lasted), a wait for
  // the copy's clock (its reads stop: the replay was left, not timed out),
  // the stream (the account's one connection is free before the next
  // opens; a background check on the new copy's account gives way as the
  // stretch opens, as the watchdog's reopen does).
  stallOver(ms(), true);
  stall_.step = StallStep::None;
  stall_.reopenAt = 0;
  dropMeasure();
  if (!r.released) player_->stop();
  r.quiet = false;
  replayPick_.reset();
  if (why == SwitchWhy::Picked) replayPick_ = ReplayPick{from, index, at};
  shortStretches_ = 0;
  std::string name = store_.providerName(feeds_[index].channelId);
  if (name.empty()) name = feeds_[index].label;
  const char* head = why == SwitchWhy::Picked     ? "switched by you to "
                     : why == SwitchWhy::SetAside ? "its replay set aside; on to "
                                                  : "used again; back to ";
  playReplay(index, at, head + name + " from " + localTime(at));
  return true;
}

std::optional<Session::ReplaySwitch> Session::takeReplaySwitch() {
  std::optional<ReplaySwitch> out;
  out.swap(replaySwitch_);
  return out;
}

std::string Session::clockWords(size_t copy) const {
  if (copy >= feeds_.size()) return "";
  std::optional<int> offset, server;
  bool archive = false, measured = false;
  if (replay_ && replay_->feed == copy && replayClock_) {
    // The clock its stretch was asked in.
    offset = replayClock_->offset;
    server = replayClock_->server;
    archive = replayClock_->archive;
    measured = replayClock_->measured;
  } else {
    Store::ArchiveClock c = store_.archiveClock(feeds_[copy].channelId);
    offset = c.offset;
    server = c.server;
    archive = c.from != Store::ClockFrom::Server;
    measured = c.measured;
  }
  if (!offset) return "";
  std::string out = std::string(archive ? "archive clock " : "server clock ") + sources::xtream::offsetText(*offset) + " (";
  if (archive && server) out += "server says " + sources::xtream::offsetText(*server) + "; ";
  return out + (measured ? "measured)" : "not measured)");
}

}  // namespace marigold::engine
