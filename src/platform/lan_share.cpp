#include "platform/lan_share.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <utility>
#include <vector>

#include "core/util/log.hpp"

namespace marigold::platform {
namespace {

namespace share = report::share;

// Bytes handed to send() at a time.
constexpr size_t kSendChunk = 64 * 1024;
// Bytes read from a request at a time (tvOS: maximumLength 2048).
constexpr size_t kReadChunk = 2048;
// Pending connections the kernel queues for us.
constexpr int kBacklog = 16;

void closeFd(int& fd) {
  if (fd >= 0) ::close(fd);
  fd = -1;
}

bool nonBlocking(int fd) {
  int flags = ::fcntl(fd, F_GETFL, 0);
  return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0 &&
         ::fcntl(fd, F_SETFD, FD_CLOEXEC) == 0;
}

// `count` random bytes from the kernel; empty when it can't give them.
// (getrandom() is newer than the TVs' glibc.)
std::vector<uint8_t> randomBytes(size_t count) {
  std::vector<uint8_t> out(count);
  int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
  if (fd < 0) return {};
  size_t got = 0;
  while (got < count) {
    ssize_t n = ::read(fd, out.data() + got, count - got);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    got += static_cast<size_t>(n);
  }
  ::close(fd);
  if (got != count) return {};
  return out;
}

struct Connection {
  int fd = -1;
  std::string request;
  bool responding = false;
  bool found = false;
  std::string head;
  size_t sent = 0;  // bytes of head + body sent
  std::chrono::steady_clock::time_point last;
};

}  // namespace

std::optional<std::string> lanIPv4Address() {
  ifaddrs* list = nullptr;
  if (::getifaddrs(&list) != 0) return std::nullopt;
  std::vector<share::Interface> interfaces;
  for (ifaddrs* item = list; item; item = item->ifa_next) {
    if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET) continue;
    sockaddr_in in{};
    std::memcpy(&in, item->ifa_addr, sizeof in);
    std::string text(INET_ADDRSTRLEN, '\0');
    if (!::inet_ntop(AF_INET, &in.sin_addr, &text[0], static_cast<socklen_t>(text.size())))
      continue;
    text.resize(std::strlen(text.c_str()));
    share::Interface i;
    i.name = item->ifa_name ? item->ifa_name : "";
    i.address = std::move(text);
    i.up = (item->ifa_flags & IFF_UP) != 0;
    i.loopback = (item->ifa_flags & IFF_LOOPBACK) != 0;
    interfaces.push_back(std::move(i));
  }
  ::freeifaddrs(list);
  return share::pickAddress(interfaces);
}

LanShare::~LanShare() { stop(); }

void LanShare::setFailed() {
  std::lock_guard<std::mutex> lock(mu_);
  state_ = State::Failed;
  url_.clear();
  downloads_ = 0;
}

LanShare::Status LanShare::start(std::string body, const Options& options) {
  stop();
  if (body.size() > options.maxBytes) {
    MLOG_W("share", "export is %zu bytes, over the %zu-byte limit", body.size(), options.maxBytes);
    setFailed();
    return status();
  }
  std::optional<std::string> host =
      options.host.empty() ? lanIPv4Address() : std::optional<std::string>(options.host);
  std::vector<uint8_t> token = randomBytes(share::kTokenBytes);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = 0;  // the system picks a free port
  if (!host || token.empty() || ::inet_pton(AF_INET, host->c_str(), &address.sin_addr) != 1) {
    MLOG_W("share", "no network address or random token; not sharing");
    setFailed();
    return status();
  }

  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  socklen_t length = sizeof address;
  int pipeFds[2] = {-1, -1};
  bool ok = fd >= 0 && nonBlocking(fd) &&
            ::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof address) == 0 &&
            ::listen(fd, kBacklog) == 0 &&
            ::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) == 0 &&
            ::pipe(pipeFds) == 0 && nonBlocking(pipeFds[0]) && nonBlocking(pipeFds[1]);
  if (!ok) {
    int error = errno;
    MLOG_W("share", "could not open the share socket (errno %d)", error);
    closeFd(fd);
    closeFd(pipeFds[0]);
    closeFd(pipeFds[1]);
    setFailed();
    return status();
  }
  wakeRead_ = pipeFds[0];
  wakeWrite_ = pipeFds[1];

  const int port = ntohs(address.sin_port);
  const std::string path = share::sharePath(share::hex(token), options.filename);
  const Clock::time_point deadline = Clock::now() + std::chrono::seconds(options.lifetimeSeconds);
  {
    std::lock_guard<std::mutex> lock(mu_);
    state_ = State::Ready;
    url_ = share::shareUrl(*host, port, path);
    deadline_ = deadline;
    downloads_ = 0;
  }
  // Never the URL: the token is the only key.
  MLOG_I("share", "sharing %zu bytes for %d s", body.size(), options.lifetimeSeconds);
  thread_ = std::thread(&LanShare::run, this, fd,
                        std::make_shared<const std::string>(std::move(body)), path,
                        options.contentType, options.filename, deadline);
  return status();
}

