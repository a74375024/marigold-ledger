#include "core/util/base64.hpp"

#include "doctest.h"

using marigold::base64::decode;
using marigold::base64::decodeLenient;
using marigold::base64::encode;

TEST_CASE("base64 strict decoding") {
  CHECK(decode("") == std::string());
  CHECK(decode("TQ==") == std::string("M"));
  CHECK(decode("TWE=") == std::string("Ma"));
  CHECK(decode("TWFu") == std::string("Man"));
  CHECK(decode("TmV3cyBhdCA2") == std::string("News at 6"));
  // UTF-8 survives: "Fútbol".
  CHECK(decode("RsO6dGJvbA==") == std::string("F\xC3\xBAtbol"));
  // Binary, including NUL and high bytes.
  CHECK(decode("AP8=") == std::string("\x00\xFF", 2));
  CHECK(decode("+/+/") == std::string("\xFB\xFF\xBF"));
}

TEST_CASE("base64 strict decoding rejects what Foundation rejects") {
  CHECK_FALSE(decode("TQ"));        // missing padding
  CHECK_FALSE(decode("TQ="));       // length not a multiple of 4
  CHECK_FALSE(decode("T==="));      // three pads
  CHECK_FALSE(decode("===="));
  CHECK_FALSE(decode("TQ==TQ=="));  // padding in the middle
  CHECK_FALSE(decode("TW E="));     // whitespace
  CHECK_FALSE(decode("TWE=\n"));
  CHECK_FALSE(decode("-_-_"));      // URL-safe alphabet
  CHECK_FALSE(decode("T*E="));
}

TEST_CASE("base64 plain titles that look like base64") {
  // "Test" is valid base64 but decodes to bytes that are not UTF-8; callers
  // check UTF-8 and keep the original text (see Xtream short EPG).
  auto bytes = decode("Test");
  REQUIRE(bytes);
  CHECK(bytes->size() == 3);
}

TEST_CASE("base64 lenient decoding") {
  CHECK(decodeLenient("TQ") == std::string("M"));
  CHECK(decodeLenient("TWE") == std::string("Ma"));
  CHECK(decodeLenient(" TW\nFu\r\n") == std::string("Man"));
  CHECK(decodeLenient("-_-_") == std::string("\xFB\xFF\xBF"));
  CHECK(decodeLenient("TWE=") == std::string("Ma"));
  CHECK_FALSE(decodeLenient("T"));      // a dangling digit
  CHECK_FALSE(decodeLenient("TQ=x"));   // data after padding
  CHECK_FALSE(decodeLenient("TQ="));    // padding that doesn't complete a group
  CHECK_FALSE(decodeLenient("T==="));
  CHECK_FALSE(decodeLenient("T*E="));
}

TEST_CASE("base64 round trip") {
  std::string all;
  for (int i = 0; i < 256; ++i) all += static_cast<char>(i);
  for (size_t len = 0; len <= all.size(); len += 7) {
    std::string part = all.substr(0, len);
    std::string text = encode(part);
    CHECK(text.size() % 4 == 0);
    CHECK(decode(text) == part);
    CHECK(decodeLenient(text) == part);
  }
  CHECK(encode("M") == "TQ==");
  CHECK(encode("Ma") == "TWE=");
}
