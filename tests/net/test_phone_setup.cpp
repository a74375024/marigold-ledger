// Phone sign-in: the small HTTP reader and writer, and the session's rules
// (the token, the 6-digit code and its tries, the form and the status the
// phone polls). The sockets are tested in test_lan_server.cpp.
#include <doctest.h>

#include "core/net/lan_http.hpp"
#include "core/net/phone_setup.hpp"
#include "core/util/json.hpp"

using namespace marigold;

namespace {

lan::Request get(const std::string& path) {
  lan::Request r;
  r.method = "GET";
  r.path = path;
  return r;
}

lan::Request post(const std::string& path, const std::string& body) {
  lan::Request r;
  r.method = "POST";
  r.path = path;
  r.body = body;
  return r;
}

const std::string kToken = "00112233445566778899aabbccddeeff";

phone::Session session() {
  // "0001e240" is 123456.
  return phone::Session(kToken, [](size_t) { return std::string("0001e240"); }, "Living room");
}

json::Value body(const lan::Response& r) { return *json::parse(r.body); }

}  // namespace

TEST_CASE("lan http: requests in pieces, bodies by length, and what is refused") {
  lan::Request r;
  std::string head = "POST /abc/send?x=1 HTTP/1.1\r\nHost: tv\r\nContent-Length: 7\r\nCookie: a=1; mg = two \r\n\r\n";
  CHECK(lan::parse(head.substr(0, 20), r) == lan::Parse::Incomplete);
  CHECK(lan::parse(head + "kind=", r) == lan::Parse::Incomplete);
  REQUIRE(lan::parse(head + "kind=m3", r) == lan::Parse::Complete);
  CHECK(r.method == "POST");
  CHECK(r.path == "/abc/send");
  CHECK(r.query == "x=1");
  CHECK(r.body == "kind=m3");
  CHECK(r.headers.at("host") == "tv");
  CHECK(r.cookie("mg") == "two");
  CHECK(r.cookie("none").empty());

  CHECK(lan::parse("GET / HTTP/2\r\n\r\n", r) == lan::Parse::Bad);
  CHECK(lan::parse("GET nothing HTTP/1.1\r\n\r\n", r) == lan::Parse::Bad);
  CHECK(lan::parse("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n", r) == lan::Parse::Bad);
  CHECK(lan::parse("POST / HTTP/1.1\r\nContent-Length: 99999999\r\n\r\n", r) == lan::Parse::Bad);
  CHECK(lan::parse("POST / HTTP/1.1\r\nContent-Length: -1\r\n\r\n", r) == lan::Parse::Bad);
  CHECK(lan::parse("GET / HTTP/1.1\r\n\r\nGET / HTTP/1.1\r\n\r\n", r) == lan::Parse::Bad);
  CHECK(lan::parse(std::string(lan::kMaxHead + 10, 'a'), r) == lan::Parse::Bad);

  std::string out = lan::serialize(lan::Response::html("<p>hi</p>"));
  CHECK(out.rfind("HTTP/1.1 200 OK\r\n", 0) == 0);
  CHECK(out.find("Content-Length: 9\r\n") != std::string::npos);
  CHECK(out.find("Cache-Control: no-store\r\n") != std::string::npos);
  CHECK(out.find("Connection: close\r\n\r\n<p>hi</p>") != std::string::npos);
  CHECK(lan::serialize(lan::Response::redirect("/t/")).find("Location: /t/\r\n") != std::string::npos);
}

TEST_CASE("lan http: form decoding and escaping") {
  auto f = lan::parseForm("server=http%3A%2F%2Fa.b%3A8080&user=me+you&pw=%zz&empty=&flag");
  CHECK(f.at("server") == "http://a.b:8080");
  CHECK(f.at("user") == "me you");
  CHECK(f.at("pw") == "%zz");
  CHECK(f.at("empty").empty());
  CHECK(f.count("flag") == 1);
  CHECK(lan::htmlEscape("<a href=\"x\">&'") == "&lt;a href=&quot;x&quot;&gt;&amp;&#39;");
}

TEST_CASE("phone sign-in: the form page answers only under the token") {
  phone::Session s = session();
  CHECK(s.path() == "/" + kToken + "/");
  CHECK(s.status().code == "123456");
  CHECK(s.status().phase == phone::Phase::Waiting);
  CHECK(s.handle(get("/00112233/")).status == 404);
  CHECK(s.handle(get("/" + kToken + "/status")).status == 200);
  CHECK(s.status().phase == phone::Phase::Waiting);  // polling isn't opening the page
  lan::Response page = s.handle(get("/" + kToken + "/"));
  CHECK(page.status == 200);
  CHECK(page.type.find("text/html") == 0);
  CHECK(page.body.find("Living room") != std::string::npos);
  CHECK(page.body.find("Send to TV") != std::string::npos);
  CHECK(s.status().phase == phone::Phase::Connected);
  CHECK(s.handle(get("/" + kToken)).status == 303);
  CHECK(s.handle(post("/" + kToken + "/", "")).status == 404);
  CHECK(s.handle(get("/favicon.ico")).status == 404);
}

