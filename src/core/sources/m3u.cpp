#include "core/sources/m3u.hpp"

#include <cmath>
#include <cstdlib>

#include "core/util/strings.hpp"

namespace marigold::sources::m3u {
namespace {

constexpr std::string_view kBom = "\xEF\xBB\xBF";

bool isSpace(char c) { return c == ' ' || c == '\t'; }

bool isKeyChar(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
         c == ':' || c == '.';
}

std::string_view trimLine(std::string_view s) {
  while (!s.empty() && (isSpace(s.front()) || s.front() == '\0')) s.remove_prefix(1);
  while (str::startsWith(s, kBom)) {
    s.remove_prefix(kBom.size());
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
  }
  while (!s.empty() && (isSpace(s.back()) || s.back() == '\0')) s.remove_suffix(1);
  return s;
}

bool startsWithNoCase(std::string_view s, std::string_view prefix) {
  return s.size() >= prefix.size() && str::iequals(s.substr(0, prefix.size()), prefix);
}

// "scheme://rest" with a plausible scheme and no whitespace.
bool isAbsoluteLink(std::string_view link) {
  size_t sep = link.find("://");
  if (sep == std::string_view::npos || sep == 0) return false;
  for (size_t i = 0; i < sep; ++i) {
    char c = link[i];
    bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    bool ok = letter || (i > 0 && ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.'));
    if (!ok) return false;
  }
  if (sep + 3 >= link.size()) return false;
  for (char c : link)
    if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7F) return false;
  return true;
}

// Index of the first ',' outside double quotes, or npos. The channel name
// follows it; attribute values may hold commas ("ESPN, Brasil").
size_t nameComma(std::string_view s) {
  bool quoted = false;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '"') quoted = !quoted;
    else if (s[i] == ',' && !quoted) return i;
  }
  return std::string_view::npos;
}

// tvg-shift is in hours ("-2", "+1.5"); minutes back, within a day.
std::optional<int> shiftMinutes(std::string_view text) {
  std::string t(str::trim(text));
  if (t.empty()) return std::nullopt;
  size_t i = 0;
  if (t[i] == '+' || t[i] == '-') ++i;
  bool digit = false, dot = false;
  for (; i < t.size(); ++i) {
    if (t[i] >= '0' && t[i] <= '9') digit = true;
    else if ((t[i] == '.' || t[i] == ',') && !dot) {
      dot = true;
      t[i] = '.';
    } else {
      return std::nullopt;
    }
  }
  if (!digit) return std::nullopt;
  double hours = std::strtod(t.c_str(), nullptr);
  double minutes = std::round(hours * 60.0);
  if (!(std::fabs(minutes) <= kMaxShiftMinutes)) return std::nullopt;
  return static_cast<int>(minutes);
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string percentDecode(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() && hexValue(s[i + 1]) >= 0 && hexValue(s[i + 2]) >= 0) {
      int v = hexValue(s[i + 1]) * 16 + hexValue(s[i + 2]);
      out += v < 0x20 || v == 0x7F ? ' ' : static_cast<char>(v);
      i += 2;
    } else {
      out += s[i] == '+' ? ' ' : s[i];
    }
  }
  return out;
}

// Header values must stay on one line and carry no control characters.
bool headerValueOk(std::string_view v) {
  if (v.empty()) return false;
  for (char c : v)
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) return false;
  return true;
}

using Headers = std::vector<std::pair<std::string, std::string>>;

// Adds a request header; a later value for the same name replaces the earlier.
void setHeader(Headers& headers, std::string name, std::string_view value) {
  std::string v(str::trim(value));
  if (!headerValueOk(v)) return;
  for (auto& h : headers) {
    if (str::iequals(h.first, name)) {
      h.second = std::move(v);
      return;
    }
  }
  headers.emplace_back(std::move(name), std::move(v));
}

}  // namespace

std::optional<std::string> attribute(std::string_view s, std::string_view name) {
  size_t i = 0;
  const size_t n = s.size();
  while (i < n) {
    while (i < n && isSpace(s[i])) ++i;
    size_t keyStart = i;
    while (i < n && isKeyChar(s[i])) ++i;
    std::string_view key = s.substr(keyStart, i - keyStart);
    if (!key.empty() && i < n && s[i] == '=') {
      ++i;
      std::string_view value;
      if (i < n && (s[i] == '"' || s[i] == '\'')) {
        char quote = s[i++];
        size_t end = s.find(quote, i);
        if (end == std::string_view::npos) end = n;
        value = s.substr(i, end - i);
        i = end < n ? end + 1 : n;
      } else {
        size_t start = i;
        while (i < n && !isSpace(s[i])) ++i;
        value = s.substr(start, i - start);
      }
      if (str::iequals(key, name)) return std::string(value);
      continue;
    }
    // Not an attribute: skip the token, including any quoted part.
    if (i == keyStart && i < n && !isSpace(s[i]) && s[i] != '"') ++i;
    while (i < n && !isSpace(s[i])) {
      if (s[i] == '"') {
        size_t end = s.find('"', i + 1);
        i = end == std::string_view::npos ? n : end + 1;
      } else {
        ++i;
      }
    }
  }
  return std::nullopt;
}

