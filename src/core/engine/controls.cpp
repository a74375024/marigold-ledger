#include "core/engine/controls.hpp"

#include <algorithm>

#include "core/engine/replay.hpp"

namespace marigold::engine::controls {
namespace {

constexpr const char* kDot = " \xc2\xb7 ";
constexpr const char* kEllipsis = "\xe2\x80\xa6";
// When the replay's moment has no programme known: the bar starts this
// long before it.
constexpr time::Seconds kUnknownBefore = 30 * 60;
// Live with nothing on known: an hour back to half an hour on (as before).
constexpr time::Seconds kLiveBefore = 3600, kLiveAfter = 1800;
// The fewest letters a cut name keeps.
constexpr size_t kFewLetters = 3;

double fraction(time::Seconds t, time::Seconds a, time::Seconds b) {
  double f = static_cast<double>(t - a) / static_cast<double>(std::max<time::Seconds>(1, b - a));
  return std::clamp(f, 0.0, 1.0);
}

// The byte offsets where each UTF-8 code point after the first starts, and
// the end.
std::vector<size_t> boundaries(const std::string& s) {
  std::vector<size_t> out;
  for (size_t i = 1; i < s.size(); ++i)
    if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) out.push_back(i);
  out.push_back(s.size());
  return out;
}

}  // namespace

Group groupOf(Button b) {
  switch (b) {
    case Button::Live:
    case Button::Pause:
    case Button::Rewind:
    case Button::StartOver:
    case Button::SkipBreak: return Group::Watching;
    case Button::Subtitles:
    case Button::Sources:
    case Button::Swap:
    case Button::Cameras: return Group::Channel;
    default: return Group::Elsewhere;
  }
}

std::vector<Button> row(const RowIn& in) {
  std::vector<Button> out;
  // Build 169: replay's buttons as engine::replay::buttons has them (Build
  // 184: Pause after Live behind live, every remote); Build 170: Start over
  // after Rewind; Build 177: Skip break after it.
  for (replay::Button b : replay::buttons(in.replayDays, in.behind, in.startOver, in.skipBreak)) {
    switch (b) {
      case replay::Button::GoLive: out.push_back(Button::Live); break;
      case replay::Button::Pause: out.push_back(Button::Pause); break;
      case replay::Button::Rewind: out.push_back(Button::Rewind); break;
      case replay::Button::StartOver: out.push_back(Button::StartOver); break;
      case replay::Button::SkipBreak: out.push_back(Button::SkipBreak); break;
    }
  }
  out.push_back(Button::Subtitles);
  out.push_back(Button::Sources);
  if (in.bothVersions) out.push_back(Button::Swap);
  if (in.cameras) out.push_back(Button::Cameras);
  if (in.back) out.push_back(Button::Back);
  out.push_back(Button::Guide);
  if (in.favorite) out.push_back(Button::Favorite);
  out.push_back(Button::Settings);
  return out;
}

std::optional<time::Seconds> startOver(const StartOverIn& in) {
  if (!in.replayDays) return std::nullopt;
  time::Seconds shown = in.replayAt ? std::min(*in.replayAt, in.now) : in.now;
  // A game from its scheduled start once it began; else the programme on.
  std::optional<time::Seconds> start = in.gameStart && *in.gameStart <= shown ? in.gameStart : in.showStart;
  if (!start || *start > shown || shown - *start <= replay::kStartOverAfter) return std::nullopt;
  if (!replay::covers(in.replayDays, *start, in.now)) return std::nullopt;
  return start;
}

int opening(const std::vector<Button>& row, bool behind) {
  // Build 177: a break to skip comes first.
  for (size_t i = 0; i < row.size(); ++i)
    if (row[i] == Button::SkipBreak) return static_cast<int>(i);
  Button first = behind ? Button::Live : Button::Subtitles;
  for (size_t i = 0; i < row.size(); ++i)
    if (row[i] == first) return static_cast<int>(i);
  for (size_t i = 0; i < row.size(); ++i)
    if (row[i] == Button::Subtitles) return static_cast<int>(i);
  return -1;
}

