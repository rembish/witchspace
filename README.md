# Elite Plus (1991) — decompilation & port

Elite Plus is the PC version of Elite by David Braben and Ian Bell, written in assembly for
DOS by Chris Sawyer (Realtime Software); the executable calls itself "Release: V3.1 August
1990". This project reconstructs the game from `ELITE.EXE` as portable C and runs it with a
new SDL2 frontend. No original game files are included.

## How it works

- **The core** (`core/`) is the game itself, rebuilt routine by routine from the machine
  code: the galaxy, markets, ships and their AI, combat, the station screens, saving and
  loading, sound. It is plain C99 with no I/O and fully deterministic, and it keeps the
  original's quirks and bugs.
- **Every part is checked against the original.** The original runs inside an emulator;
  each reconstructed routine gets the same game state (taken from the running original and
  fuzzed) and must end with the same memory, draw the same things and make the same sounds.
  Two further tests boot the whole original and compare the start-up and a game played from
  the title to the first frame in space, byte for byte.
- **The core says what to draw, not how.** It emits primitives (polygons, lines, circles,
  sprites by number, text strings with their colour codes) in the original's 320 × 200
  coordinates. The frontend in `src/` draws them as the original did; a modern renderer
  could draw the same stream differently (see
  [docs/core-interface.md](docs/core-interface.md)).

## Status

The game is complete and playable: title, new game, every station screen and dialogue,
flight (combat, the ship AI, scooping, docking, hyperspace and witchspace), the pause menu
and options, saving and loading commanders, the PC speaker's music and effects, the AdLib's
title music (its driver plays the original's `ADBLUE.MID` on an emulated OPL2), and keyboard,
joystick and mouse controls. The copy protection is reconstructed too, but off unless asked
for (`--protection`).

Not done yet: the AdLib's sound effects in flight (silent for now), and the 16-colour EGA/VGA
screen modes (the frontend shows the 256-colour MCGA mode). Two rare
edge cases still behave approximately; they are described in [re/FLIGHT.md](re/FLIGHT.md).

## Playing

You need your own copy of the DOS release; the frontend takes its pictures from
`ELITE.GRF` (without it the game runs with placeholders) and the AdLib's music from
`ADBLUE.MID`.

```sh
cmake -S . -B build && cmake --build build -j       # needs SDL2 (apt install libsdl2-dev)
./build/eliteplus --data original                  # the folder with your ELITE.GRF
```

Options: `--saves DIR` (where commanders are saved, default the current folder), `--speaker`
(the PC speaker instead of the AdLib), `--protection` (ask the novella question). Alt+Enter
toggles full screen. The keys are the original's; a game controller acts as the joystick.

## Layout

| Path     | Contents |
|----------|----------|
| `core/`  | The game, reconstructed from `ELITE.EXE`: plain C99, no I/O, deterministic |
| `src/`   | SDL2 frontend: screen, timer, keyboard/mouse/controller, PC speaker and AdLib, save files |
| `tests/` | Small programs the differential tests drive, and `ep_flow` (checks the emulator cannot do) |
| `re/`    | Reverse-engineering notes, unpacker, table generator, Ghidra scripts, emulator harness |
| `docs/`  | How a frontend uses the core |

## Reverse engineering

The tools in `re/` need your own copy of the game in `original/` (git-ignored, never
distributed); the expected files and their SHA-256 are listed in
[re/NOTES.md](re/NOTES.md). `ELITE.EXE` is EXEPACK-compressed, hand-written assembly with
some obfuscation (computed addresses and calls, `push`/`ret` jumps); `re/tools/unexepack.py`
restores a plain executable and `re/tools/explore.py` recovers its control flow.

The notes:

- [re/NOTES.md](re/NOTES.md) — the executable, start-up, galaxy, descriptions, pictures,
  copy protection, sound, and the object update.
- [re/FLIGHT.md](re/FLIGHT.md) — the flight loop, travel, docking, and the remaining
  approximations.
- [re/AI.md](re/AI.md), [re/SHIPS.md](re/SHIPS.md) — the ship AI and the ships.

```sh
python3 -m venv ~/tools/venv --system-site-packages && ~/tools/venv/bin/pip install unicorn capstone
~/tools/venv/bin/python re/tools/gen_tables.py      # regenerate core/ep_tables.c from original/
~/tools/venv/bin/python re/emu/corpus.py            # game states from the original (git-ignored)
~/tools/venv/bin/python re/emu/subtest.py frame --fuzz 4   # one routine vs the core (see ROUTINES)
~/tools/venv/bin/python re/emu/boottest.py          # start-up, original booted vs the core
~/tools/venv/bin/python re/emu/flowtest.py 5        # title to first flight frame, vs the core
~/tools/venv/bin/python re/emu/titletest.py 600     # title frames, vs the core
~/tools/venv/bin/python re/emu/play.py              # the original in the harness, in a window
ctest --test-dir build                              # core checks outside the emulator
re/ghidra/run.sh                                    # Ghidra project, decompiled C and listing
```

| Tool | Used for | Install |
|------|----------|---------|
| Python 3 + Unicorn, capstone | Unpacking, control-flow recovery, emulator harness, differential tests | `pip install unicorn capstone` in a venv |
| Ghidra (headless) | Decompiling `ELITE.EXE` (16-bit real mode); names in `re/ghidra/names.txt` | zip from GitHub into `~/tools/`, needs `openjdk-21-jdk` |
| DOSBox-X + Xvfb + xdotool + ffmpeg | Running the original as a reference, headless screenshots | `apt install dosbox-x xvfb xdotool ffmpeg` |
| gcc + CMake + SDL2 | Building | `apt install cmake libsdl2-dev` |

## Credits

Elite © 1984 David Braben and Ian Bell. Elite Plus © 1991 Chris Sawyer, Realtime Software,
and Bell & Braben. This is an unofficial fan reimplementation for preservation; no original
game files are distributed. Font: Exo 2 (SIL OFL). Text rendering: stb_truetype (public
domain). The AdLib's chip: [Nuked OPL3](https://github.com/nukeykt/Nuked-OPL3) by Nuke.YKT
(LGPL-2.1+, in `third_party/nuked-opl3`).
