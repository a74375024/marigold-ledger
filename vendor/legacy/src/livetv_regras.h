// REGRAS DA LIVE TV que nao dependem de rede nem de SDL: resolucao lida do
// nome da fonte/canal, nome-base para achar as variantes FHD/HD/SD do mesmo
// canal, e as recomendacoes do diagnostico da Live TV (livetvdiag.c).
//
// Funcoes puras e inline, para o teste compilar no Mac (tests/livetv_regras.sh).
#ifndef NV_LIVETV_REGRAS_H
#define NV_LIVETV_REGRAS_H

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

// --- resolucao -----------------------------------------------------------------
// Opcao de Ajustes "Resolucao principal": 0 Automatica, 1 4K, 2 1080p, 3 720p, 4 SD.
static inline int nv_res_opcao_altura(int opcao) {
  static const int A[] = { 0, 2160, 1080, 720, 480 };
  return opcao >= 0 && opcao <= 4 ? A[opcao] : 0;
}
static inline int nv_res_altura_opcao(int altura) {
  return altura >= 2160 ? 1 : altura >= 1080 ? 2 : altura >= 720 ? 3 : altura > 0 ? 4 : 0;
}

// marigold: o resto (nome da fonte, nome-base, busca do guia, diagnostico)
// nao e usado aqui; o catalogo tem as proprias regras (src/core/catalog).

#endif
