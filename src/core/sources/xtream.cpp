#include "core/sources/xtream.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "core/net/lan_http.hpp"
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

// A whole number sent as a number or as text ("7", 7); 0 otherwise.
int64_t digits(const json::Value& v) {
  std::optional<std::string> text = lenientString(v);
  if (!text || text->empty() || text->size() > 12) return 0;
  int64_t n = 0;
  for (char ch : *text) {
    if (ch < '0' || ch > '9') return 0;
    n = n * 10 + (ch - '0');
  }
  return n;
}

// 1, "1", true.
bool flag(const json::Value& v) { return digits(v) == 1; }

// tv_archive (1 / "1") and tv_archive_duration (days, a number or text):
// the provider's catch-up archive; 0 when it has none.
uint16_t archiveDays(const json::Value& archive, const json::Value& duration) {
  if (!flag(archive)) return 0;
  return static_cast<uint16_t>(std::clamp<int64_t>(digits(duration), 0, 365));
}

bool contains(const std::vector<std::string>& list, std::string_view value) {
  return std::find(list.begin(), list.end(), value) != list.end();
}

// server_info's time_now: "YYYY-MM-DD HH:MM:SS" (or with a 'T'; the seconds
// may be left out), read as if it were UTC.
std::optional<time::Seconds> wallClock(const json::Value& v) {
  if (!v.isString()) return std::nullopt;
  std::string text = v.asString();
  std::string_view s = trimWhitespaceAndNewlines(text);
  auto number = [&s](size_t at, size_t n) -> int {
    if (at + n > s.size()) return -1;
    int out = 0;
    for (size_t i = at; i < at + n; ++i) {
      if (s[i] < '0' || s[i] > '9') return -1;
      out = out * 10 + (s[i] - '0');
    }
    return out;
  };
  if (s.size() < 16 || s[4] != '-' || s[7] != '-' || (s[10] != ' ' && s[10] != 'T') || s[13] != ':')
    return std::nullopt;
  time::Civil c;
  c.year = number(0, 4);
  c.month = number(5, 2);
  c.day = number(8, 2);
  c.hour = number(11, 2);
  c.minute = number(14, 2);
  c.second = s.size() >= 19 && s[16] == ':' ? number(17, 2) : 0;
  if (c.year < 2000 || c.month < 1 || c.month > 12 || c.day < 1 || c.day > time::daysInMonth(c.year, c.month) ||
      c.hour < 0 || c.hour > 23 || c.minute < 0 || c.minute > 59 || c.second < 0 || c.second > 60)
    return std::nullopt;
  return time::secondsFromCivil(c);
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
  account.trial = info.trial;
  if (info.utcOffset) account.serverOffset = info.utcOffset;
}

std::optional<int> serverOffset(std::optional<time::Seconds> timeNow, std::optional<time::Seconds> timestampNow,
                                std::string_view timezone, time::Seconds at) {
  constexpr time::Seconds kQuarter = 15 * 60, kMost = 14 * 3600;
  if (timeNow && timestampNow && *timestampNow > 0) {
    time::Seconds d = *timeNow - *timestampNow;
    // To the nearest quarter hour (the two readings can be a second apart).
    time::Seconds q = (d >= 0 ? d + kQuarter / 2 : d - kQuarter / 2) / kQuarter * kQuarter;
    if (q >= -kMost && q <= kMost) return static_cast<int>(q);
  }
  if (std::optional<time::Zone> z = time::Zone::named(trimWhitespaceAndNewlines(timezone)))
    return z->offsetAt(timestampNow.value_or(at));
  return std::nullopt;
}

std::string offsetText(int seconds) {
  if (seconds == 0) return "UTC";
  int m = std::abs(seconds) / 60;
  std::string out = seconds < 0 ? "UTC\xe2\x88\x92" : "UTC+";
  out += std::to_string(m / 60);
  if (m % 60) {
    char buf[8];
    std::snprintf(buf, sizeof buf, ":%02d", m % 60);
    out += buf;
  }
  return out;
}

int timeshiftMinutes(time::Seconds start, time::Seconds now) {
  time::Seconds back = std::max<time::Seconds>(0, now - start);
  return static_cast<int>(std::clamp<time::Seconds>(back / 60 + 5, 1, kTimeshiftMaxMinutes));
}

bool endedStatus(const AccountInfo& info) { return str::lowerAscii(str::trim(info.status)) == "expired"; }

