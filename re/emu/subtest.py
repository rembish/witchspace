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
import struct
import sys
import tempfile

from corpus import load
from eliteemu import CS, DS, REGS, SS, Elite
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EFLAGS, UC_X86_REG_IP, UC_X86_REG_SP

HERE = os.path.dirname(os.path.abspath(__file__))
ARGS = [a for i, a in enumerate(sys.argv[1:], 1) if not a.startswith("--") and sys.argv[i - 1] not in ("--fuzz", "--show")]
FUZZ_N = int(sys.argv[sys.argv.index("--fuzz") + 1]) if "--fuzz" in sys.argv else 0
SHOW = int(sys.argv[sys.argv.index("--show") + 1]) if "--show" in sys.argv else 3
NAME = ARGS[0]
PATTERN = ARGS[1] if len(ARGS) > 1 else os.path.join(HERE, "corpus", "*.bin")
TOOL = ARGS[2] if len(ARGS) > 2 else os.path.join(HERE, "..", "..", "build", "ep_subsys")

# Scratch space the original reuses within a routine (not state): INT 0 resume address, draw
# parameters, matrices and model temporaries, rotation temporary; and sound state (the core
# reports sounds as events).
SCRATCH = [(0x0002, 0x002C), (0x54CC, 0x54E1), (0x6405, 0x6405), (0x63F2, 0x6401), (0x92D4, 0x92DE), (0x92FA, 0x92FA), (0x8D00, 0x8D09), (0xA3A0, 0xA40F), (0xACA8, 0xACAF), (0xACB1, 0xACB3),
           (0x031B, 0x031E), (0x03F2, 0x03F2),
           (0x01F8, 0x01F9), (0x1074, 0x108E), (0x1091, 0x10BB), (0x10BD, 0x10C9), (0x28D0, 0x28E5), (0x2B66, 0x2BF5), (0x2CB1, 0x2CB2),
           (0x76D6, 0x76D7), (0x45E8, 0x45E9), (0x45EB, 0x45FF), (0x4FE0, 0x4FE0), (0x1F15, 0x1F16)]  # 1f15: the flash colour (3921)

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
UNPORTED = []  # routines the core reports as EP_EV_UNPORTED instead of running
UNPORTED_FOR = {}

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
    "dust": (0x4FA3, {}, {}),
    "dust_reset": (0x5374, {}, {}),
    "tribbles": (0x1221, {}, {}),
    "missile_lock": (0xA3F4, {}, {}),
    "jump_drive": (0xA5EE, {}, {}),
    "flight_start": (0x64D0, {}, {}),
    "select_system": (0x5EE8, {}, {}),
    "arrive": (0x72D8, {}, {}),
    "frame": (0xA040, {}, {0xA073: "end"}),
    "launch": (0xA027, {}, {0xA040: "end"}),
    "status": (0xA012, {}, {0x8DAC: "end"}),
    "market": (0x9048, {}, {0x9124: "end", 0x90B7: "end"}),
    "market_session": (0x9048, {}, {}),
    "dock": (0x6864, {}, {}),
    "loop": (0xA040, {"di": 0x7BDE}, {0xA021: "frame 1", 0x9E80: "frame 3"}),
    "key_bar": (0x0299, {}, {}),
    "commands": (0x03C0, {"di": 0x7BDE}, {0xA040: "cmd 1"}),  # DI as the frame leaves it
    "countdowns": (0xA0ED, {}, {}),
    "jump_missions": (0x753C, {}, {}),
    "witchspace": (0x7500, {}, {}),
    "rings": (0x7499, {}, {}),
    "new_system": (0x666B, {}, {}),
    "explode": (0x7EA8, {}, {}),
    "equip_screen": (0x924A, {}, {0x92D3: "end"}),
    "chart_session": (0x5AC0, {}, {0xA040: "end"}),
    "data_screen": (0x8880, {}, {0x8AFA: "end"}),
    "pause_session": (0x0425, {}, {0x9E80: "end", 0x00BA: "end"}),
    "start_game": (0xA004, {}, {0x8DAC: "end"}),  # a040: no chart in witchspace, back to the view
    "equip_session": (0x924A, {}, {}),
    "save_session": (0x07AA, {}, {}),
    "load_session": (0x08AB, {}, {0x9E80: "end"}),
    "title_open": (0x9E9A, {}, {0x9F21: "end"}),
    "title_session": (0x9F21, {}, {0xA004: "start", **{a: "end" for a in (  # a screen up
        0x0480, 0x0DF6, 0x0945, 0x08E4, 0x0AAC, 0x0AEF, 0x8DAC, 0x9124, 0x90B7, 0x92D3, 0x5C80, 0x595A, 0x8AFA, 0xA040)}}),
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


def dust_world(img, rng):
    """Any view, speed, steering, invert options and jump mode, particles near the edges."""
    w = lambda a, v, n=1: img.__setitem__(slice(DS * 16 + a, DS * 16 + a + n), (v & (256 ** n - 1)).to_bytes(n, "little"))
    w(0xB0DE, rng.choice([0, 0x200, 0x400, 0x600, 0]), 2)
    w(0xAF56, rng.choice([0, 4, rng.randint(0, 0x34), 0x30, 0x34]), 2)
    s = rng.choice([0, rng.getrandbits(16), rng.getrandbits(8), rng.getrandbits(8) << 8, 0x8080])
    w(0x09D7, s, 2)
    w(0xB136, rng.choice([0, 0, 1, 2]))
    w(0xB137, rng.choice([0, 0, 1]))
    w(0xB0DD, rng.choice([0, 0, 0, 1, 0x20]))
    for i in range(30):
        p = 0x5314 + 7 * i
        if rng.random() < 0.3:
            w(p, rng.choice([rng.randint(-0x2400, 0x23FF), rng.choice([0x1F80, -0x2000, 0x0500, -0x0600])]), 2)
            w(p + 2, rng.choice([rng.randint(-0x1200, 0x11FF), rng.choice([0x0F80, -0x1000, 0x0300, -0x0300])]), 2)
            w(p + 4, rng.choice([1, 2, rng.getrandbits(8)]))
        if rng.random() < 0.2:
            w(p + 5, rng.choice([0, 1]))


def lock_roles(img, rng):
    """Every type and class in the slots, some of them mission ships."""
    for i in range(2, 20):
        b = DS * 16 + 0x76DE + 0x40 * i
        if img[b] & 1 and rng.random() < 0.7:
            img[b] = (img[b] & 0xC1) | rng.randrange(32) << 1
            img[b + 0x33] = rng.choice([0, 1, 2, 3, 3, 4, 4, 5, 6, 7])
            img[b + 0x1E] = rng.choice([0, 2, 0x20, 0x60, 0x40])


def far_masses(img, rng):
    """Sun and planet beyond 16 bits, ships on or off the scanner."""
    for i in (0, 1):
        b = DS * 16 + 0x76DE + 0x40 * i
        if rng.random() < 0.7:
            img[b + 1 + rng.randrange(3)] = rng.choice([1, 0x10, 0xFE])
    for i in range(3, 36):
        b = DS * 16 + 0x76DE + 0x40 * i
        if img[b] & 1 and rng.random() < 0.5:
            img[b + 0x1E] &= 0xFD
        if rng.random() < 0.1:
            img[b] = (rng.choice([5, 17, 6, 11, 9]) << 1) | 1
            img[b + 0x1E] |= 2


def near_centre(img, rng):
    """A zoomed chart with the cursor near its middle and the centre on a busy area."""
    if rng.random() < 0.5:
        d = DS * 16
        img[d + 0x831E] = 1
        img[d + 0x8318] = 0x50 + rng.randint(-60, 60)
        img[d + 0x8319] = 0x40 + rng.randint(-40, 40)


