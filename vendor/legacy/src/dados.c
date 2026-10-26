// A medicao inclui torrents esparsos >2 GB nos ARM de 32 bits (#297).
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#include "app_id.h"
#include "dados.h"
#include "androidcache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <ifaddrs.h>
#include <sys/socket.h>
#include <netpacket/packet.h>

static void espacoGravado(const char *nome);


#define NV_FS_TRAVAR()   ((void)0)
#define NV_FS_LIBERAR()  ((void)0)

// Fora do #ifdef: main.c imprime estes numeros em todo alvo, e no webOS e no
// Mac eles ficam em zero porque nao ha descarga nenhuma a fazer.
int    dados_desc_n;
double dados_desc_ms;
void dados_desc_zerar(void) { dados_desc_n = 0; dados_desc_ms = 0.0; }
int    dados_sync_sucessos;
int    dados_sync_falhas;


void dados_fs_travar(void)  { NV_FS_TRAVAR(); }
void dados_fs_liberar(void) { NV_FS_LIBERAR(); }

void dados_marcar_sujo(int leve) {
  (void)leve;
}

void dados_sincronizar_logo(void) {
}

static char dir[512];
static char clienteId[64];

// Tenta criar a pasta e escrever nela. Criar nao basta: em varios pontos do
// sistema de arquivos do aparelho o mkdir passa e o open falha depois, e um
// teste que so olha o mkdir escolheria uma pasta onde nada e gravado.
static int serve(const char *candidato) {
  char teste[600];
  FILE *f;
  if (!candidato || !*candidato) return 0;
  mkdir(candidato, 0755);   // ja existir nao e erro para o que interessa aqui
  snprintf(teste, sizeof teste, "%s/.escrita", candidato);
  f = fopen(teste, "w");
  if (!f) return 0;
  if (fputs("ok\n", f) < 0) { fclose(f); return 0; }
  if (fclose(f) != 0) return 0;
  remove(teste);
  return 1;
}

void dados_iniciar(const char *dirArte) {
  char lar[512], envLar[512], arteLar[512];
  int isolado = strcmp(NV_APP_ID, NV_APP_ID_PRODUCTION) != 0;
  const char *env = getenv("NUVIO_DADOS");
  const char *home = getenv("HOME");
  const char *candidatos[5];
  int n = 0, i;

  if (env && *env) {
    if (!isolado) candidatos[n++] = env;
    else {
      int size = snprintf(envLar, sizeof envLar, "%s/%s", env, NV_APP_ID);
      if (size > 0 && size < (int)sizeof envLar) candidatos[n++] = envLar;
    }
  }
  if (home && *home) {
    int size = snprintf(lar, sizeof lar, isolado ? "%s/.%s" : "%s/.nuvio", home, NV_APP_ID);
    if (size > 0 && size < (int)sizeof lar) candidatos[n++] = lar;
  }
  // The debug package never probes or reads the production developer folder.
  candidatos[n++] = isolado ? "/media/developer/temp/" NV_APP_ID : "/media/developer/temp/nuvio";
  if (dirArte && *dirArte) {
    if (!isolado) candidatos[n++] = dirArte;
    else {
      int size = snprintf(arteLar, sizeof arteLar, "%s/.%s", dirArte, NV_APP_ID);
      if (size > 0 && size < (int)sizeof arteLar) candidatos[n++] = arteLar;
    }
  }

  for (i = 0; i < n; i++) {
    if (serve(candidatos[i])) {
      snprintf(dir, sizeof dir, "%s", candidatos[i]);
      printf("[dados] gravando em %s\n", dir);
      fflush(stdout);
      return;
    }
    printf("[dados] recusou %s\n", candidatos[i]);
  }
  dir[0] = 0;
  printf("[dados] NENHUMA pasta gravavel: sessao e ajustes nao vao sobreviver "
         "ao proximo arranque\n");
  fflush(stdout);
}

const char *dados_dir(void) { return dir; }

int dados_modo_recuperacao(void) {
  return 0;
}