Parser::Parser(std::string accountId, OnChannel onChannel)
    : sourceId_("m3u:" + std::move(accountId)), onChannel_(std::move(onChannel)) {
  line_.reserve(256);
}

bool Parser::feed(std::string_view chunk) {
  size_t i = 0;
  const size_t n = chunk.size();
  while (i < n && !stopped_) {
    size_t j = i;
    while (j < n && chunk[j] != '\n' && chunk[j] != '\r') ++j;
    if (!dropping_) {
      if (line_.size() + (j - i) > kMaxLineBytes) {
        line_.append(chunk.data() + i, kMaxLineBytes - line_.size());
        longLine();
        line_.clear();
        dropping_ = true;
      } else {
        line_.append(chunk.data() + i, j - i);
      }
    }
    if (j < n) {  // end of line
      if (!dropping_) line(line_);
      line_.clear();
      dropping_ = false;
      ++j;
    }
    i = j;
  }
  return !stopped_;
}

std::optional<Error> Parser::finish() {
  if (!stopped_ && !dropping_ && !line_.empty()) line(line_);
  line_.clear();
  dropping_ = false;
  if (error_) return error_;
  if (!header_.sawExtM3u && channels_ == 0 && !truncated_) return Error::make("The link didn't return a playlist.");
  return std::nullopt;
}

// A line over the cap is dropped. If it was a link or an #EXTINF, the entry
// it belonged to is dropped too, so the next link doesn't take its name.
void Parser::longLine() {
  ++longLines_;
  std::string_view head = trimLine(line_);
  if (firstLine_) {
    firstLine_ = false;
    if (head.empty() || (head[0] != '#' && head.find("://") == std::string_view::npos)) {
      error_ = Error::make("The link didn't return a playlist.");
      stopped_ = true;
      return;
    }
  }
  if (!head.empty() && head[0] == '#' && !startsWithNoCase(head, "#EXTINF")) return;
  if (pending_.info || (!head.empty() && head[0] != '#')) ++skipped_;
  pending_ = Pending();
}

void Parser::line(std::string_view raw) {
  std::string_view l = trimLine(raw);
  if (l.empty()) return;
  if (firstLine_) {
    firstLine_ = false;
    // Error pages and JSON replies are not playlists.
    if (l[0] != '#' && !isAbsoluteLink(l)) {
      error_ = Error::make("The link didn't return a playlist.");
      stopped_ = true;
      return;
    }
  }
  if (startsWithNoCase(l, "#EXTM3U")) {
    header_.sawExtM3u = true;
    std::string_view attrs = l.substr(7);
    if (!header_.guideUrl) {
      std::optional<std::string> url = attribute(attrs, "url-tvg");
      if (!url) url = attribute(attrs, "x-tvg-url");
      if (!url) url = attribute(attrs, "tvg-url");
      if (url) {
        // Several guides separated by commas: the first.
        std::string first(str::trim(std::string_view(*url).substr(0, url->find(','))));
        if (!first.empty()) header_.guideUrl = std::move(first);
      }
    }
    if (std::optional<std::string> shift = attribute(attrs, "tvg-shift")) header_.tvgShiftMinutes = shiftMinutes(*shift);
    return;
  }
  if (startsWithNoCase(l, "#EXTINF")) {
    // A second #EXTINF abandons the entry that had no link.
    if (pending_.info) ++skipped_;
    pending_ = Pending();
    pending_.info = true;
    std::string_view rest = l.substr(7);
    if (!rest.empty() && rest[0] == ':') rest.remove_prefix(1);
    size_t comma = nameComma(rest);
    std::string_view attrs = comma == std::string_view::npos ? rest : rest.substr(0, comma);
    if (comma != std::string_view::npos) pending_.name = std::string(trimWhitespaceAndNewlines(rest.substr(comma + 1)));
    auto get = [&](std::string_view key) {
      std::optional<std::string> v = attribute(attrs, key);
      return v ? std::string(trimWhitespaceAndNewlines(*v)) : std::string();
    };
    pending_.tvgId = get("tvg-id");
    pending_.tvgName = get("tvg-name");
    pending_.logo = get("tvg-logo");
    pending_.group = get("group-title");
    pending_.number = get("tvg-chno");
    if (pending_.number.empty()) pending_.number = get("channel-number");
    std::string radio = str::lowerAscii(get("radio"));
    pending_.radio = radio == "true" || radio == "1" || radio == "yes";
    if (std::optional<std::string> shift = attribute(attrs, "tvg-shift")) pending_.shift = shiftMinutes(*shift);
    // catchup, catchup-source and catchup-days are not used (no archive playback).
    return;
  }
  if (startsWithNoCase(l, "#EXTGRP:")) {
    pending_.extGroup = std::string(trimWhitespaceAndNewlines(l.substr(8)));
    return;
  }
  if (startsWithNoCase(l, "#EXTVLCOPT:")) {
    std::string_view opt = l.substr(11);
    size_t eq = opt.find('=');
    if (eq == std::string_view::npos) return;
    std::string_view key = str::trim(opt.substr(0, eq));
    std::string_view value = opt.substr(eq + 1);
    if (str::iequals(key, "http-user-agent")) setHeader(pending_.headers, "User-Agent", value);
    else if (str::iequals(key, "http-referrer") || str::iequals(key, "http-referer"))
      setHeader(pending_.headers, "Referer", value);
    else if (str::iequals(key, "http-origin")) setHeader(pending_.headers, "Origin", value);
    return;
  }
  if (startsWithNoCase(l, "#EXT-X-TARGETDURATION") || startsWithNoCase(l, "#EXT-X-STREAM-INF") ||
      startsWithNoCase(l, "#EXT-X-MEDIA-SEQUENCE")) {
    // An HLS playlist is one video, not a list of channels.
    error_ = Error::make("This link is a single video stream, not a channel playlist.");
    stopped_ = true;
    return;
  }
  if (l[0] == '#') return;
  entry(l);
}