def jump_world(img, rng):
    """A jump under way: galactic or not, its target, missions near their start, rings."""
    w = lambda a, v, n=1: img.__setitem__(slice(DS * 16 + a, DS * 16 + a + n), (v & (256 ** n - 1)).to_bytes(n, "little"))
    w(0xAE24, rng.choice([0, 0, 1, 2]))
    w(0x8315, rng.choice([0, 1, 6, 7, 7, 8]))
    w(0x83B4, rng.randrange(256))
    for k in range(0x19):
        w(0x8611 + k, rng.getrandbits(8))
    w(0x8610, rng.choice([0, 0, 0, 1]))
    w(0x82D6, rng.randrange(0x50))
    w(0x8356, rng.choice([0, 0x10, 0x46]))
    w(0x836B, rng.choice([0, 3, 5, 0x40]))
    w(0x83A0, rng.choice([0, 0, 1, 3, 4]))
    w(0x83A2, rng.choice([0, 1, 2, 3]))
    w(0x83A3, rng.randrange(256))
    w(0x83B0, rng.choice([0, 1]))
    w(0x83AB, rng.choice([0, 1]))
    w(0x83B3, rng.choice([0, 1, 2]))
    w(0x83A1, rng.choice([0, 1]))
    w(0x839E, rng.choice([0x1F, 0x37, 0x4F, 0x6D, 0x8B, 0x9F, 0xFF, 0x10]))
    w(0x83A4, rng.choice([0, 0, 1, 0x64]))
    w(0x831E, rng.choice([0, 1]))
    w(0x108F, rng.choice([0, 0, 1, 3]), 2)
    w(0x1B3E, rng.randrange(6))
    w(0x8361, rng.choice([0, 1]))
    w(0x0D2F, rng.choice([0xFF, 0x20, 0x39]))
    if rng.random() < 0.6:
        for k, v in enumerate([1, 0x14, 0x0C, 5, 0x0F, 0x0C, 8, 0x0B, 0x0C, 0x0A, 8, 0x0A, 0x0B, 6, 0x0A, 0x0C, 6, 0x0F,
                               0x0D, 6, 0x0F, 0x0E, 6, 0x0F, 0x0F, 6, 0x0F, 0x14, 6, 0x0C]):
            w(0x85DC + k, v)
    else:
        for k in range(10):
            w(0x85DC + 3 * k, rng.choice([0, 0, 1, 5]))
            w(0x85DD + 3 * k, rng.choice([0, 6, 0x13, 0x14, 0x80, 0x95, 0x96]))
    if rng.random() < 0.3:  # the flight generator about to misjump
        for j in range(3):
            w(0x830F + 2 * j, rng.randrange(1, 4), 2)  # never all zero (it would stay 0)


def bar_world(img, rng):
    """Any screen, the bar as drawn, the equipment the flight bar looks at."""
    w = lambda a, v, n=1: img.__setitem__(slice(DS * 16 + a, DS * 16 + a + n), (v & (256 ** n - 1)).to_bytes(n, "little"))
    w(0x02F9, rng.choice([0, 0, 0, 1, 2, 3, 4]))
    w(0x02FA, rng.choice([0, 0, 1, 2, 0xFF]))
    w(0x8711, rng.choice([0, 0, 1, 2]))
    ids = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xA, 0xB, 0xE, 0xF, 0x23, 0x24, 0x0C, 0x10, 0x20]
    for k in range(12):
        w(0x0301 + k, rng.choice(ids + [0xFF]))
    for a, vals in ((0x8357, [0, 1, 4]), (0x8359, [0, 1]), (0x835D, [0, 1]), (0x835E, [0, 1]), (0x8364, [0, 1]),
                    (0x83AC, [0, 1]), (0x8361, [0, 1]), (0x54CA, [0, 1, 2]), (0xAE60, [0, 0, 0, 3]), (0xAE23, [0, 0, 5]),
                    (0x031D, [0, 1]), (0x031E, [0, 1, 2, 4, 8, 0xF])):
        w(a, rng.choice(vals))


def command_world(img, rng):
    """A key pressed, and the state the flight commands test."""
    w = lambda a, v, n=1: img.__setitem__(slice(DS * 16 + a, DS * 16 + a + n), (v & (256 ** n - 1)).to_bytes(n, "little"))
    for k in range(12):  # 0299 has run: no ff (redraw marker) left
        if img[DS * 16 + 0x0301 + k] == 0xFF:
            img[DS * 16 + 0x0301 + k] = 0
    slot = rng.randrange(12)
    w(0x0301 + slot, rng.choice([1, 5, 6, 7, 8, 8, 9, 0xA, 0xB, 0xE, 0xF, 0x23, 0x24]))
    keys = [0x97 + slot, ord("1234567890-="[slot])]
    w(0x0D2F, rng.choice(keys * 4 + [0xFF, 0x20, 0x1B, 0x41]))
    w(0x54CA, rng.choice([0, 1, 2, 2]))
    w(0xB0DE, rng.choice([0, 0x400, 0x200, 0x600, 0x4FF]), 2)
    w(0x02F9, rng.choice([img[DS * 16 + 0x02F9], img[DS * 16 + 0x02F9], 1]))  # sometimes at the station
    for a, vals in ((0xAF14, [0, 0, 1]), (0xB126, [0, 0, 1]), (0xB0E0, [0, 0, 1]), (0x83AA, [0, 0, 1]),
                    (0x83AB, [0, 1]), (0x83B1, [0, 1]), (0xAE25, [0, 0, 3]), (0xAE60, [0, 0, 0, 2]), (0xAE23, [0, 0, 3]),
                    (0xB1F8, [0, 0, 1]), (0xB3D5, [0, 0, 5]), (0x7680, [0, 1, 1]), (0x8360, [0, 1]),
                    (0xB139, [0, 1]), (0x8711, [0, 0, 0, 1]), (0xAF56, [0, 4, 0x14]), (0x8356, [0, 0x23, 0x46, 0xFF])):
        w(a, rng.choice(vals))
    w(0x54C8, rng.choice([0x10, 0x77, 0x78, 0x2FF, 0x300, 0x3FF]), 2)
    w(0x8343, rng.choice([0, 1, 0x10, 0x46, 0x47, 0x80]), 2)
    w(0x8367, rng.choice([0, 0x1F3, 0x1F4, 100000]), 4)
    w(0x775E + 0x1E, rng.choice([0, 1, 4]))
    w(0xB0E1, 0x76DE + 0x40 * rng.randint(2, 19), 2)  # a locked missile has a target
    if rng.random() < 0.3:
        for j in range(3):
            w(0x830F + 2 * j, rng.randrange(1, 4), 2)  # never all zero (it would stay 0)


def autopilot_world(img, rng):
    """The docking computer in any of its states, the station around."""
    if rng.random() < 0.3:
        return
    w = lambda a, v, n=1: img.__setitem__(slice(DS * 16 + a, DS * 16 + a + n), (v & (256 ** n - 1)).to_bytes(n, "little"))
    w(0xAF14, 1)
    w(0xAF17, rng.randrange(13))
    w(0xAF5B, rng.choice([0, 1]))
    w(0xAF59, rng.randrange(0x800), 2)
    w(0xAF56, rng.choice([0, 4, 8, 0x14, 0x30, 2]), 2)
    b = 0x76DE + 0x80
    w(b, (rng.choice([0, 1]) << 1) | 1)
    r = rng.choice([300, 700, 2000, 5000, 9000])
    for k in range(3):
        v = rng.randint(-r, r) if k < 2 else rng.choice([rng.randint(-r, r), -rng.randint(0x200, 0x400), -0x28A, -0x289, -0x3E8])
        w(b + 4 + 2 * k, v, 2)
        w(b + 1 + k, 0xFF if v < 0 else 0)
    w(b + 0x0E, rng.randrange(0x800), 2)
    w(0x76DC, rng.randrange(0x800), 2)
    step = img[DS * 16 + 0xAF17]
    sp = rng.choice([4, 8, 0x14, 0x30])
    if step == 4 and rng.random() < 0.7:  # within a step or two of the docking point
        w(0xAF56, sp, 2)
        off = [rng.randint(-8, 8), rng.randint(-8, 8), rng.randint(0x34, 0x70)]  # beyond a step at any speed
        for k in range(3):
            v = off[k] - (0x7D0 if k == 2 else 0)
            w(b + 4 + 2 * k, v, 2)
            w(b + 1 + k, 0xFF if v < 0 else 0)
    if step == 8 and rng.random() < 0.7:  # around the slot
        v = -rng.choice([0x288, 0x289, 0x28A, 0x28B, 0x3E7, 0x3E8, 0x500])
        w(b + 8, v, 2)
        w(b + 3, 0xFF)
    if step in (2, 6, 9, 0xA, 0xB) and rng.random() < 0.7:  # rolls nearly matched
        roll = rng.randrange(0x800)
        w(0x76DC, roll, 2)
        w(0xAF59, roll + rng.randint(-0x15, 0x15), 2)
        w(b + 0x0E, roll + rng.choice([0, 0x400]) + rng.randint(-0x0C, 0x0C), 2)


