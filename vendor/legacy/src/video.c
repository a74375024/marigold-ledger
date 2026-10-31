// marigold: trimmed to a live-only player. Dolby Vision, DTS conversion, MKV
// probing, subtitles, seeking, playback speed, cache boost and the VOD
// reconnect machine are gone; what remains is upstream code. See UPSTREAM.md.
#include "app_id.h"
#include "video.h"
#include "mediaurl.h"
#include "webosver.h"
#include "esmaecer.h"
#include "video_escala.h"
#include <SDL2/SDL.h>
#include "marco.h"
#include "js.h"
#include "lsregistro.h"
#include "rede.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <ctype.h>
#include <stdint.h>
#include <stdatomic.h>
#include <unistd.h>
#include <math.h>
#include <limits.h>

// This source already played (webOS): a later reload of the SAME source
// is not a source that failed to open.
static int fonteTocou;
int video_fonte_tocou(void) { return fonteTocou; }

static char  urlAtual[NV_MEDIA_URL_MAX];
// Recuperacao de pipeline destruido: pedida pelo fio de resposta do luna e
// executada no fio principal (video_bombear), porque recarregar de dentro do
// tratador de evento reentra no mesmo caminho que acabou de falhar.
static int    recuperando;
// FAIXA de audio a restaurar depois de uma queda de pipeline. `-1` = nao ha o
// que restaurar.
static int   audioAoCarregar = -1;
void video_escolher_audio(int i);
static _Atomic int pausaPedida;   // 1 enquanto a pausa foi pedida por nos
static int    pausaConfirmada;
// Identidade monotonica do pipeline. Callbacks do LS2 podem sobreviver ao
// unload; sem uma geracao, a resposta antiga pode ocupar o estado da proxima
// abertura e fazer o load correto ser ignorado.
static unsigned  sessao;

#include <dlfcn.h>

typedef struct LSHandle LSHandle;
typedef struct LSMessage LSMessage;
typedef int (*Filtro)(LSHandle *, LSMessage *, void *);

// LSError, na forma do luna-service2 (include/public/luna-service2/
// lunaservice.h). Nao ha header C no SDK, entao a struct e repetida aqui — e a
// folga de 256 bytes continua, para a lib nunca escrever alem do que alocamos.
//
// O CAMPO `message` E UM PONTEIRO.
typedef struct {
  int         code;
  char       *message;
  const char *file;
  int         line;
  const char *func;
  void       *padding;
  unsigned long magic;
} NvLsErro;
static union { NvLsErro e; char folga[256]; } ERRO_U;
#define ERRO ((void *)&ERRO_U)

static int  (*lsErroIniciar)(void *);   // LSErrorInit: poe o `magic`
static void (*lsErroLiberar)(void *);   // LSErrorFree: solta a `message`

// Antes de CADA chamada que recebe o LSError: solta a mensagem da anterior e
// reinicia a struct como a lib espera (LSErrorInit), em vez do memset puro.
static void erroLimpar(void) {
  if (ERRO_U.e.message && lsErroLiberar) lsErroLiberar(ERRO);
  memset(&ERRO_U, 0, sizeof ERRO_U);
  if (lsErroIniciar) lsErroIniciar(ERRO);
}

static void logErroLs(const char *onde) {
  const char *nome = lsreg_nome_codigo(ERRO_U.e.code);
  printf("[video] %s: lsError code=%d (%s) msg=%.200s\n", onde, ERRO_U.e.code,
         nome ? nome : "?", ERRO_U.e.message ? ERRO_U.e.message : "(vazio)");
  fflush(stdout);
}

// Contexto do processo, UMA vez, na primeira recusa. O papel LS2 do app casa
// pelo exeName (/var/palm/ls2-dev/roles/*/<app id>.json).
static void logContextoLs(void) {
  char exe[256]; ssize_t n;
  const char *home = getenv("HOME"), *app = getenv("APPID");
  n = readlink("/proc/self/exe", exe, sizeof exe - 1);
  exe[n > 0 ? n : 0] = 0;
  printf("[video] contexto do registro: uid=%d exe=%s HOME=%s APPID=%s papel=%s\n",
         (int)getuid(), exe[0] ? exe : "?", home ? home : "-", app ? app : "-",
         access("/var/palm/ls2-dev/roles/pub/" NV_APP_ID ".json", F_OK) == 0
           ? "visivel" : "nao visivel daqui");
  fflush(stdout);
}

static LsRegEstado regEstado;
static int regNegado;          // PERMISSION nesta sessao
static int regAvisouDesistir;

static int         (*lsRegister)(const char *, LSHandle **, void *);
static int         (*lsUnregister)(LSHandle *, void *);
static int         (*lsAttach)(LSHandle *, void *, void *);
static int         (*lsCall)(LSHandle *, const char *, const char *, Filtro, void *, unsigned long *, void *);
static int (*lsCallUma)(LSHandle *, const char *, const char *, Filtro, void *, unsigned long *, void *);
static const char *(*lsPayload)(LSMessage *);
static void *(*loopNovo)(void *, int);
static void  (*loopRodar)(void *);
static void  (*loopParar)(void *);
static unsigned (*loopAgendar)(int, int (*)(void *), void *, void (*)(void *));

// libAcbAPI e SEMPRE por dlopen. Linkar cria um DT_NEEDED e, se a lib faltar
// (ela sumiu no webOS 5), o processo morre antes do main e antes do log —
// nao sobra nem uma linha para diagnosticar.
static long (*acbCriar)(void);
static int  (*acbIniciar)(long, int, const char *, void *);
static int  (*acbSink)(long, int);
static int  (*acbMidia)(long, const char *);
static int  (*acbEstado)(long, int, int, long *);
static int  (*acbJanela)(long, long, long, long, long, int, long *);
// Janela CUSTOMIZADA: recorte de fonte + retangulo de destino. E o caminho com
// permissao: chamar o com.webos.service.tv.display direto e RECUSADO pelo hub,
// e a libAcbAPI alcanca o servico por dentro, como o navegador da TV faz.
static int  (*acbJanelaCustom)(long, long, long, long, long,
                               long, long, long, long, int, long *);
static void (*acbDestruir)(long);
static int  (*acbFinalizar)(long);
// O navegador da TV chama isto e nos nao chamavamos: sem o connect o plano de
// video existe, decodifica e toca o audio, mas nao e ligado a saida — tela
// preta com som, exatamente o sintoma observado.
static int  (*acbConectar)(long, int, long *);
// Recebe JSON como string (confirmado: a lib chama strlen no argumento antes de
// montar um std::string). Sem esta chamada o servico do ACB nunca repassa nada
// para com.webos.service.tv.display e o plano de video nao liga — o sintoma e
// audio normal com tela preta.
static int  (*acbVideoData)(long, const char *, long *);
static int  (*acbAudioData)(long, const char *, long *);

// ---------------------------------------------------------------- webOS 5+
//
// A LG apagou a libAcbAPI na webOS 5.0. O caminho de la e outro: a SDL da
// propria TV exporta uma janela para o plano de video e o uMS recebe o ID dela
// em `windowId`, no lugar do "window_id_dummy" que o caminho do ACB usa. E o
// mesmo que o Kodi e o moonlight fazem na 5+.
//
// Assinaturas TIRADAS DO HEADER DO SDK (SDL2/SDL_webOS.h do buildroot da
// openlgtv), nao de memoria:
//   const char *SDL_webOSCreateExportedWindow(int type);
//   SDL_bool    SDL_webOSSetExportedWindow(const char *id, SDL_Rect *src, SDL_Rect *dst);
//   SDL_bool    SDL_webOSExportedSetCropRegion(const char *id, SDL_Rect *org,
//                                              SDL_Rect *src, SDL_Rect *dst);
//   void        SDL_webOSDestroyExportedWindow(const char *id);
// type 0 = SDL_WEBOS_EXPORED_WINDOW_TYPE_VIDEO (a grafia truncada e do header).
//
// Por dlsym e nao por chamada direta: o binario e o MESMO nos dois mundos, e na
// webOS 4 esses simbolos nao existem.
static const char *(*sdlExpCriar)(int);
static int         (*sdlExpJanela)(const char *, SDL_Rect *, SDL_Rect *);
static int         (*sdlExpRecorte)(const char *, SDL_Rect *, SDL_Rect *, SDL_Rect *);
static void        (*sdlExpDestruir)(const char *);
// ID devolvido pela SDL. Vazio = estamos no caminho do ACB (webOS 4).
static char        expWin[64];

static LSHandle *bus;
static void     *laco;
static pthread_t fio;
// 1 a partir do video_encerrar (#205): nenhum callback do laco age depois
// disto, e o fio e parado/ESPERADO antes do LSUnregister.
static int      encerrando;   // __atomic sempre
static long      acb;
// Retangulo pedido pela UI. Guardado porque o ACB so aceita a janela depois do
// loadCompleted, que chega muito depois de quem pediu.
static int       janX, janY, janW = 1920, janH = 1080;
// Tamanho da superficie (drawable) em que o retangulo de DESTINO do plano de
// video e entendido; 1920x1080 ate o main dizer outra coisa. Ver video_escala.h.
static int       escW = 1920, escH = 1080;
// Destino em unidades de layout -> pixels da superficie. So o DESTINO escala: a
// fonte (recorte) e o quadro `org` sao coordenadas do quadro decodificado.
static SDL_Rect escDst(int x, int y, int w, int h) {
  NvRetInt r = { x, y, w, h };
  SDL_Rect o;
  r = nv_video_escalar(r, 1920, 1080, escW, escH);
  o.x = r.x; o.y = r.y; o.w = r.w; o.h = r.h;
  return o;
}
// Ultimo par fonte/destino aplicado, para nao repetir a mesma chamada a cada
// quadro. fonX = -1 quer dizer "nada aplicado".
static int       fonX = -1, fonY, fonW, fonH, dstX = -1, dstY, dstW, dstH;
// Caracteristicas do fluxo, tiradas do evento videoInfo da assinatura do uMS.
// O ACB precisa delas para descrever o video ao pipeline de exibicao.
static int       vidW = 1920, vidH = 1080, vidTaxa = 30;
// Tamanho do quadro usado na ultima SDL_webOSSetExportedWindow (#158). Quando o
// videoInfo chega depois com outro tamanho, a janela e reaplicada com ele.
static int       expSrcW, expSrcH;
static void      expJanelaAplicar(void);
static long      vidBits;
static char      vidVarredura[24] = "progressive";
// hdrType real informado pelo uMS para a camada que chegou ao decoder.
static char      vidHdr[24] = "none";
static long      seiX0, seiX1, seiX2, seiY0, seiY1, seiY2;
static long      seiBrancoX, seiBrancoY, seiMinLum, seiMaxLum;
static long      seiMaxCLL, seiMaxFALL;
static int       vuiPrim = 2, vuiTrans = 2, vuiMatriz = 2;
static int       viuVideo;

