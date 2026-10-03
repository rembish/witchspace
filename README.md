# Elite Plus (1991) — decompilation & multiplatform port (work in progress)

Elite Plus is the PC version of Elite by David Braben and Ian Bell, written for DOS in
assembly by Chris Sawyer (Realtime Software); the executable says "Release: V3.1 August
1990". This repo reverse-engineers `ELITE.EXE` with the same approach as the BlockOut and
Welltris ports: a deterministic C core reconstructed from the machine code and checked
against the original running in an emulator, with a fresh frontend on top. No original game
files are included.

**Status:** the game logic is reconstructed and checked against the original: galaxy and
system generation, descriptions, markets, the commander block (save files), ship models and
rendering, the title (setup, intro and credits, frame by frame), starting, saving and loading
a game, every station screen (status, market, equipment, charts, data, the arrival dialogues
and missions), the pause menu and options, and the whole flight loop (object update with sun,
planet and scanner, controls, laser, messages, energy, dashboard, star dust, combat, the ship
AI, docking, launching, hyperspace and witchspace). Each part is difftested on game states
taken from the running original (`re/emu/corpus.py`, `re/emu/subtest.py`); `ep_flow` covers
what the emulator cannot (the timer). Left: the key, joystick and mouse set-up screens, the
copy protection (to be opt-in, off by default), and the frontend, which so far shows the
title's ship only.

## Layout

| Path        | Contents |
|-------------|----------|
| `core/`     | Game logic reconstructed from `ELITE.EXE`: plain C99, no I/O, deterministic |
| `src/`      | SDL2 frontend (so far the title screen) |
| `tests/`    | Tools the differential tests drive (`galdump`) |
| `re/`       | Notes, unpacker, explorer, Ghidra scripts, emulator harness, DOSBox-X runner |

## The original

The tools in `re/` need your own copy of the DOS release in `original/` (git-ignored, never
distributed). The files they were written against are listed with their SHA-256 in
[`re/NOTES.md`](re/NOTES.md). `ELITE.EXE` is EXEPACK-compressed hand-written assembly with
some obfuscation (computed addresses and calls, `push`/`ret` jumps); `re/tools/unexepack.py`
restores a plain executable and `re/tools/explore.py` recovers its control flow.

## Building and testing

```sh
cmake -S . -B build && cmake --build build -j       # needs SDL2 (apt install libsdl2-dev)
./build/eliteplus
python3 -m venv ~/tools/venv --system-site-packages && ~/tools/venv/bin/pip install unicorn capstone
~/tools/venv/bin/python re/tools/gen_tables.py      # regenerate core/ep_tables.c from original/
~/tools/venv/bin/python re/emu/galaxytest.py        # core vs the original's code, all systems
~/tools/venv/bin/python re/emu/titletest.py 6000    # whole original booted headless vs core
~/tools/venv/bin/python re/emu/play.py              # the original in the harness, in a window
~/tools/venv/bin/python re/emu/corpus.py            # flight states from the original (git-ignored)
~/tools/venv/bin/python re/emu/subtest.py controls --fuzz 10   # one subsystem vs the core
ctest --test-dir build                               # core checks outside the emulator
re/ghidra/run.sh                                     # Ghidra project, decompiled C and listing
```

## Tooling

| Tool | Used for | Install |
|------|----------|---------|
| Ghidra (headless) | Decompiling `ELITE.EXE` (16-bit real mode); names in `re/ghidra/names.txt` | zip from GitHub into `~/tools/`, needs `openjdk-21-jdk` |
| Python 3 + capstone, Unicorn | Unpacking, control-flow recovery, emulator harness and differential tests | `pip install unicorn capstone` in a venv |
| DOSBox-X + Xvfb + xdotool + ffmpeg | Running the original as a reference, headless screenshots | `apt install dosbox-x xvfb xdotool ffmpeg` |
| gcc + CMake | Native build | `apt install cmake` |

## Credits

Elite © 1984 David Braben and Ian Bell. Elite Plus © 1991 Chris Sawyer, Realtime Software,
and Bell & Braben. This is an unofficial fan reimplementation for preservation; no original
game files are distributed. Font: Exo 2 (SIL OFL). Text rendering: stb_truetype (public
domain).