def status_world(img, rng):
    """Docking with any equipment, cash, legal status, kills, Tribbles; no dialogs (yet)."""
    w = lambda a, v, n=1: img.__setitem__(slice(DS * 16 + a, DS * 16 + a + n), (v & (256 ** n - 1)).to_bytes(n, "little"))
    kills = rng.choice([0, 1, 2, 3, 8, 9, 0x13, 0x14, 0x5A, 0x3E7, 0x3E8, 0xEA5F])
    w(0x836C, kills, 2)
    w(0x836E, kills, 2)
    w(0x83B7, 0, 2)
    w(0x83A0, 0)
    w(0x83B5, rng.choice([0, 0, 1, 2, 30000]), 2)
    w(0x839B, rng.choice([0, 0, 0x14]))
    w(0x836B, rng.choice([0, 1, 0x27, 0x28, 0xFF]))
    w(0x8356, rng.choice([0, 1, 0x46, 0xFF]))
    w(0x83A4, rng.choice([0, 0, 1]))
    w(0x54CB, rng.randrange(4))
    for k in range(14):
        w(0x8357 + k, rng.choice([0, 0, 1] if k else [0, 1, 4]))
    w(0x8365, rng.randrange(16))
    w(0x8366, rng.getrandbits(8))


def market_world(img, rng):
    """Docked or in flight, the market drawn already or not, any cargo, any economy."""
    w = lambda a, v, n=1: img.__setitem__(slice(DS * 16 + a, DS * 16 + a + n), (v & (256 ** n - 1)).to_bytes(n, "little"))
    w(0x02F9, rng.choice([1, 1, 0, 2]))
    w(0x839D, rng.choice([0, 1]))
    for k in range(17):
        w(0x8379 + 2 * k, rng.choice([0, 0, 1, 7, 20, 200]))
        w(0x837A + 2 * k, rng.choice([0, 0, 1, 9, 30, 0xFA]))
    w(0x832C, rng.randrange(8))
    w(0x832D, rng.randrange(8))
    w(0x832E, rng.choice([0, 4, 9, 12, 15]))
    for j in range(3):
        w(0x92E0 + 2 * j, rng.getrandbits(16), 2)
    w(0x8367, rng.choice([0, 1, 99999, 1234567]), 4)


def start_world(img, rng):
    """Any time of day; a saved commander a little different from the current one."""
    w = lambda a, v, n=1: img.__setitem__(slice(DS * 16 + a, DS * 16 + a + n), (v & (256 ** n - 1)).to_bytes(n, "little"))
    for j, top in enumerate((24, 60, 60, 100)):
        w(0xFF30 + j, rng.randrange(top))
    b = 0x83BE  # fields of the saved commander (its texts stay valid)
    w(b + 0x7B, rng.choice([0, 0x46, 0xFF]))                       # fuel
    for k in range(14):
        w(b + 0x7C + k, rng.choice([0, 0, 1]))                     # equipment
    w(b + 0x8C, rng.choice([0, 1000, 99999, 0x10000]), 4)        # cash
    w(b + 0x90, rng.choice([0, 5, 0x28, 0xFF]))                    # legal status
    w(b + 0xC5, rng.choice([0, 0, 1, 3, 4]))                       # mission
    w(b + 0xD5, rng.choice([0, 0, 1]))                             # mission phase
    w(b + 0x3A, rng.randrange(8))                                  # galaxy
    w(0x83BE + 0x93, rng.choice([0, 1, 9, 0x14]), 2)  # its kills (836e) and the current ones
    w(0x83BE + 0x91, rng.choice([0, 1, 9, 0x14, 0x15]), 2)


FILES = 0xFC00  # the fake disk: a count, then 16 bytes a file (name, NUL, kind, a byte to vary it)
FILE_GOOD, FILE_BAD_SUM, FILE_SHORT, FILE_NO_OPEN, FILE_READ_ONLY = range(5)


def disk(image):
    """The files ds:fc00 describes: {name: (kind, contents)}, in directory order. A commander file
    is the commander in the state with its cash and fuel varied (tests/subsys.c makes the same)."""
    files = {}
    base = bytearray(image[DS * 16 + 0x82DB:DS * 16 + 0x82DB + 0xE2])
    for j in range(image[DS * 16 + FILES]):
        at = DS * 16 + FILES + 0x10 + 0x10 * j
        name = bytes(image[at:at + 13]).split(b"\0")[0].decode()
        kind, vary = image[at + 13], image[at + 14]
        c = bytearray(base)
        c[0x7B] = vary
        c[0x8C] ^= vary
        s = 0x454C
        for b in c[:0xE0]:
            s = (s & 0xFF00) + (s & 0xFF) + b & 0xFFFF  # add al; adc ah, 0
            s = (s << 1 | s >> 15) & 0xFFFF
        c[0xE0:0xE2] = (s ^ (1 if kind == FILE_BAD_SUM else 0)).to_bytes(2, "little")
        files[name] = (kind, bytes(c[:100] if kind == FILE_SHORT else c))
    return files


def fake_dos(files, written):
    """int 21h for the commander files: {name: (kind, contents)}; what is written is noted."""
    found, handles = [], {}

    def string(e, at):
        out = b""
        while e.r8(at + len(out)):
            out += bytes([e.r8(at + len(out))])
        return out.decode("latin-1")

    def dos(e, intno):
        if intno != 0x21:
            return False
        mu = e.mu
        ax, bx, cx, dx = (mu.reg_read(REGS[k]) for k in ("ax", "bx", "cx", "dx"))
        fn, fail, out = ax >> 8, False, ax
        if fn == 0x1A:
            assert dx == 2
        elif fn in (0x4E, 0x4F):
            if fn == 0x4E:
                assert string(e, dx) == "*.CDR"
                found[:] = list(files)
            if found:
                mu.mem_write(DS * 16 + 0x20, found.pop(0).encode() + b"\0")
            else:
                fail, out = True, 0x12
        elif fn == 0x3D:
            name = string(e, dx)
            if name not in files or files[name][0] == FILE_NO_OPEN:
                fail, out = True, 2
            else:
                out = 5 + len(handles)
                handles[out] = name
        elif fn == 0x3C:
            name = string(e, dx)
            if name in files and files[name][0] == FILE_READ_ONLY:
                fail, out = True, 5
            else:
                files[name] = (FILE_GOOD, b"")
                out = 5 + len(handles)
                handles[out] = name
        elif fn == 0x3F:
            data = files[handles[bx]][1][:cx]
            mu.mem_write(DS * 16 + dx, data)
            out = len(data)
        elif fn == 0x40:
            data = bytes(mu.mem_read(DS * 16 + dx, cx))
            files[handles[bx]] = (FILE_GOOD, data)
            written.append(f"write {handles[bx]}:{data.hex()}")
            out = cx
        elif fn == 0x3E:  # a bad file is closed twice, the second time with the checksum as the handle
            if handles.pop(bx, None) is None:
                fail, out = True, 6
        else:
            raise RuntimeError(f"int 21h function {fn:#x}")
        mu.reg_write(REGS["ax"], out)
        fl = mu.reg_read(UC_X86_REG_EFLAGS)
        mu.reg_write(UC_X86_REG_EFLAGS, fl | 1 if fail else fl & ~1)
        return True
    return dos


def files_world(img, rng):
    """Commander files on the disk (none to a screenful and more), good and bad; a name to save."""
    w = lambda a, v, n=1: img.__setitem__(slice(DS * 16 + a, DS * 16 + a + n), (v & (256 ** n - 1)).to_bytes(n, "little"))
    name = rng.choice(["JAMESON", "A", "ELITE-8", "Z9", "NEWCMDR", "LAVE", "ABCDEFGH"])
    w(0x8370, 0, 9)
    img[DS * 16 + 0x8370:DS * 16 + 0x8370 + len(name)] = name.encode()
    n = rng.choice([0, 1, 2, 3, 5, 12, 13, 14, 20, 40])
    names = set()
    while len(names) < n:
        names.add("".join(rng.choice("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-") for _ in range(rng.randint(1, 8))))
    names = sorted(names)
    if names and rng.random() < 0.5:
        names[rng.randrange(len(names))] = name  # saving would overwrite it
    names = list(dict.fromkeys(names))
    w(FILES, len(names))
    for j, f in enumerate(names):
        at = FILES + 0x10 + 0x10 * j
        w(at, 0, 16)
        img[DS * 16 + at:DS * 16 + at + len(f) + 4] = (f + ".CDR").encode()
        w(at + 13, rng.choice([FILE_GOOD, FILE_GOOD, FILE_GOOD, FILE_BAD_SUM, FILE_SHORT, FILE_NO_OPEN, FILE_READ_ONLY]))
        w(at + 14, rng.randrange(256))
    w(0x8365, rng.choice([0, 1, 1, 3]))  # lasers, a military one in front
    w(0x8366, rng.choice([0, 3, 3, 0x0F]))
    w(0x8363, rng.choice([0, 0, 1]))
    keys = []
    if NAME == "save_session":
        if rng.random() < 0.5:
            keys += [8] * rng.randrange(4) + [ord(c) for c in rng.choice(["", "X", "LAVE", "AB-1", "x"])]
        keys += [rng.choice([0x0D, 0x0D, 0x0D, 0x1B, 0xFF])]
    if rng.random() < 0.5:  # down past the end of a long list and back up past its top
        keys += [0x50] * rng.randint(11, 16) + [0x48] * rng.randint(11, 15)
    while len(keys) < 32:
        keys.append(rng.choice([0x48, 0x50, 0x50, 0x50, 0x0D, 0x1B, 0xFF, ord("Y"), ord("n"), ord("N"), ord("y"), 0x20, 0x20]))
    for j in range(32):
        w(0xFF10 + j, keys[j])


