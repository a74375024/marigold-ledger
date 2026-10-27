#include "core/util/log.hpp"

#include <string>
#include <thread>
#include <vector>

#include "doctest.h"

TEST_CASE("log lines are redacted") {
  MLOG_I("test", "tuning %s", "http://example.invalid/live/u/p/9.ts");
  auto lines = marigold::log::recent(1);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].find("example.invalid") == std::string::npos);
  CHECK(lines[0].find("[link]") != std::string::npos);
}

TEST_CASE("logging from many threads") {
  std::vector<std::thread> threads;
  for (int t = 0; t < 8; ++t) {
    threads.emplace_back([t] {
      for (int i = 0; i < 200; ++i) MLOG_D("thread", "t=%d i=%d", t, i);
    });
  }
  for (auto& th : threads) th.join();
  CHECK(marigold::log::recent(500).size() == 200);  // the ring keeps the newest 200
}

namespace {
std::vector<std::string> gMirrored;
void mirrorInto(marigold::log::Level, const std::string& line) { gMirrored.push_back(line); }
}  // namespace

TEST_CASE("the mirror gets each line, redacted") {
  gMirrored.clear();
  marigold::log::setMirror(mirrorInto);
  MLOG_I("test", "playing http://example.invalid/live/user/pass/1.ts");
  marigold::log::setMirror(nullptr);
  MLOG_I("test", "not mirrored");
  REQUIRE(gMirrored.size() == 1);
  CHECK(gMirrored[0].find("example.invalid") == std::string::npos);
  CHECK(gMirrored[0].find("[test]") != std::string::npos);
}
