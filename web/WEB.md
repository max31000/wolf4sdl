# Wolf4SDL web build (Emscripten / WebAssembly)

Branch `bliss-web` of [max31000/wolf4sdl](https://github.com/max31000/wolf4sdl), a plain mirror of
the maintained Wolf4SDL upstream [bitbucket.org/ks-presto/wolf4sdl](https://bitbucket.org/ks-presto/wolf4sdl)
(KS-Presto's continuation of Moritz "Ripper" Kroll's port; the old GitHub repo KS-Presto/Wolf4SDL
is archived and points there). Used by Bliss (mvv42.ru/bliss) to run Wolfenstein 3D shareware v1.4.

The module contains no game data. The host page supplies the original data files.

## Build

```sh
emcmake cmake -S . -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-web            # -> build-web/bin/wolf3d.js + wolf3d.wasm
```

Emscripten 6.0.10 (CI: `emscripten/emsdk:6.0.10`, `.github/workflows/web-release.yml`).
`CMakeLists.txt` only builds the web version; native builds keep using the Makefile.
CMake options:

- `WOLF_VERSION_DEFINES` (default `CARMACIZED;UPLOAD` = shareware v1.4, `*.WL1`): the version.h
  defines of the game version. For example, `CARMACIZED` alone gives the registered Apogee v1.4 (`*.WL6`).
- `WOLF_WEB_PRELOAD_DIR`: dev only. Packages a data directory into `/data` with `--preload-file`.

The build always defines `CLASSIC_MENU`, which keeps the original menus and default controls
(no free look, WASD or display menu).

Release: pushing a tag `web-vX.Y.Z` attaches `wolf3d-web-X.Y.Z.tgz` and a `.sha256` file to the
GitHub release. The archive holds `wolf3d.js`, `wolf3d.wasm`, `README.md` and the licence files.

## Host contract

```js
import createWolf3d from './wolf3d.js';           // ES module, MODULARIZE factory

const files = { 'AUDIOHED.WL1': u8, 'AUDIOT.WL1': u8, 'GAMEMAPS.WL1': u8, 'MAPHEAD.WL1': u8,
                'VGADICT.WL1': u8, 'VGAGRAPH.WL1': u8, 'VGAHEAD.WL1': u8, 'VSWAP.WL1': u8 };
const wolf = await createWolf3d({
  canvas,                                          // <canvas>; pixel size becomes 320x200
  locateFile: (name) => wasmUrl,                   // 'wolf3d.wasm'
  blissDataDir: '/data',                           // default '/data'
  preRun: [(M) => {                                // the directory already exists here
    for (const [name, bytes] of Object.entries(files)) M.FS.writeFile(`/data/${name}`, bytes);
  }],
  onBlissEvent(type, json) { /* see below */ },
  arguments: [],                                   // optional extra Wolf4SDL options, e.g. ['--goobers']
});
```

- Data files may be written in upper or lower case. When `main()` starts it renames them to lower case,
  which is what Wolf4SDL opens. Async loading works: call `M.addRunDependency('x')` in preRun and
  `M.removeRunDependency('x')` once the files are written (see `web/host-test.html`).
- Canvas: the game renders the original 320x200 mode. The host sets the displayed size with CSS,
  4:3 like a CRT (e.g. 960x720) with `image-rendering: pixelated`. SDL does not resize the canvas.
  The canvas gets `tabindex=0`, takes focus on pointer down and has no context menu. Keyboard input
  is bound to the canvas only (`SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT "#canvas"`, SDL text input
  off), so typing in page inputs does not reach the game. Call `canvas.focus()` after `started`.
- Input is keyboard only. The mouse is disabled because there is no pointer lock. Controls are the
  originals: arrows move, Ctrl fires, Alt strafes, Shift runs, Space opens doors, 1-4 pick weapons,
  Esc opens the menu, F1-F10 work as in DOS (F10 quits).
- Audio: AdLib music and AdLib sound effects (MAME OPL2 emulation), digitized sound effects
  (8 channels) and the PC speaker, all mixed by SDL2_mixer into the SDL2 AudioContext
  (`Module.SDL2.audioContext`). The AudioContext resumes on the first key or pointer gesture.
- Config and save games are written to MEMFS (`$HOME/.wolf4sdl`) and are not persisted.
- Command line defaults: `--windowed --res 320 200`, followed by `Module.arguments`.

### Events: `onBlissEvent(type, json)`

| type | json | when |
|---|---|---|
| `started` | `{}` | first frame presented (signon screen) |
| `newGame` | `{"difficulty":0..3,"episode":0..5}` | New Game was chosen |
| `pauseChanged` | `{"paused":bool}` | `bliss_pause` / `bliss_resume` |
| `error` | `{"message":"..."}` | fatal error (`Quit("...")`, missing data, ...) |
| `quit` | `{"status":0\|1}` | the game exited (F10 / menu Quit / error). The main loop is stopped. |

### Exports (`Module._name` / `ccall`)

| function | |
|---|---|
| `bliss_pause()` / `bliss_resume()` | Stops or resumes the main loop and suspends or resumes the AudioContext. This is a host-level freeze, separate from the in-game Pause key. |
| `bliss_is_paused() -> 0/1` | |
| `bliss_set_master_volume(0..100) -> int` | Master gain on the whole mix (curve `(v/100)^2`); 100 = unchanged |
| `bliss_get_master_volume() -> int` | |
| `bliss_get_frame_count() -> uint` | Frames presented by the game (diagnostics) |
| `bliss_get_frame_work() -> char*` | JSON `{"frames","avgMs","maxMs"}` of game CPU time per animation frame since the last call (read it with `UTF8ToString`) |

Runtime methods exported: `FS`, `ccall`, `cwrap`, `UTF8ToString`, `addRunDependency`, `removeRunDependency`.

## How the main loop works

Wolf3D is written as nested blocking loops: signon, title and demo loop, menus, fades, fizzle,
`IN_Ack`, intermission counters, `PlayLoop`, the death cam and so on. Wolf4SDL has more than 30 of them,
waiting with `SDL_Delay`, `SDL_WaitEvent` or tic polling. Rewriting all of them into a state
machine would change most of the game's control flow and make upstream merges very hard. So the
unmodified game runs on an **Emscripten fiber (Asyncify)**, and the browser drives it from
`emscripten_set_main_loop(Frame, 0 /*requestAnimationFrame*/, 1)` (`web/bliss_web.c`):

- `Frame()` switches into the game fiber. The game runs until it gives control back:
  - `SDL_RenderPresent` marks the end of a frame. Control returns to the browser and the game
    continues on the next animation frame.
  - `SDL_Delay(ms)` returns to the browser until `ms` have passed. A wait directly after a present
    counts from the present, so "present + wait one VBL" (fades) takes one animation frame, not two.
  - `SDL_WaitEvent` returns to the browser until an event is queued.
  - Guard: `IN_ProcessEvents` / `GetTimeCount` give control back if the game has run for 50 ms
    without yielding (for polling loops without a wait).
- These are macros in `web/bliss_web.h`, included from `wl_def.h` in `__EMSCRIPTEN__` builds.
  The game sources change only in a few small `#ifdef __EMSCRIPTEN__` blocks.
- Timing stays the original 70 Hz tic clock (`SDL_GetTicks`). The game draws at most one frame per
  animation frame: 60 fps on a 60 Hz display, about 70 fps (the original tic rate) on faster displays.
- No pthreads. `exit()` / `Quit()` stop the main loop and emit `quit`.

Cost: Asyncify instrumentation makes the wasm about 1.5 MB (550 KB gzip). Game CPU time is about
1 ms per frame in Chromium on a desktop. `ASYNCIFY_IGNORE_INDIRECT` would save about 300 KB, but it is
unsafe here: menu routines and actor think functions are called through pointers and can yield.

## Dev test

```sh
python -m http.server 8777 --bind 127.0.0.1 --directory M:/Projects    # repo + data under one root
python web/host_test.py --browser all --seconds 60 --shots out/
```

`web/host-test.html?build=../build-web/bin/&data=/bliss/assets/wolf3d/&scale=3` is the host page.
`web/host_test.py` (Python Playwright) runs this sequence: start, signon, title, menu, New Game,
then about 60 s of walking, turning and shooting, then host pause/resume, master volume and F10
quit. It checks the events, frame pacing, AudioContext state, audio output (analyser tap) and that
the console has no errors. Browsers run headless and muted (Chromium `--mute-audio`, Firefox
`media.volume_scale=0.0`), with pointer lock stubbed out.
