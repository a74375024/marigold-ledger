// The rules of the Help exports' download link (PlaybackDiagnosticExportServer
// in tvOS), kept here so they are tested on the host; the server itself is
// platform/lan_share. The TV serves one file from memory on its LAN address
// at a random port and an unguessable path, for 10 minutes, and answers only
// an exact GET of that path.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace marigold::report::share {

// Big provider lists (70k channels) make a 20-30 MB report.
constexpr size_t kMaxBytes = 96u * 1024u * 1024u;
// The link stops working after 10 minutes.
constexpr int kLifetimeSeconds = 600;
// A request (line and headers) larger than this is dropped.
constexpr size_t kMaxRequestBytes = 8192;
// Connections served at once; more are closed at once.
constexpr int kMaxConnections = 4;
// A connection that makes no progress for this long is closed. (tvOS closed
// every connection 30 s after it opened, which can cut a big report on slow
// Wi-Fi; here it is 30 s without progress.)
constexpr int kIdleSeconds = 30;
// Random bytes in the path token (128 bits).
constexpr size_t kTokenBytes = 16;

constexpr std::string_view kCsvType = "text/csv; charset=utf-8";
constexpr std::string_view kChannelsFile = "marigold-channels.csv";
constexpr std::string_view kPlaybackLogFile = "marigold-playback-log.csv";

// True once `buffered` holds a whole request head ("\r\n\r\n").
bool headComplete(std::string_view buffered);

// Exact tokenized GET only: "GET <path> HTTP/1.1" (or 1.0), nothing after
// the head, valid UTF-8, no Content-Length or Transfer-Encoding header. No
// file-system routes, request bodies or directory listing.
bool accepts(std::string_view request, std::string_view path);

// The response head: "200 OK" with the file's type, length and
// "Content-Disposition: attachment", or "404 Not Found" (length 0).
// Cache-Control: no-store, X-Content-Type-Options: nosniff,
// Connection: close.
std::string responseHead(bool found, std::string_view contentType, size_t length,
                         std::string_view filename);

// A file name safe in a header and a URL: letters, digits, '.', '-' and '_'
// kept, anything else becomes '-'; "download" when nothing is left.
std::string safeFilename(std::string_view name);

// Lower-case hex of `bytes`.
std::string hex(const std::vector<uint8_t>& bytes);

// "/<token>/<filename>"
std::string sharePath(std::string_view token, std::string_view filename);
// "http://<host>:<port><path>"
std::string shareUrl(std::string_view host, int port, std::string_view path);

// One IPv4 address of a network interface (getifaddrs).
struct Interface {
  std::string name;     // "eth0", "wlan0"...
  std::string address;  // dotted quad
  bool up = false;
  bool loopback = false;
};

// The TV's LAN address: an interface that is up, not loopback, not
// 127.x / 0.x / link-local 169.254.x and not a Wi-Fi Direct ("p2p")
// interface. A private-range address (10.x, 172.16-31.x, 192.168.x) wins
// over others; otherwise interface order. (tvOS took the first "en"
// interface, Apple's name for Ethernet and Wi-Fi.)
std::optional<std::string> pickAddress(const std::vector<Interface>& interfaces);

}  // namespace marigold::report::share
