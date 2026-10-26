// A made-up IPTV provider for tests, like the tvOS scripts/mock-xtream.py:
// Xtream get_live_categories / get_live_streams JSON and an M3U playlist for
// any number of channels, shaped like a big real list (US networks in
// several versions, West feeds, locals, numbered event slots, 24/7 loops,
// separator rows, radio, other countries). Every name is generated here and
// every link is on example.invalid; the login is the fake "u" / "p".
//
// Channels are generated on demand from their index, so a 60,000-channel
// body can be produced in chunks without ever being held whole.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mock {

constexpr std::string_view kHost = "http://mock.example.invalid:8080";
constexpr std::string_view kUser = "u";
constexpr std::string_view kPass = "p";

struct Category {
  std::string id;
  std::string name;
};

struct MockChannel {
  size_t index = 0;
  std::string streamId;      // "100000" + index
  std::string name;          // empty for the deliberately broken rows
  size_t category = 0;       // index into categories()
  std::string epgId;
  std::string logo;
  bool radio = false;
  bool usable = true;        // false: no name, a client must skip it
};

// Small deterministic hash so each index gets stable variety.
inline uint32_t mix(uint64_t x) {
  x ^= x >> 33;
  x *= 0xff51afd7ed558ccdull;
  x ^= x >> 33;
  x *= 0xc4ceb9fe1a85ec53ull;
  x ^= x >> 33;
  return static_cast<uint32_t>(x);
}

class Provider {
 public:
  explicit Provider(size_t channels) : count_(channels) {
    const char* names[] = {
        "US| NEWS",        "US| SPORTS",      "US| ENTERTAINMENT", "US| MOVIES",     "US| KIDS",
        "US| LOCALS",      "US| NCAAF PPV",   "US| NBA PPV",       "US| ESPN+ PPV",  "US| 24/7",
        "US| ####",        "UK| SPORTS",      "UK| GENERAL",       "MX| DEPORTES",   "MX| GENERAL",
        "FR| GENERAL",     "DE| GENERAL",     "PT| GENERAL",       "RADIO",          "  ES| GENERAL  ",
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i)
      categories_.push_back(Category{std::to_string(i + 1), names[i]});
  }

  size_t size() const { return count_; }
  const std::vector<Category>& categories() const { return categories_; }

