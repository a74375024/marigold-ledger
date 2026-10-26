#include <zlib.h>

#include <string>

#include "core/util/gunzip.hpp"
#include "doctest.h"

using marigold::gz::Gunzip;

namespace {

std::string gzip(const std::string& in) {
  z_stream s{};
  REQUIRE(deflateInit2(&s, 6, Z_DEFLATED, 16 + 15, 8, Z_DEFAULT_STRATEGY) == Z_OK);
  std::string out(deflateBound(&s, in.size()) + 32, '\0');
  s.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
  s.avail_in = static_cast<uInt>(in.size());
  s.next_out = reinterpret_cast<Bytef*>(&out[0]);
  s.avail_out = static_cast<uInt>(out.size());
  REQUIRE(deflate(&s, Z_FINISH) == Z_STREAM_END);
  out.resize(out.size() - s.avail_out);
  deflateEnd(&s);
  return out;
}

std::string sample(size_t n) {
  std::string s;
  while (s.size() < n) s += "<programme channel=\"a.example\" start=\"20260101000000 +0000\"/>\n";
  s.resize(n);
  return s;
}

std::string run(const std::string& in, size_t step, bool* ok = nullptr, size_t cap = size_t(1) << 30) {
  std::string out;
  Gunzip g([&](std::string_view c) { out.append(c); return true; }, cap);
  bool good = true;
  for (size_t i = 0; i < in.size() && good; i += step) good = g.feed(std::string_view(in).substr(i, step));
  good = good && g.finish();
  if (ok) *ok = good;
  return out;
}

}  // namespace

TEST_CASE("gunzip inflates in any chunk size") {
  std::string plain = sample(300000);
  std::string z = gzip(plain);
  for (size_t step : {size_t(1), size_t(7), size_t(4096), z.size()}) {
    bool ok = false;
    CHECK(run(z, step, &ok) == plain);
    CHECK(ok);
  }
}

TEST_CASE("gunzip reads concatenated members and trailing zeros") {
  std::string z = gzip("first,") + gzip("second") + std::string(8, '\0');
  bool ok = false;
  CHECK(run(z, 5, &ok) == "first,second");
  CHECK(ok);
}

TEST_CASE("gunzip passes plain data through") {
  bool ok = false;
  CHECK(run("<tv></tv>", 1, &ok) == "<tv></tv>");
  CHECK(ok);
  CHECK(run("x", 1, &ok) == "x");
  CHECK(ok);
}

TEST_CASE("gunzip reports truncation, corruption and the cap") {
  std::string z = gzip(sample(10000));
  bool ok = true;
  run(z.substr(0, z.size() / 2), 100, &ok);
  CHECK_FALSE(ok);
  std::string bad = z;
  bad[20] = static_cast<char>(bad[20] ^ 0x55);
  bad[21] = static_cast<char>(bad[21] ^ 0x55);
  run(bad, 100, &ok);
  CHECK_FALSE(ok);
  run(z, 100, &ok, 5000);
  CHECK_FALSE(ok);
}

TEST_CASE("gunzip stops when the sink says so") {
  std::string z = gzip(sample(200000));
  int calls = 0;
  Gunzip g([&](std::string_view) { return ++calls < 2; });
  bool good = true;
  for (size_t i = 0; i < z.size() && good; i += 1000) good = g.feed(std::string_view(z).substr(i, 1000));
  CHECK_FALSE(good);
  CHECK(calls == 2);
}
