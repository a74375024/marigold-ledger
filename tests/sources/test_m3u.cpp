#include "core/sources/m3u.hpp"

#include <set>
#include <string>
#include <vector>

#include "doctest.h"
#include "sources/mock_provider.hpp"

using namespace marigold;
using namespace marigold::sources;

namespace {

struct Parsed {
  std::vector<Channel> channels;
  std::vector<m3u::StreamInfo> streams;
  m3u::PlaylistHeader header;
  std::optional<Error> error;
  size_t skipped = 0;
  size_t longLines = 0;
};

Parsed parse(std::string_view text, size_t step = 0) {
  Parsed out;
  m3u::Parser p("pl1", [&](Channel&& c, m3u::StreamInfo&& s) {
    out.channels.push_back(std::move(c));
    out.streams.push_back(std::move(s));
    return true;
  });
  if (step == 0) step = text.size() ? text.size() : 1;
  for (size_t i = 0; i < text.size(); i += step)
    if (!p.feed(text.substr(i, step))) break;
  out.error = p.finish();
  out.header = p.header();
  out.skipped = p.skipped();
  out.longLines = p.longLines();
  return out;
}

const char* kSample =
    "\xEF\xBB\xBF#EXTM3U url-tvg=\"http://epg.example.invalid/a.xml.gz,http://epg.example.invalid/b.xml\" "
    "tvg-shift=\"-1.5\"\r\n"
    "#EXTINF:-1 tvg-id=\"espn.us\" tvg-name=\"ESPN\" tvg-logo=\"http://logos.example.invalid/espn.png\" "
    "group-title=\"US| SPORTS\" tvg-chno=\"206\",US: ESPN FHD\r\n"
    "http://pl.example.invalid/live/u/p/1.ts\r\n"
    "#EXTINF:-1 tvg-name=\"ESPN, Brasil\" group-title=\"BR, Esportes;Futebol\",ESPN, Brasil\r\n"
    "#EXTVLCOPT:http-user-agent=MockPlayer/1.0\r\n"
    "#EXTVLCOPT:http-referrer=http://ref.example.invalid/\r\n"
    "http://pl.example.invalid/live/u/p/2.ts\r\n"
    "#EXTINF:0 tvg-id=\"jazz\" radio=\"true\" catchup=\"default\" catchup-days=\"7\" tvg-shift=\"+2\",\r\n"
    "#EXTGRP:Radio\r\n"
    "http://pl.example.invalid/live/u/p/3.mp3|User-Agent=Kodi%2F20&Referer=http%3A%2F%2Fref.example.invalid%2F\r\n"
    "\r\n"
    "#EXTINF:-1,\r\n"
    "http://pl.example.invalid/live/u/p/4.ts\r\n";

}  // namespace

