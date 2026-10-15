// Emscripten build glue: see bliss_web.h and web/WEB.md.
#define BLISS_WEB_IMPL
#include "bliss_web.h"

#include <emscripten.h>
#include <emscripten/fiber.h>
#include <SDL_mixer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>

#define GAME_C_STACK_SIZE        (1024 * 1024)
#define GAME_ASYNCIFY_STACK_SIZE (256 * 1024)
#define MAIN_ASYNCIFY_STACK_SIZE (64 * 1024)
// A game loop that neither presents nor waits for this long yields anyway.
#define BUSY_YIELD_MS            50

static emscripten_fiber_t mainFiber, gameFiber;
static void *gameCStack, *gameAsyncifyStack, *mainAsyncifyStack;

static int onGameFiber;       // currently executing on the game fiber
static int gameExited;        // the game called exit()/Quit()
static int paused;            // host pause
static int shutDown;          // audio/SDL closed, listeners removed (quit or bliss_dispose)
static Uint32 pausedAt;       // real SDL_GetTicks() when the host paused
static Uint32 pausedTotal;    // real time spent in host pause: the game clock skips it
static int startedEmitted;
static unsigned frameCount;   // frames presented by the game

static Uint32 resumedAt;      // when the game fiber was last resumed
static Uint32 lastPresentAt;  // when the last frame was presented
static int resumedFromPresent;// the last yield was the end of a frame (no wait since)

int BW_MasterVolume = 100;

static int gameArgc;
static char **gameArgv;

void BW_Emit (const char *type, const char *json)
{
    EM_ASM({
        var type = UTF8ToString($0);
        var json = UTF8ToString($1);
        if (Module['blissOnEventInternal']) Module['blissOnEventInternal'](type, json);
        if (Module['onBlissEvent'])
        {
            try { Module['onBlissEvent'](type, json); }
            catch (e) { console.error('[bliss] onBlissEvent handler failed', e); }
        }
    }, type, json ? json : "{}");
}

static void JsonString (char *out, size_t size, const char *str)
{
    size_t n = 0;
    if (size < 3) return;
    out[n++] = '"';
    for (const unsigned char *p = (const unsigned char *) str; p && *p && n + 7 < size; p++)
    {
        if (*p == '"' || *p == '\\') { out[n++] = '\\'; out[n++] = *p; }
        else if (*p == '\n') { out[n++] = '\\'; out[n++] = 'n'; }
        else if (*p < 0x20) n += snprintf (out + n, size - n, "\\u%04x", *p);
        else out[n++] = *p;
    }
    out[n++] = '"';
    out[n] = 0;
}

// Game clock (SDL_GetTicks in the game sources): stands still while the host has paused.
Uint32 BW_GetTicks (void)
{
    return SDL_GetTicks () - pausedTotal;
}

// ---- cooperative scheduling ---------------------------------------------------------------

void BW_Yield (void)
{
    if (!onGameFiber)
        return;
    onGameFiber = 0;
    emscripten_fiber_swap (&gameFiber, &mainFiber);
    onGameFiber = 1;
    resumedAt = SDL_GetTicks ();
    resumedFromPresent = 0;
}

void BW_YieldIfBusy (void)
{
    if (onGameFiber && SDL_GetTicks () - resumedAt >= BUSY_YIELD_MS)
        BW_Yield ();
}

void BW_RenderPresent (SDL_Renderer *renderer)
{
    SDL_RenderPresent (renderer);
    frameCount++;
    if (!startedEmitted)
    {
        startedEmitted = 1;
        BW_Emit ("started", "{}");
    }
    lastPresentAt = SDL_GetTicks ();
    BW_Yield ();
    resumedFromPresent = 1;
}

void BW_Delay (Uint32 ms)
{
    // A wait right after a presented frame counts from the moment the frame was presented,
    // so "present; wait one VBL" (fades, fizzle) takes one animation frame, not two.
    Uint32 start = resumedFromPresent ? lastPresentAt : SDL_GetTicks ();
    resumedFromPresent = 0;
    if (!onGameFiber)
        return;
    while (SDL_GetTicks () - start < ms)
        BW_Yield ();
}

int BW_WaitEvent (SDL_Event *event)
{
    while (!SDL_PollEvent (NULL))
        BW_Yield ();
    return event ? SDL_PollEvent (event) : 1;
}

