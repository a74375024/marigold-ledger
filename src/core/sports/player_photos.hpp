// Tennis player cut-out photos (PlayerPhotos.swift) for the players on
// tennis tiles, and F1 drivers when the race data has none, from a free
// player search. Kept light (the budget is part of the design):
// - only players in matches you can watch, live or within the next day
//   (board::Board::tennisPlayers), and singles players (a name with a space);
// - one search per player, saved for 90 days (a miss is tried again after a
//   week); entries older than 120 days are dropped when saving;
// - one search every 6 seconds at most (the free limit is 30 a minute), at
//   most kDailyCap a day (then nothing until tomorrow), and a 30-minute
//   pause after a failure.
// Only the player's name is sent.
//
// No I/O: the caller owns a PlayerPhotos (built from the saved JSON):
//   if (photos.request(board.tennisPlayers, now))
//     while (auto r = photos.nextRequest(now, zone)) {
//       wait r->waitSeconds; send; photos.onResponse(status, body, now);
//       if (photos.persistDue()) write photos.persist(now);
//     }
//   write photos.persist(now) (when it returns a value); redraw if revision() moved.
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/sources/common.hpp"
#include "core/util/time.hpp"

namespace marigold::sports::player_photos {

using time::Seconds;

constexpr const char* kBase = "https://www.thesportsdb.com/api/v1/json/123/searchplayers.php?p=";
constexpr int kTimeoutSeconds = 15;
constexpr int kDailyCap = 120;
constexpr Seconds kSpacing = 6;
constexpr Seconds kFoundLife = 90 * 86'400;
constexpr Seconds kMissLife = 7 * 86'400;
constexpr Seconds kKeep = 120 * 86'400;
constexpr Seconds kFailurePause = 30 * 60;
// Saved as it goes, every this many searches (a long pass can be cut short).
constexpr int kPersistEvery = 10;

// "Player Álpha-Beta" -> "player alpha beta" (accents, case and dashes folded).
std::string key(std::string_view name);
// The search for a player: accents folded, spaces as underscores.
sources::Request searchRequest(std::string_view name);
// The tennis player (or racing driver) whose name matches `wanted` (a key):
// the cut-out first, else the photo; https only.
std::optional<std::string> parse(std::string_view body, const std::string& wanted);

struct Entry {
  std::string url;  // empty when the search found no photo
  Seconds at = 0;
};
struct Saved {
  std::map<std::string, Entry> entries;  // by key
  std::optional<std::string> requestDay;  // "yyyy-MM-dd" in the viewer's zone
  int requestCount = 0;
};
std::string toJson(const Saved& saved);
std::optional<Saved> savedFromJson(std::string_view text);

class PlayerPhotos {
 public:
  struct Request {
    std::string name;
    std::string key;
    sources::Request request;
    // Wait this long before sending (6 s between searches, across passes).
    Seconds waitSeconds = 0;
  };

  PlayerPhotos() = default;
  explicit PlayerPhotos(Saved saved) : saved_(std::move(saved)) {}

  // The player's photo, when saved (no request).
  std::optional<std::string> photo(std::string_view name) const;
  // The smaller size the tile tries first (the full one if that fails).
  static std::string smallUrl(const std::string& url);
  // For board::Input::playerPhoto: a copy of the saved photos (safe on a worker).
  std::function<std::optional<std::string>(std::string_view)> photoHook() const;

  // Players on screen: queue the ones not saved yet (or stale). True when
  // the caller should start draining (none running, not paused). Cheap to
  // call often.
  bool request(const std::vector<std::string>& names, Seconds now);
  // The next search, or nullopt when the drain is over (queue empty, today's
  // cap reached, or after a failure).
  std::optional<Request> nextRequest(Seconds now, const time::Zone& zone);
  // The answer to the last search: HTTP status (0 = none) and body. A
  // failure (rate limited, offline) pauses 30 minutes; the name stays queued.
  void onResponse(int status, std::string_view body, Seconds now);
  bool draining() const { return draining_; }

  // Write now: kPersistEvery searches since the last write during a drain.
  bool persistDue() const { return dirty_ && asked_ > 0 && asked_ % kPersistEvery == 0 && !awaiting_; }
  // What to write when something changed (old entries dropped); nullopt
  // when nothing did.
  std::optional<Saved> persist(Seconds now);

  // Bumped when photos arrive.
  int revision() const { return revision_; }
  const Saved& saved() const { return saved_; }
  Seconds pausedUntil() const { return pausedUntil_; }
  size_t queued() const { return queue_.size(); }

 private:
  Saved saved_;
  std::vector<std::string> queue_;
  std::set<std::string> queued_;
  bool draining_ = false;
  bool awaiting_ = false;
  std::string awaitingName_;
  Seconds pausedUntil_ = 0;
  std::optional<Seconds> lastRequestAt_;
  bool dirty_ = false;
  int asked_ = 0;
  int revision_ = 0;
};

}  // namespace marigold::sports::player_photos