// Faixas de audio lidas do sourceInfo.
static VideoFaixa faixaAudio[NV_FAIXA_MAX];
static int nAudio, audioAtual;
// marigold: faixas de video do sourceInfo (-1 = ainda nao veio) e o codec
// da primeira.
static int nVideo = -1;
static char codecVideo[24];

static char      midia[64];
static double    posSeg, durSeg;
// marigold: when the last currentTime came (SDL ticks; 0 = none since the
// load), so the caption clock can carry it on between events.
static Uint32    posEm;
static _Atomic int tocando, falhou, terminou;
static int       pronto, ligado;
static _Atomic int ackMidia; // presenca de midia, sem ler a string entre fios
static void ack_publicar(void);
// Ver video_erro_texto. Escrito no fio do LS2, lido pelo de desenho: e so
// texto curto e o pior caso de corrida e ler meia mensagem num quadro.
static char      erroTexto[96];
// errorCode 200 "Audio Codec Not Supported": o VIDEO segue tocando e so o
// audio morre. Ver o tratamento em lerEvento.
static int       audioNaoSup;
static char cabsHttp[512];

// Os enums do ACB nao tem header publico e chutar sai caro: com playerType 10 e
// sink 1 o aparelho registrou "playerType":"mse","vsmSinkType":"sub" — ou seja,
// o video foi para o plano SECUNDARIO (PIP) e a tela ficou preta. Os numeros
// ficam ajustaveis por /tmp/marigold-acb para conferir contra o que o
// ls-monitor mostra que o ACB resolveu, em vez de adivinhar de novo.
static int tipoJogador = 0, tipoSink = 0, estCarregado = 1, estTocando = 2;
static int tipoJogadorManual, acbTipoAtual = -1, acbTipoFalhou = -1;

static void lerAjustesAcb(void) {
  FILE *f = fopen("/tmp/marigold-acb", "r");   // marigold: own name
  if (!f) return;
  if (fscanf(f, "%d %d %d %d", &tipoJogador, &tipoSink, &estCarregado, &estTocando) > 0) {
    tipoJogadorManual = 1;
    printf("[video] acb ajustes: jogador=%d sink=%d carregado=%d tocando=%d\n",
           tipoJogador, tipoSink, estCarregado, estTocando);
  }
  fclose(f);
}

#define NV_ACB_FOREGROUND 1

// Procura a chave e exige que o que vem depois seja NUMERO.
//
// O evento e {"currentTime":{"currentTime":8580,...}}: a primeira ocorrencia da
// chave e o objeto externo, e atof("{...") devolve 0.
static double numeroDe(const char *p, const char *chave) {
  const char *q = p;
  size_t n = strlen(chave);
  while ((q = strstr(q, chave)) != NULL) {
    const char *v = q + n;
    while (*v == ' ') v++;
    if ((*v >= '0' && *v <= '9') || *v == '-' || *v == '.') return atof(v);
    q += n;
  }
  return -1.0;
}

static pthread_t fioBind;
static volatile int bindVivo = 0;   // existe um bind em andamento?
static int bindJoinable;
typedef struct { unsigned sessao; long acb; int tipo; char midia[64]; } AcbBind;
static int bindAtivo(void) { return __atomic_load_n(&bindVivo, __ATOMIC_ACQUIRE); }
static int acbConfigurarTipo(void);
static void acbBindRecolher(void) {
  if (bindJoinable && !bindAtivo()) { pthread_join(fioBind, NULL); bindJoinable = 0; }
}
// Se o load novo termina durante o bind lento da sessao anterior, guarda o
// trabalho. video_bombear inicia o bind assim que o fio anterior liberar.
static volatile int bindPendente;

// Latencia do pipeline: pedido de load -> loadCompleted -> primeiro quadro.
static struct timespec t0Pedido;
static int cronPediu, cronLoad, cronQuadro;
static long msDesdePedido(void) {
  struct timespec a;
  clock_gettime(CLOCK_MONOTONIC, &a);
  return (a.tv_sec - t0Pedido.tv_sec) * 1000L + (a.tv_nsec - t0Pedido.tv_nsec) / 1000000L;
}
// Ate onde o buffer do pipeline ja cobre (segundos), do evento bufferRange.
static double bufferSeg;
// Instante do bufferingStart que AINDA nao teve bufferingEnd; 0 = nao esta
// bufferizando.
static Uint32 bufferandoDesde;

static void esperar(int ms) { struct timespec t; t.tv_sec = ms / 1000;
  t.tv_nsec = (long)(ms % 1000) * 1000000L; nanosleep(&t, NULL); }

// Versao maior do webOS desta TV. Importa porque o pipeline RENOMEOU campos na
// 5.0 e um campo com nome errado e ignorado em silencio. Conferido no Kodi
// (MediaPipelineWebOS.cpp, SetHDR):
//   hdrData[m_webOSVersion < 5 ? "mediaSei" : "sei"] = sei;
//   hdrData[m_webOSVersion < 5 ? "mediaVui" : "vui"] = vui;
// Quem decide a versao e o webosver.c (webos_release do nyx, depois o
// starfish-release). Desconhecida continua valendo o chute de sempre (5 com a
// libAcbAPI ausente, senao 4).
static int webosMaior(void) {
  static int v, lido;
  if (v) return v;
  v = nv_webos_major();
  if (!lido) {
    lido = 1;
    char sf[128];
    nv_webos_starfish_linha(sf, sizeof sf);
    printf("[video] starfish-release: %s\n", sf[0] ? sf : "(sem arquivo)");
    printf("[video] webos major=%d fonte=%s\n", v, nv_webos_fonte());
    fflush(stdout);
  }
  if (!v) v = expWin[0] ? 5 : 4;
  return v;
}

// O JSON de video do ACB. VUI segue H.273/HEVC: 9=BT.2020, 16=PQ (SMPTE 2084)
// — e o par que HDR10 pede; SDR fica em 2 (unspecified), que e o que sempre foi
// mandado e toca.
static void montarVideoData(char *vd, size_t n, const char *ctx,
                            const char *htipo, int prim, int trans, int matriz) {
  const char *varr = strstr(vidVarredura, "inter") ? "VIDEO_INTERLACED"
                   : "VIDEO_PROGRESSIVE";
  const char *kSei = webosMaior() < 5 ? "mediaSei" : "sei";
  const char *kVui = webosMaior() < 5 ? "mediaVui" : "vui";
  char cor[768] = "";
  if (!strcmp(htipo, "HDR10")) {
    snprintf(cor, sizeof cor,
      "\"%s\":{\"displayPrimariesX0\":%ld,\"displayPrimariesX1\":%ld,"
      "\"displayPrimariesX2\":%ld,\"displayPrimariesY0\":%ld,"
      "\"displayPrimariesY1\":%ld,\"displayPrimariesY2\":%ld,"
      "\"maxContentLightLevel\":%ld,\"maxDisplayMasteringLuminance\":%ld,"
      "\"maxPicAverageLightLevel\":%ld,\"minDisplayMasteringLuminance\":%ld,"
      "\"whitePointX\":%ld,\"whitePointY\":%ld},"
      "\"%s\":{\"colorPrimaries\":%d,\"matrixCoeffs\":%d,"
      "\"transferCharacteristics\":%d,\"videoFullRangeFlag\":false},",
      kSei, seiX0, seiX1, seiX2, seiY0, seiY1, seiY2, seiMaxCLL, seiMaxLum,
      seiMaxFALL, seiMinLum, seiBrancoX, seiBrancoY,
      kVui, prim, matriz, trans);
  } else if (!strcmp(htipo, "none")) {
    snprintf(cor, sizeof cor,
      "\"%s\":{\"displayPrimariesX0\":0,\"displayPrimariesX1\":0,"
      "\"displayPrimariesX2\":0,\"displayPrimariesY0\":0,"
      "\"displayPrimariesY1\":0,\"displayPrimariesY2\":0,"
      "\"maxContentLightLevel\":0,\"maxDisplayMasteringLuminance\":0,"
      "\"maxPicAverageLightLevel\":0,\"minDisplayMasteringLuminance\":0,"
      "\"whitePointX\":0,\"whitePointY\":0},"
      "\"%s\":{\"colorPrimaries\":2,\"matrixCoeffs\":2,"
      "\"transferCharacteristics\":2,\"videoFullRangeFlag\":false},",
      kSei, kVui);
  }
  snprintf(vd, n,
    //  - "context" com o mediaId TEM de vir no proprio JSON: o servico do
    //    ACB repassa o payload como veio, nao insere o campo. Sem ele o
    //    tv.display responde ERROR_06 "Invalid argument".
    "{\"content\":\"movie\",\"context\":\"%s\",\"video\":{"
    "\"adaptive\":false,\"bitRate\":%ld,"
    "\"data3D\":{\"currentPattern\":\"2d\",\"originalPattern\":\"2d\","
            "\"typeLR\":\"LR\"},"
    "\"frameRate\":%d.0,\"hdrType\":\"%s\","
    "\"height\":%d,\"width\":%d,"
    "%s"
    "\"hfr\":false,"
    "\"path\":\"network\","
    "\"pixelAspectRatio\":{\"height\":1,\"width\":1},"
    "\"rotation\":\"0\",\"scanType\":\"%s\",\"specificRendering\":\"none\""
    "}}", ctx, vidBits, vidTaxa, htipo, vidH, vidW, cor, varr);
}

static int acbBindValido(const AcbBind *bind) {
  return bind->sessao == __atomic_load_n(&sessao, __ATOMIC_ACQUIRE) &&
    bind->acb == acb && !strcmp(bind->midia, midia);
}

