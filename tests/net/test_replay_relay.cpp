// Build 172: the replay relay on the loopback address, against a pretend
// provider (the LAN server) whose archive starts every stretch at its
// chunk: the player's stream starts at the moment asked, by a byte range
// (asked again nearer when the first guess is off) or by reading through,
// and the relay says where it really began.
#include <doctest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <string>

#include "core/media/ts_start.hpp"
#include "playback/ts_builder.hpp"
#include "platform/lan_server.hpp"
#include "platform/replay_relay.hpp"

using namespace marigold;
namespace tsc = marigold::media::tsclock;

namespace {

constexpr int64_t kStart = 5000000;

// `seconds` of 25 fps video from kStart (~80 KB a second).
std::string chunk(int seconds) {
  tsgen::Muxer m;
  tsgen::VideoFormat f;
  for (int i = 0; i < seconds * 25; ++i) {
    if (i % 10 == 0) {
      m.section(0, tsgen::pat());
      m.section(tsgen::kPmtPid, tsgen::pmt(0x1B, 0));
    }
    int64_t t = kStart + static_cast<int64_t>(i) * 3600;
    m.pes(tsgen::kVideoPid, tsgen::videoPes(t, t, tsgen::picture(f, i % 50 == 0, 3000, 0, static_cast<uint8_t>(i))));
  }
  return std::string(m.out.begin(), m.out.end());
}

// What a player gets from `url` (the body, after the headers).
std::string play(const std::string& url, std::string* status = nullptr) {
  size_t colon = url.find(':', 7), slash = url.find('/', 7);
  int port = std::atoi(url.substr(colon + 1, slash - colon - 1).c_str());
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(static_cast<uint16_t>(port));
  ::inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
    ::close(fd);
    return "";
  }
  std::string req = "GET " + url.substr(slash) + " HTTP/1.1\r\nHost: 127.0.0.1\r\nRange: bytes=0-\r\n\r\n";
  ::send(fd, req.data(), req.size(), 0);
  std::string out;
  char buf[65536];
  ssize_t n;
  while ((n = ::recv(fd, buf, sizeof buf, 0)) > 0) out.append(buf, static_cast<size_t>(n));
  ::close(fd);
  size_t head = out.find("\r\n\r\n");
  if (status) *status = out.substr(0, out.find("\r\n"));
  return head == std::string::npos ? std::string() : out.substr(head + 4);
}

int64_t firstPts(const std::string& body) {
  tsc::TsClock c;
  c.feed(reinterpret_cast<const uint8_t*>(body.data()), body.size());
  return c.first() ? *c.first() : -1;
}

}  // namespace

TEST_CASE("replay relay: starts the stream at the moment, by a range or by reading through") {
  const std::string archive = chunk(40);
  std::atomic<int> ranged{0};
  platform::LanServer provider;
  platform::LanServer::Options o;
  o.host = "127.0.0.1";
  o.lifetimeSeconds = 60;
  auto st = provider.start(
      [&](const lan::Request& r) {
        lan::Response out;
        out.type = "video/mp2t";
        if (r.path == "/moved/9.ts") return lan::Response::redirect("/plain/9.ts");
        auto range = r.headers.find("range");
        if (r.path == "/ranges/9.ts" && range != r.headers.end()) {
          uint64_t from = std::strtoull(range->second.c_str() + 6, nullptr, 10);
          ++ranged;
          out.status = 206;
          out.body = archive.substr(std::min<size_t>(from, archive.size()));
          out.headers.push_back({"Content-Range", "bytes " + std::to_string(from) + "-" +
                                                      std::to_string(archive.size() - 1) + "/" +
                                                      std::to_string(archive.size())});
          return out;
        }
        if (r.path == "/ranges/9.ts" || r.path == "/plain/9.ts") {
          out.status = 200;
          out.body = archive;
          return out;
        }
        return lan::Response::notFound();
      },
      o);
  REQUIRE(st.state == platform::LanServer::State::Ready);
  const std::string base = st.base();

  platform::ReplayRelay relay;
  REQUIRE(relay.start());
  CHECK(platform::ReplayRelay::takes(base + "/plain/9.ts"));
  CHECK_FALSE(platform::ReplayRelay::takes("https://p.example.invalid/9.ts"));
  CHECK(relay.localUrl("https://p.example.invalid/9.ts", "", {}).empty());

  // 20 s in by a range: the bytes a second known well.
  tsc::TsClock whole;
  whole.feed(reinterpret_cast<const uint8_t*>(archive.data()), archive.size());
  uint32_t rate = whole.byteRate();
  REQUIRE(rate > 0);
  platform::ReplayRelay::Start seek;
  seek.skip = 20;
  seek.seek = true;
  seek.byteRate = rate;
  seek.startByte = uint64_t{20} * rate / 188 * 188;
  std::string url = relay.localUrl(base + "/ranges/9.ts", "User-Agent: Test", seek);
  REQUIRE(url.rfind("http://127.0.0.1:", 0) == 0);
  std::string body = play(url);
  REQUIRE(firstPts(body) >= 0);
  CHECK(tsc::delta(firstPts(body), kStart) / 90000 == 20);
  CHECK(relay.skipped() == 20);
  CHECK(relay.opens() == 2);  // its first picture, then the range
  CHECK(relay.lastRange() == seek.startByte);
  CHECK(body.size() < archive.size() / 2 + 200 * 1024);
  // The tables lead.
  CHECK(static_cast<uint8_t>(body[0]) == 0x47);
  CHECK((((static_cast<uint8_t>(body[1]) & 0x1F) << 8) | static_cast<uint8_t>(body[2])) == 0);

  // A guess far off (a quarter of the way): asked again nearer.
  platform::ReplayRelay::Start off = seek;
  off.skip = 30;
  off.startByte = uint64_t{30} * rate / 4 / 188 * 188;
  body = play(relay.localUrl(base + "/ranges/9.ts", "", off));
  CHECK(tsc::delta(firstPts(body), kStart) / 90000 == 30);
  CHECK(relay.opens() == 3);
  CHECK(relay.lastRange() != off.startByte);

  // No ranges: read through from the start (behind a redirect).
  platform::ReplayRelay::Start through;
  through.skip = 15;
  through.byteRate = rate;
  body = play(relay.localUrl(base + "/moved/9.ts", "", through));
  CHECK(tsc::delta(firstPts(body), kStart) / 90000 == 15);
  CHECK(relay.skipped() == 15);
  CHECK(relay.opens() == 1);

  // A range ignored (200): read through all the same.
  body = play(relay.localUrl(base + "/plain/9.ts", "", seek));
  CHECK(tsc::delta(firstPts(body), kStart) / 90000 == 20);

  // An old session's link, and a provider that isn't there.
  std::string old = relay.localUrl(base + "/plain/9.ts", "", through);
  std::string fresh = relay.localUrl(base + "/plain/9.ts", "", through);
  std::string status;
  CHECK(play(old, &status).empty());
  CHECK(status.find("404") != std::string::npos);
  play(relay.localUrl(base + "/gone/9.ts", "", through), &status);
  CHECK(status.find("502") != std::string::npos);
  relay.stop();
  CHECK_FALSE(relay.skipped());
  (void)fresh;
  provider.stop();
}

