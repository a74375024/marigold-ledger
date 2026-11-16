// marigold: stand-in for Legacy's esmaecer.h (the old app's own idle screen).
// Marigold has no idle screen. The TV screensaver is held while video plays
// (video.c decides that by itself) and, since Build 166, while the app asks
// for it (Test all sources: platform::Window::keepAwake), through the same
// "hold" answer to the TV's screensaver request.
#ifndef NV_ESMAECER_H
#define NV_ESMAECER_H
#ifdef __cplusplus
extern "C" {
#endif
// Whether the app holds the TV's screensaver now (video.c's ACK_DESCANSO).
int esmaecer_segura_protetor_tv(void);
// video.c hands over the function that publishes its answer again; called
// whenever the hold changes.
void esmaecer_protetor_publicador(void (*fn)(void));
// marigold: the app holds (1) or lets go of (0) the screensaver. Any thread.
void esmaecer_segurar(int on);
#ifdef __cplusplus
}
#endif
#endif