static void *prenderPlano(void *u) {
  AcbBind *bind = u;
  const char *minha = bind->midia;
  const long meuAcb = bind->acb;
  // O bind e POR SESSAO: cada loadCompleted tem de religar o plano. Foi o bug
  // da "segunda reproducao preta com som" — o ACB continuava apontando para o
  // mediaId da sessao anterior, que o unload matou. A guarda de midia cobre a
  // troca de canal no MEIO do bind.
  printf("[video] bind inicio sessao=%u tipo=%d acb=%ld mediaId=%.63s\n",
         bind->sessao, bind->tipo, bind->acb, bind->midia);
  fflush(stdout);
  if (!acbBindValido(bind)) goto fora;
  long tarefa = 0;
  printf("[video] bind mediaId=%d\n", acbMidia(meuAcb, minha)); esperar(200);
  if (!acbBindValido(bind)) goto fora;
  printf("[video] bind loaded=%d\n", acbEstado(meuAcb, NV_ACB_FOREGROUND, estCarregado, &tarefa)); esperar(200);
  if (!acbBindValido(bind)) goto fora;
  printf("[video] connect=%d\n", acbConectar(meuAcb, tipoSink, &tarefa)); esperar(200);
  if (!acbBindValido(bind)) goto fora;
  {
    // marigold: live channels are SDR or HDR10; the Dolby Vision branches of
    // upstream are gone.
    char htipo[24];
    int prim = 2, trans = 2, matriz = 2;
    if (!strcasecmp(vidHdr, "HDR10")) {
                                  snprintf(htipo, sizeof htipo, "HDR10");
                                  prim = vuiPrim; trans = vuiTrans; matriz = vuiMatriz; }
    else                           snprintf(htipo, sizeof htipo, "none");
    char vd[2048];
    montarVideoData(vd, sizeof vd, minha, htipo, prim, trans, matriz);
    { char ad[160];
      snprintf(ad, sizeof ad,
               "{\"context\":\"%s\",\"audio\":{\"immersive\":\"none\"}}", minha);
      if (!acbBindValido(bind)) goto fora;
      int rvd = acbVideoData(meuAcb, vd, &tarefa);
      printf("[video] videoData=%d (hdrType=%s)\n", rvd, htipo);
      if (rvd != 1 && strcmp(htipo, "none")) {
        montarVideoData(vd, sizeof vd, minha, "none", 2, 2, 2);
        printf("[video] videoData recusou hdrType=%s, repetindo sem HDR: %d\n",
               htipo, acbVideoData(meuAcb, vd, &tarefa));
      }
      if (acbAudioData) {
        if (!acbBindValido(bind)) goto fora;
        printf("[video] audioData=%d\n", acbAudioData(meuAcb, ad, &tarefa));
      }
      fflush(stdout);
    }
    esperar(300);
    if (!acbBindValido(bind)) goto fora;
  }
  // COM RECORTE DE FONTE JA PEDIDO, prende o plano com o recorte.
  if (!acbBindValido(bind)) goto fora;
  { SDL_Rect d = escDst(fonX >= 0 ? dstX : janX, fonX >= 0 ? dstY : janY,
                        fonX >= 0 ? dstW : janW, fonX >= 0 ? dstH : janH);
    if (fonX >= 0 && acbJanelaCustom)
      printf("[video] bind customWindow=%d\n", acbJanelaCustom(meuAcb, fonX, fonY, fonW, fonH, d.x, d.y, d.w, d.h,
                      (dstX == 0 && dstY == 0 && dstW == 1920 && dstH == 1080), &tarefa));
    else {
      d = escDst(janX, janY, janW, janH);
      printf("[video] bind window=%d\n", acbJanela(meuAcb, d.x, d.y, d.w, d.h,
                (janX == 0 && janY == 0 && janW == 1920 && janH == 1080), &tarefa));
    } }
  if (!acbBindValido(bind)) goto fora;
  printf("[video] bind playing=%d\n", acbEstado(meuAcb, NV_ACB_FOREGROUND, pausaPedida ? 3 : estTocando, &tarefa));
  printf("[video] plano preso em %d,%d %dx%d%s\n", janX, janY, janW, janH,
         fonX >= 0 ? " (com recorte)" : "");
  fflush(stdout);
fora:
  if (!acbBindValido(bind)) printf("[video] bind abortado: sessao ou midia mudou\n");
  fflush(stdout);
  free(bind);
  __atomic_store_n(&bindVivo, 0, __ATOMIC_RELEASE);
  return NULL;
}

// Ver o comentario em lerEvento (#111). So faz algo quando JA ha um recorte
// aplicado nesta midia; o caminho e o mesmo que o video_janela_fonte usaria.
static void recorteNoPrimeiroQuadro(void) {
  if (fonX < 0 || !ligado || !midia[0]) return;
  if (expWin[0] && sdlExpRecorte) {
    SDL_Rect org, src, dst;
    int ok;
    org.x = 0; org.y = 0; org.w = vidW > 0 ? vidW : 1920; org.h = vidH > 0 ? vidH : 1080;
    src.x = fonX; src.y = fonY; src.w = fonW; src.h = fonH;
    dst = escDst(dstX, dstY, dstW, dstH);
    ok = sdlExpRecorte(expWin, &org, &src, &dst);
    printf("[video] recorte reaplicado no primeiro quadro (janela exportada) -> %d\n", ok);
    fflush(stdout);
  } else if (acb && acbJanelaCustom) {
    printf("[video] recorte reaplicado no primeiro quadro (acb)\n");
    video_recorte_reaplicar();
  }
}

