// Just enough HTTP/1.1 for a page the TV serves on the home network (phone
// sign-in): one request per connection, an optional small form body, and
// the response written in one go. The sockets are platform/lan_server; the
// rules are here so they are tested on the host.
#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace marigold::lan {

// A request head larger than this is refused; so is a body over kMaxBody.
constexpr size_t kMaxHead = 8192;
constexpr size_t kMaxBody = 16384;

struct Request {
  std::string method;  // "GET", "POST"
  std::string path;    // the target without its query ("/abc/send")
  std::string query;   // after '?', undecoded
  std::map<std::string, std::string> headers;  // names lower-case
  std::string body;

  // The value of one cookie ("" when absent).
  std::string cookie(std::string_view name) const;
};

struct Response {
  int status = 404;
  std::string type = "text/plain; charset=utf-8";
  std::string body;
  std::vector<std::pair<std::string, std::string>> headers;  // extra ones

  static Response html(std::string body, int status = 200);
  static Response json(std::string body, int status = 200);
  static Response redirect(std::string location);
  static Response notFound();
};

enum class Parse { Incomplete, Complete, Bad };

// Reads `buffered` (what arrived so far) into `out`. Incomplete until the
// head and a body of Content-Length bytes are in. Bad: not HTTP/1.x, too
// big, chunked bodies, or a body without Content-Length.
Parse parse(std::string_view buffered, Request& out);

// Status line, Content-Type and Content-Length, then the extra headers and
// the ones every response carries: Cache-Control: no-store,
// X-Content-Type-Options: nosniff, Referrer-Policy: no-referrer and
// Connection: close.
std::string serialize(const Response& r);

// application/x-www-form-urlencoded: '+' is a space, %XX a byte. Later keys
// win; malformed escapes are kept as they are.
std::map<std::string, std::string> parseForm(std::string_view body);
std::string urlDecode(std::string_view s);

// &, <, >, " and ' as entities.
std::string htmlEscape(std::string_view s);

}  // namespace marigold::lan
