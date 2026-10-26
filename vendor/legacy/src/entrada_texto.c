// Teclado/ditado do sistema por plataforma. API e regra de uso: entrada_texto.h.
//
// LG webOS (pesquisa e medicao de 01/10/2026)
//   O app usa o libSDL2 DO APARELHO. Na C9 do dono (webOS 4.5, Rockhopper
//   4.10.2) o arquivo e /usr/lib/libSDL2-2.0.so.0.4.1, e as strings dele dizem
//   "lib32-libsdl2-webos/2.0.5-83.gld4tv": e o SDL 2.0.5 da LG, nao o upstream.
//   MEDIDO (strings do binario na TV): ele tem WebOSShowScreenKeyboard,
//   WebOSHideScreenKeyboard, WebOSIsScreenKeyboardShown, text_model_factory e
//   handlers TextModelEnter/Leave/KeySym/DeleteSurroundingText/
//   InputPanelState, mais "called text_model_activate". Ou seja, o SDL de
//   fabrica liga SDL_StartTextInput ao text_model do compositor (o mesmo
//   protocolo que o MaliitServer da TV atende). MEDIDO no D1 (registro 15988 e
//   16001, webos, Mali-G71): "[spotlight] lg sdl 2.0.5 osk=1 textinput=0" —
//   SDL_HasScreenKeyboardSupport responde 1 na TV.
//   MEDIDO NA C9 (OLED65C9PSA, 01/10/2026, porta "ime:abrir" + captura do
//   compositor por com.webos.service.tv.capture/executeOneShot):
//     - SDL_StartTextInput abre o teclado da LG (QWERTY "POR", com tecla de
//       MICROFONE, Enter, Limpar todos) na metade de baixo da tela; o
//       SDL_IsScreenKeyboardShown vira 1 uns 5 quadros depois.
//     - Texto confirmado chega como UM SDL_TEXTINPUT com a palavra inteira
//       (insertText "matrix" -> TEXTINPUT de 6 bytes; "x ção" -> 7 bytes,
//       UTF-8 certo), SEM KEYDOWN de letra. Apagar no teclado chega como
//       KEYDOWN SDLK_BACKSPACE (scancode 42).
//     - Enter do teclado FECHA o teclado e nao manda Return ao app; so se ve
//       o IsScreenKeyboardShown cair. Por isso na LG TS_EV_FIM nunca diz
//       "confirmou".
//     - SDL_StopTextInput esconde o teclado.
//     - O texto foi injetado pelo com.webos.service.ime/insertText (API
//       interna, so root): o MaliitServer entrega ao app pelo mesmo
//       commit_string que a digitacao no controle. Digitar com o controle de
//       verdade e a tecla de microfone NAO foram exercitados por mim.
//   ARMADILHA MEDIDA: o Spotlight de hoje descarta TEXTINPUT ASCII (espera o
//   KEYDOWN, que na LG nao vem) e fecha com Backspace no campo vazio — por isso
//   o consumidor TEM de usar texto_sistema_engole e o valor deste modulo.
//   Ditado: o servico com.webos.service.voiceinput da TV e PRIVADO
//   (api-permissions.d: getDevices/startStreaming... em "private"); app de
//   terceiros nao o chama. TS_VOZ fica 0 na LG.
//
// Android: o mesmo SDL_StartTextInput (o Spotlight ja usava). Ditado continua
// no android_ditado_* do Spotlight; aqui TS_VOZ fica 0.
//
// Samsung .wgt: o tizen-shell.html tem um <input> escondido e a funcao
// window.nvTexto. focus() por JS abre o IME porque o metadado
// use.keypad.without.useraction vale true por padrao (Samsung, guia
// "Keyboard/IME"). Done = keyCode 65376, Cancel = 65385. O JS guarda o valor e
// um contador; texto_sistema_quadro le os dois por EM_ASM (sem chamar o WASM de
// dentro de um evento JS, que com ASYNCIFY pode cair no meio de um sono).
// O <input> serve a digitacao e ao botao de microfone das buscas;
// nvTexto.microfone diz o que a TV respondeu sobre microfone.
//
// Samsung .tpk: o host .NET registra abrir/fechar por nv_tpk_texto_registrar
// (canario, Texto.cs) ou so o botao de microfone por nv_tpk_voz_registrar
// (release, Voz.cs), e devolve por nv_tpk_texto_valor/nv_tpk_texto_fim. Host
// antigo nao registra: texto_sistema_disponivel() da 0 e fica o teclado do app.
#include "entrada_texto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include "webosver.h"

