#include "core/sources/xtream.hpp"

#include <string>
#include <vector>

#include "core/util/strings.hpp"
#include "doctest.h"
#include "sources/mock_provider.hpp"

using namespace marigold;
using namespace marigold::sources;

namespace {

Account account(std::string server = "http://provider.example.invalid:8080/") {
  Account a;
  a.kind = AccountKind::Xtream;
  a.id = "acc1";
  a.displayName = "Mock IPTV";
  a.serverUrl = std::move(server);
  a.userName = "u";
  a.password = "p";
  return a;
}

xtream::Client client(Account a = account()) {
  Result<xtream::Client> c = xtream::Client::make(a);
  REQUIRE(c.ok());
  return c.value();
}

// Feeds `body` to a StreamsReader `step` bytes at a time.
Result<std::vector<Channel>> streamChannels(std::string_view body, size_t step, int status = 200,
                                            xtream::CategoryNames categories = {}) {
  std::vector<Channel> out;
  xtream::StreamsReader reader(status, "xt:acc1", std::move(categories), [&](Channel&& c) {
    out.push_back(std::move(c));
    return true;
  });
  for (size_t i = 0; i < body.size(); i += step)
    if (!reader.feed(body.substr(i, step))) break;
  if (std::optional<Error> e = reader.finish()) return *e;
  return out;
}

bool leaksLogin(const std::string& text) {
  return text.find("example.invalid") != std::string::npos || text.find("password") != std::string::npos ||
         text.find("/live/") != std::string::npos;
}

}  // namespace

TEST_CASE("xtream: server address like XtreamAccount.serverURL") {
  CHECK(xtream::serverURL("http://provider.example.invalid:8080") == std::string("http://provider.example.invalid:8080"));
  CHECK(xtream::serverURL("  provider.example.invalid:8080///  ") ==
        std::string("http://provider.example.invalid:8080"));
  CHECK(xtream::serverURL("HTTPS://provider.example.invalid/") == std::string("HTTPS://provider.example.invalid"));
  CHECK(xtream::serverURL("http://provider.example.invalid/panel") == std::string("http://provider.example.invalid/panel"));
  CHECK_FALSE(xtream::serverURL(""));
  CHECK_FALSE(xtream::serverURL("provider example"));
  // A pasted playlist link is refused instead of being sent with a second login.
  CHECK_FALSE(xtream::serverURL("http://provider.example.invalid/get.php?type=m3u"));
  Result<xtream::Client> bad = xtream::Client::make(account("not a server"));
  REQUIRE_FALSE(bad.ok());
  CHECK(bad.error().message == "Invalid server address");
}

TEST_CASE("xtream: request builders") {
  xtream::Client c = client();
  Request info = c.accountInfoRequest();
  CHECK(info.method == "GET");
  CHECK(info.url == "http://provider.example.invalid:8080/player_api.php?username=u&password=p");
  CHECK(info.timeoutSeconds == 45);
  CHECK(info.header("User-Agent").empty());  // the TV's own agent, not the app's name
  CHECK(c.categoriesRequest().url ==
        "http://provider.example.invalid:8080/player_api.php?username=u&password=p&action=get_live_categories");
  CHECK(c.streamsRequest().url ==
        "http://provider.example.invalid:8080/player_api.php?username=u&password=p&action=get_live_streams");
  // Login first, then the call's own parameters sorted by name.
  CHECK(c.shortEpgRequest("100005").url ==
        "http://provider.example.invalid:8080/player_api.php?username=u&password=p"
        "&action=get_short_epg&limit=10&stream_id=100005");
  CHECK(c.guideURL() == "http://provider.example.invalid:8080/xmltv.php?username=u&password=p");

  Account insecure = account();
  insecure.allowInsecureTLS = true;
  CHECK(client(insecure).streamsRequest().allowInsecureTLS);
  CHECK_FALSE(c.streamsRequest().allowInsecureTLS);
}

TEST_CASE("xtream: logins with URL characters are encoded") {
  Account a = account();
  a.userName = "u@x";
  a.password = "p+/ &";
  xtream::Client c = client(a);
  std::string url = c.accountInfoRequest().url;
  CHECK(str::endsWith(url, "?username=u%40x&password=p%2B%2F%20%26"));
  CHECK(*c.streamURL("7", {}, true) == "http://provider.example.invalid:8080/live/u%40x/p%2B%2F%20%26/7.ts");
}

