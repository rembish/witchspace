# Witchspace — a free reimplementation of Elite Plus (1991)

Elite Plus is the PC version of Elite by David Braben and Ian Bell, written in assembly for
DOS by Chris Sawyer (Realtime Software). Witchspace rebuilds that game, routine by routine,
as portable C, and runs it on today's computers and in the browser. Everything is there:
trading, combat, missions, docking, hyperspace, the AdLib music.

Witchspace contains none of the original game. It reads the game's data from your copy of
Elite Plus when it starts. (The name is the game's own word for the space between the stars.)

## See it

| | | |
|:-:|:-:|:-:|
| [<img src="https://github.com/rembish/witchspace/releases/download/v0.1.1/witchspace-title.gif" width="260" alt="The title, with the AdLib music">](https://github.com/rembish/witchspace/releases/download/v0.1.1/witchspace-title.mp4) | [<img src="https://github.com/rembish/witchspace/releases/download/v0.1.1/witchspace-launch.gif" width="260" alt="Launching from Lave">](https://github.com/rembish/witchspace/releases/download/v0.1.1/witchspace-launch.mp4) | [<img src="https://github.com/rembish/witchspace/releases/download/v0.1.1/witchspace-screens.gif" width="260" alt="The station's screens">](https://github.com/rembish/witchspace/releases/download/v0.1.1/witchspace-screens.mp4) |
| The title, with the AdLib music | Launching from Lave | The station's screens |
| [<img src="https://github.com/rembish/witchspace/releases/download/v0.1.1/witchspace-docking.gif" width="260" alt="The docking computer">](https://github.com/rembish/witchspace/releases/download/v0.1.1/witchspace-docking.mp4) | [<img src="https://github.com/rembish/witchspace/releases/download/v0.1.1/witchspace-hyperspace.gif" width="260" alt="A hyperspace jump to Zaonce">](https://github.com/rembish/witchspace/releases/download/v0.1.1/witchspace-hyperspace.mp4) | [<img src="https://github.com/rembish/witchspace/releases/download/v0.1.1/witchspace-combat.gif" width="260" alt="A pirate">](https://github.com/rembish/witchspace/releases/download/v0.1.1/witchspace-combat.mp4) |
| The docking computer | A hyperspace jump to Zaonce | A pirate (the start staged) |

Click a picture for the video with sound. They are made with `make clips` from scripted scenes
played through Witchspace (and kept with the [release](https://github.com/rembish/witchspace/releases),
not in this repository).

## Play in your browser

**[rembish.github.io/witchspace](https://rembish.github.io/witchspace/)**

The page asks where the game's three files should come from: your own copy, or the copy
preserved by the [Internet Archive](https://archive.org/details/b1022001), which it downloads
for you once you agree. The files stay in your browser for the next visit.

## Play on your computer

1. **Get the game's files.** You need three files from Elite Plus for DOS (V3.1, 1991):

   | File | What for |
   |------|----------|
   | `ELITE.EXE` | the game's data: tables, texts, ships |
   | `ELITE.GRF` | the pictures (without it the game shows placeholders) |
   | `ADBLUE.MID` | the AdLib's title music |

   Upper or lower case both work; the other files of the original are not needed. To check
   your copy (`sha256sum`), two copies of `ELITE.EXE` are known to work; they differ only in
   how the copy protection was patched out:

   ```
   6d7e748345f31e41cd8c90fc739a2c23fed32a277fc9b3e530f5d6c6bae86ef5  ELITE.EXE
   9c257f53909c8335cfd240c8be6d94128361fe8906ef781de774d3434a959668  ELITE.EXE  (the Internet Archive's)
   900fb787f5e7ed905e6931f1b522085a88a538b4e61396aed4ba329e05593b22  ELITE.GRF
   ee0d9a9d4b388f3af3ed6ec6aadd38a27823c1eee614af4bf378227e98364081  ADBLUE.MID
   ```

2. **Get Witchspace.** Download it for Linux, Windows or macOS from the
   [releases](https://github.com/rembish/witchspace/releases), or build it. To build you need
   a C compiler, CMake and SDL2:

   ```sh
   sudo apt install build-essential cmake libsdl2-dev     # Debian, Ubuntu
   brew install cmake sdl2                                  # macOS
   cmake -S . -B build && cmake --build build -j
   ```

   It is developed on Linux; the release's macOS and Windows builds are made by CI and not yet
   tried by hand.

3. **Run it.** Put `witchspace` (from the download, or `build/witchspace`) into the folder with
   the game's files and start it there. Or keep it anywhere and point it at them:

   ```sh
   ./witchspace --data /path/to/elite-plus
   ```

### Keys

As in the original's manual:

| Key | |
|-----|---|
| keypad <kbd>8</kbd> <kbd>2</kbd>, or <kbd>↑</kbd> <kbd>↓</kbd> | dive, climb |
| keypad <kbd>4</kbd> <kbd>6</kbd>, or <kbd>←</kbd> <kbd>→</kbd> | roll anticlockwise, clockwise |
| <kbd>&gt;</kbd> <kbd>&lt;</kbd> (the <kbd>.</kbd> and <kbd>,</kbd> keys) | faster, slower |
| <kbd>Space</kbd> | fire the laser |
| <kbd>F1</kbd>…<kbd>F12</kbd>, or <kbd>1</kbd>…<kbd>9</kbd> <kbd>0</kbd> <kbd>-</kbd> <kbd>=</kbd> | the icon with that number on the bar at the bottom: launch and docking, views, market, charts, equipment, missiles, ECM, jumps… |
| <kbd>Esc</kbd> | the menu: controls, sound, save and load, quit |
| <kbd>Alt</kbd>+<kbd>Enter</kbd> | full screen |

The keypad's keys steer while Num Lock is off; with it on they select icons, as the number
keys do. On the title screen the icons choose keyboard, joystick or mouse control (and let
you redefine the keys) before <kbd>Space</kbd> starts the game. A game controller works as
the joystick.

Everything else behaves as
it did in 1991: the original's manual, [preserved by the Internet Archive](https://archive.org/details/Elite_Plus_Manual)
([PDF](https://ia600708.us.archive.org/18/items/Elite_Plus_Manual/Elite_Plus_Manual.pdf)),
explains trading, combat, the ship's equipment and the missions.

### Options

| Option | |
|--------|---|
| `--data DIR` | where the game's files are (default: this program's folder, then the current one, then `original/`) |
| `--saves DIR` | where commanders are saved (default: beside the game's files, as the original did) |
| `--speaker` | the PC speaker instead of the AdLib |
| `--protection` | ask the original's copy-protection question (off by default) |
| `--version` | print the version |

## Status

The game is complete. Not planned: the Roland LAPC-1's music and effects and the 16-colour
EGA/VGA screen modes; the AdLib and the 256-colour MCGA mode cover them. Two places where
the original misbehaves are described in [re/FLIGHT.md](re/FLIGHT.md): a bug in the docking
computer, which Witchspace handles without the original's stray picture, and a case shown
never to happen in play.

## How it works

- **The core** (`core/`) is the game itself, rebuilt from the original's machine code: the
  galaxy, markets, ships and their AI, combat, the station screens, saving and loading, the
  speaker's sound and the AdLib's music driver. It is plain C99, deterministic, with no
  input or output of its own, and it keeps the original's quirks.
- **It is checked against the original.** The original runs in an x86 emulator; each
  reconstructed routine gets the same game state (taken from the running original, and
  randomly varied) and must leave the same memory that the core models, draw the same
  primitives and make the same sounds (the writes to the speaker and the sound chip), byte
  for byte. Larger tests boot the whole original and compare its start-up, play from the
  title into space and compare where both arrive, and compare the title screen frame by
  frame. These compare what the core says, not the final pixels; the frontend's renderer has
  checks of its own where its colours matter.
- **The core says what to draw, not how.** It emits polygons, lines, circles, pictures by
  number and text in the original's 320 × 200 coordinates; the frontend (`src/`, SDL2) draws
  them as the original did. Another renderer could draw the same stream its own way
  ([docs/core-interface.md](docs/core-interface.md)).

## For developers

| Path | Contents |
|------|----------|
| `core/` | The game: plain C99, deterministic, no I/O |
| `src/` | The SDL2 frontend: screen, timer, keyboard, mouse, controller, speaker and AdLib, files |
| `web/` | The browser page around the Emscripten build |
| `tests/` | Small programs the differential tests drive; `ep_flow`; `ep_datadump` |
| `re/` | Reverse engineering: notes, tools (`re/tools`), the emulator harness (`re/emu`) |
| `docs/` | How a frontend uses the core; testing the Windows version |
| `third_party/` | Nuked OPL3, the AdLib's chip |

Everyday commands are in the Makefile (`make help`):

```sh
make build        # the game and the test tools, warnings as errors
make web          # the browser version (needs Emscripten)
make check        # formatting, Python lint and types, the tests, the data check
make difftest     # every reconstructed routine against the original (about an hour)
make wintest      # from WSL: the Windows build's tests, run on Windows (docs/windows-testing.md)
make clips        # the videos and previews above, from your copy (needs ffmpeg)
```

The tools in `re/` are Python 3.12, typed and checked with ruff and mypy; `uv sync --extra dev`
makes their environment. They need your copy of the game in `original/` (git-ignored, never
distributed). CI checks everything that does not need the game: the build with gcc and
clang, the Windows build as released, the web build, formatting, and the Python tooling.
Testing the Windows version from WSL: [docs/windows-testing.md](docs/windows-testing.md).

The notes on the original:

- [re/NOTES.md](re/NOTES.md): the executable, start-up, galaxy, descriptions, pictures,
  copy protection, sound and the AdLib driver, the object update.
- [re/FLIGHT.md](re/FLIGHT.md): the flight loop, travel, docking, the original's bugs.
- [re/AI.md](re/AI.md), [re/SHIPS.md](re/SHIPS.md): the ship AI and the ships.

| Tool | Used for |
|------|----------|
| Python 3.12, uv, Unicorn, capstone | unpacking, control-flow recovery, the emulator harness, differential tests |
| Ghidra (headless) | decompiling `ELITE.EXE` (16-bit real mode); names in `re/ghidra/names.txt` |
| DOSBox-X, Xvfb, xdotool, ffmpeg | running the original for reference, screenshots |
| Emscripten | the web build |

## License

Witchspace is free software under the [BSD 3-Clause License](LICENSE). It includes Nuked
OPL3 by Nuke.YKT (`third_party/nuked-opl3`, LGPL-2.1 or later), in the release binaries
SDL2 (zlib), and in the web version Emscripten's runtime and musl (MIT); see [THIRD_PARTY.md](THIRD_PARTY.md), which also says how to relink with your own
Nuked OPL3.

Elite © 1984 David Braben and Ian Bell. Elite Plus © 1991 Chris Sawyer, Realtime Software,
and Bell & Braben. Elite is a trademark of Frontier Developments. Witchspace is an
unofficial reimplementation for preservation, not affiliated with any of them. The program
and its source contain no part of the original game; the clips above show it being played
(they are kept with the release, not in this repository).