void BW_Error (const char *message)
{
    char json[600];
    char str[540];
    fprintf (stderr, "[wolf4sdl] %s\n", message);
    JsonString (str, sizeof(str), message);
    snprintf (json, sizeof(json), "{\"message\":%s}", str);
    BW_Emit ("error", json);
}

// Stops everything for good: main loop, audio device (SDL closes its AudioContext), SDL
// (window event handlers), pre.js listeners, pointer lock. Idempotent.
static void ShutDown (void)
{
    if (shutDown)
        return;
    shutDown = 1;
    gameExited = 1;
    emscripten_cancel_main_loop ();
    Mix_CloseAudio ();
    SDL_Quit ();
    EM_ASM({
        if (Module['blissAbort']) Module['blissAbort'].abort();
        if (typeof document !== 'undefined' && document.pointerLockElement && document.exitPointerLock)
            document.exitPointerLock();
    });
}

void BW_Exit (int status)
{
    char json[64];
    ShutDown ();
    snprintf (json, sizeof(json), "{\"status\":%d}", status);
    BW_Emit ("quit", json);
    // Leave the game fiber for good (main() returned, Quit(), fatal error). If exit() was
    // somehow called outside the fiber, the main loop is already cancelled.
    if (onGameFiber)
    {
        onGameFiber = 0;
        emscripten_fiber_swap (&gameFiber, &mainFiber);
    }
    abort ();   // not reached
}

// ---- audio ---------------------------------------------------------------------------------

void BW_ApplyMasterVolume (Uint8 *stream, int len)
{
    int volume = BW_MasterVolume;
    if (volume >= 100)
        return;
    Sint16 *samples = (Sint16 *) stream;
    int count = len / 2;
    if (volume <= 0)
    {
        memset (stream, 0, len);
        return;
    }
    // Linear: 0..100 -> gain volume/100 (same curve as the other Bliss engines).
    int gain = volume * 65536 / 100;
    for (int i = 0; i < count; i++)
        samples[i] = (Sint16) ((samples[i] * gain) >> 16);
}

// ---- main loop -----------------------------------------------------------------------------

// Wolf4SDL opens lower-case file names (vswap.wl1); the host may write VSWAP.WL1.
static void LowerCaseDataFiles (void)
{
    DIR *dir = opendir (".");
    struct dirent *entry;
    struct stat st;
    char lower[256];
    if (!dir)
        return;
    while ((entry = readdir (dir)) != NULL)
    {
        size_t i, n = strlen (entry->d_name);
        if (n >= sizeof(lower))
            continue;
        for (i = 0; i <= n; i++)
            lower[i] = (char) tolower ((unsigned char) entry->d_name[i]);
        if (strcmp (lower, entry->d_name) && stat (lower, &st) != 0)
            rename (entry->d_name, lower);
    }
    closedir (dir);
}

static void GameEntry (void *arg)
{
    (void) arg;
    LowerCaseDataFiles ();
    onGameFiber = 1;
    resumedAt = SDL_GetTicks ();
    int status = wolf_main (gameArgc, gameArgv);
    BW_Exit (status);
}

// CPU time the game spends per animation frame (diagnostics: bliss_get_frame_work).
static double workTotalMs, workMaxMs;
static unsigned workFrames;

static void Frame (void)
{
    if (paused || gameExited)
        return;
    double t0 = emscripten_get_now ();
    emscripten_fiber_swap (&mainFiber, &gameFiber);
    double dt = emscripten_get_now () - t0;
    workTotalMs += dt;
    if (dt > workMaxMs) workMaxMs = dt;
    workFrames++;
}

// {"frames":N,"avgMs":x,"maxMs":y} since the last call; the string is valid until the next call.
EMSCRIPTEN_KEEPALIVE const char *bliss_get_frame_work (void)
{
    static char json[96];
    snprintf (json, sizeof(json), "{\"frames\":%u,\"avgMs\":%.3f,\"maxMs\":%.3f}",
              workFrames, workFrames ? workTotalMs / workFrames : 0.0, workMaxMs);
    workTotalMs = workMaxMs = 0;
    workFrames = 0;
    return json;
}

