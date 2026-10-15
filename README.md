# wolf4sdl (mirror + web build)

This is a plain git mirror of **Wolf4SDL**, the SDL2 source port of id Software's Wolfenstein 3D.
The upstream is the maintained repository
[bitbucket.org/ks-presto/wolf4sdl](https://bitbucket.org/ks-presto/wolf4sdl), which continues
Moritz "Ripper" Kroll's Wolf4SDL. The former GitHub home, KS-Presto/Wolf4SDL, is archived and
points to Bitbucket.

- `master`: upstream, unchanged.
- `bliss-web`: adds an Emscripten / WebAssembly build. The original game loop runs in the browser
  on a fiber; see [`web/WEB.md`](web/WEB.md). It is used by Bliss (retro games in the browser) to
  play the Wolfenstein 3D shareware episode.

The upstream documentation is in [README.TXT](README.TXT).

## Licence

Wolf4SDL is distributed under the licences listed in README.TXT:

- **Original Wolfenstein 3D source code** (id Software, 1995): licensed at your choice under
  [license-id.txt](license-id.txt) or [license-gpl.txt](license-gpl.txt) (GPL v2).
  - license-id.txt is id's *Limited Use Software License Agreement*. It allows **non-commercial use
    only** and gives no rights to the "Wolfenstein 3D" trademark or to the game data.
- **OPL2 emulator** `mame/fmopl.c`: [license-mame.txt](license-mame.txt). It allows
  **non-commercial use only**, and redistributed modified builds must come with their complete source.
  With `USE_GPL`, the GPL DOSBox OPL emulator is used instead.

The web build uses the MAME OPL emulator, so it is **for non-commercial use only**. That fits
Bliss, a free personal site. The full source of the web build is in this repository.

No game data is included. The Wolfenstein 3D shareware episode (v1.4, `*.WL1`) may be
redistributed unaltered and for free, as stated in Apogee's shareware terms. The registered episodes (`*.WL6`) are
commercial.
