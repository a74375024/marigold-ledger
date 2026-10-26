#include "core/sources/emby.hpp"

#include <string>
#include <vector>

#include "core/util/json.hpp"
#include "core/util/strings.hpp"
#include "doctest.h"

using namespace marigold;
using namespace marigold::sources;

namespace {

emby::Client client(std::string base = "http://emby.example.invalid:8096/") {
  Result<emby::Client> c = emby::Client::make("jf:srv1", base, "tok", "user1");
  REQUIRE(c.ok());
  return c.value();
}

json::Value body(const Request& r) { return json::parse(r.body).value_or(json::Value()); }

}  // namespace

TEST_CASE("emby: request headers and URLs") {
  emby::Client c = client();
  Request r = c.channelsRequest();
  CHECK(r.method == "GET");
  CHECK(r.timeoutSeconds == 25);
  CHECK(r.url ==
        "http://emby.example.invalid:8096/LiveTv/Channels?EnableFavoriteSorting=true&EnableImageTypes=Primary"
        "&EnableUserData=true&Fields=ChannelInfo%2CTags%2CGenres&ImageTypeLimit=1&UserId=user1");
  std::string auth =
      "MediaBrowser Client=\"Marigold\", Device=\"webOS TV\", DeviceId=\"Marigold-webOS\", Version=\"0.1.0\", "
      "Token=\"tok\"";
  CHECK(r.header("Authorization") == auth);
  CHECK(r.header("X-Emby-Authorization") == auth);
  CHECK(r.header("X-Emby-Token") == "tok");
  CHECK(r.header("Accept") == "application/json");
  CHECK(r.header("Content-Type") == "");
  CHECK(r.body.empty());

  Result<emby::Client> bad = emby::Client::make("jf:x", "emby.example.invalid", "t", "u");
  REQUIRE_FALSE(bad.ok());
  CHECK(bad.error().message == "Invalid server address");

  Account a;
  a.kind = AccountKind::Emby;
  a.id = "srv2";
  a.serverUrl = "https://emby.example.invalid/emby/";
  a.token = "tok";
  a.userId = "user1";
  a.allowInsecureTLS = true;
  Result<emby::Client> fromAccount = emby::Client::make(a);
  REQUIRE(fromAccount.ok());
  CHECK(fromAccount.value().serverId() == "jf:srv2");
  Request fav = fromAccount.value().setFavoriteRequest(true, "abc123");
  CHECK(fav.method == "POST");
  CHECK(fav.url == "https://emby.example.invalid/emby/Users/user1/FavoriteItems/abc123");
  CHECK(fav.allowInsecureTLS);
  CHECK(fromAccount.value().setFavoriteRequest(false, "abc123").method == "DELETE");
}

TEST_CASE("emby: status errors") {
  CHECK(emby::checkStatus(401)->message == "Signed out of the server. Sign in again in Settings.");
  CHECK(emby::checkStatus(401)->status == 401);
  CHECK(emby::checkStatus(403)->message == "This account doesn't have Live TV access.");
  CHECK(emby::checkStatus(404)->message == "This server doesn't have Live TV.");
  CHECK(emby::checkStatus(502)->message == "The server returned an error (502).");
  CHECK(emby::checkStatus(0)->message == "No response from the server");
  CHECK_FALSE(emby::checkStatus(200));
  CHECK(emby::describeErrorCode("NoCompatibleStream") ==
        "The server has no stream this device can play for this channel.");
  CHECK(emby::describeErrorCode("NotAllowed") == "This account isn't allowed to watch this channel.");
  CHECK(emby::describeErrorCode("RateLimitExceeded") ==
        "Too many streams are open on this account right now. Close one and try again.");
  CHECK(emby::describeErrorCode("TunerBusy") == "The server couldn't open this channel (TunerBusy).");
  // Server text is redacted and shortened before it can be shown.
  std::string leaky = emby::describeErrorCode("see http://emby.example.invalid/x?api_key=abc token=abc");
  CHECK(leaky.find("example.invalid") == std::string::npos);
  CHECK(leaky.find("abc") == std::string::npos);
  CHECK(emby::describeErrorCode(std::string(500, 'x')).size() < 120);
}

