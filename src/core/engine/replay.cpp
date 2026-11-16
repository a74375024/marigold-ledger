#include "core/engine/replay.hpp"

#include <algorithm>
#include <cstdio>

namespace marigold::engine::replay {
namespace {

constexpr time::Seconds kMinute = 60;
constexpr time::Seconds kHalfHour = 30 * 60;
// Build 166: an archive's oldest hour is left alone. A provider keeping one
// day failed a request for exactly 24 h back (its clock, its trimming).
constexpr time::Seconds kArchiveMargin = 3600;
// A playhead this close after a programme's start is at its start.
constexpr time::Seconds kAtStart = 30;
// Repeat events before a held key moves faster: five minutes, then fifteen.
constexpr int kRepeatsToFive = 6;
constexpr int kRepeatsToFifteen = 16;

time::Seconds clampTo(time::Seconds t, time::Seconds earliest, time::Seconds live) {
  if (live < earliest) return live;
  return std::clamp(t, earliest, live);
}

time::Seconds floorDiv(time::Seconds a, time::Seconds b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

}  // namespace

time::Seconds minuteOf(time::Seconds t) { return floorDiv(t, kMinute) * kMinute; }

time::Seconds archiveStart(time::Seconds now, uint16_t days) {
  time::Seconds back = now - static_cast<time::Seconds>(days) * 86400 + kArchiveMargin;
  return floorDiv(back + kMinute - 1, kMinute) * kMinute;
}

bool covers(uint16_t days, time::Seconds at, time::Seconds now) {
  return days > 0 && at >= archiveStart(now, days);
}

uint16_t usableDays(uint16_t claimed, int archiveOk, uint16_t measuredDays) {
  if (claimed == 0 || archiveOk == 0) return 0;
  if (archiveOk == 1 && measuredDays > 0) return std::min(claimed, measuredDays);
  return claimed;
}

bool startOverOffered(time::Seconds start, time::Seconds end, time::Seconds now, uint16_t days) {
  if (days == 0 || !(start <= now && now < end)) return false;
  if (now - start <= kStartOverAfter) return false;
  return covers(days, start, now);
}

int stepMinutes(int repeats) {
  if (repeats >= kRepeatsToFifteen) return 15;
  if (repeats >= kRepeatsToFive) return 5;
  return 1;
}

time::Seconds step(time::Seconds at, int dir, int repeats, time::Seconds earliest, time::Seconds live) {
  time::Seconds by = static_cast<time::Seconds>(stepMinutes(repeats)) * kMinute;
  // On whole minutes: off one (live, a click), the first step back lands
  // on the minute it is in.
  time::Seconds to = dir < 0 ? minuteOf(at + kMinute - 1) - by : minuteOf(at) + by;
  return clampTo(to, earliest, live);
}

const Programme* on(const std::vector<Programme>& list, time::Seconds t) {
  for (const Programme& p : list)
    if (p.start <= t && t < p.end) return &p;
  return nullptr;
}

time::Seconds jump(const std::vector<Programme>& list, time::Seconds at, int dir, time::Seconds earliest,
                   time::Seconds live) {
  // In a gap of the listings (or with none), half hours.
  bool gap = on(list, at) == nullptr;
  if (dir < 0) {
    const Programme* best = nullptr;
    for (const Programme& p : list)
      if (p.start < at - kAtStart && (!best || p.start > best->start)) best = &p;
    time::Seconds half = floorDiv(at - kAtStart - 1, kHalfHour) * kHalfHour;
    time::Seconds to = best ? best->start : half;
    if (best && gap && half > best->start) to = half;
    return clampTo(to, earliest, live);
  }
  const Programme* best = nullptr;
  for (const Programme& p : list)
    if (p.start > at + kAtStart && (!best || p.start < best->start)) best = &p;
  time::Seconds half = (floorDiv(at + kAtStart, kHalfHour) + 1) * kHalfHour;
  time::Seconds to = best ? best->start : (gap ? half : live);
  if (best && gap && half < best->start) to = half;
  if (to >= live) return live;
  return clampTo(to, earliest, live);
}

Span opening(const std::vector<Programme>& list, time::Seconds live, time::Seconds earliest) {
  Span s;
  s.to = live;
  s.from = live - 2 * 3600;
  if (const Programme* now = on(list, live)) {
    const Programme* before = nullptr;
    for (const Programme& p : list)
      if (p.start < now->start && (!before || p.start > before->start)) before = &p;
    time::Seconds start = before ? before->start : now->start;
    s.from = std::min(s.from, start);
  }
  // Never wider than the archive, nor a day.
  s.from = std::max({s.from, earliest, live - 24 * 3600});
  if (s.to - s.from < 10 * kMinute) s.from = s.to - 10 * kMinute;
  return s;
}

Span follow(Span shown, time::Seconds at, time::Seconds earliest, time::Seconds live) {
  time::Seconds len = std::max<time::Seconds>(shown.length(), 10 * kMinute);
  if (at >= shown.from && at <= shown.to && shown.to <= live) return shown;
  Span s = shown;
  if (at < shown.from) {
    s.from = at - len / 10;
    s.to = s.from + len;
  } else {
    s.to = at + len / 10;
    s.from = s.to - len;
  }
  if (s.to > live) {
    s.to = live;
    s.from = live - len;
  }
  if (s.from < earliest - len / 10) {
    s.from = earliest - len / 10;
    s.to = s.from + len;
  }
  return s;
}

time::Seconds at(Span shown, double fraction, time::Seconds earliest, time::Seconds live) {
  fraction = std::clamp(fraction, 0.0, 1.0);
  auto t = shown.from + static_cast<time::Seconds>(fraction * static_cast<double>(shown.length()) + 0.5);
  if (t >= live) return live;
  return clampTo(minuteOf(t), earliest, live);
}

std::vector<Programme> merged(std::vector<Programme> guide, const std::vector<Programme>& own) {
  std::vector<Programme> out = std::move(guide);
  size_t fromGuide = out.size();
  for (const Programme& p : own) {
    time::Seconds len = std::max<time::Seconds>(1, p.end - p.start);
    bool covered = false;
    for (size_t i = 0; i < fromGuide && !covered; ++i) {
      const Programme& g = out[i];
      time::Seconds overlap = std::min(p.end, g.end) - std::max(p.start, g.start);
      covered = overlap * 2 > len;
    }
    if (!covered) out.push_back(p);
  }
  std::stable_sort(out.begin(), out.end(), [](const Programme& a, const Programme& b) { return a.start < b.start; });
  return out;
}

std::string behindText(time::Seconds behind) {
  behind = std::max<time::Seconds>(0, behind);
  char buf[32];
  if (behind >= 3600)
    std::snprintf(buf, sizeof buf, "\xe2\x88\x92%lld:%02d:%02d", static_cast<long long>(behind / 3600),
                  static_cast<int>(behind / 60 % 60), static_cast<int>(behind % 60));
  else
    std::snprintf(buf, sizeof buf, "\xe2\x88\x92%d:%02d", static_cast<int>(behind / 60), static_cast<int>(behind % 60));
  return buf;
}

std::string behindMinutes(time::Seconds behind) {
  long long m = std::max<long long>(0, (static_cast<long long>(behind) + 30) / 60);
  char buf[32];
  if (m >= 60)
    std::snprintf(buf, sizeof buf, "\xe2\x88\x92%lld h %02lld min", m / 60, m % 60);
  else
    std::snprintf(buf, sizeof buf, "\xe2\x88\x92%lld min", m);
  return buf;
}

}  // namespace marigold::engine::replay