int dados_sync_pendente(void) {
  return 0;
}

int dados_sync_em_recuo(void) {
  return 0;
}

void dados_sincronizar(void) {
}

char *dados_caminho(char *dst, unsigned tam, const char *nome) {
  if (!dir[0] || !nome || !*nome) return NULL;
  const char *base = dir;
  if (!base[0]) return NULL;
  snprintf(dst, tam, "%s/%s", base, nome);
  return dst;
}

// GRAVACAO QUE NAO MERECE UMA DESCARGA SO PARA ELA.
//
// `leve` escolhe entre os dois relogios do topo deste arquivo: 700 ms para dado
// do usuario, 15 s para conteudo re-obtivel. A diferenca aparece na TV Samsung
// e nao na LG, porque no WASM a descarga e FS.syncfs e ela custa 30 a 52 ms
// SINCRONOS — medido no aparelho do relator do #21, uma descarga a cada janela
// de relatorio enquanto ele so andava com o foco.
//
// A posicao da home era o gatilho: ela e gravada quando o foco descansa, e ia
// pelo caminho de 700 ms. Andar pela home virava uma descarga a cada movimento.
// Posicao de cursor e re-obtivel — perde-la ao fechar o app custa uma rolagem,
// contra 30-52 ms de quadro travado a cada passo.
static int gravarInterno(const char *nome, const char *conteudo, int leve) {
  char caminho[600], tmp[600];
  FILE *f;
  size_t n;
  if (!dados_caminho(caminho, sizeof caminho, nome)) return 0;
  snprintf(tmp, sizeof tmp, "%s.tmp", caminho);
  NV_FS_TRAVAR();
  f = fopen(tmp, "w");
  if (!f) { NV_FS_LIBERAR(); return 0; }
  n = conteudo ? strlen(conteudo) : 0;
  if (n && fwrite(conteudo, 1, n, f) != n) { fclose(f); remove(tmp); NV_FS_LIBERAR(); return 0; }
  // Progress must survive power loss, including the directory entry rename.
  int durable = !strcmp(nome, "progresso.txt");
  if (durable && (fflush(f) != 0 || fsync(fileno(f)) != 0)) {
    fclose(f); remove(tmp); NV_FS_LIBERAR(); return 0;
  }
  if (fclose(f) != 0) { remove(tmp); NV_FS_LIBERAR(); return 0; }
  if (rename(tmp, caminho) != 0) { remove(tmp); NV_FS_LIBERAR(); return 0; }
  if (durable) {
    int fd = open(dir, O_RDONLY | O_DIRECTORY);
    int ok = fd >= 0 && fsync(fd) == 0;
    if (fd >= 0) close(fd);
    if (!ok) { NV_FS_LIBERAR(); return 0; }
  }
  NV_FS_LIBERAR();
  (void)leve;
  return 1;
}

int dados_gravar(const char *nome, const char *conteudo) {
  return gravarInterno(nome, conteudo, 0);
}

int dados_gravar_leve(const char *nome, const char *conteudo) {
  int ok = gravarInterno(nome, conteudo, 1);
  if (ok) espacoGravado(nome);
  return ok;
}

char *dados_ler(const char *nome) {
  char caminho[600];
  FILE *f;
  long n;
  char *buf;
  if (!dados_caminho(caminho, sizeof caminho, nome)) return NULL;
  NV_FS_TRAVAR();
  f = fopen(caminho, "rb");
  if (!f) { NV_FS_LIBERAR(); return NULL; }
  fseek(f, 0, SEEK_END);
  n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n < 0) { fclose(f); NV_FS_LIBERAR(); return NULL; }
  buf = (char *)malloc((size_t)n + 1);
  if (!buf) { fclose(f); NV_FS_LIBERAR(); return NULL; }
  n = (long)fread(buf, 1, (size_t)n, f);
  fclose(f);
  NV_FS_LIBERAR();
  buf[n] = 0;
  return buf;
}

