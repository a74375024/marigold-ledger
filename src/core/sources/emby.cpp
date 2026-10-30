#include "core/sources/emby.hpp"

#include <algorithm>
#include <map>

#include "core/util/json.hpp"
#include "core/util/redact.hpp"
#include "core/util/strings.hpp"

namespace marigold::sources::emby {
namespace {

constexpr size_t kMaxErrorCodeBytes = 64;

// String.liveNonEmpty: trimmed, nullopt when empty.
std::optional<std::string> liveNonEmpty(std::string_view s) {
  std::string_view t = trimWhitespaceAndNewlines(s);
  if (t.empty()) return std::nullopt;
  return std::string(t);
}

std::optional<std::string> liveNonEmptyField(const json::Value& v) {
  if (!v.isString()) return std::nullopt;
  return liveNonEmpty(v.asString());
}

// A `String?` field of a Swift Decodable: present text (even empty).
std::optional<std::string> optString(const json::Value& v) {
  if (!v.isString()) return std::nullopt;
  return v.asString();
}

bool optBool(const json::Value& v) { return v.isBool() && v.asBool(); }

bool readDate(const json::Value& v, time::Seconds& out) {
  if (!v.isString()) return false;
  std::optional<time::Seconds> t = date(v.asString());
  if (!t) return false;
  out = *t;
  return true;
}

std::string errorWhileReading() { return "The server sent a reply that couldn't be read."; }

// The MediaBrowser authorization value; the Token part only once signed in.
std::string authorization(std::string_view token, std::string_view deviceId) {
  std::string auth = "MediaBrowser Client=\"" + std::string(kClientName) + "\", Device=\"" + std::string(kDeviceName) +
                     "\", DeviceId=\"" + std::string(deviceId) + "\", Version=\"" + std::string(kClientVersion) + "\"";
  if (!token.empty()) auth += ", Token=\"" + std::string(token) + "\"";
  return auth;
}

// Trimmed, without trailing slashes.
std::string normalizedBase(std::string_view baseUrl) {
  std::string base(trimWhitespaceAndNewlines(baseUrl));
  while (!base.empty() && base.back() == '/') base.pop_back();
  return base;
}

}  // namespace

std::optional<Error> checkStatus(int status) {
  if (status >= 200 && status <= 299) return std::nullopt;
  if (status <= 0) return Error::make("No response from the server");
  switch (status) {
    // The Swift points at "Settings → Jellyfin"; the Marigold screens differ.
    case 401: return Error::make("Signed out of the server. Sign in again in Settings.", status);
    case 403: return Error::make("This account doesn't have Live TV access.", status);
    case 404: return Error::make("This server doesn't have Live TV.", status);
    default: return Error::make("The server returned an error (" + std::to_string(status) + ").", status);
  }
}

std::string describeErrorCode(std::string_view code) {
  if (code == "NoCompatibleStream") return "The server has no stream this device can play for this channel.";
  if (code == "NotAllowed") return "This account isn't allowed to watch this channel.";
  if (code == "RateLimitExceeded")
    return "Too many streams are open on this account right now. Close one and try again.";
  // Server text: shortened and redacted before it is shown.
  std::string shown(code.substr(0, kMaxErrorCodeBytes));
  return redact("The server couldn't open this channel (" + shown + ").");
}

std::optional<time::Seconds> date(std::string_view raw) {
  std::string value(trimWhitespace(raw));
  if (value.empty()) return std::nullopt;
  size_t dot = value.find('.');
  if (dot != std::string::npos) {
    size_t tail = dot;
    while (tail < value.size() && (value[tail] == '.' || (value[tail] >= '0' && value[tail] <= '9'))) ++tail;
    value = value.substr(0, dot) + value.substr(tail);
  }
  // hasSuffix("Z") or [+-]\d\d:?\d\d$
  auto hasOffset = [&]() {
    auto digit = [&](size_t i) { return value[i] >= '0' && value[i] <= '9'; };
    size_t n = value.size();
    if (n >= 6 && (value[n - 6] == '+' || value[n - 6] == '-') && digit(n - 5) && digit(n - 4) && value[n - 3] == ':' &&
        digit(n - 2) && digit(n - 1))
      return true;
    return n >= 5 && (value[n - 5] == '+' || value[n - 5] == '-') && digit(n - 4) && digit(n - 3) && digit(n - 2) &&
           digit(n - 1);
  };
  if (!(str::endsWith(value, "Z") || hasOffset())) value += "Z";
  return parseInternetDateTime(value);
}

std::vector<std::string> lenientNames(const json::Value& v) {
  std::vector<std::string> found;
  for (const json::Value& item : v.items()) {
    if (item.isString()) {
      found.push_back(item.asString());
    } else if (item.isObject() && (item["Name"].isString() || item["Name"].isNull())) {
      if (item["Name"].isString()) found.push_back(item["Name"].asString());
    } else {
      break;
    }
  }
  return found;
}

// ---------------------------------------------------------------------------
// Sign-in

std::string deviceIdFor(std::string_view id) {
  if (str::startsWith(id, "jf:")) id.remove_prefix(3);
  return id.empty() ? std::string("tv") : std::string(id);
}

Request authenticateRequest(std::string_view baseUrl, std::string_view user, std::string_view password,
                            bool allowInsecureTLS, std::string_view deviceId) {
  Request r;
  r.method = "POST";
  std::string base = normalizedBase(baseUrl);
  if (isValidBaseUrl(base)) r.url = base + "/Users/AuthenticateByName";
  r.timeoutSeconds = kTimeoutSeconds;
  r.allowInsecureTLS = allowInsecureTLS;
  // The Swift sends Authorization; Emby reads X-Emby-Authorization. Both go.
  std::string auth = authorization({}, deviceIdFor(deviceId));
  r.headers.push_back({"Authorization", auth});
  r.headers.push_back({"X-Emby-Authorization", auth});
  r.headers.push_back({"Accept", "application/json"});
  r.headers.push_back({"Content-Type", "application/json"});
  json::Value body;
  body.set("Username", user);
  body.set("Pw", password);
  r.body = json::write(body);
  return r;
}

Result<std::pair<std::string, std::string>> parseAuthenticate(int status, std::string_view body) {
  if (status == 401) return Error::make("Wrong user name or password", status);
  if (std::optional<Error> e = checkStatus(status)) return *e;
  std::optional<json::Value> root = json::parse(body);
  if (!root) return Error::make(errorWhileReading());
  std::optional<std::string> token = stringField((*root)["AccessToken"]);
  std::optional<std::string> userId = stringField((*root)["User"]["Id"]);
  if (!token || !userId) return Error::make(errorWhileReading());
  return std::make_pair(std::move(*token), std::move(*userId));
}

// ---------------------------------------------------------------------------
// Client

Result<Client> Client::make(std::string serverId, std::string_view baseUrl, std::string token, std::string userId,
                            bool allowInsecureTLS) {
  std::string base = normalizedBase(baseUrl);
  if (!isValidBaseUrl(base)) return Error::make("Invalid server address");
  return Client(std::move(serverId), std::move(base), std::move(token), std::move(userId), allowInsecureTLS);
}

Result<Client> Client::make(const Account& account) {
  return make(account.sourceId(), account.serverUrl, account.token, account.userId, account.allowInsecureTLS);
}

std::string Client::makeURL(std::string_view path, std::vector<std::pair<std::string, std::string>> query) const {
  std::string url = base_ + "/" + std::string(path);
  std::string q = queryString(std::move(query));
  if (!q.empty()) url += "?" + q;
  return url;
}

Request Client::request(std::string_view path, std::vector<std::pair<std::string, std::string>> query,
                        std::string_view method, const json::Value* jsonBody) const {
  Request r;
  r.method = std::string(method);
  r.url = makeURL(path, std::move(query));
  r.timeoutSeconds = kTimeoutSeconds;
  r.allowInsecureTLS = insecure_;
  std::string auth = authorization(token_, deviceId_);
  r.headers.push_back({"Authorization", auth});
  r.headers.push_back({"X-Emby-Authorization", auth});
  r.headers.push_back({"X-Emby-Token", token_});
  r.headers.push_back({"Accept", "application/json"});
  if (jsonBody) {
    r.headers.push_back({"Content-Type", "application/json"});
    r.body = json::write(*jsonBody);
  }
  return r;
}

std::string Client::imageURL(const std::string& itemId, const std::string& tag, std::string_view type) const {
  // The Swift adds api_key=<token>; images are served without it, and a
  // token in a logo link would end up in channel caches.
  return makeURL("Items/" + percentEncode(itemId) + "/Images/" + std::string(type),
                 {{"tag", tag}, {"maxHeight", "360"}});
}

std::optional<std::string> Client::serverURL(std::string_view relative) const {
  if (str::startsWith(str::lowerAscii(relative), "http")) {
    for (char c : relative)
      if (static_cast<unsigned char>(c) <= 0x20) return std::nullopt;
    return std::string(relative);
  }
  std::string path = !relative.empty() && relative[0] == '/' ? std::string(relative) : "/" + std::string(relative);
  // Some servers include their own base path ("/emby") already.
  std::string bp(basePath(base_));
  if (!bp.empty() && bp != "/" && str::startsWith(str::lowerAscii(path), str::lowerAscii(bp) + "/"))
    path.erase(0, bp.size());
  std::string url = base_ + path;
  for (char c : url)
    if (static_cast<unsigned char>(c) <= 0x20) return std::nullopt;
  size_t hash = url.find('#');
  std::string fragment = hash == std::string::npos ? std::string() : url.substr(hash);
  if (hash != std::string::npos) url.erase(hash);
  size_t q = url.find('?');
  bool hasKey = false;
  if (q != std::string::npos) {
    for (std::string_view item : str::split(std::string_view(url).substr(q + 1), '&')) {
      std::string_view name = item.substr(0, item.find('='));
      if (str::iequals(name, "api_key")) hasKey = true;
    }
  }
  if (!hasKey) url += (q == std::string::npos ? "?" : (q + 1 == url.size() ? "" : "&")) + std::string("api_key=") +
                      percentEncode(token_);
  return url + fragment;
}

// MARK: Channels

Request Client::channelsRequest() const {
  return request("LiveTv/Channels", {{"UserId", userId_},
                                     {"EnableFavoriteSorting", "true"},
                                     {"EnableImageTypes", "Primary"},
                                     {"ImageTypeLimit", "1"},
                                     {"EnableUserData", "true"},
                                     {"Fields", "ChannelInfo,Tags,Genres"}});
}

std::optional<Channel> Client::channelFromItem(const json::Value& item) const {
  // Swift decodes Id as a required String; a list item without one fails the
  // whole list there and is skipped here.
  if (!item["Id"].isString()) return std::nullopt;
  std::string id = item["Id"].asString();
  if (!isSafeItemId(id)) return std::nullopt;
  Channel c;
  c.id = serverId_ + "|" + id;
  c.serverId = serverId_;
  c.itemId = id;
  c.name = liveNonEmptyField(item["Name"]).value_or("Channel");
  // `Number ?? ChannelNumber`: a present Number wins even when blank. Numbers
  // sent as JSON numbers are read as text (Swift would fail the decode).
  const json::Value& number = item["Number"].isString() || item["Number"].isNumber() ? item["Number"]
                                                                                      : item["ChannelNumber"];
  if (std::optional<std::string> text = lenientString(number)) c.number = liveNonEmpty(trimWhitespace(*text));
  if (std::optional<std::string> tag = optString(item["ImageTags"]["Primary"])) c.imageUrl = imageURL(id, *tag);
  c.isFavorite = optBool(item["UserData"]["IsFavorite"]);
  std::vector<std::string> names = lenientNames(item["Tags"]);
  for (std::string& n : lenientNames(item["TagItems"])) names.push_back(std::move(n));
  for (const std::string& n : names)
    if (std::optional<std::string> t = liveNonEmpty(n)) c.tags.push_back(std::move(*t));
  if (!c.tags.empty()) c.group = c.tags.front();  // what LiveTVWork.annotated picks first
  c.isRadio = item["ChannelType"].isString() && str::lowerAscii(item["ChannelType"].asString()) == "radio";
  return c;
}

Result<std::vector<Channel>> Client::parseChannels(int status, std::string_view body) const {
  std::vector<Channel> out;
  ChannelsReader reader(*this, status, [&](Channel&& c) {
    out.push_back(std::move(c));
    return true;
  });
  reader.feed(body);
  if (std::optional<Error> e = reader.finish()) return *e;
  return out;
}

// MARK: Guide

std::optional<Request> Client::programsRequest(const std::vector<std::string>& channelItemIds, time::Seconds from,
                                               time::Seconds to) const {
  if (channelItemIds.empty()) return std::nullopt;
  return request("LiveTv/Programs", {{"UserId", userId_},
                                     {"ChannelIds", str::join(channelItemIds, ",")},
                                     {"MinEndDate", formatInternetDateTime(from)},
                                     {"MaxStartDate", formatInternetDateTime(to)},
                                     {"SortBy", "StartDate"},
                                     {"EnableTotalRecordCount", "false"},
                                     {"EnableImageTypes", "Primary,Thumb"},
                                     {"ImageTypeLimit", "1"},
                                     {"Fields", "Overview"}});
}

std::optional<Program> Client::programFromItem(const json::Value& item, const TitleGenres& titleGenres) const {
  if (!item["Id"].isString()) return std::nullopt;
  std::optional<std::string> channel = optString(item["ChannelId"]);
  // Plain values rather than optionals: gcc's -Wmaybe-uninitialized misreads
  // optional<int64_t> here in optimised builds.
  time::Seconds start = 0, end = 0;
  if (!channel || !readDate(item["StartDate"], start) || !readDate(item["EndDate"], end) || end <= start)
    return std::nullopt;
  std::string id = item["Id"].asString();
  Program p;
  p.id = serverId_ + "|" + id;
  p.channelId = serverId_ + "|" + *channel;
  p.title = liveNonEmptyField(item["Name"]).value_or("");
  p.episodeTitle = liveNonEmptyField(item["EpisodeTitle"]);
  p.overview = liveNonEmptyField(item["Overview"]);
  p.start = start;
  p.end = end;
  if (optBool(item["IsMovie"])) p.genres |= kGenreMovie;
  if (optBool(item["IsSports"])) p.genres |= kGenreSports;
  if (optBool(item["IsNews"])) p.genres |= kGenreNews;
  if (optBool(item["IsKids"])) p.genres |= kGenreKids;
  p.isLive = optBool(item["IsLive"]);
  p.isPremiere = optBool(item["IsPremiere"]);
  if (std::optional<std::string> thumb = optString(item["ImageTags"]["Thumb"])) {
    p.imageUrl = imageURL(id, *thumb, "Thumb");
  } else if (std::optional<std::string> primary = optString(item["ImageTags"]["Primary"])) {
    p.imageUrl = imageURL(id, *primary);
  }
  // ProgramGenre.withTitleFlags: only when the server tagged nothing.
  if (p.genres == 0 && titleGenres) p.genres = titleGenres(p.title, p.episodeTitle) & (kGenreSports | kGenreNews);
  return p;
}

Result<std::vector<Program>> Client::parsePrograms(int status, std::string_view body,
                                                   const TitleGenres& titleGenres) const {
  if (std::optional<Error> e = checkStatus(status)) return *e;
  std::vector<Program> out;
  json::ArrayReader reader(
      [&](json::Value&& item) {
        if (std::optional<Program> p = programFromItem(item, titleGenres)) out.push_back(std::move(*p));
        return true;
      },
      {"Items"});
  if (!reader.feed(body) || !reader.finish()) return Error::make(errorWhileReading());
  return out;
}

std::vector<Request> Client::searchRequests(const std::string& term, time::Seconds now) const {
  std::vector<Request> out;
  out.push_back(request("LiveTv/Programs", {{"UserId", userId_},
                                            {"SearchTerm", term},
                                            {"HasAired", "false"},
                                            {"MaxStartDate", formatInternetDateTime(now + kSearchHorizon)},
                                            {"Limit", std::to_string(kSearchLimit)},
                                            {"SortBy", "StartDate"},
                                            {"EnableTotalRecordCount", "false"},
                                            {"EnableImageTypes", "Primary,Thumb"},
                                            {"ImageTypeLimit", "1"},
                                            {"Fields", "Overview"}}));
  out.push_back(request("Users/" + percentEncode(userId_) + "/Items",
                        {{"SearchTerm", term},
                         {"IncludeItemTypes", "Program,LiveTvProgram"},
                         {"Recursive", "true"},
                         {"Limit", std::to_string(kSearchLimit)},
                         {"EnableTotalRecordCount", "false"},
                         {"EnableImageTypes", "Primary,Thumb"},
                         {"ImageTypeLimit", "1"},
                         {"Fields", "Overview,ChannelInfo"}}));
  return out;
}

std::vector<Program> Client::parseSearch(const std::string& term, time::Seconds now,
                                         const std::vector<std::pair<int, std::string_view>>& responses,
                                         const TitleGenres& titleGenres) const {
  // Swift lowercases with Unicode rules; this folds ASCII letters only.
  std::string needle = str::lowerAscii(term);
  std::map<std::string, Program> found;
  for (const auto& [status, body] : responses) {
    Result<std::vector<Program>> list = parsePrograms(status, body, titleGenres);
    if (!list) continue;
    for (Program& p : list.value())
      if (p.end > now && str::lowerAscii(p.title).find(needle) != std::string::npos) found[p.id] = std::move(p);
  }
  std::vector<Program> out;
  out.reserve(found.size());
  for (auto& entry : found) out.push_back(std::move(entry.second));
  // By start; ties by id so the order is stable.
  std::stable_sort(out.begin(), out.end(), [](const Program& a, const Program& b) { return a.start < b.start; });
  return out;
}

// MARK: Playback

Request Client::openStreamRequest(const std::string& channelItemId) const {
  json::Value directPlay;
  directPlay.set("Type", "Video");
  directPlay.set("Container", "ts,mpegts,m2ts,mp4,m4v,mkv,mov,hls,m3u8,flv,avi,webm");
  json::Value transcoding;
  transcoding.set("Type", "Video");
  transcoding.set("Container", "ts");
  transcoding.set("Protocol", "hls");
  transcoding.set("Context", "Streaming");
  transcoding.set("VideoCodec", "h264,hevc");
  transcoding.set("AudioCodec", "aac,ac3,eac3,mp3");
  transcoding.set("BreakOnNonKeyFrames", true);
  transcoding.set("MinSegments", 1);
  json::Value subtitle;
  subtitle.set("Format", "vtt");
  subtitle.set("Method", "Hls");
  json::Value profile;
  profile.set("Name", std::string(kClientName) + " " + std::string(kDeviceName));
  profile.set("MaxStreamingBitrate", kMaxStreamingBitrate);
  profile.set("MaxStaticBitrate", kMaxStreamingBitrate);
  profile.set("DirectPlayProfiles", json::Value(json::Value::Array{directPlay}));
  profile.set("TranscodingProfiles", json::Value(json::Value::Array{transcoding}));
  profile.set("ContainerProfiles", json::Value::array());
  profile.set("CodecProfiles", json::Value::array());
  profile.set("SubtitleProfiles", json::Value(json::Value::Array{subtitle}));
  json::Value body;
  body.set("UserId", userId_);
  body.set("AutoOpenLiveStream", true);
  body.set("IsPlayback", true);
  body.set("MaxStreamingBitrate", kMaxStreamingBitrate);
  body.set("DeviceProfile", profile);
  return request("Items/" + percentEncode(channelItemId) + "/PlaybackInfo",
                 {{"UserId", userId_}, {"AutoOpenLiveStream", "true"}, {"IsPlayback", "true"}}, "POST", &body);
}

Result<Stream> Client::parseOpenStream(int status, std::string_view body, const std::string& channelItemId) const {
  if (std::optional<Error> e = checkStatus(status)) return *e;
  std::optional<json::Value> root = json::parse(body);
  if (!root || !root->isObject()) return Error::make(errorWhileReading());
  if (std::optional<std::string> code = optString((*root)["ErrorCode"])) return Error::make(describeErrorCode(*code));
  const json::Value& sources = (*root)["MediaSources"];
  if (!sources.isArray() || sources.size() == 0)
    return Error::make("The server didn't return a stream for this channel.");
  const json::Value& source = sources[size_t{0}];
  Stream s;
  s.liveStreamId = optString(source["LiveStreamId"]);
  s.playSessionId = optString((*root)["PlaySessionId"]);
  s.itemId = channelItemId;
  s.mediaSourceId = optString(source["Id"]);

  if (std::optional<std::string> direct = liveNonEmptyField(source["DirectStreamUrl"])) {
    if (std::optional<std::string> url = serverURL(*direct)) {
      s.url = std::move(*url);
      return s;
    }
  }
  if (optBool(source["SupportsDirectStream"])) {
    std::vector<std::pair<std::string, std::string>> query = {{"Static", "true"},
                                                              {"MediaSourceId", s.mediaSourceId.value_or(channelItemId)},
                                                              {"DeviceId", deviceId_},
                                                              {"api_key", token_}};
    if (s.liveStreamId) query.emplace_back("LiveStreamId", *s.liveStreamId);
    if (s.playSessionId) query.emplace_back("PlaySessionId", *s.playSessionId);
    s.url = makeURL("Videos/" + percentEncode(channelItemId) + "/stream.ts", std::move(query));
    return s;
  }
  if (std::optional<std::string> transcode = liveNonEmptyField(source["TranscodingUrl"])) {
    if (std::optional<std::string> url = serverURL(*transcode)) {
      s.url = std::move(*url);
      s.isTranscode = true;
      return s;
    }
  }
  if (std::optional<std::string> path = optString(source["Path"])) {
    if (str::startsWith(str::lowerAscii(*path), "http") && path->find_first_of(" \t\r\n") == std::string::npos) {
      s.url = std::move(*path);
      return s;
    }
  }
  return Error::make("This channel can't be streamed to this device.");
}

std::string Client::quickStreamURL(const std::string& channelItemId) const {
  return makeURL("Videos/" + percentEncode(channelItemId) + "/stream.ts",
                 {{"Static", "true"}, {"MediaSourceId", channelItemId}, {"DeviceId", deviceId_}, {"api_key", token_}});
}

json::Value Client::playbackBody(const Stream& stream) const {
  json::Value body;
  body.set("ItemId", stream.itemId);
  body.set("PositionTicks", 0);
  body.set("CanSeek", false);
  body.set("IsPaused", false);
  body.set("PlayMethod", stream.isTranscode ? "Transcode" : "DirectStream");
  if (stream.mediaSourceId) body.set("MediaSourceId", *stream.mediaSourceId);
  if (stream.playSessionId) body.set("PlaySessionId", *stream.playSessionId);
  if (stream.liveStreamId) body.set("LiveStreamId", *stream.liveStreamId);
  return body;
}

Request Client::reportPlayingRequest(const Stream& stream) const {
  json::Value body = playbackBody(stream);
  return request("Sessions/Playing", {}, "POST", &body);
}

std::vector<Request> Client::closeStreamRequests(const Stream& stream) const {
  std::vector<Request> out;
  json::Value body = playbackBody(stream);
  out.push_back(request("Sessions/Playing/Stopped", {}, "POST", &body));
  if (stream.isTranscode) {
    std::vector<std::pair<std::string, std::string>> query = {{"DeviceId", deviceId_}};
    if (stream.playSessionId) query.emplace_back("PlaySessionId", *stream.playSessionId);
    out.push_back(request("Videos/ActiveEncodings", std::move(query), "DELETE"));
  }
  if (stream.liveStreamId) {
    json::Value empty = json::Value::object();
    out.push_back(request("LiveStreams/Close", {{"LiveStreamId", *stream.liveStreamId}}, "POST", &empty));
  }
  return out;
}

Request Client::setFavoriteRequest(bool favorite, const std::string& itemId) const {
  return request("Users/" + percentEncode(userId_) + "/FavoriteItems/" + percentEncode(itemId), {},
                 favorite ? "POST" : "DELETE");
}

// ---------------------------------------------------------------------------
// ChannelsReader

ChannelsReader::ChannelsReader(Client client, int status, OnChannel onChannel)
    : client_(std::move(client)),
      statusError_(checkStatus(status)),
      onChannel_(std::move(onChannel)),
      reader_(
          [this](json::Value&& item) {
            std::optional<Channel> c = client_.channelFromItem(item);
            if (!c) {
              ++skipped_;
              return true;
            }
            ++channels_;
            if (!onChannel_(std::move(*c))) {
              stoppedByCaller_ = true;
              return false;
            }
            return true;
          },
          {"Items"}) {}

bool ChannelsReader::feed(std::string_view chunk) {
  if (statusError_) return false;
  return reader_.feed(chunk);
}

std::optional<Error> ChannelsReader::finish() {
  if (statusError_) return statusError_;
  if (stoppedByCaller_) return std::nullopt;
  // A missing or null Items is an empty list (Items is optional in the Swift).
  if (!reader_.finish()) return Error::make(errorWhileReading());
  return std::nullopt;
}

}  // namespace marigold::sources::emby
