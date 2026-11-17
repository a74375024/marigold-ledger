#include "platform/replay_relay.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <string_view>
#include <vector>

#include "core/media/ts_start.hpp"
#include "core/net/http.hpp"
#include "core/util/log.hpp"

namespace marigold::platform {

namespace tsc = media::tsclock;

namespace {

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

constexpr int kConnectMs = 8000;
constexpr int kIdleMs = 15000;     // no byte from the provider this long: it's over
constexpr int kRedirects = 5;
constexpr size_t kHeadMax = 16 * 1024;
// Build 174: the same agent the engine's archive reads send (http.hpp).
constexpr const char* kAgent = http::kPlayerAgent;

int64_t steadyMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void noSigpipe(int fd) {
#ifdef SO_NOSIGPIPE
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#else
  (void)fd;
#endif
}

void recvTimeout(int fd, int ms) {
  timeval tv{ms / 1000, (ms % 1000) * 1000};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
}

bool sendAll(int fd, const char* p, size_t n) {
  while (n > 0) {
    ssize_t w = ::send(fd, p, n, kSendFlags);
    if (w < 0 && errno == EINTR) continue;
    if (w <= 0) return false;
    p += w;
    n -= static_cast<size_t>(w);
  }
  return true;
}

std::string lower(std::string_view s) {
  std::string out(s);
  for (char& c : out)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return out;
}

struct Url {
  std::string host, port = "80", path = "/";
};
std::optional<Url> parseUrl(const std::string& url) {
  if (lower(url.substr(0, 7)) != "http://") return std::nullopt;
  std::string rest = url.substr(7);
  size_t slash = rest.find('/');
  std::string authority = rest.substr(0, slash);
  Url u;
  if (slash != std::string::npos) u.path = rest.substr(slash);
  if (authority.find('@') != std::string::npos || authority.empty()) return std::nullopt;
  if (authority[0] == '[') {
    size_t close = authority.find(']');
    if (close == std::string::npos) return std::nullopt;
    u.host = authority.substr(1, close - 1);
    if (close + 1 < authority.size() && authority[close + 1] == ':') u.port = authority.substr(close + 2);
  } else {
    size_t colon = authority.rfind(':');
    u.host = authority.substr(0, colon);
    if (colon != std::string::npos) u.port = authority.substr(colon + 1);
  }
  if (u.host.empty() || u.port.empty()) return std::nullopt;
  for (char c : url)
    if (static_cast<unsigned char>(c) <= 0x20) return std::nullopt;
  return u;
}

// A socket to the provider (nullopt: none), given up on `cancel`.
int connectTo(const Url& u, const std::atomic<bool>& cancel) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* list = nullptr;
  if (getaddrinfo(u.host.c_str(), u.port.c_str(), &hints, &list) != 0 || !list) return -1;
  int fd = -1;
  for (addrinfo* a = list; a && fd < 0 && !cancel.load(); a = a->ai_next) {
    int s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
    if (s < 0) continue;
    noSigpipe(s);
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
    int r = ::connect(s, a->ai_addr, a->ai_addrlen);
    bool ok = r == 0;
    if (!ok && errno == EINPROGRESS) {
      int64_t until = steadyMs() + kConnectMs;
      while (!cancel.load() && steadyMs() < until) {
        pollfd p{s, POLLOUT, 0};
        int n = ::poll(&p, 1, 250);
        if (n > 0) {
          int err = 0;
          socklen_t len = sizeof err;
          getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len);
          ok = err == 0;
          break;
        }
        if (n < 0 && errno != EINTR) break;
      }
    }
    if (!ok) {
      ::close(s);
      continue;
    }
    fcntl(s, F_SETFL, flags);
    recvTimeout(s, 250);
    fd = s;
  }
  freeaddrinfo(list);
  return fd;
}

