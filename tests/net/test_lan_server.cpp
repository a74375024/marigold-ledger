// The phone sign-in server on the loopback address: a real socket, one
// request per connection, the handler's answer, and stop().
#include <doctest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>

#include "platform/lan_server.hpp"

using namespace marigold;

namespace {

std::string ask(int port, const std::string& request) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(static_cast<uint16_t>(port));
  ::inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
    ::close(fd);
    return "";
  }
  ::send(fd, request.data(), request.size(), 0);
  std::string out;
  char buf[4096];
  ssize_t n;
  while ((n = ::recv(fd, buf, sizeof buf, 0)) > 0) out.append(buf, static_cast<size_t>(n));
  ::close(fd);
  return out;
}

}  // namespace

TEST_CASE("lan server: answers on loopback, refuses junk, stops") {
  platform::LanServer server;
  platform::LanServer::Options o;
  o.host = "127.0.0.1";
  o.lifetimeSeconds = 30;
  auto st = server.start(
      [](const lan::Request& r) {
        if (r.path == "/hi") return lan::Response::html("hello " + r.body);
        return lan::Response::notFound();
      },
      o);
  REQUIRE(st.state == platform::LanServer::State::Ready);
  CHECK(st.port > 0);
  CHECK(st.base() == "http://127.0.0.1:" + std::to_string(st.port));
  CHECK(st.secondsLeft > 25);

  std::string a = ask(st.port, "POST /hi HTTP/1.1\r\nContent-Length: 3\r\n\r\nyou");
  CHECK(a.rfind("HTTP/1.1 200 OK", 0) == 0);
  CHECK(a.find("\r\n\r\nhello you") != std::string::npos);
  CHECK(ask(st.port, "GET /else HTTP/1.1\r\n\r\n").rfind("HTTP/1.1 404", 0) == 0);
  CHECK(ask(st.port, "NONSENSE\r\n\r\n").rfind("HTTP/1.1 400", 0) == 0);

  server.stop();
  CHECK(server.status().state == platform::LanServer::State::Stopped);
  CHECK(ask(st.port, "GET /hi HTTP/1.1\r\n\r\n").empty());
  CHECK(platform::randomHex(16).size() == 32);
  CHECK(platform::randomHex(16) != platform::randomHex(16));
}