def title_world(img, rng):
    """Any sound; keys for the waits or the passes; a title ship part way. (Not the video mode:
    its routines are chosen once, at the start.)"""
    w = lambda a, v, n=1: img.__setitem__(slice(DS * 16 + a, DS * 16 + a + n), (v & (256 ** n - 1)).to_bytes(n, "little"))
    w(0x4801, rng.choice([0, 1, 2]))
    w(0x45EA, rng.randrange(4))
    w(0x76B5, rng.choice([3, 3, 12, 36]))
    keys = []
    while len(keys) < 12:
        if NAME == "title_session":  # mostly passes with no key; F-keys, Esc; space late if at all
            keys.append(rng.choice([0xFF] * 12 + [0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F, 0xA0, 0xA1, 0xA2,
                                                  0x1B, ord("x"), 0x20 if len(keys) > 8 else 0xFF]))
        else:
            keys += [0xFF] * rng.choice([0, 0, 1, 3]) + [rng.choice([0x20, 0x20, 0x9A, 0x9B, 0x9D, 0x9E, 0x1B, ord("x"), 0xFF])]
    for j in range(12):
        w(0xFF10 + j, keys[j])
    if NAME == "title_session":  # where the ship is
        w(0xB25F, rng.choice([0, 0, 1, 0x76, 0x77, 0x78]), 2)
        w(0x775E + 8, rng.choice([5000, 4900, 1000, 200]), 2)
        at = rng.choice([0, 5, 13, 22, 23, 23])  # 23: the last before the end mark
        w(0xB261, 0xB263 + at, 2)
        w(0xB1BB, img[DS * 16 + 0xB263 + at])
        w(0x775E, img[DS * 16 + 0xB263 + at] << 1 | 1)


def pause_world(img, rng):
    """From any screen; options toggled, sound, abandon or exit asked, space."""
    w = lambda a, v, n=1: img.__setitem__(slice(DS * 16 + a, DS * 16 + a + n), (v & (256 ** n - 1)).to_bytes(n, "little"))
    w(0x02F9, rng.choice([0, 0, 1, 2, 3]))
    w(0x02FA, rng.choice([0, 0xFF]))
    for j in range(12):
        w(0xFF10 + j, rng.choice([0x9A, 0x9B, 0x9C, 0x9D, 0x9F, 0xA1, 0xA2, ord("Y"), ord("n"), ord("x"), 0xFF, 0x20, 0x20]))


def chart_world(img, rng):
    """Docked or in flight, a chart already up or not, cursors, fuel; keys and arrows per pass."""
    w = lambda a, v, n=1: img.__setitem__(slice(DS * 16 + a, DS * 16 + a + n), (v & (256 ** n - 1)).to_bytes(n, "little"))
    w(0x02F9, rng.choice([0, 1, 1, 2]))
    w(0x8711, rng.choice([0, 1, 1, 2]))
    w(0x6404, rng.choice([0, 0xFF]))
    w(0x83A4, rng.choice([0, 0, 0, 1, 0x64]))
    w(0x8315, rng.randrange(8))
    w(0x8329, rng.randrange(256))
    w(0x8356, rng.choice([0, 0x46, 0xFF]))
    for a in (0x831A, 0x831C):
        w(a, rng.randrange(256))
    for a in (0x831B, 0x831D):
        w(a, rng.randrange(0x80))
    for a in (0x8316, 0x8317):
        w(a, rng.randrange(0x100 if a == 0x8316 else 0x80))
    keys = []  # single keys, or a find: F9, some letters, Enter or Esc (Esc elsewhere is the menu)
    while len(keys) < 12:
        if rng.random() < 0.3:
            keys += [0x9F] + [rng.choice([ord("L"), ord("A"), ord("V"), ord("E"), ord("Z"), 8, ord("l"), 0xFF])
                              for _ in range(rng.randrange(5))] + [rng.choice([0x0D, 0x0D, 0x1B])]
        else:
            keys.append(rng.choice([0xFF, 0xFF, 0x9E, 0xA0, 0x9A, ord("L"), 0x0D]))
    for j in range(12):
        w(0xFF10 + j, keys[j])
        w(0xFF20 + j, rng.choice([0, 0, 1, 2, 4, 8, 5, 10]))


def equip_world(img, rng):
    """Docked, any tech level, equipment, lasers fitted, cash, fuel, cargo."""
    w = lambda a, v, n=1: img.__setitem__(slice(DS * 16 + a, DS * 16 + a + n), (v & (256 ** n - 1)).to_bytes(n, "little"))
    w(0x02F9, 1)
    w(0x832E, rng.choice([0, 5, 9, 12, 14]))
    w(0x832C, rng.randrange(8))
    w(0x832D, rng.randrange(8))
    w(0x8356, rng.choice([0, 0x46, 0xFA, 0xFB, 0xFF]))
    for k in range(14):
        w(0x8357 + k, rng.choice([0, 0, 1] if k else [0, 1, 3, 4]))
    mounts, types = rng.choice([0, 1, 3, 5, 7, 0xF, rng.randrange(16)]), rng.getrandbits(8)
    w(0x8365, mounts)
    w(0x8366, types)
    for t, row in enumerate((4, 5, 12, 13)):  # each laser counted as often as it is fitted
        w(0x8356 + row, sum(1 for m in range(4) if mounts >> m & 1 and types >> (2 * m) & 3 == t))
    w(0x8367, rng.choice([0, 5, 0x1F4, 100000, 9999999]), 4)
    w(0x839C, rng.choice([0, 0x14, 0x15, 0x23]))
    w(0x83A0, rng.choice([0, 0, 1]))


def equip_keys(img, rng):
    """12 keys: arrows, buy (F9/9), sell (F10/0), Enter, nothing."""
    for j in range(12):
        img[DS * 16 + 0xFF10 + j] = rng.choice([0xFF, 0x48, 0x50, 0x50, 0x50, 0x9F, 0x9F, 0xA0, ord("9"), ord("0"), 0x0D,
                                                0x0D])


def market_keys(img, rng):
    """Docked, and 12 keys: arrows, buy (F9 or 9), sell (F10 or 0), nothing."""
    img[DS * 16 + 0x02F9] = 1
    for j in range(12):
        img[DS * 16 + 0xFF10 + j] = rng.choice([0xFF, 0x48, 0x50, 0x50, 0x9F, 0x9F, 0xA0, ord("9"), ord("0")])


def arrival_dialogs(img, rng):
    """Promotions, the Tribble offer, briefings and debriefings, and the keys answering them."""
    if rng.random() < 0.3:
        return
    w = lambda a, v, n=1: img.__setitem__(slice(DS * 16 + a, DS * 16 + a + n), (v & (256 ** n - 1)).to_bytes(n, "little"))
    t = [2, 4, 9, 0x14, 0x23, 0x5A, 0x9B, 0x3E8, 0xEA5E]  # 60000 and up overrun the table (garbage)
    k = rng.choice(t)
    w(0x836E, k - rng.choice([1, 0, 0]), 2)
    w(0x836C, k + rng.choice([0, 0, 1]), 2)
    w(0x83B7, rng.choice([0, 0, 0x100, 0xFFFF]), 2)
    w(0x8367, rng.choice([0, 0xFF, 0x10000, 0xC350]), 4)
    w(0x83A0, rng.choice([0, 1, 2, 3, 4, 5, 6]))
    w(0x83B0, rng.choice([0, 1, 1, 2]))
    w(0x7613, rng.choice([0, 1, 1]))
    for a in (0x83A7, 0x83B1, 0x83A8, 0x83B2, 0x83AA, 0x8358):
        w(a, rng.choice([0, 1]))
    w(0x83A2, rng.choice([0, 1, 3]))
    w(0x83A3, rng.choice([img[DS * 16 + 0x8329], 7]))
    w(0x839B, rng.choice([0, 0x14, 0x23]))
    for j in range(8):
        w(0xFF10 + j, ord(rng.choice("YNynx ")))


