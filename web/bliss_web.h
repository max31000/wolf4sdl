// Emscripten (WebAssembly) build glue: cooperative main loop, host API, host events.
// Included by wl_def.h after <SDL.h> in __EMSCRIPTEN__ builds only. Contract: web/WEB.md.
//
// The original game is a set of nested blocking loops (menus, fades, intermissions, PlayLoop,
// IN_Ack, ...). Instead of rewriting the game flow into a state machine, the unmodified game
// runs on an Emscripten fiber (Asyncify) and the browser drives it from
// emscripten_set_main_loop (requestAnimationFrame): every frame the main loop switches into
// the game fiber, the game runs until it presents a frame or waits, and switches back.
#ifndef BLISS_WEB_H
#define BLISS_WEB_H

#ifdef __EMSCRIPTEN__

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

// The game's own main() (wl_main.c), started on the game fiber.
int wolf_main (int argc, char *argv[]);

// Game clock: SDL_GetTicks() without the time spent in host pause.
Uint32 BW_GetTicks (void);
// Gives control back to the browser until the next animation frame.
void BW_Yield (void);
// Yields if the game has been running for a long time without yielding (spin-wait guard).
void BW_YieldIfBusy (void);
// SDL_RenderPresent + end of frame (yield).
void BW_RenderPresent (SDL_Renderer *renderer);
// SDL_Delay replacement: yields until the time has passed.
void BW_Delay (Uint32 ms);
// SDL_WaitEvent replacement: yields until an event is pending.
int BW_WaitEvent (SDL_Event *event);
// exit() replacement: stops the main loop, emits "quit" and never returns.
void BW_Exit (int status) __attribute__((noreturn));
// Error message box replacement (console + "error" event).
void BW_Error (const char *message);
// Host event channel: Module.onBlissEvent(type, json).
void BW_Emit (const char *type, const char *json);
// Master volume 0..100 applied to the whole mix (digitized SFX, AdLib music and sounds).
extern int BW_MasterVolume;
void BW_ApplyMasterVolume (Uint8 *stream, int len);

#ifdef __cplusplus
}
#endif

#ifndef BLISS_WEB_IMPL
#define SDL_GetTicks()          BW_GetTicks()
#define SDL_Delay(ms)           BW_Delay(ms)
#define SDL_RenderPresent(r)    BW_RenderPresent(r)
#define SDL_WaitEvent(e)        BW_WaitEvent(e)
#define exit(status)            BW_Exit(status)
#define main                    wolf_main
#endif

#endif // __EMSCRIPTEN__

#endif // BLISS_WEB_H
