// A tiny HTTP server on the TV's LAN address for phone sign-in
// (core/net/phone_setup): every request goes to a handler, one request per
// connection, at a port the system picks. It stops by itself after the
// lifetime or on stop(). Plain POSIX sockets and one background thread, like
// lan_share (the Help exports); start(), stop(), status() and the destructor
// are called from the UI thread, the handler from the server thread.
#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "core/net/lan_http.hpp"

namespace marigold::platform {

// `bytes` random bytes from the kernel as lower-case hex; "" when there are
// none.
std::string randomHex(size_t bytes);

class LanServer {
 public:
  enum class State {
    Stopped,  // never started, stopped, or the lifetime is over
    Ready,
    Failed,   // no network address or no socket
  };
  using Handler = std::function<lan::Response(const lan::Request&)>;

  struct Options {
    int lifetimeSeconds = 600;
    // Bind here instead of the LAN address (tests use "127.0.0.1").
    std::string host;
  };

  struct Status {
    State state = State::Stopped;
    std::string host;  // the dotted quad, while Ready
    int port = 0;
    int secondsLeft = 0;
    // "http://<host>:<port>" while Ready.
    std::string base() const;
  };

  LanServer() = default;
  ~LanServer();
  LanServer(const LanServer&) = delete;
  LanServer& operator=(const LanServer&) = delete;

  Status start(Handler handler, const Options& options);
  void stop();
  Status status() const;

 private:
  using Clock = std::chrono::steady_clock;
  void run(int listenFd, Handler handler, Clock::time_point deadline);

  mutable std::mutex mu_;
  State state_ = State::Stopped;
  std::string host_;
  int port_ = 0;
  Clock::time_point deadline_{};
  std::thread thread_;
  int wakeRead_ = -1, wakeWrite_ = -1;
};

}  // namespace marigold::platform
