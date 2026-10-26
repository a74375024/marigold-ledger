// The pure parts of Legacy's TS proxy (vendor/legacy/src/proxyts.c): source
// choice, playlist parsing, link joining and the MPEG-TS table capture. The
// file is included with MARIGOLD_PROXYTS_PURO, which leaves out the sockets,
// threads and downloads, so nothing here touches the network.
#define MARIGOLD_PROXYTS_PURO 1
#include "proxyts.c"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"

namespace {

std::unique_ptr<PxLista> parse(const std::string& text, const char* base, int resolution = 0, int* ok = nullptr) {
  auto l = std::make_unique<PxLista>();
  int r = pxLer(text.c_str(), base, resolution, l.get());
  if (ok) *ok = r;
  return l;
}

std::string join(const char* base, const char* rel) {
  char out[NV_MEDIA_URL_MAX];
  pxJuntar(base, rel, out, sizeof out);
  return out;
}

using Packet = std::vector<unsigned char>;

Packet packet(int pid, bool start) {
  Packet k(188, 0xFF);
  k[0] = 0x47;
  k[1] = static_cast<unsigned char>((start ? 0x40 : 0) | ((pid >> 8) & 0x1F));
  k[2] = static_cast<unsigned char>(pid & 0xFF);
  k[3] = 0x10;  // payload only
  return k;
}

// A PAT with one programme whose PMT is on `pmtPid`.
Packet pat(int pmtPid) {
  Packet k = packet(0, true);
  k[4] = 0;     // pointer field
  k[5] = 0x00;  // table id
  k[6] = 0xB0;
  k[7] = 13;    // section length: header 5 + one programme 4 + CRC 4
  k[8] = 0x00;
  k[9] = 0x01;  // transport stream id
  k[10] = 0xC1;
  k[11] = 0x00;
  k[12] = 0x00;
  k[13] = 0x00;
  k[14] = 0x01;  // programme 1
  k[15] = static_cast<unsigned char>(0xE0 | ((pmtPid >> 8) & 0x1F));
  k[16] = static_cast<unsigned char>(pmtPid & 0xFF);
  return k;
}

std::vector<unsigned char> stream(const std::vector<Packet>& packets, size_t junk = 0) {
  std::vector<unsigned char> out(junk, 0x00);
  for (const Packet& p : packets) out.insert(out.end(), p.begin(), p.end());
  return out;
}

}  // namespace

TEST_CASE("proxy candidates: http(s) links that are not progressive files") {
  CHECK(proxyts_candidata("http://example.invalid/live/1.m3u8", 0));
  CHECK(proxyts_candidata("https://example.invalid/live/1.ts", 0));
  CHECK(proxyts_candidata("http://example.invalid/play?id=1.mp4x", 0));
  CHECK_FALSE(proxyts_candidata("http://example.invalid/movie.mp4", 0));
  CHECK_FALSE(proxyts_candidata("http://example.invalid/movie.mkv?t=abc", 0));
  CHECK_FALSE(proxyts_candidata("http://example.invalid/live/1.m3u8", 1));
  CHECK_FALSE(proxyts_candidata("rtmp://example.invalid/live", 0));
  CHECK_FALSE(proxyts_candidata("", 0));
  CHECK_FALSE(proxyts_candidata(nullptr, 0));
}

TEST_CASE("segment links are joined to the playlist link") {
  const char* base = "http://example.invalid:8080/live/a/b/list.m3u8?t=1/2";
  CHECK(join(base, "seg1.ts") == "http://example.invalid:8080/live/a/b/seg1.ts");
  CHECK(join(base, "/hls/seg1.ts") == "http://example.invalid:8080/hls/seg1.ts");
  CHECK(join(base, "https://cdn.example.invalid/x.ts") == "https://cdn.example.invalid/x.ts");
  CHECK(join("http://example.invalid", "/x.ts") == "http://example.invalid/x.ts");
}

TEST_CASE("a live media playlist") {
  int ok = 0;
  auto l = parse("#EXTM3U\r\n#EXT-X-TARGETDURATION:6\r\n#EXT-X-MEDIA-SEQUENCE:1200\r\n"
                 "#EXTINF:6.0,\r\nseg1200.ts\r\n#EXTINF:6.0,\r\nseg1201.ts\r\n#EXTINF:6.0,\r\nseg1202.ts\r\n",
                 "http://example.invalid/live/list.m3u8", 0, &ok);
  CHECK(ok == 1);
  CHECK(l->alvo == 6);
  CHECK(l->seq0 == 1200);
  CHECK(l->n == 3);
  CHECK(std::string(l->uri[2]) == "http://example.invalid/live/seg1202.ts");
  CHECK_FALSE(l->mestre);
  CHECK_FALSE(l->fim);
  CHECK_FALSE(l->cifrado);
  CHECK_FALSE(l->fmp4);
}

TEST_CASE("playlist flags: end, encryption, fMP4") {
  const char* base = "http://example.invalid/list.m3u8";
  CHECK(parse("#EXTM3U\n#EXTINF:4,\na.ts\n#EXT-X-ENDLIST\n", base)->fim);
  CHECK(parse("#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"k\"\n#EXTINF:4,\na.ts\n", base)->cifrado);
  CHECK_FALSE(parse("#EXTM3U\n#EXT-X-KEY:METHOD=NONE\n#EXTINF:4,\na.ts\n", base)->cifrado);
  CHECK(parse("#EXTM3U\n#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:4,\na.m4s\n", base)->fmp4);
}

