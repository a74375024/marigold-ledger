#include "core/util/json_doc.hpp"

#include <map>
#include <vector>

#include "doctest.h"

using namespace marigold;

TEST_CASE("json writer: the same bytes as write() of the whole document") {
  json::Value doc = json::Value::object();
  doc.set("a", 1);
  json::Value list = json::Value::array();
  for (int i = 0; i < 3; ++i) {
    json::Value item = json::Value::object();
    item.set("n", i);
    item.set("t", "quote \" and \\ and \xC3\xA9 and \n");
    list.push(item);
  }
  doc.set("b", list);
  json::Value byId = json::Value::object();
  byId.set("x|1", 0);
  byId.set("y|2", 2.5);
  doc.set("c", byId);
  doc.set("d", json::Value::array());
  doc.set("e", nullptr);

  std::string streamed = json::written([&](json::Writer& w) {
    w.beginObject();
    w.key("a");
    w.value(1);
    w.key("b");
    w.beginArray();
    for (const json::Value& item : list.items()) w.value(item);
    w.endArray();
    w.key("c");
    w.beginObject();
    for (const auto& [k, v] : byId.fields()) {
      w.key(k);
      w.value(v);
    }
    w.endObject();
    w.key("d");
    w.beginArray();
    w.endArray();
    w.key("e");
    w.value(json::Value());
    w.endObject();
  });
  CHECK(streamed == json::write(doc));

  // Small buffers flush often and give the same bytes.
  std::string chunked;
  int flushes = 0;
  json::Writer w(
      [&](std::string_view c) {
        chunked.append(c.data(), c.size());
        ++flushes;
        return true;
      },
      8);
  w.beginArray();
  for (const json::Value& item : list.items()) w.value(item);
  w.endArray();
  CHECK(w.finish());
  CHECK(chunked == json::write(list));
  CHECK(flushes > 1);

  // A sink that fails is reported.
  json::Writer broken([](std::string_view) { return false; }, 1);
  broken.beginArray();
  broken.value(1);
  broken.endArray();
  CHECK_FALSE(broken.finish());
}

TEST_CASE("document reader: small members whole, streamed ones piece by piece") {
  std::string text =
      R"({"built":5,"lists":[[{"s":1}],[{"s":2},{"s":3}]],"notes":{"a":"x"},"programs":{"c1":0,"c2":1},"shards":["us"]})";
  std::map<std::string, json::Value> fields;
  std::vector<std::pair<std::string, std::string>> parts;
  std::vector<size_t> listSizes;
  for (size_t chunk : {size_t(1), size_t(7), text.size()}) {
    CAPTURE(chunk);
    fields.clear();
    parts.clear();
    listSizes.clear();
    json::DocumentReader reader(
        {"lists", "programs"},
        [&](const std::string& key, json::Value&& v) {
          fields[key] = std::move(v);
          return true;
        },
        [&](const std::string& key, const std::string& member, json::Value&& v) {
          parts.push_back({key, member});
          if (key == "lists") listSizes.push_back(v.size());
          return true;
        });
    for (size_t at = 0; at < text.size(); at += chunk) REQUIRE(reader.feed(std::string_view(text).substr(at, chunk)));
    REQUIRE(reader.finish());
    CHECK(fields.size() == 3);
    CHECK(fields["built"].asInt() == 5);
    CHECK(fields["notes"]["a"].asString() == "x");
    CHECK(fields["shards"][size_t(0)].asString() == "us");
    CHECK(listSizes == std::vector<size_t>{1, 2});
    CHECK(parts == std::vector<std::pair<std::string, std::string>>{
                       {"lists", ""}, {"lists", ""}, {"programs", "c1"}, {"programs", "c2"}});
  }

  // Not an object, cut short, or stopped by a callback: false.
  json::DocumentReader array({}, [](const std::string&, json::Value&&) { return true; },
                             [](const std::string&, const std::string&, json::Value&&) { return true; });
  array.feed("[1]");
  CHECK_FALSE(array.finish());
  json::DocumentReader cut({}, [](const std::string&, json::Value&&) { return true; },
                           [](const std::string&, const std::string&, json::Value&&) { return true; });
  cut.feed(R"({"a":[1,2)");
  CHECK_FALSE(cut.finish());
  json::DocumentReader stop({"a"}, [](const std::string&, json::Value&&) { return true; },
                            [](const std::string&, const std::string&, json::Value&&) { return false; });
  stop.feed(R"({"a":[1,2]})");
  CHECK_FALSE(stop.finish());
}

TEST_CASE("document reader: streamed members inside a nested object") {
  std::string text = R"({"catalog":{"entries":[{"id":"a"},{"id":"b"}],"small":{"k":1},"traits":{"x":{"q":1}}},"version":6})";
  std::vector<std::string> fields, parts;
  json::DocumentReader reader(
      {"catalog.entries", "catalog.traits"},
      [&](const std::string& key, json::Value&& v) {
        fields.push_back(key + "=" + json::write(v));
        return true;
      },
      [&](const std::string& key, const std::string& member, json::Value&& v) {
        parts.push_back(key + "/" + member + "=" + json::write(v));
        return true;
      });
  REQUIRE(reader.feed(text));
  REQUIRE(reader.finish());
  CHECK(fields == std::vector<std::string>{"catalog.small={\"k\":1}", "version=6"});
  CHECK(parts == std::vector<std::string>{"catalog.entries/={\"id\":\"a\"}", "catalog.entries/={\"id\":\"b\"}",
                                          "catalog.traits/x={\"q\":1}"});
}
