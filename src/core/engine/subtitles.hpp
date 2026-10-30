// The captions of the stream playing, and what the player shows of them.
//
// They come one of two ways: read from the stream itself (the LG relay hands
// every byte on its way to the TV's player to bytes(), on the relay's
// thread), or decoded by the platform's player and handed over as tracks and
// lines (Android). Either way the player screen draws them, the same on
// every platform; this only decides what is on screen now.
//
// Read from the stream, captions arrive as far ahead of the picture as the
// player's buffer: tick() holds each one back by that lead, so it shows with
// the frame it belongs to.
#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/captions/cea608.hpp"
#include "core/captions/ts_captions.hpp"

namespace marigold::engine {

struct CaptionTrack {
  std::string id;     // "cc1".."cc4" (from the stream), else the player's own
  std::string label;  // "English", "Español"
  bool operator==(const CaptionTrack& o) const { return id == o.id && label == o.label; }
};

// "en" / "eng" / "English" -> "English"; "" when unknown.
std::string languageName(const std::string& codeOrName);
// A 608 caption service's name: CC1 is the channel's own language, CC3 the
// second one US stations send ("English" / "Español").
std::string captionServiceLabel(int channel, const std::string& language);

class Subtitles {
 public:
  Subtitles();
  ~Subtitles();
  Subtitles(const Subtitles&) = delete;
  Subtitles& operator=(const Subtitles&) = delete;

  // A new stream. `session`: the relay session its bytes will carry (0: it
  // doesn't come through the relay). `language`: the channel's own, naming
  // the caption services ("English" when unknown).
  void begin(unsigned session, const std::string& language);
  void end();

  // The relay's thread: the stream as it reaches the player. Cheap; bytes of
  // another session are ignored.
  void bytes(const uint8_t* data, size_t n, unsigned session);

  // From a player that decodes captions itself. `known`: the player lists a
  // track only when the stream has one (tvOS); ExoPlayer lists CC1 for any
  // TS stream, so there captions count once text arrives.
  void playerTracks(std::vector<CaptionTrack> tracks, bool known);
  void playerLines(const std::vector<std::string>& lines);

  // Every frame while playing (UI thread). `leadMs`: how far the player's
  // buffer runs ahead of the picture (-1 unknown); `showing`: the picture is
  // up. True when what is on screen changed.
  bool tick(int64_t nowMs, int64_t leadMs, bool showing);

  // The stream carries captions (text has been seen, or the player lists a
  // track).
  bool present() const;
  std::vector<CaptionTrack> tracks() const;
  // The track for a language picked earlier ("" = the channel's own), or the
  // first; "" when there are none.
  std::string pick(const std::string& language) const;
  void select(const std::string& trackId);  // "" = none shown
  const std::string& selected() const { return selected_; }
  std::string selectedLabel() const;

  // On screen now, top to bottom.
  const std::vector<captions::Line>& lines() const { return lines_; }
  // Captions read from the stream (all capitals in the US) rather than a
  // player's text track.
  bool fromStream() const { return session_ != 0; }

 private:
  mutable std::mutex mu_;  // the relay thread's side: ts_, queue_, newest_
  std::unique_ptr<captions::TsCaptions> ts_;
  std::deque<captions::CcPair> queue_;
  std::vector<captions::CcPair> ready_;  // reused for each chunk
  int64_t newest_ = -1;
  bool sawData_ = false;
  unsigned session_ = 0;  // read on the relay thread under mu_

  std::string language_ = "English";
  std::vector<captions::Cea608> cc_;  // CC1..CC4
  std::vector<CaptionTrack> playerTracks_;
  bool playerTracksKnown_ = false, sawPlayerText_ = false;
  std::vector<captions::Line> playerLines_;
  std::string selected_;
  std::vector<captions::Line> lines_;
};

}  // namespace marigold::engine
