// In-app updates (Build 148), only where the device installs the app by
// itself (Android and Fire TV, the Mac: Host::canSelfUpdate). webOS and the
// Apple TV never make one: no check, no request, nothing on screen.
//
// The check: GitHub's latest release, once shortly after launch (when the
// app is idle and the first channel list isn't loading) and then at most
// once a day, unauthenticated through the app's own pool (its neutral
// agent; no name of ours in any header). Newer is a plain x.y.z above the
// running version; a prerelease or any other tag is ignored. A failure is
// one log line (never a link) and another try an hour later.
//
// The update, when chosen: the release is read again, its file for this
// device downloaded to the app's cache and checked against the release's
// SHA-256 digest (none, or a mismatch: refused), then handed to the
// platform (Host::installUpdate).
//
// What is kept (update.json): the day of the last check, the newer version
// it found, and the day (and version) the card last showed, so it shows at
// most once a day.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "core/engine/host.hpp"
#include "core/util/time.hpp"

namespace marigold::engine::updates {

constexpr const char* kLatestUrl = "https://api.github.com/repos/a74375024/marigold-ledger/releases/latest";
constexpr const char* kStateFile = "update.json";
// The first check waits this long after launch (and for the app to be idle).
constexpr uint32_t kFirstCheckMs = 20000;
// A check that got no answer: tried again this much later.
constexpr uint32_t kRetryMs = 3600u * 1000u;
// "Couldn't update. Try again later." stays this long.
constexpr uint32_t kFailedMs = 5000;

// The texts on screen.
constexpr const char* kAvailableText = "Update available";
constexpr const char* kUpdatingText = "Updating\xe2\x80\xa6";
constexpr const char* kFailedText = "Couldn't update. Try again later.";

struct Version {
  int major = 0, minor = 0, patch = 0;
};
// "1.0.2" or "v1.0.2". Plain releases only: three numbers, nothing after
// them ("1.0.2-beta", "1.0", "latest": nullopt).
std::optional<Version> parseVersion(std::string_view s);
// `candidate` is a later release than `current` (1.0.10 after 1.0.9).
// False when either isn't a plain version.
bool newer(std::string_view candidate, std::string_view current);

struct Release {
  std::string version;  // "1.0.2"
  std::string url;      // the file's download link (https only)
  std::string sha256;   // 64 lowercase hex digits; empty: the release gives none
  uint64_t size = 0;    // bytes, as the release gives it (0: not said)
};
// GitHub's answer for a release, with the file named `asset`. nullopt: a
// draft or prerelease, a tag that isn't a plain version, or no such file
// (or one without an https link).
std::optional<Release> parseRelease(std::string_view body, const std::string& asset);

class Updates {
 public:
  enum class Phase { Idle, Updating, Failed };

  // nullptr unless the host can update itself (Host::canSelfUpdate).
  // `version`: the running app's ("1.0.1"). `now` (tests): the clock;
  // unset: time::now.
  static std::unique_ptr<Updates> make(const Host& host, std::string version,
                                       std::function<time::Seconds()> now = nullptr);
  Updates(const Host& host, std::string version, std::function<time::Seconds()> now = nullptr);

  // The app loop. `quiet`: nothing is pressed or starting and the first
  // channel list isn't loading. Checks when due; a failure line runs out.
  // True when what is shown changed.
  bool tick(uint32_t ms, bool quiet);

  // A newer release is known (from today's check or an earlier one).
  bool available() const;
  // The card may show: available, idle, and not shown yet today for it.
  bool cardDue() const;
  // The card is up: kept, so not again before tomorrow.
  void cardShown();
  // "Update": read the release again, download, check, install.
  void update();
  // The pool dropped every request (a provider change): whatever was under
  // way ends (an update as failed).
  void abandoned();

  Phase phase() const { return phase_; }
  // While Failed: the line to show, and until when (app ms).
  const std::string& failText() const { return failText_; }
  uint32_t failedUntil() const { return failedUntil_; }
  // The update is in place and the app must end for it (the Mac).
  bool quitWanted() const { return quit_; }

 private:
  void load();
  void save() const;
  int64_t today() const;
  void check(uint32_t ms);
  void checked(uint32_t ms, int status, const std::string& error, const std::optional<Release>& rel);
  void fetched(int status, const std::optional<Release>& rel);
  void download(const Release& rel);
  void install();
  void fail(const std::string& why, const std::string& message = std::string());

  http::Pool& pool_;
  Files& files_;
  std::function<uint32_t()> ms_;
  std::function<const time::Zone&()> zone_;
  std::function<time::Seconds()> now_;
  std::function<UpdateInstall(const std::string&)> install_;
  std::string version_, asset_, file_;

  // Kept in update.json.
  int64_t checkDay_ = 0;
  std::string latest_;  // newer than the running version; empty: none known
  int64_t cardDay_ = 0;
  std::string cardVersion_;

  bool checking_ = false;
  uint32_t nextCheckMs_ = kFirstCheckMs;
  Phase phase_ = Phase::Idle;
  std::string failText_;
  uint32_t failedUntil_ = 0;
  bool quit_ = false;
  uint64_t generation_ = 0;  // answers from before abandoned() are dropped
};

}  // namespace marigold::engine::updates