TEST_CASE("xtream: stream URL format rules") {
  xtream::Client c = client();
  std::string base = "http://provider.example.invalid:8080/live/u/p/100005.";
  CHECK(*c.streamURL("100005", {}, false) == base + "m3u8");  // both allowed: HLS
  CHECK(*c.streamURL("100005", {}, true) == base + "ts");     // preferTS
  CHECK(*c.streamURL("100005", {"m3u8", "ts"}, false) == base + "m3u8");
  CHECK(*c.streamURL("100005", {"m3u8", "ts"}, true) == base + "ts");
  CHECK(*c.streamURL("100005", {"ts"}, false) == base + "ts");      // no HLS
  CHECK(*c.streamURL("100005", {"m3u8"}, true) == base + "m3u8");   // TS not allowed
  CHECK(*c.streamURL("100005", {"rtmp"}, false) == base + "ts");    // neither listed: TS
  // The account's own setting.
  Account a = account();
  a.preferTS = true;
  CHECK(*client(a).streamURL("100005") == base + "ts");
  CHECK(*c.streamURL("100005") == base + "m3u8");
  // Ids that would change the path are refused.
  CHECK_FALSE(c.streamURL("../1", {}, false));
  CHECK_FALSE(c.streamURL("1?x", {}, false));
  CHECK_FALSE(c.streamURL("", {}, false));
}

TEST_CASE("xtream: account info") {
  xtream::Client c = client();
  auto ok = c.parseAccountInfo(200, R"({"user_info":{"auth":1,"status":"Active","exp_date":"1900000000",
      "max_connections":"2","active_cons":0,"allowed_output_formats":["m3u8","ts"]},"server_info":{}})");
  REQUIRE(ok.ok());
  CHECK(ok.value().status == "Active");
  CHECK(ok.value().expires == 1900000000);
  CHECK(ok.value().maxConnections == 2);
  CHECK(ok.value().activeConnections == 0);
  CHECK(ok.value().formats == std::vector<std::string>{"m3u8", "ts"});

  // Numbers as strings, auth "1", no status, no expiry ("0" or null), odd formats.
  auto loose = c.parseAccountInfo(200, R"({"user_info":{"auth":"1","exp_date":"0","max_connections":"1.0",
      "active_cons":"x","allowed_output_formats":["ts",3]}})");
  REQUIRE(loose.ok());
  CHECK(loose.value().status == "Active");
  CHECK_FALSE(loose.value().expires);
  CHECK(loose.value().maxConnections == 1);
  CHECK_FALSE(loose.value().activeConnections);
  CHECK(loose.value().formats.empty());  // `as? [String]` fails on a mixed list

  Account a = account();
  xtream::applyAccountInfo(a, ok.value());
  CHECK(a.expires == 1900000000);
  CHECK(a.maxConnections == 2);

  auto wrong = c.parseAccountInfo(200, R"({"user_info":{"auth":0}})");
  REQUIRE_FALSE(wrong.ok());
  CHECK(wrong.error().message == "Wrong username or password.");
  CHECK(c.parseAccountInfo(200, R"({"user_info":{}})").error().message == "Wrong username or password.");

  for (const char* body : {"<html>Not Found</html>", "[]", "{}", R"({"user_info":"x"})", ""}) {
    auto r = c.parseAccountInfo(200, body);
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().message ==
          "The server didn't answer like an Xtream provider. Check the address (including the port).");
  }
}

TEST_CASE("xtream: HTTP errors") {
  xtream::Client c = client();
  CHECK(c.parseAccountInfo(401, "").error().message == "The provider refused the sign-in (401).");
  CHECK(c.parseAccountInfo(403, "").error().status == 403);
  CHECK(c.parseAccountInfo(512, "").error().message == "The provider returned an error (512).");
  CHECK(c.parseAccountInfo(0, "").error().message == "No response from the provider");
  CHECK_FALSE(xtream::checkStatus(204));
  auto r = streamChannels("[]", 1, 403);
  REQUIRE_FALSE(r.ok());
  CHECK(r.error().message == "The provider refused the sign-in (403).");
  for (int status : {0, 401, 404, 500}) {
    std::optional<Error> e = xtream::checkStatus(status);
    REQUIRE(e);
    CHECK_FALSE(leaksLogin(e->message));
  }
}

TEST_CASE("xtream: categories") {
  auto names = xtream::parseCategories(200, R"([{"category_id":"1","category_name":"  US| SPORTS \n"},
      {"category_id":2,"category_name":"UK| GENERAL"},{"category_id":"3","category_name":""},
      {"category_id":"","category_name":"skip"},{"category_name":"no id"},{"category_id":"4","category_name":5}])");
  CHECK(names.size() == 2);
  CHECK(names["1"] == "US| SPORTS");
  CHECK(names["2"] == "UK| GENERAL");
  CHECK(xtream::parseCategories(200, "[{\"category_id\":\"1\"").empty());  // truncated
  CHECK(xtream::parseCategories(500, "[]").empty());
  CHECK(xtream::parseCategories(200, "{}").empty());
}

