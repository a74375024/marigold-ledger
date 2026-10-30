// The Live TV part of the Emby / Jellyfin client API: channels, guide,
// search, opening and closing streams, favorites. Port of LiveTVClient.swift.
// Uses the account's sign-in token; any user with Live TV access can make
// these calls (no admin rights needed).
//
// No networking: the client builds Requests and parses bodies.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/model/channel.hpp"
#include "core/sources/account.hpp"
#include "core/sources/common.hpp"
#include "core/util/json_stream.hpp"

namespace marigold::sources::emby {

constexpr int kTimeoutSeconds = 25;
// Plain names, not the app's; the device id is the account's own random id
// (each TV its own, so two never share a session).
constexpr std::string_view kClientName = "Media Player";
constexpr std::string_view kDeviceName = "TV";
constexpr std::string_view kClientVersion = "0.1.0";
constexpr int kMaxStreamingBitrate = 120000000;
// Search: results per route, and how far ahead upcoming programmes are looked for.
constexpr int kSearchLimit = 60;
constexpr time::Seconds kSearchHorizon = 24 * 3600;

// A stream the server opened for a channel (LiveTVStream). `url` carries
// the token: a secret.
struct Stream {
  std::string url;
  // Server-side live stream to close when we're done (tuner / connection slot).
  std::optional<std::string> liveStreamId;
  std::optional<std::string> playSessionId;
  std::string itemId;
  std::optional<std::string> mediaSourceId;
  bool isTranscode = false;
};

// HTTP status check shared by every call: 2xx passes; 401, 403 and 404
// have their own messages.
std::optional<Error> checkStatus(int status);

// PlaybackInfo ErrorCode as a message.
std::string describeErrorCode(std::string_view code);

// EPG dates look like "2026-10-21T20:00:00.0000000Z"; fractional digits vary
// by server, so they're dropped before parsing; a missing zone means UTC.
std::optional<time::Seconds> date(std::string_view raw);

// LenientNames: a list of names that may arrive as strings or as
// {"Name": ...} objects. Reading stops at the first other element (keeping
// what came before); anything but an array gives none.
std::vector<std::string> lenientNames(const json::Value& v);

// MARK: Sign-in (JellyfinClient.authenticate in the tvOS app)

// POST Users/AuthenticateByName with {"Username", "Pw"}: the user name and
// password exchanged once for an access token. The headers carry the client
// identity and no token. The Request holds the password, so it is a secret.
// When the base address can't be used the url is left empty (isValidBaseUrl).
// The id sent as this TV's device for an account ("jf:<id>" or the bare id).
std::string deviceIdFor(std::string_view sourceOrAccountId);
Request authenticateRequest(std::string_view baseUrl, std::string_view user, std::string_view password,
                            bool allowInsecureTLS = false, std::string_view deviceId = {});
// (AccessToken, User.Id). 401 is "Wrong user name or password"; other
// statuses as checkStatus(). Messages never quote the body or the request.
Result<std::pair<std::string, std::string>> parseAuthenticate(int status, std::string_view body);

class Client {
 public:
  // serverId is the channels' server id ("jf:<account>"). Fails with
  // "Invalid server address" when the base address can't be used.
  static Result<Client> make(std::string serverId, std::string_view baseUrl, std::string token, std::string userId,
                             bool allowInsecureTLS = false);
  static Result<Client> make(const Account& account);

  const std::string& serverId() const { return serverId_; }
  const std::string& userId() const { return userId_; }

  // MARK: Channels
  Request channelsRequest() const;
  Result<std::vector<Channel>> parseChannels(int status, std::string_view body) const;
  // One LiveTv/Channels item as a channel; nullopt without a usable Id.
  std::optional<Channel> channelFromItem(const json::Value& item) const;

