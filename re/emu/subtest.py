"""Subsystem test on game states from the running original (corpus.py).

usage: subtest.py NAME [corpus glob] [path/to/ep_subsys] [--fuzz N]

For every state: the original's routine runs in the emulator on the whole memory image, the
core's on the data segment loaded through tests/statemap.c. Compares the data-segment bytes
the core models and the primitives drawn; lists the bytes the original changed that the core
does not model yet (what to reconstruct next). With --fuzz N, each state is also tried N
times with random values in the fields the routine reads (FUZZ), to reach every branch.
"""
import collections
import random
import glob
import os
import subprocess
import sys
import tempfile

from corpus import load
from eliteemu import CS, DS, Elite
from unicorn import UC_HOOK_CODE

HERE = os.path.dirname(os.path.abspath(__file__))
ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
FUZZ_N = int(sys.argv[sys.argv.index("--fuzz") + 1]) if "--fuzz" in sys.argv else 0
if FUZZ_N:
    ARGS.remove(str(FUZZ_N))
NAME = ARGS[0]
PATTERN = ARGS[1] if len(ARGS) > 1 else os.path.join(HERE, "corpus", "*.bin")
TOOL = ARGS[2] if len(ARGS) > 2 else os.path.join(HERE, "..", "..", "build", "ep_subsys")

# Scratch space the original reuses within a routine (not state): INT 0 resume address, draw
# parameters, matrices and model temporaries, rotation temporary; and sound state (the core
# reports sounds as events).
SCRATCH = [(0x54CC, 0x54E1), (0x6405, 0x6405), (0x45E4, 0x45E5), (0x92D4, 0x92DE), (0x92FA, 0x92FA), (0xA500, 0xA7FF), (0x8D00, 0x8D4E), (0x92F9, 0x92F9), (0xA3A0, 0xA4FF), (0xACA8, 0xACB3), (0xAD2B, 0xAD2D),
           (0x031D, 0x031E), (0x03F1, 0x03F2), (0x0980, 0x0990),
           (0x01F8, 0x01F9), (0x1074, 0x10BB), (0x10BD, 0x10C9), (0x28D0, 0x28E5), (0x2B66, 0x2BF5), (0x2CB1, 0x2CB2),
           (0x76D6, 0x76D7), (0x45E6, 0x45FF), (0x4FE0, 0x4FE0)]

EV_SOUND, EV_SURFACE, EV_UNPORTED = 1, 2, 3

# Sound routines that wrap 4c98 (some skip the sound depending on audio state, which the core
# does not keep): logged with the sound number they pass.
SOUND_WRAPPERS = {
    0x4D9F: lambda r: 0x0B, 0x4DC9: lambda r: 0x14 + (r["ax"] & 3), 0x4DEB: lambda r: 0x0F,
    0x4DF5: lambda r: 0x12, 0x4DFF: lambda r: 0x13, 0x4EA7: lambda r: 2, 0x4EAC: lambda r: 5,
    0x4EB1: lambda r: 6, 0x4DA4: lambda r: 0x17, 0x4E98: lambda r: 0x0C, 0x4E9D: lambda r: 0x19,
    0x4EA2: lambda r: 1, 0x4EB6: lambda r: 0x0D, 0x4EBB: lambda r: 0x0E, 0x4EC0: lambda r: 0x18,
    0x4EC5: lambda r: 0x1A, 0x4ECA: lambda r: 0x1B,
}
# Routines the core does not reconstruct yet: stubbed and logged on both sides.
UNPORTED = [0x7EA8, 0x83F5, 0x8352, 0x84E1, 0x84FC, 0x8645, 0x873B]
UNPORTED_FOR = {"laser_hits": [], "explode": [0x83F5, 0x8352, 0x84E1, 0x84FC, 0x8645, 0x873B]}

