#include "core/engine/subtitles.hpp"

#include <algorithm>
#include <cstdlib>

#include "core/captions/ts_captions.hpp"
#include "core/util/strings.hpp"

namespace marigold::engine {
namespace {

// Held for the picture at most this long: no TV player buffers more of a
// live stream (Android's keeps up to 30 s).
constexpr int64_t kMaxLeadMs = 30000;
// When the player says neither its position nor its buffer: a TV player's
// live buffer is a few seconds.
constexpr int64_t kDefaultLeadMs = 3000;
// Pairs kept waiting for their picture (a minute of captions is ~3600).
constexpr size_t kQueueCap = 20000;
// The player's position is carried on between its reports (webOS: every
// ~200 ms) at most this long; no report for longer means it stopped.
constexpr int64_t kMaxCarryMs = 1000;
// A report this far from where the clock was carried on to is a jump of
// the player's own (its position starting again): the clock carries on.
constexpr int64_t kPositionJumpMs = 3000;
// The first report comes this soon after the first picture, at most.
constexpr int64_t kFirstReportMs = 150;
// The player's clock can't be ahead of the pictures read; past this, its
// position isn't used (the buffer's lead stands in).
constexpr int64_t kAheadSlackMs = 500;
// A player that says its position: how long captions wait for the first
// report once the picture shows, before going by the buffer's lead.
constexpr int64_t kAwaitPositionMs = 2000;
// The playback log's figures: one sample every half second.
constexpr int64_t kSampleMs = 500;

int64_t median(std::vector<int64_t> v) {
  if (v.empty()) return 0;
  std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), v.end());
  return v[v.size() / 2];
}

}  // namespace

std::string captionTimingLog(const CaptionTiming& t) {
  std::string out = "clock " + t.clock;
  if (t.clock != "player") {
    out += "; ahead median " + std::to_string(t.aheadMedian) + " ms (min " + std::to_string(t.aheadMin) + ", max " +
           std::to_string(t.aheadMax) + ")";
    if (t.errorMs >= 0) out += "; error \xc2\xb1" + std::to_string(t.errorMs) + " ms";
    out += "; jumps " + std::to_string(t.jumps);
  }
  int a = std::abs(t.delayMs);
  std::string sign = t.delayMs < 0 ? "\xe2\x88\x92" : t.delayMs > 0 ? "+" : "";
  out += "; setting " + sign + std::to_string(a / 1000) + "." + std::to_string(a % 1000 / 100) + " s";
  return out;
}

std::string captionTimingLabel(int ms) {
  if (ms == 0) return "On time";
  int a = std::abs(ms);
  std::string n = std::to_string(a / 1000);
  if (a % 1000) n += "." + std::to_string(a % 1000 / 100);
  return n + (ms < 0 ? " s earlier" : " s later");
}

// US stations send their own language on CC1 and, when there is one, the
// second language on CC3 (CC2 and CC4 are rare).
std::string captionServiceLabel(int channel, const std::string& language) {
  std::string other = language == "English" ? "Espa\xc3\xb1ol" : "English";
  switch (channel) {
    case 1: return language;
    case 2: return language + " 2";
    case 3: return other;
    default: return other + " 2";
  }
}

std::string languageName(const std::string& codeOrName) {
  std::string s = str::lowerAscii(codeOrName);
  size_t cut = s.find_first_of("-_");
  if (cut != std::string::npos && cut >= 2) s = s.substr(0, cut);
  struct Name {
    const char *a, *b, *c, *name;
  };
  static const Name kNames[] = {
      {"en", "eng", "english", "English"},          {"es", "spa", "spanish", "Espa\xc3\xb1ol"},
      {"fr", "fre", "french", "Fran\xc3\xa7" "ais"}, {"de", "ger", "german", "Deutsch"},
      {"it", "ita", "italian", "Italiano"},         {"pt", "por", "portuguese", "Portugu\xc3\xaas"},
      {"nl", "dut", "dutch", "Nederlands"},         {"ar", "ara", "arabic", "\xd8\xa7\xd9\x84\xd8\xb9\xd8\xb1\xd8\xa8\xd9\x8a\xd8\xa9"},
  };
  for (const Name& n : kNames)
    if (s == n.a || s == n.b || s == n.c) return n.name;
  if (s == "fra") return "Fran\xc3\xa7" "ais";
  if (s == "deu") return "Deutsch";
  if (s == "nld") return "Nederlands";
  if (s == "espa\xc3\xb1ol") return "Espa\xc3\xb1ol";
  return {};
}