TEST_CASE("the target duration is kept between 1 and 15 s; 6 s when missing") {
  const char* base = "http://example.invalid/list.m3u8";
  CHECK(parse("#EXTM3U\n#EXT-X-TARGETDURATION:30\na.ts\n", base)->alvo == 15);
  CHECK(parse("#EXTM3U\n#EXT-X-TARGETDURATION:0\na.ts\n", base)->alvo == 1);
  CHECK(parse("#EXTM3U\na.ts\n", base)->alvo == 6);
}

TEST_CASE("not a playlist, or one without segments") {
  int ok = 1;
  parse("<html>nope</html>", "http://example.invalid/x", 0, &ok);
  CHECK(ok == 0);
  parse("#EXTM3U\n#EXT-X-TARGETDURATION:6\n", "http://example.invalid/x", 0, &ok);
  CHECK(ok == 0);
}

TEST_CASE("at most 64 segments are read from a playlist") {
  std::string text = "#EXTM3U\n";
  for (int i = 0; i < 100; ++i) text += "#EXTINF:2,\ns" + std::to_string(i) + ".ts\n";
  auto l = parse(text, "http://example.invalid/list.m3u8");
  CHECK(l->n == PX_SEG_MAX);
  CHECK(std::string(l->uri[63]) == "http://example.invalid/s63.ts");
}

TEST_CASE("a master playlist: the highest variant, or the nearest to the chosen resolution") {
  const std::string master =
      "#EXTM3U\n"
      "#EXT-X-STREAM-INF:BANDWIDTH=800000,RESOLUTION=640x360\nlow/index.m3u8\n"
      "#EXT-X-STREAM-INF:BANDWIDTH=2500000,RESOLUTION=1280x720\nmid/index.m3u8\n"
      "#EXT-X-STREAM-INF:BANDWIDTH=6000000,RESOLUTION=1920x1080\nhigh/index.m3u8\n"
      "#EXT-X-STREAM-INF:BANDWIDTH=7000000,RESOLUTION=1920x1080\nhigher/index.m3u8\n";
  const char* base = "http://example.invalid/ch/master.m3u8";
  auto best = parse(master, base, 0);
  CHECK(best->mestre);
  CHECK(best->n == 1);
  // Same height: the higher bandwidth wins.
  CHECK(std::string(best->uri[0]) == "http://example.invalid/ch/higher/index.m3u8");
  auto hd = parse(master, base, nv_res_altura_opcao(720));
  CHECK(std::string(hd->uri[0]) == "http://example.invalid/ch/mid/index.m3u8");
  auto sd = parse(master, base, nv_res_altura_opcao(480));
  CHECK(std::string(sd->uri[0]) == "http://example.invalid/ch/low/index.m3u8");
}

TEST_CASE("resolution options and heights") {
  CHECK(nv_res_opcao_altura(0) == 0);
  CHECK(nv_res_opcao_altura(1) == 2160);
  CHECK(nv_res_opcao_altura(2) == 1080);
  CHECK(nv_res_opcao_altura(3) == 720);
  CHECK(nv_res_opcao_altura(4) == 480);
  CHECK(nv_res_opcao_altura(9) == 0);
  CHECK(nv_res_altura_opcao(0) == 0);
  CHECK(nv_res_altura_opcao(2160) == 1);
  CHECK(nv_res_altura_opcao(1080) == 2);
  CHECK(nv_res_altura_opcao(720) == 3);
  CHECK(nv_res_altura_opcao(576) == 4);
}

TEST_CASE("TS alignment is found after leading junk; anything else is not TS") {
  std::vector<unsigned char> ts = stream({packet(0x100, false), packet(0x100, false), packet(0x100, false)}, 5);
  CHECK(pxInicio(ts.data(), static_cast<long>(ts.size())) == 5);
  std::vector<unsigned char> text(600, 'a');
  CHECK(pxInicio(text.data(), static_cast<long>(text.size())) == -1);
  // Two packets are not enough to be sure.
  std::vector<unsigned char> shortTs = stream({packet(0x100, false), packet(0x100, false)});
  CHECK(pxInicio(shortTs.data(), static_cast<long>(shortTs.size())) == -1);
}

TEST_CASE("the PAT and PMT of a segment are kept for the next one") {
  Packet p = pat(0x1E1);
  Packet m = packet(0x1E1, true);
  std::vector<unsigned char> seg = stream({p, m, packet(0x1E2, true), packet(0x1E2, false)});
  CHECK(pxPid(p.data()) == 0);
  CHECK(pxPid(m.data()) == 0x1E1);
  PxTabelas t;
  std::memset(&t, 0, sizeof t);
  pxColher(seg.data(), static_cast<long>(seg.size()), &t);
  CHECK(t.temPat);
  CHECK(t.temPmt);
  CHECK(t.pmtPid == 0x1E1);
  CHECK(std::memcmp(t.pat, p.data(), 188) == 0);
  CHECK(std::memcmp(t.pmt, m.data(), 188) == 0);
}

TEST_CASE("table capture stops at a broken packet") {
  Packet p = pat(0x1E1);
  Packet broken = packet(0x1E1, true);
  broken[0] = 0x00;
  std::vector<unsigned char> seg = stream({p, broken});
  PxTabelas t;
  std::memset(&t, 0, sizeof t);
  pxColher(seg.data(), static_cast<long>(seg.size()), &t);
  CHECK(t.temPat);
  CHECK_FALSE(t.temPmt);
}
