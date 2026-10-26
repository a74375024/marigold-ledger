#include "core/report/share.hpp"

#include "core/util/strings.hpp"

namespace marigold::report::share {
namespace {

// Dotted quad to its four parts; nullopt when it isn't one.
std::optional<std::vector<int>> octets(std::string_view address) {
  std::vector<std::string_view> parts = str::split(address, '.', true);
  if (parts.size() != 4) return std::nullopt;
  std::vector<int> out;
  for (std::string_view p : parts) {
    if (p.empty() || p.size() > 3) return std::nullopt;
    int v = 0;
    for (char c : p) {
      if (c < '0' || c > '9') return std::nullopt;
      v = v * 10 + (c - '0');
    }
    if (v > 255) return std::nullopt;
    out.push_back(v);
  }
  return out;
}

bool isPrivate(const std::vector<int>& o) {
  return o[0] == 10 || (o[0] == 172 && o[1] >= 16 && o[1] <= 31) || (o[0] == 192 && o[1] == 168);
}

}  // namespace

bool headComplete(std::string_view buffered) {
  return buffered.find("\r\n\r\n") != std::string_view::npos;
}

bool accepts(std::string_view request, std::string_view path) {
  if (request.size() > kMaxRequestBytes || !str::utf8Valid(request)) return false;
  size_t headEnd = request.find("\r\n\r\n");
  // The head must end the request: no body, no second request.
  if (headEnd == std::string_view::npos || headEnd + 4 != request.size()) return false;
  std::string_view head = request.substr(0, headEnd);
  // Lines split on "\r\n" (Swift components(separatedBy:)).
  std::vector<std::string_view> lines;
  size_t from = 0;
  while (true) {
    size_t at = head.find("\r\n", from);
    lines.push_back(head.substr(from, at == std::string_view::npos ? std::string_view::npos
                                                                     : at - from));
    if (at == std::string_view::npos) break;
    from = at + 2;
  }
  std::vector<std::string_view> parts = str::split(lines.front(), ' ', true);
  if (parts.size() != 3 || parts[0] != "GET" || parts[1] != path ||
      (parts[2] != "HTTP/1.1" && parts[2] != "HTTP/1.0"))
    return false;
  for (size_t i = 1; i < lines.size(); ++i) {
    std::string_view line = lines[i];
    std::string name = str::lowerAscii(line.substr(0, line.find(':')));
    if (name == "content-length" || name == "transfer-encoding") return false;
  }
  return true;
}

std::string responseHead(bool found, std::string_view contentType, size_t length,
                         std::string_view filename) {
  std::string head = found ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 404 Not Found\r\n";
  head += "Content-Type: " + std::string(contentType) + "\r\n";
  head += "Content-Length: " + std::to_string(found ? length : 0) + "\r\n";
  head += "Content-Disposition: attachment; filename=\"" + safeFilename(filename) + "\"\r\n";
  head += "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n";
  return head;
}

std::string safeFilename(std::string_view name) {
  std::string out;
  for (char c : name) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '-' || c == '_';
    out += ok ? c : '-';
  }
  // No hidden or relative names.
  while (!out.empty() && out.front() == '.') out.erase(0, 1);
  return out.empty() ? "download" : out;
}

std::string hex(const std::vector<uint8_t>& bytes) {
  static const char kDigits[] = "0123456789abcdef";
  std::string out;
  out.reserve(bytes.size() * 2);
  for (uint8_t b : bytes) {
    out += kDigits[b >> 4];
    out += kDigits[b & 15];
  }
  return out;
}

std::string sharePath(std::string_view token, std::string_view filename) {
  return "/" + std::string(token) + "/" + safeFilename(filename);
}

std::string shareUrl(std::string_view host, int port, std::string_view path) {
  return "http://" + std::string(host) + ":" + std::to_string(port) + std::string(path);
}

std::optional<std::string> pickAddress(const std::vector<Interface>& interfaces) {
  std::optional<std::string> fallback;
  for (const Interface& i : interfaces) {
    if (!i.up || i.loopback || str::startsWith(i.name, "p2p")) continue;
    std::optional<std::vector<int>> o = octets(i.address);
    if (!o) continue;
    const std::vector<int>& q = *o;
    if (q[0] == 127 || q[0] == 0 || (q[0] == 169 && q[1] == 254)) continue;
    if (isPrivate(q)) return i.address;
    if (!fallback) fallback = i.address;
  }
  return fallback;
}

}  // namespace marigold::report::share
