#include "core/sources/common.hpp"

#include "doctest.h"

using namespace marigold;
using namespace marigold::sources;

namespace {
json::Value parsed(const char* text) { return json::parse(text).value_or(json::Value()); }
}  // namespace

TEST_CASE("sources: lenientInt reads what XtreamClient.int reads") {
  CHECK(lenientInt(json::Value(5)) == 5);
  CHECK(lenientInt(json::Value(5.9)) == 5);    // NSNumber.intValue truncates
  CHECK(lenientInt(json::Value(-5.9)) == -5);
  CHECK(lenientInt(json::Value(true)) == 1);
  CHECK(lenientInt(json::Value("42")) == 42);
  CHECK(lenientInt(json::Value("+42")) == 42);
  CHECK(lenientInt(json::Value("-7")) == -7);
  CHECK(lenientInt(json::Value("1767225600")) == 1767225600);
  CHECK(lenientInt(json::Value("1.9")) == 1);  // Int() fails, Double() reads it
  CHECK(lenientInt(json::Value("1e3")) == 1000);
  CHECK(lenientInt(json::Value(".5")) == 0);
  CHECK(lenientInt(json::Value("9223372036854775807")) == INT64_MAX);
  CHECK_FALSE(lenientInt(json::Value("")));
  CHECK_FALSE(lenientInt(json::Value(" 5")));  // Swift rejects surrounding spaces
  CHECK_FALSE(lenientInt(json::Value("5 ")));
  CHECK_FALSE(lenientInt(json::Value("abc")));
  CHECK_FALSE(lenientInt(json::Value("0x1A")));
  CHECK_FALSE(lenientInt(json::Value("inf")));
  CHECK_FALSE(lenientInt(json::Value("nan")));
  CHECK_FALSE(lenientInt(json::Value("1e400")));  // would trap in Swift
  CHECK_FALSE(lenientInt(json::Value(1e30)));
  CHECK_FALSE(lenientInt(json::Value()));
  CHECK_FALSE(lenientInt(parsed("[1]")));
}

TEST_CASE("sources: lenientString reads what XtreamClient.string reads") {
  CHECK(lenientString(json::Value("abc")) == std::string("abc"));
  CHECK_FALSE(lenientString(json::Value("")));
  CHECK(lenientString(json::Value(100005)) == std::string("100005"));
  CHECK(lenientString(json::Value(1.5)) == std::string("1.5"));
  CHECK(lenientString(json::Value(0.1)) == std::string("0.1"));
  CHECK(lenientString(json::Value(true)) == std::string("1"));
  CHECK_FALSE(lenientString(json::Value()));
  CHECK_FALSE(lenientString(parsed("{}")));
  CHECK(stringField(json::Value("x")) == std::string("x"));
  CHECK_FALSE(stringField(json::Value(3)));
  CHECK_FALSE(stringField(json::Value("")));
}

TEST_CASE("sources: Swift whitespace trimming") {
  CHECK(trimWhitespaceAndNewlines("  ESPN \n") == "ESPN");
  CHECK(trimWhitespaceAndNewlines("\xC2\xA0" "ESPN\xE2\x80\x83") == "ESPN");  // NBSP, em space
  CHECK(trimWhitespaceAndNewlines("\xE2\x80\xA8x\r\n\t") == "x");              // line separator
  CHECK(trimWhitespaceAndNewlines(" \n ") == "");
  CHECK(trimWhitespaceAndNewlines("caf\xC3\xA9") == "caf\xC3\xA9");
  CHECK(trimWhitespace(" 4 \n") == "4 \n");  // .whitespaces keeps line breaks
  CHECK(trimWhitespace("\xC2\xA0 4\t") == "4");
  // Invalid UTF-8 at the edges is kept, not misread.
  CHECK(trimWhitespaceAndNewlines("\xC2 x \xE2\x80") == "\xC2 x \xE2\x80");
}

