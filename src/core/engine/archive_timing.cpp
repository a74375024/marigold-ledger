#include "core/engine/archive_timing.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

#include "core/engine/source_probe.hpp"
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

StreamRead readStream(http::Pool& pool, const std::string& url, std::vector<std::string> headers,
                      const Clock& clock, const std::atomic<bool>* abort, uint32_t ms, bool askRange) {
  StreamRead out;
  if (askRange) headers.push_back("Range: bytes=0-");
  media::tsclock::TsClock ts;
  std::string text;  // an answer that isn't a stream
  bool stream = false, sniffed = false;
  const uint32_t t0 = clock();
  http::Request q;
  q.url = url;
  q.headers = std::move(headers);
  q.tag = "archive.timing";
  q.priority = http::Priority::Background;
  q.timeoutMs = ms + 1000;
  q.maxBytes = 8u * 1024u * 1024u;
  q.onChunk = [&](std::string_view chunk) -> bool {
    if (abort && abort->load()) {
      out.aborted = true;
      return false;
    }
    if (!out.firstAt) out.firstAt = time::now();
    if (!sniffed) {
      sniffed = true;
      stream = !chunk.empty() && static_cast<unsigned char>(chunk[0]) == 0x47;
      if (!stream) {
        // Not at a packet: a stream whose packets start further in, or
        // a message.
        size_t sync = chunk.find('\x47');
        stream = sync != std::string_view::npos && sync + 188 < chunk.size() &&
                 static_cast<unsigned char>(chunk[sync + 188]) == 0x47;
      }
    }
    if (!stream) {
      text.append(chunk.substr(0, std::min<size_t>(chunk.size(), 2048 - std::min<size_t>(2048, text.size()))));
      return text.size() < 2048;
    }
    ts.feed(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size());
    uint32_t spent = clock() - t0;
    if (ts.first()) {
      // Enough: a few seconds of video for its rate, or the time is up.
      if (ts.seconds() >= 3.0 || ts.bytes() >= 4u * 1024u * 1024u || spent >= ms) return false;
    } else if (spent >= ms) {
      return false;
    }
    return true;
  };
  http::Response r = pool.fetchNow(std::move(q));
  out.status = r.status;
  if (out.aborted || (abort && abort->load())) {
    out.aborted = true;
    return out;
  }
  out.ranges = answersRanges(r.status, r.contentRange, r.acceptRanges, r.contentLength);
  if (probe::refusal(r.status, ts.first() ? std::string_view() : std::string_view(r.error), text)) {
    out.refused = true;
    out.why = r.status ? "http " + std::to_string(r.status) : std::string("refused");
    return out;
  }
  out.pts = ts.first();
  out.byteRate = ts.byteRate();
  if (!out.pts) {
    if (r.status && (r.status < 200 || r.status >= 300)) out.why = "http " + std::to_string(r.status);
    else if (!text.empty()) out.why = "not a stream";
    else if (!r.error.empty() && r.error != "cancelled") out.why = r.error;
    else out.why = "no picture";
  }
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
  for (const Read& a : reads) {
    time::Seconds behind = static_cast<time::Seconds>(media::tsclock::ahead(*r.live->pts, *a.pts) / kHz);
    time::Seconds start = r.live->asked - behind;
    // Not the broadcast's clock: a start after the moment asked, or hours
    // before it.
    if (start > a.asked + 10 * 60 || start < a.asked - 4 * 3600) {
      t.kind = Kind::Rewritten;
      return t;
    }
    starts.push_back(start);
  }
  // Exact: the same lag behind every moment asked.
  time::Seconds low = reads[0].asked - starts[0], high = low, sum = 0;
  for (size_t i = 0; i < reads.size(); ++i) {
    time::Seconds lag = reads[i].asked - starts[i];
    low = std::min(low, lag);
    high = std::max(high, lag);
    sum += lag;
  }
  if (high - low <= kSame) {
    time::Seconds lag = sum / static_cast<time::Seconds>(reads.size());
    // The live stream's own delay is a few seconds: under 45 s is none.
    t.offset = std::abs(lag) < 45 ? 0 : static_cast<int>(std::lround(static_cast<double>(lag) / 60.0)) * 60;
    if (std::abs(t.offset) > 90 * 60) {
      t.kind = Kind::Unmeasured;
      t.offset = 0;
      return t;
    }
    t.kind = Kind::Exact;
    return t;
  }
  // Chunked: the sizes whose boundaries give every start.
  std::vector<int> fits;
  for (int size : kChunkMinutes) {
    bool all = true;
    for (size_t i = 0; i < reads.size() && all; ++i) all = near(starts[i], chunkStart(reads[i].asked, size, r.clock));
    if (all) fits.push_back(size);
  }
  if (!fits.empty()) {
    if (ambiguous && fits.size() > 1) *ambiguous = fits;
    t.kind = Kind::Chunked;
    t.chunkMinutes = fits.front();
    return t;
  }
  // The programme's start.
  bool programme = !r.programmeStarts.empty();
  for (size_t i = 0; i < reads.size() && programme; ++i) {
    std::optional<time::Seconds> s = programmeStart(r.programmeStarts, reads[i].asked);
    programme = s && near(starts[i], *s);
  }
  t.kind = programme ? Kind::Programme : Kind::Unmeasured;
  return t;
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
  StreamRead live = readStream(pool, req.live, req.headers, clock, abort, share(3), false);
  if (stop(live)) return out;
  if (live.pts) rd.live = Read{live.firstAt ? live.firstAt : time::now(), live.pts};
  time::Seconds first = firstMoment(now);
  std::string why;
  for (int i = 0; i < 2; ++i) {
    time::Seconds at = first + i * kApart;
    std::optional<std::string> link = req.linkFor ? req.linkFor(at) : std::nullopt;
    if (!link || !left()) break;
    StreamRead a = readStream(pool, *link, req.headers, clock, abort, share(2 - i), i == 1);
    if (stop(a)) return out;
    if (i == 1) rd.seekable = a.status ? (a.ranges ? 1 : 0) : -1;
    rd.byteRate = std::max(rd.byteRate, a.byteRate);
    if (!a.pts && why.empty()) why = a.why;
    rd.archive.push_back(Read{at, a.pts});
  }
  if (std::none_of(rd.archive.begin(), rd.archive.end(), [](const Read& a) { return a.pts.has_value(); })) {
    out.why = why.empty() ? std::string("no archive read") : why;
    if (!live.pts && !live.why.empty()) out.why = live.why;
    return out;
  }
  std::vector<int> sizes;
  Timing t = classify(rd, &sizes);
  if (sizes.size() > 1 && left() > 300 && rd.live && rd.archive[0].pts) {
    // Two sizes fit: a third moment where they differ.
    time::Seconds behind = static_cast<time::Seconds>(media::tsclock::ahead(*rd.live->pts, *rd.archive[0].pts) / kHz);
    time::Seconds at = decidingMoment(rd.live->asked - behind, sizes, req.clock);
    std::optional<std::string> link = req.linkFor ? req.linkFor(at) : std::nullopt;
    if (link && at < now - 30 * 60) {
      StreamRead a = readStream(pool, *link, req.headers, clock, abort, share(1), false);
      if (stop(a)) return out;
      if (a.pts) {
        rd.archive.push_back(Read{at, a.pts});
        t = classify(rd, nullptr);
      }
    }
  }
  out.timing = t;
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
  t.offset = offset;
  t.seekable = seekable;
  t.byteRate = rate;
  t.at = at;
  return t;
}

}  // namespace marigold::engine::archive