static int eventoPayload(const char *p, unsigned minhaSessao) {
  if (minhaSessao != sessao) return 1;
  if (!p) return 1;
  // OS EVENTOS DE RELOGIO SAO AMOSTRADOS NO LOG. currentTime chega a cada
  // ~200 ms e bufferRange/streamingInfo quase tanto. Um de cada tipo a cada
  // 30 s basta para saber que o video andava; estado, erro e o resto
  // continuam inteiros.
  { static Uint32 ultimoRel[4];
    static const char *const RELOGIO[4] = {
      "\"currentTime\"", "\"bufferRange\"", "\"streamingInfo\"", "subtitlePosition"
    };
    size_t n = strlen(p);
    int k, logar = 1;
    for (k = 0; k < 4; k++)
      if (strstr(p, RELOGIO[k])) {
        Uint32 agora = SDL_GetTicks();
        if (ultimoRel[k] && agora - ultimoRel[k] < 30000) logar = 0;
        else ultimoRel[k] = agora;
        break;
      }
    if (strstr(p, "\"error") || strstr(p, "rror\"")) logar = 1;   // erro sempre sai
    while (n && (p[n - 1] == '\n' || p[n - 1] == '\r' || p[n - 1] == ' ')) n--;
    if (logar) { printf("[video] ev %.*s\n", (int)n, p); fflush(stdout); } }
  if (strstr(p, "sourceInfo")) {
    const char *q;
    nAudio = 0;
    // Empty strings/arrays mean no tracks. Stay inside each track array so
    // the following videoTrackInfo cannot be mistaken for audio.
    q = js_array(p, NULL, "audioTrackInfo");
    if (q) {
      const char *o = q;
      while (o && *o == '{' && nAudio < NV_FAIXA_MAX) {
        const char *fo = js_fim(o);
        if (!fo) break;
        VideoFaixa *f = &faixaAudio[nAudio];
        char cod[16] = "";
        memset(f, 0, sizeof *f);
        f->numero = nAudio;
        { const char *l = strstr(o, "\"language\":\"");
          if (l && (!fo || l < fo)) {
            size_t k = 0; l += 12;
            while (*l && *l != '"' && k + 1 < sizeof f->idioma) f->idioma[k++] = *l++;
            f->idioma[k] = 0;
            if (!strcmp(f->idioma, "(null)")) f->idioma[0] = 0;
          } }
        { const char *c2 = strstr(o, "\"codec\":\"");
          if (c2 && (!fo || c2 < fo)) {
            size_t k = 0; c2 += 9;
            while (*c2 && *c2 != '"' && k + 1 < sizeof cod) cod[k++] = *c2++;
            cod[k] = 0;
          } }
        snprintf(f->codec, sizeof f->codec, "%s", cod);
        f->canais = (int)js_num(o, fo, "channels", 0);
        nAudio++;
        o = js_prox(fo);
      }
    }
    // marigold: as de video tambem (a verificacao da imagem: um fluxo so
    // com som). Sem a chave, nao se sabe; com o array vazio, nenhuma.
    if (strstr(p, "\"videoTrackInfo\"")) {
      const char *o = js_array(p, NULL, "videoTrackInfo");
      nVideo = 0;
      codecVideo[0] = 0;
      while (o && *o == '{' && nVideo < NV_FAIXA_MAX) {
        const char *fo = js_fim(o);
        if (!fo) break;
        if (nVideo == 0) {
          const char *c2 = strstr(o, "\"codec\":\"");
          if (c2 && c2 < fo) {
            size_t k = 0; c2 += 9;
            while (*c2 && *c2 != '"' && k + 1 < sizeof codecVideo) codecVideo[k++] = *c2++;
            codecVideo[k] = 0;
          }
        }
        nVideo++;
        o = js_prox(fo);
      }
    }
    printf("[video] faixas: audio=%d video=%d\n", nAudio, nVideo);
    fflush(stdout);
  }

  if (strstr(p, "videoInfo")) {
    viuVideo = 1;
    double v;
    v = numeroDe(p, "\"width\":");      if (v > 0) vidW = (int)v;
    v = numeroDe(p, "\"height\":");     if (v > 0) vidH = (int)v;
    // O quadro real chegou diferente do que a janela exportada recebeu como
    // origem (#158): reaplica, sem recorte de fonte (esse tem caminho proprio).
    if (expWin[0] && fonX < 0 && expSrcW > 0 && (vidW != expSrcW || vidH != expSrcH))
      expJanelaAplicar();
    v = numeroDe(p, "\"frameRate\":");  if (v > 0) vidTaxa = (int)v;
    v = numeroDe(p, "\"bitRate\":");    if (v > 0) vidBits = (long)v;
    { const char *q = strstr(p, "\"scanType\":\"");
      if (q) { const char *f; q += 12; f = strchr(q, '"');
        if (f && f - q < (int)sizeof vidVarredura) {
          memcpy(vidVarredura, q, f - q); vidVarredura[f - q] = 0; } } }
    { const char *q = strstr(p, "\"hdrType\":\"");
      if (q) { const char *f; q += 11; f = strchr(q, '"');
        if (f && f - q < (int)sizeof vidHdr) {
          memcpy(vidHdr, q, f - q); vidHdr[f - q] = 0;
          printf("[video] HDR do pipeline: %s\n", vidHdr); } } }
    { double x;
#define LER_SEI(nome, dst) do { x = numeroDe(p, "\"" nome "\":"); if (x >= 0) dst = (long)x; } while (0)
      LER_SEI("displayPrimariesX0", seiX0); LER_SEI("displayPrimariesX1", seiX1);
      LER_SEI("displayPrimariesX2", seiX2); LER_SEI("displayPrimariesY0", seiY0);
      LER_SEI("displayPrimariesY1", seiY1); LER_SEI("displayPrimariesY2", seiY2);
      LER_SEI("whitePointX", seiBrancoX); LER_SEI("whitePointY", seiBrancoY);
      LER_SEI("minDisplayMasteringLuminance", seiMinLum);
      LER_SEI("maxDisplayMasteringLuminance", seiMaxLum);
      LER_SEI("maxContentLightLevel", seiMaxCLL);
      LER_SEI("maxPicAverageLightLevel", seiMaxFALL);
#undef LER_SEI
      x = numeroDe(p, "\"colorPrimaries\":"); if (x >= 0) vuiPrim = (int)x;
      x = numeroDe(p, "\"transferCharacteristics\":"); if (x >= 0) vuiTrans = (int)x;
      x = numeroDe(p, "\"matrixCoeffs\":"); if (x >= 0) vuiMatriz = (int)x;
    }
  }
  if (strstr(p, "loadCompleted")) {
    marco("video loadCompleted");
    if (audioAoCarregar >= 0) {
      int a2 = audioAoCarregar; audioAoCarregar = -1;
      video_escolher_audio(a2);
    }
    pronto = 1;
    if (cronPediu && !cronLoad) {
      cronLoad = 1;
      printf("[video] load->loadCompleted %lums\n", msDesdePedido());
    }
    // O bind do ACB vai para um fio proprio COM PAUSAS entre os passos.
    // Motivo medido: cada chamada do AcbAPI e assincrona (o servico responde
    // pelo barramento) e disparando tudo em sequencia o setMediaVideoData
    // chegava antes do register terminar. E POR SESSAO, nao uma vez por
    // processo: sem isto a segunda reproducao herda um ACB apontando para o
    // mediaId morto da anterior.
    if (acb && midia[0]) bindPendente = 1;
  }
  if (strstr(p, "bufferRange")) {
    double e = numeroDe(p, "\"endTime\":");
    if (e >= 0) bufferSeg = e;
  }
  // PAUSA POR FALTA DE DADOS: encher e esvaziar o buffer nao passa por
  // `paused` nem por erro. Carimba quanto do buffer havia no instante.
  if (strstr(p, "bufferingStart")) {
    char m[64];
    // So o PRIMEIRO de uma sequencia carimba. O uMS repete o bufferingStart
    // enquanto nao enche.
    if (!bufferandoDesde) bufferandoDesde = SDL_GetTicks();
    snprintf(m, sizeof m, "buffering INICIO (buffer %+.1fs a frente)",
             bufferSeg - posSeg);
    marco(m);
  }
  if (strstr(p, "bufferingEnd")) {
    char m[64];
    bufferandoDesde = 0;
    snprintf(m, sizeof m, "buffering FIM (buffer %+.1fs a frente)",
             bufferSeg - posSeg);
    marco(m);
  }
  if (strstr(p, "playing")) {
    pausaConfirmada = 0;
    tocando = 1;
    if (acb && midia[0]) {
      long tarefa = 0;
      // COM RECORTE DE FONTE, reaplicar o recorte — e nao a janela lisa.
      if (fonX >= 0 && acbJanelaCustom) {
        SDL_Rect d = escDst(dstX, dstY, dstW, dstH);
        acbJanelaCustom(acb, fonX, fonY, fonW, fonH, d.x, d.y, d.w, d.h,
                        (dstX == 0 && dstY == 0 && dstW == 1920 && dstH == 1080), &tarefa);
        printf("[video] recorte reaplicado com o fluxo ja tocando\n");
      } else {
        SDL_Rect d = escDst(janX, janY, janW, janH);
        acbJanela(acb, d.x, d.y, d.w, d.h,
                  (janX == 0 && janY == 0 && janW == 1920 && janH == 1080), &tarefa);
        printf("[video] janela reaplicada com o fluxo ja tocando\n");
      }
      fflush(stdout);
    }
  }
  if (strstr(p, "paused")) {
    if (tocando && !pausaPedida) marco("pausado PELO PIPELINE");
    tocando = 0;
    pausaConfirmada = pausaPedida;
  }
  if (strstr(p, "endOfStream")) { tocando = 0; terminou = 1; marco("endOfStream"); }

  // ERRO DE VERDADE, e nao "errorCode: 0": o uMS manda esse campo em resposta
  // NORMAL ("No Error").
  if (strstr(p, "errorText") && !strstr(p, "\"No Error\"")) {
    const char *q = strstr(p, "errorText");
    char m[96];
    snprintf(m, sizeof m, "pipeline erro: %.60s", q);
    { char *n2; for (n2 = m; *n2; n2++) if (*n2 == '\n' || *n2 == '\r') *n2 = ' '; }
    marco(m);
    // Guardado para a tela (video_erro_texto): codigo e texto, sem o resto do
    // JSON. "40403 server error:40403" e o que o registro 4958 (#158) trouxe.
    { double cod = numeroDe(p, "\"errorCode\":");
      const char *t = strstr(p, "errorText\":\"");
      char txt[72] = "";
      if (t) {
        const char *f;
        t += 12;
        f = strchr(t, '"');
        if (f && f - t < (int)sizeof txt) { memcpy(txt, t, (size_t)(f - t)); txt[f - t] = 0; }
      }
      snprintf(erroTexto, sizeof erroTexto, "%.0f %s", cod >= 0 ? cod : 0.0, txt); }
    // AUDIO NAO SUPORTADO NAO E FONTE MORTA: o uMS manda errorCode 200 "Audio
    // Codec Not Supported" e segue tocando o video. Marcar `falhou` aqui faria
    // o watchdog de canal pular a fonte com a imagem tocando.
    if (js_num(p, NULL, "errorCode", -1) == 200) {
      audioNaoSup = 1;
      marco("audio nao suportado pela TV (errorCode 200): video segue");
    } else if (strstr(p, "is not running") && urlAtual[0] && !recuperando) {
      // PIPELINE DESTRUIDO: recarrega a mesma fonte (no fio principal).
      tocando = 0;
      recuperando = 1;
      audioAoCarregar = audioAtual;
      marco("pipeline morreu: recarregando");
    } else {
      // marigold: live channels have no VOD reconnect machine; the channel
      // watchdog above this module decides between retry and the next feed.
      tocando = 0; falhou = 1;
    }
  }
  { double v = numeroDe(p, "\"currentTime\":");
    if (v >= 0) {
      posSeg = v / 1000.0;
      posEm = SDL_GetTicks();
      if (!posEm) posEm = 1;
      // Primeiro quadro com avanco: o numero de inicio de verdade.
      if (cronPediu && !cronQuadro && posSeg > 0.0) {
        cronQuadro = 1;
        printf("[video] load->primeiro quadro %lums\n", msDesdePedido());
        // #111 (webOS 25): "comeca com tela preta; trocar a proporcao faz a
        // imagem aparecer". Reaplicar o ultimo recorte agora e barato.
        recorteNoPrimeiroQuadro();
        fflush(stdout);
      }
    } }
  { double v = numeroDe(p, "\"duration\":");
    if (v >= 0) durSeg = v / 1000.0; }
  ack_publicar();
  return 1;
}

static int aoEvento(LSHandle *h, LSMessage *m, void *u) {
  (void)h;
  return eventoPayload(lsPayload(m), (unsigned)(uintptr_t)u);
}

static int soLog(LSHandle *h, LSMessage *m, void *u) {
  (void)h; (void)u;
  printf("[video] %s\n", lsPayload(m)); fflush(stdout);
  return 1;
}

// LSCall com LSError PROPRIO, na pilha: assim nenhuma chamada divide o ERRO
// global com o LSRegister nem com outra chamada em outro fio.
static int lsChamar(const char *uri, const char *carga, Filtro cb, void *ctx,
                    const char *rotulo) {
  union { NvLsErro e; char folga[256]; } er;
  unsigned long tok = 0;
  int ok;
  if (__atomic_load_n(&encerrando, __ATOMIC_ACQUIRE) || !bus) return 0;
  memset(&er, 0, sizeof er);
  if (lsErroIniciar) lsErroIniciar(&er);
  ok = lsCall(bus, uri, carga, cb, ctx, &tok, &er);
  if (!ok) {
    const char *nome = lsreg_nome_codigo(er.e.code);
    printf("[video] %s falhou: code=%d (%s) msg=%.200s\n", rotulo, er.e.code,
           nome ? nome : "?", er.e.message ? er.e.message : "(vazio)");
  }
  if (er.e.message && lsErroLiberar) lsErroLiberar(&er);
  return ok;
}

// CHAMADA LUNA PARA QUEM NAO E O PLAYER. Mesmo barramento: o app roda como
// usuario comum no jail e nao pode executar luna-send; o LS2 por dlopen e a
// porta que funciona. Uma resposta so (LSCallOneReply), entregue no fio do
// laco do glib.
static int iniciar(int automatico);
typedef struct { void (*cb)(const char *, void *); void *ctx; } LunaPedido;
static int lunaResposta(LSHandle *h, LSMessage *m, void *u) {
  LunaPedido *p = u;
  (void)h;
  if (p) { if (p->cb) p->cb(lsPayload(m), p->ctx); free(p); }
  return 1;
}
int video_luna(const char *uri, const char *carga, void (*cb)(const char *, void *), void *ctx) {
  union { NvLsErro e; char folga[256]; } er;
  unsigned long tok = 0;
  LunaPedido *p;
  int ok;
  if (!bus) iniciar(1);
  if (!bus || !lsCallUma) { printf("[video] luna %s: bus unavailable\n", uri); fflush(stdout); return 0; }
  p = malloc(sizeof *p);
  if (!p) return 0;
  p->cb = cb; p->ctx = ctx;
  memset(&er, 0, sizeof er);
  if (lsErroIniciar) lsErroIniciar(&er);
  ok = lsCallUma(bus, uri, carga, lunaResposta, p, &tok, &er);
  if (!ok) {
    printf("[video] luna %s failed: code=%d msg=%.200s\n", uri, er.e.code,
           er.e.message ? er.e.message : "(vazio)");
    fflush(stdout);
    free(p);
  }
  if (er.e.message && lsErroLiberar) lsErroLiberar(&er);
  return ok;
}