TEST_CASE("emby: ISO 8601 dates") {
  CHECK(emby::date("2026-10-21T20:00:00.0000000Z") == 1792612800);
  CHECK(emby::date("2026-10-21T20:00:00Z") == 1792612800);
  CHECK(emby::date("2026-10-21T20:00:00") == 1792612800);           // no zone: UTC
  CHECK(emby::date("2026-10-21T20:00:00.123") == 1792612800);
  CHECK(emby::date(" 2026-10-21T20:00:00.5Z ") == 1792612800);
  CHECK(emby::date("2026-10-21T22:00:00.000+02:00") == 1792612800);
  CHECK(emby::date("2026-10-21T15:00:00-0500") == 1792612800);
  CHECK(emby::date("2026-10-21T20:00:00.9999999+00:00") == 1792612800);  // fraction dropped, not rounded
  CHECK_FALSE(emby::date(""));
  CHECK_FALSE(emby::date("   "));
  CHECK_FALSE(emby::date("2026-10-21"));
  CHECK_FALSE(emby::date("yesterday"));
  CHECK_FALSE(emby::date("2026-10-21T20:00:00+2"));
}

TEST_CASE("emby: lenient names") {
  auto names = [](const char* text) { return emby::lenientNames(json::parse(text).value_or(json::Value())); };
  CHECK(names(R"(["a","b"])") == std::vector<std::string>{"a", "b"});
  CHECK(names(R"(["a",{"Name":"b"},{"Id":"x"},{"Name":null},"c"])") == std::vector<std::string>{"a", "b", "c"});
  CHECK(names(R"(["a",3,"b"])") == std::vector<std::string>{"a"});  // stops at the first other element
  CHECK(names(R"(["a",{"Name":3},"b"])") == std::vector<std::string>{"a"});
  CHECK(names(R"("a")").empty());
  CHECK(names("null").empty());
}

TEST_CASE("emby: channels") {
  emby::Client c = client();
  std::string text = R"({"Items":[
    {"Id":"a1","Name":"  ESPN  ","Number":" 206 ","ImageTags":{"Primary":"tag1"},"UserData":{"IsFavorite":true},
     "Tags":["Sports",{"Name":" HD "}],"TagItems":[{"Name":"US"},{"Name":""}],"ChannelType":"TV"},
    {"Id":"a2","ChannelNumber":"5.1","ChannelType":"Radio"},
    {"Id":"a3","Name":"FS1","Number":"","ChannelNumber":"7"},
    {"Id":"a4","Name":"NBA TV","Number":42},
    {"Name":"no id"},
    {"Id":"bad/id","Name":"x"},
    {"Id":5,"Name":"x"}
  ],"TotalRecordCount":7})";
  for (size_t step : {text.size(), size_t{1}, size_t{5}}) {
    std::vector<Channel> list;
    emby::ChannelsReader reader(c, 200, [&](Channel&& ch) {
      list.push_back(std::move(ch));
      return true;
    });
    for (size_t i = 0; i < text.size(); i += step) reader.feed(std::string_view(text).substr(i, step));
    CHECK_FALSE(reader.finish());
    CHECK(reader.skipped() == 3);
    REQUIRE(list.size() == 4);
    const Channel& espn = list[0];
    CHECK(espn.id == "jf:srv1|a1");
    CHECK(espn.serverId == "jf:srv1");
    CHECK(espn.itemId == "a1");
    CHECK(espn.name == "ESPN");
    CHECK(espn.number == std::string("206"));
    CHECK(espn.imageUrl == "http://emby.example.invalid:8096/Items/a1/Images/Primary?maxHeight=360&tag=tag1");
    CHECK(espn.imageUrl.find("tok") == std::string::npos);  // no token in logo links
    CHECK(espn.isFavorite);
    CHECK(espn.tags == std::vector<std::string>{"Sports", "HD", "US"});
    CHECK(espn.group == std::string("Sports"));
    CHECK_FALSE(espn.isRadio);

    CHECK(list[1].name == "Channel");
    CHECK(list[1].number == std::string("5.1"));
    CHECK(list[1].isRadio);
    CHECK(list[1].imageUrl.empty());
    CHECK(list[1].tags.empty());
    CHECK_FALSE(list[1].isFavorite);
    CHECK_FALSE(list[2].number);  // a blank Number wins over ChannelNumber
    CHECK(list[3].number == std::string("42"));
  }
  auto all = c.parseChannels(200, text);
  REQUIRE(all.ok());
  CHECK(all.value().size() == 4);
  CHECK(c.parseChannels(200, "{}").value().empty());
  CHECK(c.parseChannels(200, R"({"Items":null})").value().empty());
  CHECK(c.parseChannels(200, R"({"Items":[{"Id":"a")").error().message ==
        "The server sent a reply that couldn't be read.");
  CHECK(c.parseChannels(200, "<html>").error().message == "The server sent a reply that couldn't be read.");
  CHECK(c.parseChannels(401, "").error().status == 401);
}

