// HTTP and background work on a small worker pool. The pool is the same on
// every platform; one transfer goes through the platform's network stack
// (http_transport.hpp: the vendored Legacy rede on webOS, verified TLS,
// redirect rules, body caps; HttpURLConnection on Android).
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

#include "core/net/http.hpp"

namespace marigold::http {

namespace transport {
struct Group;
}

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

class Client final : public Pool {
 public:
  Client() = default;
  ~Client() override;
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  // Call once on the main thread (webOS: libcurl's global init is not thread safe).
  // `wake` is called from a worker when a result is ready for pump().
  void start(int threads, std::function<void()> wake);
  void stop();

  // Pool. Callbacks run on the UI thread from pump().
  void submit(Request req, Done done) override;
  void post(std::function<void()> work, std::function<void()> done,
            Priority priority = Priority::Normal) override;
  Response fetchNow(Request req) override;
  void cancelAll() override;
  // UI thread: runs finished callbacks. Returns how many ran.
  int pump();

  size_t queued() const;
  size_t running() const { return running_.load(); }

 private:
  enum class Lane { General, Long, Playback };
  void worker(Lane lane);
  void execute(Ticket::Job& job);
  Ticket enqueue(std::shared_ptr<Ticket::Job> job);

  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::deque<std::shared_ptr<Ticket::Job>> queue_;
  std::deque<std::shared_ptr<Ticket::Job>> longQueue_;  // Priority::Long(First), one at a time
  std::deque<std::shared_ptr<Ticket::Job>> playQueue_;  // Priority::Playback, its own thread
  std::vector<std::thread> threads_;
  bool stopping_ = false;
  transport::Group* group_ = nullptr;

  std::mutex doneMu_;
  std::vector<std::shared_ptr<Ticket::Job>> done_;
  std::function<void()> wake_;
  std::atomic<size_t> running_{0};
};

// Reads one header from a raw header block ("Name: value\r\n..."), from the
// last response in it (redirect hops come first). Case-insensitive name.
std::string headerValue(std::string_view block, std::string_view name);

}  // namespace marigold::http
