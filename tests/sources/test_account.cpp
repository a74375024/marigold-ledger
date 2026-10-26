#include "core/sources/account.hpp"

#include <string>
#include <vector>

#include "core/util/json.hpp"
#include "doctest.h"

using namespace marigold;
using namespace marigold::sources;

namespace {

std::vector<Account> sample() {
  Account x;
  x.kind = AccountKind::Xtream;
  x.id = "acc-1";
  x.displayName = "Mock IPTV";
  x.serverUrl = "http://provider.example.invalid:8080";
  x.userName = "u";
  x.password = "p";
  x.preferTS = true;
  x.expires = 1900000000;
  x.maxConnections = 2;
  x.lastChannelCount = 60000;

  Account e;
  e.kind = AccountKind::Emby;
  e.id = "srv_2";
  e.displayName = "Home Emby";
  e.serverUrl = "https://emby.example.invalid/emby";
  e.userName = "viewer";
  e.userId = "f00d";
  e.token = "t0k";
  e.allowInsecureTLS = true;

  Account m;
  m.kind = AccountKind::M3U;
  m.id = "pl.3";
  m.displayName = "";
  m.playlistUrl = "http://pl.example.invalid/get.php?type=m3u_plus&output=ts";
  m.guideUrl = "http://guide.example.invalid/xmltv.php?k=1";
  m.guideFromPlaylist = true;
  return {x, e, m};
}

}  // namespace

TEST_CASE("accounts: source ids and kinds") {
  std::vector<Account> list = sample();
  CHECK(list[0].sourceId() == "xt:acc-1");
  CHECK(list[1].sourceId() == "jf:srv_2");
  CHECK(list[2].sourceId() == "m3u:pl.3");
  CHECK(kindName(AccountKind::Emby) == "emby");
  CHECK(kindFromName("jellyfin") == AccountKind::Emby);
  CHECK(kindFromName("m3u") == AccountKind::M3U);
  CHECK_FALSE(kindFromName("XTREAM"));
  for (const Account& a : list) CHECK(hasSecrets(a));
  Account empty;
  CHECK_FALSE(hasSecrets(empty));
}

TEST_CASE("accounts: secrets stay out of the main JSON") {
  std::vector<Account> list = sample();
  EncodedAccounts enc = encodeAccounts(list);
  CHECK(enc.main.find("\"p\"") == std::string::npos);
  CHECK(enc.main.find("password") == std::string::npos);
  CHECK(enc.main.find("t0k") == std::string::npos);
  CHECK(enc.main.find("token") == std::string::npos);
  CHECK(enc.main.find("pl.example.invalid") == std::string::npos);
  CHECK(enc.main.find("get.php") == std::string::npos);
  CHECK(enc.main.find("guide.example.invalid") == std::string::npos);
  CHECK(enc.secrets.find("guide.example.invalid") != std::string::npos);
  CHECK(enc.secrets.find("t0k") != std::string::npos);
  CHECK(enc.secrets.find("pl.example.invalid") != std::string::npos);
  // The secrets document holds secrets only: no names or servers.
  CHECK(enc.secrets.find("Mock IPTV") == std::string::npos);
  CHECK(enc.secrets.find("provider.example.invalid") == std::string::npos);

  auto main = json::parse(enc.main);
  REQUIRE(main);
  CHECK((*main)["version"].asInt() == 1);
  CHECK((*main)["accounts"].size() == 3);
  CHECK((*main)["accounts"][size_t{0}]["server"].asString() == "http://provider.example.invalid:8080");
  CHECK_FALSE((*main)["accounts"][size_t{2}].has("server"));
}