# name -> (address, registers, {exit address: line printed}) - exits are where a routine
# leaves without returning (the core reports them as a result line instead)
ROUTINES = {
    "update_objects": (0x4154, {}, {}),
    "message": (0x702A, {}, {}),
    "fuel_leak": (0x75D5, {}, {}),
    "energy_drain": (0xA52F, {}, {}),
    "laser": (0xA183, {}, {}),
    "tunnel": (0xA0CC, {}, {0xA0E9: "end 1"}),
    "controls": (0xA63D, {}, {}),
    "laser_hits": (0xAC52, {}, {}),
    "collisions": (0x66D6, {}, {}),
    "enemy_fire": (0xAE50, {}, {}),
    "ai": (0x77E0, {}, {}),
    "dashboard": (0x549F, {}, {}),
    "explode": (0x7EA8, {}, {}),
    "buy": (0x96DE, {}, {}),
    "sell": (0x9781, {}, {}),
    "equip": (0x932F, {}, {0x94BF: "choose mount"}),
}


def ship_in_sights(img, rng):
    """A random ship in a random slot, in view near the crosshair."""
    slot = rng.randint(2, 19)
    base = DS * 16 + 0x76DE + 0x40 * slot
    t = rng.choice([0, 1, 5, 7, 22, 28, rng.randrange(30), rng.randrange(30)])
    img[base] = (t << 1) | 0x81 | (0x40 if rng.random() < 0.5 else 0)
    z = rng.choice([100, 200, 600, 1500, 4000, 12000])
    lim = 2 * z // 256 + 40
    for k, v in enumerate((rng.randint(-lim, lim), rng.randint(-lim, lim), z)):
        img[base + 0x10 + 2 * k:base + 0x12 + 2 * k] = (v & 0xFFFF).to_bytes(2, "little")
    img[base + 0x1E] = rng.choice([0, 0, 4, 0x20, 0x60, rng.getrandbits(8)])
    img[base + 0x2B] = rng.choice([0, 1, 2, 3, 4, 5, rng.getrandbits(8)])
    img[base + 0x30] = rng.choice([0, 0xFD, rng.getrandbits(8)])
    img[base + 0x31] = rng.choice([0, 0xFF, rng.getrandbits(8)])
    img[base + 0x25] = rng.choice([0, 1, 2])
    img[base + 0x3A:base + 0x3C] = rng.choice([0, 1, 0x1234]).to_bytes(2, "little")
    if rng.random() < 0.3:
        img[DS * 16 + 0xB0E1:DS * 16 + 0xB0E3] = (0x76DE + 0x40 * slot).to_bytes(2, "little")


def something_close(img, rng):
    """A ship or a station near the player, possibly lined up for docking."""
    slot = rng.randint(0, 19)
    base = DS * 16 + 0x76DE + 0x40 * slot
    t = rng.choice([0, 1, 0, 1, rng.randrange(30)])
    img[base] = (t << 1) | 1 | rng.choice([0, 0x80, 0x80, 0xC0])
    r = 275 if t <= 1 else 100
    for k in range(3):
        v = rng.choice([rng.randint(-r + 1, r - 1), rng.randint(-89, 89), r, -r, rng.randint(-400, 400)])
        img[base + 4 + 2 * k:base + 6 + 2 * k] = (v & 0xFFFF).to_bytes(2, "little")
        img[base + 1 + k] = 0xFF if v < 0 else 0
    img[base + 0x0C] = rng.getrandbits(8)
    roll = rng.randrange(2048)
    img[base + 0x0E:base + 0x10] = roll.to_bytes(2, "little")
    img[base + 0x1E] = rng.choice([0, 0, 1, rng.getrandbits(8)])
    img[base + 0x31] = rng.choice([0, 0xFF, rng.getrandbits(8)])
    near = lambda c: (c + rng.randint(-260, 260)) & 0x7FF
    a0 = near(rng.choice([0, 0x400]))
    angles = [a0, near(rng.choice([0, 0x400])), near(rng.choice([roll, roll + 0x400]))]
    for k, a in enumerate(angles):
        img[DS * 16 + 0x76D8 + 2 * k:DS * 16 + 0x76DA + 2 * k] = a.to_bytes(2, "little")