void LanShare::stop() {
  if (thread_.joinable()) {
    if (wakeWrite_ >= 0) {
      const char byte = 1;
      ssize_t n = ::write(wakeWrite_, &byte, 1);
      (void)n;  // a full pipe already wakes the thread
    }
    thread_.join();
  }
  closeFd(wakeRead_);
  closeFd(wakeWrite_);
  std::lock_guard<std::mutex> lock(mu_);
  if (state_ == State::Ready) MLOG_I("share", "sharing stopped");
  state_ = state_ == State::Failed ? State::Failed : State::Stopped;
  url_.clear();
}

LanShare::Status LanShare::status() const {
  std::lock_guard<std::mutex> lock(mu_);
  Status s;
  s.state = state_;
  s.downloads = downloads_;
  if (state_ == State::Ready) {
    s.url = url_;
    auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline_ - Clock::now());
    s.secondsLeft = static_cast<int>(std::max<int64_t>(0, (left.count() + 999) / 1000));
  }
  return s;
}

void LanShare::run(int listenFd, std::shared_ptr<const std::string> body, std::string path,
                   std::string contentType, std::string filename, Clock::time_point deadline) {
  std::vector<Connection> connections;
  const auto idle = std::chrono::seconds(share::kIdleSeconds);
  bool expired = false;
  std::vector<pollfd> fds;
  std::string chunk(kReadChunk, '\0');

  while (true) {
    Clock::time_point now = Clock::now();
    if (now >= deadline) {
      expired = true;
      break;
    }
    // Sleep until something happens, the link expires or a connection idles
    // out (at most a second, so a missed wake-up costs little).
    auto wake = std::min(deadline, now + std::chrono::seconds(1));
    for (const Connection& c : connections) wake = std::min(wake, c.last + idle);
    int timeout = static_cast<int>(
        std::max<int64_t>(0, std::chrono::duration_cast<std::chrono::milliseconds>(wake - now).count()));

    fds.clear();
    fds.push_back(pollfd{wakeRead_, POLLIN, 0});
    fds.push_back(pollfd{listenFd, POLLIN, 0});
    for (const Connection& c : connections)
      fds.push_back(pollfd{c.fd, static_cast<short>(c.responding ? POLLOUT : POLLIN), 0});
    int ready = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), timeout);
    if (ready < 0 && errno != EINTR) {
      MLOG_W("share", "poll failed (errno %d)", errno);
      break;
    }
    if (ready > 0 && (fds[0].revents & POLLIN)) break;  // stop()
    now = Clock::now();

    // Connections first (their pollfd entries match `connections` by index).
    for (size_t i = 0; i < connections.size(); ++i) {
      Connection& c = connections[i];
      short revents = ready > 0 ? fds[i + 2].revents : 0;
      bool close = false;
      if (!c.responding && (revents & (POLLIN | POLLHUP | POLLERR))) {
        ssize_t n = ::recv(c.fd, &chunk[0], chunk.size(), 0);
        if (n > 0) {
          c.request.append(chunk, 0, static_cast<size_t>(n));
          c.last = now;
          if (c.request.size() > share::kMaxRequestBytes) {
            close = true;
          } else if (share::headComplete(c.request)) {
            c.found = share::accepts(c.request, path);
            c.head = share::responseHead(c.found, contentType, body->size(), filename);
            c.responding = true;
          }
        } else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
          close = true;  // gone before a whole request
        }
      } else if (c.responding && (revents & (POLLOUT | POLLHUP | POLLERR))) {
        const size_t total = c.head.size() + (c.found ? body->size() : 0);
        while (c.sent < total) {
          const char* data;
          size_t left;
          if (c.sent < c.head.size()) {
            data = c.head.data() + c.sent;
            left = c.head.size() - c.sent;
          } else {
            data = body->data() + (c.sent - c.head.size());
            left = total - c.sent;
          }
          ssize_t n = ::send(c.fd, data, std::min(left, kSendChunk), MSG_NOSIGNAL);
          if (n > 0) {
            c.sent += static_cast<size_t>(n);
            c.last = now;
            continue;
          }
          if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) break;
          close = true;  // the other side went away
          break;
        }
        if (!close && c.sent >= total) {
          if (c.found) {
            std::lock_guard<std::mutex> lock(mu_);
            ++downloads_;
          }
          ::shutdown(c.fd, SHUT_WR);
          close = true;
        }
      } else if (revents & POLLNVAL) {
        close = true;
      }
      if (now - c.last >= idle) close = true;
      if (close) closeFd(c.fd);
    }
    connections.erase(std::remove_if(connections.begin(), connections.end(),
                                     [](const Connection& c) { return c.fd < 0; }),
                      connections.end());

    // New connections; beyond the limit they are closed at once.
    if (ready > 0 && (fds[1].revents & POLLIN)) {
      while (true) {
        int fd = ::accept(listenFd, nullptr, nullptr);
        if (fd < 0) break;
        if (static_cast<int>(connections.size()) >= share::kMaxConnections || !nonBlocking(fd)) {
          ::close(fd);
          continue;
        }
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
    if (state_ == State::Ready) {
      state_ = State::Stopped;
      url_.clear();
    }
    MLOG_I("share", "share link expired");
  }
}

}  // namespace marigold::platform
