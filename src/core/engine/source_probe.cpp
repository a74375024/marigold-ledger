#include "core/engine/source_probe.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>

#include "core/media/hls.hpp"
#include "core/media/ts_video_probe.hpp"

namespace marigold::engine::probe {
namespace {

constexpr size_t kPlaylistMax = 1024u * 1024u;  // a playlist's text
constexpr size_t kTextMax = 2048;               // an answer that isn't a stream
constexpr size_t kSniff = 376;                  // two packets decide what a body is

bool contains(std::string_view s, std::string_view part) { return s.find(part) != std::string_view::npos; }

std::string lower(std::string_view s) {
  std::string out(s);
  for (char& c : out)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return out;
}

// Mostly printable: a message, not a stream.
bool looksLikeText(std::string_view head) {
  if (head.empty()) return false;
  size_t printable = 0;
  for (unsigned char c : head)
    if ((c >= 0x20 && c < 0x7F) || c == '\n' || c == '\r' || c == '\t') ++printable;
  return printable * 10 >= head.size() * 9;
}

// Two packet starts 188 bytes apart.
bool looksLikeTs(std::string_view head) {
  for (size_t i = 0; i + 188 < head.size() && i < 188; ++i)
    if (static_cast<unsigned char>(head[i]) == 0x47 && static_cast<unsigned char>(head[i + 188]) == 0x47) return true;
  return !head.empty() && static_cast<unsigned char>(head[0]) == 0x47 && head.size() < 189;
}

std::string seconds(uint32_t ms) { return std::to_string((ms + 500) / 1000) + " s"; }

// One transfer: what came, read as it came.
struct Read {
  enum class Kind { Unknown, Ts, Playlist, Text } kind = Kind::Unknown;
  std::string head;  // the first bytes, until the kind is known
  std::string text;  // a playlist's or a message's text
  std::unique_ptr<media::TsVideoProbe> ts;
  uint32_t keyAt = 0;  // clock() at the first key picture
  bool keySeen = false;
  bool aborted = false, stopped = false, timedOut = false, scrambled = false, audioOnly = false;
  uint64_t bytes = 0;
};

}  // namespace

Clock steadyClock() {
  return [] {
    using namespace std::chrono;
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
  };
}

bool refusal(int status, std::string_view error, std::string_view body) {
  if (status == 403 || status == 429 || status == 451 || status == 458) return true;
  if (status == 0) {
    std::string e = lower(error);
    if (contains(e, "connect") || contains(e, "refused") || contains(e, "(curl 7)")) return true;
  }
  if (!body.empty() && looksLikeText(body.substr(0, std::min<size_t>(body.size(), 512)))) {
    std::string b = lower(body.substr(0, std::min<size_t>(body.size(), kTextMax)));
    if (contains(b, "max") && contains(b, "connection")) return true;
  }
  return false;
}

Outcome run(http::Pool& pool, const Request& req, const Clock& clock, const std::atomic<bool>* abort,
            uint32_t timeoutMs) {
  Outcome out;
  const uint32_t t0 = clock();
  auto since = [&](uint32_t t) { return t - t0; };
  std::string url = req.url;
  // What a master playlist said of the variant followed (a segment that
  // can't be read for its picture still has these).
  int listedW = 0, listedH = 0;
  double listedRate = 0;
  for (int hop = 0; hop < kMaxHops; ++hop) {
    uint32_t now = clock();
    if (since(now) >= timeoutMs) {
      out.why = "no picture in " + seconds(timeoutMs);
      return out;
    }
    Read rd;
    http::Request q;
    q.url = url;
    q.headers = req.headers;
    q.tag = "test.probe";
    q.priority = http::Priority::Background;
    q.timeoutMs = timeoutMs - since(now) + 1000;
    q.maxBytes = kMaxBytes;
    q.onChunk = [&](std::string_view chunk) -> bool {
      if (abort && abort->load()) {
        rd.aborted = true;
        return false;
      }
      uint32_t t = clock();
      if (!out.firstByteMs) out.firstByteMs = std::max<uint32_t>(1, since(t));
      rd.bytes += chunk.size();
      if (rd.kind == Read::Kind::Unknown) {
        rd.head.append(chunk.substr(0, std::min(chunk.size(), kSniff - std::min(kSniff, rd.head.size()))));
        if (media::hls::looksLikePlaylist(rd.head)) {
          rd.kind = Read::Kind::Playlist;
        } else if (looksLikeTs(rd.head)) {
          rd.kind = Read::Kind::Ts;
        } else if (rd.head.size() >= kSniff) {
          rd.kind = looksLikeText(rd.head) ? Read::Kind::Text : Read::Kind::Ts;
        } else {
          // Not enough to tell: kept, and fed once it's known.
          if (since(t) >= timeoutMs) {
            rd.timedOut = true;
            return false;
          }
          return true;
        }
        // Every byte before this piece is in the head (the kind wasn't
        // known), and the head's last part is from this piece.
        size_t before = static_cast<size_t>(rd.bytes - chunk.size());
        size_t fromThis = rd.head.size() > before ? rd.head.size() - before : 0;
        std::string_view rest = fromThis < chunk.size() ? chunk.substr(fromThis) : std::string_view();
        if (rd.kind == Read::Kind::Ts) {
          rd.ts = std::make_unique<media::TsVideoProbe>();
          rd.ts->feed(reinterpret_cast<const uint8_t*>(rd.head.data()), rd.head.size());
          if (!rest.empty()) rd.ts->feed(reinterpret_cast<const uint8_t*>(rest.data()), rest.size());
        } else {
          rd.text = rd.head;
          rd.text.append(rest);
        }
      } else if (rd.kind == Read::Kind::Ts) {
        rd.ts->feed(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size());
      } else if (rd.kind == Read::Kind::Playlist || rd.kind == Read::Kind::Text) {
        rd.text.append(chunk);
      }
      switch (rd.kind) {
        case Read::Kind::Ts: {
          const media::TsVideoProbe& p = *rd.ts;
          if (!rd.keySeen && p.intraPictures() > 0) {
            rd.keySeen = true;
            rd.keyAt = t;
          }
          media::Verdict v = p.verdict();
          if (v == media::Verdict::Scrambled) {
            rd.scrambled = true;
            rd.stopped = true;
            return false;
          }
          if (rd.keySeen) {
            // Enough: the size and the frame rate, or the rate isn't coming.
            if ((p.facts().header && p.facts().frameRate > 0) || t - rd.keyAt >= kRateWaitMs) {
              rd.stopped = true;
              return false;
            }
          } else if (v == media::Verdict::NoVideo && since(t) >= 3000) {
            // A radio channel: sound and no video. It plays.
            rd.audioOnly = true;
            rd.keyAt = t;
            rd.stopped = true;
            return false;
          }
          if (since(t) >= timeoutMs) {
            rd.timedOut = !rd.keySeen;
            rd.stopped = true;
            return false;
          }
          return true;
        }
        case Read::Kind::Playlist:
          if (since(t) >= timeoutMs) {
            rd.timedOut = true;
            return false;
          }
          return rd.text.size() < kPlaylistMax;
        case Read::Kind::Text: return rd.text.size() < kTextMax;
        default: return true;
      }
    };
    http::Response r = pool.fetchNow(std::move(q));
    if (rd.aborted) {
      out.aborted = true;
      return out;
    }
    // Headers only, or a short body that never said what it was.
    if (rd.kind == Read::Kind::Unknown && !rd.head.empty()) {
      if (media::hls::looksLikePlaylist(rd.head)) rd.kind = Read::Kind::Playlist;
      else if (looksLikeTs(rd.head)) rd.kind = Read::Kind::Ts;
      else rd.kind = Read::Kind::Text;
      rd.text = rd.head;
      if (rd.kind == Read::Kind::Ts) {
        rd.ts = std::make_unique<media::TsVideoProbe>();
        rd.ts->feed(reinterpret_cast<const uint8_t*>(rd.head.data()), rd.head.size());
        if (rd.ts->intraPictures() > 0) {
          rd.keySeen = true;
          rd.keyAt = clock();
        }
      }
    }
    if (refusal(r.status, rd.stopped ? std::string_view() : std::string_view(r.error),
                rd.kind == Read::Kind::Text ? std::string_view(rd.text) : std::string_view())) {
      out.refused = true;
      out.why = r.status ? "http " + std::to_string(r.status) : std::string("refused");
      if (rd.kind == Read::Kind::Text && r.status >= 200 && r.status < 300) out.why = "connections used up";
      return out;
    }
    bool httpError = r.status != 0 && (r.status < 200 || r.status >= 300);
    if (rd.kind == Read::Kind::Ts && !httpError) {
      const media::TsVideoProbe& p = *rd.ts;
      if (!rd.keySeen && p.intraPictures() > 0) {
        rd.keySeen = true;
        rd.keyAt = clock();
      }
      if (rd.keySeen || rd.audioOnly) {
        out.ok = true;
        out.pictureMs = std::max<uint32_t>(1, since(rd.keyAt));
        const media::VideoFacts& f = p.facts();
        out.width = f.width > 0 ? f.width : listedW;
        out.height = f.height > 0 ? f.height : listedH;
        double rate = f.frameRate > 0 ? f.frameRate : listedRate;
        out.fps = rate > 0 ? static_cast<int>(std::lround(rate)) : 0;
        return out;
      }
      if (rd.scrambled) out.why = "scrambled";
      else if (rd.timedOut || since(clock()) >= timeoutMs) out.why = "no picture in " + seconds(timeoutMs);
      else if (p.verdict() == media::Verdict::TablesOnly) out.why = "no picture or sound";
      else if (!r.error.empty() && !rd.stopped) out.why = r.error;
      else out.why = "no picture";
      return out;
    }
    if (rd.kind == Read::Kind::Playlist && !httpError) {
      out.hls = true;
      media::hls::Playlist pl = media::hls::parse(rd.text);
      if (pl.master) {
        const media::hls::Variant* v = media::hls::pickVariant(pl);
        if (!v) {
          out.why = "empty playlist";
          return out;
        }
        listedW = v->width;
        listedH = v->height;
        listedRate = v->frameRate;
        url = media::hls::resolve(url, v->uri);
        continue;
      }
      const media::hls::Segment* s = media::hls::pickSegment(pl);
      if (!s) {
        out.why = rd.timedOut ? "no picture in " + seconds(timeoutMs) : std::string("empty playlist");
        return out;
      }
      url = media::hls::resolve(url, s->uri);
      if (pl.fragmentedMp4 || pl.encrypted) {
        // Fetched for its bytes only (one more hop, read below).
        uint32_t at = clock();
        if (since(at) >= timeoutMs) {
          out.why = "no picture in " + seconds(timeoutMs);
          return out;
        }
        http::Request seg;
        seg.url = url;
        seg.headers = req.headers;
        seg.tag = "test.probe";
        seg.priority = http::Priority::Background;
        seg.timeoutMs = timeoutMs - since(at) + 1000;
        seg.maxBytes = kMaxBytes;
        uint64_t got = 0;
        bool stoppedSeg = false;
        seg.onChunk = [&](std::string_view chunk) {
          if (abort && abort->load()) {
            stoppedSeg = true;
            return false;
          }
          got += chunk.size();
          return got < 512u * 1024u;
        };
        http::Response sr = pool.fetchNow(std::move(seg));
        if (stoppedSeg && abort && abort->load()) {
          out.aborted = true;
          return out;
        }
        if (refusal(sr.status, got ? std::string_view() : std::string_view(sr.error), {})) {
          out.refused = true;
          out.why = sr.status ? "http " + std::to_string(sr.status) : std::string("refused");
          return out;
        }
        out.ok = got > 0 && (sr.status == 0 || (sr.status >= 200 && sr.status < 300));
        out.pictureMs = std::max<uint32_t>(1, since(clock()));
        out.width = listedW;
        out.height = listedH;
        out.fps = listedRate > 0 ? static_cast<int>(std::lround(listedRate)) : 0;
        if (!out.ok) out.why = sr.status ? "http " + std::to_string(sr.status) : std::string("no segment");
        return out;
      }
      continue;
    }
    // Nothing that plays.
    if (httpError) out.why = "http " + std::to_string(r.status);
    else if (!r.error.empty()) out.why = r.error;
    else if (rd.kind == Read::Kind::Text) out.why = "not a stream";
    else out.why = "nothing came";
    return out;
  }
  out.why = "too many playlists";
  return out;
}

}  // namespace marigold::engine::probe