def dashboard_world(img, rng):
    """Gauges around the condition thresholds and the station around the safe-zone radius."""
    if rng.random() < 0.25:
        return
    w8 = lambda a, v: img.__setitem__(DS * 16 + a, v & 0xFF)
    near = lambda c, d: rng.randint(c - d, c + d)
    level = rng.randrange(3)
    energy = near((0x100, 0x200, 0x300)[level], 2)
    img[DS * 16 + 0x54C8:DS * 16 + 0x54CA] = max(0, min(energy, 0x3FF)).to_bytes(2, "little")
    good = [rng.choice([0, 1, rng.randrange(0x7E)]) for _ in range(5)]
    w8(0x54C1, rng.choice([near((0xE0, 0xC0, 0x80)[level], 1), rng.randrange(0x7F)]))
    w8(0x54C3, rng.choice([near((0x20, 0x28, 0x80)[level], 1), 0xFE]))
    w8(0x54C4, rng.choice([near(0x80, 1), 0, 0xFF, 0x80 + good[0]]))
    w8(0x54C5, rng.choice([near(0x80, 1), 0, 0xFF, 0x80 + good[1]]))
    base = DS * 16 + 0x76DE + 0x80
    img[base] = (rng.choice([0, 1, 2]) << 1) | rng.choice([1, 1, 0x81, 0])
    big = rng.random() < 0.2
    for k in range(3):
        v = rng.choice([near(0x32C8, 40), -near(0x32C8, 40), rng.randint(-0x3000, 0x3000), rng.randint(-200, 200),
                        rng.randint(-0x7FFF, 0x7FFF)]) if k == rng.randrange(3) else rng.randint(-0x1000, 0x1000)
        img[base + 4 + 2 * k:base + 6 + 2 * k] = (v & 0xFFFF).to_bytes(2, "little")
        img[base + 1 + k] = rng.getrandbits(8) if big else (0xFF if v < 0 else 0)


def docking_approach(img, rng):
    """The station just ahead, nearly lined up: docking, bouncing off or crashing."""
    slot = rng.choice([1, 2, 3])
    base = DS * 16 + 0x76DE + 0x40 * slot
    img[base] = (rng.choice([0, 1]) << 1) | 0x81
    for k, v in enumerate((rng.randint(-130, 130), rng.randint(-130, 130), rng.randint(-274, 274))):
        img[base + 4 + 2 * k:base + 6 + 2 * k] = (v & 0xFFFF).to_bytes(2, "little")
        img[base + 1 + k] = 0xFF if v < 0 else 0
    img[base + 0x0C] = rng.choice([0, 0, 1])
    img[base + 0x1E] = rng.choice([0, 0, 0, 1])
    roll = rng.randrange(2048)
    img[base + 0x0E:base + 0x10] = roll.to_bytes(2, "little")
    near = lambda c: (c + rng.randint(-120, 120)) & 0x7FF
    first = rng.choice([0, 0x400])
    angles = [near(first), near(0x400 - first), near(rng.choice([roll, roll + 0x400]))]
    for k, a in enumerate(angles):
        img[DS * 16 + 0x76D8 + 2 * k:DS * 16 + 0x76DA + 2 * k] = a.to_bytes(2, "little")