int dados_apagar(const char *nome) {
  char caminho[600];
  if (!dados_caminho(caminho, sizeof caminho, nome)) return 0;
  NV_FS_TRAVAR();
  if (remove(caminho) != 0) { NV_FS_LIBERAR(); return 0; }
  NV_FS_LIBERAR();
  return 1;
}

void dados_uuid(char *dst, unsigned tam) {
  static const char *hex = "0123456789abcdef";
  static int semeado;
  int i;
  if (tam < 37) { if (tam) dst[0] = 0; return; }
  if (!semeado) {
    srand((unsigned)time(NULL) ^ (unsigned)getpid() ^ (unsigned)(size_t)dst);
    semeado = 1;
  }
  for (i = 0; i < 36; i++) {
    if (i == 8 || i == 13 || i == 18 || i == 23) { dst[i] = '-'; continue; }
    if (i == 14) { dst[i] = '4'; continue; }              // versao
    if (i == 19) { dst[i] = hex[8 + (rand() & 3)]; continue; }  // variante
    dst[i] = hex[rand() & 15];
  }
  dst[36] = 0;
}

const char *dados_aparelho_id(void) {
  static char id[64];
  if (id[0]) return id;
  const char *raw = getenv("NUVIO_DEVICE_ID");
  char hardware[128] = "";
  if (!raw || !*raw) {
    // Native TV network interfaces survive uninstall. Prefer wired over Wi-Fi;
    // enumerate interfaces, not currently connected routes.
    struct ifaddrs *list = NULL;
    char best[64] = "";
    if (!getifaddrs(&list)) {
      for (struct ifaddrs *p = list; p; p = p->ifa_next) {
        if (!p->ifa_addr || !p->ifa_name || !strncmp(p->ifa_name, "lo", 2)) continue;
        const unsigned char *mac = NULL;
        if (p->ifa_addr->sa_family == AF_PACKET) {
          struct sockaddr_ll *a = (struct sockaddr_ll *)p->ifa_addr;
          if (a->sll_halen == 6) mac = a->sll_addr;
        }
        if (!mac || !(mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5])) continue;
        if (!best[0] || strcmp(p->ifa_name, best) < 0) {
          snprintf(best, sizeof best, "%s", p->ifa_name);
          snprintf(hardware, sizeof hardware, "%02x%02x%02x%02x%02x%02x",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
      }
      freeifaddrs(list);
    }
    raw = hardware;
  }
  if (!raw || !*raw) return "";
  // Opaque identifier, never upload/log the raw DUID, Android ID or MAC.
  unsigned long long h = 1469598103934665603ULL;
  for (; *raw; raw++) { h ^= (unsigned char)*raw; h *= 1099511628211ULL; }
  snprintf(id, sizeof id, "device-%016llx", h);
  return id;
}

const char *dados_cliente_id(void) {
  char *lido;
  if (clienteId[0]) return clienteId;

  lido = dados_ler("cliente.txt");
  if (lido) {
    char *fim = lido + strlen(lido);
    while (fim > lido && (fim[-1] == '\n' || fim[-1] == '\r' || fim[-1] == ' ')) *--fim = 0;
    if (lido[0]) snprintf(clienteId, sizeof clienteId, "%s", lido);
    free(lido);
    if (clienteId[0]) return clienteId;
  }

  // Formato de UUID v4 porque e o que o servidor recebe do Android e do web; a
  // aleatoriedade nao precisa ser criptografica — este numero identifica um
  // aparelho para nao ecoar a propria escrita, nao protege nada.
  dados_uuid(clienteId, sizeof clienteId);

  { char linha[64];
    snprintf(linha, sizeof linha, "%s\n", clienteId);
    dados_gravar("cliente.txt", linha); }
  return clienteId;
}

int dados_despedida_ler(void) {
  return 0;
}

void dados_despedida_fim(void) {
}

void dados_descarregar_e_sair(void) {
}

int dados_persistente(void) {
  return dir[0] != 0;
}

#include "espaco.inc"
