// Shared pieces of the provider clients: HTTP requests as plain values,
// errors that are safe to show, and the lenient readers the Swift clients
// use for provider JSON.
//
// The clients never do I/O. They build Request values for the platform's
// HTTP layer and parse the bodies it hands back. A Request's URL and headers
// carry the account's login, so they are secrets: never log or show them.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/util/json.hpp"
#include "core/util/time.hpp"

namespace marigold::sources {

struct Header {
  std::string name;
  std::string value;
};

struct Request {
  std::string method = "GET";
  std::string url;               // secret: carries the login
  std::vector<Header> headers;   // secret: may carry a token
  std::string body;              // JSON for POSTs, empty otherwise
  int timeoutSeconds = 30;
  bool allowInsecureTLS = false; // per-account opt-out of certificate checks

  // The value of a header (case-insensitive name), or empty.
  std::string header(std::string_view name) const;
};

// A message for the user. Built only through Error::make, which runs the text
// through redact() so a link, login or token can never be shown or logged.
struct Error {
  std::optional<int> status;  // HTTP status, when the error came from one
  std::string message;

  static Error make(std::string message, std::optional<int> status = std::nullopt);
};

template <class T>
class Result {
 public:
  Result(T value) : value_(std::move(value)) {}  // NOLINT: implicit on purpose
  Result(Error error) : error_(std::move(error)) {}  // NOLINT

  bool ok() const { return value_.has_value(); }
  explicit operator bool() const { return ok(); }
  const T& value() const { return *value_; }
  T& value() { return *value_; }
  T&& take() { return std::move(*value_); }
  const Error& error() const { return error_; }

 private:
  std::optional<T> value_;
  Error error_;
};

// Genre bits (kGenreSports, kGenreNews...) read from a programme's title and
// episode title: ProgramGenre.flags(title:episode:) of the tvOS app, which
// lives in the guide module. Clients take it as a parameter; null means no
// title flags.
using TitleGenres = std::function<uint8_t(const std::string& title, const std::optional<std::string>& episode)>;

// Percent-encodes everything but RFC 3986 unreserved characters
// (A-Z a-z 0-9 - . _ ~). Used for query values and path segments.
std::string percentEncode(std::string_view text);
// "a=1&b=2", keys sorted like the Swift clients (query.sorted { $0.key < $1.key }).
std::string queryString(std::vector<std::pair<std::string, std::string>> items);

// A base address the clients append paths to: http(s)://host[:port][/path],
// with no whitespace, control characters, user:password@, query or fragment.
bool isValidBaseUrl(std::string_view url);
// The path of a valid base address ("/emby"), or "" when it has none.
std::string_view basePath(std::string_view url);

// Swift's trimmingCharacters(in: .whitespacesAndNewlines) and
// (in: .whitespaces): ASCII and Unicode spaces (U+00A0, U+2000..U+200A, ...),
// the second without line breaks.
std::string_view trimWhitespaceAndNewlines(std::string_view s);
std::string_view trimWhitespace(std::string_view s);

// XtreamClient.int: a number, or text that reads as an integer or a decimal
// (truncated). Booleans count as 1 / 0 (NSNumber). Not finite or out of
// range gives nullopt (Swift would trap).
std::optional<int64_t> lenientInt(const json::Value& v);
// XtreamClient.string: non-empty text, or a number as text ("123").
std::optional<std::string> lenientString(const json::Value& v);
// A non-empty string value, else nullopt (no number conversion).
std::optional<std::string> stringField(const json::Value& v);

// Provider item ids are used in ids and in URL paths. Accept only short ids
// made of letters, digits, '-', '_' and '.', which every Xtream and Emby
// server uses, so a hostile list can't inject a path or a separator.
bool isSafeItemId(std::string_view id);
constexpr size_t kMaxItemIdBytes = 64;

// ISO 8601 internet date-time ("2026-10-21T20:00:00Z", "...+02:00",
// "...+0200"), no fraction. Like ISO8601DateFormatter with
// .withInternetDateTime.
std::optional<time::Seconds> parseInternetDateTime(std::string_view text);
// "2026-10-21T20:00:00Z".
std::string formatInternetDateTime(time::Seconds t);

}  // namespace marigold::sources