def arrival_world(img, rng):
    """Any galaxy (the hidden 8 too), system, tech, government; witchspace; docked here before."""
    w = lambda a, v, n=1: img.__setitem__(slice(DS * 16 + a, DS * 16 + a + n), (v & (256 ** n - 1)).to_bytes(n, "little"))
    galaxy = rng.choice([0, 1, 7, 8, rng.randrange(8)])
    system = rng.randrange(256)
    w(0x8315, galaxy)
    w(0x8329, system)
    w(0x832E, rng.choice([0, 8, 9, 12]))
    w(0x832C, rng.randrange(8))
    w(0x83A4, rng.choice([0, 0, 0, 1, 0x64]))
    w(0x83AE, rng.choice([0, 0, 1]), 2)
    w(0x83B9, rng.choice([galaxy, galaxy, 3]))
    w(0x83BA, rng.choice([system, system, 5]))
    w(0x83AA, rng.choice([0, 1]))


def tribble_world(img, rng):
    """Any number of Tribbles, cargo to eat, sprites walking to the edges."""
    w = lambda a, v, n=1: img.__setitem__(slice(DS * 16 + a, DS * 16 + a + n), (v & (256 ** n - 1)).to_bytes(n, "little"))
    w(0x83B5, rng.choice([0, 1, 1, 2, 14, 15, 29, 30, 79, 80, 124, 125, 0x5E, 0x5F, 0x2AB, 0x2AC, 0x1000, 0x98C9, 0x98CA]), 2)
    w(0x0AA4, rng.choice([0, 1]), 2)
    n = rng.choice([0, 1, 5, 0x3F, 0x40])
    w(0x0AA6, n, 2)
    for k in range(n):
        p = 0x0AA8 + 8 * k
        w(p, rng.choice([rng.randint(8, 0x127), 8, 9, 0x126, 0x127]), 2)
        w(p + 2, rng.randint(0, 0xBC), 2)
        w(p + 4, rng.choice([0, 1, 2, -1, -2]), 2)
    for k in range(17):
        if rng.random() < 0.3:
            w(0x8379 + 2 * k, rng.choice([0, 1, 5]))
    if rng.random() < 0.4:  # the main generator about to give small numbers
        for j in range(4):
            w(0x0205 + 2 * j, rng.randrange(1, 0x30), 2)


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


