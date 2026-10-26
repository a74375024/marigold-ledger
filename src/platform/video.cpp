#include "platform/video.hpp"

#include <cmath>

#include "core/util/log.hpp"
#include "core/util/redact.hpp"
#include "video.h"

namespace marigold::platform {

bool Video::start() {
  if (started_) return true;
  started_ = video_iniciar_auto() != 0;
  MLOG_I("video", "LS2 registration %s", started_ ? "ok" : "failed");
  return started_;
}

bool Video::play(const std::string& url, const std::string& headers, int loadMode) {
  if (url.empty()) return false;
  video_definir_cabecalhos(headers.c_str());
  video_definir_modo_live(loadMode);
  bool ok = video_tocar(url.c_str()) != 0;
  started_ = started_ || ok;
  MLOG_I("video", "play (mode %d): %s", loadMode, ok ? "load sent" : "refused");
  return ok;
}

void Video::stop() { video_parar(); }

void Video::pump() { video_bombear(); }

void Video::setWindow(gfx::Rect r) {
  video_janela(static_cast<int>(std::lround(r.x)), static_cast<int>(std::lround(r.y)),
               static_cast<int>(std::lround(r.w)), static_cast<int>(std::lround(r.h)));
}

void Video::shutdown() {
  if (started_) video_encerrar();
  started_ = false;
}

VideoState Video::state() const {
  VideoState s;
  s.registered = started_;
  s.registrationDenied = video_registro_negado() != 0;
  s.active = video_ativo() != 0;
  s.ready = video_pronto() != 0;
  s.playing = video_tocando() != 0;
  s.failed = video_falhou() != 0;
  s.ended = video_terminou() != 0;
  s.decoderSeen = video_decoder_anunciou() != 0;
  s.audioUnsupported = video_audio_nao_suportado() != 0;
  s.bufferingMs = video_bufferando_ms();
  s.position = video_pos();
  s.bufferEnd = video_buffer_fim();
  s.width = video_largura();
  s.height = video_altura();
  s.hdr = video_hdr();
  s.error = redact(video_erro_texto());
  return s;
}

}  // namespace marigold::platform
