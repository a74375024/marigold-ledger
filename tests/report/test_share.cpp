#include <string>
#include <vector>

#include "core/report/share.hpp"
#include "doctest.h"

namespace share = marigold::report::share;

namespace {
const std::string kPath = "/00112233445566778899aabbccddeeff/marigold-channels.csv";
}

TEST_CASE("share: only an exact GET of the path is served") {
  CHECK(share::accepts("GET " + kPath + " HTTP/1.1\r\nHost: tv\r\n\r\n", kPath));
  CHECK(share::accepts("GET " + kPath + " HTTP/1.0\r\n\r\n", kPath));
  CHECK(share::accepts("GET " + kPath + " HTTP/1.1\r\nUser-Agent: phone\r\nAccept: */*\r\n\r\n", kPath));

  CHECK_FALSE(share::accepts("HEAD " + kPath + " HTTP/1.1\r\n\r\n", kPath));
  CHECK_FALSE(share::accepts("POST " + kPath + " HTTP/1.1\r\n\r\n", kPath));
  CHECK_FALSE(share::accepts("get " + kPath + " HTTP/1.1\r\n\r\n", kPath));
  CHECK_FALSE(share::accepts("GET / HTTP/1.1\r\n\r\n", kPath));
  CHECK_FALSE(share::accepts("GET " + kPath + "?x=1 HTTP/1.1\r\n\r\n", kPath));
  CHECK_FALSE(share::accepts("GET " + kPath + "/../x HTTP/1.1\r\n\r\n", kPath));
  CHECK_FALSE(share::accepts("GET /00112233445566778899aabbccddeeff/ HTTP/1.1\r\n\r\n", kPath));
  CHECK_FALSE(share::accepts("GET " + kPath + " HTTP/2\r\n\r\n", kPath));
  CHECK_FALSE(share::accepts("GET  " + kPath + " HTTP/1.1\r\n\r\n", kPath));  // two spaces
  CHECK_FALSE(share::accepts("GET " + kPath + " HTTP/1.1\r\n", kPath));       // incomplete
  CHECK_FALSE(share::accepts("", kPath));
  CHECK_FALSE(share::accepts("\r\n\r\n", kPath));
}

TEST_CASE("share: bodies, second requests and oversized or invalid heads are refused") {
  CHECK_FALSE(share::accepts("GET " + kPath + " HTTP/1.1\r\nContent-Length: 0\r\n\r\n", kPath));
  CHECK_FALSE(share::accepts("GET " + kPath + " HTTP/1.1\r\ncontent-length: 0\r\n\r\n", kPath));
  CHECK_FALSE(
      share::accepts("GET " + kPath + " HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n", kPath));
  CHECK_FALSE(share::accepts("GET " + kPath + " HTTP/1.1\r\n\r\nGET / HTTP/1.1\r\n\r\n", kPath));
  CHECK_FALSE(share::accepts("GET " + kPath + " HTTP/1.1\r\n\r\nx", kPath));
  std::string big = "GET " + kPath + " HTTP/1.1\r\nX-Pad: " + std::string(8200, 'a') + "\r\n\r\n";
  CHECK_FALSE(share::accepts(big, kPath));
  CHECK_FALSE(share::accepts("GET " + kPath + " HTTP/1.1\r\nX: \xff\xfe\r\n\r\n", kPath));
}

TEST_CASE("share: head complete") {
  CHECK_FALSE(share::headComplete("GET / HTTP/1.1\r\n"));
  CHECK(share::headComplete("GET / HTTP/1.1\r\n\r\n"));
  CHECK_FALSE(share::headComplete(""));
}

TEST_CASE("share: response heads") {
  std::string ok = share::responseHead(true, share::kCsvType, 1234, "marigold-channels.csv");
  CHECK(ok ==
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/csv; charset=utf-8\r\n"
        "Content-Length: 1234\r\n"
        "Content-Disposition: attachment; filename=\"marigold-channels.csv\"\r\n"
        "Cache-Control: no-store\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Connection: close\r\n\r\n");
  std::string missing = share::responseHead(false, share::kCsvType, 1234, "a.csv");
  CHECK(missing.rfind("HTTP/1.1 404 Not Found\r\n", 0) == 0);
  CHECK(missing.find("Content-Length: 0\r\n") != std::string::npos);
  // A file name can't break out of its header.
  std::string odd = share::responseHead(true, share::kCsvType, 1, "a\"b\r\nX: y.csv");
  CHECK(odd.find("filename=\"a-b--X--y.csv\"") != std::string::npos);
  CHECK(odd.find("\r\nX: ") == std::string::npos);
}

TEST_CASE("share: file names, tokens, paths and urls") {
  CHECK(share::safeFilename("marigold-playback-log.csv") == "marigold-playback-log.csv");
  CHECK(share::safeFilename("../etc/passwd") == "-etc-passwd");
  CHECK(share::safeFilename("..") == "download");
  CHECK(share::safeFilename("") == "download");
  CHECK(share::safeFilename("caf\xC3\xA9.csv") == "caf--.csv");
  CHECK(share::hex({0x00, 0x0f, 0xa0, 0xff}) == "000fa0ff");
  CHECK(share::hex(std::vector<uint8_t>(share::kTokenBytes, 7)).size() == 32);
  CHECK(share::sharePath("abc", "r.csv") == "/abc/r.csv");
  CHECK(share::shareUrl("192.168.1.20", 40123, "/abc/r.csv") ==
        "http://192.168.1.20:40123/abc/r.csv");
  CHECK(share::kMaxBytes == 96u * 1024u * 1024u);
  CHECK(share::kLifetimeSeconds == 600);
}

TEST_CASE("share: the LAN address") {
  using I = share::Interface;
  CHECK_FALSE(share::pickAddress({}));
  CHECK_FALSE(share::pickAddress({I{"lo", "127.0.0.1", true, true}}));
  // Down, loopback, link-local and Wi-Fi Direct are skipped.
  CHECK(share::pickAddress({I{"lo", "127.0.0.1", true, true}, I{"eth0", "192.168.1.20", false, false},
                            I{"wlan0", "169.254.3.4", true, false}, I{"p2p0", "192.168.49.1", true, false},
                            I{"wlan0", "10.1.2.3", true, false}}) == std::string("10.1.2.3"));
  // A private address wins over a public one, whatever the order.
  CHECK(share::pickAddress({I{"eth0", "203.0.113.9", true, false}, I{"wlan0", "172.20.0.5", true, false}}) ==
        std::string("172.20.0.5"));
  CHECK(share::pickAddress({I{"eth0", "172.32.0.5", true, false}}) == std::string("172.32.0.5"));
  // The first private one in interface order.
  CHECK(share::pickAddress({I{"eth0", "192.168.0.10", true, false}, I{"wlan0", "192.168.0.11", true, false}}) ==
        std::string("192.168.0.10"));
  CHECK_FALSE(share::pickAddress({I{"eth0", "not an address", true, false}, I{"eth1", "0.0.0.0", true, false},
                                  I{"eth2", "1.2.3", true, false}, I{"eth3", "1.2.3.256", true, false}}));
}
