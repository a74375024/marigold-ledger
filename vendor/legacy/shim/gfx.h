// marigold: the part of Legacy's gfx.h that ponteiro.c uses, implemented by
// Marigold's own renderer (src/platform/gfx_c.cpp).
#ifndef NV_GFX_H
#define NV_GFX_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { float x, y, w, h; } GfxRect;
extern float gfx_opacidade_grupo;
float gfx_escala(void);
void gfx_sem_recorte(void);
// Rounded rectangle; `raio` <= 0.5 is a fraction of the shorter side.
void gfx_cor(GfxRect r, float raio, float cr, float cg, float cb, float ca);
#ifdef __cplusplus
}
#endif
#endif
