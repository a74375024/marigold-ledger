#include "core/sources/xtream.hpp"

#include <algorithm>
#include <utility>

#include "core/util/base64.hpp"
#include "core/util/json.hpp"
#include "core/util/strings.hpp"

namespace marigold::sources::xtream {
namespace {

std::optional<int> toInt(std::optional<int64_t> v) {
  if (!v || *v < INT32_MIN || *v > INT32_MAX) return std::nullopt;
  return static_cast<int>(*v);
}

// XtreamClient.decode: base64 text when it decodes to UTF-8 (trimmed),
// otherwise the text as given.
std::optional<std::string> decodeText(const json::Value& v) {
  if (!v.isString()) return std::nullopt;
  std::string text = v.asString();
  if (text.empty()) return std::nullopt;
  if (std::optional<std::string> bytes = base64::decode(text); bytes && str::utf8Valid(*bytes))
    return std::string(trimWhitespaceAndNewlines(*bytes));
  return text;
}

bool contains(const std::vector<std::string>& list, std::string_view value) {
  return std::find(list.begin(), list.end(), value) != list.end();
}

}  // namespace

std::optional<Error> checkStatus(int status) {
  if (status >= 200 && status <= 299) return std::nullopt;
  if (status <= 0) return Error::make("No response from the provider");
  if (status == 401 || status == 403)
    return Error::make("The provider refused the sign-in (" + std::to_string(status) + ").", status);
  return Error::make("The provider returned an error (" + std::to_string(status) + ").", status);
}

std::optional<std::string> serverURL(std::string_view serverUrlString) {
  std::string text(trimWhitespaceAndNewlines(serverUrlString));
  while (!text.empty() && text.back() == '/') text.pop_back();
  if (!str::startsWith(str::lowerAscii(text), "http")) text = "http://" + text;
  // URL(string:) in Swift; here a stricter check (see isValidBaseUrl).
  if (!isValidBaseUrl(text)) return std::nullopt;
  return text;
}

void applyAccountInfo(Account& account, const AccountInfo& info) {
  account.expires = info.expires;
  account.maxConnections = info.maxConnections;
}

Result<Client> Client::make(const Account& account) {
  std::optional<std::string> base = serverURL(account.serverUrl);
  if (!base) return Error::make("Invalid server address");
  return Client(account, std::move(*base));
}

// The login goes first, then the call's own parameters sorted by name, as
// the Swift builds them.
Request Client::get(std::vector<std::pair<std::string, std::string>> query) const {
  std::string url = base_ + std::string(kApiPath) + "?" + loginQuery();
  std::string rest = queryString(std::move(query));
  if (!rest.empty()) url += "&" + rest;
  Request r;
  r.url = std::move(url);
  r.timeoutSeconds = kTimeoutSeconds;
  r.allowInsecureTLS = account_.allowInsecureTLS;
  r.headers.push_back({"User-Agent", std::string(kUserAgent)});
  return r;
}

Request Client::accountInfoRequest() const { return get({}); }

Result<AccountInfo> Client::parseAccountInfo(int status, std::string_view body) const {
  if (std::optional<Error> e = checkStatus(status)) return *e;
  std::optional<json::Value> root = json::parse(body);
  if (!root || !root->isObject() || !(*root)["user_info"].isObject())
    return Error::make("The server didn't answer like an Xtream provider. Check the address (including the port).");
  const json::Value& user = (*root)["user_info"];
  if (lenientInt(user["auth"]).value_or(0) != 1) return Error::make("Wrong username or password.");
  AccountInfo info;
  info.status = user["status"].isString() ? user["status"].asString() : "Active";
  if (std::optional<int64_t> exp = lenientInt(user["exp_date"]); exp && *exp > 0) info.expires = *exp;
  // `as? [String]`: a list with anything but strings counts as none.
  const json::Value& formats = user["allowed_output_formats"];
  bool allStrings = formats.isArray();
  for (const json::Value& f : formats.items()) allStrings = allStrings && f.isString();
  if (allStrings)
    for (const json::Value& f : formats.items()) info.formats.push_back(f.asString());
  info.maxConnections = toInt(lenientInt(user["max_connections"]));
  info.activeConnections = toInt(lenientInt(user["active_cons"]));
  return info;
}

Request Client::categoriesRequest() const { return get({{"action", "get_live_categories"}}); }
Request Client::streamsRequest() const { return get({{"action", "get_live_streams"}}); }

Request Client::shortEpgRequest(std::string_view streamId, int limit) const {
  return get({{"action", "get_short_epg"}, {"stream_id", std::string(streamId)}, {"limit", std::to_string(limit)}});
}

std::vector<Program> Client::parseShortEpg(int status, std::string_view body, const std::string& channelId,
                                           const TitleGenres& titleGenres) const {
  std::vector<Program> out;
  if (checkStatus(status)) return out;
  std::optional<json::Value> root = json::parse(body);
  if (!root || !(*root)["epg_listings"].isArray()) return out;
  for (const json::Value& item : (*root)["epg_listings"].items()) {
    std::optional<int64_t> start = lenientInt(item["start_timestamp"]);
    std::optional<int64_t> end = lenientInt(item["stop_timestamp"]);
    if (!start || !end || *end <= *start) continue;
    std::string title = decodeText(item["title"]).value_or("");
    if (title.empty()) continue;
    Program p;
    p.id = channelId + "|" + std::to_string(*start);
    p.channelId = channelId;
    p.title = std::move(title);
    p.overview = decodeText(item["description"]);
    p.start = *start;
    p.end = *end;
    // Only sports and news can be told from a title (isMovie and isKids
    // stay false, as in the Swift).
    if (titleGenres) p.genres = titleGenres(p.title, std::nullopt) & (kGenreSports | kGenreNews);
    out.push_back(std::move(p));
  }
  return out;
}

std::string Client::guideURL() const { return base_ + std::string(kGuidePath) + "?" + loginQuery(); }

// "username=<user>&password=<password>", both percent-encoded. Secret.
std::string Client::loginQuery() const {
  return "username=" + percentEncode(account_.userName) + "&password=" + percentEncode(account_.password);
}

std::optional<std::string> Client::streamURL(std::string_view streamId, const std::vector<std::string>& formats,
                                             bool preferTS) const {
  if (!isSafeItemId(streamId)) return std::nullopt;
  bool allowsTS = formats.empty() || contains(formats, "ts");
  bool allowsHLS = formats.empty() || contains(formats, "m3u8");
  const char* ext = (preferTS && allowsTS) || !allowsHLS ? "ts" : "m3u8";
  return base_ + "/live/" + percentEncode(account_.userName) + "/" + percentEncode(account_.password) + "/" +
         std::string(streamId) + "." + ext;
}

CategoryNames parseCategories(int status, std::string_view body) {
  CategoryNames names;
  if (checkStatus(status)) return names;
  json::ArrayReader reader([&](json::Value&& item) {
    std::optional<std::string> id = lenientString(item["category_id"]);
    const json::Value& name = item["category_name"];
    if (id && name.isString()) {
      std::string trimmed(trimWhitespaceAndNewlines(name.asString()));
      // The Swift keeps an empty name (and tags the channel ""); an empty
      // tag would only make an unnamed group, so it is dropped here.
      if (!trimmed.empty()) names[*id] = std::move(trimmed);
    }
    return true;
  });
  if (!reader.feed(body) || !reader.finish()) return CategoryNames();
  return names;
}

std::optional<Channel> channelFromStream(const json::Value& item, const std::string& sourceId,
                                         const CategoryNames& categories) {
  std::optional<std::string> streamId = lenientString(item["stream_id"]);
  if (!streamId || !isSafeItemId(*streamId) || !item["name"].isString()) return std::nullopt;
  std::string name(trimWhitespaceAndNewlines(item["name"].asString()));
  if (name.empty()) return std::nullopt;
  Channel c;
  c.id = sourceId + "|" + *streamId;
  c.serverId = sourceId;
  c.itemId = std::move(*streamId);
  c.name = std::move(name);
  c.number = lenientString(item["num"]);
  if (std::optional<std::string> icon = stringField(item["stream_icon"])) c.imageUrl = std::move(*icon);
  if (std::optional<std::string> id = lenientString(item["category_id"])) {
    auto it = categories.find(*id);
    if (it != categories.end()) {
      c.tags.push_back(it->second);
      // What LiveTVWork.annotated picks first (the channel's first tag).
      c.group = it->second;
    }
  }
  c.epgId = stringField(item["epg_channel_id"]);
  // Not in the Swift: Xtream marks radio streams in the same list.
  c.isRadio = item["stream_type"].isString() && item["stream_type"].asString() == "radio_streams";
  return c;
}

StreamsReader::StreamsReader(int status, std::string sourceId, CategoryNames categories, OnChannel onChannel)
    : statusError_(checkStatus(status)),
      sourceId_(std::move(sourceId)),
      categories_(std::move(categories)),
      onChannel_(std::move(onChannel)),
      reader_([this](json::Value&& item) {
        std::optional<Channel> c = channelFromStream(item, sourceId_, categories_);
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
      }) {}

bool StreamsReader::feed(std::string_view chunk) {
  if (statusError_) return false;
  return reader_.feed(chunk);
}

std::optional<Error> StreamsReader::finish() {
  if (statusError_) return statusError_;
  if (stoppedByCaller_) return std::nullopt;
  if (!reader_.finish() || !reader_.foundArray()) return Error::make("The provider didn't return a channel list.");
  return std::nullopt;
}

Result<std::vector<Channel>> parseChannels(const std::string& sourceId, int categoriesStatus,
                                           std::string_view categoriesBody, int streamsStatus,
                                           std::string_view streamsBody) {
  std::vector<Channel> out;
  StreamsReader reader(streamsStatus, sourceId, parseCategories(categoriesStatus, categoriesBody),
                       [&](Channel&& c) {
                         out.push_back(std::move(c));
                         return true;
                       });
  reader.feed(streamsBody);
  if (std::optional<Error> e = reader.finish()) return *e;
  return out;
}

}  // namespace marigold::sources::xtream