Subtitles::Subtitles() : ts_(std::make_unique<captions::TsCaptions>()) {
  for (int ch = 1; ch <= 4; ++ch) cc_.emplace_back(ch);
}

Subtitles::~Subtitles() = default;

void Subtitles::begin(unsigned session, const std::string& language) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    ts_->reset();
    queue_.clear();
    haveMedia_ = haveKey_ = false;
    newest_ = keyMedia_ = firstPts_ = 0;
    jumps_ = 0;
    sawData_ = false;
    session_ = session;
  }
  haveClock_ = anchored_ = false;
  clock_ = offset_ = 0;
  showingSince_ = -1;
  anchorNote_.clear();
  lastPos_ = -1;
  lastPosAt_ = 0;
  ranSince_ = false;
  jumpsSeen_ = jumpsLogged_ = 0;
  CaptionTiming discard;
  takeTiming(discard);
  pending_.clear();
  playerLatest_.clear();
  language_ = language.empty() ? "English" : language;
  for (captions::Cea608& d : cc_) d.reset();
  playerTracks_.clear();
  playerTracksKnown_ = sawPlayerText_ = false;
  playerLines_.clear();
  selected_.clear();
  lines_.clear();
}

void Subtitles::end() { begin(0, language_); }

void Subtitles::bytes(const uint8_t* data, size_t n, unsigned session) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!session || session != session_) return;
  ts_->feed(data, n);
  ts_->takeInto(ready_);
  for (const captions::CcPair& p : ready_) {
    if (queue_.size() >= kQueueCap) queue_.pop_front();
    queue_.push_back(p);
  }
  haveMedia_ = ts_->haveMedia();
  newest_ = ts_->newestMediaMs();
  haveKey_ = ts_->haveKey();
  keyMedia_ = ts_->keyMediaMs();
  firstPts_ = ts_->firstPtsMs();
  jumps_ = ts_->jumps();
  sawData_ = sawData_ || ts_->sawCaptionData();
}

void Subtitles::playerTracks(std::vector<CaptionTrack> tracks, bool known) {
  for (CaptionTrack& t : tracks)
    if (t.label.empty()) t.label = language_;
  playerTracks_ = std::move(tracks);
  playerTracksKnown_ = known;
}

void Subtitles::playerLines(const std::vector<std::string>& lines) {
  std::vector<captions::Line> out;
  int row = 15 - static_cast<int>(lines.size());
  for (const std::string& text : lines) {
    captions::Line l;
    l.text = text;
    l.row = std::max(0, row++);
    if (!str::trim(text).empty()) out.push_back(std::move(l));
  }
  if (!out.empty()) sawPlayerText_ = true;
  if (out == playerLatest_) return;
  playerLatest_ = out;
  // Shown by tick(), once the Caption timing has passed.
  if (pending_.size() >= 256) pending_.pop_front();
  pending_.push_back({lastNow_, std::move(out)});
}

bool Subtitles::tick(int64_t nowMs, int64_t leadMs, bool showing) {
  PlayerClock c;
  c.showing = showing;
  c.running = true;
  c.leadMs = leadMs;
  return tick(nowMs, c);
}

