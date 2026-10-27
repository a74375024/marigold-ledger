#include "platform/lan_server.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <vector>

#include "core/util/log.hpp"
#include "platform/lan_share.hpp"

#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

namespace marigold::platform {
namespace {

constexpr size_t kReadChunk = 4096;
constexpr int kBacklog = 16;
constexpr int kMaxConnections = 8;
constexpr auto kIdle = std::chrono::seconds(30);

void closeFd(int& fd) {
  if (fd >= 0) ::close(fd);
  fd = -1;
}

bool nonBlocking(int fd) {
  int flags = ::fcntl(fd, F_GETFL, 0);
  return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0 && ::fcntl(fd, F_SETFD, FD_CLOEXEC) == 0;
}

struct Connection {
  int fd = -1;
  std::string request;
  std::string reply;  // empty until the request is in
  size_t sent = 0;
  std::chrono::steady_clock::time_point last;
};

}  // namespace

std::string randomHex(size_t bytes) {
  std::vector<uint8_t> out(bytes);
  int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
  if (fd < 0) return {};
  size_t got = 0;
  while (got < bytes) {
    ssize_t n = ::read(fd, out.data() + got, bytes - got);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    got += static_cast<size_t>(n);
  }
  ::close(fd);
  if (got != bytes) return {};
  static const char* kDigits = "0123456789abcdef";
  std::string hex;
  for (uint8_t b : out) {
    hex += kDigits[b >> 4];
    hex += kDigits[b & 15];
  }
  return hex;
}

std::string LanServer::Status::base() const {
  if (state != State::Ready) return {};
  return "http://" + host + ":" + std::to_string(port);
}

LanServer::~LanServer() { stop(); }

LanServer::Status LanServer::start(Handler handler, const Options& options) {
  stop();
  std::optional<std::string> host = options.host.empty() ? lanIPv4Address() : std::optional<std::string>(options.host);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = 0;
  auto fail = [this] {
    std::lock_guard<std::mutex> lock(mu_);
    state_ = State::Failed;
    host_.clear();
    port_ = 0;
  };
  if (!host || ::inet_pton(AF_INET, host->c_str(), &address.sin_addr) != 1) {
    MLOG_W("phone", "no network address; not serving");
    fail();
    return status();
  }
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  socklen_t length = sizeof address;
  int pipeFds[2] = {-1, -1};
  bool ok = fd >= 0 && nonBlocking(fd) && ::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof address) == 0 &&
            ::listen(fd, kBacklog) == 0 &&
            ::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) == 0 && ::pipe(pipeFds) == 0 &&
            nonBlocking(pipeFds[0]) && nonBlocking(pipeFds[1]);
  if (!ok) {
    MLOG_W("phone", "could not open the socket (errno %d)", errno);
    closeFd(fd);
    closeFd(pipeFds[0]);
    closeFd(pipeFds[1]);
    fail();
    return status();
  }
  wakeRead_ = pipeFds[0];
  wakeWrite_ = pipeFds[1];
  Clock::time_point deadline = Clock::now() + std::chrono::seconds(options.lifetimeSeconds);
  {
    std::lock_guard<std::mutex> lock(mu_);
    state_ = State::Ready;
    host_ = *host;
    port_ = ntohs(address.sin_port);
    deadline_ = deadline;
  }
  MLOG_I("phone", "serving the sign-in page for %d s", options.lifetimeSeconds);
  thread_ = std::thread(&LanServer::run, this, fd, std::move(handler), deadline);
  return status();
}

void LanServer::stop() {
  if (thread_.joinable()) {
    if (wakeWrite_ >= 0) {
      const char byte = 1;
      ssize_t n = ::write(wakeWrite_, &byte, 1);
      (void)n;
    }
    thread_.join();
  }
  closeFd(wakeRead_);
  closeFd(wakeWrite_);
  std::lock_guard<std::mutex> lock(mu_);
  if (state_ == State::Ready) MLOG_I("phone", "sign-in page stopped");
  if (state_ != State::Failed) state_ = State::Stopped;
  host_.clear();
  port_ = 0;
}

