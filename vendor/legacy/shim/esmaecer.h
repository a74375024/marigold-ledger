// marigold: stand-in for Legacy's esmaecer.h (the old app's own idle screen).
// Marigold has no idle screen, so the TV screensaver is held only while video
// plays (video.c decides that by itself).
#ifndef NV_ESMAECER_H
#define NV_ESMAECER_H
static inline int esmaecer_segura_protetor_tv(void) { return 0; }
static inline void esmaecer_protetor_publicador(void (*fn)(void)) { (void)fn; }
#endif