std::string Parser::itemIdFor(const std::string& tvgId, const std::string& name, const std::string& group) {
  std::string key = tvgId + '\x1f' + name + '\x1f' + group;
  uint64_t base = str::fnv1a64(key);
  // The same (tvg-id, name, group) again: the n-th copy gets its own id. A
  // count per key keeps this linear (lists repeat names thousands of times).
  uint32_t& copies = copies_[base];
  uint64_t h = base;
  do {
    ++copies;
    h = copies == 1 ? base : str::fnv1a64(key + '\x1f' + std::to_string(copies));
  } while (!seen_.insert(h).second);
  return str::hex64(h);
}

void Parser::entry(std::string_view text) {
  Pending p = std::move(pending_);
  pending_ = Pending();
  if (!p.info) {  // a link with no #EXTINF has no name
    ++skipped_;
    return;
  }
  ++entries_;
  // Kodi style: "link|User-Agent=x&Referer=y".
  std::string_view link = text;
  size_t bar = text.find('|');
  if (bar != std::string_view::npos) {
    link = str::trim(text.substr(0, bar));
    for (std::string_view part : str::split(text.substr(bar + 1), '&')) {
      size_t eq = part.find('=');
      if (eq == std::string_view::npos) continue;
      std::string_view key = str::trim(part.substr(0, eq));
      std::string value = percentDecode(part.substr(eq + 1));
      if (str::iequals(key, "User-Agent")) setHeader(p.headers, "User-Agent", value);
      else if (str::iequals(key, "Referer") || str::iequals(key, "Referrer")) setHeader(p.headers, "Referer", value);
      else if (str::iequals(key, "Origin")) setHeader(p.headers, "Origin", value);
    }
  }
  if (!isAbsoluteLink(link)) {
    ++skipped_;
    return;
  }
  if (channels_ >= kMaxEntries) {
    truncated_ = true;
    stopped_ = true;
    return;
  }

  std::string name = !p.name.empty() ? p.name : !p.tvgName.empty() ? p.tvgName : p.tvgId;
  if (name.empty()) name = "Channel " + std::to_string(entries_);
  std::string group = !p.group.empty() ? p.group : p.extGroup;

  Channel c;
  c.itemId = itemIdFor(p.tvgId, name, group);
  c.serverId = sourceId_;
  c.id = sourceId_ + "|" + c.itemId;
  c.name = std::move(name);
  if (!p.number.empty()) c.number = p.number;
  c.imageUrl = std::move(p.logo);
  // group-title may list several groups separated by ';'.
  for (std::string_view g : str::split(group, ';')) {
    std::string_view t = trimWhitespaceAndNewlines(g);
    if (!t.empty()) c.tags.emplace_back(t);
  }
  if (!c.tags.empty()) c.group = c.tags.front();  // what LiveTVWork.annotated picks first
  if (!p.tvgId.empty()) c.epgId = p.tvgId;
  c.isRadio = p.radio;

  StreamInfo s;
  s.url = std::string(link);
  for (auto& [header, value] : p.headers) s.headers.push_back(header + ": " + value);
  s.tvgShiftMinutes = p.shift;
  ++channels_;
  if (!onChannel_(std::move(c), std::move(s))) stopped_ = true;
}

Result<Playlist> parsePlaylist(std::string accountId, std::string_view text) {
  Playlist out;
  Parser parser(std::move(accountId), [&](Channel&& c, StreamInfo&& s) {
    out.streams.emplace(c.itemId, std::move(s));
    out.channels.push_back(std::move(c));
    return true;
  });
  parser.feed(text);
  if (std::optional<Error> e = parser.finish()) return *e;
  out.header = parser.header();
  out.skipped = parser.skipped();
  return out;
}

}  // namespace marigold::sources::m3u
