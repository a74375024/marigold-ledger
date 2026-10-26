#include "core/util/json_stream.hpp"

#include <algorithm>
#include <random>
#include <string>
#include <vector>

#include "doctest.h"

using marigold::json::ArrayLimits;
using marigold::json::ArrayReader;
using marigold::json::StreamHandler;
using marigold::json::StreamLimits;
using marigold::json::StreamReader;
using marigold::json::Value;

namespace {

// Writes every event as text, so two runs can be compared.
class Trace final : public StreamHandler {
 public:
  std::string out;
  int stopAfter = -1;  // stop after this many events

  bool onNull() override { return add("n"); }
  bool onBool(bool v) override { return add(v ? "T" : "F"); }
  bool onNumber(double v) override { return add("#" + std::to_string(v)); }
  bool onString(std::string_view v) override { return add("s(" + std::string(v) + ")"); }
  bool onKey(std::string_view k) override { return add("k(" + std::string(k) + ")"); }
  bool onStartObject() override { return add("{"); }
  bool onEndObject() override { return add("}"); }
  bool onStartArray() override { return add("["); }
  bool onEndArray() override { return add("]"); }

 private:
  bool add(const std::string& s) {
    out += s;
    out += ' ';
    if (stopAfter >= 0 && --stopAfter < 0) return false;
    return true;
  }
};

struct Run {
  bool ok = false;
  std::string trace;
  std::string error;
};

// Feeds `text` cut at the given sizes (cycled), then finishes.
Run runChunks(std::string_view text, const std::vector<size_t>& sizes, StreamLimits limits = {}) {
  Trace t;
  StreamReader r(t, limits);
  size_t i = 0, k = 0;
  bool ok = true;
  while (i < text.size() && ok) {
    size_t n = sizes.empty() ? text.size() : std::max<size_t>(1, sizes[k++ % sizes.size()]);
    ok = r.feed(text.substr(i, n));
    i += n;
  }
  Run run;
  run.ok = ok && r.finish();
  run.trace = t.out;
  run.error = r.error();
  return run;
}

Run runWhole(std::string_view text, StreamLimits limits = {}) { return runChunks(text, {}, limits); }
Run runBytes(std::string_view text) { return runChunks(text, {1}); }

// Every two-piece split of `text` gives the same events as one feed.
void checkAllSplits(std::string_view text) {
  Run whole = runWhole(text);
  for (size_t cut = 0; cut <= text.size(); ++cut) {
    Trace t;
    StreamReader r(t);
    bool ok = r.feed(text.substr(0, cut)) && r.feed(text.substr(cut)) && r.finish();
    CHECK(ok == whole.ok);
    CHECK(t.out == whole.trace);
  }
  Run bytes = runBytes(text);
  CHECK(bytes.ok == whole.ok);
  CHECK(bytes.trace == whole.trace);
}

std::vector<Value> elements(std::string_view text, const std::vector<size_t>& sizes,
                            std::vector<std::string> path = {}, bool* ok = nullptr, bool* found = nullptr) {
  std::vector<Value> out;
  ArrayReader r([&](Value&& v) {
    out.push_back(std::move(v));
    return true;
  }, std::move(path));
  size_t i = 0, k = 0;
  bool good = true;
  while (i < text.size() && good) {
    size_t n = sizes.empty() ? text.size() : sizes[k++ % sizes.size()];
    good = r.feed(text.substr(i, n));
    i += n;
  }
  good = good && r.finish();
  if (ok) *ok = good;
  if (found) *found = r.foundArray();
  return out;
}

}  // namespace

TEST_CASE("json stream: events for a small document") {
  Run run = runWhole(R"({"a":[1,-2.5e2,true,false,null],"b":{"c":"x"},"d":""})");
  CHECK(run.ok);
  CHECK(run.trace ==
        "{ k(a) [ #1.000000 #-250.000000 T F n ] k(b) { k(c) s(x) } k(d) s() } ");
  CHECK(runWhole("  42  ").ok);
  CHECK(runWhole("\"top\"").trace == "s(top) ");
  CHECK(runWhole("[]").trace == "[ ] ");
  CHECK(runWhole("{}").trace == "{ } ");
}

TEST_CASE("json stream: any chunk boundary gives the same events") {
  checkAllSplits(R"([{"name":"US: ESPN FHD","num":12,"x":-0.5e-3,"ok":true,"no":null},[],{}])");
  checkAllSplits(R"(["esc \"q\" \\ \/ \b\f\n\r\t", "\u00e9\u4e2d", "\ud83d\ude00", "tail"])");
  checkAllSplits("[123456789012, 0, -0, 1E+2, 3.25]");
  checkAllSplits("\xEF\xBB\xBF[true,false,null]");
  // Malformed input is reported the same way wherever it is cut.
  checkAllSplits("[1, 2,, 3]");
  checkAllSplits("[\"\\ud83d\"]");
}