def trading(img, rng):
    """A random commander and market: cargo, cash, equipment, the docked system, a row."""
    d = DS * 16
    for k in range(17):
        img[d + 0x8379 + 2 * k] = rng.choice([0, 0, 1, 5, 0xF9, 0xFA, 0xFF, rng.getrandbits(8)])
        img[d + 0x837A + 2 * k] = rng.choice([0, 1, 7, 0xF9, 0xFA, 0xFF, rng.getrandbits(8)])
    img[d + 0x839C] = rng.choice([0, 5, 0x13, 0x14, 0x22, 0x23, 0x30])
    cash = rng.choice([0, 1, 100, 1000, 30000, 0x10000, 0x7FFFF, rng.getrandbits(20)])
    img[d + 0x8367:d + 0x836B] = cash.to_bytes(4, "little")
    for k in range(14):
        img[d + 0x8356 + k] = rng.choice([0, 0, 0, 1, rng.getrandbits(8)])
    img[d + 0x8356] = rng.choice([0, 0x10, 0xFA, 0xFB, 0xFF, rng.getrandbits(8)])
    img[d + 0x8357] = rng.choice([0, 3, 4, 5])
    img[d + 0x8358] = rng.choice([0, 1])
    img[d + 0x835C] = rng.choice([0, 1])
    img[d + 0x8365] = rng.choice([0, 1, 0x0F, 0x0E, 0x07, rng.getrandbits(4)])
    img[d + 0x8366] = rng.getrandbits(8)
    img[d + 0x836B] = rng.choice([0, 0x20, 0xFF])
    img[d + 0x832C:d + 0x832F] = bytes([rng.randrange(8), rng.randrange(8), rng.randrange(13)])
    img[d + 0x83A0] = rng.choice([0, 0, 1, 4])
    img[d + 0xAD2B] = rng.choice([rng.randrange(17), rng.randrange(14), 0, 0, 1, 4, 5, 12, 13])
    img[d + 0x839D] = 1  # on the market screen the table is already drawn
    if NAME == "equip":  # only rows the station lists (min tech <= tech + 1)
        row = img[d + 0xAD2B] % 14
        img[d + 0xAD2B] = row
        min_tech = [1, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 10][row]
        img[d + 0x832E] = rng.randint(min_tech - 1, 12)


def exploding(img, rng):
    """A random ship to explode (slot index in the spare byte ds:ff00)."""
    slot = rng.randint(2, 19)
    base = DS * 16 + 0x76DE + 0x40 * slot
    img[DS * 16 + 0xFF00] = slot
    img[base] = (rng.randrange(30) << 1) | 1 | rng.choice([0, 0x80, 0x80])
    img[base + 0x2D] = rng.choice([0, 1, 3, 7, rng.randrange(12)])
    img[base + 0x2C] = rng.choice([0, 1, 3, 7, rng.getrandbits(8)])
    img[base + 0x1E] = rng.choice([0, 0x20, rng.getrandbits(8)])
    img[base + 0x31] = rng.choice([0, 0xC8, 0xFF])
    for k in range(3):
        img[base + 0x19 + k] = rng.getrandbits(8)
    for i in range(3, 36):
        if rng.random() < 0.5:
            img[DS * 16 + 0x76DE + 0x40 * i] |= 1


def ai_world(img, rng):
    """Random counts per class, missions, witchspace, government."""
    d = DS * 16
    for i in range(3, 36):
        b = d + 0x76DE + 0x40 * i
        if rng.random() < 0.4:
            img[b] = (rng.randrange(30) << 1) | 1
            img[b + 0x33] = rng.choice([0, 3, 4, 5, 6, 7, 7])
            img[b + 0x2E] = rng.choice([1, 2, 30])
        elif rng.random() < 0.5:
            img[b] &= 0xFE
    img[d + 0x832C] = rng.randrange(8)
    img[d + 0x76B6] = rng.randrange(8)
    img[d + 0x888F:d + 0x8891] = (4 * rng.randrange(8)).to_bytes(2, "little")
    img[d + 0x8362] = rng.choice([0, 1])


def attacker(img, rng):
    slot = rng.randint(2, 19)
    base = DS * 16 + 0x76DE + 0x40 * slot
    img[base] = (rng.randrange(30) << 1) | 1 | rng.choice([0, 0x80])
    for k, v in enumerate((rng.randint(-3000, 3000), rng.randint(-3000, 3000), rng.choice([0, 100, 500, 4000]))):
        img[base + 0x10 + 2 * k:base + 0x12 + 2 * k] = (v & 0xFFFF).to_bytes(2, "little")
    img[DS * 16 + 0x7610:DS * 16 + 0x7612] = (0x76DE + 0x40 * slot).to_bytes(2, "little")


