#include "core/engine/breaks.hpp"

#include <algorithm>

namespace marigold::engine::breaks {
namespace {

constexpr const char* kDot = " \xc2\xb7 ";

long minutesOf(time::Seconds s) { return std::max<long>(1, static_cast<long>((s + 30) / 60)); }

double fraction(time::Seconds t, time::Seconds a, time::Seconds b) {
  double f = static_cast<double>(t - a) / static_cast<double>(std::max<time::Seconds>(1, b - a));
  return std::clamp(f, 0.0, 1.0);
}

}  // namespace

time::Seconds playFrom(const Break& b, const replay::Reach& r, time::Seconds earliest, time::Seconds now) {
  return replay::startOverTarget(r, b.to, earliest, now);
}

time::Seconds backTo(time::Seconds at, const replay::Reach& r, time::Seconds earliest, time::Seconds now) {
  return replay::startOverTarget(r, at, earliest, now);
}

Moment moment(const std::vector<Break>& list, time::Seconds at, time::Seconds now, const replay::Reach& r,
              time::Seconds earliest, time::Seconds skipped) {
  for (const Break& b : list) {
    bool in = b.open ? at >= b.from : b.from <= at && at < b.to;
    if (!in) continue;
    // The break just skipped: the minute played from can still be in it.
    if (skipped && b.from == skipped) return {};
    Moment m;
    m.in = &b;
    if (b.open) {
      m.kind = Moment::Kind::Live;
      return m;
    }
    time::Seconds target = playFrom(b, r, earliest, now);
    if (now - target < kNearLive || now - b.to < kNearLive) {
      // The next play is (about) live: nothing to skip to but live.
      m.kind = Moment::Kind::Live;
      return m;
    }
    if (target - at < kLeastSaved) return {};
    m.kind = Moment::Kind::Skip;
    m.target = target;
    m.saves = target - at;
    return m;
  }
  return {};
}

std::string about(time::Seconds s) { return "about " + std::to_string(minutesOf(s)) + " min"; }

std::string tagText(const Moment& m) {
  switch (m.kind) {
    case Moment::Kind::Skip: return about(m.saves) + kDot + "OK to skip";
    case Moment::Kind::Live: return "live is in it too";
    case Moment::Kind::None: break;
  }
  return {};
}

std::string skippedTitle(time::Seconds saved) { return "Skipped a " + std::to_string(minutesOf(saved)) + " min break"; }

std::string lengthText(time::Seconds s) {
  s = std::max<time::Seconds>(0, s);
  if (s < 60) return std::to_string(s) + " s";
  std::string out = std::to_string(s / 60) + " min";
  if (s % 60) out += " " + std::to_string(s % 60) + " s";
  return out;
}

std::vector<time::Seconds> stops(const std::vector<Break>& list, const replay::Reach& r, time::Seconds earliest,
                                 time::Seconds now) {
  std::vector<time::Seconds> out;
  for (const Break& b : list) {
    if (b.open) continue;
    time::Seconds t = playFrom(b, r, earliest, now);
    if (now - t >= kNearLive && t >= earliest) out.push_back(t);
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

std::vector<Block> blocks(const std::vector<Break>& list, time::Seconds from, time::Seconds to, time::Seconds now) {
  std::vector<Block> out;
  if (to <= from) return out;
  for (const Break& b : list) {
    time::Seconds a = std::max(b.from, from);
    time::Seconds e = std::min(b.open ? std::max(b.to, now) : b.to, to);
    if (e <= a) continue;
    out.push_back({fraction(a, from, to), fraction(e, from, to), b.halftime});
  }
  return out;
}

}  // namespace marigold::engine::breaks