TEST_CASE("accounts: round trip") {
  std::vector<Account> list = sample();
  EncodedAccounts enc = encodeAccounts(list);
  Result<std::vector<Account>> back = decodeAccounts(enc.main, enc.secrets);
  REQUIRE(back.ok());
  REQUIRE(back.value().size() == 3);
  const Account& x = back.value()[0];
  CHECK(x.kind == AccountKind::Xtream);
  CHECK(x.id == "acc-1");
  CHECK(x.displayName == "Mock IPTV");
  CHECK(x.serverUrl == "http://provider.example.invalid:8080");
  CHECK(x.userName == "u");
  CHECK(x.password == "p");
  CHECK(x.preferTS);
  CHECK_FALSE(x.allowInsecureTLS);
  CHECK(x.expires == 1900000000);
  CHECK(x.maxConnections == 2);
  CHECK(x.lastChannelCount == 60000);
  const Account& e = back.value()[1];
  CHECK(e.kind == AccountKind::Emby);
  CHECK(e.userId == "f00d");
  CHECK(e.token == "t0k");
  CHECK(e.allowInsecureTLS);
  CHECK_FALSE(e.expires);
  const Account& m = back.value()[2];
  CHECK(m.kind == AccountKind::M3U);
  CHECK(m.playlistUrl == "http://pl.example.invalid/get.php?type=m3u_plus&output=ts");
  CHECK(m.serverUrl.empty());
  CHECK(m.guideUrl == "http://guide.example.invalid/xmltv.php?k=1");
  CHECK(m.guideFromPlaylist);

  // Without the secrets file: accounts load, and need signing in again.
  Result<std::vector<Account>> bare = decodeAccounts(enc.main, "");
  REQUIRE(bare.ok());
  for (const Account& a : bare.value()) CHECK_FALSE(hasSecrets(a));
  Result<std::vector<Account>> garbled = decodeAccounts(enc.main, "{not json");
  REQUIRE(garbled.ok());
  CHECK(garbled.value().size() == 3);
}

TEST_CASE("accounts: bad documents and entries") {
  CHECK_FALSE(decodeAccounts("", "").ok());
  CHECK_FALSE(decodeAccounts("[]", "").ok());
  CHECK(decodeAccounts("{not json", "").error().message == "The saved accounts couldn't be read.");
  auto r = decodeAccounts(R"({"version":1,"accounts":[
      {"id":"a","kind":"xtream","name":"A"},
      {"id":"a","kind":"xtream","name":"dup"},
      {"id":"bad/id","kind":"xtream"},
      {"id":"b","kind":"ftp"},
      {"kind":"m3u"},
      {"id":"c","kind":"m3u","server":"http://x.invalid","maxConnections":"3","expires":"soon"}]})",
                          R"({"accounts":{"c":{"playlist":"http://pl.example.invalid/x"}}})");
  REQUIRE(r.ok());
  REQUIRE(r.value().size() == 2);
  CHECK(r.value()[0].displayName == "A");
  CHECK(r.value()[1].serverUrl.empty());  // M3U accounts have no server field
  CHECK_FALSE(r.value()[1].maxConnections);
  CHECK_FALSE(r.value()[1].expires);
  CHECK(r.value()[1].playlistUrl == "http://pl.example.invalid/x");
}

TEST_CASE("accounts: server addresses lose any login") {
  CHECK(stripServerSecrets("http://u:p@provider.example.invalid:8080/x") == "http://provider.example.invalid:8080/x");
  CHECK(stripServerSecrets("http://provider.example.invalid/get.php?type=m3u#x") ==
        "http://provider.example.invalid/get.php");
  CHECK(stripServerSecrets("provider.example.invalid") == "provider.example.invalid");
  CHECK(stripServerSecrets("http://a@b.invalid") == "http://b.invalid");
  Account a = sample()[0];
  a.serverUrl = "http://u:p@provider.example.invalid:8080/?extra=1";
  EncodedAccounts enc = encodeAccounts({a});
  CHECK(enc.main.find("u:p@") == std::string::npos);
  CHECK(enc.main.find("extra") == std::string::npos);
}

TEST_CASE("accounts: describe() never shows a host or a login") {
  std::vector<Account> list = sample();
  CHECK(describe(list[0]) == "Xtream \"Mock IPTV\", 60000 channels, 2 connection(s)");
  CHECK(describe(list[1]) == "Emby \"Home Emby\", certificate checks off");
  CHECK(describe(list[2]) == "M3U \"Playlist\"");
  Account bare = list[0];
  bare.password.clear();
  bare.lastChannelCount.reset();
  bare.maxConnections.reset();
  CHECK(describe(bare) == "Xtream \"Mock IPTV\", needs sign-in");
  // A name that is a link (or holds a login) is not shown.
  Account linky = list[0];
  linky.displayName = "http://provider.example.invalid:8080/get.php?type=m3u";
  CHECK(describe(linky).find("example") == std::string::npos);
  CHECK(describe(linky).find("Xtream provider") != std::string::npos);
  linky.displayName = "My list password=p";
  CHECK(describe(linky).find("password=p") == std::string::npos);
  for (const Account& a : list) {
    std::string d = describe(a);
    CHECK(d.find("example.invalid") == std::string::npos);
    CHECK(d.find("viewer") == std::string::npos);
    CHECK(d.find("t0k") == std::string::npos);
  }
}
