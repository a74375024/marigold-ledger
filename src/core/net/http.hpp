// HTTP requests and background jobs as the engine sees them: what it asks
// for, what comes back, and the pool that runs both. Each platform supplies
// the pool (webOS: platform::http::Client over libcurl; a test: a fake that
// answers from canned bodies). Nothing here does I/O.
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace marigold::http {

// `Long` is for jobs that hold a thread for minutes (guide builds, big
// downloads): they run one at a time on a thread of their own, so they can
// never take every worker away from what is on screen.
// `LongFirst` is a long job that goes ahead of the other waiting long jobs
// (the provider's own guide before outside listings and logo files).
enum class Priority { Background = 0, Normal = 1, Visible = 2, Long = 3, LongFirst = 4 };

struct Response;
// Worker thread, after the transfer and before the UI callback: the place to
// parse or decode a body without blocking the UI.
using WorkerStep = std::function<void(Response&)>;

struct Request {
  std::string method;  // empty = GET
  std::string url;
  std::vector<std::string> headers;  // "Name: value"
  std::string body;
  unsigned timeoutMs = 15000;
  size_t maxBytes = 0;   // 0 = the safe default (8 MB buffered, 1 GB streamed)
  bool follow = true;
  bool personalServer = false;  // Emby on a LAN: self-signed certificates allowed
  bool gunzip = false;          // inflate a gzip body (sniffed; plain passes)
  Priority priority = Priority::Normal;
  std::string tag;  // for the log, e.g. "xtream.live"
  WorkerStep afterTransfer;

  // Worker thread. Return false to stop the transfer.
  std::function<bool(std::string_view)> onChunk;
};

struct Response {
  int status = 0;           // 0 = no HTTP response
  bool ok = false;          // transport fine and 2xx (or 304)
  bool cancelled = false;
  std::string error;        // short code for the log, never a URL
  std::string body;         // empty when streamed
  std::string mime, etag, lastModified;
  int retryAfterS = 0;
  unsigned ms = 0;
  uint64_t bytes = 0;       // body bytes after decoding
};

// Runs requests and jobs off the UI thread and hands results back on it.
class Pool {
 public:
  using Done = std::function<void(Response&&)>;
  virtual ~Pool() = default;

  // `done` runs on the UI thread with the response.
  virtual void submit(Request req, Done done) = 0;
  // Runs `work` on a pool thread (file reads, parsing, cache writes), then
  // `done` on the UI thread.
  virtual void post(std::function<void()> work, std::function<void()> done,
                    Priority priority = Priority::Normal) = 0;
  // Runs a request on the calling thread and returns its response. For
  // background jobs already on a pool thread (guide builds); never on the UI
  // thread. cancelAll() stops it too.
  virtual Response fetchNow(Request req) = 0;
  // Drops every queued and running request (account switch, exit). Their
  // callbacks never run.
  virtual void cancelAll() = 0;
  // Waits on a pool thread: the back-off before a retry, the spacing
  // between requests to one site. A test pool returns at once.
  virtual void pause(unsigned ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
};

}  // namespace marigold::http
