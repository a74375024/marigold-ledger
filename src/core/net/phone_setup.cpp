#include "core/net/phone_setup.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "core/util/json.hpp"

namespace marigold::phone {
namespace {

std::string trimmed(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return {};
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

std::string lowerAscii(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

const char* phaseName(Phase p) {
  switch (p) {
    case Phase::Waiting:
    case Phase::Connected: return "ready";
    case Phase::SigningIn: return "signing";
    case Phase::SignedIn: return "done";
    case Phase::Failed: return "failed";
  }
  return "ready";
}

// The page shared by the code screen and the form: phone-sized, light,
// system fonts, nothing loaded from anywhere else.
const char* const kStyle = R"CSS(
*{box-sizing:border-box}body{margin:0;background:#f2f2f7;color:#111;font:17px/1.4 -apple-system,system-ui,Roboto,"Segoe UI",sans-serif}
main{max-width:460px;margin:0 auto;padding:22px 20px 40px}
.tv{font-size:13px;font-weight:700;letter-spacing:.4px;color:#2f6fea;text-transform:uppercase}
h1{font-size:28px;margin:6px 0 2px}p.sub{color:#6b6b72;font-size:15px;margin:0 0 14px}
.seg{display:flex;background:#e3e3e8;border-radius:10px;padding:3px;margin:16px 0 6px}
.seg button{flex:1;border:0;background:none;font:600 14px system-ui,sans-serif;padding:9px 0;border-radius:8px;color:#333}
.seg button.on{background:#fff;box-shadow:0 1px 3px rgba(0,0,0,.15);color:#000}
label{display:block;font-size:13px;font-weight:600;color:#6b6b72;margin:14px 4px 6px;text-transform:uppercase;letter-spacing:.3px}
input{width:100%;height:48px;border-radius:11px;border:1px solid #d9d9de;background:#fff;padding:0 14px;font-size:17px;color:#111}
input.bad{border:2px solid #e5372b}.pw{position:relative}.pw button{position:absolute;right:8px;top:8px;height:32px;border:0;background:none;color:#2f6fea;font-weight:600}
.go{display:block;width:100%;height:52px;border:0;border-radius:14px;background:#2f6fea;color:#fff;font:700 18px system-ui,sans-serif;margin-top:22px}
.go:disabled{opacity:.6}.note{font-size:13px;color:#8a8a90;margin-top:14px}
.err{background:#fdecea;color:#b3261e;border-radius:12px;padding:12px 14px;font-size:15px;margin-top:12px}
.ok{width:86px;height:86px;border-radius:43px;background:#34c759;color:#fff;font-size:46px;font-weight:800;display:flex;align-items:center;justify-content:center;margin:60px auto 18px}
.center{text-align:center}.hide{display:none}
.spin{width:44px;height:44px;border:5px solid #d9d9de;border-top-color:#2f6fea;border-radius:50%;margin:70px auto 18px;animation:s 1s linear infinite}
@keyframes s{to{transform:rotate(360deg)}}
body[data-kind=m3u] .creds{display:none}body:not([data-kind=m3u]) .m3u{display:none}
)CSS";

const char* const kForm = R"HTML(
<section id="form">
<h1>Add your provider</h1>
<p class="sub">It goes straight to your TV over your home Wi-Fi.</p>
<div id="err" class="err hide"></div>
<div class="seg"><button type="button" data-k="xtream" class="on">Xtream</button><button type="button" data-k="emby">Emby / Jellyfin</button><button type="button" data-k="m3u">M3U</button></div>
<form id="f" autocomplete="on">
<label for="name">Name</label><input id="name" name="name" placeholder="Optional" autocapitalize="words">
<label for="server" id="serverLabel">Server address</label><input id="server" name="server" inputmode="url" autocapitalize="off" autocorrect="off" spellcheck="false" placeholder="http://example.com:8080">
<div class="creds"><label for="user">User name</label><input id="user" name="user" autocomplete="username" autocapitalize="off" autocorrect="off" spellcheck="false">
<label for="password">Password</label><div class="pw"><input id="password" name="password" type="password" autocomplete="current-password"><button type="button" id="show">Show</button></div></div>
<div class="m3u"><label for="guide">Guide link (optional)</label><input id="guide" name="guide" inputmode="url" autocapitalize="off" autocorrect="off" spellcheck="false" placeholder="http://... (XMLTV)"></div>
<button class="go" id="go" type="submit">Send to TV</button>
</form>
<p class="note">This page comes from your TV, not the internet. It works for 10 minutes, and nothing is kept on your phone.</p>
</section>
<section id="wait" class="hide center"><div class="spin"></div><h1>Signing in&hellip;</h1><p class="sub">Look at your TV.</p></section>
<section id="done" class="hide center"><div class="ok">&#10003;</div><h1>Signed in</h1><p class="sub" id="what"></p>
<p style="margin-top:22px">You can put your phone down. The rest of setup is on your TV.</p>
<p class="note">To add another provider later, use Settings &rsaquo; Providers on the TV.</p></section>
<script>
(function(){
var kind='xtream',$=function(s){return document.querySelector(s)};
function setKind(k){kind=k;document.body.dataset.kind=k;
 document.querySelectorAll('.seg button').forEach(function(b){b.classList.toggle('on',b.dataset.k===k)});
 $('#serverLabel').textContent=k==='m3u'?'Playlist link':'Server address';}
document.querySelectorAll('.seg button').forEach(function(b){b.onclick=function(){setKind(b.dataset.k)}});
$('#show').onclick=function(){var p=$('#password');p.type=p.type==='password'?'text':'password';this.textContent=p.type==='password'?'Show':'Hide'};
function show(id){['#form','#wait','#done'].forEach(function(s){$(s).classList.toggle('hide',s!==id)})}
function problem(msg,fields){show('#form');var e=$('#err');e.textContent=msg;e.classList.remove('hide');
 document.querySelectorAll('input').forEach(function(i){i.classList.toggle('bad',(fields||[]).indexOf(i.name)>=0)});
 $('#go').disabled=false;$('#go').textContent='Send again';window.scrollTo(0,0);}
var gone='Your TV stopped listening. Open the code on the TV again.';
function poll(){setTimeout(function(){fetch('status').then(function(r){return r.json()}).then(function(j){
 if(j.phase==='done'){$('#what').textContent=j.provider+(j.channels?' · '+j.channels.toLocaleString()+' channels':'');show('#done');}
 else if(j.phase==='failed'){problem(j.message,j.fields);} else poll();}).catch(function(){problem(gone,[])})},1000)}
$('#f').onsubmit=function(e){e.preventDefault();var d=new URLSearchParams(new FormData($('#f')));d.set('kind',kind);
 $('#go').disabled=true;$('#go').textContent='Sending…';
 fetch('send',{method:'POST',body:d}).then(function(r){return r.json()}).then(function(j){
  if(!j.ok){problem(j.message,j.fields);return}show('#wait');poll();}).catch(function(){problem(gone,[])});};
})();
</script>
)HTML";

std::string page(const std::string& tvName, const std::string& content) {
  return std::string("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
                     "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                     "<title>Marigold</title><style>") +
         kStyle + "</style></head><body data-kind=\"xtream\"><main><div class=\"tv\">Marigold &middot; " +
         lan::htmlEscape(tvName) + "</div>" + content + "</main></body></html>";
}

std::string jsonReply(bool ok, const std::string& message, const std::vector<std::string>& fields) {
  json::Value v = json::Value::object();
  v.set("ok", ok);
  if (!message.empty()) v.set("message", message);
  json::Value list = json::Value::array();
  for (const std::string& f : fields) list.push(f);
  v.set("fields", list);
  return json::write(v);
}

}  // namespace

Session::Session(std::string token, Random random, std::string tvName)
    : token_(std::move(token)), random_(std::move(random)), tvName_(std::move(tvName)) {
  newCode();
  status_.codeChanges = 0;
}

std::string Session::path() const { return "/" + token_ + "/"; }

void Session::newCode() {
  std::string hex = random_ ? random_(4) : std::string();
  status_.code = codeFromHex(hex);
  ++status_.codeChanges;
  wrongTries_ = 0;
}

lan::Response Session::handle(const lan::Request& r) {
  std::lock_guard<std::mutex> lock(mu_);
  const std::string base = "/" + token_ + "/";
  if (r.path == "/" && r.method == "GET") return codePage("");
  if (r.path == "/code" && r.method == "POST") return checkCode(r);
  if (r.path == "/" + token_ && r.method == "GET") return lan::Response::redirect(base);
  if (r.path == base && r.method == "GET") {
    if (status_.phase == Phase::Waiting) status_.phase = Phase::Connected;
    return formPage();
  }
  if (r.path == base + "send" && r.method == "POST") return send(r);
  if (r.path == base + "status" && r.method == "GET") return statusJson();
  return lan::Response::notFound();
}

lan::Response Session::formPage() const { return lan::Response::html(page(tvName_, kForm)); }

lan::Response Session::codePage(const std::string& message) const {
  std::string content =
      "<h1>Enter the code</h1><p class=\"sub\">The 6 digits on your TV screen.</p>" +
      (message.empty() ? std::string() : "<div class=\"err\">" + lan::htmlEscape(message) + "</div>") +
      "<form method=\"post\" action=\"/code\"><label for=\"code\">Code</label>"
      "<input id=\"code\" name=\"code\" inputmode=\"numeric\" pattern=\"[0-9 ]*\" autocomplete=\"one-time-code\" "
      "maxlength=\"7\" autofocus><button class=\"go\" type=\"submit\">Continue</button></form>"
      "<p class=\"note\">Your phone must be on the same Wi-Fi as the TV.</p>";
  return lan::Response::html(page(tvName_, content));
}

lan::Response Session::checkCode(const lan::Request& r) {
  std::map<std::string, std::string> form = lan::parseForm(r.body);
  std::string typed;
  for (char c : form["code"]) {
    if (c >= '0' && c <= '9') typed += c;
  }
  if (!typed.empty() && typed == status_.code) {
    wrongTries_ = 0;
    return lan::Response::redirect("/" + token_ + "/");
  }
  if (++wrongTries_ >= kCodeTries) {
    newCode();
    return codePage("Too many tries. Your TV shows a new code now.");
  }
  int left = kCodeTries - wrongTries_;
  return codePage("That code didn't match. " + std::to_string(left) + (left == 1 ? " try" : " tries") + " left.");
}

lan::Response Session::send(const lan::Request& r) {
  if (status_.phase == Phase::SigningIn)
    return lan::Response::json(jsonReply(false, "Your TV is still signing in. Wait a moment.", {}), 409);
  if (status_.phase == Phase::SignedIn)
    return lan::Response::json(jsonReply(false, "Your TV is already signed in.", {}), 409);
  std::string problem;
  std::vector<std::string> bad;
  std::optional<ProviderForm> form = formFrom(lan::parseForm(r.body), problem, bad);
  if (!form) return lan::Response::json(jsonReply(false, problem, bad), 400);
  pending_ = std::move(*form);
  status_.phase = Phase::SigningIn;
  status_.message.clear();
  status_.fields.clear();
  return lan::Response::json(jsonReply(true, "", {}));
}

lan::Response Session::statusJson() const {
  json::Value v = json::Value::object();
  v.set("phase", phaseName(status_.phase));
  if (status_.phase == Phase::Failed) {
    v.set("message", status_.message);
    json::Value list = json::Value::array();
    for (const std::string& f : status_.fields) list.push(f);
    v.set("fields", list);
  }
  if (status_.phase == Phase::SignedIn) {
    v.set("provider", status_.provider);
    v.set("channels", status_.channels);
  }
  return lan::Response::json(json::write(v));
}

Status Session::status() const {
  std::lock_guard<std::mutex> lock(mu_);
  return status_;
}

std::optional<ProviderForm> Session::take() {
  std::lock_guard<std::mutex> lock(mu_);
  std::optional<ProviderForm> out = std::move(pending_);
  pending_.reset();
  return out;
}

void Session::signedIn(std::string provider, int channels) {
  std::lock_guard<std::mutex> lock(mu_);
  status_.phase = Phase::SignedIn;
  status_.provider = std::move(provider);
  status_.channels = channels;
}

void Session::failed(std::string message, std::vector<std::string> fields) {
  std::lock_guard<std::mutex> lock(mu_);
  status_.phase = Phase::Failed;
  status_.message = std::move(message);
  status_.fields = std::move(fields);
}

std::optional<ProviderForm> formFrom(const std::map<std::string, std::string>& fields, std::string& problem,
                                     std::vector<std::string>& bad) {
  auto get = [&](const char* key) {
    auto it = fields.find(key);
    return it == fields.end() ? std::string() : it->second;
  };
  ProviderForm f;
  std::string kind = get("kind");
  if (kind == "emby") f.kind = ProviderForm::Kind::Emby;
  else if (kind == "m3u") f.kind = ProviderForm::Kind::M3U;
  else if (kind == "xtream") f.kind = ProviderForm::Kind::Xtream;
  else {
    problem = "Pick Xtream, Emby / Jellyfin or M3U.";
    return std::nullopt;
  }
  f.name = trimmed(get("name"));
  f.server = trimmed(get("server"));
  bool m3u = f.kind == ProviderForm::Kind::M3U;
  if (m3u) {
    f.guide = trimmed(get("guide"));
  } else {
    f.user = trimmed(get("user"));
    f.password = get("password");  // as typed: spaces can be part of it
  }
  if (f.server.empty()) bad.push_back("server");
  if (!m3u && f.user.empty()) bad.push_back("user");
  if (!m3u && f.password.empty()) bad.push_back("password");
  if (!bad.empty()) {
    problem = bad.front() == "server" ? (m3u ? "Enter the playlist link." : "Enter the server address.")
                                      : "Enter the user name and password.";
    return std::nullopt;
  }
  return f;
}

std::vector<std::string> fieldsForProblem(std::string_view problem) {
  std::string p = lowerAscii(problem);
  if (p.find("password") != std::string::npos || p.find("user") != std::string::npos ||
      p.find("refused") != std::string::npos)
    return {"user", "password"};
  if (p.find("reach") != std::string::npos || p.find("server") != std::string::npos ||
      p.find("address") != std::string::npos || p.find("playlist") != std::string::npos)
    return {"server"};
  return {};
}

std::string codeFromHex(std::string_view hex) {
  unsigned long n = 0;
  for (size_t i = 0; i < hex.size() && i < 8; ++i) {
    char c = hex[i];
    int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 0;
    n = n * 16 + static_cast<unsigned long>(v);
  }
  std::string out = std::to_string(n % 1000000);
  return std::string(6 - out.size(), '0') + out;
}

std::string spacedCode(std::string_view code) {
  if (code.size() != 6) return std::string(code);
  return std::string(code.substr(0, 3)) + " " + std::string(code.substr(3));
}

}  // namespace marigold::phone