static void chamar(const char *metodo, const char *carga, Filtro cb) {
  char uri[128];
  snprintf(uri, sizeof uri, "luna://com.webos.media/%s", metodo);
  lsChamar(uri, carga, cb, NULL, metodo);
}

// Variante para callbacks que precisam saber a qual sessao pertencem. O
// contexto e um inteiro convertido em ponteiro; nao ha alocacao para vazar nem
// memoria cujo tempo de vida possa acabar antes da resposta assincrona.
static int chamarCtx(const char *metodo, const char *carga, Filtro cb,
                      void *ctx) {
  char uri[128];
  snprintf(uri, sizeof uri, "luna://com.webos.media/%s", metodo);
  return lsChamar(uri, carga, cb, ctx, metodo);
}

static void protegerScreensaver(void);
static int modoLoad;   // video_modo_live_consumir do load em curso
static int aoCarregar(LSHandle *h, LSMessage *m, void *u) {
  const char *p = lsPayload(m), *q;
  char b[256];
  unsigned minhaSessao = (unsigned)(uintptr_t)u;
  (void)h;
  printf("[video] load: %s\n", p ? p : "(nulo)"); fflush(stdout);
  if (minhaSessao != sessao) {
    printf("[video] load antigo ignorado (sessao %u, atual %u)\n",
           minhaSessao, sessao);
    // Um load cancelado ainda pode criar um pipeline no uMS. Liberar esse
    // recurso evita deixar o decoder ocupado quando o usuario troca de canal.
    char antigo[96] = "";
    if (p) js_texto(p, NULL, "mediaId", antigo, sizeof antigo);
    if (antigo[0] && strcmp(antigo, midia)) {
      snprintf(b, sizeof b, "{\"mediaId\":\"%s\"}", antigo);
      chamar("unload", b, soLog);
    }
    return 1;
  }
  if (!p || midia[0]) return 1;
  q = strstr(p, "\"mediaId\":\"");
  if (!q) return 1;
  q += 11;
  { const char *f = strchr(q, '"');
    if (!f || f - q >= (int)sizeof midia) return 1;
    memcpy(midia, q, f - q); midia[f - q] = 0; }

  ackMidia = midia[0] != 0;
  ack_publicar();
  protegerScreensaver();
  snprintf(b, sizeof b, "{\"connectionId\":\"%s\"}", midia);
  chamar("notifyForeground", b, soLog);
  snprintf(b, sizeof b, "{\"mediaId\":\"%s\"}", midia);
  chamarCtx("subscribe", b, aoEvento, (void *)(uintptr_t)minhaSessao);
  // Let uMS discover and select its default tracks. Selecting video index 0
  // before sourceInfo can stall TS startup on webOS 24 (#625); a successful
  // load only allocates the pipeline, so request play without selecting tracks.
  snprintf(b, sizeof b, "{\"mediaId\":\"%s\"}", midia);
  chamar("play", b, soLog);
  return 1;
}

static void *rodarLaco(void *u) { (void)u; loopRodar(laco); return NULL; }

// SCREENSAVER DA LG DURANTE O VIDEO. O buraco de video composto por GL nao
// conta como "video em tela cheia" para o tvpower, entao o timer de
// inatividade do remoto dispara o screensaver no meio do jogo. API nao
// documentada: assina registerScreenSaverRequest; quando o screensaver vai
// ativar chega state "Active" + timestamp, e responder ack:false com o MESMO
// timestamp o cancela. Sem video tocando respondemos ack:true para nao segurar
// o screensaver normal da TV.
static int protetorLigado;
// O ACK e publicado pelos setters e eventos, mesmo sem desenho (#205/#428).
// O callback le so o retrato atomico.
static int ackEstado;   // bits ACK_*; __atomic sempre
#define ACK_SEGURAR   1   // video tocando: ack:false
#define ACK_DESCANSO  2   // tela de descanso do app: ack:false
#define ACK_MIDIA     4
#define ACK_TOCANDO   8
#define ACK_PAUSA    16
#define ACK_FIM      32
#define ACK_ERRO     64
static void ack_publicar(void) {
  static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  int e = 0;
  pthread_mutex_lock(&mutex);
  if (ackMidia) e |= ACK_MIDIA;
  if (tocando) e |= ACK_TOCANDO;
  if (pausaPedida) e |= ACK_PAUSA;
  if (terminou) e |= ACK_FIM;
  if (falhou) e |= ACK_ERRO;
  if ((e & (ACK_MIDIA | ACK_TOCANDO | ACK_PAUSA | ACK_FIM | ACK_ERRO)) ==
      (ACK_MIDIA | ACK_TOCANDO)) e |= ACK_SEGURAR;
  else if (esmaecer_segura_protetor_tv()) e |= ACK_DESCANSO;
  __atomic_store_n(&ackEstado, e, __ATOMIC_RELEASE);
  pthread_mutex_unlock(&mutex);
}
static int aoPedidoScreensaver(LSHandle *h, LSMessage *m, void *u) {
  const char *p = lsPayload(m);
  char estado[24], ts[64], b[192];
  int e, segurar;
  (void)h; (void)u;
  if (__atomic_load_n(&encerrando, __ATOMIC_ACQUIRE) || !ligado || !bus || !p ||
      !js_texto_raiz(p, "state", estado, sizeof estado) || strcmp(estado, "Active")) return 1;
  // Preserva o token recebido, inclusive quando string, sem arredondar numeros
  // grandes ou responder com timestamp truncado.
  if (!js_bruto(p, NULL, "timestamp", ts, sizeof ts)) return 1;
  if (ts[0] != '"') {
    char *fim;
    if (!isdigit((unsigned char)ts[0]) && ts[0] != '-') return 1;
    strtod(ts, &fim);
    if (*fim) return 1;
  }
  e = __atomic_load_n(&ackEstado, __ATOMIC_ACQUIRE);
  segurar = (e & ACK_SEGURAR) ? 1 : (e & ACK_DESCANSO) ? 2 : 0;
  snprintf(b, sizeof b, "{\"clientName\":\"" NV_APP_ID "\",\"ack\":%s,\"timestamp\":%s}",
           segurar ? "false" : "true", ts);
  printf("[video] screensaver pedido: %s timestamp=%s midia=%d tocando=%d pausa=%d fim=%d erro=%d\n",
         segurar == 2 ? "seguro (tela de descanso)"
                      : segurar ? "seguro (video tocando)" : "liberado",
         ts, !!(e & ACK_MIDIA), !!(e & ACK_TOCANDO), !!(e & ACK_PAUSA),
         !!(e & ACK_FIM), !!(e & ACK_ERRO));
  fflush(stdout);
  lsChamar("luna://com.webos.service.tvpower/power/responseScreenSaverRequest",
           b, NULL, NULL, "responseScreenSaverRequest");
  return 1;
}
static void protegerScreensaver(void) {
  if (protetorLigado || !ligado || !bus) return;
  esmaecer_protetor_publicador(ack_publicar);
  protetorLigado = lsChamar("luna://com.webos.service.tvpower/power/registerScreenSaverRequest",
           "{\"subscribe\":true,\"clientName\":\"" NV_APP_ID "\"}",
           aoPedidoScreensaver, NULL, "registerScreenSaverRequest");
}

// O ACB EXIGE um callback de verdade. Passar NULL nao e ignorado: no primeiro
// evento ele salta para o endereco 0 e o app morre com SIGSEGV em pc=0x0, longe
// do ponto onde o NULL foi escrito.
static void acbNotificou(long h, long tarefa, long evento,
                         long estApp, long estToca, int resposta) {
  (void)h; (void)tarefa;
  printf("[video] acb evento=%ld app=%ld toca=%ld resp=%d\n",
         evento, estApp, estToca, resposta);
  fflush(stdout);
}

/* URI sources use VIDEO (0). An explicit acb override wins. Rebuild only on
 * the main thread after the old bind has stopped, so no bind can call a freed
 * handle. Failed initialization is not retried every frame. */
static int acbConfigurarTipo(void) {
  int alvo = tipoJogador;   // marigold: no BUFFERSTREAM (MSE) path
  if (!acbCriar || !acbIniciar) return 0;
  if (acb && acbTipoAtual == alvo) return 1;
  if (bindAtivo() || acbTipoFalhou == alvo) return 0;
  acbBindRecolher();
  if (acb) {
    if (acbFinalizar) printf("[video] acb finalize=%d\n", acbFinalizar(acb));
    acbDestruir(acb); acb = 0;
  }
  long novo = acbCriar();
  int ok = novo && acbIniciar(novo, alvo, NV_APP_ID, (void *)acbNotificou);
  printf("[video] acb tipo=%d init=%d\n", alvo, ok); fflush(stdout);
  if (!ok) {
    if (novo) { if (acbFinalizar) acbFinalizar(novo); acbDestruir(novo); }
    acbTipoAtual = -1; acbTipoFalhou = alvo; return 0;
  }
  acb = novo; acbTipoAtual = alvo; acbTipoFalhou = -1;
  printf("[video] acb sink=%d\n", acbSink(acb, tipoSink)); fflush(stdout);
  if (pronto && midia[0]) bindPendente = 1;
  return 1;
}

#define SIM(h, v, n) do { \
    *(void **)(&v) = dlsym(h, n); \
    if (!v) { printf("[video] falta %s\n", n); return 0; } \
  } while (0)

int video_iniciar(void)      { return iniciar(0); }
int video_iniciar_auto(void) { return iniciar(1); }
int video_registro_negado(void) { return regNegado; }