# name -> data-segment fields (address, size) given random values when fuzzing; a value list
# picks from interesting values instead of all
FUZZ = {
    "message": [(0x8058, [0x81FE, 0x8212, 0x802C]), (0x805A, 1), (0x805B, [0, 0xFF]), (0x8056, [0x81FE, 0x802C]),
                (0xB0DE, [0, 0x200, 0x400, 0x600, 0x100]), (0xB126, [0, 0, 1]), (0x81F4, [0, 0, 1, 5]),
                (0x81F5, 1), (0x81F2, 2), (0x8892, [0, 1, 2]), (0x54C3, [0x10, 0x31, 0x32, 0xFE]),
                (0x54C1, [0x10, 0xE0, 0xE1]), (0x54C8, [0xFF, 0x100, 0x3FF])],
    "fuel_leak": [(0x83A5, [0, 0, 1, 2, 9]), (0x83A6, [0, 1, 2, 0x33]), (0x8356, [0, 3, 5, 6, 0x46, 0xFF])],
    "energy_drain": [(0xB139, [0, 1, 2]), (0x54C8, [0, 1, 2, 3, 0x3FF])],
    "laser": [(0xB126, [0, 0, 1]), (0xAE23, [0, 0, 1]), (0x020D + 0x39, [0, 0x80]), (0x8365, 1), (0x8366, 1),
              (0xB0DE, [0, 0x200, 0x400, 0x600]), (0x54C2, [0, 0xEB, 0xEF, 0xF0, 0xFC]), (0xB3D3, [0, 0, 1]),
              (0xB125, [0, 1])],
    "tunnel": [(0xAE23, [0, 1, 2, 30]), (0x83B5, [0, 0, 5])],
    "laser_hits": [(0, ship_in_sights), (0, ship_in_sights), (0xB0E4, [1, 1, 1, 0]), (0xB0E3, [0, 1, 2, 3]),
                   (0x83AA, [0, 0, 1]), (0x83A0, [0, 4, 6]), (0x83A2, [0, 1, 2, 3]), (0x836B, [0, 0xD7, 0xD8, 0xFF]),
                   (0x7680, [0, 1]), (0x83A4, [0, 0, 1, 5, 6, 0x23, 0x24, 0x40]), (0x805A, [0, 0, 3]),
                   (0x54CA, [0, 2]), (0xAF14, [0, 1]), (0x54B9, [0, 1]), (0x54BA, [0, 2]), (0x54BB, [0, 1, 2, 3])],
    "explode": [(0, exploding), (0xAE22, [0, 0, 1]), (0x83A9, [0, 0, 1, 2]), (0x7FDF, [16])],
    "dashboard": [(0x54C8, [0, 0xFF, 0x100, 0x1FF, 0x200, 0x2FF, 0x300, 0x3FE, 0x3FF]), (0x54C1, [0, 0x7F, 0x80, 0xBF, 0xC0, 0xDF, 0xE0]),
                  (0x54C3, [0x1F, 0x20, 0x27, 0x28, 0x7F, 0x80, 0xFF]), (0x54C4, [0, 1, 0x7F, 0x80, 0xFF]),
                  (0x54C5, [0, 1, 0x7F, 0x80, 0xFF]), (0x54C2, [0, 1, 2, 0x80]), (0x835F, [0, 1]), (0xB126, [0, 0, 1]),
                  (0xAE23, [0, 0, 1]), (0x54C0, [0, 1]), (0, something_close), (0, dashboard_world)],
    "ai": [(0, ai_world), (0xB0DD, [0, 0, 1]), (0x83A4, [0, 0, 0, 5]), (0x83A9, [0, 0, 0, 3]), (0x83B3, [0, 1]),
           (0x83A7, [0, 0, 1]), (0x83AA, [0, 0, 1]), (0x83B1, [0, 1]), (0x83AB, [0, 0, 1]), (0x7680, [0, 1]),
           (0x83A0, [0, 4, 5, 6]), (0x83A2, [0, 2, 3]), (0x83B0, [0, 1]), (0x839E, [0, 5, 0x0D]),
           (0x839F, [0, 1]), (0x83A3, [0, 7]), (0x8329, [7, 7, 3])],
    "buy": [(0, trading)], "sell": [(0, trading)], "equip": [(0, trading)],
    "collisions": [(0, something_close), (0, docking_approach), (0x83AA, [0, 0, 1]), (0xAE23, [0, 0, 0, 1]),
                   (0x54C4, [0, 10, 0x80, 0xFF]), (0x54C8, [0, 0x10, 0x200, 0x3FF])],
    "enemy_fire": [(0, attacker), (0x7612, [1, 1, 1, 0]), (0x7681, [0, 0x80]), (0x54C4, [0, 5, 14, 15, 16, 0xFF]),
                   (0x54C5, [0, 5, 14, 15, 16, 0xFF]), (0x54C8, [0, 1, 0x10, 0x3FF])],
    "controls": [(0x020D + 0x48, [0, 0x80, 0x80]), (0x020D + 0x50, [0, 0x80, 0x80]),
                 (0x020D + 0x4B, [0, 0x80, 0x80]), (0x020D + 0x4D, [0, 0x80, 0x80]),
                 (0x020D + 0x34, [0, 0x80, 0x80]), (0x020D + 0x33, [0, 0x80, 0x80]),
                 (0x09D1, [0, 1, 2, 0x16, 0x17, 0xE9, 0xEA, 0xFF, 0x0B, 0xF5]),
                 (0x09D2, [0, 1, 2, 0x16, 0x17, 0xE9, 0xEA, 0xFF, 0x0B, 0xF5]),
                 (0x09D3, [0, 1, 0xFF]), (0x09D4, [0, 1, 0xFF]),
                 (0x09D5, [0, 1, 0x16, 0x17, 0xE9, 0xEA, 0xFF, 0x0C]), (0x09D6, [0, 1, 0x16, 0x17, 0xE9, 0xEA, 0xFF, 0x0C]), (0xB134, [0, 1]), (0xB135, [0, 1]), (0xB136, [0, 0, 1]),
                 (0xB137, [0, 0, 1]), (0xAF56, [4, 8, 0x2C, 0x30]), (0xAF58, [0, 1]), (0xB0DD, [0, 0, 1]),
                 (0x76D8, 2), (0x76DA, 2), (0x76DC, 2), (0xAE23, [0, 0, 0, 3]), (0xB126, [0, 0, 0, 0x3C, 5])],
}