def ai_handlers(img, rng):
    """Ships of every class in every state, near the boxes their handlers test, with targets."""
    d = DS * 16
    slot = lambda i: 0x76DE + 0x40 * i
    w = lambda a, v, n=1: img.__setitem__(slice(d + a, d + a + n), (v & (256 ** n - 1)).to_bytes(n, "little"))
    used = []
    for i in range(2, 20):
        b = slot(i)
        if rng.random() < 0.35:
            continue
        cls = rng.choice([1, 2, 2, 3, 4, 4, 5, 5, 6, 6]) if i > 2 else 1
        types = {1: [0, 1], 2: [20], 3: [3, 1, 7, 12, 5], 4: [9, 10, 28, 28, 5, 12, 13], 5: [22, 7, 22, 15, 25],
                 6: [15, 17, 18]}[cls]
        t = rng.choice(types)
        w(b, (t << 1) | 1 | rng.choice([0, 0x80]))
        w(b + 0x33, cls)
        w(b + 0x17, rng.choice([0, 1, 2, 3, 4, 5, 10, 2, 3]))
        w(b + 0x1E, rng.choice([0, 1, 2, 3, 3, 0x11, 0x13, 4, 6]))
        w(b + 0x16, rng.choice([1, 2, 3]))
        w(b + 0x30, rng.choice([0, 3, 9, 10, 0x18, 0x19, 0x80, 0xFF]))
        w(b + 0x32, rng.choice([0, 1, 3]))
        w(b + 0x1F, rng.choice([0, 1, 2]))
        w(b + 0x2B, rng.choice([0, 2, 4, 7, 8, 9, 0x20, 0xFF]))
        w(b + 0x35, rng.choice([0, 0, 1, 2, 5]))
        w(b + 0x1C, rng.getrandbits(8))
        w(b + 0x1D, rng.choice([1, 4, 0x10, 0x1E]))
        w(b + 0x18, rng.choice([2, 9, 10, 12, 30]))
        w(b + 0x3A, rng.choice([0, 1, 1, slot(rng.randint(2, 19))]), 2)
        r = rng.choice([200, 450, 800, 1000, 2000, 5000, 0x1C2, 0x3000])
        for k in range(3):
            v = rng.choice([rng.randint(-r, r), rng.randint(-r - 20, -r + 20), rng.randint(r - 20, r + 20),
                            rng.randint(-0x7FFF, 0x7FFF)])
            w(b + 4 + 2 * k, v, 2)
            w(b + 1 + k, 0xFF if v < 0 else 0)
        if rng.random() < 0.1:
            w(b + 1 + rng.randrange(3), rng.getrandbits(8))
        for k in range(3):
            w(b + 0x0A + 2 * k, rng.getrandbits(16), 2)
        used.append(i)
    for i in used:
        b = slot(i)
        if img[d + b + 0x33] in (2, 6) and rng.random() < 0.7:
            j = rng.choice(used + [0, 2])
            w(b + 0x29, slot(j) if j else 0, 2)
            if img[d + b + 0x33] == 2 and j and rng.random() < 0.6:
                for k in range(3):
                    w(b + 4 + 2 * k, int.from_bytes(img[d + slot(j) + 4 + 2 * k:d + slot(j) + 6 + 2 * k], "little")
                      + rng.randint(-220, 220), 2)
    for i in used:
        b = slot(i)
        if img[d + b + 0x17] in (3, 4, 5) and rng.random() < 0.5:  # near the stale-DL range
            k = rng.randrange(3)
            for j in range(3):
                v = (img[d + b + 0x1C] << 8) + rng.randint(-300, 300) if j == k else rng.randint(-200, 200)
                v = rng.choice([v, -v])
                w(b + 4 + 2 * j, v, 2)
                w(b + 1 + j, 0xFF if v < 0 else 0)
        if img[d + b + 0x33] == 2 and rng.random() < 0.3:  # a missile at the player
            w(b + 0x29, 0, 2)
            for j in range(3):
                v = rng.randint(-230, 230)
                w(b + 4 + 2 * j, v, 2)
                w(b + 1 + j, 0xFF if v < 0 else 0)
    if rng.random() < 0.5:  # the station was hit, far enough to launch
        b = slot(2)
        w(b, rng.choice([0, 1]) << 1 | 1)
        w(b + 0x33, 1)
        w(b + 0x1E, img[d + b + 0x1E] | 1)
        for j in range(3):
            v = rng.choice([rng.randint(-3000, 3000), 460, -460, 0x1C2])
            w(b + 4 + 2 * j, v, 2)
            w(b + 1 + j, 0xFF if v < 0 else 0)
        if rng.random() < 0.5:  # slot 3 reads the DL the station left (c2, or a launch's)
            b3 = slot(3)
            w(b3, (rng.choice([9, 10, 15]) << 1) | 1)
            w(b3 + 0x33, rng.choice([4, 5, 6]))
            w(b3 + 0x17, rng.choice([3, 4]))
            w(b3 + 0x2B, 0x20)
            hi = rng.randrange(1, 0x30)
            w(b3 + 0x1C, hi)
            for j in range(3):
                v = (hi << 8 | rng.choice([0xC2, 0xC3, 0xC1, rng.randrange(256)])) if j == 0 else rng.randint(-100, 100)
                w(b3 + 4 + 2 * j, v, 2)
                w(b3 + 1 + j, 0xFF if v < 0 else 0)
    if rng.random() < 0.35:  # a generator about to give small numbers: every chance gate opens
        for j in range(3):
            w(0x830F + 2 * j, rng.randrange(1, 4), 2)  # never all zero (it would stay 0)
    w(0x836B, rng.choice([0, 4, 5, 9, 10, 39, 40, 0xFB, 0xFF]))
    w(0x836C, rng.choice([0, 2, 3, 0xFF]))
    w(0x83AA, rng.choice([0, 0, 1]))
    w(0x8891, rng.choice([0, 0, 0, 1, 20]))
    w(0xB126, rng.choice([0, 0, 0, 1]))
    w(0xAE23, rng.choice([0, 0, 0, 1]))
    w(0xB138, rng.choice([0, 0, 0, 1]))
    w(0xB139, rng.choice([0, 1]))
    w(0x7680, rng.choice([0, 1]))
    w(0x76B6, rng.choice([0, 1, 3]))


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
                (0x81F5, 1), (0x81F2, [0x81FE, 0x8212]), (0x8892, [0, 1, 2]), (0x54C3, [0x10, 0x31, 0x32, 0xFE]),
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
    "dust": [(0, dust_world)],
    "tribbles": [(0, tribble_world)],
    "flight_start": [(0, arrival_world)],
    "arrive": [(0, arrival_world), (0, jump_world)],
    "key_bar": [(0, bar_world)],
    "commands": [(0, bar_world), (0, command_world), (0, ship_in_sights)],
    "countdowns": [(0, jump_world), (0xAE60, [0, 0, 1, 2, 10, 11]), (0xAE61, [1, 1, 2, 10]), (0xAE25, [0, 1, 5]),
                   (0xB3D5, [0, 0, 1, 2, 7, 15, 0x28]), (0x54C8, [0x100, 0x2FE, 0x2FF, 0x3FF]), (0, ai_world)],
    "status": [(0, status_world), (0, arrival_dialogs)],
    "market": [(0, market_world)],
    "market_session": [(0, trading), (0, market_world), (0, market_keys)],
    "launch": [(0, arrival_world), (0x8711, [0, 1]), (0x4801, [0, 1, 2]), (0x45E7, [0, 1]), (0, dust_world)],
    "dock": [(0x7613, [0, 1, 1]), (0x4801, [0, 1, 2]), (0x45E7, [0, 1]), (0, dust_world), (0, ai_world)],
    "loop": [(0, ai_handlers), (0, autopilot_world), (0, dust_world), (0, ship_in_sights), (0, bar_world), (0, command_world),
             (0x76BD, [0, 0, 0, 1]), (0xB126, [0, 0, 1, 2, 0x3C]), (0x839C, [0, 3]), (0x7613, [0, 0, 0, 1]),
             (0xAE23, [0, 0, 1, 2])],
    "frame": [(0, ai_handlers), (0, dust_world), (0, ship_in_sights), (0, tribble_world), (0, dashboard_world),
              (0x54CA, [0, 0, 1, 2]), (0x020D + 0x39, [0, 0, 0x80]), (0x8365, [0, 1, 0xF]), (0x020D + 0x48, [0, 0x80]),
              (0x020D + 0x50, [0, 0x80])],
    "jump_missions": [(0, jump_world)],
    "witchspace": [(0, jump_world), (0x8316, 1), (0x8317, 1)],
    "rings": [(0, jump_world)],
    "select_system": [(0x8315, [0, 1, 2, 3, 4, 5, 6, 7, 8]), (0x8318, 1), (0x8319, 1), (0x831E, [0, 0, 1, 2]),
                      (0x8316, 1), (0x8317, 1), (0, near_centre), (0x834A, 1), (0x834B, 1), (0x834C, 1)],
    "new_system": [(0, arrival_world)],
    "jump_drive": [(0xB0DD, [0, 1, 1, 1]), (0xAF14, [0, 0, 1]), (0xAF56, [0x30, 0x30, 0x2F, 4]), (0xAE20, [0, 1]),
                   (0x7680, [0, 0, 1]), (0, far_masses), (0x76B5, [36, 36, 36, 2, 3, 4])],
    "missile_lock": [(0, ship_in_sights), (0, ship_in_sights), (0x54CA, [1, 1, 1, 0, 2]), (0x4801, [0, 2]),
                     (0, lock_roles)],
    "dust_reset": [(0x805A, [0, 0x100, 0x128, 0x28])],
    "dashboard": [(0x54C8, [0, 0xFF, 0x100, 0x1FF, 0x200, 0x2FF, 0x300, 0x3FE, 0x3FF]), (0x54C1, [0, 0x7F, 0x80, 0xBF, 0xC0, 0xDF, 0xE0]),
                  (0x54C3, [0x1F, 0x20, 0x27, 0x28, 0x7F, 0x80, 0xFF]), (0x54C4, [0, 1, 0x7F, 0x80, 0xFF]),
                  (0x54C5, [0, 1, 0x7F, 0x80, 0xFF]), (0x54C2, [0, 1, 2, 0x80]), (0x835F, [0, 1]), (0xB126, [0, 0, 1]),
                  (0xAE23, [0, 0, 1]), (0x54C0, [0, 1]), (0, something_close), (0, dashboard_world)],
    "ai": [(0, ai_handlers), (0, ai_world), (0xB0DD, [0, 0, 1]), (0x83A4, [0, 0, 0, 5]), (0x83A9, [0, 0, 0, 3]), (0x83B3, [0, 1]),
           (0x83A7, [0, 0, 1]), (0x83AA, [0, 0, 1]), (0x83B1, [0, 1]), (0x83AB, [0, 0, 1]), (0x7680, [0, 1]),
           (0x83A0, [0, 4, 5, 6]), (0x83A2, [0, 2, 3]), (0x83B0, [0, 1]), (0x839E, [0, 5, 0x0D]),
           (0x839F, [0, 1]), (0x83A3, [0, 7]), (0x8329, [7, 7, 3])],
    "equip_screen": [(0, trading), (0, equip_world)],
    "chart_session": [(0, chart_world)],
    "pause_session": [(0, pause_world)],
    "title_open": [(0, title_world)],
    "title_session": [(0, title_world)],
    "save_session": [(0, files_world)],
    "load_session": [(0, files_world)],
    "start_game": [(0, start_world), (0, arrival_dialogs)],
    "data_screen": [(0, chart_world), (0x031D, [0, 1]), (0xAE60, [0, 0, 3]), (0x831E, [0, 1])],
    "equip_session": [(0, trading), (0, equip_world), (0, equip_keys)],
    "collisions": [(0, something_close), (0, docking_approach), (0x83AA, [0, 0, 1]), (0xAE23, [0, 0, 0, 1]),
                   (0x54C4, [0, 10, 0x80, 0xFF]), (0x54C8, [0, 0x10, 0x200, 0x3FF])],
    "enemy_fire": [(0, attacker), (0x7612, [1, 1, 1, 0]), (0x7681, [0, 0x80]), (0x54C4, [0, 5, 14, 15, 16, 0xFF]),
                   (0x54C5, [0, 5, 14, 15, 16, 0xFF]), (0x54C8, [0, 1, 0x10, 0x3FF])],
    "controls": [(0, autopilot_world), (0x020D + 0x48, [0, 0x80, 0x80]), (0x020D + 0x50, [0, 0x80, 0x80]),
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


FRAME_DL = [0]


def text_bytes(e, si):
    """A text as 2e6d walks it: codes 1 and 2 carry 1 and 4 data bytes; the NUL included."""
    out = bytearray()
    while len(out) < 512:
        c = e.r8(si + len(out))
        out.append(c)
        if c == 0:
            break
        for _ in range(1 if c == 1 else 4 if c == 2 else 0):
            out.append(e.r8(si + len(out)))
    return out.hex()


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
    e.hook(0x4D6C, lambda e, r: sounds.append(f"event 6:{r['ax'] & 0xFF}"))  # the music driver switched
    for at, kind in ((0x397C, 7), (0x3981, 8)):  # the screen under a box or the top line kept, put back
        e.hook(at, lambda e, r, kind=kind: sounds.append(f"event {kind}:{1 if r['ax'] == 0x18 else 2}"))
    if NAME in ("arrive", "countdowns", "commands"):  # the frame wait, view clearing, crosshair: frontend's
        for stub in (0x301A, 0x3130, 0x4F34):
            e.hook(stub, lambda e, r: None)
    if NAME == "key_bar":  # icon redraws
        def icon(e, r):  # only the bar's own (from 0312); the Esc menu's marks are the frontend's
            if e.mu.mem_read(SS * 16 + e.mu.reg_read(UC_X86_REG_SP), 2) == b"\x15\x03":
                sounds.append(f"event 4:{((r['cx'] - 0x10) // 0x18) << 8 | (r['bx'] & 0xFF)}")
        e.hook(0x37BD, icon)
    if NAME == "loop":  # back at the top of the loop: the frame is over
        visits = []
        e.mu.hook_add(UC_HOOK_CODE, lambda mu, ad, sz, u: (visits.append(1), len(visits) > 1 and (
            left.append("frame 0"), mu.emu_stop())), begin=CS * 16 + 0xA040, end=CS * 16 + 0xA040)
    if NAME in ("loop", "frame"):  # bar icons
        def bar_icon(e, r):
            if e.mu.mem_read(SS * 16 + e.mu.reg_read(UC_X86_REG_SP), 2) == b"\x15\x03":
                sounds.append(f"event 4:{((r['cx'] - 0x10) // 0x18) << 8 | (r['bx'] & 0xFF)}")
        e.hook(0x37BD, bar_icon)
    if NAME in ("commands", "loop"):
        if NAME == "commands":  # the bar's icons as events; the cockpit (763e) is drawing
            def cmd_icon(e, r):
                if e.mu.mem_read(SS * 16 + e.mu.reg_read(UC_X86_REG_SP), 2) == b"\x15\x03":
                    sounds.append(f"event 4:{((r['cx'] - 0x10) // 0x18) << 8 | (r['bx'] & 0xFF)}")
            e.hook(0x37BD, cmd_icon)
        done = "cmd 2" if NAME == "commands" else "frame 2"
        for at in (0x8DAC, 0x9124, 0x90B7, 0x92D3, 0x5C80, 0x595A, 0x8AFA, 0x0DF6, 0x0AAC, 0x0AEF, 0x08E4):  # screens up
            e.mu.hook_add(UC_HOOK_CODE, lambda mu, ad, sz, u: (left.append(done), mu.emu_stop()),
                          begin=CS * 16 + at, end=CS * 16 + at)
        paused = "cmd 3" if NAME == "commands" else "frame 4"
        e.mu.hook_add(UC_HOOK_CODE, lambda mu, ad, sz, u: (left.append(paused), mu.emu_stop()),
                      begin=CS * 16 + 0x0480, end=CS * 16 + 0x0480)  # the pause menu is up
        e.on_intr = fake_dos({}, [])  # no commander files
        for stub in (0x0674, 0x0736, 0x0779):  # not reconstructed yet
            e.mu.hook_add(UC_HOOK_CODE, lambda mu, ad, sz, u, stub=stub: (
                sounds.append(f"event {EV_UNPORTED}:{stub}"),
                left.append("frame 1" if stub == 0x6864 else done), mu.emu_stop()),
                begin=CS * 16 + stub, end=CS * 16 + stub)
    if NAME in ("frame", "loop", "launch", "dock"):
        for stub in (0x301A, 0x3130):
            e.hook(stub, lambda e, r: None)
    if NAME in ("frame", "loop"):  # the original's DL at the AI, passed to the core
        e.mu.hook_add(UC_HOOK_CODE, lambda mu, ad, sz, u: FRAME_DL.__setitem__(0, mu.reg_read(REGS["dx"]) & 0xFF),
                      begin=CS * 16 + 0x77E0, end=CS * 16 + 0x77E0)
    if NAME in ("launch", "dock", "loop", "commands"):  # the launch sound and its wait
        def launch_sound(e, r):
            if e.r8(0x4801) != 2 and e.r8(0x45E7) == 0:
                sounds.append(f"event {EV_SOUND}:17")
                sounds.append(f"event 5:{0x78 if e.r8(0x4801) else 0x23A}")
        e.hook(0x4E5A, launch_sound)
        e.hook(0x028D, lambda e, r: None)  # the mouse driver
    def pixel(e, r):
        x = (r["ax"] + (r["ax"] >> 2) - 8) & 0xFFFF
        if x < 0x130 and r["bx"] < 0x7C:
            prims.append(f"8:{e.r8(r['si'] + 6) & 0xF},{x},{r['bx']}")
    e.hook(0x2973, pixel)
    for at, shadow in ((0x2E6D, 0), (0x2EC0, 1)):  # texts (observed, not replaced)
        e.mu.hook_add(UC_HOOK_CODE, lambda mu, ad, sz, u, shadow=shadow: prims.append(
            f"text {s16(mu.reg_read(REGS['bx']))},{s16(mu.reg_read(REGS['cx']))},{e.r8(0x10A2)},{shadow}:"
            + text_bytes(e, mu.reg_read(REGS['si']))), begin=CS * 16 + at, end=CS * 16 + at)
    if NAME in ("tribbles", "status", "market", "market_session", "equip_screen", "equip_session", "chart_session", "data_screen", "pause_session", "start_game", "save_session", "load_session", "title_open", "title_session"):
        def sprite_or_icon(e, r):
            sp = SS * 16 + e.mu.reg_read(UC_X86_REG_SP)
            if e.mu.mem_read(sp, 2) == b"\x15\x03" or e.mu.mem_read(sp, 2) == b"\xce\x37" and e.mu.mem_read(sp + 8, 2) == b"\x15\x03":  # the bar's (0312, through 37bd on EGA/VGA)
                sounds.append(f"event 4:{((r['cx'] - 0x10) // 0x18) << 8 | (r['bx'] & 0xFF)}")
            else:
                prims.append(f"10:{r['bx'] & 0xFF},{s16(r['cx'])},{s16(r['dx'])}")
        e.hook(0x3411, sprite_or_icon)
    if NAME == "start_game":  # the music stops; the time of day from ds:ff30
        e.hook(0x4D55, lambda e, r: sounds.append("event 6:1"))
        e.hook(0x4AC0, lambda e, r: None)

        def clock(mu, ad, sz, u):
            t = image[DS * 16 + 0xFF30:DS * 16 + 0xFF34]
            mu.reg_write(REGS["cx"], t[0] << 8 | t[1])
            mu.reg_write(REGS["dx"], t[2] << 8 | t[3])
            mu.reg_write(UC_X86_REG_IP, 0x7264)
        e.mu.hook_add(UC_HOOK_CODE, clock, begin=CS * 16 + 0x7260, end=CS * 16 + 0x7260)
    if NAME in ("status", "start_game"):  # scripted keys for the waits (ds:ff10, then Y); no palette cycling
        keys = list(image[DS * 16 + 0xFF10:DS * 16 + 0xFF18])

        def key(e, r):
            k = keys.pop(0) if keys else ord("Y")
            e.mu.mem_write(DS * 16 + 0x0D2F, b"\xff")
            return {"ax": (r["ax"] & 0xFF00) | k, "flags": r["flags"] | 1}
        e.hook(0x0276, key)
        e.hook(0x3BB1, lambda e, r: None)
        e.hook(0x3821, lambda e, r: None)  # the palette (waits for the retrace)
    if NAME == "pause_session":  # a key at each pass (0480) or question (0aac, 0aef), 12 keys
        pkeys = list(image[DS * 16 + 0xFF10:DS * 16 + 0xFF1C])

        def pause_key(mu, ad, sz, u):
            if not pkeys:
                left.append("end")
                mu.emu_stop()
                return
            mu.mem_write(DS * 16 + 0x0D2F, bytes([pkeys.pop(0)]))
        for at in (0x0480, 0x0AAC, 0x0AEF):
            e.mu.hook_add(UC_HOOK_CODE, pause_key, begin=CS * 16 + at, end=CS * 16 + at)
    written = []
    if NAME in ("save_session", "load_session"):  # the keys (0276, 32 of them) and a fake DOS
        fkeys = list(image[DS * 16 + 0xFF10:DS * 16 + 0xFF30])
        files = disk(image)

        def file_key(e, r):
            if not fkeys:
                left.append("end")
                e.mu.emu_stop()
                return
            e.mu.mem_write(DS * 16 + 0x0D2F, b"\xff")
            return {"ax": (r["ax"] & 0xFF00) | fkeys.pop(0), "flags": r["flags"] | 1}
        e.hook(0x0276, file_key)
        e.hook(0x4D55, lambda e, r: sounds.append("event 6:1"))
        e.hook(0x4AC0, lambda e, r: None)

        e.on_intr = fake_dos(files, written)
    if NAME in ("title_open", "title_session"):  # drawn on both pages (EGA, VGA): the second is skipped
        def second_page(mu, ad, sz, u):
            ret = struct.unpack("<H", mu.mem_read(SS * 16 + mu.reg_read(UC_X86_REG_SP), 2))[0]
            mu.reg_write(UC_X86_REG_SP, mu.reg_read(UC_X86_REG_SP) + 2)
            mu.reg_write(UC_X86_REG_IP, ret)
        for at in (0x2F2D, 0x2F64, 0x37D1):  # 2f12, 2f4d, 37bd
            e.mu.hook_add(UC_HOOK_CODE, second_page, begin=CS * 16 + at, end=CS * 16 + at)
    if NAME == "title_open":  # keys at 0276 (ffh: none); one page drawn; no hardware
        tkeys = list(image[DS * 16 + 0xFF10:DS * 16 + 0xFF1C])

        def title_key(e, r):
            if not tkeys:
                left.append("end")
                e.mu.emu_stop()
                return
            k = tkeys.pop(0)
            if k == 0xFF:
                return {"flags": r["flags"] & ~1}
            e.mu.mem_write(DS * 16 + 0x0D2F, b"\xff")
            return {"ax": (r["ax"] & 0xFF00) | k, "flags": r["flags"] | 1}
        e.hook(0x0276, title_key)
        e.hook(0x4D21, lambda e, r: sounds.append("event 6:2"))
        for stub in (0x30DC, 0x3821, 0x3941, 0x3956, 0x3B3E, 0x3130, 0x301A):
            e.hook(stub, lambda e, r: None)

    if NAME == "title_session":  # a key at each pass (9f21), 12 passes
        tkeys = list(image[DS * 16 + 0xFF10:DS * 16 + 0xFF1C])

        def title_pass(mu, ad, sz, u):
            if not tkeys:
                left.append("end")
                mu.emu_stop()
                return
            mu.mem_write(DS * 16 + 0x0D2F, bytes([tkeys.pop(0)]))
        e.mu.hook_add(UC_HOOK_CODE, title_pass, begin=CS * 16 + 0x9F21, end=CS * 16 + 0x9F21)
        e.hook(0x4D21, lambda e, r: sounds.append("event 6:2"))
        for stub in (0x3130, 0x301A, 0x028D):
            e.hook(stub, lambda e, r: None)
        for stub in (0x0674, 0x0736, 0x0779):  # not reconstructed yet
            e.mu.hook_add(UC_HOOK_CODE, lambda mu, ad, sz, u, stub=stub: (
                sounds.append(f"event {EV_UNPORTED}:{stub}"), left.append("end"), mu.emu_stop()),
                begin=CS * 16 + stub, end=CS * 16 + stub)
    if NAME == "chart_session":  # a key and the arrows held at each pass (5c80, 595a), 12 passes
        ckeys = list(image[DS * 16 + 0xFF10:DS * 16 + 0xFF1C])
        carrows = list(image[DS * 16 + 0xFF20:DS * 16 + 0xFF2C])

        def chart_pass(mu, ad, sz, u):
            if not ckeys:
                left.append("end")
                mu.emu_stop()
                return
            a = carrows.pop(0)
            for j, b in enumerate((0xB255, 0xB257, 0xB259, 0xB25B)):
                ptr = e.r8(b) | e.r8(b + 1) << 8
                mu.mem_write(DS * 16 + ptr, bytes([0 if a >> j & 1 else 0x80]))
            mu.mem_write(DS * 16 + 0x0D2F, bytes([ckeys.pop(0)]))
        for at in (0x5C80, 0x595A):
            e.mu.hook_add(UC_HOOK_CODE, chart_pass, begin=CS * 16 + at, end=CS * 16 + at)

        def typing(mu, ad, sz, u):  # each turn of the text's loop takes the next key
            if not ckeys:
                left.append("end")
                mu.emu_stop()
                return
            carrows.pop(0)
            mu.mem_write(DS * 16 + 0x0D2F, bytes([ckeys.pop(0)]))
        e.mu.hook_add(UC_HOOK_CODE, typing, begin=CS * 16 + 0x0DF6, end=CS * 16 + 0x0DF6)
        e.mu.hook_add(UC_HOOK_CODE, lambda mu, ad, sz, u: (mu.reg_read(REGS["ax"]) < 0x130 and mu.reg_read(REGS["bx"]) < 0x7C)
                      and prims.append(f"8:{mu.reg_read(REGS['cx']) & 0xFF},{mu.reg_read(REGS['ax'])},{mu.reg_read(REGS['bx'])}"),
                      begin=CS * 16 + 0x291B, end=CS * 16 + 0x291B)  # pixels
        e.hook(0x3956, lambda e, r: None)  # the display pages
        e.hook(0x3941, lambda e, r: None)
        e.hook(0x396E, lambda e, r: None)
    if NAME == "equip_session":  # a scripted key at each pass (92d3) or dialog loop (9502, 968f)
        keys = list(image[DS * 16 + 0xFF10:DS * 16 + 0xFF1C])

        def equip_key(mu, ad, sz, u):
            if not keys:
                left.append("end")
                mu.emu_stop()
                return
            mu.mem_write(DS * 16 + 0x0D2F, bytes([keys.pop(0)]))
        for at in (0x92D3, 0x9502, 0x968F):
            e.mu.hook_add(UC_HOOK_CODE, equip_key, begin=CS * 16 + at, end=CS * 16 + at)
    if NAME == "market_session":  # a scripted key at the start of each pass (9124), 12 passes
        keys = list(image[DS * 16 + 0xFF10:DS * 16 + 0xFF1C])

        def pass_start(mu, ad, sz, u):
            if not keys:
                left.append("end")
                mu.emu_stop()
                return
            mu.mem_write(DS * 16 + 0x0D2F, bytes([keys.pop(0)]))
        for at in (0x9124, 0x90B7):  # docked, in flight
            e.mu.hook_add(UC_HOOK_CODE, pass_start, begin=CS * 16 + at, end=CS * 16 + at)
    if NAME in ("status", "market", "market_session", "equip_screen", "equip_session", "chart_session", "data_screen", "pause_session", "start_game", "save_session", "load_session", "title_open", "title_session"):  # rects
        e.hook(0x2FD4, lambda e, r: prims.append(
            f"rect {e.r8(0x10A2)}:{s16(r['ax'])},{s16(r['bx'])},{s16(r['cx'])},{s16(r['dx'])}"))
    e.hook(0x2576, lambda e, r: prim(6, [r["cx"], r["ax"], r["dx"], r["bx"]]))  # clipped line
    try:
        if NAME == "explode":
            regs = dict(regs, di=0x76DE + 0x40 * image[DS * 16 + 0xFF00] % (0x40 * 36))
        e.call(addr, max_insns=100_000_000 if NAME == "title_session" else 10_000_000, **regs)  # 12 title passes
    except RuntimeError:
        if not left:
            raise
    if NAME == "pause_session" and not left:
        left.append("end")
    if NAME in ("chart_session", "save_session", "load_session", "title_open", "title_session") and not left:
        left.append("end")
    if NAME == "commands" and not left:
        left.append("cmd 0")
    if NAME == "tunnel" and not left:
        left.append("end 0")
    if NAME in ("buy", "sell", "equip", "dashboard", "arrive", "countdowns", "launch", "dock"):  # the screens' drawing is the frontend's
        prims, spans = [], []
    return bytes(e.mu.mem_read(DS * 16, 0x10000)), prims + spans + left + sounds + written


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
        if NAME in ("frame", "loop"):  # the C side gets the original's DL at the AI
            before = before[:0xFF00] + bytes([FRAME_DL[0]]) + before[0xFF01:]
            want = want[:0xFF00] + bytes([FRAME_DL[0]]) + want[0xFF01:]
            want_prims = [l for l in want_prims if l != "end"]
        inf, outf = os.path.join(tmp, "in"), os.path.join(tmp, "out")
        open(inf, "wb").write(before)
        out = subprocess.run([TOOL, NAME, inf, outf], capture_output=True, text=True, check=True).stdout
        got, got_prims = open(outf, "rb").read(), out.splitlines()
        if NAME in ("frame", "loop", "commands"):  # drawing is checked per subsystem; here state and events
            want_prims = [l for l in want_prims if l.startswith(("event", "frame", "cmd"))]
            got_prims = [l for l in got_prims if l.startswith(("event", "frame", "cmd"))]
        diff = [i for i in range(0x10000) if mask[i] and want[i] != got[i]]
        for i in range(0x10000):
            if not mask[i] and want[i] != before[i] and not any(a <= i <= b for a, b in SCRATCH):
                unmodelled[i] += 1
        if diff or want_prims != got_prims:
            bad += 1
            if bad <= SHOW:
                print(f"{os.path.basename(path)}{'' if variant is None else ' fuzz ' + str(variant)}: {len(diff)} modelled bytes differ "
                      f"{[f'{i:x}:{want[i]:02x}/{got[i]:02x}' for i in diff[:12]]}, primitives {len(want_prims)} vs {len(got_prims)}"
                      + ("" if want_prims == got_prims else " (differ)"))
                if want_prims != got_prims and SHOW > 3:
                    for k, (a, b) in enumerate(zip(want_prims + ["-"] * 99, got_prims + ["-"] * 99)):
                        if a != b:
                            print(f"    first difference at {k}: want {a} got {b}")
                            break
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