TEST_CASE("sources: percent encoding and queries") {
  CHECK(percentEncode("aZ09-._~") == "aZ09-._~");
  CHECK(percentEncode("a b+c/d?e&f=g@h:i#") == "a%20b%2Bc%2Fd%3Fe%26f%3Dg%40h%3Ai%23");
  CHECK(percentEncode("\xC3\xA9") == "%C3%A9");
  CHECK(queryString({{"b", "2"}, {"a", "1"}, {"c", "x y"}}) == "a=1&b=2&c=x%20y");
  CHECK(queryString({}) == "");
}

TEST_CASE("sources: base addresses") {
  CHECK(isValidBaseUrl("http://provider.example.invalid:8080"));
  CHECK(isValidBaseUrl("https://emby.example.invalid/emby"));
  CHECK(isValidBaseUrl("HTTP://x.invalid"));
  CHECK_FALSE(isValidBaseUrl("ftp://x.invalid"));
  CHECK_FALSE(isValidBaseUrl("http://"));
  CHECK_FALSE(isValidBaseUrl("http://:80"));
  CHECK_FALSE(isValidBaseUrl("http://a b.invalid"));
  CHECK_FALSE(isValidBaseUrl("http://x.invalid/get.php?type=m3u"));
  CHECK_FALSE(isValidBaseUrl("http://x.invalid/#frag"));
  CHECK_FALSE(isValidBaseUrl("http://u:p@x.invalid"));
  CHECK(basePath("https://emby.example.invalid/emby") == "/emby");
  CHECK(basePath("https://emby.example.invalid") == "");
}

TEST_CASE("sources: item ids") {
  CHECK(isSafeItemId("100005"));
  CHECK(isSafeItemId("a1b2c3d4e5f60718293a4b5c6d7e8f90"));
  CHECK(isSafeItemId("ch.1_2-3"));
  CHECK_FALSE(isSafeItemId(""));
  CHECK_FALSE(isSafeItemId("."));
  CHECK_FALSE(isSafeItemId(".."));
  CHECK_FALSE(isSafeItemId("1/../2"));
  CHECK_FALSE(isSafeItemId("1|2"));
  CHECK_FALSE(isSafeItemId("1 2"));
  CHECK_FALSE(isSafeItemId("1?x=2"));
  CHECK_FALSE(isSafeItemId(std::string(kMaxItemIdBytes + 1, '1')));
}

TEST_CASE("sources: internet date-time") {
  CHECK(parseInternetDateTime("2026-10-21T20:00:00Z") == 1792612800);
  CHECK(parseInternetDateTime("2026-10-21T22:00:00+02:00") == 1792612800);
  CHECK(parseInternetDateTime("2026-10-21T16:00:00-0400") == 1792612800);
  CHECK(parseInternetDateTime("2026-10-21t20:00:00z") == 1792612800);
  CHECK(parseInternetDateTime("2024-02-29T00:00:00Z"));
  CHECK_FALSE(parseInternetDateTime("2025-02-29T00:00:00Z"));
  CHECK_FALSE(parseInternetDateTime("2026-13-01T00:00:00Z"));
  CHECK_FALSE(parseInternetDateTime("2026-10-21T24:00:00Z"));
  CHECK_FALSE(parseInternetDateTime("2026-10-21T20:00:00"));
  CHECK_FALSE(parseInternetDateTime("2026-10-21T20:00:00.5Z"));
  CHECK_FALSE(parseInternetDateTime("2026-10-21 20:00:00Z"));
  CHECK_FALSE(parseInternetDateTime("2026-10-21T20:00:00+2:00"));
  CHECK_FALSE(parseInternetDateTime(""));
  CHECK(formatInternetDateTime(1792612800) == "2026-10-21T20:00:00Z");
}

TEST_CASE("sources: errors are redacted") {
  Error e = Error::make("Failed to open http://example.invalid:8080/live/u/p/1.ts (password=p)", 500);
  CHECK(e.status == 500);
  CHECK(e.message.find("example.invalid") == std::string::npos);
  CHECK(e.message.find("/live/") == std::string::npos);
  CHECK(e.message.find("password=p") == std::string::npos);
  CHECK(e.message.find("[link]") != std::string::npos);

  Request r;
  r.headers.push_back({"X-Emby-Token", "t"});
  CHECK(r.header("x-emby-token") == "t");
  CHECK(r.header("Accept") == "");
}