TEST_CASE("xtream: channel fields map like the Swift") {
  xtream::CategoryNames cats{{"1", "US| SPORTS"}, {"7", "UK| SPORTS"}};
  std::string body = R"([
    {"num":1,"name":" US: ESPN FHD ","stream_type":"live","stream_id":100000,"stream_icon":"http://logos.example.invalid/1.png",
     "epg_channel_id":"espn.us","category_id":"1"},
    {"num":"2","name":"UK| Sky Sports Main Event","stream_id":"100001","stream_icon":"","epg_channel_id":null,"category_id":7},
    {"name":"RADIO: Jazz FM","stream_type":"radio_streams","stream_id":"100002","category_id":"99","epg_channel_id":""},
    {"name":"","stream_id":100003},
    {"name":"no id"},
    {"name":"bad id","stream_id":"1/../2"},
    {"name":5,"stream_id":100004},
    "not an object", 7, null,
    {"name":"(MX) Canal 5 HD","stream_id":100005.0,"num":3.5}
  ])";
  for (size_t step : {body.size(), size_t{1}, size_t{7}}) {
    auto r = streamChannels(body, step, 200, cats);
    REQUIRE(r.ok());
    const std::vector<Channel>& list = r.value();
    REQUIRE(list.size() == 4);
    const Channel& espn = list[0];
    CHECK(espn.id == "xt:acc1|100000");
    CHECK(espn.serverId == "xt:acc1");
    CHECK(espn.itemId == "100000");
    CHECK(espn.name == "US: ESPN FHD");
    CHECK(espn.number == std::string("1"));
    CHECK(espn.imageUrl == "http://logos.example.invalid/1.png");
    CHECK(espn.tags == std::vector<std::string>{"US| SPORTS"});
    CHECK(espn.group == std::string("US| SPORTS"));
    CHECK(espn.epgId == std::string("espn.us"));
    CHECK_FALSE(espn.isRadio);
    CHECK_FALSE(espn.isFavorite);

    const Channel& sky = list[1];
    CHECK(sky.id == "xt:acc1|100001");
    CHECK(sky.number == std::string("2"));
    CHECK(sky.imageUrl.empty());
    CHECK(sky.tags == std::vector<std::string>{"UK| SPORTS"});  // category id sent as a number
    CHECK_FALSE(sky.epgId);

    const Channel& radio = list[2];
    CHECK(radio.isRadio);
    CHECK(radio.tags.empty());  // unknown category
    CHECK_FALSE(radio.group);
    CHECK_FALSE(radio.epgId);
    CHECK_FALSE(radio.number);

    CHECK(list[3].itemId == "100005");
    CHECK(list[3].number == std::string("3.5"));
  }
}

TEST_CASE("xtream: channel list errors") {
  for (const char* body : {"{}", "null", R"({"user_info":{"auth":0}})", "[{\"name\":\"x\"", "<html>", ""}) {
    CAPTURE(body);
    auto r = streamChannels(body, 1);
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().message == "The provider didn't return a channel list.");
  }
  auto empty = streamChannels("[]", 1);
  REQUIRE(empty.ok());
  CHECK(empty.value().empty());
  // A UTF-8 byte order mark is fine.
  auto bom = streamChannels("\xEF\xBB\xBF[{\"name\":\"A\",\"stream_id\":1}]", 1);
  REQUIRE(bom.ok());
  CHECK(bom.value().size() == 1);
}

TEST_CASE("xtream: the reader can be stopped") {
  size_t seen = 0;
  xtream::StreamsReader reader(200, "xt:acc1", {}, [&](Channel&&) { return ++seen < 3; });
  mock::Provider p(50);
  std::string body = p.liveStreamsJson();
  CHECK_FALSE(reader.feed(body));
  CHECK(seen == 3);
  CHECK_FALSE(reader.finish());  // stopped on purpose is not an error
}

