# Elite Plus (1991) — decompilation & multiplatform port (work in progress)

Elite Plus is the PC version of Elite by David Braben and Ian Bell, rewritten for DOS in
assembly (Firebird/MicroProse, 1991). This repo reverse-engineers `ELITE.EXE` with the same
approach as the BlockOut and Welltris ports: a deterministic C core reconstructed from the
machine code and checked against the original running in an emulator, with a fresh frontend
on top. No original game files are included.

**Status:** early reverse engineering. Done so far: unpacking, control-flow recovery,
an emulator harness, and the galaxy generator (all 8 × 256 systems match the original).

## Layout

| Path        | Contents |
|-------------|----------|
| `core/`     | Game logic reconstructed from `ELITE.EXE`: plain C99, no I/O, deterministic |
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
cmake -S . -B build && cmake --build build -j
python3 -m venv ~/tools/venv --system-site-packages && ~/tools/venv/bin/pip install unicorn capstone
~/tools/venv/bin/python re/tools/gen_tables.py      # regenerate core/ep_tables.c from original/
~/tools/venv/bin/python re/emu/galaxytest.py        # core vs the original's code, all systems
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

Elite © 1984 David Braben and Ian Bell. Elite Plus © 1991 Firebird / MicroProse. This is an
unofficial fan reimplementation for preservation; no original game files are distributed.