std::optional<Account> panelAccount(std::string_view playlistUrl) {
  std::string_view url = trimWhitespaceAndNewlines(playlistUrl);
  size_t q = url.find('?');
  if (q == std::string_view::npos) return std::nullopt;
  std::string_view path = url.substr(0, q);
  std::string_view query = url.substr(q + 1);
  if (size_t hash = query.find('#'); hash != std::string_view::npos) query = query.substr(0, hash);
  constexpr std::string_view kGet = "/get.php";
  if (path.size() <= kGet.size() || str::lowerAscii(path.substr(path.size() - kGet.size())) != kGet)
    return std::nullopt;
  // The panel's address: what's left must be a plain base address (a
  // scheme and a host, no login before it).
  std::string base(path.substr(0, path.size() - kGet.size()));
  std::string lower = str::lowerAscii(base);
  if (!str::startsWith(lower, "http://") && !str::startsWith(lower, "https://")) return std::nullopt;
  if (!isValidBaseUrl(base)) return std::nullopt;
  // As the panel reads its query ('+' is a space, %XX a byte).
  std::map<std::string, std::string> form = lan::parseForm(query);
  auto user = form.find("username");
  auto pass = form.find("password");
  if (user == form.end() || pass == form.end() || user->second.empty() || pass->second.empty()) return std::nullopt;
  Account a;
  a.kind = AccountKind::Xtream;
  a.serverUrl = std::move(base);
  a.userName = user->second;
  a.password = pass->second;
  return a;
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
  // A number or text ("1788393600"); null, "" or 0 is no end date.
  if (std::optional<int64_t> exp = lenientInt(user["exp_date"]); exp && *exp > 0) info.expires = *exp;
  info.trial = lenientInt(user["is_trial"]).value_or(0) == 1;
  // `as? [String]`: a list with anything but strings counts as none.
  const json::Value& formats = user["allowed_output_formats"];
  bool allStrings = formats.isArray();
  for (const json::Value& f : formats.items()) allStrings = allStrings && f.isString();
  if (allStrings)
    for (const json::Value& f : formats.items()) info.formats.push_back(f.asString());
  info.maxConnections = toInt(lenientInt(user["max_connections"]));
  info.activeConnections = toInt(lenientInt(user["active_cons"]));
  // The panel's clock, for catch-up links (asked for in its own local time).
  const json::Value& server = (*root)["server_info"];
  if (server.isObject()) {
    if (server["timezone"].isString())
      info.timezone = std::string(trimWhitespaceAndNewlines(server["timezone"].asString()));
    if (std::optional<int64_t> ts = lenientInt(server["timestamp_now"]); ts && *ts > 0) info.timestampNow = *ts;
    info.timeNow = wallClock(server["time_now"]);
    info.utcOffset = serverOffset(info.timeNow, info.timestampNow, info.timezone, time::now());
  }
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

std::optional<std::string> Client::timeshiftURL(std::string_view streamId, time::Seconds start, int minutes,
                                               int utcOffset) const {
  if (!isSafeItemId(streamId)) return std::nullopt;
  minutes = std::clamp(minutes, 1, kTimeshiftMaxMinutes);
  time::Civil c = time::civilFromSeconds(start + utcOffset);
  char when[40];
  std::snprintf(when, sizeof when, "%04d-%02d-%02d:%02d-%02d", c.year, c.month, c.day, c.hour, c.minute);
  return base_ + "/timeshift/" + percentEncode(account_.userName) + "/" + percentEncode(account_.password) + "/" +
         std::to_string(minutes) + "/" + when + "/" + std::string(streamId) + ".ts";
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
  c.archiveDays = archiveDays(item["tv_archive"], item["tv_archive_duration"]);
  c.isAdult = flag(item["is_adult"]);
  c.hasDirectSource = item["direct_source"].isString() && !trimWhitespaceAndNewlines(item["direct_source"].asString()).empty();
  c.added = digits(item["added"]);
  if (item["category_ids"].isArray()) {
    for (const json::Value& id : item["category_ids"].items()) {
      std::optional<std::string> key = lenientString(id);
      if (!key) continue;
      auto it = categories.find(*key);
      if (it == categories.end() || (c.group && it->second == *c.group)) continue;
      if (c.moreGroups.size() > 200) break;
      if (!c.moreGroups.empty()) c.moreGroups += " | ";
      c.moreGroups += it->second;
    }
  }
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