def fuzz(image, rng):
    img = bytearray(image)
    for addr, spec in FUZZ.get(NAME, []):
        if callable(spec):
            spec(img, rng)
            continue
        if isinstance(spec, list):
            v, n = rng.choice(spec), 2 if max(spec) > 0xFF else 1
        else:
            n = spec
            v = rng.getrandbits(8 * n)
        for k in range(n):
            img[DS * 16 + addr + k] = (v >> (8 * k)) & 0xFF
    return bytes(img)


def s16(v):
    return v - 65536 if v >= 32768 else v


def run_original(image, addr, regs, exits=None):
    e = Elite()
    exits = exits or {}
    left = []
    for a, line in exits.items():
        e.mu.hook_add(UC_HOOK_CODE, lambda mu, ad, sz, u, line=line: (left.append(line), mu.emu_stop()),
                      begin=CS * 16 + a, end=CS * 16 + a)
    e.mu.mem_write(0, image)
    prims = []

    def prim(kind, pts):
        prims.append(f"{kind}:{e.r8(0x10A2)}" + "".join(f",{s16(v)}" for v in pts))

    e.hook(0x172C, lambda e, r: prim(0, [r["cx"], r["dx"], r["ax"], r["bx"], r["si"], r["bp"]]))
    e.hook(0x1A7A, lambda e, r: prim(2, [r["ax"], r["bx"], r["cx"], r["dx"], e.r16(0x10B0),
                                         e.r16(0x10B4), r["si"], r["bp"]]))
    e.hook(0x261B, lambda e, r: prim(4, [r["cx"], r["dx"], r["ax"], r["bx"]]))

    spans = []

    def span(e, r):
        if s16(r["cx"]) >= 0:
            spans.append(f"span {s16(r['bx'])},{s16(r['cx']) or 1},{(s16(r['di']) - 0x168) // 0x28}")
    e.hook(0x16DA, span)
    e.hook(0x1514, span)
    sounds = []
    e.hook(0x4E1A, lambda e, r: sounds.append(f"event {EV_SURFACE}:{r['ax']}"))
    e.hook(0x4C98, lambda e, r: sounds.append(f"event {EV_SOUND}:{r['ax'] & 0xFF}"))
    for wrapper, snd in SOUND_WRAPPERS.items():
        e.hook(wrapper, lambda e, r, snd=snd: sounds.append(f"event {EV_SOUND}:{snd(r)}"))
    for stub in UNPORTED_FOR.get(NAME, UNPORTED):
        e.hook(stub, lambda e, r, stub=stub: sounds.append(f"event {EV_UNPORTED}:{stub}"))
    e.hook(0x487E, lambda e, r: None)  # compass: drawing only
    if NAME in ("buy", "sell"):
        e.call(0x97D8)
    elif NAME == "equip":
        e.call(0x9161)
    e.hook(0x2FC0, lambda e, r: left.append(f"result {r['si']}"))
    e.hook(0x2576, lambda e, r: prim(6, [r["cx"], r["ax"], r["dx"], r["bx"]]))  # clipped line
    try:
        if NAME == "explode":
            regs = dict(regs, di=0x76DE + 0x40 * image[DS * 16 + 0xFF00] % (0x40 * 36))
        e.call(addr, **regs)
    except RuntimeError:
        if not left:
            raise
    if NAME == "tunnel" and not left:
        left.append("end 0")
    if NAME in ("buy", "sell", "equip", "dashboard"):  # the screens' drawing is the frontend's
        prims, spans = [], []
    return bytes(e.mu.mem_read(DS * 16, 0x10000)), prims + spans + left + sounds


