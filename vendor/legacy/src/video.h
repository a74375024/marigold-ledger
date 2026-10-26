// Reproducao de video de verdade nesta TV, por LS2 direto.
//
// marigold: trimmed to the live-only API (see UPSTREAM.md).
//
// O modulo NAO desenha nada. O video vive num PLANO DE HARDWARE separado, atras
// da superficie GL do app; o que o app faz e abrir um buraco transparente por
// onde esse plano aparece. Consequencia que economiza horas: glReadPixels e o
// servico de captura da TV NUNCA vao fotografar o video. Isso e o modelo, nao
// defeito — verificar reproducao se faz pelo estado.
//
// Por que LS2 direto e nao StarfishMediaAPIs, tudo medido no aparelho:
// o construtor da StarfishMediaAPIs chama exit(0) quando o processo nao casa com
// o "exeName" do papel LS2 (nao e crash: atexit dispara e o journal fica mudo),
// e mesmo com o papel certo ele nunca chega a falar com com.webos.media aqui —
// responde erro 202 "Media Not Found", que e string interna da propria lib.
// A sequencia abaixo, ao contrario, saiu de captura ls-monitor do navegador da
// TV tocando o mesmo arquivo: e o que comprovadamente funciona.
#ifndef NV_VIDEO_H
#define NV_VIDEO_H

#ifdef __cplusplus
extern "C" {
#endif

// Registra no barramento e sobe o laco de eventos. 1 se deu certo.
// Falhar aqui nao e fatal: o app segue sem video.
int  video_iniciar(void);
// O MESMO, para tentativas automaticas: segue o recuo de lsregistro.h e
// desiste depois de poucas recusas.
int  video_iniciar_auto(void);
// 1 quando o hub LS2 negou o registro por PERMISSAO (LS_ERROR_CODE_PERMISSION)
// nesta sessao: nenhum video abre, e a fonte nao tem culpa.
int  video_registro_negado(void);

// Comeca a tocar. `url` e http(s):// ou file://. NAO mandar mediaTransportType
// no modo 0: o transporte sai do prefixo da URL.
int  video_tocar(const char *url);

// Chamar UMA VEZ POR QUADRO: liga o plano depois do load (fio do bind) e
// recarrega um pipeline que morreu.
void video_bombear(void);
void video_parar(void);
void video_pausar(int pausado);
// Volume do pipeline, 0..100 (uMS setVolume).
void video_volume(int pct);

// Retangulo do plano de video, em coordenadas de tela 1920x1080. Fica preso a
// tela: pedir origem negativa ou tamanho maior que o painel APAGA o plano — um
// plano de hardware nao recorta o excedente. Para ampliar, use a funcao abaixo.
void video_janela(int x, int y, int w, int h);

// Zoom de verdade: recorta a FONTE (coordenadas do quadro decodificado, ver
// video_largura/video_altura) e desenha no destino (coordenadas de tela).
void video_janela_fonte(int sx, int sy, int sw, int sh,
                        int dx, int dy, int dw, int dh);
// Manda de novo ao plano o ultimo par fonte/destino, sem o dedup.
void video_recorte_reaplicar(void);
// Tamanho da superficie (drawable) em que o destino do plano de video e lido; o
// layout e 1920x1080 e o retangulo e escalado por isso (issue #176). Chamar uma
// vez, depois de criar a janela. Ver video_escala.h.
void video_escala_definir(int sw, int sh);

double video_pos(void);      // segundos decorridos
double video_duracao(void);  // 0 enquanto desconhecida (canal ao vivo: 0)
double video_buffer_fim(void); // ate onde o buffer cobre (s); 0 se desconhecido
// Ha quanto tempo (ms) o pipeline esta parado esperando dados, do par de
// eventos bufferingStart/bufferingEnd do uMS; 0 quando NAO esta bufferizando.
// Uma fonte que morre no meio da reproducao deixa o app com imagem congelada e
// video_falhou() em 0 para sempre: quem vigia fonte de canal precisa disto.
unsigned video_bufferando_ms(void);

// CABECALHOS QUE A FONTE EXIGE, uma linha "Nome: valor" por cabecalho. Chamar
// ANTES de video_tocar. String vazia limpa. So Referer, User-Agent e Cookie
// chegam ao pipeline (libpf so consome esses tres).
void video_definir_cabecalhos(const char *cabs);
const char *video_cabecalhos(void);

// MODO DO LOAD para o PROXIMO video_tocar (#158, LG C4: dado chega, VDEC
// concedido, decoder nunca anuncia). 0/1 = default-track startup; 2 = the lean
// live payload (mediaTransportType HLS/URI, sem useSeekableRanges/bufferControl).
// Vale UMA vez (video_modo_live_consumir zera). Ver video_modo.c.
void video_definir_modo_live(int modo);
int  video_modo_live_consumir(void);

int    video_tocando(void);
// Pausa comprovada pelo backend da sessao atual, nao a intencao local.
int    video_pausa_confirmada(void);
int    video_pronto(void);   // 1 depois do loadCompleted
int    video_ativo(void);    // 1 assim que ha mediaId — e o que abre o furo
int    video_falhou(void);   // 1 depois de um errorText real na fonte atual
// O ULTIMO ERRO REAL do pipeline na fonte atual, como o pipeline o disse
// ("40403 server error:40403", "100 Playing error"); "" sem erro.
const char *video_erro_texto(void);
// 1 depois que o decoder se anunciou (videoInfo do uMS) na fonte atual. Separa
// "abre devagar" de "chega dado e o decoder nunca comeca" (#158).
int video_decoder_anunciou(void);
// 1 once the current source has played (kept across reloads of it).
int video_fonte_tocou(void);
int    video_audio_nao_suportado(void);  // uMS errorCode 200: video segue sem som
int    video_terminou(void); // 1 depois do fim de fluxo (endOfStream) da fonte atual

// --- faixas de audio (evento sourceInfo do uMS) -----------------------------
#define NV_FAIXA_MAX 16

typedef struct {
  char idioma[8];    // "en"; vazio quando o fluxo nao etiqueta
  int  numero;       // indice que o selectTrack espera
  char codec[24];
  int  canais;       // 0 = nao informado
} VideoFaixa;

int  video_n_audio(void);
const VideoFaixa *video_audio(int i);
int  video_audio_atual(void);
void video_escolher_audio(int i);

const char *video_hdr(void);   // hdrType cru: "none", "HDR10"...
// Dimensoes do QUADRO decodificado, do videoInfo.
int  video_largura(void);
int  video_altura(void);

void video_encerrar(void);

// One-reply webOS LS2 request; callback runs on the GLib thread.
int video_luna(const char *uri, const char *payload, void (*callback)(const char *, void *), void *context);

#ifdef __cplusplus
}
#endif
#endif
