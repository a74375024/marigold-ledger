// marigold: see esmaecer.h.
#include "esmaecer.h"

static int segurando;                 // __atomic
static void (*publicador)(void);      // __atomic

int esmaecer_segura_protetor_tv(void) { return __atomic_load_n(&segurando, __ATOMIC_ACQUIRE); }

void esmaecer_protetor_publicador(void (*fn)(void)) { __atomic_store_n(&publicador, fn, __ATOMIC_RELEASE); }

void esmaecer_segurar(int on) {
  int novo = on ? 1 : 0;
  if (__atomic_exchange_n(&segurando, novo, __ATOMIC_ACQ_REL) == novo) return;
  void (*fn)(void) = __atomic_load_n(&publicador, __ATOMIC_ACQUIRE);
  if (fn) fn();
}
