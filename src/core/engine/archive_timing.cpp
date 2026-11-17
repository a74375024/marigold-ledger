#include "core/engine/archive_timing.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

#include "core/engine/source_probe.hpp"
#include "core/media/hls.hpp"
#include "core/media/ts_start.hpp"

namespace marigold::engine::archive {
namespace {

using media::tsclock::kHz;

time::Seconds floorDiv(time::Seconds a, time::Seconds b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
time::Seconds mod(time::Seconds a, time::Seconds b) { return a - floorDiv(a, b) * b; }

// The chunk of `size` minutes holding `t` (asked in `clock`): its start.
time::Seconds chunkStart(time::Seconds t, int size, int clock) {
  time::Seconds c = static_cast<time::Seconds>(size) * 60;
  return floorDiv(t + clock, c) * c - clock;
}

// The programme holding `t` (within six hours): its start.
std::optional<time::Seconds> programmeStart(const std::vector<time::Seconds>& starts, time::Seconds t) {
  std::optional<time::Seconds> best;
  for (time::Seconds s : starts)
    if (s <= t && s > t - 6 * 3600 && (!best || s > *best)) best = s;
  return best;
}

// A start measured against one predicted: the live stream's own delay can
// make the measured one a little late.
bool near(time::Seconds measured, time::Seconds predicted) {
  time::Seconds d = measured - predicted;
  return d >= -kSame && d <= 2 * kSame;
}

// Where an archive read really begins, by the live read: live's PTS is
// this far ahead of it.
time::Seconds startOf(const Read& live, const Read& a) {
  time::Seconds behind = static_cast<time::Seconds>(media::tsclock::ahead(*live.pts, *a.pts) / kHz);
  return live.asked - behind;
}

// Build 173: a start no archive clock can put that far from the moment
// asked (14 hours either way, and ten minutes) isn't the broadcast's.
bool withinClocks(time::Seconds start, time::Seconds asked) {
  constexpr time::Seconds kMost = kClockMost + 10 * 60;
  return start <= asked + kMost && start >= asked - kMost;
}

std::string minutesText(int seconds) { return std::to_string(std::abs(seconds) / 60) + " min"; }

std::string chunkWords(const Timing& t) {
  return t.kind == Kind::Programme ? std::string("starts at the programme's start")
                                   : std::to_string(t.chunkMinutes) + "-min chunks";
}

bool seeks(const Timing& t) { return t.seekable == 1 && t.byteRate > 0; }

// One stream read for its clock: the first video PTS and the bytes a
// second, and what the answer said of ranges.
struct StreamRead {
  int status = 0;
  bool refused = false, aborted = false;
  std::string why;
  std::optional<int64_t> pts;
  time::Seconds firstAt = 0;  // wall clock when the first bytes came
  uint32_t byteRate = 0;
  bool ranges = false;
};

// Build 174: what a body is, told from its first bytes together (up to
// kSniffMax), never from its first piece alone: a transfer can hand over a
// few bytes first, or a stream whose packets start further in. 1.0.16 took
// a first piece without a packet start in it for "not a stream" for good.
constexpr size_t kSniffMax = 4096;
constexpr size_t kTextMax = 2048;              // a message's text kept
constexpr size_t kPlaylistMax = 256u * 1024u;  // a playlist's text
constexpr int kHops = 3;                       // master, media playlist, segment

enum class Body { Unknown, Ts, Playlist, Text, Other };

bool printable(std::string_view head) {
  if (head.empty()) return false;
  size_t n = 0;
  for (unsigned char c : head)
    if ((c >= 0x20 && c < 0x7F) || c == '\n' || c == '\r' || c == '\t') ++n;
  return n * 10 >= head.size() * 9;
}

// Packet starts 188 bytes apart (three where that much is in), from any of
// the first bytes.
bool hasPackets(std::string_view head) {
  auto at = [&](size_t i) { return static_cast<unsigned char>(head[i]) == 0x47; };
  for (size_t i = 0; i + 188 < head.size() && i < 2048; ++i)
    if (at(i) && at(i + 188) && (i + 376 >= head.size() || at(i + 376))) return true;
  return false;
}

// `done`: the body ended (what came is all there is).
Body sniff(std::string_view head, bool done) {
  if (head.empty()) return Body::Unknown;
  if (media::hls::looksLikePlaylist(head)) return Body::Playlist;
  if (hasPackets(head)) return Body::Ts;
  if (done && static_cast<unsigned char>(head[0]) == 0x47 && head.size() < 189) return Body::Ts;
  if (printable(head) && (head.size() >= 512 || done)) return Body::Text;
  if (head.size() >= kSniffMax || done) return Body::Other;
  return Body::Unknown;
}

std::string secondsText(uint32_t ms) { return std::to_string((ms + 500) / 1000) + " s"; }

// An answer's type, for the log when it isn't a stream ("text/html");
// nothing that isn't plainly a type.
std::string typeText(const std::string& mime) {
  std::string t;
  for (char c : mime) {
    char l = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    if (l == ';' || l == ' ') break;
    if (!((l >= 'a' && l <= 'z') || (l >= '0' && l <= '9') || l == '/' || l == '.' || l == '+' || l == '-')) return {};
    t += l;
  }
  return t.size() <= 40 && t.find('/') != std::string::npos ? t : std::string();
}

// A transfer that failed without an answer, in words (from our own short
// codes, never an error text that could carry a link).
std::string connectionWhy(const std::string& error, uint32_t wait) {
  if (error == "timeout") return "no answer in " + secondsText(wait);
  if (error == "redirect") return "a redirect it couldn't follow";
  if (error == "body-cap") return "an answer too big to read";
  auto curl = [&](int code) { return error.find("(curl " + std::to_string(code) + ")") != std::string::npos; };
  if (curl(6)) return "server not found";
  if (curl(35) || curl(51) || curl(58) || curl(60) || curl(77) || curl(83) || curl(90) || curl(91))
    return "secure connection refused";
  if (curl(52)) return "not a video stream (empty)";
  if (curl(18) || curl(56)) return "the connection broke";
  return "no connection";
}

// Build 174: reads `url` the way the player gets it (its agent, a redirect
// from https to http followed, an HLS answer followed to its media: an
// archive's first segment, live's latest), for its first video PTS. `ms`:
// the whole read, the wait for its first byte included.
StreamRead readStream(http::Pool& pool, std::string url, std::vector<std::string> headers, const Clock& clock,
                      const std::atomic<bool>* abort, uint32_t ms, bool askRange, bool firstOnly = false,
                      bool archiveRead = true) {
  StreamRead out;
  headers = probe::asPlayer(std::move(headers));
  if (askRange) headers.push_back("Range: bytes=0-");
  const uint32_t t0 = clock();
  auto spentMs = [&]() -> uint32_t { return clock() - t0; };
  bool listed = false;  // a playlist was followed
  for (int hop = 0; hop < kHops; ++hop) {
    uint32_t spent = spentMs();
    if (spent >= ms) {
      out.why = (hop ? "no picture in " : "no answer in ") + secondsText(ms);
      return out;
    }
    media::tsclock::TsClock ts;
    std::string head, text;
    Body kind = Body::Unknown;
    bool cut = false;  // stopped here: the time was up
    uint64_t bytes = 0;
    auto feedTs = [&](std::string_view part) {
      if (!part.empty()) ts.feed(reinterpret_cast<const uint8_t*>(part.data()), part.size());
    };
    http::Request q;
    q.url = url;
    q.headers = headers;
    q.tag = "archive.timing";
    q.priority = http::Priority::Background;
    q.media = true;
    q.timeoutMs = ms - spent + 1000;
    q.maxBytes = 8u * 1024u * 1024u;
    q.onChunk = [&](std::string_view chunk) -> bool {
      if (abort && abort->load()) {
        out.aborted = true;
        return false;
      }
      bytes += chunk.size();
      uint32_t now = spentMs();
      if (kind == Body::Unknown) {
        size_t took = std::min(chunk.size(), kSniffMax - std::min(kSniffMax, head.size()));
        head.append(chunk.substr(0, took));
        kind = sniff(head, false);
        std::string_view rest = chunk.substr(took);
        if (kind == Body::Unknown) {
          if (now >= ms) {
            cut = true;
            return false;
          }
          return true;
        }
        if (kind == Body::Ts) {
          out.firstAt = time::now();
          feedTs(head);
          feedTs(rest);
        } else {
          text = head;
          text.append(rest.substr(0, std::min(rest.size(), kPlaylistMax - std::min(kPlaylistMax, text.size()))));
        }
      } else if (kind == Body::Ts) {
        feedTs(chunk);
      } else if (text.size() < kPlaylistMax) {
        text.append(chunk.substr(0, std::min(chunk.size(), kPlaylistMax - text.size())));
      }
      switch (kind) {
        case Body::Ts:
          if (ts.first()) {
            // Enough: a few seconds of video for its rate, or the time is up.
            // Build 173: a first replay's reads want the first picture only.
            if (firstOnly || ts.seconds() >= 3.0 || ts.bytes() >= 4u * 1024u * 1024u || now >= ms) return false;
          } else if (now >= ms) {
            cut = true;
            return false;
          }
          return true;
        case Body::Playlist:
          if (now >= ms) {
            cut = true;
            return false;
          }
          return text.size() < kPlaylistMax;
        default: return text.size() < kTextMax;
      }
    };
    http::Response r = pool.fetchNow(std::move(q));
    out.status = r.status;
    if (out.aborted || (abort && abort->load())) {
      out.aborted = true;
      return out;
    }
    bool ok2xx = r.status >= 200 && r.status < 300;
    if (kind == Body::Unknown && !head.empty()) {
      // The body ended (or was cut) before it said what it was.
      kind = sniff(head, true);
      if (kind == Body::Ts) {
        out.firstAt = time::now();
        feedTs(head);
      } else {
        text = head;
      }
    }
    out.ranges = answersRanges(r.status, r.contentRange, r.acceptRanges, r.contentLength);
    if (probe::refusal(r.status, ts.first() || bytes ? std::string_view() : std::string_view(r.error),
                       kind == Body::Text ? std::string_view(text) : std::string_view())) {
      out.refused = true;
      out.why = r.status && !ok2xx ? "http " + std::to_string(r.status) : std::string("refused");
      return out;
    }
    if (kind == Body::Ts && ts.first()) {
      out.pts = ts.first();
      out.byteRate = ts.byteRate();
      return out;
    }
    if (r.status && !ok2xx) {
      out.why = "http " + std::to_string(r.status);
      return out;
    }
    if (kind == Body::Playlist && !cut) {
      // An HLS answer: its media, as a player would play it.
      media::hls::Playlist pl = media::hls::parse(text);
      std::string next;
      if (pl.master) {
        if (const media::hls::Variant* v = media::hls::pickVariant(pl)) next = v->uri;
      } else if (!pl.segments.empty() && !pl.encrypted && !pl.fragmentedMp4) {
        next = archiveRead ? pl.segments.front().uri : pl.segments.back().uri;
      }
      if (next.empty()) {
        out.why = pl.encrypted       ? "not a video stream (encrypted playlist)"
                  : pl.fragmentedMp4 ? "not a video stream (fMP4 playlist)"
                                     : "not a video stream (empty playlist)";
        return out;
      }
      listed = true;
      url = media::hls::resolve(url, next);
      continue;
    }
    // Nothing that gives a picture: why, in words.
    if (kind == Body::Ts) {
      out.why = cut || spentMs() >= ms ? "no picture in " + secondsText(ms) : std::string("no picture");
    } else if (kind == Body::Text || kind == Body::Other) {
      std::string type = typeText(r.mime);
      if (type.empty()) type = kind == Body::Text ? "text" : "unknown data";
      out.why = "not a video stream (" + type + ")";
    } else if (kind == Body::Playlist || cut) {
      out.why = "no picture in " + secondsText(ms);
    } else if (ok2xx && r.error.empty()) {
      out.why = "not a video stream (empty)";
    } else {
      out.why = connectionWhy(r.error, ms);
    }
    if (listed && out.why.rfind("not a video", 0) != 0) out.why += " (after a playlist)";
    return out;
  }
  out.why = "not a video stream (playlists only)";
  return out;
}

}  // namespace

// ------------------------------------------------------------ classify

Timing classify(const Readings& r, std::vector<int>* ambiguous) {
  Timing t;
  t.at = r.at;
  t.seekable = r.seekable;
  t.byteRate = r.byteRate;
  std::vector<Read> reads;
  for (const Read& a : r.archive)
    if (a.pts) reads.push_back(a);
  if (reads.size() < 2) {
    t.kind = Kind::Unmeasured;
    return t;
  }
  // Rewritten: every stretch starts its clock at (about) zero.
  bool nearZero = std::all_of(reads.begin(), reads.end(), [](const Read& a) { return *a.pts < 60 * kHz; });
  if (nearZero) {
    t.kind = Kind::Rewritten;
    return t;
  }
  if (!r.live || !r.live->pts) {
    // No live to place them: only the gap between the two starts.
    double gap = static_cast<double>(media::tsclock::delta(*reads[1].pts, *reads[0].pts)) / kHz;
    double asked = static_cast<double>(reads[1].asked - reads[0].asked);
    t.kind = std::fabs(gap - asked) <= kSame ? Kind::Exact : Kind::Unmeasured;
    return t;
  }
  // Where each stretch really begins: live's PTS is this far ahead of it.
  std::vector<time::Seconds> starts;
  bool plausible = true;
  for (const Read& a : reads) {
    time::Seconds start = startOf(*r.live, a);
    // Not the broadcast's clock: further from the moment asked than any
    // archive clock can put it.
    if (!withinClocks(start, a.asked)) {
      t.kind = Kind::Rewritten;
      return t;
    }
    // Further than a recording or a chunk puts it (after the moment asked,
    // or hours before it): only another clock can explain it.
    if (start > a.asked + 10 * 60 || start < a.asked - 4 * 3600) plausible = false;
    starts.push_back(start);
  }
  // Exact: the same lag behind every moment asked.
  if (reads.empty() || starts.size() != reads.size()) return t;
  time::Seconds low = reads.front().asked - starts.front(), high = low, sum = 0;
  for (size_t i = 0; i < reads.size(); ++i) {
    time::Seconds lag = reads[i].asked - starts[i];
    low = std::min(low, lag);
    high = std::max(high, lag);
    sum += lag;
  }
  if (high - low <= kSame) {
    // Build 173: a whole number of quarter hours is the archive's clock;
    // the rest (the live stream's own few seconds aside) the recording's.
    LagParts parts = splitLag(sum / static_cast<time::Seconds>(reads.size()));
    if (!parts.usable || (!parts.clock && !plausible)) {
      t.kind = plausible ? Kind::Unmeasured : Kind::Rewritten;
      return t;
    }
    t.kind = Kind::Exact;
    t.offset = parts.offset;
    t.clock = r.clock + parts.clock;
    return t;
  }
  // Chunked: the sizes whose boundaries give every start, in the clock
  // asked in or (Build 173) one `shift` seconds east of it, whose
  // boundaries fall `shift` earlier than those of the clock asked in.
  auto chunkSizes = [&](int shift) {
    std::vector<int> fits;
    for (int size : kChunkMinutes) {
      bool all = true;
      for (size_t i = 0; i < reads.size() && all; ++i)
        all = near(starts[i], chunkStart(reads[i].asked, size, r.clock) - shift);
      if (all) fits.push_back(size);
    }
    return fits;
  };
  auto chunked = [&](const std::vector<int>& fits, int shift) {
    if (ambiguous && fits.size() > 1) *ambiguous = fits;
    t.kind = Kind::Chunked;
    t.chunkMinutes = fits.front();
    t.clock = r.clock + shift;
    return t;
  };
  if (std::vector<int> fits = chunkSizes(0); !fits.empty()) return chunked(fits, 0);
  // The programme's start.
  bool programme = !r.programmeStarts.empty();
  for (size_t i = 0; i < reads.size() && programme; ++i) {
    std::optional<time::Seconds> s = programmeStart(r.programmeStarts, reads[i].asked);
    programme = s && near(starts[i], *s);
  }
  if (programme) {
    t.kind = Kind::Programme;
    t.clock = r.clock;
    return t;
  }
  // Build 173: chunks on another clock, the nearest first.
  for (int shift = kClockStep; shift <= kClockMost; shift += kClockStep)
    for (int east : {shift, -shift})
      if (std::vector<int> fits = chunkSizes(east); !fits.empty()) return chunked(fits, east);
  t.kind = plausible ? Kind::Unmeasured : Kind::Rewritten;
  return t;
}

std::optional<Timing> classifyQuick(const Readings& r) {
  if (!r.live || !r.live->pts || r.archive.empty() || !r.archive.front().pts) return std::nullopt;
  const Read& a = r.archive.front();
  // A stretch whose clock starts near zero may be rewritten: the full
  // reads tell.
  if (*a.pts < 60 * kHz) return std::nullopt;
  time::Seconds start = startOf(*r.live, a);
  if (!withinClocks(start, a.asked)) return std::nullopt;
  time::Seconds lag = a.asked - start;
  LagParts parts = splitLag(lag);
  // Whole quarter hours only (none included), to within a minute: read 7
  // minutes past a quarter hour, no chunk starts that far before it.
  if (!parts.usable || (parts.clock == 0 && std::abs(parts.offset) > 60)) return std::nullopt;
  // A programme's start can be any minute: a stretch starting on one says
  // nothing of the clock.
  if (std::abs(lag) >= 45)
    for (time::Seconds s : r.programmeStarts)
      if (near(start, s)) return std::nullopt;
  Timing t;
  t.kind = Kind::Exact;
  t.offset = parts.offset;
  t.clock = r.clock + parts.clock;
  t.seekable = r.seekable;
  t.byteRate = r.byteRate;
  t.at = r.at;
  return t;
}

time::Seconds quickMoment(time::Seconds picked, time::Seconds now) {
  time::Seconds minute = floorDiv(picked, 60) * 60;
  // The minute 7 past a quarter hour at or before it, and the one after.
  time::Seconds before = minute - mod(minute - 7 * 60, kClockStep);
  time::Seconds after = before + kClockStep;
  time::Seconds latest = now - 10 * 60;
  time::Seconds best = (after - picked < picked - before && after <= latest) ? after : before;
  while (best > latest) best -= kClockStep;
  return best;
}

LagParts splitLag(time::Seconds lag) {
  auto minutes = [](time::Seconds s) {
    return std::abs(s) < 45 ? 0 : static_cast<int>(std::lround(static_cast<double>(s) / 60.0)) * 60;
  };
  LagParts p;
  time::Seconds q = static_cast<time::Seconds>(std::lround(static_cast<double>(lag) / kClockStep)) * kClockStep;
  if (q != 0 && std::abs(q) <= kClockMost) {
    int rest = minutes(lag - q);
    if (std::abs(rest) <= 60) {
      p.clock = static_cast<int>(q);
      p.offset = rest;
      return p;
    }
  }
  p.offset = minutes(lag);
  p.usable = std::abs(p.offset) <= 90 * 60;
  if (!p.usable) p.offset = 0;
  return p;
}

time::Seconds decidingMoment(time::Seconds start, const std::vector<int>& sizes, int clock) {
  if (sizes.size() < 2) return start;
  for (time::Seconds t = start + 60; t <= start + 2 * 3600; t += 60)
    if (chunkStart(t, sizes[0], clock) != chunkStart(t, sizes[1], clock)) return t + 2 * 60;
  return start;
}

bool answersRanges(int status, const std::string& contentRange, const std::string& acceptRanges,
                   uint64_t contentLength) {
  std::string range = contentRange, accept = acceptRanges;
  for (char& c : range) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
  for (char& c : accept) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
  if (status == 206 && range.rfind("bytes ", 0) == 0) return true;
  return status >= 200 && status < 300 && accept.find("bytes") != std::string::npos && contentLength > 0;
}

time::Seconds firstMoment(time::Seconds now) {
  time::Seconds back = now - 2 * 3600 - 15 * 60;
  time::Seconds hour = floorDiv(back, 3600) * 3600;
  time::Seconds t = hour + 23 * 60;
  return t > back ? t - 3600 : t;
}

Outcome measure(http::Pool& pool, const Request& req, const Clock& clock, const std::atomic<bool>* abort,
                uint32_t budgetMs, time::Seconds now) {
  Outcome out;
  const uint32_t t0 = clock();
  auto left = [&]() -> uint32_t {
    uint32_t spent = clock() - t0;
    return spent >= budgetMs ? 0 : budgetMs - spent;
  };
  // Each read gets its share of what's left.
  auto share = [&](int reads) -> uint32_t { return std::max<uint32_t>(300, left() / static_cast<uint32_t>(reads)); };
  Readings rd;
  rd.clock = req.clock;
  rd.programmeStarts = req.programmeStarts;
  rd.at = now;
  auto stop = [&](const StreamRead& s) {
    if (s.aborted) out.aborted = true;
    if (s.refused) {
      out.refused = true;
      out.why = s.why;
    }
    return s.aborted || s.refused;
  };
  StreamRead live;
  std::string why;
  // Build 174: the archive asked as the player asks it: without the live
  // copy's headers (Store::catchupFeed clears them for the player), with
  // the player's agent (readStream).
  const std::vector<std::string> archiveHeaders;
  auto liveWhy = [](const std::string& w) { return w + " (live)"; };
  auto archiveWhy = [](const std::string& w) { return w + " (archive)"; };
  if (req.picked) {
    // Build 173: a replay waiting: live and one stretch near its moment,
    // each to its first picture, within `quickMs`.
    const uint32_t quickMs = req.quickMs ? std::min(req.quickMs, budgetMs) : budgetMs;
    auto quickLeft = [&]() -> uint32_t {
      uint32_t spent = clock() - t0;
      return spent >= quickMs ? 0 : quickMs - spent;
    };
    live = readStream(pool, req.live, req.headers, clock, abort, std::max<uint32_t>(300, quickLeft() / 2), false, true,
                      false);
    if (stop(live)) return out;
    if (live.pts) rd.live = Read{live.firstAt ? live.firstAt : time::now(), live.pts};
    time::Seconds at = quickMoment(*req.picked, now);
    std::optional<std::string> link = req.linkFor ? req.linkFor(at) : std::nullopt;
    if (link && rd.live && quickLeft()) {
      StreamRead a =
          readStream(pool, *link, archiveHeaders, clock, abort, std::max<uint32_t>(300, quickLeft()), false, true);
      if (stop(a)) return out;
      if (!a.pts) why = archiveWhy(a.why);
      rd.archive.push_back(Read{at, a.pts});
      if (std::optional<Timing> quick = classifyQuick(rd)) {
        out.timing = quick;
        return out;
      }
    }
  } else {
    live = readStream(pool, req.live, req.headers, clock, abort, share(3), false, false, false);
    if (stop(live)) return out;
    if (live.pts) rd.live = Read{live.firstAt ? live.firstAt : time::now(), live.pts};
  }
  time::Seconds first = firstMoment(now);
  for (int i = 0; i < 2; ++i) {
    time::Seconds at = first + i * kApart;
    std::optional<std::string> link = req.linkFor ? req.linkFor(at) : std::nullopt;
    if (!link || !left()) break;
    StreamRead a = readStream(pool, *link, archiveHeaders, clock, abort, share(2 - i), i == 1);
    if (stop(a)) return out;
    if (i == 1) rd.seekable = a.status ? (a.ranges ? 1 : 0) : -1;
    rd.byteRate = std::max(rd.byteRate, a.byteRate);
    if (!a.pts && why.empty()) why = archiveWhy(a.why);
    rd.archive.push_back(Read{at, a.pts});
  }
  if (std::none_of(rd.archive.begin(), rd.archive.end(), [](const Read& a) { return a.pts.has_value(); })) {
    out.why = why.empty() ? std::string(left() ? "no archive read" : "out of time") : why;
    if (!live.pts && !live.why.empty()) out.why = liveWhy(live.why);
    return out;
  }
  std::vector<int> sizes;
  Timing t = classify(rd, &sizes);
  auto read = std::find_if(rd.archive.begin(), rd.archive.end(), [](const Read& a) { return a.pts.has_value(); });
  if (sizes.size() > 1 && left() > 300 && rd.live && t.clock == req.clock) {
    // Two sizes fit: a third moment where they differ.
    time::Seconds behind = static_cast<time::Seconds>(media::tsclock::ahead(*rd.live->pts, *read->pts) / kHz);
    time::Seconds at = decidingMoment(rd.live->asked - behind, sizes, req.clock);
    std::optional<std::string> link = req.linkFor ? req.linkFor(at) : std::nullopt;
    if (link && at < now - 30 * 60) {
      StreamRead a = readStream(pool, *link, archiveHeaders, clock, abort, share(1), false);
      if (stop(a)) return out;
      if (a.pts) {
        rd.archive.push_back(Read{at, a.pts});
        t = classify(rd, nullptr);
      }
    }
  }
  size_t told = static_cast<size_t>(
      std::count_if(rd.archive.begin(), rd.archive.end(), [](const Read& a) { return a.pts.has_value(); }));
  if (!t.clock && rd.live && told < 2) {
    // Build 174: one stretch read where the other wasn't (a server that
    // answers a byte range badly, a read cut short): what the live read and
    // the first one alone say, as for a first replay. The first moment (23
    // past an hour) is no whole number of quarter hours after any chunk's
    // start, as quickMoment's isn't.
    Readings one = rd;
    one.archive.clear();
    if (!rd.archive.empty() && rd.archive.front().pts) one.archive.push_back(rd.archive.front());
    if (std::optional<Timing> quick = classifyQuick(one)) {
      quick->seekable = rd.seekable;
      quick->byteRate = rd.byteRate;
      t = *quick;
    }
  }
  // Build 173: reads the budget cut short tell nothing yet (the copy is
  // measured again), not that its archive can't be measured.
  if (t.kind == Kind::Unmeasured && !left()) {
    out.why = "out of time";
    return out;
  }
  out.timing = t;
  // Build 174: why the reads couldn't tell the archive's clock, for the log.
  if (!t.clock) {
    out.why = t.kind == Kind::Rewritten ? std::string("timestamps rewritten, the video can't tell")
              : !live.pts && !live.why.empty() ? liveWhy(live.why)
              : !why.empty()                   ? why
                                               : std::string("the video couldn't tell");
  }
  return out;
}

// ------------------------------------------------------------------ plan

Plan plan(const std::optional<Timing>& t, time::Seconds picked, int clock, bool canSkip, bool next,
          const std::vector<time::Seconds>& programmeStarts) {
  Plan p;
  p.ask = p.base = p.shows = picked;
  if (!t) return p;
  if (t->kind == Kind::Exact) {
    p.ask = picked + t->offset;
    return p;
  }
  if (t->kind != Kind::Chunked && t->kind != Kind::Programme) return p;
  time::Seconds wanted = picked + t->offset;
  time::Seconds start = 0;
  std::optional<time::Seconds> after;
  if (t->kind == Kind::Chunked) {
    if (t->chunkMinutes <= 0) return p;
    start = chunkStart(wanted, t->chunkMinutes, clock);
    after = start + static_cast<time::Seconds>(t->chunkMinutes) * 60;
    p.chunkMinutes = t->chunkMinutes;
  } else {
    std::optional<time::Seconds> s = programmeStart(programmeStarts, wanted);
    if (!s) {
      p.ask = wanted;
      return p;
    }
    start = *s;
    for (time::Seconds x : programmeStarts)
      if (x > wanted && (!after || x < *after)) after = x;
    p.programme = true;
  }
  time::Seconds gap = wanted - start;
  p.ask = start;
  p.base = start - t->offset;
  p.shows = p.base;
  if (gap < kSame) return p;
  if (canSkip && seeks(*t)) {
    p.how = Plan::How::Seek;
    p.skip = static_cast<int>(gap);
    p.byteRate = t->byteRate;
    p.startByte = static_cast<uint64_t>(gap) * t->byteRate / 188 * 188;
    p.shows = picked;
    return p;
  }
  if (canSkip && gap <= kReadThrough) {
    p.how = Plan::How::ReadThrough;
    p.skip = static_cast<int>(gap);
    p.byteRate = t->byteRate;
    p.shows = picked;
    return p;
  }
  p.how = Plan::How::FromChunk;
  if (next && after) {
    // Following a stretch that ended: on from the next start, not back.
    p.ask = *after;
    p.base = p.shows = *after - t->offset;
  }
  return p;
}

replay::Reach reach(const std::optional<Timing>& t, int clock, bool canSkip) {
  replay::Reach r;
  if (!t || t->kind != Kind::Chunked || t->chunkMinutes <= 0) return r;
  if (canSkip && seeks(*t)) return r;
  r.chunk = static_cast<time::Seconds>(t->chunkMinutes) * 60;
  r.phase = mod(-static_cast<time::Seconds>(clock) - t->offset, r.chunk);
  r.readThrough = canSkip ? std::min(kReadThrough, r.chunk - 60) : 0;
  return r;
}

// ----------------------------------------------------------------- words

std::string describe(const Timing& t, bool canSkip) {
  std::string out = "archive: ";
  std::string offset;
  if (t.offset) offset = "recording " + minutesText(t.offset) + (t.offset > 0 ? " behind" : " ahead");
  switch (t.kind) {
    case Kind::Exact: return out + (offset.empty() ? std::string("exact") : offset);
    case Kind::Chunked:
    case Kind::Programme:
      out += chunkWords(t);
      out += canSkip && seeks(t) ? ", skips to the minute"
             : canSkip           ? ", reads through up to 4 min"
                                 : ", no skip";
      if (!offset.empty()) out += "; " + offset;
      return out;
    case Kind::Rewritten: return out + "timestamps rewritten, not measured";
    case Kind::Unmeasured: return out + "not measured";
    case Kind::Unknown: break;
  }
  return out + "not measured yet";
}

std::string planText(const Plan& p, const std::optional<Timing>& t,
                     const std::function<std::string(time::Seconds)>& hm) {
  if (!t) return "";
  if (t->kind == Kind::Exact) {
    if (!t->offset) return "";
    return "(recording " + minutesText(t->offset) + (t->offset > 0 ? " behind)" : " ahead)");
  }
  if (!p.chunkMinutes && !p.programme) return "";
  std::string where = (p.programme ? "programme start " : "chunk ") + (hm ? hm(p.base) : std::string());
  switch (p.how) {
    case Plan::How::Seek:
    case Plan::How::ReadThrough:
      return "(" + where + ", skipped " + std::to_string(std::max(1, p.skip / 60)) + " min)";
    case Plan::How::FromChunk: return "(" + where + ", no skip)";
    case Plan::How::AsAsked: break;
  }
  return "(" + where + ")";
}

std::string providerText(const std::vector<Timing>& all) {
  std::map<std::string, size_t> counts;
  std::vector<std::string> order;
  size_t n = 0;
  for (const Timing& t : all) {
    if (!t.told()) continue;
    std::string words = describe(t, true).substr(9);  // without "archive: "
    if (t.kind == Kind::Chunked || t.kind == Kind::Programme) {
      // "30-min chunks (skips to the minute)"
      size_t comma = words.find(", ");
      if (comma != std::string::npos) words = words.substr(0, comma) + " (" + words.substr(comma + 2) + ")";
    }
    if (!counts.count(words)) order.push_back(words);
    ++counts[words];
    ++n;
  }
  if (!n) return "";
  std::stable_sort(order.begin(), order.end(),
                   [&counts](const std::string& a, const std::string& b) { return counts[a] > counts[b]; });
  if (order.size() == 1) return order[0] + " on all replay channels";
  std::string out;
  for (size_t i = 0; i < order.size(); ++i) {
    long pct = std::lround(100.0 * static_cast<double>(counts[order[i]]) / static_cast<double>(n));
    if (i) out += ", ";
    out += order[i] + " " + std::to_string(pct) + "%";
  }
  return out;
}

std::string encode(const Timing& t) {
  std::string k;
  switch (t.kind) {
    case Kind::Exact: k = "e"; break;
    case Kind::Chunked: k = "c" + std::to_string(t.chunkMinutes); break;
    case Kind::Programme: k = "p"; break;
    case Kind::Rewritten: k = "r"; break;
    case Kind::Unmeasured: k = "u"; break;
    case Kind::Unknown: return "";
  }
  char tail[96];
  std::snprintf(tail, sizeof tail, ",%d,%d,%u,%lld", t.offset, t.seekable, t.byteRate, static_cast<long long>(t.at));
  return k + tail;
}

std::optional<Timing> decode(const std::string& s) {
  if (s.empty() || s.size() > 80) return std::nullopt;
  Timing t;
  char k = s[0];
  int chunk = 0, offset = 0, seekable = -1;
  unsigned rate = 0;
  long long at = 0;
  size_t comma = s.find(',');
  if (comma == std::string::npos) return std::nullopt;
  if (k == 'c') chunk = std::atoi(s.c_str() + 1);
  if (std::sscanf(s.c_str() + comma, ",%d,%d,%u,%lld", &offset, &seekable, &rate, &at) != 4) return std::nullopt;
  switch (k) {
    case 'e': t.kind = Kind::Exact; break;
    case 'c':
      if (std::find(std::begin(kChunkMinutes), std::end(kChunkMinutes), chunk) == std::end(kChunkMinutes))
        return std::nullopt;
      t.kind = Kind::Chunked;
      t.chunkMinutes = chunk;
      break;
    case 'p': t.kind = Kind::Programme; break;
    case 'r': t.kind = Kind::Rewritten; break;
    case 'u': t.kind = Kind::Unmeasured; break;
    default: return std::nullopt;
  }
  if (offset % 60 || std::abs(offset) > 90 * 60 || seekable < -1 || seekable > 1) return std::nullopt;
  // Build 173: 1.0.15 kept a clock error as an offset: measured again.
  if (splitLag(offset).clock) return std::nullopt;
  t.offset = offset;
  t.seekable = seekable;
  t.byteRate = rate;
  t.at = at;
  return t;
}

}  // namespace marigold::engine::archive
