#include "platform/http.hpp"

#include <algorithm>
#include <cstring>

#include "core/util/gunzip.hpp"
#include "core/util/log.hpp"
#include "core/util/strings.hpp"
#include "platform/http_transport.hpp"

namespace marigold::http {

struct Ticket::Job {
  Request req;
  Client::Done done;
  std::function<void()> work, workDone;  // post() jobs instead of HTTP
  Response resp;
  transport::Handle* handle = nullptr;
  std::atomic<bool> cancelled{false};
  ~Job() {
    if (handle) transport::releaseHandle(handle);
  }
  bool stopped() const { return cancelled.load() || !transport::live(handle); }
};

void Ticket::cancel() {
  if (!job_) return;
  job_->cancelled = true;
  if (job_->handle) transport::cancelHandle(job_->handle);
}

namespace {

// Body sink shared by the streamed and the gunzip-buffered paths.
struct Sink {
  Ticket::Job* job = nullptr;
  std::unique_ptr<gz::Gunzip> gunzip;
  std::string* buffer = nullptr;  // gunzip without onChunk: collect here
  size_t bufferCap = 0;
  uint64_t bytes = 0;
  bool overflow = false;

  bool deliver(std::string_view c) {
    bytes += c.size();
    if (job->req.onChunk) return job->req.onChunk(c);
    if (buffer->size() + c.size() > bufferCap) {
      overflow = true;
      return false;
    }
    buffer->append(c);
    return true;
  }
};

}  // namespace

std::string headerValue(std::string_view block, std::string_view name) {
  std::string value;
  size_t pos = 0;
  while (pos < block.size()) {
    size_t end = block.find('\n', pos);
    if (end == std::string_view::npos) end = block.size();
    std::string_view line = str::trim(block.substr(pos, end - pos));
    pos = end + 1;
    if (str::startsWith(line, "HTTP/")) {
      value.clear();  // a new response (after a redirect or 100 Continue)
      continue;
    }
    size_t colon = line.find(':');
    if (colon == std::string_view::npos) continue;
    if (str::iequals(str::trim(line.substr(0, colon)), name))
      value = std::string(str::trim(line.substr(colon + 1)));
  }
  return value;
}

Client::~Client() { stop(); }

void Client::start(int threads, std::function<void()> wake) {
  if (!threads_.empty()) return;
  transport::prepare();  // webOS: dlopen + curl_global_init here, on the main thread
  group_ = transport::createGroup();
  wake_ = std::move(wake);
  stopping_ = false;
  for (int i = 0; i < std::max(1, threads); ++i) threads_.emplace_back([this] { worker(false); });
  threads_.emplace_back([this] { worker(true); });
}

void Client::stop() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (threads_.empty() && !group_) return;
    stopping_ = true;
    queue_.clear();
    longQueue_.clear();
  }
  if (group_) transport::cancelGroup(group_);
  cv_.notify_all();
  for (std::thread& t : threads_) t.join();
  threads_.clear();
  {
    std::lock_guard<std::mutex> lock(doneMu_);
    done_.clear();
  }
  if (group_) {
    transport::releaseGroup(group_);
    group_ = nullptr;
  }
}

void Client::submit(Request req, Done done) {
  auto job = std::make_shared<Ticket::Job>();
  job->req = std::move(req);
  job->done = std::move(done);
  enqueue(std::move(job));
}

void Client::post(std::function<void()> work, std::function<void()> done, Priority priority) {
  auto job = std::make_shared<Ticket::Job>();
  job->work = std::move(work);
  job->workDone = std::move(done);
  job->req.priority = priority;
  enqueue(std::move(job));
}

Ticket Client::enqueue(std::shared_ptr<Ticket::Job> job) {
  std::lock_guard<std::mutex> lock(mu_);
  job->handle = transport::createHandle(group_);
  if (stopping_ || threads_.empty()) {
    job->cancelled = true;
    return Ticket(job);
  }
  if (job->req.priority == Priority::Long) {
    longQueue_.push_back(job);
    cv_.notify_all();
    return Ticket(job);
  }
  // Highest priority first; first come, first served within a priority.
  auto pos = std::find_if(queue_.begin(), queue_.end(), [&](const auto& other) {
    return other->req.priority < job->req.priority;
  });
  queue_.insert(pos, job);
  cv_.notify_all();  // the long-lane thread shares cv_
  return Ticket(job);
}

Response Client::fetchNow(Request req) {
  Ticket::Job job;
  job.req = std::move(req);
  {
    std::lock_guard<std::mutex> lock(mu_);
    job.handle = transport::createHandle(group_);
  }
  execute(job);
  return std::move(job.resp);
}

