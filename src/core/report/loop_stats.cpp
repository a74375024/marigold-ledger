#include "core/report/loop_stats.hpp"

#include <cstdio>

namespace marigold::report {
namespace {

// The clock's redraw each minute is not something happening.
constexpr int kQuietFrames = 2;

double d(float v) { return static_cast<double>(v); }

}  // namespace

void LoopStats::step(const Times& ms, bool drew, bool hidden, bool worked) {
  hidden_ = hidden;
  if (hidden) {
    ++c_.hiddenWakeups;
    if (worked) ++c_.hiddenWork;
    return;
  }
  ++c_.steps;
  if (drew) ++c_.frames;
  float total = 0;
  for (int p = 0; p < kParts; ++p) {
    total += ms[p];
    if (ms[p] > c_.partWorst[p]) c_.partWorst[p] = ms[p];
    if (ms[p] > kSlowMs) ++c_.partSlow[p];
  }
  if (total > kSlowMs) ++c_.slow;
  if (total > c_.worst) {
    c_.worst = total;
    c_.worstSplit = ms;
  }
}

std::string LoopStats::line(uint32_t now, const char* screen) {
  if (!started_) {
    started_ = true;
    since_ = now;
    return {};
  }
  if (now - since_ < kEveryMs) return {};
  since_ = now;
  Counts c = c_;
  // What happened while hidden goes on to the next line logged.
  c_ = Counts{};
  c_.hiddenWakeups = c.hiddenWakeups;
  c_.hiddenWork = c.hiddenWork;
  if (hidden_) return {};
  if (c.frames <= kQuietFrames && c.slow == 0 && c.hiddenWakeups == 0) return {};
  c_.hiddenWakeups = c_.hiddenWork = 0;
  const Times& w = c.worstSplit;
  const Times& p = c.partWorst;
  char buf[512];
  std::snprintf(buf, sizeof buf,
                "%s: %d frames in %d steps; slowest %.1f ms (events %.1f, store %.1f, screen %.1f, draw %.1f); "
                "%d over 50 ms (events %d, store %d, screen %d, draw %d); each part's slowest: events %.1f, "
                "store %.1f, screen %.1f, draw %.1f ms; hidden: %d wakeups, %d with work",
                screen ? screen : "-", c.frames, c.steps, d(c.worst), d(w[Events]), d(w[Store]), d(w[Screen]),
                d(w[Draw]), c.slow, c.partSlow[Events], c.partSlow[Store], c.partSlow[Screen], c.partSlow[Draw],
                d(p[Events]), d(p[Store]), d(p[Screen]), d(p[Draw]), c.hiddenWakeups, c.hiddenWork);
  return buf;
}

}  // namespace marigold::report
