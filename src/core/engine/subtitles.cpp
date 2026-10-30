#include "core/engine/subtitles.hpp"

#include <algorithm>

#include "core/captions/ts_captions.hpp"
#include "core/util/strings.hpp"

namespace marigold::engine {
namespace {

// Held for the picture at most this long (a burst at the start of a live
// stream fills the buffer far ahead; nothing waits longer than this).
constexpr int64_t kMaxLeadMs = 10000;
// When the player doesn't say how far ahead its buffer runs.
constexpr int64_t kDefaultLeadMs = 1000;
// Pairs kept waiting for their picture (a minute of captions is ~3600).
constexpr size_t kQueueCap = 20000;

}  // namespace

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
    newest_ = -1;
    sawData_ = false;
    session_ = session;
  }
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
  newest_ = ts_->newestPtsMs();
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
  if (out == playerLines_) return;
  playerLines_ = std::move(out);
}

bool Subtitles::tick(int64_t nowMs, int64_t leadMs, bool showing) {
  (void)nowMs;
  if (session_) {
    std::vector<captions::CcPair> due;
    int64_t clock = -1;
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (newest_ >= 0) {
        int64_t lead = leadMs < 0 ? kDefaultLeadMs : std::min(leadMs, kMaxLeadMs);
        clock = newest_ - lead;
        // Never hold back more than the buffer can hold.
        while (!queue_.empty() && (queue_.front().ptsMs <= clock || newest_ - queue_.front().ptsMs > kMaxLeadMs)) {
          due.push_back(queue_.front());
          queue_.pop_front();
        }
      }
    }
    for (const captions::CcPair& p : due)
      for (captions::Cea608& d : cc_) d.feed(p.field, p.b1, p.b2, p.ptsMs);
    if (clock >= 0)
      for (captions::Cea608& d : cc_) d.tick(clock);
  }
  std::vector<captions::Line> before = lines_;
  if (!showing || selected_.empty()) {
    lines_.clear();
  } else if (selected_.size() == 3 && selected_.compare(0, 2, "cc") == 0 && session_) {
    int ch = selected_[2] - '0';
    lines_ = ch >= 1 && ch <= 4 ? cc_[static_cast<size_t>(ch - 1)].screen().lines : std::vector<captions::Line>{};
  } else {
    lines_ = playerLines_;
  }
  return !(lines_ == before);
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