// #642 (webOS 26): a chamada de IME do SDL de fabrica pode derrubar o app
// inteiro (teclado do sistema abre = crash). A protecao abaixo apanha o sinal
// dentro da chamada e desliga o teclado do sistema nesta sessao: a grade do
// app assume (texto_sistema_disponivel -> 0) e o IME nao e chamado de novo.
// O mesmo vale para o teclado que nunca aparece (180 quadros sem OSK).
static sigjmp_buf saltaIme;
// Armado POR FIO: um sinal em outro fio nunca pode saltar para a pilha deste
// fio (o saltaIme e daqui); a falha alheia cai no tratador anterior (queda.c).
static __thread volatile sig_atomic_t imeArmado;
static volatile sig_atomic_t imeQuebrou;
static const int imeSinais[5] = { SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE };
static struct sigaction imeVelho[5];
static void sinalIme(int s, siginfo_t *si, void *ctx) {
  if (imeArmado) { imeArmado = 0; imeQuebrou = 1; siglongjmp(saltaIme, 1); }
  // Falha fora da chamada (outro fio, instalacao ou restauracao dos
  // tratadores): repassa ao tratador que estava. Devolver aqui faria a falha
  // se repetir para sempre, pois SA_NODEFER nao bloqueia o sinal.
  {
    int k;
    for (k = 0; k < 5; k++) if (imeSinais[k] == s) {
      struct sigaction *v = &imeVelho[k];
      if ((v->sa_flags & SA_SIGINFO) && v->sa_sigaction) { v->sa_sigaction(s, si, ctx); return; }
      if (!(v->sa_flags & SA_SIGINFO) && v->sa_handler &&
          v->sa_handler != SIG_DFL && v->sa_handler != SIG_IGN) { v->sa_handler(s); return; }
    }
    signal(s, SIG_DFL);
    raise(s);
  }
}
static void notaQueda(void) {
  printf("[texto] chamada de IME do sistema falhou (#642): o teclado do app assume\n");
  fflush(stdout);
}
// O timeout de 180 quadros so condena o IME no webOS 26 (onde ele morre de
// verdade). Nas versoes antigas o teclado pode so demorar: a sessao cai para
// o teclado do app desta vez, mas TS_TECLADO volta a ser oferecido depois.
static int imeSemOskMata(void) {
  return nv_webos_major() >= 26;
}
static int sdlImeVazio(void (*fn)(void)) {
  struct sigaction novo;
  int k;
  if (imeQuebrou) return -1;
  // O salto fica valido ANTES de instalar os tratadores e e desarmado antes
  // de restaura-los: nunca ha siglongjmp para um buffer vencido.
  if (sigsetjmp(saltaIme, 1) == 0) {
    memset(&novo, 0, sizeof novo);
    novo.sa_sigaction = sinalIme;
    sigemptyset(&novo.sa_mask);
    novo.sa_flags = SA_SIGINFO | SA_NODEFER;
    for (k = 0; k < 5; k++) sigaction(imeSinais[k], &novo, &imeVelho[k]);
    imeArmado = 1;
    fn();
    imeArmado = 0;
  } else { notaQueda(); }
  for (k = 0; k < 5; k++) sigaction(imeSinais[k], &imeVelho[k], NULL);
  return imeQuebrou ? -1 : 0;
}
static SDL_Window *imeWin;
static int imeMostra;
static void medeMostra(void) { imeMostra = imeWin ? (int)SDL_IsScreenKeyboardShown(imeWin) : 0; }
static int sdlImeMostra(SDL_Window *w) {
  imeWin = w; imeMostra = 0;
  return sdlImeVazio(medeMostra) < 0 ? -1 : imeMostra;
}
static int imeQuebrado(void) { return imeQuebrou; }