LanServer::Status LanServer::status() const {
  std::lock_guard<std::mutex> lock(mu_);
  Status s;
  s.state = state_;
  if (state_ == State::Ready) {
    s.host = host_;
    s.port = port_;
    auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline_ - Clock::now());
    s.secondsLeft = static_cast<int>(std::max<int64_t>(0, (left.count() + 999) / 1000));
  }
  return s;
}

void LanServer::run(int listenFd, Handler handler, Clock::time_point deadline) {
  std::vector<Connection> connections;
  std::vector<pollfd> fds;
  std::string chunk(kReadChunk, '\0');
  bool expired = false;
  while (true) {
    Clock::time_point now = Clock::now();
    if (now >= deadline) {
      expired = true;
      break;
    }
    auto wake = std::min(deadline, now + std::chrono::seconds(1));
    for (const Connection& c : connections) wake = std::min(wake, c.last + kIdle);
    int timeout = static_cast<int>(
        std::max<int64_t>(0, std::chrono::duration_cast<std::chrono::milliseconds>(wake - now).count()));
    fds.clear();
    fds.push_back(pollfd{wakeRead_, POLLIN, 0});
    fds.push_back(pollfd{listenFd, POLLIN, 0});
    for (const Connection& c : connections)
      fds.push_back(pollfd{c.fd, static_cast<short>(c.reply.empty() ? POLLIN : POLLOUT), 0});
    int ready = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), timeout);
    if (ready < 0 && errno != EINTR) {
      MLOG_W("phone", "poll failed (errno %d)", errno);
      break;
    }
    if (ready > 0 && (fds[0].revents & POLLIN)) break;  // stop()
    now = Clock::now();

    for (size_t i = 0; i < connections.size(); ++i) {
      Connection& c = connections[i];
      short revents = ready > 0 ? fds[i + 2].revents : 0;
      bool close = false;
      if (c.reply.empty() && (revents & (POLLIN | POLLHUP | POLLERR))) {
        ssize_t n = ::recv(c.fd, &chunk[0], chunk.size(), 0);
        if (n > 0) {
          c.request.append(chunk, 0, static_cast<size_t>(n));
          c.last = now;
          lan::Request req;
          lan::Parse p = lan::parse(c.request, req);
          if (p == lan::Parse::Complete) {
            c.reply = lan::serialize(handler(req));
          } else if (p == lan::Parse::Bad) {
            lan::Response bad;
            bad.status = 400;
            bad.body = "Bad request";
            c.reply = lan::serialize(bad);
          }
        } else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
          close = true;
        }
      } else if (!c.reply.empty() && (revents & (POLLOUT | POLLHUP | POLLERR))) {
        while (c.sent < c.reply.size()) {
          ssize_t n = ::send(c.fd, c.reply.data() + c.sent, c.reply.size() - c.sent, kSendFlags);
          if (n > 0) {
            c.sent += static_cast<size_t>(n);
            c.last = now;
            continue;
          }
          if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) break;
          close = true;
          break;
        }
        if (!close && c.sent >= c.reply.size()) {
          ::shutdown(c.fd, SHUT_WR);
          close = true;
        }
      } else if (revents & POLLNVAL) {
        close = true;
      }
      if (now - c.last >= kIdle) close = true;
      if (close) closeFd(c.fd);
    }
    connections.erase(std::remove_if(connections.begin(), connections.end(), [](const Connection& c) { return c.fd < 0; }),
                      connections.end());

    if (ready > 0 && (fds[1].revents & POLLIN)) {
      while (true) {
        int fd = ::accept(listenFd, nullptr, nullptr);
        if (fd < 0) break;
        if (static_cast<int>(connections.size()) >= kMaxConnections || !nonBlocking(fd)) {
          ::close(fd);
          continue;
        }
#if defined(SO_NOSIGPIPE)
        int one = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
        Connection c;
        c.fd = fd;
        c.last = now;
        connections.push_back(std::move(c));
      }
    }
  }
  for (Connection& c : connections) closeFd(c.fd);
  closeFd(listenFd);
  if (expired) {
    std::lock_guard<std::mutex> lock(mu_);
    if (state_ == State::Ready) state_ = State::Stopped;
    MLOG_I("phone", "sign-in page expired");
  }
}

}  // namespace marigold::platform
