// Build 181, "This feed is wrong…" (session.hpp, Session::setAside): the
// playback side of a feed set aside for its channel. Kept apart from
// session.cpp: the store keeps the record, the log has the line, and the
// channel playing moves off the copy at once.
#include <chrono>

#include <algorithm>

#include "core/engine/replay.hpp"
#include "core/engine/session.hpp"
#include "core/engine/source_test.hpp"
#include "core/engine/store.hpp"

namespace marigold::engine {
namespace {

int64_t wallNowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

}  // namespace

Session::SetAsideOutcome Session::setAsideReplay(setaside::Record r) {
  SetAsideOutcome out;
  bool here = entry_ == r.entryId && !camera_ && !feeds_.empty();
  size_t copy = feeds_.size();
  for (size_t i = 0; here && i < feeds_.size(); ++i)
    if (feeds_[i].channelId == r.copyId) copy = i;
  // The moment showing (a replay of this channel), the clock its replay
  // asks in, what the guide and the copy's own list had then.
  bool replaying = here && replay_.has_value();
  time::Seconds at = replaying ? replay_->at() : time::now();
  std::string when = replaying ? localTime(at) : std::string();
  std::string clock = clockWords(copy);
  std::string guide = store_.guideOnNow(r.entryId, at);
  std::string own = store_.copyOnNow(r.copyId, at);
  out.record = store_.setAside(std::move(r));
  const setaside::Record& s = out.record;
  log_.add(wallNowMs(), "set aside", here ? session_ : 0, s.entryName, setaside::feedName(s.feed),
           store_.providerName(s.copyId), setaside::replayLogDetail(s, when, clock, guide, own));
  // "It's at the wrong time": as a failed clock check of its account (the
  // background check measures its clock again).
  if (s.reason == setaside::Reason::WrongTime) store_.noteClockCheck(s.copyId, Store::CheckEnd::Failed, ms());
  // Its own table on the Rewind bar goes (asked again of the copy then
  // replaying, when it can be).
  if (pastCopy_ == s.copyId) {
    pastFor_.clear();
    past_.reset();
    ++pastSerial_;
    if (hooks_.changed) hooks_.changed();
  }
  if (!replaying) return out;
  out.playing = feeds_[std::min(replay_->feed, feeds_.size() - 1)].channelId;
  // Its programme names only, or another copy than the one replaying: the
  // replay goes on as it is.
  if (s.scope != setaside::Scope::Replay || copy != replay_->feed) return out;
  // The copy replaying: on at once on the next copy that reaches the moment.
  std::optional<size_t> next = archiveCopyFor(replay::minuteOf(replay_->at()), replayLive_, replay_->feed);
  if (!next || *next == replay_->feed || !switchReplay(*next, SwitchWhy::SetAside)) {
    out.alone = true;
    return out;
  }
  replayAsideFrom_ = s.copyId;
  out.switched = true;
  out.playing = feeds_[*next].channelId;
  return out;
}

Session::SetAsideOutcome Session::setAside(setaside::Record r) {
  // Build 182: a copy's replay alone.
  if (setaside::isReplay(r.scope)) return setAsideReplay(std::move(r));
  SetAsideOutcome out;
  time::Seconds now = time::now();
  // What the copy's own listing and the channel's guide have on now,
  // before the guide is decided again without it.
  std::string onNow = store_.copyOnNow(r.copyId, now);
  std::string guide = store_.guideOnNow(r.entryId, now);
  bool here = entry_ == r.entryId && !camera_ && !feeds_.empty();
  std::string live = playingChannel();
  std::string replaying = replay_ && replay_->feed < feeds_.size() ? feeds_[replay_->feed].channelId : std::string();
  out.record = store_.setAside(std::move(r));
  const setaside::Record& s = out.record;
  log_.add(wallNowMs(), "set aside", here ? session_ : 0, s.entryName, setaside::feedName(s.feed),
           store_.providerName(s.copyId), setaside::logDetail(s, onNow, guide));
  if (!here) return out;
  out.playing = live;
  if (setaside::playable(s.reason)) return out;
  size_t at = feeds_.size();
  for (size_t i = 0; i < feeds_.size(); ++i)
    if (feeds_[i].channelId == s.copyId) at = i;
  if (at == feeds_.size()) return out;  // not one of this tune's feeds
  bool onIt = live == s.copyId || replaying == s.copyId;
  // A backup of what plays: its feeds are read again without it (the copy
  // playing opens again, first), so no switch to a backup or a replay
  // lands on it. A replay on another copy goes on; the next tune leaves it
  // out.
  if (!onIt && replay_) return out;
  // The one playing: the feed after it in the list (wrapping) that still
  // plays here.
  std::string next = onIt ? std::string() : live;
  for (size_t k = 1; k < feeds_.size() && next.empty(); ++k) {
    const std::string& c = feeds_[(at + k) % feeds_.size()].channelId;
    if (c != s.copyId && !store_.notPlayedOn(entry_, c)) next = c;
  }
  std::vector<Feed> fresh = store_.feeds(entry_);
  if (next.empty() || fresh.empty()) {
    // Nothing else plays this channel: it plays on, and the notice says so.
    out.alone = onIt;
    return out;
  }
  size_t start = 0;
  for (size_t i = 0; i < fresh.size(); ++i)
    if (fresh[i].channelId == next) start = i;
  if (!begin(entry_, std::move(fresh), {}, start)) {
    out.alone = onIt;
    return out;
  }
  if (onIt) asideFrom_ = s.copyId;
  out.switched = onIt;
  out.playing = playingChannel();
  return out;
}

bool Session::useAgain(const std::string& entryId, const std::string& copyId, bool replay) {
  std::optional<setaside::Record> r = store_.setAsideOf(entryId, copyId, replay);
  if (!r || !store_.useAgain(entryId, copyId, replay)) return false;
  bool here = entry_ == entryId && !camera_ && !feeds_.empty();
  std::string day = sourcetest::testedDay(r->at, time::now(), host_.zone());
  log_.add(wallNowMs(), "used again", here ? session_ : 0, r->entryName, setaside::feedName(r->feed),
           store_.providerName(copyId), setaside::usedAgainDetail(*r, day));
  if (replay) {
    // Build 182: its own table may be asked for again; the replay moved
    // off it goes back to it, from the moment showing.
    if (pastCopy_ == copyId) {
      pastFor_.clear();
      past_.reset();
      ++pastSerial_;
      if (hooks_.changed) hooks_.changed();
    }
    if (here && replay_ && replayAsideFrom_ == copyId) {
      for (size_t i = 0; i < feeds_.size(); ++i)
        if (feeds_[i].channelId == copyId) {
          switchReplay(i, SwitchWhy::UsedAgain);
          break;
        }
    }
    replayAsideFrom_.clear();
    return true;
  }
  // Playing that channel live: its feeds are read again, so the copy is
  // one of them. The undo of a move off it plays it again; otherwise what
  // plays stays first. (A replay goes on as it is; the next tune has it.)
  if (here && !replay_ && r->scope == setaside::Scope::Whole) {
    std::string first = asideFrom_ == copyId ? copyId : playingChannel();
    std::vector<Feed> fresh = store_.feeds(entry_);
    size_t start = fresh.size();
    for (size_t i = 0; i < fresh.size(); ++i)
      if (fresh[i].channelId == first) start = i;
    bool listed = false;
    for (const Feed& f : fresh) listed = listed || f.channelId == copyId;
    if (listed && start < fresh.size()) begin(entry_, std::move(fresh), {}, start);
  }
  asideFrom_.clear();
  return true;
}

}  // namespace marigold::engine