TEST_CASE("json stream: escapes and surrogate pairs") {
  CHECK(runBytes(R"(["\ud83d\ude00"])").trace == "[ s(\xF0\x9F\x98\x80) ] ");
  CHECK(runBytes(R"(["\u00E9"])").trace == "[ s(\xC3\xA9) ] ");
  // Lone halves become U+FFFD; what follows them is kept.
  CHECK(runBytes(R"(["\ud83d"])").trace == "[ s(\xEF\xBF\xBD) ] ");
  CHECK(runBytes(R"(["\ud83dx"])").trace == "[ s(\xEF\xBF\xBDx) ] ");
  CHECK(runBytes(R"(["\ud83d\n"])").trace == "[ s(\xEF\xBF\xBD\n) ] ");
  CHECK(runBytes(R"(["\ud83d\u0041"])").trace == "[ s(\xEF\xBF\xBD" "A) ] ");
  CHECK(runBytes(R"(["\ude00"])").trace == "[ s(\xEF\xBF\xBD) ] ");
  CHECK(runBytes(R"(["\ud83d\ud83d\ude00"])").trace == "[ s(\xEF\xBF\xBD\xF0\x9F\x98\x80) ] ");
  // \u0000 is kept as a NUL byte.
  Run nul = runWhole(R"(["a\u0000b"])");
  CHECK(nul.ok);
  CHECK(nul.trace == std::string("[ s(a\0b) ] ", 11));
}

TEST_CASE("json stream: malformed input fails without crashing") {
  const char* bad[] = {
      "", "   ", "[", "]", "[1,]", "[,1]", "[01]", "[1.]", "[.5]", "[1e]", "[+1]", "[-]", "[1 2]",
      "{\"a\" 1}", "{\"a\":}", "{a:1}", "{\"a\":1,}", "[\"\\x\"]", "[\"\\u12G4\"]", "[\"abc",
      "[tru]", "[nul", "[true false]", "[truex]", "[1] 2", "[1]]", "{\"a\":1}}", "\xEF\xBB[1]",
      "\xEF[1]", "[\"\\", "[\"\\u12", "[1}", "{\"a\":1]", "nan", "[Infinity]", "[0x10]",
  };
  for (const char* text : bad) {
    CAPTURE(text);
    Run whole = runWhole(text);
    CHECK_FALSE(whole.ok);
    CHECK_FALSE(runBytes(text).ok);
    if (*text) CHECK_FALSE(whole.error.empty());
  }
}

TEST_CASE("json stream: errors give a position, never the input") {
  Run run = runWhole(R"(["http://example.invalid/live/u/p/1.ts" oops])");
  CHECK_FALSE(run.ok);
  CHECK(run.error.find("byte") != std::string::npos);
  CHECK(run.error.find("example") == std::string::npos);
  CHECK(run.error.find("oops") == std::string::npos);
}

TEST_CASE("json stream: depth and token limits") {
  std::string deep(65, '[');
  deep += std::string(65, ']');
  CHECK_FALSE(runWhole(deep).ok);
  std::string ok(64, '[');
  ok += std::string(64, ']');
  CHECK(runWhole(ok).ok);
  std::string deep2(100000, '[');  // no recursion: a huge depth is just an error
  CHECK_FALSE(runChunks(deep2, {4096}).ok);

  StreamLimits small;
  small.maxTokenBytes = 16;
  CHECK(runWhole("[\"0123456789abcdef\"]", small).ok);
  CHECK_FALSE(runWhole("[\"0123456789abcdefg\"]", small).ok);
  CHECK_FALSE(runChunks("[\"0123456789abcdefg\"]", {1}, small).ok);
  CHECK_FALSE(runWhole("[12345678901234567]", small).ok);
}

TEST_CASE("json stream: the handler can stop") {
  Trace t;
  t.stopAfter = 2;
  StreamReader r(t);
  CHECK_FALSE(r.feed("[1,2,3]"));
  CHECK(r.status() == StreamReader::Status::Stopped);
  CHECK_FALSE(r.feed("]"));
  CHECK_FALSE(r.finish());
  CHECK(t.out == "[ #1.000000 #2.000000 ");
}

TEST_CASE("json stream: random cuts and mutations never crash and agree") {
  const std::string base =
      R"({"Items":[{"Id":"a1","Name":"UK| Sky Sports Main \u00e9","Tags":["x",{"Name":"y"}],"N":3.5},)"
      R"({"Id":"b2","Name":"\ud83c\udfc8 NCAAF 16","UserData":{"IsFavorite":true}}],"Total":2})";
  std::mt19937 rng(7);
  for (int round = 0; round < 400; ++round) {
    std::string text = base;
    int edits = static_cast<int>(rng() % 4);
    for (int e = 0; e < edits; ++e) {
      size_t at = rng() % text.size();
      switch (rng() % 3) {
        case 0: text[at] = static_cast<char>(rng() % 256); break;
        case 1: text.erase(at, 1); break;
        default: text.insert(at, 1, "\"\\{}[],:u0-e"[rng() % 12]); break;
      }
    }
    std::vector<size_t> sizes;
    for (int k = 0; k < 8; ++k) sizes.push_back(1 + rng() % 9);
    Run whole = runWhole(text);
    Run cut = runChunks(text, sizes);
    CHECK(whole.ok == cut.ok);
    CHECK(whole.trace == cut.trace);
    // The element reader agrees too.
    bool ok1 = false, ok2 = false;
    auto a = elements(text, {}, {"Items"}, &ok1);
    auto b = elements(text, sizes, {"Items"}, &ok2);
    CHECK(ok1 == ok2);
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i) CHECK(marigold::json::write(a[i]) == marigold::json::write(b[i]));
  }
}

