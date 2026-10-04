# Third-party software

Witchspace's own code is under the [BSD 3-Clause License](LICENSE). The program and the web
version also contain the following, each under its own license. Their full license texts come
with every download.

| Component | Used for | License | Where |
|-----------|----------|---------|-------|
| [Nuked OPL3](https://github.com/nukeykt/Nuked-OPL3) 1.8 by Nuke.YKT, commit `765ec962e473aeb767e4cba74ffdc8f588ffbfe8`, unmodified | the AdLib's chip (an emulated OPL2), linked statically into the program and the web version | LGPL-2.1-or-later: `LICENSE.Nuked-OPL3.txt` (from `third_party/nuked-opl3/LICENSE`) | `third_party/nuked-opl3` |
| [SDL2](https://www.libsdl.org/) 2.32.10 by Sam Lantinga and contributors | window, input, sound; linked statically into the release binaries | zlib: `LICENSE.SDL2.txt` | built from source by CMake (`WS_VENDOR_SDL`) |
| [Emscripten](https://emscripten.org/)'s runtime and its SDL2 port | the web version's JavaScript and libraries | MIT (or UIUC) and zlib | linked by the Emscripten build |

## Nuked OPL3 and the LGPL

The LGPL lets you change the library and use your changed version with Witchspace. The
complete source of each release is public at its tag (for example
<https://github.com/rembish/witchspace/tree/v0.1.1>). To relink with your own version of Nuked
OPL3, replace `third_party/nuked-opl3/opl3.c` and `opl3.h` and build as the README says:

```sh
cmake -S . -B build && cmake --build build -j
```

For the web version, build with Emscripten (`make web`).

## Not included

Witchspace contains no part of Elite Plus. It reads the game's data from your copy, and the web
version fetches it in your browser. Elite © 1984 David Braben and Ian Bell; Elite Plus © 1991
Chris Sawyer, Realtime Software, and Bell & Braben.