// The host passes extra command line arguments with Module.arguments (e.g. ["--goobers"]).
// The web build always runs windowed at the original 320x200 resolution.
int main (int argc, char *argv[])
{
    static const char *defaults[] = { "--windowed", "--res", "320", "200" };
    const int ndefaults = sizeof(defaults) / sizeof(defaults[0]);

    gameArgc = argc + ndefaults;
    gameArgv = (char **) calloc (gameArgc + 1, sizeof(char *));
    gameArgv[0] = argc > 0 ? argv[0] : (char *) "wolf3d";
    for (int i = 0; i < ndefaults; i++)
        gameArgv[1 + i] = (char *) defaults[i];
    for (int i = 1; i < argc; i++)
        gameArgv[ndefaults + i] = argv[i];

    // Keyboard input only while the canvas has focus.
    SDL_SetHint (SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas");

    gameCStack = malloc (GAME_C_STACK_SIZE);
    gameAsyncifyStack = malloc (GAME_ASYNCIFY_STACK_SIZE);
    mainAsyncifyStack = malloc (MAIN_ASYNCIFY_STACK_SIZE);
    emscripten_fiber_init_from_current_context (&mainFiber, mainAsyncifyStack, MAIN_ASYNCIFY_STACK_SIZE);
    emscripten_fiber_init (&gameFiber, GameEntry, NULL, gameCStack, GAME_C_STACK_SIZE,
                           gameAsyncifyStack, GAME_ASYNCIFY_STACK_SIZE);

    emscripten_set_main_loop (Frame, 0, 1);   // 0 fps = requestAnimationFrame
    return 0;
}

// ---- host API (EXPORTED_FUNCTIONS) ---------------------------------------------------------

static void SetAudioRunning (int running)
{
    // Pausing the SDL device keeps SDL's own AudioContext auto-resume from unpausing the
    // sound; suspending the context saves the CPU of the script processor.
    Mix_PauseAudio (!running);
    EM_ASM({
        var sdl2 = Module['SDL2'];
        var ctx = sdl2 && sdl2.audioContext;
        if (!ctx) return;
        if ($0) { if (ctx.state === 'suspended') ctx.resume(); }
        else if (ctx.state === 'running') ctx.suspend();
    }, running);
}

static void EmitPause (void)
{
    char json[32];
    snprintf (json, sizeof(json), "{\"paused\":%s}", paused ? "true" : "false");
    BW_Emit ("pauseChanged", json);
}

EMSCRIPTEN_KEEPALIVE void bliss_pause (void)
{
    if (paused || gameExited) return;
    paused = 1;
    pausedAt = SDL_GetTicks ();
    SetAudioRunning (0);
    EmitPause ();
}

EMSCRIPTEN_KEEPALIVE void bliss_resume (void)
{
    if (!paused || gameExited) return;
    paused = 0;
    pausedTotal += SDL_GetTicks () - pausedAt;
    SetAudioRunning (1);
    EmitPause ();
}

EMSCRIPTEN_KEEPALIVE int bliss_is_paused (void)
{
    return paused;
}

EMSCRIPTEN_KEEPALIVE int bliss_set_master_volume (int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    BW_MasterVolume = volume;
    return volume;
}

EMSCRIPTEN_KEEPALIVE int bliss_get_master_volume (void)
{
    return BW_MasterVolume;
}

// Releases all keys the game thinks are held (pre.js calls it when the canvas loses focus,
// as the key-up events then go elsewhere).
extern void IN_ClearKeysDown (void);
EMSCRIPTEN_KEEPALIVE void bliss_release_keys (void)
{
    if (!shutDown)
        IN_ClearKeysDown ();
}

// Ends the module for good: stops the main loop, closes audio (and its AudioContext), quits
// SDL, removes the pre.js listeners and exits pointer lock. Idempotent; the module cannot be
// used afterwards (create a new one with createWolf3d).
EMSCRIPTEN_KEEPALIVE void bliss_dispose (void)
{
    if (shutDown)
        return;
    paused = 0;
    ShutDown ();
    BW_Emit ("disposed", "{}");
}

// Number of frames the game has presented (diagnostics / tests).
EMSCRIPTEN_KEEPALIVE unsigned bliss_get_frame_count (void)
{
    return frameCount;
}
