#include "platform/ts_proxy.hpp"

#include "core/util/log.hpp"
#include "livetv_regras.h"
#include "proxyts.h"

namespace marigold::platform {

bool TsProxy::wanted() { return true; }

namespace {

// proxyts.c copies the headers into 512-byte buffers and sends at most 7
// lines; a longer block would be cut, so such feeds play directly.
constexpr size_t kHeaderCap = 512;
constexpr int kHeaderLines = 7;
// The playlist link is kept in a 4096-byte buffer (PxFluxo.lista).
constexpr size_t kSourceCap = 4096;
// "http://127.0.0.1:65535/live.ts?s=4294967295" and the terminator.
constexpr size_t kLocalUrlCap = 64;

// A control character or a space in the link would end up in the 302's
// Location line.
bool safeLink(const std::string& s) {
  for (char c : s) {
    unsigned char u = static_cast<unsigned char>(c);
    if (u <= 0x20 || u == 0x7f) return false;
  }
  return true;
}

// Keeps "Name: value" lines, drops '\r' and lines without a name.
bool normalizeHeaders(const std::string& in, std::string& out) {
  out.clear();
  int lines = 0;
  size_t start = 0;
  while (start <= in.size()) {
    size_t end = in.find('\n', start);
    if (end == std::string::npos) end = in.size();
    std::string line = in.substr(start, end - start);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    size_t colon = line.find(':');
    if (colon != std::string::npos && colon > 0) {
      if (++lines > kHeaderLines) return false;
      if (!out.empty()) out += '\n';
      out += line;
    }
    start = end + 1;
  }
  return out.size() < kHeaderCap;
}

}  // namespace

bool TsProxy::start() {
  if (started_) return true;
  started_ = proxyts_disponivel() != 0 && proxyts_iniciar() != 0;
  MLOG_I("proxy", "local TS proxy %s", started_ ? "listening" : "unavailable");
  return started_;
}

void TsProxy::stop() {
  if (started_) proxyts_parar();
}

bool TsProxy::candidate(const std::string& source) {
  return !source.empty() && proxyts_candidata(source.c_str(), 0) != 0;
}

bool TsProxy::isLocalUrl(const std::string& url) { return proxyts_e_url(url.c_str()) != 0; }

namespace {
void tapBytes(const unsigned char* data, size_t n, unsigned session, void* u) {
  static_cast<TsProxy::Tap*>(u)->bytes(data, n, session);
}
}  // namespace

void TsProxy::setTap(Tap* tap) { proxyts_definir_espiao(tap ? tapBytes : nullptr, tap); }

unsigned TsProxy::session() const { return started_ ? proxyts_sessao() : 0; }

std::string TsProxy::localUrl(const std::string& source, const std::string& headers, bool passThrough) {
  if (!started_ || source.size() >= kSourceCap || !candidate(source) || isLocalUrl(source) ||
      !safeLink(source)) {
    return {};
  }
  std::string lines;
  if (!normalizeHeaders(headers, lines)) {
    MLOG_W("proxy", "headers too long for the proxy; playing direct");
    return {};
  }
  proxyts_definir_cabecalhos(lines.c_str());
  proxyts_definir_passagem(passThrough ? 1 : 0);
  char out[kLocalUrlCap];
  if (!proxyts_url(source.c_str(), out, sizeof out)) {
    MLOG_W("proxy", "no session (the proxy is not listening)");
    return {};
  }
  return out;
}

bool TsProxy::redirected() const { return started_ && proxyts_redirecionou() != 0; }

TsProxy::Source TsProxy::source() const {
  Source s;
  if (started_) s.known = proxyts_fonte(proxyts_sessao(), &s.http, &s.bytes, &s.reopened) != 0;
  return s;
}

void TsProxy::setPreferredHeight(int height) { proxyts_definir_resolucao(nv_res_altura_opcao(height)); }

}  // namespace marigold::platform
