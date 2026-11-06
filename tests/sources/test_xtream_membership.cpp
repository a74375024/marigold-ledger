// Build 144: what an Xtream panel says about the membership (exp_date,
// is_trial, status) and M3U links that are a panel's own.
#include <string>

#include "core/sources/account.hpp"
#include "core/sources/xtream.hpp"
#include "doctest.h"

using namespace marigold;
using namespace marigold::sources;

namespace {

xtream::Client panel() {
  Account a;
  a.kind = AccountKind::Xtream;
  a.id = "acc1";
  a.serverUrl = "http://provider.example.invalid:8080";
  a.userName = "u";
  a.password = "p";
  Result<xtream::Client> c = xtream::Client::make(a);
  REQUIRE(c.ok());
  return c.value();
}

xtream::AccountInfo info(const std::string& userInfo) {
  auto r = panel().parseAccountInfo(200, R"({"user_info":{"auth":1,)" + userInfo + "}}");
  REQUIRE(r.ok());
  return r.value();
}

}  // namespace

TEST_CASE("xtream membership: exp_date as text, a number, 0, empty or null") {
  CHECK(info(R"("exp_date":"1788393600")").expires == 1788393600);
  CHECK(info(R"("exp_date":1788393600)").expires == 1788393600);
  CHECK(info(R"("exp_date":"1788393600.0")").expires == 1788393600);
  CHECK_FALSE(info(R"("exp_date":"0")").expires);  // unlimited
  CHECK_FALSE(info(R"("exp_date":0)").expires);
  CHECK_FALSE(info(R"("exp_date":"")").expires);
  CHECK_FALSE(info(R"("exp_date":null)").expires);
  CHECK_FALSE(info(R"("exp_date":"never")").expires);
  CHECK_FALSE(info(R"("status":"Active")").expires);  // not sent
}

TEST_CASE("xtream membership: is_trial and status") {
  CHECK(info(R"("is_trial":"1")").trial);
  CHECK(info(R"("is_trial":1)").trial);
  CHECK(info(R"("is_trial":true)").trial);
  CHECK_FALSE(info(R"("is_trial":"0")").trial);
  CHECK_FALSE(info(R"("is_trial":0)").trial);
  CHECK_FALSE(info(R"("is_trial":"")").trial);
  CHECK_FALSE(info(R"("status":"Active")").trial);

  CHECK(info(R"("status":"Expired")").status == "Expired");
  CHECK(xtream::endedStatus(info(R"("status":"Expired")")));
  CHECK(xtream::endedStatus(info(R"("status":" expired ")")));
  CHECK_FALSE(xtream::endedStatus(info(R"("status":"Active")")));
  CHECK_FALSE(xtream::endedStatus(info(R"("status":"Banned")")));
  CHECK_FALSE(xtream::endedStatus(info(R"("exp_date":"1788393600")")));  // "Active" when not said
}

TEST_CASE("xtream membership: saved on the account and kept across launches") {
  Account a;
  a.kind = AccountKind::Xtream;
  a.id = "acc1";
  a.displayName = "Backup TV";
  a.serverUrl = "http://provider.example.invalid:8080";
  a.userName = "u";
  a.password = "p";
  xtream::applyAccountInfo(a, info(R"("exp_date":"1788393600","is_trial":"1","max_connections":"2")"));
  CHECK(a.expires == 1788393600);
  CHECK(a.trial);
  CHECK(a.maxConnections == 2);
  a.noticeDay = 20397;
  EncodedAccounts enc = encodeAccounts({a});
  auto back = decodeAccounts(enc.main, enc.secrets);
  REQUIRE(back.ok());
  REQUIRE(back.value().size() == 1);
  CHECK(back.value()[0].expires == 1788393600);
  CHECK(back.value()[0].trial);
  CHECK(back.value()[0].maxConnections == 2);
  CHECK(back.value()[0].noticeDay == 20397);
  // A renewal with no end date clears it (unlimited shows nothing).
  xtream::applyAccountInfo(a, info(R"("exp_date":null,"is_trial":"0")"));
  CHECK_FALSE(a.expires);
  CHECK_FALSE(a.trial);
}

TEST_CASE("xtream membership: an M3U link from an Xtream panel") {
  auto a = xtream::panelAccount("http://panel.example.invalid:8080/get.php?username=u&password=p&type=m3u_plus&output=ts");
  REQUIRE(a);
  CHECK(a->kind == AccountKind::Xtream);
  CHECK(a->serverUrl == "http://panel.example.invalid:8080");
  CHECK(a->userName == "u");
  CHECK(a->password == "p");
  // Its player API: the same panel and login.
  auto c = xtream::Client::make(*a);
  REQUIRE(c.ok());
  CHECK(c.value().accountInfoRequest().url ==
        "http://panel.example.invalid:8080/player_api.php?username=u&password=p");

  // https, a path before get.php, escapes and spaces, the order of the query.
  auto b = xtream::panelAccount(" https://panel.example.invalid/iptv/GET.PHP?type=m3u&password=p%2Bq&username=a%40b ");
  REQUIRE(b);
  CHECK(b->serverUrl == "https://panel.example.invalid/iptv");
  CHECK(b->userName == "a@b");
  CHECK(b->password == "p+q");
  // As the panel reads it: '+' is a space.
  CHECK(xtream::panelAccount("http://panel.example.invalid/get.php?username=a+b&password=p")->userName == "a b");
  // No type is fine; a fragment is dropped.
  CHECK(xtream::panelAccount("http://panel.example.invalid/get.php?username=u&password=p#x")->password == "p");
}

TEST_CASE("xtream membership: other links are not taken for a panel") {
  for (const char* link : {
           "http://panel.example.invalid:8080/playlist.m3u",
           "http://panel.example.invalid:8080/playlist.m3u?username=u&password=p",
           "http://panel.example.invalid/get.php",
           "http://panel.example.invalid/get.php?",
           "http://panel.example.invalid/get.php?username=u&type=m3u",
           "http://panel.example.invalid/get.php?password=p&type=m3u",
           "http://panel.example.invalid/get.php?username=&password=p",
           "http://panel.example.invalid/get.php?username=u&password=",
           "http://panel.example.invalid/xget.php?username=u&password=p",
           "http://panel.example.invalid/get.php.m3u?username=u&password=p",
           "ftp://panel.example.invalid/get.php?username=u&password=p",
           "http://user:pw@panel.example.invalid/get.php?username=u&password=p",
           "get.php?username=u&password=p",  // no host (.invalid rule)
           "/get.php?username=u&password=p",  // no host (.invalid rule)
           "http:///get.php?username=u&password=p",  // no host (.invalid rule)
           "",
       }) {
    CAPTURE(link);
    CHECK_FALSE(xtream::panelAccount(link));
  }
}