static int iniciar(int automatico) {
  void *L, *G, *A;
  if (ligado) return 1;
  if (!lsreg_pode_tentar(&regEstado, SDL_GetTicks(), automatico)) {
    if (lsreg_desistiu(&regEstado) && !regAvisouDesistir) {
      regAvisouDesistir = 1;
      printf("[video] registro recusado %d vez(es) (%s): tentativas automaticas param "
             "nesta sessao; play ainda tenta\n", regEstado.falhas,
             lsreg_nome_codigo(regEstado.ultimoCodigo)
               ? lsreg_nome_codigo(regEstado.ultimoCodigo) : "?");
      fflush(stdout);
    }
    return 0;
  }
  L = dlopen("libluna-service2.so.3", RTLD_NOW);
  if (!L) L = dlopen("libluna-service2.so", RTLD_NOW);
  G = dlopen("libglib-2.0.so.0", RTLD_NOW);
  A = dlopen("libAcbAPI.so.1", RTLD_NOW);
  if (!L || !G) { printf("[video] libs: %s\n", dlerror()); return 0; }
  if (!A) {
    // webOS 5+: sem ACB, a janela exportada da SDL e quem prende o plano.
    // dlopen(NULL) devolve o handle do PROPRIO processo: a libSDL2 ja esta
    // carregada (o binario linka contra ela), entao os simbolos, quando
    // existem, estao ai.
    void *eu = dlopen(NULL, RTLD_NOW);
    printf("[video] sem libAcbAPI (webOS 5+): tentando janela exportada da SDL\n");
    *(void **)(&sdlExpCriar)    = dlsym(eu, "SDL_webOSCreateExportedWindow");
    *(void **)(&sdlExpJanela)   = dlsym(eu, "SDL_webOSSetExportedWindow");
    *(void **)(&sdlExpRecorte)  = dlsym(eu, "SDL_webOSExportedSetCropRegion");
    *(void **)(&sdlExpDestruir) = dlsym(eu, "SDL_webOSDestroyExportedWindow");
    // O recorte de fonte e opcional (custa o zoom, nao a imagem).
    if (!sdlExpRecorte) printf("[video] sem SDL_webOSExportedSetCropRegion; zoom fica indisponivel\n");
    if (!sdlExpCriar || !sdlExpJanela) {
      printf("[video] esta TV nao tem ACB nem janela exportada; sem caminho de video\n");
      return 0;
    }
    { const char *id = sdlExpCriar(0);   /* 0 = ..._TYPE_VIDEO */
      if (!id || !*id) { printf("[video] SDL_webOSCreateExportedWindow nao devolveu id\n"); return 0; }
      snprintf(expWin, sizeof expWin, "%s", id);
      printf("[video] janela exportada: %s\n", expWin); }
  }

  SIM(L, lsRegister, "LSRegister");
  SIM(L, lsAttach,   "LSGmainAttach");
  SIM(L, lsCall,     "LSCall");
  SIM(L, lsPayload,  "LSMessageGetPayload");
  // Soft: sem eles o erro so fica sem `magic` e sem liberar, como era antes.
  *(void **)(&lsErroIniciar) = dlsym(L, "LSErrorInit");
  *(void **)(&lsErroLiberar) = dlsym(L, "LSErrorFree");
  *(void **)(&lsUnregister) = dlsym(L, "LSUnregister");
  *(void **)(&lsCallUma) = dlsym(L, "LSCallOneReply");
  SIM(G, loopNovo,   "g_main_loop_new");
  SIM(G, loopRodar,  "g_main_loop_run");
  SIM(G, loopParar,  "g_main_loop_quit");
  SIM(G, loopAgendar, "g_idle_add_full");
  if (A) {
  SIM(A, acbCriar,    "AcbAPI_create");
  SIM(A, acbIniciar,  "AcbAPI_initialize");
  SIM(A, acbSink,     "AcbAPI_setSinkType");
  SIM(A, acbMidia,    "AcbAPI_setMediaId");
  SIM(A, acbEstado,   "AcbAPI_setState");
  SIM(A, acbJanela,   "AcbAPI_setDisplayWindow");
  // NAO usa SIM: se a lib desta TV nao tiver o simbolo, o app segue sem zoom
  // em vez de nao iniciar.
  *(void **)(&acbJanelaCustom) = dlsym(A, "AcbAPI_setCustomDisplayWindow");
  if (!acbJanelaCustom) printf("[video] sem AcbAPI_setCustomDisplayWindow; zoom fica indisponivel\n");
  SIM(A, acbDestruir, "AcbAPI_destroy");
  *(void **)(&acbFinalizar) = dlsym(A, "AcbAPI_finalize");
  SIM(A, acbConectar, "AcbAPI_connectDass");
  SIM(A, acbVideoData, "AcbAPI_setMediaVideoData");
  // NAO usa SIM: os dumps de firmware da webosbrew mostram
  // AcbAPI_setMediaAudioData ausente na webOS 3.4.0 (W16N, 2016) e presente
  // na 3.9.2 e na 4.10.
  *(void **)(&acbAudioData) = dlsym(A, "AcbAPI_setMediaAudioData");
  if (!acbAudioData) printf("[video] sem AcbAPI_setMediaAudioData; seguindo sem ele\n");
  }

  // O nome PRECISA casar com o padrao do papel LS2 do app
  // (allowedNames: "com.webos.media.client.*"). Qualquer outro nome e recusado
  // pelo hub e nada depois disso acontece.
  //
  // Depois de um deploy que mata o processo sem video_encerrar(), o hub ainda
  // segura esse nome por um tempo; o sufixo com o PID e o mesmo padrao
  // permitido e libera o player sem esperar o hub soltar o cadastro fantasma.
  bus = NULL;
  erroLimpar();
  if (!lsRegister(NV_LS_MEDIA_CLIENT, &bus, ERRO)) {
    char alt[160];
    logErroLs("LSRegister nome fixo recusado");
    snprintf(alt, sizeof alt, "%s.%d", NV_LS_MEDIA_CLIENT, (int)getpid());
    printf("[video] tentando %s\n", alt);
    bus = NULL;
    erroLimpar();
    if (!lsRegister(alt, &bus, ERRO)) {
      int cod = ERRO_U.e.code;
      logErroLs("LSRegister recusado");
      if (!regEstado.falhas) logContextoLs();
      lsreg_falhou(&regEstado, cod, SDL_GetTicks());
      if (cod == LSR_PERMISSION) regNegado = 1;
      erroLimpar();
      bus = NULL;
      return 0;
    }
  }
  if (!laco) laco = loopNovo(NULL, 0);
  erroLimpar();
  if (!lsAttach(bus, laco, ERRO)) {
    logErroLs("attach falhou");
    // Devolve o nome ao hub: sem isto o registro fica preso e a PROXIMA
    // tentativa de video_iniciar cai no "LSRegister recusado" para sempre.
    erroLimpar();
    if (lsUnregister) lsUnregister(bus, ERRO);
    erroLimpar();
    bus = NULL;
    lsreg_falhou(&regEstado, 0, SDL_GetTicks());
    return 0;
  }
  erroLimpar();
  lsreg_deu_certo(&regEstado);
  regNegado = 0;
  // Laco proprio: o LS2 exige um GMainLoop girando, e girar isso no laco de
  // desenho custaria quadros. As respostas chegam neste fio e so mexem em
  // variaveis simples, lidas pelo desenho sem trava.
  __atomic_store_n(&encerrando, 0, __ATOMIC_RELEASE);   // reinicio pos-encerrar
  if (pthread_create(&fio, NULL, rodarLaco, NULL) != 0) {
    printf("[video] fio do laco nao nasceu\n");
    if (lsUnregister) { erroLimpar(); lsUnregister(bus, ERRO); erroLimpar(); }
    bus = NULL;
    return 0;
  }

  if (A) {
    lerAjustesAcb();
    acbConfigurarTipo();
  }
  ligado = 1;
  ack_publicar();
  printf("[video] pronto (webOS %d, acb=%ld, janela=%s)\n",
         webosMaior(), acb, expWin[0] ? expWin : "-");
  fflush(stdout);
  protegerScreensaver();
  return 1;
}

static int tocarInterno(const char *url);
static void pararSessao(void);

int video_tocar(const char *url) {
  // O modo vale para esta fonte e para os recarregar dela (tocarInterno).
  modoLoad = video_modo_live_consumir();
  fonteTocou = 0;
  falhou = 0; terminou = 0; audioNaoSup = 0;
  ack_publicar();
  snprintf(urlAtual, sizeof urlAtual, "%s", url ? url : "");
  return tocarInterno(url);
}

void video_bombear(void) {
  ack_publicar();
  acbBindRecolher();
  if (ligado) acbConfigurarTipo();
  // O ACB demora cerca de 1,5 s para ligar uma sessao. Se o usuario trocar de
  // canal nesse intervalo, o loadCompleted novo encontra bindVivo=1; o pedido
  // fica pendente.
  if (bindPendente && !bindAtivo() && acb && midia[0] && pronto && acbConfigurarTipo()) {
    AcbBind *bind = malloc(sizeof *bind);
    if (bind) {
      bind->sessao = __atomic_load_n(&sessao, __ATOMIC_ACQUIRE);
      bind->acb = acb; bind->tipo = acbTipoAtual; snprintf(bind->midia, sizeof bind->midia, "%s", midia);
      bindPendente = 0;
      __atomic_store_n(&bindVivo, 1, __ATOMIC_RELEASE);
      if (pthread_create(&fioBind, NULL, prenderPlano, bind) == 0) bindJoinable = 1;
      else { free(bind); __atomic_store_n(&bindVivo, 0, __ATOMIC_RELEASE); bindPendente = 1; }
    }
  }
  // RECUPERACAO DO PIPELINE, no fio principal. Ver a nota em `recuperando`.
  if (recuperando) {
    int aud = audioAoCarregar;
    recuperando = 0;
    marco("recarregando a fonte");
    if (tocarInterno(urlAtual)) audioAoCarregar = aud;
  }
  if (pronto && posSeg > 0.5) fonteTocou = 1;
}

