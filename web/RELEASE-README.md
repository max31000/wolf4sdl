# wolf3d-web

Wolfenstein 3D as WebAssembly. This is the Wolf4SDL source port, built with Emscripten 6.0.10.
Source: https://github.com/max31000/wolf4sdl (branch `bliss-web`), host contract in `web/WEB.md`.

- `wolf3d.js`: ES module. The default export is the factory `createWolf3d(moduleArgs) -> Promise<Module>`.
- `wolf3d.wasm`

The package contains **no game data**. The host writes the Wolfenstein 3D data files (for example the
freely redistributable shareware v1.4 `*.WL1`) into `/data` from `preRun`.

## Licences

- Wolfenstein 3D source code (id Software): licensed at your choice under `license-id.txt`
  (Limited Use Software License, non-commercial) or `license-gpl.txt`.
- OPL2 emulator (MAME `fmopl.c`): `license-mame.txt`, which allows non-commercial use only and
  requires the complete source of modified versions (see the repository above).

As a whole, this build may only be used non-commercially.
