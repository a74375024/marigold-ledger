#include "core/media/hls.hpp"

#include <algorithm>
#include <cstdlib>

namespace marigold::media::hls {
namespace {

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
  return s;
}

bool startsWith(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.substr(0, p.size()) == p; }

// An attribute of a tag's list (BANDWIDTH=..., RESOLUTION=1920x1080,
// CODECS="a,b"), "" when absent.
std::string_view attribute(std::string_view list, std::string_view name) {
  size_t i = 0;
  while (i < list.size()) {
    while (i < list.size() && (list[i] == ',' || list[i] == ' ')) ++i;
    size_t eq = list.find('=', i);
    if (eq == std::string_view::npos) return {};
    std::string_view key = trim(list.substr(i, eq - i));
    size_t v = eq + 1, end = v;
    if (v < list.size() && list[v] == '"') {
      end = list.find('"', v + 1);
      if (end == std::string_view::npos) end = list.size();
      if (key == name) return list.substr(v + 1, end - v - 1);
      i = end + 1;
      continue;
    }
    end = list.find(',', v);
    if (end == std::string_view::npos) end = list.size();
    if (key == name) return trim(list.substr(v, end - v));
    i = end;
  }
  return {};
}

uint64_t toU64(std::string_view s) {
  uint64_t v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') break;
    v = v * 10 + static_cast<uint64_t>(c - '0');
  }
  return v;
}

double toDouble(std::string_view s) {
  std::string t(s);
  return std::strtod(t.c_str(), nullptr);
}

}  // namespace

bool looksLikePlaylist(std::string_view head) {
  if (startsWith(head, "\xEF\xBB\xBF")) head.remove_prefix(3);
  head = trim(head);
  return startsWith(head, "#EXTM3U");
}

Playlist parse(std::string_view text) {
  Playlist p;
  std::optional<Variant> pendingVariant;
  std::optional<double> pendingSeconds;
  size_t at = 0;
  while (at <= text.size()) {
    size_t nl = text.find('\n', at);
    if (nl == std::string_view::npos) nl = text.size();
    std::string_view line = trim(text.substr(at, nl - at));
    at = nl + 1;
    if (line.empty()) {
      if (nl >= text.size()) break;
      continue;
    }
    if (line[0] == '#') {
      if (startsWith(line, "#EXT-X-STREAM-INF:")) {
        std::string_view list = line.substr(18);
        Variant v;
        v.bandwidth = toU64(attribute(list, "BANDWIDTH"));
        std::string_view res = attribute(list, "RESOLUTION");
        size_t x = res.find('x');
        if (x != std::string_view::npos) {
          v.width = static_cast<int>(toU64(res.substr(0, x)));
          v.height = static_cast<int>(toU64(res.substr(x + 1)));
        }
        std::string_view rate = attribute(list, "FRAME-RATE");
        if (!rate.empty()) v.frameRate = toDouble(rate);
        pendingVariant = v;
        p.master = true;
      } else if (startsWith(line, "#EXTINF:")) {
        pendingSeconds = toDouble(line.substr(8));
      } else if (startsWith(line, "#EXT-X-ENDLIST")) {
        p.ended = true;
      } else if (startsWith(line, "#EXT-X-MAP")) {
        p.fragmentedMp4 = true;
      } else if (startsWith(line, "#EXT-X-KEY:")) {
        std::string_view method = attribute(line.substr(11), "METHOD");
        if (!method.empty() && method != "NONE") p.encrypted = true;
      }
      if (nl >= text.size()) break;
      continue;
    }
    // A link: the variant or segment the tags before it describe.
    if (pendingVariant) {
      pendingVariant->uri = std::string(line);
      p.variants.push_back(std::move(*pendingVariant));
      pendingVariant.reset();
    } else {
      Segment s;
      s.uri = std::string(line);
      s.seconds = pendingSeconds.value_or(0);
      p.segments.push_back(std::move(s));
    }
    pendingSeconds.reset();
    if (nl >= text.size()) break;
  }
  return p;
}

const Variant* pickVariant(const Playlist& p) {
  const Variant* best = nullptr;
  for (const Variant& v : p.variants) {
    if (v.uri.empty()) continue;
    if (!best || v.bandwidth > best->bandwidth || (v.bandwidth == best->bandwidth && v.height > best->height))
      best = &v;
  }
  return best;
}

const Segment* pickSegment(const Playlist& p) {
  if (p.segments.empty()) return nullptr;
  if (p.ended) return &p.segments.front();
  size_t n = p.segments.size();
  return &p.segments[n >= 3 ? n - 3 : 0];
}

std::string resolve(std::string_view base, std::string_view ref) {
  if (ref.find("://") != std::string_view::npos) return std::string(ref);
  size_t scheme = base.find("://");
  if (scheme == std::string_view::npos) return std::string(ref);
  if (startsWith(ref, "//")) return std::string(base.substr(0, scheme + 1)) + std::string(ref);
  size_t hostEnd = base.find('/', scheme + 3);
  std::string_view origin = hostEnd == std::string_view::npos ? base : base.substr(0, hostEnd);
  if (startsWith(ref, "/")) return std::string(origin) + std::string(ref);
  // Relative: the base's folder (its query and file name dropped).
  std::string_view path = base;
  size_t q = path.find_first_of("?#", scheme + 3);
  if (q != std::string_view::npos) path = path.substr(0, q);
  size_t slash = path.rfind('/');
  if (slash == std::string_view::npos || slash < scheme + 3) return std::string(origin) + "/" + std::string(ref);
  return std::string(path.substr(0, slash + 1)) + std::string(ref);
}

}  // namespace marigold::media::hls