TEST_CASE("emby: programmes") {
  emby::Client c = client("https://emby.example.invalid/emby");
  CHECK_FALSE(c.programsRequest({}, 0, 1));
  std::optional<Request> r = c.programsRequest({"a1", "a2"}, 1792612800, 1792699200);
  REQUIRE(r);
  CHECK(r->url ==
        "https://emby.example.invalid/emby/LiveTv/Programs?ChannelIds=a1%2Ca2&EnableImageTypes=Primary%2CThumb"
        "&EnableTotalRecordCount=false&Fields=Overview&ImageTypeLimit=1&MaxStartDate=2026-10-22T20%3A00%3A00Z"
        "&MinEndDate=2026-10-21T20%3A00%3A00Z&SortBy=StartDate&UserId=user1");

  std::string text = R"({"Items":[
    {"Id":"p1","ChannelId":"a1","Name":" NFL Football ","EpisodeTitle":"","Overview":" Week 8 ",
     "StartDate":"2026-10-21T20:00:00.0000000Z","EndDate":"2026-10-21T23:00:00.0000000Z","IsLive":true,
     "ImageTags":{"Primary":"t1","Thumb":"t2"}},
    {"Id":"p2","ChannelId":"a1","Name":"Movie Night","StartDate":"2026-10-21T23:00:00Z",
     "EndDate":"2026-10-22T01:00:00+00:00","IsMovie":true,"IsPremiere":true,"ImageTags":{"Primary":"t3"}},
    {"Id":"p3","ChannelId":"a2","Name":"Evening News","StartDate":"2026-10-21T18:00:00-02:00",
     "EndDate":"2026-10-21T21:00:00Z"},
    {"Id":"p4","ChannelId":"a2","StartDate":"2026-10-21T21:00:00Z","EndDate":"2026-10-21T21:00:00Z"},
    {"Id":"p5","StartDate":"2026-10-21T21:00:00Z","EndDate":"2026-10-21T22:00:00Z"},
    {"Id":"p6","ChannelId":"a2","StartDate":"bad","EndDate":"2026-10-21T22:00:00Z"},
    {"Id":"p7","ChannelId":"a2","StartDate":"2026-10-21T21:00:00Z","EndDate":"2026-10-21T22:00:00Z"}
  ]})";
  auto genres = [](const std::string& title, const std::optional<std::string>&) -> uint8_t {
    return title.find("News") != std::string::npos ? kGenreNews | kGenreKids : 0;
  };
  auto list = c.parsePrograms(200, text, genres);
  REQUIRE(list.ok());
  REQUIRE(list.value().size() == 4);
  const Program& nfl = list.value()[0];
  CHECK(nfl.id == "jf:srv1|p1");
  CHECK(nfl.channelId == "jf:srv1|a1");
  CHECK(nfl.title == "NFL Football");
  CHECK_FALSE(nfl.episodeTitle);
  CHECK(nfl.overview == std::string("Week 8"));
  CHECK(nfl.start == 1792612800);
  CHECK(nfl.end == 1792612800 + 3 * 3600);
  CHECK(nfl.isLive);
  CHECK(nfl.imageUrl == "https://emby.example.invalid/emby/Items/p1/Images/Thumb?maxHeight=360&tag=t2");
  const Program& movie = list.value()[1];
  CHECK(movie.genres == kGenreMovie);  // the server's flag, no title flags on top
  CHECK(movie.isPremiere);
  CHECK(movie.imageUrl == "https://emby.example.invalid/emby/Items/p2/Images/Primary?maxHeight=360&tag=t3");
  CHECK(list.value()[2].start == 1792612800);
  CHECK(list.value()[2].genres == kGenreNews);  // title flags: sports and news only
  CHECK(list.value()[3].title.empty());

  CHECK(c.parsePrograms(500, text).error().message == "The server returned an error (500).");
  CHECK_FALSE(c.parsePrograms(200, "{\"Items\":[").ok());
}

