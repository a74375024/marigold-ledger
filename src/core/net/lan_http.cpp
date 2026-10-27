#include "core/net/lan_http.hpp"

#include <cctype>
#include <cstdlib>

namespace marigold::lan {
namespace {

std::string lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
  return s;
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

const char* reason(int status) {
  switch (status) {
    case 200: return "OK";
    case 303: return "See Other";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 429: return "Too Many Requests";
    default: return "Error";
  }
}

}  // namespace

std::string Request::cookie(std::string_view name) const {
  auto it = headers.find("cookie");
  if (it == headers.end()) return {};
  std::string_view all = it->second;
  while (!all.empty()) {
    size_t semi = all.find(';');
    std::string_view part = trim(all.substr(0, semi));
    size_t eq = part.find('=');
    if (eq != std::string_view::npos && trim(part.substr(0, eq)) == name) return std::string(trim(part.substr(eq + 1)));
    if (semi == std::string_view::npos) break;
    all.remove_prefix(semi + 1);
  }
  return {};
}

Response Response::html(std::string body, int status) {
  Response r;
  r.status = status;
  r.type = "text/html; charset=utf-8";
  r.body = std::move(body);
  return r;
}

Response Response::json(std::string body, int status) {
  Response r;
  r.status = status;
  r.type = "application/json";
  r.body = std::move(body);
  return r;
}

Response Response::redirect(std::string location) {
  Response r;
  r.status = 303;
  r.headers.emplace_back("Location", std::move(location));
  return r;
}

Response Response::notFound() {
  Response r;
  r.status = 404;
  r.body = "Not found";
  return r;
}

Parse parse(std::string_view buffered, Request& out) {
  size_t end = buffered.find("\r\n\r\n");
  if (end == std::string_view::npos) return buffered.size() > kMaxHead ? Parse::Bad : Parse::Incomplete;
  if (end > kMaxHead) return Parse::Bad;
  std::string_view head = buffered.substr(0, end);
  size_t lineEnd = head.find("\r\n");
  std::string_view line = head.substr(0, lineEnd);
  size_t sp1 = line.find(' ');
  size_t sp2 = sp1 == std::string_view::npos ? sp1 : line.find(' ', sp1 + 1);
  if (sp2 == std::string_view::npos) return Parse::Bad;
  std::string_view version = line.substr(sp2 + 1);
  if (version != "HTTP/1.1" && version != "HTTP/1.0") return Parse::Bad;
  Request r;
  r.method = std::string(line.substr(0, sp1));
  std::string_view target = line.substr(sp1 + 1, sp2 - sp1 - 1);
  if (target.empty() || target.front() != '/') return Parse::Bad;
  size_t q = target.find('?');
  r.path = std::string(target.substr(0, q));
  if (q != std::string_view::npos) r.query = std::string(target.substr(q + 1));
  std::string_view rest = lineEnd == std::string_view::npos ? std::string_view() : head.substr(lineEnd + 2);
  while (!rest.empty()) {
    size_t e = rest.find("\r\n");
    std::string_view h = rest.substr(0, e);
    size_t colon = h.find(':');
    if (colon == std::string_view::npos || colon == 0) return Parse::Bad;
    r.headers[lower(trim(h.substr(0, colon)))] = std::string(trim(h.substr(colon + 1)));
    if (e == std::string_view::npos) break;
    rest.remove_prefix(e + 2);
  }
  if (r.headers.count("transfer-encoding")) return Parse::Bad;
  size_t length = 0;
  auto cl = r.headers.find("content-length");
  if (cl != r.headers.end()) {
    const std::string& v = cl->second;
    if (v.empty() || v.size() > 9 || v.find_first_not_of("0123456789") != std::string::npos) return Parse::Bad;
    length = static_cast<size_t>(std::strtoul(v.c_str(), nullptr, 10));
    if (length > kMaxBody) return Parse::Bad;
  }
  size_t bodyStart = end + 4;
  if (buffered.size() < bodyStart + length) return Parse::Incomplete;
  if (buffered.size() > bodyStart + length) return Parse::Bad;  // one request per connection
  r.body = std::string(buffered.substr(bodyStart, length));
  out = std::move(r);
  return Parse::Complete;
}

std::string serialize(const Response& r) {
  std::string out = "HTTP/1.1 " + std::to_string(r.status) + " " + reason(r.status) + "\r\n";
  out += "Content-Type: " + r.type + "\r\n";
  out += "Content-Length: " + std::to_string(r.body.size()) + "\r\n";
  for (const auto& [name, value] : r.headers) out += name + ": " + value + "\r\n";
  out += "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\n"
         "Connection: close\r\n\r\n";
  out += r.body;
  return out;
}

std::string urlDecode(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (c == '+') {
      out += ' ';
    } else if (c == '%' && i + 2 < s.size() && hexValue(s[i + 1]) >= 0 && hexValue(s[i + 2]) >= 0) {
      out += static_cast<char>(hexValue(s[i + 1]) * 16 + hexValue(s[i + 2]));
      i += 2;
    } else {
      out += c;
    }
  }
  return out;
}

std::map<std::string, std::string> parseForm(std::string_view body) {
  std::map<std::string, std::string> out;
  while (!body.empty()) {
    size_t amp = body.find('&');
    std::string_view pair = body.substr(0, amp);
    if (!pair.empty()) {
      size_t eq = pair.find('=');
      std::string key = urlDecode(pair.substr(0, eq));
      std::string value = eq == std::string_view::npos ? std::string() : urlDecode(pair.substr(eq + 1));
      out[key] = std::move(value);
    }
    if (amp == std::string_view::npos) break;
    body.remove_prefix(amp + 1);
  }
  return out;
}

std::string htmlEscape(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += c;
    }
  }
  return out;
}

}  // namespace marigold::lan