  // MARK: Guide
  // Programmes overlapping from...to for the given channels (item ids);
  // nullopt when there are no channels to ask about.
  std::optional<Request> programsRequest(const std::vector<std::string>& channelItemIds, time::Seconds from,
                                         time::Seconds to) const;
  Result<std::vector<Program>> parsePrograms(int status, std::string_view body,
                                             const TitleGenres& titleGenres = nullptr) const;

  // Server-side search for upcoming programmes by title. Emby and Jellyfin
  // differ in which search parameters they honor, so both routes are asked
  // and the results filtered to titles that really match.
  std::vector<Request> searchRequests(const std::string& term, time::Seconds now) const;
  // Responses in the order of searchRequests(); failed routes are skipped.
  std::vector<Program> parseSearch(const std::string& term, time::Seconds now,
                                   const std::vector<std::pair<int, std::string_view>>& responses,
                                   const TitleGenres& titleGenres = nullptr) const;

  // MARK: Playback
  // Asks the server to open the channel. Prefers the untouched stream (no
  // server transcoding, which a non-admin account may not be allowed), then
  // the server's HLS.
  Request openStreamRequest(const std::string& channelItemId) const;
  Result<Stream> parseOpenStream(int status, std::string_view body, const std::string& channelItemId) const;
  // Marigold, experimental: the channel's direct stream link, without
  // asking the server to open it first (PlaybackInfo). The server opens the
  // live stream when the link is read; it may refuse.
  std::string quickStreamURL(const std::string& channelItemId) const;

  // Tells the server playback started, so its session (and stream count) is
  // tracked like any Emby / Jellyfin app's.
  Request reportPlayingRequest(const Stream& stream) const;
  // Ends the stream: reports playback stopped (which is how Emby / Jellyfin
  // apps release a live stream), stops any server transcode, then closes the
  // live stream itself. Send in order; each step is best-effort.
  std::vector<Request> closeStreamRequests(const Stream& stream) const;

  Request setFavoriteRequest(bool favorite, const std::string& itemId) const;

  // Item image link (no token: Emby and Jellyfin serve images without one).
  std::string imageURL(const std::string& itemId, const std::string& tag, std::string_view type = "Primary") const;
  // Server-relative links ("/videos/...") joined onto the base, with the
  // token added when the server left it out. Secret.
  std::optional<std::string> serverURL(std::string_view relative) const;

 private:
  Client(std::string serverId, std::string base, std::string token, std::string userId, bool insecure)
      : serverId_(std::move(serverId)),
        base_(std::move(base)),
        token_(std::move(token)),
        userId_(std::move(userId)),
        deviceId_(deviceIdFor(serverId_)),
        insecure_(insecure) {}

  std::string makeURL(std::string_view path, std::vector<std::pair<std::string, std::string>> query) const;
  Request request(std::string_view path, std::vector<std::pair<std::string, std::string>> query = {},
                  std::string_view method = "GET", const json::Value* jsonBody = nullptr) const;
  json::Value playbackBody(const Stream& stream) const;
  std::optional<Program> programFromItem(const json::Value& item, const TitleGenres& titleGenres) const;

  std::string serverId_;
  std::string base_;
  std::string token_;
  std::string userId_;
  std::string deviceId_;
  bool insecure_ = false;
};

// Streams a LiveTv/Channels body ({"Items":[...]}) one channel at a time,
// for servers with very large lineups.
class ChannelsReader {
 public:
  using OnChannel = std::function<bool(Channel&& channel)>;

  ChannelsReader(Client client, int status, OnChannel onChannel);
  bool feed(std::string_view chunk);
  std::optional<Error> finish();
  size_t channels() const { return channels_; }
  size_t skipped() const { return skipped_; }

 private:
  Client client_;
  std::optional<Error> statusError_;
  OnChannel onChannel_;
  size_t channels_ = 0;
  size_t skipped_ = 0;
  bool stoppedByCaller_ = false;
  json::ArrayReader reader_;
};

}  // namespace marigold::sources::emby