TEST_CASE("replay relay Build 177: what it passes on is read for ad markers, across its sessions until taken") {
  // 40 s declaring SCTE-35, a break going out at 5 s (before the moment:
  // never passed on), 25 s and 32 s.
  tsgen::Muxer m;
  tsgen::VideoFormat f;
  for (int i = 0; i < 40 * 25; ++i) {
    if (i % 10 == 0) {
      m.section(0, tsgen::pat());
      m.section(tsgen::kPmtPid, tsgen::pmt(0x1B, 0, false, 0x86));
    }
    if (i == 5 * 25 || i == 25 * 25 || i == 32 * 25)
      m.section(tsgen::kCuePid, tsgen::spliceInsert(static_cast<uint32_t>(i), true, 30));
    int64_t t = kStart + static_cast<int64_t>(i) * 3600;
    m.pes(tsgen::kVideoPid, tsgen::videoPes(t, t, tsgen::picture(f, i % 50 == 0, 3000, 0, static_cast<uint8_t>(i))));
  }
  const std::string archive(m.out.begin(), m.out.end());
  platform::LanServer provider;
  platform::LanServer::Options o;
  o.host = "127.0.0.1";
  o.lifetimeSeconds = 60;
  auto st = provider.start(
      [&](const lan::Request& r) {
        if (r.path != "/plain/9.ts") return lan::Response::notFound();
        lan::Response out;
        out.type = "video/mp2t";
        out.status = 200;
        out.body = archive;
        return out;
      },
      o);
  REQUIRE(st.state == platform::LanServer::State::Ready);
  platform::ReplayRelay relay;
  REQUIRE(relay.start());
  CHECK_FALSE(relay.takeAdMarkers().read());  // nothing passed yet

  platform::ReplayRelay::Start through;
  through.skip = 20;
  std::string body = play(relay.localUrl(st.base() + "/plain/9.ts", "", through));
  REQUIRE(firstPts(body) >= 0);
  media::scte35::Tally t = relay.takeAdMarkers();
  CHECK(t.read());
  CHECK(t.declared);
  CHECK(t.cues == 2);
  CHECK(t.outOfNetwork == 2);
  CHECK(t.videoMs >= 19000);
  CHECK(t.videoMs <= 20500);
  CHECK(media::scte35::replayText(t) == "ad markers seen 2 times in 20 s (splice_insert, out of network, break 30 s)");
  // Taken: cleared.
  CHECK_FALSE(relay.takeAdMarkers().read());

  // Two stretches, the first replaced by the second, the second stopped:
  // both counted in one take.
  play(relay.localUrl(st.base() + "/plain/9.ts", "", through));
  play(relay.localUrl(st.base() + "/plain/9.ts", "", through));
  relay.stop();
  media::scte35::Tally both = relay.takeAdMarkers();
  CHECK(both.cues == 4);
  CHECK(both.videoMs >= 38000);
  CHECK(media::scte35::replayText(both).find("://") == std::string::npos);
  provider.stop();
}