Label label(Button b, const LabelIn& in) {
  Label l;
  switch (b) {
    case Button::Live:
      // Build 167: a Final's replay: the game has no live left, the
      // channel has (Build 183: "Watch NBA TV live"). Build 183: in a
      // replay, "Go live · 12 min behind".
      if (!in.finalChannel.empty()) {
        l.title = "Watch " + in.finalChannel + " live";
      } else if (in.replay) {
        l.title = "Go live";
        l.state = in.behind;
      } else {
        l.title = "Live";
      }
      break;
    case Button::Pause: l.title = "Pause"; break;
    // Build 183: in a replay the timeline goes either way: "Jump to".
    case Button::Rewind: l.title = in.replay ? "Jump to" : "Rewind"; break;
    case Button::StartOver:
      // Build 170: "Start over · from 7:30".
      l.title = "Start over";
      if (!in.startOverFrom.empty()) l.state = "from " + in.startOverFrom;
      break;
    case Button::SkipBreak:
      // Build 177: "Skip break · about 2 min".
      l.title = "Skip break";
      l.state = in.skipAbout;
      break;
    case Button::Subtitles:
      l.title = "Subtitles";
      l.dot = in.subtitlesOn;
      if (!in.subtitlesOn) l.state = "Off";
      else l.state = in.subtitles.empty() || in.subtitles == "Off" ? std::string("On") : in.subtitles;
      break;
    case Button::Sources:
      l.title = "Sources";
      l.state = in.source;
      if (!in.quality.empty()) l.state += (l.state.empty() ? "" : kDot) + in.quality;
      break;
    case Button::Swap: l.title = in.west ? "Watch East" : "Watch West"; break;
    case Button::Cameras: l.title = "Cameras"; break;
    case Button::Back: l.title = in.backTo.empty() ? std::string("Back") : "Back to " + in.backTo; break;
    case Button::Guide: l.title = in.game ? "Games" : "Guide"; break;
    case Button::Favorite: l.title = in.favorite ? "Unfavorite" : "Favorite"; break;
    case Button::Settings: l.title = "Settings"; break;
  }
  return l;
}

std::string text(const Label& l) { return l.state.empty() ? l.title : l.title + kDot + l.state; }

RowLayout layout(const std::vector<Button>& row, int focused, float focusedWidth, float room) {
  RowLayout out;
  if (row.empty()) return out;
  // What the row takes with every button round.
  float plain = 0;
  for (size_t i = 0; i < row.size(); ++i) {
    if (i > 0) plain += groupOf(row[i]) != groupOf(row[i - 1]) ? kDivider : kGap;
    plain += kSize;
  }
  bool has = focused >= 0 && static_cast<size_t>(focused) < row.size();
  float grown = has ? std::clamp(focusedWidth, kSize, std::max(kSize, room - plain + kSize)) : kSize;
  float x = 0;
  for (size_t i = 0; i < row.size(); ++i) {
    if (i > 0) {
      bool divide = groupOf(row[i]) != groupOf(row[i - 1]);
      if (divide) out.dividers.push_back(x + kDivider * 0.5f);
      x += divide ? kDivider : kGap;
    }
    float w = has && static_cast<int>(i) == focused ? grown : kSize;
    out.slots.push_back({x, w});
    x += w;
  }
  out.width = x;
  return out;
}

Bar bar(const std::vector<Listing>& shows, const Listing* live, std::optional<time::Seconds> replayAt,
        time::Seconds now) {
  Bar b;
  if (!replayAt) {
    // Live: the show on now, start to end, the moment at now.
    b.show = live;
    b.from = live ? live->start : now - kLiveBefore;
    b.to = live ? live->end : now + kLiveAfter;
    b.at = fraction(now, b.from, b.to);
    if (live) {
      long in = static_cast<long>(std::max<time::Seconds>(0, now - live->start) / 60);
      long left = static_cast<long>((live->end - now + 59) / 60);
      b.line = live->title.empty() ? std::string() : live->title + kDot;
      b.line += std::to_string(in) + " min in";
      if (left > 0) b.line += kDot + std::to_string(left) + " min left";
    }
    return b;
  }
  // Behind live: from the programme at the moment to live, always the
  // right end.
  time::Seconds at = std::min(*replayAt, now);
  b.replay = true;
  for (const Listing& l : shows)
    if (l.isOn(at)) {
      b.show = &l;
      break;
    }
  b.from = b.show ? b.show->start : at - kUnknownBefore;
  b.to = std::max(now, b.from + 1);
  b.at = fraction(at, b.from, b.to);
  time::Seconds last = at;
  for (const Listing& l : shows) {
    if (l.start <= at || l.start >= now) continue;
    if (l.start == last) continue;  // the same start twice
    b.ticks.push_back({fraction(l.start, b.from, b.to), l.title});
    last = l.start;
  }
  return b;
}

std::string fit(const std::string& s, float room, const Measure& measure) {
  if (s.empty() || room <= 0) return {};
  if (measure(s) <= room) return s;
  std::vector<size_t> ends = boundaries(s);  // ends[k - 1]: the end of the first k letters
  for (size_t k = ends.size() - 1; k >= kFewLetters; --k) {
    std::string cut = s.substr(0, ends[k - 1]);
    while (!cut.empty() && cut.back() == ' ') cut.pop_back();
    if (cut.empty()) continue;
    std::string shown = cut + kEllipsis;
    if (measure(shown) <= room) return shown;
  }
  return {};
}

TickName tickName(const Bar& b, float width, float clockRight, const Measure& measure) {
  TickName out;
  if (!b.replay || b.ticks.empty()) return out;
  const Bar::Tick& first = b.ticks.front();
  float tick = static_cast<float>(first.at) * width;
  float end = b.ticks.size() > 1 ? static_cast<float>(b.ticks[1].at) * width : width;
  out.x = std::max(tick + kNameAfterTick, clockRight + kNameRoom);
  out.text = fit(first.title, end - kNameRoom - out.x, measure);
  return out;
}

}  // namespace marigold::engine::controls
