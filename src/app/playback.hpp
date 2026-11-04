// The app's face of the engine's playback session (core/engine/session):
// the platform's video player (with the local TS proxy on webOS) behind
// engine::Player, and where the picture sits (full screen or the guide
// corner). Everything the session decides (feeds, backups, watched,
// previous, the log) is the engine's.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/store/store.hpp"
#include "core/engine/session.hpp"
#include "core/engine/subtitles.hpp"
#include "core/engine/views.hpp"
#include "platform/gfx.hpp"

namespace marigold::app {

class App;

class Playback : public engine::Session {
 public:
  explicit Playback(App& app);

  // Full screen (always with sound), or a corner. Search's corner is
  // `muted`; the guide's and Live Events' play with sound. Either way it is
  // the same stream: no re-tune.
  void setFull();
  void setCorner(gfx::Rect r, bool muted = false);
  bool muted() const { return muted_; }
  // A corner left playing by another screen (Live Events' or Search's,
  // back in the guide, whose preview is larger) moves into `r`, its sound as
  // it is. Nothing while full screen or stopped.
  void keepCorner(gfx::Rect r);
  // The corner just moved from one place to another and the TV may still
  // be placing the picture (LG takes up to a few hundred ms): screens show
  // the channel's card there until the first re-apply, so the old place's
  // picture never shows cut or framed in black. Snapped, never animated.
  bool placing() const { return placing_; }

  // UI thread, every loop: the session's rules, then the window re-applies.
  // True while something is pending.
  bool tick(uint32_t ms);

  // The captions of what plays (drawn by the player screen).
  engine::Subtitles& subtitles() { return *subtitles_; }
  const engine::Subtitles& subtitles() const { return *subtitles_; }
  // The player draws them itself (tvOS), so the app only offers the choice.
  static bool playerDrawsSubtitles();
  // Subtitles were switched on or off, or another language picked.
  void subtitlesChanged();
  // The channel playing is in Spanish (its own subtitles choice).
  bool spanishChannel() const;
  // Caption timing for the channel playing, ms (+ later).
  int captionTiming() const;
  // The subtitles choice for the channel playing, and switching it.
  SubtitleChoice subtitleChoice() const;
  void setSubtitleChoice(const SubtitleChoice& c);
  // On <-> off for this channel's language; true when now on.
  bool toggleSubtitles();
  // What the Subtitles button says: "Off", the language shown ("English"),
  // "On" while captions haven't arrived, or the language picked and what
  // shows instead ("English · no Español here").
  struct SubtitleStatus {
    bool on = false;
    std::string label;     // the language, or "Off" / "On"
    std::string detail;    // "waiting…", "no Español here"
  };
  SubtitleStatus subtitleStatus() const;
  // A new attempt of the stream started (the player's play()): the relay
  // session its bytes come through (0 none), and whether this feed could
  // have gone through the relay (webOS, not a server-opened stream).
  void subtitlesBegin(unsigned relaySession, bool relayPossible);

  // The guide list the channel playing was picked from (a tune with its
  // flip list just before): the guide in the player opens on it and names
  // it while that flip list stands (views::playerOpen).
  void pickedFrom(engine::views::Tab tab) {
    pickedTab_ = tab;
    pickedFlip_ = flipList();
  }
  engine::views::Tab pickedTab() const { return pickedTab_; }
  const std::vector<std::string>& pickedFlip() const { return pickedFlip_; }

  // The player's search (Build 132): what was typed there last, for
  // reopening it while the player stays open (the player clears it as it
  // closes).
  void setPlayerQuery(std::string q) { playerQuery_ = std::move(q); }
  const std::string& playerQuery() const { return playerQuery_; }

 private:
  Playback(App& app, std::shared_ptr<engine::Subtitles> subtitles);
  gfx::Rect window() const;
  void scheduleReapply();
  void tickSubtitles(uint32_t ms);

  App& app_;
  // Shared with the player, whose relay thread feeds it.
  std::shared_ptr<engine::Subtitles> subtitles_;
  bool captionsLogged_ = false, noneLogged_ = false;
  bool relayPossible_ = false;
  int textLook_ = -1;  // the caption look the stream started with (-1 none yet)
  uint32_t systemCheckAt_ = 0;
  uint64_t textVersion_ = 0;
  uint32_t textTracksAt_ = 0;
  uint32_t pictureAt_ = 0;
  uint32_t timingLogAt_ = 0;
  gfx::Rect corner_{};
  bool muted_ = false;
  uint32_t reapplyAt_[3] = {0, 0, 0};
  bool reapplyOnReady_ = false;
  bool placing_ = false;
  engine::views::Tab pickedTab_ = engine::views::Tab::Lineup;
  std::vector<std::string> pickedFlip_;
  std::string playerQuery_;
};

}  // namespace marigold::app
