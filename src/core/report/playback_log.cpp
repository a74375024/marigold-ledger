#include "core/report/playback_log.hpp"

#include <cstdlib>
#include <utility>

#include "core/report/csv.hpp"
#include "core/util/json.hpp"
#include "core/util/redact.hpp"
#include "core/util/rx.hpp"

namespace marigold::report {
namespace {

// At most `cap` bytes of `s`, never splitting a UTF-8 sequence.
std::string utf8Prefix(std::string s, size_t cap) {
  if (s.size() <= cap) return s;
  size_t end = cap;
  while (end > 0 && (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80) --end;
  s.resize(end);
  return s;
}

// Swift add(): "[a-z][a-z0-9+.-]*://\S+" -> "<link>" (case-insensitive),
// then the app-wide redact() for bare credential parameters, then the cap.
std::string scrub(std::string_view text) {
  if (text.empty()) return {};
  std::string safe = rx::replace(text, "[a-z][a-z0-9+.-]*://\\S+", "<link>", true);
  return utf8Prefix(redact(safe), PlaybackLog::kFieldCap);
}

// `v` with leading zeros to `width` digits (v >= 0).
std::string padded(int64_t v, size_t width) {
  std::string s = std::to_string(v);
  if (s.size() < width) s.insert(0, width - s.size(), '0');
  return s;
}

}  // namespace

std::string isoTimeMs(int64_t atMs, const time::Zone& zone) {
  // Floor division, so times before 1970 still read correctly.
  int64_t secs = atMs / 1000;
  int64_t ms = atMs % 1000;
  if (ms < 0) {
    ms += 1000;
    secs -= 1;
  }
  int offset = zone.offsetAt(secs);
  time::Civil c = time::civilFromSeconds(secs + offset);
  std::string out = padded(c.year, 4) + "-" + padded(c.month, 2) + "-" + padded(c.day, 2) + "T" +
                    padded(c.hour, 2) + ":" + padded(c.minute, 2) + ":" + padded(c.second, 2) +
                    "." + padded(ms, 3);
  if (offset == 0) return out + "Z";
  int a = std::abs(offset) / 60;
  out += offset < 0 ? '-' : '+';
  out += padded(a / 60, 2) + ":" + padded(a % 60, 2);
  return out;
}

int PlaybackLog::newSession() {
  ++lastSession_;
  ++revision_;
  return lastSession_;
}

void PlaybackLog::push(Entry entry) {
  entries_.push_back(std::move(entry));
  while (entries_.size() > kCap) entries_.pop_front();
  ++revision_;
}

void PlaybackLog::add(int64_t atMs, std::string_view event, int session, std::string_view channel,
                      std::string_view feed, std::string_view provider, std::string_view detail) {
  Entry e;
  e.atMs = atMs;
  e.session = session;
  e.event = scrub(event);
  e.channel = scrub(channel);
  e.feed = scrub(feed);
  e.provider = scrub(provider);
  e.detail = scrub(detail);
  push(std::move(e));
}

void PlaybackLog::add(int64_t atMs, const playback::Action& action, int session,
                      std::string_view channel, std::string_view feed, std::string_view provider) {
  if (action.kind != playback::Action::Kind::Log) return;
  add(atMs, action.text, session, channel, feed, provider, action.detail);
}

void PlaybackLog::addLoad(int64_t atMs, std::string_view event, std::string_view detail) {
  add(atMs, "load: " + std::string(event), 0, {}, {}, {}, detail);
}

void PlaybackLog::clear() {
  entries_.clear();
  ++revision_;
}

std::string PlaybackLog::csv(const time::Zone& zone) const {
  std::string out = "time,session,event,channel,feed,provider,detail\n";
  for (const Entry& e : entries_) {
    out += csvCell(isoTimeMs(e.atMs, zone)) + "," + csvCell(std::to_string(e.session)) + "," +
           csvCell(e.event) + "," + csvCell(e.channel) + "," + csvCell(e.feed) + "," +
           csvCell(e.provider) + "," + csvCell(e.detail) + "\n";
  }
  return out;
}

std::string PlaybackLog::text(const time::Zone& zone) const {
  std::string out;
  for (const Entry& e : entries_) {
    std::string line = isoTimeMs(e.atMs, zone) + "  #" + std::to_string(e.session) + "  " + e.event;
    if (!e.channel.empty() || !e.feed.empty()) {
      line += "  " + e.channel;
      if (!e.feed.empty() && e.feed != e.channel) line += (e.channel.empty() ? "" : " / ") + e.feed;
      if (!e.provider.empty()) line += " (" + e.provider + ")";
    }
    if (!e.detail.empty()) line += "  " + e.detail;
    // One event per line, whatever the fields held.
    for (char& c : line)
      if (c == '\n' || c == '\r') c = ' ';
    out += line + "\n";
  }
  return out;
}

std::string PlaybackLog::toJson() const {
  json::Value list = json::Value::array();
  for (const Entry& e : entries_) {
    json::Value o = json::Value::object();
    o.set("at", e.atMs);
    o.set("session", e.session);
    o.set("event", e.event);
    o.set("channel", e.channel);
    o.set("feed", e.feed);
    o.set("provider", e.provider);
    o.set("detail", e.detail);
    list.push(std::move(o));
  }
  return json::write(list);
}

bool PlaybackLog::restore(std::string_view text) {
  std::optional<json::Value> doc = json::parse(text);
  if (!doc || !doc->isArray()) return false;
  std::deque<Entry> restored;
  const json::Value::Array& items = doc->items();
  // Only the newest kCap can be kept.
  size_t first = items.size() > kCap ? items.size() - kCap : 0;
  for (size_t i = first; i < items.size(); ++i) {
    const json::Value& v = items[i];
    if (!v.isObject() || !v["at"].isNumber()) continue;
    Entry e;
    e.atMs = v["at"].asInt();
    e.session = static_cast<int>(v["session"].asInt());
    e.event = scrub(v["event"].asString());
    e.channel = scrub(v["channel"].asString());
    e.feed = scrub(v["feed"].asString());
    e.provider = scrub(v["provider"].asString());
    e.detail = scrub(v["detail"].asString());
    restored.push_back(std::move(e));
  }
  entries_ = std::move(restored);
  // Swift: lastSession = saved.last?.session ?? 0.
  lastSession_ = entries_.empty() ? 0 : entries_.back().session;
  ++revision_;
  return true;
}

}  // namespace marigold::report
