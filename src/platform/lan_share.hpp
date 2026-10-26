// Hands one file (the Help exports: channel report, playback log) to a phone
// or computer on the same network (PlaybackDiagnosticExportServer in tvOS).
//
// A tiny HTTP/1.1 server on the TV's LAN address, at a port the system
// picks, serving exactly one in-memory file at "/<random token>/<name>" with
// "Content-Disposition: attachment". Anything else gets a 404. It stops by
// itself after the lifetime (10 minutes) or on stop(). The token (128 random
// bits from /dev/urandom) is the only protection, so the URL is shown on the
// screen and in the QR code, and never logged.
//
// One background thread with plain POSIX sockets. start(), stop(), status()
// and the destructor are called from the UI thread; status() is cheap and
// meant to be polled every frame.
#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "core/report/share.hpp"

namespace marigold::platform {

// The TV's LAN IPv4 address (getifaddrs; loopback, link-local and Wi-Fi
// Direct skipped, private ranges preferred), or nullopt when it has none.
std::optional<std::string> lanIPv4Address();

class LanShare {
 public:
  enum class State {
    Stopped,  // never started, stopped, or the link expired
    Ready,    // serving: url() works
    Failed,   // couldn't start: too big, no network, no socket
  };

  struct Options {
    std::string filename = std::string(report::share::kChannelsFile);
    std::string contentType = std::string(report::share::kCsvType);
    size_t maxBytes = report::share::kMaxBytes;
    int lifetimeSeconds = report::share::kLifetimeSeconds;
    // Bind here instead of lanIPv4Address() (tests use "127.0.0.1").
    std::string host;
  };

  struct Status {
    State state = State::Stopped;
    // "http://<lan address>:<port>/<token>/<name>" while Ready, else empty.
    // Secret while the link lives: show it, never log it.
    std::string url;
    int secondsLeft = 0;  // until the link expires (Ready only)
    int downloads = 0;    // complete responses sent with the file
  };

  LanShare() = default;
  ~LanShare();
  LanShare(const LanShare&) = delete;
  LanShare& operator=(const LanShare&) = delete;

  // Stops any previous share, then serves `body`. Returns the new status:
  // Ready with the URL, or Failed (the body is over maxBytes, the TV has no
  // LAN address, or the socket could not be opened). Quick: it only opens
  // the socket and starts the thread; build the body on a worker first.
  Status start(std::string body, const Options& options);
  Status start(std::string body) { return start(std::move(body), Options()); }
  // Closes the link and every open connection; waits for the thread.
  void stop();
  Status status() const;

 private:
  using Clock = std::chrono::steady_clock;
  // The server thread: owns the listening socket and the connections.
  void run(int listenFd, std::shared_ptr<const std::string> body, std::string path,
           std::string contentType, std::string filename, Clock::time_point deadline);
  void setFailed();

  mutable std::mutex mu_;
  State state_ = State::Stopped;
  std::string url_;
  Clock::time_point deadline_{};
  int downloads_ = 0;

  std::thread thread_;
  // Self-pipe: stop() writes a byte to wake the thread. UI thread only.
  int wakeRead_ = -1, wakeWrite_ = -1;
};

}  // namespace marigold::platform
