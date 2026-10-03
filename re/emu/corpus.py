"""Collect game states from the running original, for subsystem tests.

usage: corpus.py [flights] [frames] [every] [outdir]

Each flight boots the original in machine.py (P, M, a word), starts the game, launches from
Lave and flies `frames` frames with random held keys (arrows, fire, faster/slower), saving the
whole memory (1 MB, zlib) at the start of every `every`-th flight frame (a040) as
OUTDIR/flight-<seed>-<frame>.bin. The images come from your own copy of the game: keep them
out of the repository (the default OUTDIR, re/emu/corpus/, is git-ignored).

Subsystem tests load an image into the emulator (code included, as patched for MCGA) and the
data segment into the core.
"""
import os
import random
import sys
import zlib

from machine import CS, Machine
from unicorn import UC_HOOK_CODE

HERE = os.path.dirname(os.path.abspath(__file__))
FLIGHT_TOP = 0xA040
# default keyboard bindings (ds:b251..b25d): faster '.', slower ',', up, down, left, right, fire
KEYS = [0x34, 0x33, 0x48, 0x50, 0x4B, 0x4D, 0x39]


def save(m, path):
    with open(path, "wb") as f:
        f.write(zlib.compress(bytes(m.mu.mem_read(0, 0x100000)), 6))


def load(path):
    with open(path, "rb") as f:
        return zlib.decompress(f.read())


def fly(seed, frames, every, outdir):
    rng = random.Random(seed)
    m = Machine()
    m.boot()
    m.press(0x19, 0x32, 0x1E, 0x1C)
    title, flight = [], []
    m.mu.hook_add(UC_HOOK_CODE, lambda mu, a, s, u: title.append(1), begin=CS * 16 + 0x9F21, end=CS * 16 + 0x9F21)
    m.mu.hook_add(UC_HOOK_CODE, lambda mu, a, s, u: flight.append(1), begin=CS * 16 + FLIGHT_TOP,
                  end=CS * 16 + FLIGHT_TOP)
    m.run(stop=lambda m: len(title) >= 5)
    m.press(0x39)
    m.run(stop=lambda m: m.ticks > 2500 and not m.keys and m.down is None, idle_ticks=True)
    m.press(0x3B)
    held = set()
    saved = []
    last = [0]

    def stop(m):
        n = len(flight)
        if n == last[0]:
            return False
        last[0] = n
        if n % every == 0:
            path = os.path.join(outdir, f"flight-{seed}-{n:05d}.bin")
            save(m, path)
            saved.append(path)
        # change held keys now and then (one keyboard interrupt per pause)
        if not any(e != 8 for e in m.pending) and rng.random() < 0.3:
            k = rng.choice(KEYS)
            if k in held:
                held.discard(k)
                m.scancode_event(k | 0x80)
            else:
                held.add(k)
                m.scancode_event(k)
        return n >= frames
    m.run(stop=stop, idle_ticks=True)
    return saved


def main():
    flights = int(sys.argv[1]) if len(sys.argv) > 1 else 2
    frames = int(sys.argv[2]) if len(sys.argv) > 2 else 400
    every = int(sys.argv[3]) if len(sys.argv) > 3 else 10
    outdir = sys.argv[4] if len(sys.argv) > 4 else os.path.join(HERE, "corpus")
    os.makedirs(outdir, exist_ok=True)
    total = 0
    for seed in range(1, flights + 1):
        total += len(fly(seed, frames, every, outdir))
    print(f"saved {total} states in {outdir}")


if __name__ == "__main__":
    main()