TEST_CASE("array reader: top-level array elements match json::parse") {
  std::string text = R"([{"stream_id":1,"name":"US: ESPN FHD","tags":["a","b"],"o":{"x":[1,{"y":null}]}},)"
                     R"("plain",7,null,[],{},{"name":"\u00c9\ud83d\ude00"}])";
  auto whole = marigold::json::parse(text);
  REQUIRE(whole);
  for (std::vector<size_t> sizes : {std::vector<size_t>{}, {1}, {3, 5, 7}}) {
    bool ok = false, found = false;
    auto list = elements(text, sizes, {}, &ok, &found);
    CHECK(ok);
    CHECK(found);
    REQUIRE(list.size() == whole->size());
    for (size_t i = 0; i < list.size(); ++i)
      CHECK(marigold::json::write(list[i]) == marigold::json::write((*whole)[i]));
  }
}

TEST_CASE("array reader: an array under a key path") {
  std::string text = R"({"TotalRecordCount":2,"Skip":{"Items":[9]},"Items":[{"Id":"1"},{"Id":"2"}],"After":[0]})";
  bool ok = false, found = false;
  auto list = elements(text, {1}, {"Items"}, &ok, &found);
  CHECK(ok);
  CHECK(found);
  REQUIRE(list.size() == 2);
  CHECK(list[1]["Id"].asString() == "2");

  auto nested = elements(R"({"a":{"x":1,"b":[[1],[2,3]]},"b":[0]})", {2}, {"a", "b"}, &ok, &found);
  CHECK(ok);
  CHECK(found);
  REQUIRE(nested.size() == 2);
  CHECK(nested[1].size() == 2);

  // Missing, null or not an array: read fine, nothing found.
  CHECK(elements(R"({"Other":[1]})", {}, {"Items"}, &ok, &found).empty());
  CHECK(ok);
  CHECK_FALSE(found);
  CHECK(elements(R"({"Items":null})", {}, {"Items"}, &ok, &found).empty());
  CHECK_FALSE(found);
  CHECK(elements(R"({"Items":{"0":1}})", {}, {"Items"}, &ok, &found).empty());
  CHECK_FALSE(found);
  CHECK(elements(R"([{"Items":[1]}])", {}, {"Items"}, &ok, &found).empty());
  CHECK_FALSE(found);
  CHECK(elements(R"({"x":1})", {}, {}, &ok, &found).empty());
  CHECK_FALSE(found);
}

TEST_CASE("array reader: stops, element cap and bounded retention") {
  int seen = 0;
  ArrayReader stop([&](Value&&) { return ++seen < 2; });
  CHECK_FALSE(stop.feed("[1,2,3,4]"));
  CHECK(seen == 2);
  CHECK(stop.status() == StreamReader::Status::Stopped);

  ArrayLimits limits;
  limits.maxElementBytes = 200;
  ArrayReader capped([](Value&&) { return true; }, {}, limits);
  std::string big = "[{\"a\":\"" + std::string(500, 'x') + "\"}]";
  CHECK_FALSE(capped.feed(big));
  CHECK(capped.status() == StreamReader::Status::Failed);
  CHECK(capped.error().find("too large") != std::string::npos);
  CHECK(capped.error().find("xxx") == std::string::npos);

  // Many small elements: the reader keeps one at a time.
  std::string many = "[";
  for (int i = 0; i < 2000; ++i) many += (i ? "," : "") + std::string(R"({"name":"Channel )") + std::to_string(i) + "\"}";
  many += "]";
  size_t count = 0;
  ArrayReader r([&](Value&&) {
    ++count;
    return true;
  });
  for (size_t i = 0; i < many.size(); i += 100) CHECK(r.feed(std::string_view(many).substr(i, 100)));
  CHECK(r.finish());
  CHECK(count == 2000);
  CHECK(r.peakElementBytes() < 400);
  CHECK(r.peakTokenBytes() < 256);
}