// The caption clock in media ms, before the Caption timing: the player's
// position mapped onto the stream's pictures, else the newest picture read
// less the buffer's lead. False while there is none yet.
bool Subtitles::streamClock(int64_t nowMs, const PlayerClock& c, int64_t newest, bool haveKey, int64_t keyMedia,
                            int64_t firstPts, Source& source, int64_t& clock) {
  if (!c.showing) showingSince_ = -1;
  else if (showingSince_ < 0) showingSince_ = nowMs;
  int64_t position = c.positionMs, age = c.positionAgeMs;
  if (position <= 0 && anchored_ && lastPos_ > 0) {
    // No position for now (it starts again from 0): the last one carries on.
    position = lastPos_;
    age = nowMs - lastPosAt_;
  }
  if (position > 0 && haveKey) {
    int64_t at = nowMs - age;  // when the player said it
    bool fresh = position != lastPos_ || at != lastPosAt_;
    if (!anchored_) {
      // The first report, just after the first picture a decoder can start
      // on showed: which way the position counts. From that picture, from
      // the stream's first timestamp, or the timestamp itself; whichever
      // puts the first report right after that picture.
      struct Way {
        int64_t offset;
        const char* note;
      };
      const Way ways[] = {{keyMedia, "the player's clock counts from the first picture"},
                          {0, "the player's clock counts from the stream's first timestamp"},
                          {-firstPts, "the player's clock is the stream's timestamp"}};
      auto miss = [&](const Way& w) { return std::llabs(position + w.offset - keyMedia - kFirstReportMs); };
      const Way* best = &ways[0];
      for (const Way& w : ways)
        if (miss(w) < miss(*best)) best = &w;
      offset_ = best->offset;
      anchorNote_ = best->note;
      anchored_ = true;
    } else if (fresh) {
      // A new report: how far it is from where the clock was carried on to.
      int64_t carried = lastPos_ + (ranSince_ ? std::min(at - lastPosAt_, kMaxCarryMs) : 0);
      int64_t off = position - carried;
      if (std::llabs(off) > kPositionJumpMs) {
        offset_ -= off;  // the player's position started again: the clock carries on
        ++positionJumps_;
      } else if (ranSince_ && c.running && errors_.size() < 1000) {
        errors_.push_back(std::llabs(off));
      }
    }
    if (fresh) {
      lastPos_ = position;
      lastPosAt_ = at;
      ranSince_ = false;
    }
    ranSince_ = ranSince_ || c.running;
    clock = position + offset_ + (c.running ? std::min(age, kMaxCarryMs) : 0);
    if (clock <= newest + kAheadSlackMs) {
      source = Source::Position;
      return true;
    }
  } else if (c.reportsPosition && !anchored_ && (showingSince_ < 0 || nowMs - showingSince_ < kAwaitPositionMs)) {
    return false;  // its first report is on its way
  }
  if (c.leadMs >= 0) {
    source = Source::Buffer;
    clock = newest - std::min(c.leadMs, kMaxLeadMs);
  } else {
    source = Source::Default;
    clock = newest - kDefaultLeadMs;
  }
  return true;
}

void Subtitles::sample(int64_t nowMs, Source source, int64_t ahead) {
  if (sampleAt_ >= 0 && nowMs - sampleAt_ < kSampleMs) return;
  sampleAt_ = nowMs;
  ++samples_;
  ++sourceCount_[static_cast<int>(source)];
  if (source != Source::None && ahead_.size() < 1000) ahead_.push_back(ahead);
}