  MockChannel channel(size_t i) const {
    static const char* const kUS[] = {"ESPN", "ESPN 2", "FS1", "FOX NEWS", "CNN", "MSNBC", "TNT", "TBS", "HBO",
                                      "SHOWTIME", "NFL NETWORK", "NBA TV", "DISNEY CHANNEL", "NICKELODEON",
                                      "A&E", "HGTV", "FOOD NETWORK", "GOLF CHANNEL"};
    static const char* const kUSCat[] = {"2", "2", "2", "1", "1", "1", "3", "3", "4", "4", "2", "2", "5", "5",
                                         "3", "3", "3", "2"};
    static const char* const kQuality[] = {"FHD", "HD", "4K", "SD", "HD", "FHD"};
    static const char* const kCities[] = {"NEW YORK", "LOS ANGELES", "CHICAGO", "DALLAS", "SEATTLE", "MIAMI"};
    static const char* const kNets[] = {"ABC", "CBS", "NBC", "FOX"};
    static const char* const kTeams[] = {"Hawks", "Bears", "Lions", "Tigers", "Sharks", "Wolves", "Eagles", "Kings"};
    static const char* const kUK[] = {"Sky Sports Main Event", "Sky Sports Premier League", "BBC One", "ITV 1",
                                      "Channel 4", "TNT Sports 1"};
    static const char* const kMX[] = {"Canal 5", "Las Estrellas", "TUDN", "Azteca 7", "ESPN M\xC3\xA9xico"};
    static const char* const kOther[] = {"FR: TF1", "DE| Das Erste", "[PT] RTP 1", "FR: France 2", "DE| ZDF",
                                         "[PT] SIC Not\xC3\xAD" "cias"};
    static const char* const kOtherCat[] = {"16", "17", "18", "16", "17", "18"};
    static const char* const kRadio[] = {"Jazz FM", "Classic Rock 101", "News Talk 880", "R\xC3\xA1" "dio Cl\xC3\xA1ssica"};
    static const char* const kSeparators[] = {"##### SPORTS #####", "#### NEWS ####", "===== MOVIES =====",
                                              "##### US LOCALS #####"};

    MockChannel c;
    c.index = i;
    c.streamId = std::to_string(100000 + i);
    uint32_t h = mix(i + 1);
    uint32_t band = static_cast<uint32_t>(i % 100);
    auto pick = [&](size_t n, int salt) { return (mix(i * 31 + static_cast<uint64_t>(salt)) % n); };
    std::string catId = "1";
    if (band < 30) {
      size_t k = pick(18, 1);
      std::string net = kUS[k];
      catId = kUSCat[k];
      std::string q = kQuality[pick(6, 2)];
      switch (h % 5) {
        case 0: c.name = "US: " + net + " " + q; break;
        case 1: c.name = "US: " + net + " (West) " + q; break;
        case 2: c.name = "US| " + net + " " + q + " (Backup)"; break;
        case 3: {
          size_t city = pick(6, 3), n = pick(4, 4);
          c.name = "US: " + std::string(kNets[n]) + " " + std::to_string(2 + pick(48, 5)) + " " + kCities[city] + " HD";
          catId = "6";
          break;
        }
        default: c.name = "US: " + net; break;
      }
      std::string id;
      for (char ch : net)
        if (ch != ' ' && ch != '&') id += static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
      c.epgId = h % 3 == 0 ? std::string() : id + ".us";
    } else if (band < 40) {
      c.name = "UK| " + std::string(kUK[pick(6, 6)]) + (h % 2 ? " FHD" : " HD");
      catId = c.name.find("Sky") != std::string::npos || c.name.find("TNT") != std::string::npos ? "12" : "13";
    } else if (band < 48) {
      c.name = "(MX) " + std::string(kMX[pick(5, 7)]) + (h % 2 ? " HD" : "");
      catId = h % 2 ? "14" : "15";
    } else if (band < 62) {
      // Numbered event slots, mostly with a game in them.
      size_t a = pick(8, 8), b = (a + 1 + pick(7, 9)) % 8;
      std::string slot = std::to_string(1 + i % 40);
      if (slot.size() < 2) slot = "0" + slot;
      switch (h % 4) {
        case 0:
          c.name = "NCAAF " + slot + ": " + kTeams[a] + " vs " + kTeams[b] + " 7:30 PM ET";
          catId = "7";
          break;
        case 1:
          c.name = "NBA " + slot + ": " + kTeams[a] + " @ " + kTeams[b] + " | Fri 23 Oct 20:00 EDT (US)";
          catId = "8";
          break;
        case 2:
          c.name = "ESPN+ 0" + slot + ": NO EVENT STREAMING";
          catId = "9";
          break;
        default:
          c.name = "NCAAF " + slot + ": Team A vs Team B 7:30 PM ET";
          catId = "7";
          break;
      }
    } else if (band < 72) {
      c.name = "24/7: Classic Show " + std::to_string(1 + i % 900);
      catId = "10";
    } else if (band < 75) {
      c.name = kSeparators[pick(4, 10)];
      catId = "11";
    } else if (band < 80) {
      c.name = "RADIO: " + std::string(kRadio[pick(4, 11)]);
      catId = "19";
      c.radio = true;
    } else if (band < 98) {
      size_t k = pick(6, 12);
      c.name = std::string(kOther[k]) + (h % 3 == 0 ? " FHD" : " HD");
      catId = kOtherCat[k];
      if (h % 7 == 0) {
        c.name = "ES: La 1 Andaluc\xC3\xAD" "a \"Directo\" HD";  // quotes and accents
        catId = "20";
      }
    } else {
      c.name = "  NCAAF 16: Team A vs Team B 7:30 PM ET  ";  // padded, trimmed by the client
      catId = "7";
    }
    // Every 1000th row is broken (no name), as real panels sometimes send.
    if (i % 1000 == 999) {
      c.name.clear();
      c.usable = false;
    }
    c.category = static_cast<size_t>(std::stoul(catId)) - 1;
    if (h % 4 != 0) c.logo = "http://logos.example.invalid/" + std::to_string(h % 5000) + ".png";
    return c;
  }

