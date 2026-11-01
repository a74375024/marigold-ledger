// How many logos are decoded and prepared at once (LogoCache, Build 119):
// two, whether the bytes came from the disk or the network, so a fast
// scroll leaves the other cores to the UI thread and the rest of the app.
// A job asked for while two run waits its turn, in the order asked. UI
// thread only; the jobs themselves run on workers.
#pragma once

#include <algorithm>
#include <deque>
#include <functional>
#include <utility>

namespace marigold::logos {

class PrepareGate {
 public:
  static constexpr int kMax = 2;
  explicit PrepareGate(int max = kMax) : max_(max) {}

  bool full() const { return running_ >= max_; }
  int running() const { return running_; }
  size_t waiting() const { return waiting_.size(); }

  // Starts the job now when there is room (`start` posts it), else when a
  // running one ends.
  void run(std::function<void()> start) {
    waiting_.push_back(std::move(start));
    next();
  }
  // A job's done half: the next one waiting starts.
  void ended() {
    if (running_ > 0) --running_;
    next();
  }
  // Everything dropped (the pool's work was cancelled): nothing runs or
  // waits any more.
  void reset() {
    running_ = 0;
    waiting_.clear();
  }

  // For the timing log: jobs done, the longest one's worker time, and the
  // most running at once since the last take(). Counts and milliseconds.
  struct Stats {
    int prepared = 0;
    double maxMs = 0;
    int maxRunning = 0;
  };
  void took(double ms) {
    ++stats_.prepared;
    stats_.maxMs = std::max(stats_.maxMs, ms);
  }
  Stats take() {
    Stats out = stats_;
    stats_ = Stats{};
    return out;
  }

 private:
  void next() {
    while (running_ < max_ && !waiting_.empty()) {
      std::function<void()> start = std::move(waiting_.front());
      waiting_.pop_front();
      ++running_;
      stats_.maxRunning = std::max(stats_.maxRunning, running_);
      if (start) start();
    }
  }

  int max_;
  int running_ = 0;
  std::deque<std::function<void()>> waiting_;
  Stats stats_;
};

}  // namespace marigold::logos
