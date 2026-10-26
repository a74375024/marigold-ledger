// What is playing: one entry, its feeds, the flip list it was picked from,
// and the previous channel. The guide preview and full screen share it, so
// OK on the previewed channel just grows the same picture (no second tune).
//
// Failover in this build: a feed that fails, or shows no first frame in 20 s,
// gives way to the next feed of the same channel, silently. The stall
// watchdog and reconnects come with milestone 2.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app/store/model.hpp"
#include "platform/gfx.hpp"

namespace marigold::app {

class App;

class Playback {
 public:
  explicit Playback(App& app) : app_(app) {}

  // Starts `entryId` (or keeps it if it is already playing). `flip` is the
  // list CH+/CH- and Up/Down move through in the player. A tune in the
  // guide corner is a preview: it is not "watched" (Recent, previous
  // channel) until it goes full screen.
  bool tune(const std::string& entryId, const std::vector<std::string>& flip = {});
  // Plays a specific feed of the current entry (Sources panel).
  bool useFeed(size_t index);
  void stop();

  // Moves through the flip list: +1 next, -1 previous. Returns the entry.
  std::string flip(int step);
  // Back to the previous channel (cable LAST).
  std::string previous();

  void setFull();
  void setCorner(gfx::Rect r);
  bool full() const { return full_; }

  // UI thread, every loop: failover. True while the picture is opening.
  bool tick(uint32_t ms);

  const std::string& entry() const { return entry_; }
  const std::string& previousEntry() const { return previous_; }
  const std::vector<Feed>& feeds() const { return feeds_; }
  const std::vector<std::string>& flipList() const { return flip_; }
  size_t feedIndex() const { return feed_; }
  bool active() const { return !entry_.empty(); }
  bool ready() const { return ready_; }
  bool failed() const { return failed_; }
  // Set when failover moved to another feed (the note at the top right).
  uint32_t switchedAt() const { return switchedAt_; }
  uint32_t tunedAt() const { return tunedAt_; }

 private:
  bool start(size_t feed);
  void watched();
  gfx::Rect window() const;
  void scheduleReapply();

  App& app_;
  std::string entry_, previous_, lastWatched_;
  std::vector<std::string> flip_;
  std::vector<Feed> feeds_;
  size_t feed_ = 0;
  size_t tried_ = 0;
  bool full_ = false;
  bool ready_ = false;
  bool failed_ = false;
  bool openFailed_ = false;
  uint64_t attempt_ = 0;
  uint32_t tunedAt_ = 0, feedAt_ = 0, switchedAt_ = 0;
  gfx::Rect corner_{};
  uint32_t reapplyAt_[3] = {0, 0, 0};
  bool reapplyOnReady_ = false;
};

}  // namespace marigold::app
