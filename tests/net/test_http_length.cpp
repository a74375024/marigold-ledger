// http::wireLength: the size a server gives for a body, for the download
// bars (Build 130).
#include <doctest.h>

#include "core/net/http.hpp"

using marigold::http::wireLength;

TEST_CASE("http: the body's size comes from Content-Length") {
  CHECK(wireLength("HTTP/1.1 200 OK\r\nContent-Type: application/xml\r\nContent-Length: 48213\r\n\r\n") == 48213);
  CHECK(wireLength("HTTP/1.1 200 OK\r\ncontent-length:   7\r\n") == 7);
  CHECK(wireLength("HTTP/2 200\nContent-Length: 12\n") == 12);
}

TEST_CASE("http: no size when none is given or it can't be read") {
  CHECK(wireLength("") == 0);
  CHECK(wireLength("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n") == 0);
  CHECK(wireLength("HTTP/1.1 200 OK\r\nContent-Length: 12a\r\n") == 0);
  CHECK(wireLength("HTTP/1.1 200 OK\r\nContent-Length: 99999999999999999999999\r\n") == 0);
}

TEST_CASE("http: a body decoded on the way has no size to compare with") {
  CHECK(wireLength("HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: 900\r\n") == 0);
  CHECK(wireLength("HTTP/1.1 200 OK\r\nContent-Encoding: identity\r\nContent-Length: 900\r\n") == 900);
}

TEST_CASE("http: the size is the last response's, after a redirect") {
  CHECK(wireLength("HTTP/1.1 302 Found\r\nContent-Length: 0\r\nLocation: /x\r\n\r\n"
                   "HTTP/1.1 200 OK\r\nContent-Length: 4096\r\n\r\n") == 4096);
  CHECK(wireLength("HTTP/1.1 301 Moved\r\nContent-Length: 120\r\nContent-Encoding: gzip\r\n\r\n"
                   "HTTP/1.1 200 OK\r\n\r\n") == 0);
  CHECK(wireLength("HTTP/1.1 302 Found\r\nContent-Encoding: gzip\r\n\r\n"
                   "HTTP/1.1 200 OK\r\nContent-Length: 50\r\n\r\n") == 50);
}