TEST_CASE("phone sign-in: the code opens the form, and five wrong tries bring a new one") {
  int calls = 0;
  phone::Session s(kToken, [&calls](size_t) { return ++calls == 1 ? std::string("0001e240") : std::string("00000007"); }, "TV");
  CHECK(s.handle(get("/")).body.find("Enter the code") != std::string::npos);
  lan::Response ok = s.handle(post("/code", "code=123+456"));
  CHECK(ok.status == 303);
  REQUIRE(ok.headers.size() == 1);
  CHECK(ok.headers[0].second == "/" + kToken + "/");
  for (int i = 1; i < phone::kCodeTries; ++i) {
    lan::Response no = s.handle(post("/code", "code=000000"));
    CHECK(no.status == 200);
    CHECK(no.body.find(std::to_string(phone::kCodeTries - i) + (phone::kCodeTries - i == 1 ? " try" : " tries")) !=
          std::string::npos);
    CHECK(no.body.find(kToken) == std::string::npos);
  }
  CHECK(s.status().code == "123456");
  CHECK(s.handle(post("/code", "code=000000")).body.find("new code") != std::string::npos);
  CHECK(s.status().code == "000007");
  CHECK(s.status().codeChanges == 1);
  CHECK(s.handle(post("/code", "code=123456")).status == 200);  // the old code is gone
  CHECK(s.handle(post("/code", "code=000007")).status == 303);
}

TEST_CASE("phone sign-in: a form goes to the TV once, and the phone follows the result") {
  phone::Session s = session();
  const std::string base = "/" + kToken + "/";
  lan::Response missing = s.handle(post(base + "send", "kind=xtream&server=http%3A%2F%2Fa.b&user=&password="));
  CHECK(missing.status == 400);
  CHECK(body(missing)["ok"].asBool() == false);
  CHECK(body(missing)["fields"].items().size() == 2);
  CHECK_FALSE(s.take());

  lan::Response sent = s.handle(post(base + "send", "kind=xtream&name=Home&server=+http%3A%2F%2Fa.b%3A80+&user=me&password=p+w"));
  CHECK(sent.status == 200);
  CHECK(body(sent)["ok"].asBool());
  CHECK(s.status().phase == phone::Phase::SigningIn);
  CHECK(body(s.handle(get(base + "status")))["phase"].asString() == "signing");
  CHECK(s.handle(post(base + "send", "kind=m3u&server=x")).status == 409);  // one at a time

  auto form = s.take();
  REQUIRE(form);
  CHECK(form->kind == phone::ProviderForm::Kind::Xtream);
  CHECK(form->name == "Home");
  CHECK(form->server == "http://a.b:80");
  CHECK(form->user == "me");
  CHECK(form->password == "p w");
  CHECK_FALSE(s.take());

  s.failed("Wrong username or password.", phone::fieldsForProblem("Wrong username or password."));
  json::Value st = body(s.handle(get(base + "status")));
  CHECK(st["phase"].asString() == "failed");
  CHECK(st["message"].asString() == "Wrong username or password.");
  CHECK(st["fields"].items().size() == 2);

  CHECK(s.handle(post(base + "send", "kind=m3u&server=http%3A%2F%2Fp.q%2Fget.php&guide=http%3A%2F%2Fg")).status == 200);
  form = s.take();
  REQUIRE(form);
  CHECK(form->kind == phone::ProviderForm::Kind::M3U);
  CHECK(form->user.empty());
  CHECK(form->guide == "http://g");
  s.signedIn("My playlist", 1240);
  st = body(s.handle(get(base + "status")));
  CHECK(st["phase"].asString() == "done");
  CHECK(st["provider"].asString() == "My playlist");
  CHECK(st["channels"].asInt() == 1240);
  CHECK(s.handle(post(base + "send", "kind=m3u&server=x")).status == 409);
  // Nothing secret comes back in the status.
  CHECK(s.handle(get(base + "status")).body.find("p.q") == std::string::npos);
}

TEST_CASE("phone sign-in: problems point at fields, and codes read well") {
  CHECK(phone::fieldsForProblem("Wrong user name or password") == std::vector<std::string>{"user", "password"});
  CHECK(phone::fieldsForProblem("The provider refused the sign-in (403).") == std::vector<std::string>{"user", "password"});
  CHECK(phone::fieldsForProblem("Couldn't reach the server (timeout)") == std::vector<std::string>{"server"});
  CHECK(phone::fieldsForProblem("Couldn't download the playlist (404)") == std::vector<std::string>{"server"});
  CHECK(phone::fieldsForProblem("Something else").empty());
  CHECK(phone::codeFromHex("0001e240") == "123456");
  CHECK(phone::codeFromHex("ffffffff") == "967295");
  CHECK(phone::codeFromHex("") == "000000");
  CHECK(phone::spacedCode("418207") == "418 207");
  std::string problem;
  std::vector<std::string> bad;
  CHECK_FALSE(phone::formFrom({{"kind", "other"}}, problem, bad));
  CHECK(problem.find("Pick") == 0);
}