def main():
    addr, regs, exits = ROUTINES[NAME]
    files = sorted(glob.glob(PATTERN))
    tmp = tempfile.mkdtemp()
    maskf = os.path.join(tmp, "mask")
    subprocess.run([TOOL, "mask", maskf], check=True)
    mask = open(maskf, "rb").read()
    bad, unmodelled = 0, collections.Counter()
    rng = random.Random(1)
    cases = [(p, None) for p in files] + [(p, k) for p in files for k in range(FUZZ_N)]
    for path, variant in cases:
        image = load(path)
        if variant is not None:
            image = fuzz(image, rng)
        before = image[DS * 16:DS * 16 + 0x10000]
        want, want_prims = run_original(image, addr, regs, exits)
        inf, outf = os.path.join(tmp, "in"), os.path.join(tmp, "out")
        open(inf, "wb").write(before)
        out = subprocess.run([TOOL, NAME, inf, outf], capture_output=True, text=True, check=True).stdout
        got, got_prims = open(outf, "rb").read(), out.splitlines()
        diff = [i for i in range(0x10000) if mask[i] and want[i] != got[i]]
        for i in range(0x10000):
            if not mask[i] and want[i] != before[i] and not any(a <= i <= b for a, b in SCRATCH):
                unmodelled[i] += 1
        if diff or want_prims != got_prims:
            bad += 1
            if bad <= 3:
                print(f"{os.path.basename(path)}{'' if variant is None else ' fuzz ' + str(variant)}: {len(diff)} modelled bytes differ "
                      f"{[hex(i) for i in diff[:12]]}, primitives {len(want_prims)} vs {len(got_prims)}"
                      + ("" if want_prims == got_prims else " (differ)"))
    print(f"{NAME}: {len(cases)} states ({FUZZ_N} fuzzed per state), {bad} differ")
    if unmodelled:
        runs, start, prev = [], None, None
        for i in sorted(unmodelled):
            if start is None:
                start = prev = i
            elif i == prev + 1:
                prev = i
            else:
                runs.append((start, prev))
                start = prev = i
        runs.append((start, prev))
        print("not modelled, changed by the original: " + " ".join(
            f"ds:{a:04x}" + (f"-{b:04x}" if b != a else "") + f"({unmodelled[a]})" for a, b in runs[:40]))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