// CABECALHOS EXIGIDOS PELA FONTE, ver video.h.
void video_definir_cabecalhos(const char *cabs) {
  snprintf(cabsHttp, sizeof cabsHttp, "%s", cabs ? cabs : "");
}
const char *video_cabecalhos(void) { return cabsHttp; }

// Monta o "httpHeader" do payload do load a partir das linhas "Nome: valor".
//
// DE ONDE SAI ESTA CHAVE, porque ela nao esta em documentacao publica: foi
// lida no proprio aparelho. Em /usr/lib/libcbe.so, a tabela de opcoes de
// media_option de umediaclient_impl.cc traz "httpHeader" entre
// "usePipelinePreload" e "bufferControl". E /usr/lib/libpf-1.0.so consome
// PF_HTTP_HEADER_REFERRER, PF_HTTP_HEADER_USER_AGENT e PF_HTTP_HEADER_COOKIES,
// que sao os tres campos abaixo.
static void montarHttpHeader(char *dst, unsigned tam) {
  char copia[512], ref[320], ua[320], ck[320];
  char *l, *ctx = NULL;
  int algum = 0;
  dst[0] = 0; ref[0] = 0; ua[0] = 0; ck[0] = 0;
  if (!cabsHttp[0]) return;
  snprintf(copia, sizeof copia, "%s", cabsHttp);
  for (l = strtok_r(copia, "\n", &ctx); l; l = strtok_r(NULL, "\n", &ctx)) {
    char *d = strchr(l, ':');
    char *v;
    if (!d) continue;
    *d = 0;
    v = d + 1;
    while (*v == ' ') v++;
    if (!strcasecmp(l, "referer") || !strcasecmp(l, "referrer"))
      snprintf(ref, sizeof ref, "%s", v);
    else if (!strcasecmp(l, "user-agent"))
      snprintf(ua, sizeof ua, "%s", v);
    else if (!strcasecmp(l, "cookie") || !strcasecmp(l, "cookies"))
      snprintf(ck, sizeof ck, "%s", v);
    // Origin e qualquer outro ficam de fora: libpf so consome estes tres.
  }
  if (!ref[0] && !ua[0] && !ck[0]) return;
  // A FORMA CERTA E O ANINHAMENTO, MEDIDA na C9 em 18/09:
  //   {"transmission":{"httpHeader":{"referer":...}}} -> errorCode 0, TOCA
  // e todas as outras formas testadas dao errorCode 203 ("AV Type Not
  // Founded": o pipeline buscou a pagina de 403 em vez do video).
  { int u = snprintf(dst, tam, "\"transmission\":{\"httpHeader\":{");
    if (ref[0]) { u += snprintf(dst + u, tam - u, "%s\"referer\":\"%s\"", algum++ ? "," : "", ref); }
    if (ua[0])  { u += snprintf(dst + u, tam - u, "%s\"userAgent\":\"%s\"", algum++ ? "," : "", ua); }
    if (ck[0])  { u += snprintf(dst + u, tam - u, "%s\"cookies\":\"%s\"", algum++ ? "," : "", ck); }
    snprintf(dst + u, tam - u, "}},");
  }
  // O cookie do stream e credencial de terceiro: o log mostra so quais campos
  // foram, nunca o valor.
  printf("[video] httpHeader no load: referer=%s userAgent=%s cookies=%s\n",
         ref[0] ? "sim" : "nao", ua[0] ? "sim" : "nao",
         ck[0] ? "sim (omitido)" : "nao");
  fflush(stdout);
}

// MODO DO LOAD (video_modo.c): 0/1 = default-track startup; 2 = the lean
// live payload (mediaTransportType HLS/URI, sem useSeekableRanges/bufferControl).
static void montarCarga(char *dst, size_t tam, const char *url, int modo,
                        const char *hh, const char *janela) {
  const char *buffer = "\"bufferControl\":{\"userBufferCtrl\":false},";
  if (modo == 2) {
    snprintf(dst, tam,
        "{\"payload\":{\"option\":{"
        "\"appId\":\"" NV_APP_ID "\","
        "%s"
        "\"mediaTransportType\":\"%s\","
        "\"windowId\":\"%s\"}},"
        "\"uri\":\"%s\",\"type\":\"media\"}",
        hh, strstr(url, ".m3u8") ? "HLS" : "URI", janela, url);
  } else {
    snprintf(dst, tam,
        "{\"payload\":{\"option\":{\"useSeekableRanges\":true,"
        "\"appId\":\"" NV_APP_ID "\","
        "%s%s"
        "\"windowId\":\"%s\"}},"
        "\"uri\":\"%s\",\"type\":\"media\"}",
        hh, buffer, janela, url);
  }
}

static int tocarInterno(const char *url) {
  // Complete supported URL plus load options/headers; reject overflow below.
  char carga[NV_MEDIA_URL_MAX + 4096];
  unsigned minhaSessao;
  if (!ligado && !video_iniciar()) return 0;
  pararSessao();
  minhaSessao = __atomic_add_fetch(&sessao, 1, __ATOMIC_ACQ_REL);
  viuVideo = 0;
  // O retangulo aplicado e da SESSAO: sem zerar, uma sessao nova que calcule o
  // mesmo rect cairia no "ja e esse" e nunca chegaria a mandar nada ao plano.
  fonX = -1; dstX = dstY = dstW = dstH = -1;
  posSeg = durSeg = bufferSeg = 0; tocando = pronto = 0; midia[0] = 0;
  posEm = 0;
  ackMidia = 0; ack_publicar();
  bufferandoDesde = 0;
  nAudio = 0; audioAtual = 0;
  nVideo = -1; codecVideo[0] = 0;
  snprintf(vidHdr, sizeof vidHdr, "none");
  seiX0 = seiX1 = seiX2 = seiY0 = seiY1 = seiY2 = 0;
  seiBrancoX = seiBrancoY = seiMinLum = seiMaxLum = seiMaxCLL = seiMaxFALL = 0;
  vuiPrim = vuiTrans = vuiMatriz = 2;
  cronPediu = 1; cronLoad = 0; cronQuadro = 0;
  clock_gettime(CLOCK_MONOTONIC, &t0Pedido);
  // windowId "window_id_dummy" NAO e enfeite: com string vazia o load responde
  // returnValue:true, aloca mediaId e nunca busca o arquivo. Falha muda.
  { char hh[768];
    montarHttpHeader(hh, sizeof hh);
    montarCarga(carga, sizeof carga, nv_media_url(url), modoLoad, hh,
                expWin[0] ? expWin : "window_id_dummy");
    if (strlen(carga) >= sizeof carga - 1) {
      printf("[video] carga do load nao coube em %zu bytes (URL de %zu): recusando\n",
             sizeof carga, strlen(url));
      fflush(stdout);
      falhou = 1;
      ack_publicar();
      return 0;
    } }
  // A URL INTEIRA NAO VAI AO LOG (#158): a do Xtream leva usuario e senha no
  // caminho. O host e a extensao bastam para a triagem.
  { char pub[160], ext[12] = "";
    const char *q = strchr(url, '?'), *b, *d;
    size_t n = q ? (size_t)(q - url) : strlen(url);
    for (b = url + n; b > url && b[-1] != '/'; b--) {}
    for (d = url + n; d > b && d[-1] != '.'; d--) {}
    if (d > b && (size_t)(url + n - d) < sizeof ext - 1) {
      size_t k = (size_t)(url + n - d), i;
      int ok = 1;
      for (i = 0; i < k; i++) if (!isalnum((unsigned char)d[i])) ok = 0;
      if (ok && k) { ext[0] = '.'; memcpy(ext + 1, d, k); ext[k + 1] = 0; }
    }
    printf("[video] URL: %s%s%s\n", rede_url_publica(url, pub, sizeof pub),
           ext[0] ? " " : "", ext);
    fflush(stdout); }
  // JANELA EXPORTADA ANTES DO LOAD (#158) — DEFENSIVO, NAO PROVADO. A ordem do
  // guia de midia do webosbrew e: cria a janela, SetExportedWindow, e so entao
  // o load com o windowId.
  if (expWin[0]) expJanelaAplicar();
  if (modoLoad) { printf("[video] modo do load: %d\n", modoLoad); fflush(stdout); }
  chamarCtx("load", carga, aoCarregar, (void *)(uintptr_t)minhaSessao);
  return 1;
}

void video_parar(void) {
  pararSessao();
}

static void pararSessao(void) {
  char b[128];
  __atomic_add_fetch(&sessao, 1, __ATOMIC_ACQ_REL);
  // Invalida tambem a sessao que ainda esta esperando o retorno de load. Esse
  // era o caso abrir -> sair -> abrir que travava: nao havia mediaId para
  // descarregar, mas o callback antigo continuava vivo e contaminava o novo.
  bindPendente = 0;
  recuperando = 0;
  audioAoCarregar = -1;
  pausaPedida = 0;
  pausaConfirmada = 0;
  if (ligado && midia[0]) {
    snprintf(b, sizeof b, "{\"mediaId\":\"%s\"}", midia);
    chamar("unload", b, soLog);
  }
  midia[0] = 0; tocando = pronto = 0; falhou = 0; audioNaoSup = 0;
  ackMidia = 0; ack_publicar();
  erroTexto[0] = 0;
}

void video_pausar(int pausado) {
  char b[128];
  if (!ligado || !midia[0]) return;
  snprintf(b, sizeof b, "{\"mediaId\":\"%s\"}", midia);
  pausaConfirmada = 0;
  pausaPedida = pausado;
  chamar(pausado ? "pause" : "play", b, soLog);
  tocando = !pausado;
  ack_publicar();
}

int video_pausa_confirmada(void) {
  return pausaPedida && pausaConfirmada && pronto && midia[0] &&
         !falhou && !terminou;
}

void video_volume(int pct) {
  char b[128];
  if (!ligado || !midia[0]) return;
  if (pct < 0) pct = 0; else if (pct > 100) pct = 100;
  snprintf(b, sizeof b, "{\"mediaId\":\"%s\",\"volume\":%d}", midia, pct);
  chamar("setVolume", b, soLog);
}

