#include "core/util/redact.hpp"

#include "doctest.h"

using marigold::containsSecret;
using marigold::redact;

TEST_CASE("links become [link]") {
  CHECK(redact("open http://example.invalid:8080/live/u/p/1.ts now") == "open [link] now");
  CHECK(redact("https://example.invalid/a?b=c") == "[link]");
  CHECK(redact("\"rtmp://example.invalid/x\"") == "\"[link]\"");
  CHECK(redact("two http://a.invalid/1 and https://b.invalid/2") == "two [link] and [link]");
}

TEST_CASE("bare credential parameters lose their value") {
  CHECK(redact("username=alice&password=s3cret&type=m3u") ==
        "username=***&password=***&type=m3u");
  CHECK(redact("token=abc def") == "token=*** def");
  CHECK(redact("Password=x") == "Password=***");
}

TEST_CASE("ordinary text is unchanged") {
  CHECK(redact("monkey=3 colon: no link") == "monkey=3 colon: no link");
  CHECK(redact("ratio 3:1, time 20:30") == "ratio 3:1, time 20:30");
  CHECK(redact("") == "");
  CHECK_FALSE(containsSecret("Chiefs at Chargers 8:15 PM"));
  CHECK(containsSecret("see http://example.invalid"));
}

TEST_CASE("a lone :// with no scheme is kept") {
  CHECK(redact("://x") == "://x");
}