  // get_live_categories, ids as strings (some panels send numbers: every
  // third one here is a number).
  std::string categoriesJson() const {
    std::string out = "[";
    for (size_t i = 0; i < categories_.size(); ++i) {
      if (i) out += ",";
      const Category& c = categories_[i];
      out += "{\"category_id\":";
      out += i % 3 == 2 ? c.id : "\"" + c.id + "\"";
      out += ",\"category_name\":\"" + escape(c.name) + "\",\"parent_id\":0}";
    }
    return out + "]";
  }

  // One get_live_streams element. Numbers arrive as numbers or as strings,
  // names sometimes with \u escapes, like different panels send them.
  std::string streamJson(size_t i) const {
    MockChannel c = channel(i);
    uint32_t h = mix(i * 7 + 3);
    std::string out = "{\"num\":";
    out += h % 2 ? std::to_string(i + 1) : "\"" + std::to_string(i + 1) + "\"";
    out += ",\"name\":\"" + (h % 5 == 0 ? escapeUnicode(c.name) : escape(c.name)) + "\"";
    out += ",\"stream_type\":\"";
    out += c.radio ? "radio_streams" : "live";
    out += "\",\"stream_id\":";
    out += h % 3 ? c.streamId : "\"" + c.streamId + "\"";
    out += ",\"stream_icon\":\"" + escape(c.logo) + "\"";
    out += ",\"epg_channel_id\":";
    out += c.epgId.empty() ? (h % 2 ? "null" : "\"\"") : "\"" + c.epgId + "\"";
    out += ",\"added\":\"1700000000\",\"category_id\":";
    const std::string& cat = categories_[c.category].id;
    out += h % 4 == 1 ? cat : "\"" + cat + "\"";
    out += ",\"custom_sid\":\"\",\"tv_archive\":0,\"direct_source\":\"\",\"tv_archive_duration\":0}";
    return out;
  }

  std::string liveStreamsJson() const {
    std::string out;
    liveStreamsChunks(1u << 16, [&](std::string_view part) { out.append(part.data(), part.size()); });
    return out;
  }

  // Calls emit(chunk) with pieces of exactly `chunkSize` bytes (the last
  // may be shorter), cut wherever the size falls.
  template <class Emit>
  void liveStreamsChunks(size_t chunkSize, Emit&& emit) const {
    Chunker<Emit> out(chunkSize, emit);
    out.add("[");
    for (size_t i = 0; i < count_; ++i) {
      if (i) out.add(",");
      out.add(streamJson(i));
    }
    out.add("]");
    out.flush();
  }

  // The same channels as an M3U playlist. `crlf` picks Windows line ends.
  std::string m3u(bool crlf = false) const {
    std::string out;
    m3uChunks(1u << 16, crlf, [&](std::string_view part) { out.append(part.data(), part.size()); });
    return out;
  }