TEST_CASE("emby: search") {
  emby::Client c = client();
  time::Seconds now = 1792612800;
  std::vector<Request> routes = c.searchRequests("news", now);
  REQUIRE(routes.size() == 2);
  CHECK(str::startsWith(routes[0].url, "http://emby.example.invalid:8096/LiveTv/Programs?"));
  CHECK(routes[0].url.find("SearchTerm=news") != std::string::npos);
  CHECK(routes[0].url.find("HasAired=false") != std::string::npos);
  CHECK(routes[0].url.find("MaxStartDate=2026-10-22T20%3A00%3A00Z") != std::string::npos);
  CHECK(routes[0].url.find("Limit=60") != std::string::npos);
  CHECK(str::startsWith(routes[1].url, "http://emby.example.invalid:8096/Users/user1/Items?"));
  CHECK(routes[1].url.find("IncludeItemTypes=Program%2CLiveTvProgram") != std::string::npos);
  CHECK(routes[1].url.find("Recursive=true") != std::string::npos);

  std::string first = R"({"Items":[
    {"Id":"p2","ChannelId":"a","Name":"Late NEWS","StartDate":"2026-10-21T23:00:00Z","EndDate":"2026-10-21T23:30:00Z"},
    {"Id":"p1","ChannelId":"a","Name":"Evening News","StartDate":"2026-10-21T19:30:00Z","EndDate":"2026-10-21T20:30:00Z"},
    {"Id":"old","ChannelId":"a","Name":"Morning News","StartDate":"2026-10-21T08:00:00Z","EndDate":"2026-10-21T09:00:00Z"},
    {"Id":"x","ChannelId":"a","Name":"Football","StartDate":"2026-10-21T21:00:00Z","EndDate":"2026-10-21T22:00:00Z"}]})";
  std::string second = R"({"Items":[
    {"Id":"p2","ChannelId":"a","Name":"Late NEWS","StartDate":"2026-10-21T23:00:00Z","EndDate":"2026-10-21T23:30:00Z"},
    {"Id":"p3","ChannelId":"b","Name":"Newsroom","StartDate":"2026-10-21T22:00:00Z","EndDate":"2026-10-21T23:00:00Z"}]})";
  auto found = c.parseSearch("NeWs", now, {{200, first}, {200, second}});
  REQUIRE(found.size() == 3);
  CHECK(found[0].id == "jf:srv1|p1");
  CHECK(found[1].id == "jf:srv1|p3");
  CHECK(found[2].id == "jf:srv1|p2");
  // A failed route is skipped.
  auto partial = c.parseSearch("news", now, {{404, ""}, {200, second}});
  CHECK(partial.size() == 2);
  CHECK(c.parseSearch("news", now, {{200, "garbage"}}).empty());
}

TEST_CASE("emby: open stream request") {
  emby::Client c = client();
  Request r = c.openStreamRequest("a1");
  CHECK(r.method == "POST");
  CHECK(r.url == "http://emby.example.invalid:8096/Items/a1/PlaybackInfo?AutoOpenLiveStream=true&IsPlayback=true"
                 "&UserId=user1");
  CHECK(r.header("Content-Type") == "application/json");
  json::Value b = body(r);
  CHECK(b["UserId"].asString() == "user1");
  CHECK(b["AutoOpenLiveStream"].asBool());
  CHECK(b["IsPlayback"].asBool());
  CHECK(b["MaxStreamingBitrate"].asInt() == 120000000);
  const json::Value& profile = b["DeviceProfile"];
  CHECK(profile["Name"].asString() == "Marigold webOS TV");
  CHECK(profile["MaxStaticBitrate"].asInt() == 120000000);
  CHECK(profile["DirectPlayProfiles"][size_t{0}]["Container"].asString() ==
        "ts,mpegts,m2ts,mp4,m4v,mkv,mov,hls,m3u8,flv,avi,webm");
  const json::Value& tp = profile["TranscodingProfiles"][size_t{0}];
  CHECK(tp["Protocol"].asString() == "hls");
  CHECK(tp["VideoCodec"].asString() == "h264,hevc");
  CHECK(tp["BreakOnNonKeyFrames"].asBool());
  CHECK(tp["MinSegments"].asInt() == 1);
  CHECK(profile["ContainerProfiles"].isArray());
  CHECK(profile["SubtitleProfiles"][size_t{0}]["Method"].asString() == "Hls");
}

