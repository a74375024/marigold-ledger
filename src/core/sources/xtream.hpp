// The parts of the Xtream Codes player API that Live TV needs: account
// check, live categories and streams, short EPG, and stream URLs. Port of
// XtreamClient.swift.
//
// No networking: the client builds Requests and parses bodies. The channel
// list (get_live_streams, tens of MB on big panels) is parsed as a stream,
// one channel object at a time.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/model/channel.hpp"
#include "core/sources/account.hpp"
#include "core/sources/common.hpp"
#include "core/util/json_stream.hpp"

namespace marigold::sources::xtream {

constexpr int kTimeoutSeconds = 45;
// The short guide's default number of programmes per channel.
constexpr int kShortEpgLimit = 10;
// Sent with every request (the Swift app sends "Mozilla/5.0 (Apple TV) NuvioTV").
constexpr std::string_view kUserAgent = "Mozilla/5.0 (webOS; Linux) Marigold";
// Player API and full guide, under the server address.
constexpr std::string_view kApiPath = "/player_api.php";
constexpr std::string_view kGuidePath = "/xmltv.php";

struct AccountInfo {
  std::string status;  // "Active" when the panel doesn't say
  std::optional<time::Seconds> expires;
  std::optional<int> maxConnections;
  std::optional<int> activeConnections;
  std::vector<std::string> formats;  // allowed_output_formats: "m3u8", "ts"
};

// category_id -> trimmed category_name.
using CategoryNames = std::unordered_map<std::string, std::string>;

// HTTP status check shared by every call: 2xx passes.
std::optional<Error> checkStatus(int status);

// XtreamAccount.serverURL: trimmed, trailing slashes dropped, "http://"
// added when the text doesn't start with "http". nullopt when that still
// isn't a usable address.
std::optional<std::string> serverURL(std::string_view serverUrlString);

// Account info expires and connection limit, saved on the account
// (Settings save in the tvOS app).
void applyAccountInfo(Account& account, const AccountInfo& info);

class Client {
 public:
  // Fails with "Invalid server address" when the address can't be used.
  static Result<Client> make(const Account& account);

  const Account& account() const { return account_; }
  const std::string& base() const { return base_; }
  std::string sourceId() const { return account_.sourceId(); }

  // MARK: Account
  Request accountInfoRequest() const;
  Result<AccountInfo> parseAccountInfo(int status, std::string_view body) const;

  // MARK: Channels
  Request categoriesRequest() const;
  Request streamsRequest() const;

  // MARK: Guide
  Request shortEpgRequest(std::string_view streamId, int limit = kShortEpgLimit) const;
  // Empty on any failure, like the Swift (which uses try?).
  std::vector<Program> parseShortEpg(int status, std::string_view body, const std::string& channelId,
                                     const TitleGenres& titleGenres = nullptr) const;
  // The provider's full XMLTV guide (xmltv.php). Its channel ids are the
  // streams' epg_channel_ids. Secret: carries the login.
  std::string guideURL() const;

  // MARK: Playback
  // Direct stream link. HLS when the provider allows it, otherwise MPEG-TS;
  // `preferTS` picks MPEG-TS whenever it's allowed. An empty `formats` list
  // allows both. nullopt for a stream id that isn't a plain id. Secret.
  std::optional<std::string> streamURL(std::string_view streamId, const std::vector<std::string>& formats,
                                       bool preferTS) const;
  // Same, with the account's own preferTS setting.
  std::optional<std::string> streamURL(std::string_view streamId, const std::vector<std::string>& formats = {}) const {
    return streamURL(streamId, formats, account_.preferTS);
  }

 private:
  Client(Account account, std::string base) : account_(std::move(account)), base_(std::move(base)) {}
  Request get(std::vector<std::pair<std::string, std::string>> query) const;
  std::string loginQuery() const;

  Account account_;
  std::string base_;
};

// get_live_categories. Failures give an empty map (the Swift ignores them:
// channels then have no category tag).
CategoryNames parseCategories(int status, std::string_view body);

// One get_live_streams element as a channel: id "xt:<account>|<stream_id>",
// name trimmed, number from "num", logo from "stream_icon", tags and group
// from the category name, epgId from "epg_channel_id", isRadio for
// stream_type "radio_streams". nullopt without a usable stream_id or name.
std::optional<Channel> channelFromStream(const json::Value& item, const std::string& sourceId,
                                         const CategoryNames& categories);

// Streams a get_live_streams body into channels, one element at a time.
// Construct with the response status, feed the body, then finish().
class StreamsReader {
 public:
  // Return false to stop reading.
  using OnChannel = std::function<bool(Channel&& channel)>;

  StreamsReader(int status, std::string sourceId, CategoryNames categories, OnChannel onChannel);

  // False once reading stopped (bad body, or the callback said stop).
  bool feed(std::string_view chunk);
  // nullopt when the whole list was read (or the callback stopped it).
  std::optional<Error> finish();

  size_t channels() const { return channels_; }
  // Elements that were not usable channels.
  size_t skipped() const { return skipped_; }
  size_t peakElementBytes() const { return reader_.peakElementBytes(); }
  size_t peakTokenBytes() const { return reader_.peakTokenBytes(); }

 private:
  std::optional<Error> statusError_;
  std::string sourceId_;
  CategoryNames categories_;
  OnChannel onChannel_;
  size_t channels_ = 0;
  size_t skipped_ = 0;
  bool stoppedByCaller_ = false;
  json::ArrayReader reader_;
};

// The whole channel load for small bodies: categories then streams.
Result<std::vector<Channel>> parseChannels(const std::string& sourceId, int categoriesStatus,
                                           std::string_view categoriesBody, int streamsStatus,
                                           std::string_view streamsBody);

}  // namespace marigold::sources::xtream