  template <class Emit>
  void m3uChunks(size_t chunkSize, bool crlf, Emit&& emit) const {
    Chunker<Emit> out(chunkSize, emit);
    const char* nl = crlf ? "\r\n" : "\n";
    out.add("#EXTM3U url-tvg=\"" + std::string(kHost) + "/xmltv.php?username=" + std::string(kUser) +
            "&password=" + std::string(kPass) + "\" tvg-shift=\"0\"");
    out.add(nl);
    for (size_t i = 0; i < count_; ++i) {
      MockChannel c = channel(i);
      uint32_t h = mix(i * 13 + 5);
      std::string line = "#EXTINF:-1";
      if (!c.epgId.empty()) line += " tvg-id=\"" + c.epgId + "\"";
      line += " tvg-name=\"" + c.name + "\"";
      if (!c.logo.empty()) line += " tvg-logo=\"" + c.logo + "\"";
      bool extgrp = h % 9 == 0;
      if (!extgrp) line += " group-title=\"" + trimmed(categories_[c.category].name) + "\"";
      if (h % 5 == 0) line += " tvg-chno=\"" + std::to_string(i + 1) + "\"";
      if (c.radio) line += " radio=\"true\"";
      if (h % 11 == 0) line += " catchup=\"default\" catchup-days=\"3\"";
      line += ",";
      line += c.name;
      out.add(line);
      out.add(nl);
      if (extgrp) {
        out.add("#EXTGRP:" + trimmed(categories_[c.category].name));
        out.add(nl);
      }
      if (h % 17 == 0) {
        out.add("#EXTVLCOPT:http-user-agent=MockPlayer/1.0");
        out.add(nl);
        out.add("#EXTVLCOPT:http-referrer=http://ref.example.invalid/");
        out.add(nl);
      }
      out.add(std::string(kHost) + "/live/" + std::string(kUser) + "/" + std::string(kPass) + "/" + c.streamId +
              (c.radio ? ".mp3" : ".ts"));
      out.add(nl);
    }
    out.flush();
  }

  // Channels a client should produce (the broken rows are skipped).
  size_t usableCount() const { return count_ - count_ / 1000; }

  static std::string escape(std::string_view s) {
    std::string out;
    for (char c : s) {
      if (c == '"' || c == '\\') out += '\\';
      out += c;
    }
    return out;
  }

  // Every non-ASCII code point and every quote as \uXXXX (surrogate pairs
  // above U+FFFF).
  static std::string escapeUnicode(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
      unsigned char c = static_cast<unsigned char>(s[i]);
      uint32_t cp = c;
      size_t len = 1;
      if (c >= 0xF0) cp = c & 0x07, len = 4;
      else if (c >= 0xE0) cp = c & 0x0F, len = 3;
      else if (c >= 0xC0) cp = c & 0x1F, len = 2;
      for (size_t k = 1; k < len && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
      i += len;
      if (cp < 0x80 && cp != '"' && cp != '\\') {
        out += static_cast<char>(cp);
        continue;
      }
      char buf[16];
      if (cp >= 0x10000) {
        uint32_t v = cp - 0x10000;
        std::snprintf(buf, sizeof buf, "\\u%04x\\u%04x", 0xD800 + (v >> 10), 0xDC00 + (v & 0x3FF));
      } else {
        std::snprintf(buf, sizeof buf, "\\u%04X", cp);
      }
      out += buf;
    }
    return out;
  }

 private:
  static std::string trimmed(const std::string& s) {
    size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' ');
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
  }

  // Buffers output and hands it on in fixed-size chunks.
  template <class Emit>
  class Chunker {
   public:
    Chunker(size_t size, Emit& emit) : size_(size), emit_(emit) { buf_.reserve(size); }
    void add(std::string_view s) {
      while (!s.empty()) {
        size_t n = std::min(size_ - buf_.size(), s.size());
        buf_.append(s.data(), n);
        s.remove_prefix(n);
        if (buf_.size() == size_) flush();
      }
    }
    void flush() {
      if (buf_.empty()) return;
      emit_(std::string_view(buf_));
      buf_.clear();
    }

   private:
    size_t size_;
    Emit& emit_;
    std::string buf_;
  };

  size_t count_;
  std::vector<Category> categories_;
};

}  // namespace mock
