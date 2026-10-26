#include "core/util/json.hpp"

#include "doctest.h"

using marigold::json::Value;

TEST_CASE("json parse and lenient readers") {
  auto v = marigold::json::parse(R"({"user_info":{"auth":1,"exp_date":"1767225600","max_connections":"2"},
                                    "list":[1,"two",{"x":null}],"t":true,"s":"café 😀"})");
  REQUIRE(v);
  CHECK((*v)["user_info"]["auth"].asInt() == 1);
  CHECK((*v)["user_info"]["exp_date"].asInt() == 1767225600);
  CHECK((*v)["user_info"]["max_connections"].asInt() == 2);
  CHECK((*v)["list"].size() == 3);
  CHECK((*v)["list"][1].asString() == "two");
  CHECK((*v)["list"][2]["x"].isNull());
  CHECK((*v)["missing"]["deeper"].isNull());
  CHECK((*v)["t"].asBool());
  CHECK((*v)["s"].asString() == "caf\xc3\xa9 \xf0\x9f\x98\x80");
  CHECK((*v)["list"][9].isNull());
}

TEST_CASE("json rejects malformed input") {
  CHECK_FALSE(marigold::json::parse("{\"a\":}"));
  CHECK_FALSE(marigold::json::parse("[1,2"));
  CHECK_FALSE(marigold::json::parse("{} trailing"));
  std::string deep(200, '[');
  CHECK_FALSE(marigold::json::parse(deep));
}

TEST_CASE("json write round trip") {
  Value o;
  o.set("name", "ESPN").set("n", 3).set("ok", true);
  o.set("tags", Value::array());
  Value list;
  list.push("a").push(1.5);
  o.set("list", list);
  std::string text = marigold::json::write(o);
  CHECK(text == R"({"list":["a",1.5],"n":3,"name":"ESPN","ok":true,"tags":[]})");
  auto back = marigold::json::parse(text);
  REQUIRE(back);
  CHECK((*back)["list"][1].asNumber() == doctest::Approx(1.5));
  // Copies share storage until written.
  Value copy = o;
  copy.set("name", "FS1");
  CHECK(o["name"].asString() == "ESPN");
  CHECK(marigold::json::escape("a\"b\n") == "a\\\"b\\n");
}