#define TS_MAX 8192   // marigold: was NV_ARTE_URL_MAX (arteurl.h not vendored)

static int aberto, viuTeclado, quadrosAberto;
static Uint32 tipoEv;
static char valor[TS_MAX];          // fio do app
static char lido[TS_MAX];           // o que texto_sistema_valor devolve

// Pendencias das pontes (qualquer fio): protegidas pelo spinlock e
// consumidas em texto_sistema_quadro, no fio do app.
static SDL_SpinLock trava;
static char pendValor[TS_MAX];
static int pendTemValor, pendFim = -1;



static void copiar(char *dst, const char *src) {
  size_t n = 0;
  if (!src) src = "";
  while (src[n] && n + 1 < TS_MAX) n++;
  // nao deixa meia sequencia UTF-8 no fim do corte
  if (src[n]) while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
  memcpy(dst, src, n);
  dst[n] = 0;
}

Uint32 texto_sistema_evento(void) {
  if (!tipoEv) {
    Uint32 t = SDL_RegisterEvents(1);
    tipoEv = t == (Uint32)-1 ? SDL_USEREVENT : t;
  }
  return tipoEv;
}

static void avisar(int codigo, int confirmou) {
  SDL_Event ev;
  SDL_zero(ev);
  ev.type = texto_sistema_evento();
  ev.user.code = codigo;
  ev.user.data1 = confirmou ? (void *)1 : NULL;
  SDL_PushEvent(&ev);
}

static void terminar(int confirmou, const char *porque) {
  if (!aberto) return;
  aberto = 0;
  printf("[texto] teclado do sistema fechou (%s%s)\n", porque, confirmou ? ", confirmou" : "");
  fflush(stdout);
  avisar(confirmou < 0 ? TS_EV_ERRO : TS_EV_FIM, confirmou > 0);
}

int texto_sistema_disponivel(void) {
  return !imeQuebrado() && SDL_HasScreenKeyboardSupport() ? TS_TECLADO : 0;
}

int texto_sistema_aberto(void) { return aberto; }

void texto_sistema_mic_modo(int modo) {
#ifdef SAMSUNG_MIC
  if (modo == vModo) return;
  vModo = modo;
  printf("[texto] botao de microfone: ajuste %d\n", modo);
  fflush(stdout);
#else
  (void)modo;
#endif
}

int texto_sistema_voz_pelo_teclado(void) {
  return 0;
}

const char *texto_sistema_valor(void) {
  memcpy(lido, valor, sizeof lido);
  return lido;
}

void texto_sistema_abrir(const char *atual, int voz) {
  int tem = texto_sistema_disponivel(), peloMic = 0;
  if (!peloMic && !(tem & TS_TECLADO)) return;
  if (!(tem & TS_VOZ)) voz = 0;
  copiar(valor, atual);
  SDL_AtomicLock(&trava); pendTemValor = 0; pendFim = -1; SDL_AtomicUnlock(&trava);
  aberto = 1; viuTeclado = 0; quadrosAberto = 0;
  printf("[texto] abrindo o teclado do sistema%s (%d bytes)\n",
         peloMic ? " pelo botao de microfone" : voz ? " com ditado" : "", (int)strlen(valor));
  fflush(stdout);
  // #642: na webOS 26 esta chamada pode derrubar o processo; a falha vira
  // TS_EV_ERRO e o teclado do app fica como unico daqui em diante.
  if (sdlImeVazio(
        SDL_StartTextInput
      ) < 0) terminar(-1, "ime-caiu");
}

void texto_sistema_fechar(void) {
  if (!aberto) return;
  sdlImeVazio(SDL_StopTextInput);
  terminar(0, "app");
}

// Apaga o ultimo caractere UTF-8.
static void apagarUltimo(void) {
  size_t n = strlen(valor);
  while (n > 0) {
    n--;
    if (((unsigned char)valor[n] & 0xC0) != 0x80) break;
  }
  valor[n] = 0;
}

static int teclaDeTexto(SDL_Keycode k) {
  return k == SDLK_BACKSPACE || (k >= 32 && k < 127);
}