// A JANELA EXPORTADA COM O QUADRO INTEIRO COMO ORIGEM (#158). Aqui ia `src`
// NULL ("o quadro inteiro"), e o SDL de parte das TVs repassa o nulo direto ao
// protocolo: em umas TVs isso so fica no log; em outras a conexao com o Wayland
// cai, o SDL manda SDL_QUIT e o app fecha. `src` e o quadro que o decoder
// entrega: {0, 0, largura, altura}.
static void expJanelaAplicar(void) {
  SDL_Rect src, dst;
  if (!expWin[0] || !sdlExpJanela || janW < 1 || janH < 1) return;
  src.x = 0; src.y = 0; src.w = vidW > 0 ? vidW : 1920; src.h = vidH > 0 ? vidH : 1080;
  dst = escDst(janX, janY, janW, janH);
  expSrcW = src.w; expSrcH = src.h;
  printf("[video] janela exportada (quadro %dx%d) -> %d\n", src.w, src.h,
         sdlExpJanela(expWin, &src, &dst));
  fflush(stdout);
}

// Chamada UMA vez pelo main, com o drawable que o SDL entregou. Fica em 1920x1080
// (escala 1, o caminho da C9) se vier algo invalido.
void video_escala_definir(int sw, int sh) {
  if (sw < 1 || sh < 1) return;
  escW = sw; escH = sh;
  printf("[video] escala da janela %.3fx%.3f (superficie %dx%d, layout 1920x1080)\n",
         (double)sw / 1920.0, (double)sh / 1080.0, sw, sh);
  fflush(stdout);
}

// O retangulo do plano de hardware. O retangulo NAO pode passar da tela.
// MEDIDO: mandar ao ACB um retangulo com origem negativa ou maior que o painel
// NAO recorta nada — o plano simplesmente APAGA.
void video_janela(int x, int y, int w, int h) {
  long tarefa = 0;
  int cheia = (x == 0 && y == 0 && w == 1920 && h == 1080);
  if (w < 1 || h < 1) return;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > 1920) w = 1920 - x;
  if (y + h > 1080) h = 1080 - y;
  if (w < 1 || h < 1) return;
  if (x == janX && y == janY && w == janW && h == janH) return;  // sem repetir o mesmo rect a cada quadro
  janX = x; janY = y; janW = w; janH = h;
  // A janela simples muda o plano sem passar pelo par fonte/destino.
  fonX = -1; dstX = -1;
  if (!ligado || !midia[0]) return;   // sem midia presa, aplicar seria no vazio
  if (!acb && !expWin[0]) return;
  printf("[video] janela %d,%d %dx%d cheia=%d\n", x, y, w, h, cheia);
  fflush(stdout);
  if (expWin[0]) {
    expJanelaAplicar();
    return;
  }
  { SDL_Rect d = escDst(x, y, w, h);
    acbJanela(acb, d.x, d.y, d.w, d.h, cheia, &tarefa); }
}

// marigold: applies the window again even when it is the same rect. A change
// made while the bind thread was still attaching the plane can be lost (the
// bind read the old rect), so the app re-applies it a few times.
void video_janela_forcar(int x, int y, int w, int h) {
  janX = janY = janW = janH = -1;
  video_janela(x, y, w, h);
}

static int semUms;   // 1 depois que o recorte de fonte foi recusado

void video_recorte_reaplicar(void) {
  long tarefa = 0;
  SDL_Rect d;
  if (fonX < 0 || !ligado || !midia[0] || !acb || !acbJanelaCustom) return;
  d = escDst(dstX, dstY, dstW, dstH);
  printf("[video] recorte repetido: fonte %d,%d %dx%d -> destino %d,%d %dx%d -> %d\n",
         fonX, fonY, fonW, fonH, dstX, dstY, dstW, dstH,
         acbJanelaCustom(acb, fonX, fonY, fonW, fonH, d.x, d.y, d.w, d.h,
                         (dstX == 0 && dstY == 0 && dstW == 1920 && dstH == 1080), &tarefa));
  fflush(stdout);
}

// ZOOM DE VERDADE: recorta a FONTE e mantem o destino dentro da tela. Ampliar
// nao e inflar o destino (que apaga o plano), e sim pedir um pedaco MENOR da
// fonte para o mesmo destino.
void video_janela_fonte(int sx, int sy, int sw, int sh,
                        int dx, int dy, int dw, int dh) {
  int cheia;
  if (sw < 2 || sh < 2 || dw < 1 || dh < 1) return;
  // Ja recusou uma vez nesta sessao: nao insistir.
  if (semUms) { video_janela(dx, dy, dw, dh); return; }
  if (dx < 0) { dw += dx; dx = 0; }
  if (dy < 0) { dh += dy; dy = 0; }
  if (dx + dw > 1920) dw = 1920 - dx;
  if (dy + dh > 1080) dh = 1080 - dy;
  if (dw < 1 || dh < 1) return;
  cheia = (dx == 0 && dy == 0 && dw == 1920 && dh == 1080);
  if (sx == fonX && sy == fonY && sw == fonW && sh == fonH &&
      dx == dstX && dy == dstY && dw == dstW && dh == dstH) return;
  fonX = sx; fonY = sy; fonW = sw; fonH = sh;
  dstX = dx; dstY = dy; dstW = dw; dstH = dh;
  janX = dx; janY = dy; janW = dw; janH = dh;   // o reaplicar do bind usa estes
  if (!ligado || !midia[0]) return;
  printf("[video] fonte %d,%d %dx%d -> destino %d,%d %dx%d\n",
         sx, sy, sw, sh, dx, dy, dw, dh);
  fflush(stdout);
  // webOS 5+: o recorte vive na janela exportada.
  if (expWin[0]) {
    SDL_Rect org, src, dst;
    org.x = 0;  org.y = 0;  org.w = vidW > 0 ? vidW : 1920; org.h = vidH > 0 ? vidH : 1080;
    src.x = sx; src.y = sy; src.w = sw; src.h = sh;
    dst = escDst(dx, dy, dw, dh);
    if (sdlExpRecorte && sdlExpRecorte(expWin, &org, &src, &dst)) return;
    printf("[video] janela exportada recusou o recorte; sem zoom\n"); fflush(stdout);
    semUms = 1;
    video_janela(dx, dy, dw, dh);
    return;
  }
  // O caminho e o ACB, nao o luna direto: o hub recusa o app no tv.display.
  if (acbJanelaCustom && acb) {
    long tarefa = 0;
    SDL_Rect d = escDst(dx, dy, dw, dh);
    int r = acbJanelaCustom(acb, sx, sy, sw, sh, d.x, d.y, d.w, d.h, cheia, &tarefa);
    printf("[video] acb janela custom -> %d\n", r); fflush(stdout);
    if (r) return;
    printf("[video] acb recusou o recorte; voltando a tela cheia\n"); fflush(stdout);
  }
  semUms = 1;
  video_janela(dx, dy, dw, dh);
}

double video_pos(void)      { return posSeg; }
int    video_pos_idade_ms(void) {
  Uint32 t = posEm;
  return t ? (int)(SDL_GetTicks() - t) : -1;
}
double video_duracao(void)  { return durSeg; }
double video_buffer_fim(void) { return bufferSeg; }
int    video_tocando(void)  { return tocando; }
int    video_pronto(void)   { return pronto; }
// Ha midia carregada. O furo na superficie usa ISTO e nao o loadCompleted:
// abrir o buraco cedo nao custa nada (atras dele so existe o plano de video).
int    video_ativo(void)    { return midia[0] != 0; }
// Erro real do pipeline na fonte atual ("Playing error" e afins). Zera no
// proximo video_tocar. O watchdog de canal usa isto para trocar de fonte.
int    video_falhou(void)   { return falhou; }
const char *video_erro_texto(void) { return erroTexto; }
int    video_decoder_anunciou(void) { return viuVideo; }
int    video_audio_nao_suportado(void) { return audioNaoSup; }
int    video_terminou(void) { return terminou; }
unsigned video_bufferando_ms(void) {
  Uint32 d = bufferandoDesde;
  if (!d) return 0;
  // 1 e nao 0 quando o carimbo acabou de sair: 0 e a resposta reservada para
  // "nao esta bufferizando".
  { Uint32 v = SDL_GetTicks() - d; return v ? (unsigned)v : 1u; }
}

int  video_n_audio(void)   { return nAudio; }
int  video_n_video(void)   { return nVideo; }
const char *video_codec_video(void) { return codecVideo; }
const VideoFaixa *video_audio(int i)   { return (i >= 0 && i < nAudio) ? &faixaAudio[i] : NULL; }
int  video_audio_atual(void)   { return audioAtual; }
const char *video_hdr(void)       { return vidHdr; }
int  video_largura(void)          { return vidW; }
int  video_altura(void)           { return vidH; }

void video_escolher_audio(int i) {
  char b[192];
  const VideoFaixa *f = video_audio(i);
  if (!ligado || !midia[0] || !f) return;
  if (i != audioAtual) audioNaoSup = 0;
  snprintf(b, sizeof b, "{\"mediaId\":\"%s\",\"type\":\"audio\",\"index\":%d}",
           midia, f->numero);
  chamar("selectTrack", b, soLog);
  audioAtual = i;
}

static int encerrarLaco(void *l) {
  loopParar(l);
  return 0; // remove a fonte
}

void video_encerrar(void) {
  if (!ligado) return;
  video_parar();
  // Ate aqui o unload ainda vale (bus vivo). Daqui em diante nenhum
  // callback do laco age, e o fio e parado/ESPERADO antes do handle sumir.
  __atomic_store_n(&encerrando, 1, __ATOMIC_RELEASE);
  if (bindJoinable) { pthread_join(fioBind, NULL); bindJoinable = 0; }
  if (acb) { if (acbFinalizar) acbFinalizar(acb); acbDestruir(acb); acb = 0; }
  acbTipoAtual = acbTipoFalhou = -1;
  if (expWin[0] && sdlExpDestruir) { sdlExpDestruir(expWin); expWin[0] = 0; }
  // O loop usa o contexto padrao (loopNovo(NULL, 0)). A fonte fica pendente
  // ate ele rodar: quit direto ANTES do run se perde e trava o join (#205).
  if (laco) loopAgendar(-100, encerrarLaco, laco, NULL);
  pthread_join(fio, NULL);
  if (bus && lsUnregister) {
    erroLimpar();
    lsUnregister(bus, ERRO);
    erroLimpar();
    bus = NULL;
  }
  protetorLigado = 0;
  ligado = 0;
  ack_publicar();
}