TEST_CASE("emby: stream choice order") {
  emby::Client c = client("http://emby.example.invalid:8096/emby");
  auto open = [&](const std::string& text) { return c.parseOpenStream(200, text, "a1"); };

  // 1. DirectStreamUrl, joined onto the base (its own "/emby" dropped), token added.
  auto direct = open(R"({"PlaySessionId":"s1","MediaSources":[{"Id":"m1","LiveStreamId":"l1",
      "DirectStreamUrl":"/emby/videos/a1/live.ts?static=true","SupportsDirectStream":true,
      "TranscodingUrl":"/videos/a1/master.m3u8"}]})");
  REQUIRE(direct.ok());
  CHECK(direct.value().url == "http://emby.example.invalid:8096/emby/videos/a1/live.ts?static=true&api_key=tok");
  CHECK(direct.value().liveStreamId == std::string("l1"));
  CHECK(direct.value().playSessionId == std::string("s1"));
  CHECK(direct.value().mediaSourceId == std::string("m1"));
  CHECK(direct.value().itemId == "a1");
  CHECK_FALSE(direct.value().isTranscode);

  // A link that already has its key is left alone; absolute links too.
  CHECK(open(R"({"MediaSources":[{"DirectStreamUrl":"videos/a1/x.ts?API_KEY=k2"}]})").value().url ==
        "http://emby.example.invalid:8096/emby/videos/a1/x.ts?API_KEY=k2");
  CHECK(open(R"({"MediaSources":[{"DirectStreamUrl":"https://cdn.example.invalid/a.ts"}]})").value().url ==
        "https://cdn.example.invalid/a.ts");

  // 2. SupportsDirectStream: the static stream.ts link.
  auto staticTs = open(R"({"PlaySessionId":"s1","MediaSources":[{"Id":"m1","LiveStreamId":"l1",
      "DirectStreamUrl":"  ","SupportsDirectStream":true,"TranscodingUrl":"/videos/a1/master.m3u8"}]})");
  REQUIRE(staticTs.ok());
  CHECK(staticTs.value().url ==
        "http://emby.example.invalid:8096/emby/Videos/a1/stream.ts?DeviceId=Marigold-webOS&LiveStreamId=l1"
        "&MediaSourceId=m1&PlaySessionId=s1&Static=true&api_key=tok");
  CHECK_FALSE(staticTs.value().isTranscode);
  // No source id: the channel's item id.
  CHECK(open(R"({"MediaSources":[{"SupportsDirectStream":true}]})").value().url ==
        "http://emby.example.invalid:8096/emby/Videos/a1/stream.ts?DeviceId=Marigold-webOS&MediaSourceId=a1"
        "&Static=true&api_key=tok");

  // 3. TranscodingUrl.
  auto hls = open(R"({"MediaSources":[{"SupportsDirectStream":false,"TranscodingUrl":"/videos/a1/master.m3u8?x=1"}]})");
  REQUIRE(hls.ok());
  CHECK(hls.value().url == "http://emby.example.invalid:8096/emby/videos/a1/master.m3u8?x=1&api_key=tok");
  CHECK(hls.value().isTranscode);

  // 4. An http Path.
  CHECK(open(R"({"MediaSources":[{"Path":"http://tuner.example.invalid/ch/5"}]})").value().url ==
        "http://tuner.example.invalid/ch/5");

  // Nothing playable, errors.
  CHECK(open(R"({"MediaSources":[{"Path":"/mnt/tv/5.ts"}]})").error().message ==
        "This channel can't be streamed to this device.");
  CHECK(open(R"({"MediaSources":[]})").error().message == "The server didn't return a stream for this channel.");
  CHECK(open(R"({})").error().message == "The server didn't return a stream for this channel.");
  CHECK(open(R"({"ErrorCode":"RateLimitExceeded","MediaSources":[{"SupportsDirectStream":true}]})").error().message ==
        "Too many streams are open on this account right now. Close one and try again.");
  CHECK(open("not json").error().message == "The server sent a reply that couldn't be read.");
  CHECK(c.parseOpenStream(403, "", "a1").error().message == "This account doesn't have Live TV access.");
}