bool Subtitles::tick(int64_t nowMs, const PlayerClock& c) {
  lastNow_ = nowMs;
  bool on = c.showing && !selected_.empty();
  if (session_) {
    std::vector<captions::CcPair> due;
    bool have = false, haveKey = false;
    int64_t newest = 0, keyMedia = 0, firstPts = 0;
    {
      std::lock_guard<std::mutex> lock(mu_);
      have = haveMedia_;
      haveKey = haveKey_;
      newest = newest_;
      keyMedia = keyMedia_;
      firstPts = firstPts_;
      jumpsSeen_ = jumps_;
    }
    Source source = Source::None;
    int64_t clock = 0;
    if (have && streamClock(nowMs, c, newest, haveKey, keyMedia, firstPts, source, clock)) {
      if (haveClock_) {
        // Waiting for data or paused, the picture holds and so do captions;
        // playing, the clock never goes back.
        clock = c.running ? std::max(clock, clock_) : clock_;
      }
      haveClock_ = true;
      clock_ = clock;
      // Caption timing: later, or earlier as far as the stream read allows.
      int64_t shown = std::min(clock - delayMs_, std::max(clock, newest));
      {
        std::lock_guard<std::mutex> lock(mu_);
        // Never held back more than any buffer holds.
        while (!queue_.empty() && (queue_.front().mediaMs <= shown || newest - queue_.front().mediaMs > kMaxLeadMs)) {
          due.push_back(queue_.front());
          queue_.pop_front();
        }
      }
      for (const captions::CcPair& p : due)
        for (captions::Cea608& d : cc_) d.feed(p.field, p.b1, p.b2, p.mediaMs);
      for (captions::Cea608& d : cc_) d.tick(shown);
      if (on) sample(nowMs, source, newest - clock);
    }
  } else {
    // A player's own lines, once the Caption timing (only later) has passed.
    int64_t hold = std::max(delayMs_, 0);
    while (!pending_.empty() && pending_.front().atMs + hold <= nowMs) {
      playerLines_ = std::move(pending_.front().lines);
      pending_.pop_front();
    }
    if (on) sample(nowMs, Source::None, 0);
  }
  std::vector<captions::Line> before = lines_;
  if (!c.showing || selected_.empty()) {
    lines_.clear();
  } else if (selected_.size() == 3 && selected_.compare(0, 2, "cc") == 0 && session_) {
    int ch = selected_[2] - '0';
    lines_ = ch >= 1 && ch <= 4 ? cc_[static_cast<size_t>(ch - 1)].screen().lines : std::vector<captions::Line>{};
  } else {
    lines_ = playerLines_;
  }
  return !(lines_ == before);
}

bool Subtitles::takeTiming(CaptionTiming& out) {
  out = CaptionTiming{};
  bool any = samples_ > 0;
  if (any) {
    static const char* const kNames[] = {"player", "currentTime", "buffer", "default"};
    int most = 0;
    for (int i = 1; i < 4; ++i)
      if (sourceCount_[i] > sourceCount_[most]) most = i;
    out.clock = kNames[most];
    out.samples = samples_;
    if (!ahead_.empty()) {
      out.aheadMedian = median(ahead_);
      out.aheadMin = *std::min_element(ahead_.begin(), ahead_.end());
      out.aheadMax = *std::max_element(ahead_.begin(), ahead_.end());
    }
    out.errorMs = errors_.empty() ? -1 : median(errors_);
    out.jumps = jumpsSeen_ - jumpsLogged_ + positionJumps_;
    out.delayMs = delayMs_;
  }
  samples_ = 0;
  for (int& n : sourceCount_) n = 0;
  ahead_.clear();
  errors_.clear();
  jumpsLogged_ = jumpsSeen_;
  positionJumps_ = 0;
  sampleAt_ = -1;
  return any;
}

std::string Subtitles::takeAnchorNote() {
  std::string out;
  out.swap(anchorNote_);
  return out;
}

bool Subtitles::present() const {
  if (!playerTracks_.empty() && (playerTracksKnown_ || sawPlayerText_)) return true;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (sawData_) return true;
  }
  for (const captions::Cea608& d : cc_)
    if (d.sawText()) return true;
  return false;
}

std::vector<CaptionTrack> Subtitles::tracks() const {
  if (!playerTracks_.empty()) return playerTracks_;
  std::vector<CaptionTrack> out;
  for (int ch = 1; ch <= 4; ++ch)
    if (cc_[static_cast<size_t>(ch - 1)].sawText()) out.push_back({"cc" + std::to_string(ch), captionServiceLabel(ch, language_)});
  return out;
}

std::string Subtitles::pick(const std::string& language) const {
  std::vector<CaptionTrack> all = tracks();
  if (all.empty()) return {};
  std::string want = language.empty() ? language_ : language;
  for (const CaptionTrack& t : all)
    if (t.label == want) return t.id;
  for (const CaptionTrack& t : all)
    if (t.label == language_) return t.id;
  return all.front().id;
}

void Subtitles::select(const std::string& trackId) { selected_ = trackId; }

std::string Subtitles::selectedLabel() const {
  for (const CaptionTrack& t : tracks())
    if (t.id == selected_) return t.label;
  return {};
}

}  // namespace marigold::engine