int texto_sistema_engole(const SDL_Event *e) {
  if (!aberto) return 0;
  if (e->type == SDL_TEXTINPUT || e->type == SDL_TEXTEDITING) return 1;
  if (e->type == SDL_KEYDOWN || e->type == SDL_KEYUP) return teclaDeTexto(e->key.keysym.sym);
  return 0;
}

void texto_sistema_observar(const SDL_Event *e) {
  if (!aberto) return;
  // LG e Android: o texto vem pelos eventos do SDL.
  if (e->type == SDL_TEXTINPUT) {
    size_t n = strlen(valor), m = strlen(e->text.text);
    printf("[texto] TEXTINPUT %d bytes\n", (int)m);
    if (n + m < TS_MAX) { memcpy(valor + n, e->text.text, m + 1); avisar(TS_EV_TEXTO, 0); }
    return;
  }
  if (e->type == SDL_TEXTEDITING) {
    printf("[texto] TEXTEDITING %d bytes start=%d len=%d\n", (int)strlen(e->edit.text), e->edit.start, e->edit.length);
    return;
  }
  if (e->type == SDL_KEYDOWN) {
    SDL_Keycode k = e->key.keysym.sym;
    printf("[texto] KEYDOWN sym=%d scancode=%d\n", (int)k, (int)e->key.keysym.scancode);
    if (k == SDLK_BACKSPACE) { apagarUltimo(); avisar(TS_EV_TEXTO, 0); }
    // Letra/digito ASCII como KEYDOWN SEM o TEXTINPUT par: so o Android faz
    // os dois, e la o TEXTINPUT tambem chega, entao nada a acrescentar aqui.
    return;
  }
}

void texto_sistema_quadro(void) {
  int temValor, fim;
  char v[TS_MAX];
  if (!aberto) return;
  quadrosAberto++;
  SDL_AtomicLock(&trava);
  temValor = pendTemValor; pendTemValor = 0;
  if (temValor) memcpy(v, pendValor, sizeof v);
  fim = pendFim; pendFim = -1;
  SDL_AtomicUnlock(&trava);
  if (temValor && strcmp(v, valor)) {
    memcpy(valor, v, sizeof valor);
    printf("[texto] valor do sistema: %d bytes\n", (int)strlen(valor));
    avisar(TS_EV_TEXTO, 0);
  }
  if (fim >= 0) { terminar(fim == 2 ? -1 : fim, "system"); return; }
  // LG/Android: fechou por fora (Voltar no teclado da TV)? So depois de o
  // teclado ter sido visto aberto, e com folga para ele subir.
  {
    SDL_Window *w = SDL_GetKeyboardFocus();
    int mostra = sdlImeMostra(w);
    if (mostra < 0) { terminar(-1, "ime-caiu"); return; }
    if (mostra && !viuTeclado) {
      viuTeclado = 1;
      printf("[texto] teclado do sistema na tela (quadro %d)\n", quadrosAberto);
      fflush(stdout);
    }
    if (viuTeclado && !mostra) { sdlImeVazio(SDL_StopTextInput); terminar(0, "osk-oculto"); }
    else if (!viuTeclado && quadrosAberto == 180) {
      printf("[texto] 180 quadros e SDL_IsScreenKeyboardShown ainda 0 (janela=%p)\n", (void *)w);
      fflush(stdout);
      // #642: 3 s sem o teclado = IME morto nesta sessao (webOS 26). Falha
      // declarada: o dono larga o sistema e a grade do app assume. So o
      // webOS 26 condena o IME para sempre; nas antigas ele pode demorar.
      if (imeSemOskMata()) { imeQuebrou = 1; notaQueda(); }
      terminar(-1, "ime-sem-osk");
    }
  }
}

void texto_sistema_ponte_valor(const char *v) {
  SDL_AtomicLock(&trava);
  copiar(pendValor, v);
  pendTemValor = 1;
  SDL_AtomicUnlock(&trava);
}

void texto_sistema_ponte_fim(int confirmou) {
  SDL_AtomicLock(&trava);
  pendFim = confirmou < 0 ? 2 : confirmou ? 1 : 0;
  SDL_AtomicUnlock(&trava);
}