TEST_CASE("emby: playback reports and the close sequence") {
  emby::Client c = client();
  emby::Stream s;
  s.url = "http://emby.example.invalid:8096/videos/a1/master.m3u8?api_key=tok";
  s.itemId = "a1";
  s.mediaSourceId = "m1";
  s.playSessionId = "s1";
  s.liveStreamId = "l1";
  s.isTranscode = true;

  Request playing = c.reportPlayingRequest(s);
  CHECK(playing.method == "POST");
  CHECK(playing.url == "http://emby.example.invalid:8096/Sessions/Playing");
  CHECK(playing.body == R"({"CanSeek":false,"IsPaused":false,"ItemId":"a1","LiveStreamId":"l1","MediaSourceId":"m1",)"
                        R"("PlayMethod":"Transcode","PlaySessionId":"s1","PositionTicks":0})");

  std::vector<Request> close = c.closeStreamRequests(s);
  REQUIRE(close.size() == 3);
  CHECK(close[0].method == "POST");
  CHECK(close[0].url == "http://emby.example.invalid:8096/Sessions/Playing/Stopped");
  CHECK(body(close[0])["PlayMethod"].asString() == "Transcode");
  CHECK(close[1].method == "DELETE");
  CHECK(close[1].url ==
        "http://emby.example.invalid:8096/Videos/ActiveEncodings?DeviceId=Marigold-webOS&PlaySessionId=s1");
  CHECK(close[1].body.empty());
  CHECK(close[2].method == "POST");
  CHECK(close[2].url == "http://emby.example.invalid:8096/LiveStreams/Close?LiveStreamId=l1");
  CHECK(close[2].body == "{}");

  // A direct stream with no live stream id: only the stopped report.
  emby::Stream plain;
  plain.itemId = "a2";
  std::vector<Request> simple = c.closeStreamRequests(plain);
  REQUIRE(simple.size() == 1);
  CHECK(body(simple[0])["PlayMethod"].asString() == "DirectStream");
  CHECK_FALSE(body(simple[0]).has("LiveStreamId"));
  CHECK_FALSE(body(simple[0]).has("PlaySessionId"));
}

TEST_CASE("emby: sign-in with user name and password") {
  Request r = emby::authenticateRequest(" http://emby.example.invalid:8096/emby/ ", "u", "p", true);
  CHECK(r.method == "POST");
  CHECK(r.url == "http://emby.example.invalid:8096/emby/Users/AuthenticateByName");
  CHECK(r.allowInsecureTLS);
  CHECK(r.timeoutSeconds == 25);
  std::string auth =
      "MediaBrowser Client=\"Marigold\", Device=\"webOS TV\", DeviceId=\"Marigold-webOS\", Version=\"0.1.0\"";
  CHECK(r.header("X-Emby-Authorization") == auth);
  CHECK(r.header("Authorization") == auth);
  CHECK(r.header("X-Emby-Token") == "");
  CHECK(r.header("Content-Type") == "application/json");
  CHECK(r.body == R"({"Pw":"p","Username":"u"})");
  // JSON-special characters in the login are escaped, not spliced in.
  Request odd = emby::authenticateRequest("http://emby.example.invalid", "u\"", "p\\\"}", false);
  json::Value b = body(odd);
  CHECK(b["Username"].asString() == "u\"");
  CHECK(b["Pw"].asString() == "p\\\"}");
  CHECK(emby::authenticateRequest("not a server", "u", "p", false).url.empty());

  auto ok = emby::parseAuthenticate(200, R"({"User":{"Id":"f00d","Name":"u"},"AccessToken":"t0k","ServerId":"s"})");
  REQUIRE(ok.ok());
  CHECK(ok.value().first == "t0k");
  CHECK(ok.value().second == "f00d");

  auto wrong = emby::parseAuthenticate(401, "");
  REQUIRE_FALSE(wrong.ok());
  CHECK(wrong.error().message == "Wrong user name or password");
  CHECK(wrong.error().status == 401);
  CHECK(emby::parseAuthenticate(403, "").error().message == "This account doesn't have Live TV access.");
  CHECK(emby::parseAuthenticate(500, "").error().message == "The server returned an error (500).");
  CHECK(emby::parseAuthenticate(0, "").error().message == "No response from the server");
  for (const char* text : {"", "<html>", "{}", R"({"AccessToken":"t0k"})", R"({"User":{"Id":"f00d"}})",
                           R"({"User":{"Id":5},"AccessToken":"t0k"})", R"({"User":{"Id":"f00d"},"AccessToken":""})"}) {
    CAPTURE(text);
    auto failed = emby::parseAuthenticate(200, text);
    REQUIRE_FALSE(failed.ok());
    CHECK(failed.error().message == "The server sent a reply that couldn't be read.");
  }
  // A server that echoes the login back: none of it reaches the message.
  for (int status : {200, 400, 401, 500}) {
    auto echo = emby::parseAuthenticate(status, R"({"Username":"u","Pw":"p\\\"}","error":"bad password p\\\"}"})");
    REQUIRE_FALSE(echo.ok());
    CHECK(echo.error().message.find("p\\\"}") == std::string::npos);
    CHECK(echo.error().message.find("password p") == std::string::npos);
  }
}