void Client::cancelAll() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    queue_.clear();
    longQueue_.clear();
  }
  // Running requests see the new generation and stop at the next chunk;
  // any result they still post is dropped by pump().
  if (group_) transport::advanceGroup(group_);
  std::lock_guard<std::mutex> lock(doneMu_);
  done_.clear();
}

size_t Client::queued() const {
  std::lock_guard<std::mutex> lock(mu_);
  return queue_.size() + longQueue_.size();
}

int Client::pump() {
  std::vector<std::shared_ptr<Ticket::Job>> ready;
  {
    std::lock_guard<std::mutex> lock(doneMu_);
    ready.swap(done_);
  }
  int ran = 0;
  for (auto& job : ready) {
    if (job->stopped()) continue;
    if (job->work) {
      if (job->workDone) job->workDone();
    } else if (job->done) {
      job->done(std::move(job->resp));
    } else {
      continue;
    }
    ++ran;
  }
  return ran;
}

void Client::worker(bool longLane) {
  std::deque<std::shared_ptr<Ticket::Job>>& queue = longLane ? longQueue_ : queue_;
  while (true) {
    std::shared_ptr<Ticket::Job> job;
    {
      std::unique_lock<std::mutex> lock(mu_);
      cv_.wait(lock, [&] { return stopping_ || !queue.empty(); });
      if (stopping_) return;
      job = std::move(queue.front());
      queue.pop_front();
    }
    if (job->stopped()) continue;
    ++running_;
    if (job->work) job->work();
    else execute(*job);
    --running_;
    if (job->stopped()) continue;
    {
      std::lock_guard<std::mutex> lock(doneMu_);
      done_.push_back(std::move(job));
    }
    if (wake_) wake_();
  }
}

void Client::execute(Ticket::Job& job) {
  const Request& q = job.req;
  Response& out = job.resp;

  bool streamed = static_cast<bool>(q.onChunk) || q.gunzip;
  Sink sink;
  sink.job = &job;
  sink.buffer = &out.body;
  sink.bufferCap = q.maxBytes ? q.maxBytes : transport::kBufferedDefault;
  if (q.gunzip) {
    Sink* s = &sink;
    sink.gunzip = std::make_unique<gz::Gunzip>([s](std::string_view c) { return s->deliver(c); },
                                               q.onChunk ? transport::kStreamedMax : sink.bufferCap + 1);
  }
  // A gzip body is capped on the wire here and inflated size in the Sink.
  size_t wireCap = q.maxBytes ? q.maxBytes : streamed && q.onChunk ? transport::kStreamedMax : 0;
  if (!q.onChunk && q.gunzip && !q.maxBytes) wireCap = transport::kBufferedMax;
  std::function<bool(std::string_view)> receive;
  if (streamed) {
    receive = [&sink](std::string_view chunk) {
      if (sink.job->stopped()) return false;
      if (sink.gunzip) return sink.gunzip->feed(chunk);
      return sink.deliver(chunk);
    };
  }

  transport::Result r = transport::fetch(q, job.handle, wireCap, receive, [&job] { return job.stopped(); });

  out.status = r.status;
  out.ms = r.ms;
  out.retryAfterS = r.retryAfterS;
  out.mime = r.mime;
  if (!r.headers.empty()) {
    out.etag = headerValue(r.headers, "ETag");
    out.lastModified = headerValue(r.headers, "Last-Modified");
  }
  if (!streamed) out.body = std::move(r.body);
  out.bytes = streamed ? sink.bytes : out.body.size();
  bool success = r.status >= 200 && r.status < 300;
  bool streamOk = true;
  if (streamed && success && sink.gunzip) streamOk = sink.gunzip->finish();

  out.cancelled = job.stopped();
  if (out.cancelled) out.error = "cancelled";
  else if (!r.error.empty()) out.error = r.error;
  else if (sink.overflow) out.error = "body-cap";
  else if (!streamOk) out.error = "gzip";
  else if (!success && r.status != 304) out.error = "http " + std::to_string(r.status);
  out.ok = out.error.empty();
  if (!streamed && !success) out.body.clear();

  const char* tag = q.tag.empty() ? "request" : q.tag.c_str();
  if (out.ok)
    MLOG_D("http", "%s %d in %u ms, %llu bytes", tag, out.status, out.ms,
           static_cast<unsigned long long>(out.bytes));
  else if (!out.cancelled)
    MLOG_W("http", "%s failed: %s in %u ms", tag, out.error.c_str(), out.ms);
  if (q.afterTransfer && !out.cancelled) q.afterTransfer(out);
}

}  // namespace marigold::http
