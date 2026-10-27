// One HTTP transfer, as each platform's network stack does it, for the
// shared worker pool in http.cpp (queues, priorities, cancellation, gzip,
// body caps, the log). webOS: http_rede.cpp over the vendored Legacy rede
// (the TV's libcurl); Android: android/http.cpp over the system's
// HttpURLConnection. Only http.cpp includes this.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "core/net/http.hpp"

namespace marigold::http::transport {

// Body caps (the same numbers as Legacy's rede.h).
constexpr size_t kBufferedDefault = 8u * 1024u * 1024u;  // a buffered body, unless the request says
constexpr size_t kBufferedMax = 32u * 1024u * 1024u;     // a gzip body on the wire
constexpr size_t kStreamedMax = 1024u * 1024u * 1024u;   // a streamed body

// Transfers belong to a group (the pool) and each has a handle. Cancelling a
// handle, cancelling the group, or advancing the group past the handle's
// generation (cancelAll) makes the transfer stop at its next chance.
struct Group;
struct Handle;

void prepare();  // main thread, once, before the first transfer
Group* createGroup();
void cancelGroup(Group* g);   // the pool is stopping
void advanceGroup(Group* g);  // drop everything started so far
void releaseGroup(Group* g);
Handle* createHandle(Group* g);
void cancelHandle(Handle* h);
bool live(const Handle* h);  // not cancelled, and its group hasn't moved on
void releaseHandle(Handle* h);

struct Result {
  int status = 0;            // 0: no HTTP response
  unsigned ms = 0;
  int retryAfterS = 0;
  std::string mime;
  std::string headers;       // the raw header block ("Name: value\r\n...")
  std::string body;          // when not streamed
  std::string error;         // "" or a short code: "timeout", "transport", "body-cap"...
};

// Runs `q` on the calling (worker) thread. `wireCap` caps the body bytes
// received (0: kBufferedDefault for a buffered body). With `receive` set
// the body is handed over in pieces and not kept; it returns false to stop.
// `stop` is polled while waiting.
Result fetch(const Request& q, Handle* h, size_t wireCap, const std::function<bool(std::string_view)>& receive,
             const std::function<bool()>& stop);

}  // namespace marigold::http::transport
