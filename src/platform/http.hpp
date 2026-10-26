// HTTP and background work on a small worker pool. HTTP goes through the
// vendored Legacy rede_pedir (verified TLS, redirect rules, body caps).
// Requests block on a worker, never on the UI thread. Completion callbacks run on the UI thread, from pump().
//
// Streaming: with `onChunk` set, the body is handed over in pieces on the
// worker as it arrives and never held whole (channel lists, guide files).
// `gunzip` inflates .gz content on the way through.
//
// Nothing here logs a URL, header or body; the log line names the tag only.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

struct RedeGrupo;
struct RedeJob;

namespace marigold::http {

enum class Priority { Background = 0, Normal = 1, Visible = 2 };

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

// A handle to cancel a pending or running request.
class Ticket {
 public:
  Ticket() = default;
  void cancel();
  bool valid() const { return job_ != nullptr; }

  struct Job;  // defined in http.cpp

 private:
  friend class Client;
  explicit Ticket(std::shared_ptr<Job> job) : job_(std::move(job)) {}
  std::shared_ptr<Job> job_;
};

class Client {
 public:
  using Done = std::function<void(Response&&)>;

  Client() = default;
  ~Client();
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  // Call once on the main thread (libcurl's global init is not thread safe).
  // `wake` is called from a worker when a result is ready for pump().
  void start(int threads, std::function<void()> wake);
  void stop();

  Ticket submit(Request req, Done done);
  // Runs `work` on a pool thread (file reads, parsing, cache writes), then
  // `done` on the UI thread from pump().
  Ticket post(std::function<void()> work, std::function<void()> done,
              Priority priority = Priority::Normal);
  // Drops every queued and running request (account switch, exit). Their
  // callbacks never run.
  void cancelAll();
  // UI thread: runs finished callbacks. Returns how many ran.
  int pump();

  size_t queued() const;
  size_t running() const { return running_.load(); }

 private:
  void worker();
  void execute(Ticket::Job& job);
  Ticket enqueue(std::shared_ptr<Ticket::Job> job);

  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::deque<std::shared_ptr<Ticket::Job>> queue_;
  std::vector<std::thread> threads_;
  bool stopping_ = false;
  RedeGrupo* group_ = nullptr;

  std::mutex doneMu_;
  std::vector<std::shared_ptr<Ticket::Job>> done_;
  std::function<void()> wake_;
  std::atomic<size_t> running_{0};
};

// Reads one header from a raw header block ("Name: value\r\n..."), from the
// last response in it (redirect hops come first). Case-insensitive name.
std::string headerValue(std::string_view block, std::string_view name);

}  // namespace marigold::http