// Bytes from a socket, with the cancel flag and the idle limit.
class Reader {
 public:
  Reader(int fd, const std::atomic<bool>& cancel, const std::function<bool()>& gone)
      : fd_(fd), cancel_(cancel), gone_(gone) {}
  // Up to `max` bytes into `out` (0: the end, or given up).
  size_t read(char* out, size_t max) {
    if (pos_ < buf_.size()) {
      size_t n = std::min(max, buf_.size() - pos_);
      std::memcpy(out, buf_.data() + pos_, n);
      pos_ += n;
      if (pos_ == buf_.size()) {
        buf_.clear();
        pos_ = 0;
      }
      return n;
    }
    int64_t idleFrom = steadyMs();
    for (;;) {
      if (cancel_.load() || (gone_ && gone_())) return 0;
      ssize_t r = ::recv(fd_, out, max, 0);
      if (r > 0) return static_cast<size_t>(r);
      if (r == 0) return 0;
      if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return 0;
      if (steadyMs() - idleFrom > kIdleMs) return 0;
    }
  }
  // A line without its CRLF (nullopt: the end first).
  std::optional<std::string> line(size_t max = kHeadMax) {
    std::string out;
    char c;
    while (out.size() < max) {
      if (read(&c, 1) == 0) return std::nullopt;
      if (c == '\n') {
        if (!out.empty() && out.back() == '\r') out.pop_back();
        return out;
      }
      out += c;
    }
    return std::nullopt;
  }
 private:
  int fd_;
  const std::atomic<bool>& cancel_;
  const std::function<bool()>& gone_;
  std::vector<char> buf_;
  size_t pos_ = 0;
};

// One GET of the provider's link, redirects followed: `onHead` gets the
// final status (return false to stop), `onBody` the body as it comes
// (false: stop). Returns the final status (0: no answer).
int fetch(const std::string& link, const std::vector<std::string>& headers, std::optional<uint64_t> rangeFrom,
          const std::atomic<bool>& cancel, const std::function<bool()>& gone, const std::function<bool(int)>& onHead,
          const std::function<bool(const char*, size_t)>& onBody) {
  std::string url = link;
  for (int hop = 0; hop <= kRedirects; ++hop) {
    std::optional<Url> u = parseUrl(url);
    if (!u) return 0;
    int fd = connectTo(*u, cancel);
    if (fd < 0) return 0;
    std::string req = "GET " + u->path + " HTTP/1.1\r\nHost: " + u->host + (u->port == "80" ? "" : ":" + u->port) +
                      "\r\nAccept: */*\r\nConnection: close\r\n";
    bool agent = false;
    for (const std::string& h : headers) {
      std::string l = lower(h.substr(0, h.find(':')));
      if (l == "range" || l == "host" || l == "connection") continue;
      if (l == "user-agent") agent = true;
      req += h + "\r\n";
    }
    if (!agent) req += std::string("User-Agent: ") + kAgent + "\r\n";
    if (rangeFrom) req += "Range: bytes=" + std::to_string(*rangeFrom) + "-\r\n";
    req += "\r\n";
    if (!sendAll(fd, req.data(), req.size())) {
      ::close(fd);
      return 0;
    }
    Reader rd(fd, cancel, gone);
    std::optional<std::string> status = rd.line();
    int code = 0;
    if (status && status->rfind("HTTP/", 0) == 0) {
      size_t sp = status->find(' ');
      if (sp != std::string::npos) code = std::atoi(status->c_str() + sp + 1);
    }
    if (!code) {
      ::close(fd);
      return 0;
    }
    std::string location;
    bool chunked = false;
    std::optional<uint64_t> length;
    size_t headBytes = 0;
    for (;;) {
      std::optional<std::string> h = rd.line();
      if (!h) {
        ::close(fd);
        return 0;
      }
      headBytes += h->size();
      if (h->empty()) break;
      if (headBytes > kHeadMax) {
        ::close(fd);
        return 0;
      }
      size_t colon = h->find(':');
      if (colon == std::string::npos) continue;
      std::string name = lower(h->substr(0, colon));
      std::string value = h->substr(colon + 1);
      while (!value.empty() && value.front() == ' ') value.erase(value.begin());
      if (name == "location") location = value;
      else if (name == "transfer-encoding") chunked = lower(value).find("chunked") != std::string::npos;
      else if (name == "content-length") length = std::strtoull(value.c_str(), nullptr, 10);
    }
    if (code >= 300 && code < 400 && !location.empty()) {
      ::close(fd);
      if (location[0] == '/') {
        url = "http://" + (u->host.find(':') != std::string::npos ? "[" + u->host + "]" : u->host) +
              (u->port == "80" ? "" : ":" + u->port) + location;
      } else {
        url = location;
      }
      continue;
    }
    if (!onHead(code)) {
      ::close(fd);
      return code;
    }
    char buf[64 * 1024];
    if (chunked) {
      for (;;) {
        std::optional<std::string> size = rd.line(64);
        if (!size) break;
        uint64_t left = std::strtoull(size->c_str(), nullptr, 16);
        if (left == 0) break;
        bool more = true;
        while (left > 0) {
          size_t n = rd.read(buf, static_cast<size_t>(std::min<uint64_t>(left, sizeof buf)));
          if (n == 0 || !onBody(buf, n)) {
            more = false;
            break;
          }
          left -= n;
        }
        if (!more || !rd.line(8)) break;
      }
    } else {
      uint64_t left = length.value_or(UINT64_MAX);
      while (left > 0) {
        size_t n = rd.read(buf, static_cast<size_t>(std::min<uint64_t>(left, sizeof buf)));
        if (n == 0 || !onBody(buf, n)) break;
        left -= n;
      }
    }
    ::close(fd);
    return code;
  }
  return 0;
}

}  // namespace