TEST_CASE("xtream: parseChannels with the mock provider, one byte at a time") {
  mock::Provider p(1200);
  std::string cats = p.categoriesJson();
  std::string body = p.liveStreamsJson();
  auto whole = xtream::parseChannels("xt:acc1", 200, cats, 200, body);
  REQUIRE(whole.ok());
  CHECK(whole.value().size() == p.usableCount());
  auto bytes = streamChannels(body, 1, 200, xtream::parseCategories(200, cats));
  REQUIRE(bytes.ok());
  REQUIRE(bytes.value().size() == whole.value().size());
  for (size_t i = 0; i < whole.value().size(); ++i) {
    const Channel& a = whole.value()[i];
    const Channel& b = bytes.value()[i];
    CHECK(a.id == b.id);
    CHECK(a.name == b.name);
    CHECK(a.tags == b.tags);
  }
  // Names arrive escaped (í, \") and padded, and come out clean.
  bool sawAccent = false, sawRadio = false, sawSeparator = false;
  for (const Channel& c : whole.value()) {
    CHECK(c.name == std::string(trimWhitespaceAndNewlines(c.name)));
    CHECK(c.tags.size() == 1);  // every mock category has a name
    CHECK_FALSE(leaksLogin(c.id));
    if (c.name.find("Andaluc\xC3\xAD" "a \"Directo\"") != std::string::npos) sawAccent = true;
    if (c.isRadio) sawRadio = true;
    if (c.name.rfind("#####", 0) == 0 || c.name.rfind("====", 0) == 0) sawSeparator = true;
  }
  CHECK(sawAccent);
  CHECK(sawRadio);
  CHECK(sawSeparator);
  // Category names are trimmed ("  ES| GENERAL  ").
  auto names = xtream::parseCategories(200, cats);
  CHECK(names["20"] == "ES| GENERAL");
}

TEST_CASE("xtream: short EPG with base64 titles") {
  xtream::Client c = client();
  std::string body = R"({"epg_listings":[
    {"title":"TkZMIEZvb3RiYWxsOiBUZWFtIEEgYXQgVGVhbSBC","description":"TGl2ZSBjb3ZlcmFnZSBmcm9tIHRoZSBzdGFkaXVtLg==",
     "start_timestamp":"1792612800","stop_timestamp":"1792623600"},
    {"title":"ICBFeWV3aXRuZXNzIE5ld3MgIAo=","start_timestamp":1792623600,"stop_timestamp":1792627200,"description":""},
    {"title":"Plain Title","start_timestamp":"1792627200","stop_timestamp":"1792630800"},
    {"title":"//79","start_timestamp":"1792630800","stop_timestamp":"1792634400"},
    {"title":"","start_timestamp":"1792634400","stop_timestamp":"1792638000"},
    {"title":"VGl0bGU=","start_timestamp":"1792638000","stop_timestamp":"1792638000"},
    {"title":"VGl0bGU=","start_timestamp":"x","stop_timestamp":"1792641600"},
    {"title":"ICAg","start_timestamp":"1792641600","stop_timestamp":"1792645200"}
  ]})";
  auto genres = [](const std::string& title, const std::optional<std::string>&) -> uint8_t {
    uint8_t f = 0;
    if (title.find("Football") != std::string::npos) f |= kGenreSports | kGenreMovie;  // movie is masked off
    if (title.find("News") != std::string::npos) f |= kGenreNews;
    return f;
  };
  std::vector<Program> list = c.parseShortEpg(200, body, "xt:acc1|100000", genres);
  REQUIRE(list.size() == 4);
  CHECK(list[0].title == "NFL Football: Team A at Team B");
  CHECK(list[0].overview == std::string("Live coverage from the stadium."));
  CHECK(list[0].id == "xt:acc1|100000|1792612800");
  CHECK(list[0].channelId == "xt:acc1|100000");
  CHECK(list[0].start == 1792612800);
  CHECK(list[0].end == 1792623600);
  CHECK(list[0].genres == kGenreSports);
  CHECK(list[1].title == "Eyewitness News");  // decoded text is trimmed
  CHECK_FALSE(list[1].overview);
  CHECK(list[1].genres == kGenreNews);
  CHECK(list[2].title == "Plain Title");  // not base64: kept as is
  CHECK(list[3].title == "//79");         // base64, but not UTF-8: kept as is
  // "ICAg" decodes to three spaces: an empty title, dropped.
  CHECK(c.parseShortEpg(200, body, "x").size() == 4);
  CHECK(c.parseShortEpg(200, body, "x")[0].genres == 0);  // no title reader given

  CHECK(c.parseShortEpg(500, body, "x").empty());
  CHECK(c.parseShortEpg(200, "{\"epg_listings\":", "x").empty());
  CHECK(c.parseShortEpg(200, "[]", "x").empty());
  CHECK(c.parseShortEpg(200, "{\"epg_listings\":null}", "x").empty());
}
