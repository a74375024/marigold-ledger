// A pretend TV for engine tests: the network answers from canned bodies, the
// saved files live in a temp folder, and the clock only moves when a test
// moves it. Nothing runs on another thread: drain() runs every queued job
// and request (and whatever they queue in turn) on the calling thread, so a
// test sees the engine reach a settled state before it checks anything.
#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <unistd.h>

#include "core/engine/host.hpp"
#include "core/net/http.hpp"
#include "core/util/time.hpp"

namespace fake {

namespace http = marigold::http;

class Pool final : public http::Pool {
 public:
  struct Canned {
    int status = 200;
    std::string body;
  };

  // Links not answered here fail like a network that is down.
  void answer(const std::string& url, std::string body, int status = 200) { canned_[url] = {status, std::move(body)}; }

  void submit(http::Request req, Done done) override {
    std::string url = req.url;
    std::function<void()> step = [this, req = std::move(req), done = std::move(done)]() mutable {
      http::Response r = run(req);
      if (done) done(std::move(r));
    };
    if (held_.count(url)) waiting_.emplace_back(std::move(url), std::move(step));
    else queue_.push_back(std::move(step));
  }

  // A slow network: requests for a held link wait until it is let go.
  void hold(const std::string& url) { held_.insert(url); }
  void letGo(const std::string& url) {
    held_.erase(url);
    for (auto it = waiting_.begin(); it != waiting_.end();) {
      if (it->first != url) {
        ++it;
        continue;
      }
      queue_.push_back(std::move(it->second));
      it = waiting_.erase(it);
    }
  }

  void post(std::function<void()> work, std::function<void()> done, http::Priority) override {
    queue_.push_back([work = std::move(work), done = std::move(done)] {
      if (work) work();
      if (done) done();
    });
  }

  http::Response fetchNow(http::Request req) override { return run(req); }

  void cancelAll() override {
    queue_.clear();
    waiting_.clear();
  }

  // No waiting on the pretend TV; the waits are counted.
  void pause(unsigned ms) override { paused += ms; }
  uint64_t paused = 0;

  // Runs everything queued, including what the queued work queues. Returns
  // the number of steps; a test can bound it to catch a loop.
  size_t drain(size_t limit = 10000) {
    size_t steps = 0;
    while (!queue_.empty() && steps < limit) {
      auto step = std::move(queue_.front());
      queue_.pop_front();
      step();
      ++steps;
    }
    return steps;
  }

  // How many times each link was asked for.
  std::map<std::string, int> asked;
  // Streamed bodies: whether the server says how big they are (onProgress
  // gets the size), and a look in after each piece (on the same thread).
  bool announceLength = false;
  std::function<void()> duringBody;

 private:
  http::Response run(http::Request& req) {
    ++asked[req.url];
    http::Response r;
    auto it = canned_.find(req.url);
    if (it == canned_.end()) {
      r.error = "offline";
    } else {
      r.status = it->second.status;
      r.ok = r.status >= 200 && r.status < 300;
      const std::string& body = it->second.body;
      r.bytes = body.size();
      if (req.onChunk) {
        // In pieces, like a real transfer, with the progress the pool
        // reports (the size only when the server is to have given it).
        constexpr size_t kPiece = 4096;
        for (size_t at = 0; at < body.size(); at += kPiece) {
          if (!req.onChunk(std::string_view(body).substr(at, kPiece))) break;
          if (req.onProgress) req.onProgress(std::min(body.size(), at + kPiece), announceLength ? body.size() : 0);
          if (duringBody) duringBody();
        }
      } else {
        r.body = body;
      }
    }
    if (req.afterTransfer) req.afterTransfer(r);
    return r;
  }

  std::map<std::string, Canned> canned_;
  std::deque<std::function<void()>> queue_;
  std::set<std::string> held_;
  std::vector<std::pair<std::string, std::function<void()>>> waiting_;
};

// A real folder (the engine checks file dates), removed afterwards.
class Files final : public marigold::engine::Files {
 public:
  Files() {
    static int serial = 0;
    dir_ = (std::filesystem::temp_directory_path() /
            ("marigold-engine-" + std::to_string(::getpid()) + "-" + std::to_string(++serial)))
               .string();
    std::filesystem::remove_all(dir_);
    std::filesystem::create_directories(dir_);
  }
  ~Files() override {
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
  }
  Files(const Files&) = delete;
  Files& operator=(const Files&) = delete;

  const std::string& dir() const override { return dir_; }
  std::string path(const std::string& name) const override { return dir_ + "/" + name; }
  std::optional<std::string> read(const std::string& name) const override {
    std::ifstream in(path(name), std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
  }
  bool write(const std::string& name, const std::string& text, bool) override {
    std::ofstream out(path(name), std::ios::binary | std::ios::trunc);
    out << text;
    return static_cast<bool>(out);
  }

 private:
  std::string dir_;
};

// Pool, folder and clock together, and the Host the engine is built with.
struct Tv {
  Pool pool;
  Files files;
  uint32_t ms = 0;
  marigold::time::Zone zone = marigold::time::Zone::utc();

  marigold::engine::Host host() {
    marigold::engine::Host h{pool, files};
    h.ms = [this] { return ms; };
    h.zone = [this]() -> const marigold::time::Zone& { return zone; };
    h.playingFull = [] { return false; };
    h.build = "test";
    return h;
  }
};

}  // namespace fake
