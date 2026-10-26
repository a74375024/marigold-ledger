// Provider accounts: Xtream logins, Emby / Jellyfin servers and M3U
// playlists. Mirrors XtreamAccount (and the Jellyfin server config) of the
// tvOS app.
//
// The TV has no keychain, so secrets live in a second JSON document that the
// caller writes to its own 0600 file: passwords, tokens and playlist links
// (a playlist link usually carries the login) never appear in the main
// accounts JSON, in logs, or in describe().
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/sources/common.hpp"
#include "core/util/time.hpp"

namespace marigold::sources {

enum class AccountKind { Xtream, Emby, M3U };

struct Account {
  AccountKind kind = AccountKind::Xtream;
  // Made by the app (a UUID); letters, digits, '-', '_' and '.' only.
  std::string id;
  std::string displayName;
  // Xtream / Emby address, scheme and port included
  // ("http://provider.example:8080"). Unused for M3U.
  std::string serverUrl;
  std::string userName;   // Xtream / Emby sign-in name
  std::string userId;     // Emby / Jellyfin user id, resolved at sign-in
  // Xtream: MPEG-TS instead of HLS whenever the provider allows it (many
  // providers build HLS on the fly, which can stutter more).
  bool preferTS = false;
  // Skip certificate checks for this provider (a broken or old certificate).
  bool allowInsecureTLS = false;
  // Learned from the provider (Xtream account info, last channel load).
  std::optional<time::Seconds> expires;
  std::optional<int> maxConnections;
  std::optional<int> lastChannelCount;

  // Secrets: written only to the secrets JSON.
  std::string password;     // Xtream
  std::string token;        // Emby / Jellyfin access token
  std::string playlistUrl;  // M3U link

  // Channels from this account use this as their server id:
  // "xt:<id>", "jf:<id>" or "m3u:<id>".
  std::string sourceId() const;
};

std::string_view kindName(AccountKind kind);  // "xtream", "emby", "m3u"
std::optional<AccountKind> kindFromName(std::string_view name);

// True when the account has the secret it needs (password, token or link).
bool hasSecrets(const Account& account);

struct EncodedAccounts {
  std::string main;     // safe to keep with the other settings
  std::string secrets;  // write to a 0600 file; never export or log
};

// Server addresses are stored without any user:password@, query or fragment.
EncodedAccounts encodeAccounts(const std::vector<Account>& accounts);

// Reads both documents back. Entries with a bad id or kind, or a repeated
// id, are skipped. An empty or unreadable secrets document leaves the
// secrets empty (hasSecrets() is then false and the user signs in again);
// an unreadable main document is an error.
Result<std::vector<Account>> decodeAccounts(std::string_view main, std::string_view secrets);

// One line for logs and the UI: kind, provider name and counts. Never a host,
// URL, user name, password or token; the name itself goes through redact().
std::string describe(const Account& account);

// Server address with any user:password@, query and fragment removed.
std::string stripServerSecrets(std::string_view url);

}  // namespace marigold::sources