struct ReplaySession {
  unsigned id = 0;
  std::string source;
  std::vector<std::string> headers;
  ReplayRelay::Start start;
  std::atomic<bool> cancel{false};
  std::atomic<int> skipped{-1};
  std::atomic<int> generation{0};
  std::atomic<int> opens{0};
  std::atomic<uint64_t> lastRange{0};
  // Build 177: what was passed on, read for ad markers (the connection's
  // thread feeds it; takeAdMarkers() reads it).
  std::mutex adsMu;
  media::scte35::Scanner ads;
};

struct ReplayRelay::Core {
  std::mutex mu;
  std::shared_ptr<ReplaySession> current;
  unsigned serial = 0;
  // Build 177: the ad markers of sessions ended since the last take.
  media::scte35::Tally ads;
  // Under `mu`: a session ending hands over its ad markers.
  void endSession() {
    if (!current) return;
    current->cancel = true;
    std::lock_guard<std::mutex> lock(current->adsMu);
    ads.merge(current->ads.take());
  }
  std::atomic<bool> stopping{false};
  int listenFd = -1;
};

namespace {

// The first video PTS of the stretch, from its start.
std::optional<int64_t> firstPts(ReplaySession& s, const std::function<bool()>& gone) {
  tsc::TsClock clock;
  uint64_t read = 0;
  ++s.opens;
  s.lastRange = 0;
  fetch(
      s.source, s.headers, std::nullopt, s.cancel, gone, [](int code) { return code >= 200 && code < 300; },
      [&](const char* p, size_t n) {
        clock.feed(reinterpret_cast<const uint8_t*>(p), n);
        read += n;
        return !clock.first() && read < 4u * 1024u * 1024u;
      });
  return clock.first();
}

// Serves one connection of the player: the stretch from the moment.
void serve(int fd, std::shared_ptr<ReplayRelay::Core> core) {
  noSigpipe(fd);
  recvTimeout(fd, 5000);
  // The player's request: its first line names the session.
  std::string head;
  char buf[2048];
  while (head.find("\r\n\r\n") == std::string::npos && head.find("\n\n") == std::string::npos &&
         head.size() < 8192) {
    ssize_t r = ::recv(fd, buf, sizeof buf, 0);
    if (r <= 0) break;
    head.append(buf, static_cast<size_t>(r));
  }
  std::shared_ptr<ReplaySession> s;
  {
    std::lock_guard<std::mutex> lock(core->mu);
    s = core->current;
  }
  size_t q = head.find("/replay.ts?s=");
  unsigned asked = q == std::string::npos ? 0 : static_cast<unsigned>(std::strtoul(head.c_str() + q + 13, nullptr, 10));
  if (!s || asked != s->id || s->cancel.load()) {
    static const char kNo[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    sendAll(fd, kNo, sizeof kNo - 1);
    ::close(fd);
    return;
  }
  bool headOnly = head.rfind("HEAD ", 0) == 0;
  // A newer connection of the same session takes over.
  int mine = ++s->generation;
  std::function<bool()> gone = [&] { return s->generation.load() != mine || core->stopping.load(); };
  bool answered = false, playerGone = false;
  auto answer = [&]() {
    if (answered) return true;
    answered = true;
    static const char kOk[] =
        "HTTP/1.1 200 OK\r\nContent-Type: video/mp2t\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n";
    return sendAll(fd, kOk, sizeof kOk - 1);
  };
  auto pass = [&](const char* p, size_t n) {
    if (n == 0) return true;
    {
      // Build 177: read for ad markers as it goes (O(1) a packet).
      std::lock_guard<std::mutex> lock(s->adsMu);
      s->ads.feed(reinterpret_cast<const uint8_t*>(p), n);
    }
    if (!answer() || !sendAll(fd, p, n)) {
      playerGone = true;
      return false;
    }
    return true;
  };
  if (headOnly) {
    answer();
    ::close(fd);
    return;
  }
  const ReplayRelay::Start st = s->start;
  const int64_t began = steadyMs();
  // Reading up to the moment is bounded in time too.
  auto late = [began] { return steadyMs() - began > ReplayRelay::kReadThroughMs; };
  auto ok = [](int code) { return code >= 200 && code < 300; };
  if (st.skip <= 0) {
    ++s->opens;
    fetch(s->source, s->headers, std::nullopt, s->cancel, gone, ok, pass);
  } else {
    uint64_t rate = st.byteRate ? st.byteRate : 750000;  // 6 Mb/s when not known
    std::optional<int64_t> p0 = st.seek ? firstPts(*s, gone) : std::nullopt;
    bool done = false;
    if (p0 && !gone() && !s->cancel.load()) {
      // Seek: from about the moment, nearer when the first picture there
      // is off; what's left before it is read through.
      uint64_t from = st.startByte / 188 * 188;
      for (int attempt = 0; attempt < 3 && !done && !playerGone && !gone() && !s->cancel.load(); ++attempt) {
        bool last = attempt == 2;
        tsc::TsStartAt cut(st.skip, std::max<uint64_t>(64ull * 1024 * 1024, 90 * rate));
        cut.target(*p0 + static_cast<int64_t>(st.skip) * tsc::kHz);
        bool ranged = false, again = false;
        uint64_t next = from;
        std::string out;
        ++s->opens;
        s->lastRange = from;
        fetch(
            s->source, s->headers, from, s->cancel, gone,
            [&](int code) {
              ranged = code == 206;
              return ok(code);
            },
            [&](const char* p, size_t n) {
              out.clear();
              bool before = cut.first().has_value();
              if (!cut.passing() && late()) cut.stopDropping();
              cut.feed(reinterpret_cast<const uint8_t*>(p), n, out);
              if (!before && cut.first() && ranged && !last && !cut.passing()) {
                double at = static_cast<double>(tsc::delta(*cut.first(), *p0)) / tsc::kHz;
                // Past it, or more than 15 s short of it: asked again.
                if (at > st.skip + 2 || at < st.skip - 15) {
                  double perSecond = at > 5 ? static_cast<double>(from) / at : static_cast<double>(rate);
                  double want = (st.skip - 10) * perSecond;
                  next = want <= 0 ? 0 : static_cast<uint64_t>(want) / 188 * 188;
                  again = next != from;
                  if (again) return false;
                }
              }
              if (cut.passing() && s->skipped.load() < 0) s->skipped = std::max(0, cut.skipped());
              return pass(out.data(), out.size());
            });
        if (again) {
          from = next;
          continue;
        }
        // Played (or no range after all: read through below).
        done = answered;
        break;
      }
    }
    if (!done && !playerGone && !gone() && !s->cancel.load()) {
      // Read through from the start.
      uint64_t cap = std::min<uint64_t>(ReplayRelay::kReadThroughCap,
                                        std::max<uint64_t>(32ull * 1024 * 1024, rate * 3 / 2 * static_cast<uint64_t>(st.skip)));
      tsc::TsStartAt cut(st.skip, cap);
      if (p0) cut.target(*p0 + static_cast<int64_t>(st.skip) * tsc::kHz);
      std::string out;
      ++s->opens;
      s->lastRange = 0;
      fetch(s->source, s->headers, std::nullopt, s->cancel, gone, ok, [&](const char* p, size_t n) {
        out.clear();
        if (!cut.passing() && late()) cut.stopDropping();
        cut.feed(reinterpret_cast<const uint8_t*>(p), n, out);
        if (cut.passing() && s->skipped.load() < 0) s->skipped = std::max(0, cut.skipped());
        return pass(out.data(), out.size());
      });
    }
  }
  if (!answered) {
    static const char kBad[] = "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    sendAll(fd, kBad, sizeof kBad - 1);
  }
  ::close(fd);
}

void listen(std::shared_ptr<ReplayRelay::Core> core) {
  while (!core->stopping.load()) {
    pollfd p{core->listenFd, POLLIN, 0};
    int n = ::poll(&p, 1, 250);
    if (n <= 0) continue;
    int fd = ::accept(core->listenFd, nullptr, nullptr);
    if (fd < 0) continue;
    std::thread([fd, core] { serve(fd, core); }).detach();
  }
}

}  // namespace

ReplayRelay::~ReplayRelay() {
  if (!core_) return;
  core_->stopping = true;
  stop();
  if (listener_.joinable()) listener_.join();
  if (core_->listenFd >= 0) ::close(core_->listenFd);
}

bool ReplayRelay::start() {
  if (port_ > 0) return true;
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return false;
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  a.sin_port = 0;
  socklen_t len = sizeof a;
  if (::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(fd, 8) != 0 ||
      getsockname(fd, reinterpret_cast<sockaddr*>(&a), &len) != 0) {
    ::close(fd);
    MLOG_W("relay", "replay relay unavailable");
    return false;
  }
  core_ = std::make_shared<Core>();
  core_->listenFd = fd;
  port_ = ntohs(a.sin_port);
  listener_ = std::thread([core = core_] { listen(core); });
  MLOG_I("relay", "replay relay listening");
  return true;
}

bool ReplayRelay::takes(const std::string& source) { return parseUrl(source).has_value(); }

std::string ReplayRelay::localUrl(const std::string& source, const std::string& headers, const Start& start) {
  if (port_ <= 0 || !core_ || !takes(source)) return {};
  auto s = std::make_shared<ReplaySession>();
  s->source = source;
  s->start = start;
  size_t at = 0;
  while (at < headers.size()) {
    size_t end = headers.find('\n', at);
    if (end == std::string::npos) end = headers.size();
    std::string line = headers.substr(at, end - at);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (line.find(':') != std::string::npos && line.find('\r') == std::string::npos) s->headers.push_back(line);
    at = end + 1;
  }
  {
    std::lock_guard<std::mutex> lock(core_->mu);
    core_->endSession();
    s->id = ++core_->serial;
    core_->current = s;
  }
  return "http://127.0.0.1:" + std::to_string(port_) + "/replay.ts?s=" + std::to_string(s->id);
}

void ReplayRelay::stop() {
  if (!core_) return;
  std::lock_guard<std::mutex> lock(core_->mu);
  core_->endSession();
  core_->current.reset();
}

media::scte35::Tally ReplayRelay::takeAdMarkers() {
  if (!core_) return {};
  std::lock_guard<std::mutex> lock(core_->mu);
  media::scte35::Tally out = core_->ads;
  core_->ads = media::scte35::Tally{};
  if (core_->current) {
    std::lock_guard<std::mutex> l(core_->current->adsMu);
    out.merge(core_->current->ads.take());
  }
  return out;
}

std::optional<int> ReplayRelay::skipped() const {
  if (!core_) return std::nullopt;
  std::lock_guard<std::mutex> lock(core_->mu);
  if (!core_->current) return std::nullopt;
  int v = core_->current->skipped.load();
  return v < 0 ? std::nullopt : std::optional<int>(v);
}

int ReplayRelay::opens() const {
  if (!core_) return 0;
  std::lock_guard<std::mutex> lock(core_->mu);
  return core_->current ? core_->current->opens.load() : 0;
}

uint64_t ReplayRelay::lastRange() const {
  if (!core_) return 0;
  std::lock_guard<std::mutex> lock(core_->mu);
  return core_->current ? core_->current->lastRange.load() : 0;
}

}  // namespace marigold::platform
