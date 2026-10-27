// Phone sign-in: while the TV's "Scan with your phone" screen is open, the
// app serves one page on the home network where a phone fills in the
// provider (Xtream, Emby / Jellyfin or an M3U playlist). No website or
// account is involved: the phone talks to the TV, and the TV signs in as if
// the form had been typed with the remote.
//
// Who can use it: whoever can see the TV. The QR code holds a link with a
// random 128-bit token; a phone that can't scan opens the TV's address and
// types the 6-digit code on the screen, which allows kCodeTries tries
// before a new code replaces it. The page and its two calls ("send" and
// "status") answer only under the token.
//
// Session holds the rules and the state shared by the server thread
// (handle) and the UI thread (status, take, signingIn / signedIn /
// failed); the sockets are platform/lan_server.
#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/net/lan_http.hpp"

namespace marigold::phone {

constexpr int kLifetimeSeconds = 600;
constexpr size_t kTokenBytes = 16;
constexpr int kCodeTries = 5;

struct ProviderForm {
  enum class Kind { Xtream, Emby, M3U } kind = Kind::Xtream;
  std::string name;
  std::string server;  // Xtream / Emby address, or the M3U playlist link
  std::string user;
  std::string password;
  std::string guide;   // M3U: the optional XMLTV link
};

enum class Phase {
  Waiting,    // no phone yet
  Connected,  // the page is open on a phone
  SigningIn,  // a form came in; the TV is signing in
  SignedIn,
  Failed,     // the provider said no (the phone shows why)
};

struct Status {
  Phase phase = Phase::Waiting;
  std::string code;  // the 6 digits on the TV
  int codeChanges = 0;  // a new code after too many wrong tries
  std::string message;  // Failed: why
  std::vector<std::string> fields;  // Failed: "server", "user", "password"
  std::string provider;  // SignedIn: its name
  int channels = 0;
};

// `bytes` random bytes as lower-case hex, or "" when there are none.
using Random = std::function<std::string(size_t bytes)>;

class Session {
 public:
  // `token`: the link's secret, as hex. `tvName` heads the phone's page.
  Session(std::string token, Random random, std::string tvName);

  // "/<token>/": the QR code's link is http://<address>:<port> + this.
  std::string path() const;

  // Server thread: one request.
  lan::Response handle(const lan::Request& r);

  // UI thread.
  Status status() const;
  // A form the phone sent, once.
  std::optional<ProviderForm> take();
  void signedIn(std::string provider, int channels);
  void failed(std::string message, std::vector<std::string> fields);

 private:
  lan::Response formPage() const;
  lan::Response codePage(const std::string& message) const;
  lan::Response checkCode(const lan::Request& r);
  lan::Response send(const lan::Request& r);
  lan::Response statusJson() const;
  void newCode();

  mutable std::mutex mu_;
  const std::string token_;
  Random random_;
  const std::string tvName_;
  Status status_;
  int wrongTries_ = 0;
  std::optional<ProviderForm> pending_;
};

// The form as the phone sent it, checked: nullopt with `problem` and the
// `fields` to fix when something required is missing.
std::optional<ProviderForm> formFrom(const std::map<std::string, std::string>& fields, std::string& problem,
                                     std::vector<std::string>& bad);

// Which fields a sign-in problem points at ("Wrong user name or password"
// -> user and password; "Couldn't reach the server" -> server).
std::vector<std::string> fieldsForProblem(std::string_view problem);

// Six digits from random hex (at least 8 hex characters).
std::string codeFromHex(std::string_view hex);
// "418207" -> "418 207".
std::string spacedCode(std::string_view code);

}  // namespace marigold::phone
