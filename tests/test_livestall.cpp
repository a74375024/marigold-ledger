// Behaviour of Legacy's live stall watchdog (vendor/legacy/src/livestall.h).
#include "livestall.h"

#include "doctest.h"

namespace {

// Plays a stream for `ms`, advancing the position in step with the clock.
unsigned playFor(NvLiveStall& s, unsigned now, unsigned ms, double& pos) {
  for (unsigned t = 0; t < ms; t += 500) {
    now += 500;
    pos += 0.5;
    CHECK(nv_ls_passo(&s, now, pos, 1, 0, 0, 0) == NV_LS_NADA);
  }
  return now;
}

}  // namespace

TEST_CASE("a frozen picture schedules a reopen with backoff") {
  NvLiveStall s;
  nv_ls_zerar(&s);
  double pos = 0;
  unsigned now = playFor(s, 1000, 5000, pos);
  int r = NV_LS_NADA;
  unsigned frozenAt = now;
  while (r == NV_LS_NADA && now < frozenAt + 20000) {
    now += 100;
    r = nv_ls_passo(&s, now, pos, 1, 0, 0, 0);
  }
  CHECK(r == NV_LS_AGENDOU);
  CHECK(now - frozenAt >= NV_LS_PARADO_MS);
  CHECK(nv_ls_passo(&s, now + 100, pos, 1, 0, 0, 0) == NV_LS_ESPERA);
  CHECK(nv_ls_passo(&s, now + nv_ls_espera_ms(1), pos, 1, 0, 0, 0) == NV_LS_REABRIR);
}

TEST_CASE("pause by the viewer never counts") {
  NvLiveStall s;
  nv_ls_zerar(&s);
  double pos = 0;
  unsigned now = playFor(s, 1000, 3000, pos);
  for (int i = 0; i < 100; ++i) {
    now += 1000;
    CHECK(nv_ls_passo(&s, now, pos, 1, 1, 0, 0) == NV_LS_NADA);
  }
}

TEST_CASE("gives up after the retry limit") {
  NvLiveStall s;
  nv_ls_zerar(&s);
  double pos = 0;
  unsigned now = 1000;
  int last = NV_LS_NADA;
  for (int round = 0; round <= NV_LS_MAX && last != NV_LS_DESISTIR; ++round) {
    now = playFor(s, now, 2000, pos);
    last = NV_LS_NADA;
    while (last != NV_LS_REABRIR && last != NV_LS_DESISTIR) {
      now += 250;
      last = nv_ls_passo(&s, now, pos, 1, 0, 0, 0);
    }
    if (last == NV_LS_REABRIR) nv_ls_reaberto(&s, now);
  }
  CHECK(last == NV_LS_DESISTIR);
}

TEST_CASE("end of stream after playing reopens") {
  NvLiveStall s;
  nv_ls_zerar(&s);
  double pos = 0;
  unsigned now = playFor(s, 1000, 3000, pos);
  CHECK(nv_ls_passo(&s, now + 10, pos, 1, 0, 1, 0) == NV_LS_AGENDOU);
}

TEST_CASE("nothing happens before the stream is ready") {
  NvLiveStall s;
  nv_ls_zerar(&s);
  for (unsigned t = 0; t < 60000; t += 1000) CHECK(nv_ls_passo(&s, t, 0, 0, 0, 0, 0) == NV_LS_NADA);
}