TEST_CASE("m3u: attributes") {
  std::string_view line = R"(-1 tvg-id="a.b" TVG-NAME='Sky "One"' tvg-logo=http://x.invalid/l.png group-title="")";
  CHECK(m3u::attribute(line, "tvg-id") == std::string("a.b"));
  CHECK(m3u::attribute(line, "tvg-name") == std::string("Sky \"One\""));
  CHECK(m3u::attribute(line, "tvg-logo") == std::string("http://x.invalid/l.png"));
  CHECK(m3u::attribute(line, "group-title") == std::string(""));
  CHECK_FALSE(m3u::attribute(line, "id"));  // only whole names
  CHECK_FALSE(m3u::attribute(line, "tvg"));
  // A quoted value's words are not attribute names.
  CHECK_FALSE(m3u::attribute(R"(tvg-name="x radio=true")", "radio"));
  CHECK(m3u::attribute(R"(tvg-id="a"tvg-name="b")", "tvg-name") == std::string("b"));
  CHECK(m3u::attribute(R"(tvg-name="unterminated)", "tvg-name") == std::string("unterminated"));
  CHECK_FALSE(m3u::attribute("", "tvg-id"));
  CHECK_FALSE(m3u::attribute("=== =\"x\"", "tvg-id"));
}

TEST_CASE("m3u: a sample playlist") {
  for (size_t step : {size_t{0}, size_t{1}, size_t{2}, size_t{13}}) {
    CAPTURE(step);
    Parsed r = parse(kSample, step);
    REQUIRE_FALSE(r.error);
    CHECK(r.header.sawExtM3u);
    CHECK(r.header.guideUrl == std::string("http://epg.example.invalid/a.xml.gz"));
    CHECK(r.header.tvgShiftMinutes == -90);
    REQUIRE(r.channels.size() == 4);

    const Channel& espn = r.channels[0];
    CHECK(espn.serverId == "m3u:pl1");
    CHECK(espn.id == "m3u:pl1|" + espn.itemId);
    CHECK(espn.itemId.size() == 16);
    CHECK(espn.name == "US: ESPN FHD");
    CHECK(espn.number == std::string("206"));
    CHECK(espn.imageUrl == "http://logos.example.invalid/espn.png");
    CHECK(espn.tags == std::vector<std::string>{"US| SPORTS"});
    CHECK(espn.group == std::string("US| SPORTS"));
    CHECK(espn.epgId == std::string("espn.us"));
    CHECK_FALSE(espn.isRadio);
    CHECK(r.streams[0].url == "http://pl.example.invalid/live/u/p/1.ts");
    CHECK(r.streams[0].headers.empty());

    // A comma inside quotes is not the name separator; ';' splits groups.
    const Channel& brasil = r.channels[1];
    CHECK(brasil.name == "ESPN, Brasil");
    CHECK(brasil.tags == std::vector<std::string>{"BR, Esportes", "Futebol"});
    CHECK_FALSE(brasil.epgId);
    CHECK(r.streams[1].headers ==
          std::vector<std::string>{"User-Agent: MockPlayer/1.0", "Referer: http://ref.example.invalid/"});

    // No name after the comma: tvg-name, then tvg-id. #EXTGRP gives the group.
    const Channel& jazz = r.channels[2];
    CHECK(jazz.name == "jazz");
    CHECK(jazz.isRadio);
    CHECK(jazz.tags == std::vector<std::string>{"Radio"});
    CHECK(r.streams[2].url == "http://pl.example.invalid/live/u/p/3.mp3");
    CHECK(r.streams[2].headers ==
          std::vector<std::string>{"User-Agent: Kodi/20", "Referer: http://ref.example.invalid/"});
    CHECK(r.streams[2].tvgShiftMinutes == 120);

    // Nothing at all: a numbered name.
    CHECK(r.channels[3].name == "Channel 4");
    CHECK(r.channels[3].tags.empty());
    CHECK_FALSE(r.channels[3].group);
  }
}

TEST_CASE("m3u: ids are stable, never from the link, and unique") {
  std::string a = "#EXTM3U\n#EXTINF:-1 tvg-id=\"x\" group-title=\"G\",Same\nhttp://a.example.invalid/live/u/p/1.ts\n"
                  "#EXTINF:-1 tvg-id=\"x\" group-title=\"G\",Same\nhttp://a.example.invalid/live/u/p/2.ts\n"
                  "#EXTINF:-1 tvg-id=\"x\" group-title=\"G\",Same\nhttp://a.example.invalid/live/u/p/3.ts\n"
                  "#EXTINF:-1 tvg-id=\"x\" group-title=\"H\",Same\nhttp://a.example.invalid/live/u/p/4.ts\n";
  // Same entries, other links (a new login): the same ids.
  std::string b = a;
  for (size_t at = b.find("/u/p/"); at != std::string::npos; at = b.find("/u/p/", at + 1)) b.replace(at, 5, "/v/q/");
  Parsed ra = parse(a), rb = parse(b);
  REQUIRE(ra.channels.size() == 4);
  REQUIRE(rb.channels.size() == 4);
  std::set<std::string> ids;
  for (size_t i = 0; i < 4; ++i) {
    CHECK(ra.channels[i].id == rb.channels[i].id);
    ids.insert(ra.channels[i].id);
    CHECK(ra.channels[i].id.find("example") == std::string::npos);
    CHECK(ra.channels[i].id.find("/") == std::string::npos);
  }
  CHECK(ids.size() == 4);
  // Two parses of the same text agree, including the duplicates' order.
  Parsed again = parse(a, 1);
  for (size_t i = 0; i < 4; ++i) CHECK(again.channels[i].id == ra.channels[i].id);
  // Another account: other ids.
  m3u::Parser other("pl2", [](Channel&& c, m3u::StreamInfo&&) {
    CHECK(c.id.rfind("m3u:pl2|", 0) == 0);
    return true;
  });
  other.feed(a);
  CHECK_FALSE(other.finish());
}

TEST_CASE("m3u: entries that can't be used") {
  std::string text =
      "#EXTM3U\n"
      "http://a.example.invalid/no-extinf.ts\n"           // a link with no #EXTINF
      "#EXTINF:-1,First\n"
      "#EXTINF:-1,Second\n"                               // abandons First
      "http://a.example.invalid/2.ts\n"
      "#EXTINF:-1,Relative\n"
      "streams/3.ts\n"                                    // not absolute
      "#EXTINF:-1,Spaces\n"
      "http://a.example.invalid/a b.ts\n"
      "#EXTINF:-1,Last\n"
      "rtmp://a.example.invalid/live/5\n"
      "#EXTINF:-1,Dangling\n";
  Parsed r = parse(text, 3);
  REQUIRE_FALSE(r.error);
  REQUIRE(r.channels.size() == 2);
  CHECK(r.channels[0].name == "Second");
  CHECK(r.channels[1].name == "Last");
  CHECK(r.streams[1].url == "rtmp://a.example.invalid/live/5");
  CHECK(r.skipped == 4);
}

TEST_CASE("m3u: lines over 4096 bytes are dropped whole") {
  std::string longName(5000, 'N');
  std::string longLink = "http://a.example.invalid/" + std::string(5000, 'x');
  std::string text = "#EXTM3U\n"
                     "#EXTINF:-1 group-title=\"G\"," + longName + "\n" +
                     "http://a.example.invalid/1.ts\n"           // its #EXTINF was dropped: no name, skipped
                     "#EXTINF:-1,Long link\n" + longLink + "\n" +
                     "http://a.example.invalid/2.ts\n"           // must not take "Long link"'s name
                     "#" + std::string(6000, 'c') + "\n"         // a long comment is harmless
                     "#EXTINF:-1,Fine\n"
                     "http://a.example.invalid/3.ts\n";
  for (size_t step : {size_t{0}, size_t{1}, size_t{4096}, size_t{4097}}) {
    Parsed r = parse(text, step);
    REQUIRE_FALSE(r.error);
    CHECK(r.longLines == 3);
    REQUIRE(r.channels.size() == 1);
    CHECK(r.channels[0].name == "Fine");
    CHECK(r.streams[0].url == "http://a.example.invalid/3.ts");
  }
  // Exactly 4096 bytes is kept.
  std::string edge = "#EXTM3U\n#EXTINF:-1," + std::string(4096 - 11, 'E') + "\nhttp://a.example.invalid/1.ts";
  Parsed r = parse(edge, 1);
  REQUIRE(r.channels.size() == 1);
  CHECK(r.channels[0].name.size() == 4096 - 11);
}

TEST_CASE("m3u: line ends, BOM and no final newline") {
  std::string lf = "#EXTM3U\n#EXTINF:-1,A\nhttp://a.example.invalid/1.ts";
  std::string cr = "#EXTM3U\r#EXTINF:-1,A\rhttp://a.example.invalid/1.ts\r";
  std::string bom = "\xEF\xBB\xBF#EXTINF:-1,A\r\nhttp://a.example.invalid/1.ts\r\n";
  for (const std::string& text : {lf, cr, bom}) {
    Parsed r = parse(text, 1);
    REQUIRE_FALSE(r.error);
    REQUIRE(r.channels.size() == 1);
    CHECK(r.channels[0].name == "A");
    CHECK(r.streams[0].url == "http://a.example.invalid/1.ts");
  }
  // A playlist with no #EXTM3U line still works (first line is a comment).
  CHECK_FALSE(parse(bom).header.sawExtM3u);
}

TEST_CASE("m3u: not a playlist") {
  for (const char* text : {"<!DOCTYPE html><html>Not found</html>", "{\"error\":\"expired\"}", "", "\n\n"}) {
    Parsed r = parse(text, 1);
    REQUIRE(r.error);
    CHECK(r.error->message == "The link didn't return a playlist.");
    CHECK(r.channels.empty());
  }
  Parsed hls = parse("#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:6\n#EXTINF:6.0,\nseg1.ts\n");
  REQUIRE(hls.error);
  CHECK(hls.error->message == "This link is a single video stream, not a channel playlist.");
  Parsed master = parse("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\nhttp://a.example.invalid/v.m3u8\n");
  REQUIRE(master.error);
  // An #EXTM3U with nothing in it is an empty playlist, not an error.
  Parsed empty = parse("#EXTM3U\n");
  CHECK_FALSE(empty.error);
  CHECK(empty.channels.empty());
  // Error text never carries the playlist's content.
  CHECK(parse("http-ish http://a.example.invalid/live/u/p/1.ts").error.has_value());
}

TEST_CASE("m3u: tvg-shift values") {
  auto shift = [](const char* value) {
    return parse(std::string("#EXTM3U tvg-shift=\"") + value + "\"\n").header.tvgShiftMinutes;
  };
  CHECK(shift("1") == 60);
  CHECK(shift("-2") == -120);
  CHECK(shift("+0.5") == 30);
  CHECK(shift("2,5") == 150);
  CHECK(shift("0") == 0);
  CHECK_FALSE(shift("25"));
  CHECK_FALSE(shift("abc"));
  CHECK_FALSE(shift(""));
  CHECK_FALSE(shift("1.2.3"));
  CHECK(parse("#EXTM3U x-tvg-url=\"http://epg.example.invalid/g.xml\"\n").header.guideUrl ==
        std::string("http://epg.example.invalid/g.xml"));
}

TEST_CASE("m3u: header values are checked") {
  std::string text = "#EXTM3U\n#EXTINF:-1,A\n#EXTVLCOPT:http-user-agent=\n#EXTVLCOPT:http-origin=http://o.example.invalid\n"
                     "#EXTVLCOPT:network-caching=1000\n"
                     "http://a.example.invalid/1.ts|User-Agent=Bad%0D%0AX-Injected:%201&Origin=\n";
  Parsed r = parse(text);
  REQUIRE(r.streams.size() == 1);
  // CR/LF decode to spaces, empty values are dropped, the later Origin is empty and ignored.
  CHECK(r.streams[0].headers ==
        std::vector<std::string>{"Origin: http://o.example.invalid", "User-Agent: Bad  X-Injected: 1"});
}

TEST_CASE("m3u: the callback can stop") {
  mock::Provider p(100);
  size_t seen = 0;
  m3u::Parser parser("pl1", [&](Channel&&, m3u::StreamInfo&&) { return ++seen < 5; });
  CHECK_FALSE(parser.feed(p.m3u()));
  CHECK(seen == 5);
  CHECK_FALSE(parser.finish());
}

TEST_CASE("m3u: the mock provider's playlist, any chunking") {
  mock::Provider p(2500);
  Result<m3u::Playlist> whole = m3u::parsePlaylist("pl1", p.m3u());
  REQUIRE(whole.ok());
  const m3u::Playlist& list = whole.value();
  CHECK(list.channels.size() == p.size());  // nameless rows get "Channel N"
  CHECK(list.streams.size() == p.size());
  CHECK(list.header.guideUrl.has_value());
  std::set<std::string> ids;
  size_t radio = 0, withHeaders = 0;
  for (const Channel& c : list.channels) {
    ids.insert(c.id);
    CHECK(c.id.find("example") == std::string::npos);
    CHECK(c.tags.size() == 1);
    if (c.isRadio) ++radio;
    auto it = list.streams.find(c.itemId);
    REQUIRE(it != list.streams.end());
    CHECK(it->second.url.find("/live/") != std::string::npos);
    if (!it->second.headers.empty()) ++withHeaders;
  }
  CHECK(ids.size() == p.size());
  CHECK(radio > 0);
  CHECK(withHeaders > 0);

  // CRLF and one byte at a time give the same channels.
  Parsed bytes = parse(p.m3u(true), 1);
  REQUIRE_FALSE(bytes.error);
  REQUIRE(bytes.channels.size() == list.channels.size());
  for (size_t i = 0; i < bytes.channels.size(); ++i) {
    CHECK(bytes.channels[i].id == list.channels[i].id);
    CHECK(bytes.channels[i].name == list.channels[i].name);
  }
}
