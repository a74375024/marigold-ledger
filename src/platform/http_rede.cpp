// webOS: one transfer over the vendored Legacy rede (the TV's libcurl,
// opened with dlopen; verified TLS, Legacy's redirect rules).
#include <algorithm>
#include <cstring>
#include <vector>

#include "platform/http_transport.hpp"
#include "rede.h"

namespace marigold::http::transport {

static_assert(kBufferedDefault == REDE_CORPO_PADRAO, "buffered body cap");
static_assert(kBufferedMax == REDE_CORPO_MAXIMO, "gzip wire cap");
static_assert(kStreamedMax == REDE_FLUXO_MAXIMO, "streamed body cap");

struct Group {
  RedeGrupo* rede = nullptr;
};
struct Handle {
  RedeJob* rede = nullptr;
};

namespace {

const char* errorName(RedeErro e) {
  switch (e) {
    case REDE_OK: return "";
    case REDE_ENTRADA: return "bad-request";
    case REDE_INDISPONIVEL: return "unavailable";
    case REDE_MEMORIA: return "memory";
    case REDE_TRANSPORTE: return "transport";
    case REDE_PRAZO: return "timeout";
    case REDE_CANCELADO: return "cancelled";
    case REDE_GERACAO: return "stale";
    case REDE_LIMITE_CORPO: return "body-cap";
    case REDE_LIMITE_CABECALHOS: return "header-cap";
    case REDE_REDIRECT: return "redirect";
  }
  return "unknown";
}

struct Calls {
  const std::function<bool(std::string_view)>* receive = nullptr;
  const std::function<bool()>* stop = nullptr;
};

int receiveBody(const void* data, size_t n, void* user) {
  const Calls* c = static_cast<const Calls*>(user);
  return (*c->receive)(std::string_view(static_cast<const char*>(data), n)) ? 1 : 0;
}

int shouldStop(void* user) { return (*static_cast<const Calls*>(user)->stop)() ? 1 : 0; }

}  // namespace

void prepare() { rede_preparar(); }
Group* createGroup() { return new Group{rede_grupo_criar()}; }
void cancelGroup(Group* g) { rede_grupo_cancelar(g->rede); }
void advanceGroup(Group* g) { rede_grupo_avancar(g->rede); }
void releaseGroup(Group* g) {
  rede_grupo_soltar(g->rede);
  delete g;
}
Handle* createHandle(Group* g) { return new Handle{rede_job_criar(g ? g->rede : nullptr)}; }
void cancelHandle(Handle* h) { rede_job_cancelar(h->rede); }
bool live(const Handle* h) { return rede_job_estado(h ? h->rede : nullptr) == REDE_OK; }
void releaseHandle(Handle* h) {
  if (h->rede) rede_job_soltar(h->rede);
  delete h;
}

Result fetch(const Request& q, Handle* h, size_t wireCap, const std::function<bool(std::string_view)>& receive,
             const std::function<bool()>& stop) {
  std::vector<const char*> headers;
  headers.reserve(q.headers.size() + 1);
  for (const std::string& line : q.headers) headers.push_back(line.c_str());
  headers.push_back(nullptr);
  Calls calls{&receive, &stop};

  RedePedido p;
  std::memset(&p, 0, sizeof p);
  p.metodo = q.method.empty() ? nullptr : q.method.c_str();
  p.url = q.url.c_str();
  p.cabecalhos = headers.data();
  p.corpo = q.body.empty() ? nullptr : q.body.data();
  p.n_corpo = q.body.size();
  p.prazo_ms = std::min(q.timeoutMs, 300000u);
  p.max_bytes = wireCap;
  p.seguir = q.follow ? 1 : 0;
  p.tls_pessoal = q.personalServer ? 1 : 0;
  p.job = h ? h->rede : nullptr;
  p.parar = shouldStop;
  p.parar_usuario = &calls;
  if (receive) {
    p.receber_corpo = receiveBody;
    p.corpo_usuario = &calls;
  }

  RedeResposta r;
  std::memset(&r, 0, sizeof r);
  rede_pedir(&p, &r);

  Result out;
  out.status = r.status;
  out.ms = r.ms;
  out.retryAfterS = r.retry_after_s;
  out.mime = r.mime;
  if (r.cabecalhos && r.n_cabecalhos) out.headers.assign(r.cabecalhos, r.n_cabecalhos);
  if (!receive && r.corpo) out.body.assign(r.corpo, r.n_corpo);
  out.error = errorName(r.erro);
  if (r.erro == REDE_TRANSPORTE && r.curl_erro) out.error += " (curl " + std::to_string(r.curl_erro) + ")";
  rede_resposta_limpar(&r);
  return out;
}

}  // namespace marigold::http::transport
