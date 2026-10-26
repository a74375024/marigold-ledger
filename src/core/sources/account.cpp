#include "core/sources/account.hpp"

#include <cstdint>
#include <set>

#include "core/util/json.hpp"
#include "core/util/redact.hpp"
#include "core/util/strings.hpp"

namespace marigold::sources {
namespace {

constexpr int kFormatVersion = 1;

std::string genericName(AccountKind kind) {
  switch (kind) {
    case AccountKind::Xtream: return "Xtream provider";
    case AccountKind::Emby: return "Emby server";
    case AccountKind::M3U: return "Playlist";
  }
  return "Provider";
}

std::string kindLabel(AccountKind kind) {
  switch (kind) {
    case AccountKind::Xtream: return "Xtream";
    case AccountKind::Emby: return "Emby";
    case AccountKind::M3U: return "M3U";
  }
  return "Provider";
}

std::optional<int> optionalInt(const json::Value& v) {
  if (!v.isNumber()) return std::nullopt;
  int64_t i = v.asInt();
  if (i < INT32_MIN || i > INT32_MAX) return std::nullopt;
  return static_cast<int>(i);
}

}  // namespace

std::string Account::sourceId() const {
  switch (kind) {
    case AccountKind::Xtream: return "xt:" + id;
    case AccountKind::Emby: return "jf:" + id;
    case AccountKind::M3U: return "m3u:" + id;
  }
  return id;
}

std::string_view kindName(AccountKind kind) {
  switch (kind) {
    case AccountKind::Xtream: return "xtream";
    case AccountKind::Emby: return "emby";
    case AccountKind::M3U: return "m3u";
  }
  return "xtream";
}

std::optional<AccountKind> kindFromName(std::string_view name) {
  if (name == "xtream") return AccountKind::Xtream;
  if (name == "emby" || name == "jellyfin") return AccountKind::Emby;
  if (name == "m3u") return AccountKind::M3U;
  return std::nullopt;
}

bool hasSecrets(const Account& a) {
  switch (a.kind) {
    case AccountKind::Xtream: return !a.password.empty();
    case AccountKind::Emby: return !a.token.empty();
    case AccountKind::M3U: return !a.playlistUrl.empty();
  }
  return false;
}

std::string stripServerSecrets(std::string_view url) {
  std::string_view s = url;
  size_t cut = s.find_first_of("?#");
  if (cut != std::string_view::npos) s = s.substr(0, cut);
  size_t scheme = s.find("://");
  size_t hostStart = scheme == std::string_view::npos ? 0 : scheme + 3;
  size_t hostEnd = s.find('/', hostStart);
  std::string_view authority = s.substr(hostStart, hostEnd == std::string_view::npos ? std::string_view::npos
                                                                                      : hostEnd - hostStart);
  size_t at = authority.rfind('@');
  if (at == std::string_view::npos) return std::string(s);
  std::string out(s.substr(0, hostStart));
  out += authority.substr(at + 1);
  if (hostEnd != std::string_view::npos) out += s.substr(hostEnd);
  return out;
}

EncodedAccounts encodeAccounts(const std::vector<Account>& accounts) {
  json::Value list = json::Value::array();
  json::Value secrets = json::Value::object();
  for (const Account& a : accounts) {
    json::Value o;
    o.set("id", a.id);
    o.set("kind", std::string(kindName(a.kind)));
    o.set("name", a.displayName);
    if (a.kind != AccountKind::M3U) {
      o.set("server", stripServerSecrets(a.serverUrl));
      o.set("user", a.userName);
    }
    if (!a.userId.empty()) o.set("userId", a.userId);
    if (a.preferTS) o.set("preferTS", true);
    if (a.allowInsecureTLS) o.set("allowInsecureTLS", true);
    if (a.expires) o.set("expires", static_cast<int64_t>(*a.expires));
    if (a.maxConnections) o.set("maxConnections", *a.maxConnections);
    if (a.lastChannelCount) o.set("lastChannelCount", *a.lastChannelCount);
    list.push(o);

    json::Value s = json::Value::object();
    if (!a.password.empty()) s.set("password", a.password);
    if (!a.token.empty()) s.set("token", a.token);
    if (!a.playlistUrl.empty()) s.set("playlist", a.playlistUrl);
    if (s.size() > 0) secrets.set(a.id, s);
  }
  json::Value main;
  main.set("version", kFormatVersion);
  main.set("accounts", list);
  json::Value secretDoc;
  secretDoc.set("version", kFormatVersion);
  secretDoc.set("accounts", secrets);
  return EncodedAccounts{json::write(main, true), json::write(secretDoc, true)};
}

Result<std::vector<Account>> decodeAccounts(std::string_view main, std::string_view secrets) {
  std::optional<json::Value> doc = json::parse(main);
  if (!doc || !doc->isObject() || !(*doc)["accounts"].isArray())
    return Error::make("The saved accounts couldn't be read.");
  json::Value secretRoot;
  if (std::optional<json::Value> s = json::parse(secrets)) secretRoot = (*s)["accounts"];

  std::vector<Account> out;
  std::set<std::string> seen;
  for (const json::Value& o : (*doc)["accounts"].items()) {
    Account a;
    a.id = o["id"].asString();
    std::optional<AccountKind> kind = kindFromName(o["kind"].asString());
    if (!kind || !isSafeItemId(a.id) || !seen.insert(a.id).second) continue;
    a.kind = *kind;
    a.displayName = o["name"].asString();
    if (a.kind != AccountKind::M3U) {
      a.serverUrl = stripServerSecrets(o["server"].asString());
      a.userName = o["user"].asString();
    }
    a.userId = o["userId"].asString();
    a.preferTS = o["preferTS"].asBool();
    a.allowInsecureTLS = o["allowInsecureTLS"].asBool();
    if (o["expires"].isNumber()) a.expires = o["expires"].asInt();
    a.maxConnections = optionalInt(o["maxConnections"]);
    a.lastChannelCount = optionalInt(o["lastChannelCount"]);
    const json::Value& s = secretRoot[a.id];
    a.password = s["password"].asString();
    a.token = s["token"].asString();
    a.playlistUrl = s["playlist"].asString();
    out.push_back(std::move(a));
  }
  return out;
}

std::string describe(const Account& a) {
  std::string name(str::trim(a.displayName));
  // A name that is (or holds) a link would show the host or the login.
  if (name.empty() || containsSecret(name)) name = genericName(a.kind);
  std::string text = kindLabel(a.kind) + " \"" + name + "\"";
  if (a.lastChannelCount) text += ", " + std::to_string(*a.lastChannelCount) + " channels";
  if (a.maxConnections) text += ", " + std::to_string(*a.maxConnections) + " connection(s)";
  if (a.allowInsecureTLS) text += ", certificate checks off";
  if (!hasSecrets(a)) text += ", needs sign-in";
  return redact(text);
}

}  // namespace marigold::sources
