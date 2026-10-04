"""Subsystem test: one routine of the original against the core's, on game states (corpus.py).

usage: subtest.py NAME [corpus glob] [path/to/ep_subsys] [--fuzz N] [--show N]
       subtest.py --list          the routines' names (the keys of ROUTINES), one a line

NAME is a key of ROUTINES; the glob defaults to corpus/*.bin, the tool to build/ep_subsys. --show
N prints the first N differing states (default 3; above 3, also the first differing line). With
EP_DUMP=path set, both sides' lines of the last state go to path.want (the original) and path.got
(the core). Exit status 1 when any state differs.

How a state flows: a corpus image (the whole 1 MB memory of the running original) is loaded;
PREPARE sets it up for routines that need it on every state (the AdLib ones); with --fuzz N it
is also tried N times with random values in the fields the routine reads (FUZZ), to reach every
branch. The original's routine then runs in the emulator (eliteemu.py) on the whole image, the
core's (ep_subsys) on the data segment loaded through tests/statemap.c. Compared: the
data-segment bytes the core models ("modelled bytes": the mask `ep_subsys mask` writes), the
primitives drawn, the events (sounds, flips, icons, ...), the sound hardware written, the exits
taken. The bytes the original changed that the core does not model and that are not SCRATCH
(space the original reuses within a routine, not state) are listed: what to reconstruct next.
"""

import collections
import glob
import os
import random
import subprocess
import sys
import tempfile
from collections.abc import Callable, Iterable
from pathlib import Path
from typing import NamedTuple, Protocol

from unicorn import UC_HOOK_CODE
from unicorn.unicorn_py3.unicorn import Uc  # unicorn.Uc, with its annotations (see eliteemu.py)
from unicorn.x86_const import UC_X86_REG_EFLAGS, UC_X86_REG_IP, UC_X86_REG_SP

from corpus import load
from eliteemu import CS, DS, LOAD, REGS, SS, CodeHook, Elite, HookFn, IntrFn, Regs

type Memory = bytes  # an image of the original's whole memory (1 MB): a corpus state
type World = Callable[[bytearray, random.Random], None]  # a fuzzer: sets fields of an image
type Prepare = Callable[[bytearray], None]  # sets up every state of a routine
type FuzzSpec = int | list[int] | World  # FUZZ: n random bytes, a value from a list, or a world
type WatchFn = Callable[[Elite, Regs], object]  # noted on entry; the routine runs
type DosFiles = dict[str, tuple[int, bytes]]  # the fake disk: {name: (kind, contents)}

HERE = os.path.dirname(os.path.abspath(__file__))
ARGS = [
    a
    for i, a in enumerate(sys.argv[1:], 1)
    if not a.startswith("--") and sys.argv[i - 1] not in ("--fuzz", "--show")
]
FUZZ_N = int(sys.argv[sys.argv.index("--fuzz") + 1]) if "--fuzz" in sys.argv else 0
SHOW = int(sys.argv[sys.argv.index("--show") + 1]) if "--show" in sys.argv else 3
NAME = ARGS[0] if ARGS else ""  # none with --list
PATTERN = ARGS[1] if len(ARGS) > 1 else os.path.join(HERE, "corpus", "*.bin")
TOOL = ARGS[2] if len(ARGS) > 2 else os.path.join(HERE, "..", "..", "build", "ep_subsys")
os.environ["EP_ORIGINAL"] = os.path.join(HERE, "..", "..", "original")  # where ep_subsys reads the music

# Scratch space the original reuses within a routine (not state): INT 0 resume address, draw
# parameters, matrices and model temporaries, rotation temporary; and sound state (the core
# reports sounds as events).
SCRATCH = [
    (0x0002, 0x002C), (0x63F2, 0x6401), (0x92D4, 0x92DE), (0x92FA, 0x92FA), (0x8D00, 0x8D09),
    (0xA3A0, 0xA40F), (0xACA8, 0xACAF), (0xACB1, 0xACB3),
    (0x031B, 0x031E), (0x03F2, 0x03F2),
    (0x01F8, 0x01F9), (0x1074, 0x108E), (0x1091, 0x10BB), (0x10BD, 0x10C9), (0x28D0, 0x28E5),
    (0x2B66, 0x2BF5), (0x2CB1, 0x2CB2),
    (0x76D6, 0x76D7), (0x45E8, 0x45E9), (0x45EB, 0x45FF), (0x4FE0, 0x4FE0),
    (0x1F15, 0x1F16),  # 1f15: the flash colour (3921)
]  # fmt: skip

EV_SOUND, EV_SURFACE, EV_UNPORTED, EV_FLIP = 1, 2, 3, 9

# Sound routines that wrap 4c98 (some skip the sound depending on audio state, which the core
# does not keep): logged with the sound number they pass.
# Routines the core does not reconstruct yet: stubbed and logged on both sides.
UNPORTED: list[int] = []  # routines the core reports as EP_EV_UNPORTED instead of running
UNPORTED_FOR: dict[str, list[int]] = {}


class Routine(NamedTuple):
    """A routine of the original: where it starts, the registers it is entered with, and the
    exits - where it leaves without returning (the core reports them as a result line)."""

    addr: int
    regs: Regs
    exits: dict[int, str]


class Poke(Protocol):
    """A writer of data-segment fields (ds_poke)."""

    def __call__(self, a: int, v: int, n: int = 1, /) -> None: ...


def ds_poke(img: bytearray) -> Poke:
    """w(a, v, n=1): v as n little-endian bytes (wrapped) at ds:a of img."""

    def w(a: int, v: int, n: int = 1) -> None:
        img[DS * 16 + a : DS * 16 + a + n] = (v & (256**n - 1)).to_bytes(n, "little")

    return w


# name -> (address, registers, {exit address: line printed}) - exits are where a routine
# leaves without returning (the core reports them as a result line instead)
# fmt: off
ROUTINES: dict[str, Routine] = {
    "update_objects": Routine(0x4154, {}, {}),
    "message": Routine(0x702A, {}, {}),
    "fuel_leak": Routine(0x75D5, {}, {}),
    "energy_drain": Routine(0xA52F, {}, {}),
    "laser": Routine(0xA183, {}, {}),
    "tunnel": Routine(0xA0CC, {}, {0xA0E9: "end 1"}),
    "controls": Routine(0xA63D, {}, {}),
    "laser_hits": Routine(0xAC52, {}, {}),
    "collisions": Routine(0x66D6, {}, {}),
    "enemy_fire": Routine(0xAE50, {}, {}),
    "ai": Routine(0x77E0, {}, {}),
    "dashboard": Routine(0x549F, {}, {}),
    "dust": Routine(0x4FA3, {}, {}),
    "dust_reset": Routine(0x5374, {}, {}),
    "tribbles": Routine(0x1221, {}, {}),
    "missile_lock": Routine(0xA3F4, {}, {}),
    "jump_drive": Routine(0xA5EE, {}, {}),
    "flight_start": Routine(0x64D0, {}, {}),
    "select_system": Routine(0x5EE8, {}, {}),
    "arrive": Routine(0x72D8, {}, {}),
    "frame": Routine(0xA040, {}, {0xA073: "end"}),
    "launch": Routine(0xA027, {}, {0xA040: "end"}),
    "status": Routine(0xA012, {}, {0x8DAC: "end"}),
    "market": Routine(0x9048, {}, {0x9124: "end", 0x90B7: "end"}),
    "market_session": Routine(0x9048, {}, {}),
    "dock": Routine(0x6864, {}, {}),
    "loop": Routine(0xA040, {"di": 0x7BDE}, {0xA021: "frame 1", 0x9E80: "frame 3"}),
    "key_bar": Routine(0x0299, {}, {}),
    "commands": Routine(0x03C0, {"di": 0xD828}, {0xA040: "cmd 1"}),  # DI as the frame's flip leaves it (MCGA)
    "countdowns": Routine(0xA0ED, {}, {}),
    "jump_missions": Routine(0x753C, {}, {}),
    "witchspace": Routine(0x7500, {}, {}),
    "rings": Routine(0x7499, {}, {}),
    "new_system": Routine(0x666B, {}, {}),
    "explode": Routine(0x7EA8, {}, {}),
    "equip_screen": Routine(0x924A, {}, {0x92D3: "end"}),
    "chart_session": Routine(0x5AC0, {}, {0xA040: "end"}),
    "data_screen": Routine(0x8880, {}, {0x8AFA: "end"}),
    "pause_session": Routine(0x0425, {}, {0x9E80: "end", 0x00BA: "end"}),
    "start_game": Routine(0xA004, {}, {0x8DAC: "end"}),  # a040: no chart in witchspace, back to the view
    "equip_session": Routine(0x924A, {}, {}),
    "save_session": Routine(0x07AA, {}, {}),
    "load_session": Routine(0x08AB, {}, {}),  # 9e80: below
    "title_open": Routine(0x9E9A, {}, {0x9F21: "end"}),
    "protection_pick": Routine(0x32B8, {}, {}),
    "timer": Routine(0x4A50, {}, {}),
    "adlib_music": Routine(0x14A8, {}, {}),
    "adlib_fx": Routine(0x14A8, {}, {}),
    "define_keys": Routine(0x0674, {}, {}),
    "joystick": Routine(0x0736, {}, {}),
    "mouse": Routine(0x0779, {}, {}),
    "key_event": Routine(0x0215, {}, {}),
    "title_session": Routine(0x9F21, {}, {0xA004: "start", **dict.fromkeys((  # a screen up
        0x0480, 0x0DF6, 0x0945, 0x08E4, 0x0AAC, 0x0AEF, 0x8DAC, 0x9124, 0x90B7, 0x92D3, 0x5C80, 0x595A,
        0x8AFA, 0xA040, 0x0694, 0x05E5, 0x0ED5), "end")}),
}
# fmt: on


def ship_in_sights(img: bytearray, rng: random.Random) -> None:
    """A random ship in a random slot, in view near the crosshair."""
    slot = rng.randint(2, 19)
    base = DS * 16 + 0x76DE + 0x40 * slot
    t = rng.choice([0, 1, 5, 7, 22, 28, rng.randrange(30), rng.randrange(30)])
    img[base] = (t << 1) | 0x81 | (0x40 if rng.random() < 0.5 else 0)
    z = rng.choice([100, 200, 600, 1500, 4000, 12000])
    lim = 2 * z // 256 + 40
    for k, v in enumerate((rng.randint(-lim, lim), rng.randint(-lim, lim), z)):
        img[base + 0x10 + 2 * k : base + 0x12 + 2 * k] = (v & 0xFFFF).to_bytes(2, "little")
    img[base + 0x1E] = rng.choice([0, 0, 4, 0x20, 0x60, rng.getrandbits(8)])
    img[base + 0x2B] = rng.choice([0, 1, 2, 3, 4, 5, rng.getrandbits(8)])
    img[base + 0x30] = rng.choice([0, 0xFD, rng.getrandbits(8)])
    img[base + 0x31] = rng.choice([0, 0xFF, rng.getrandbits(8)])
    img[base + 0x25] = rng.choice([0, 1, 2])
    img[base + 0x3A : base + 0x3C] = rng.choice([0, 1, 0x1234]).to_bytes(2, "little")
    if rng.random() < 0.3:
        img[DS * 16 + 0xB0E1 : DS * 16 + 0xB0E3] = (0x76DE + 0x40 * slot).to_bytes(2, "little")


def something_close(img: bytearray, rng: random.Random) -> None:
    """A ship or a station near the player, possibly lined up for docking."""
    slot = rng.randint(0, 19)
    base = DS * 16 + 0x76DE + 0x40 * slot
    t = rng.choice([0, 1, 0, 1, rng.randrange(30)])
    img[base] = (t << 1) | 1 | rng.choice([0, 0x80, 0x80, 0xC0])
    r = 275 if t <= 1 else 100
    for k in range(3):
        v = rng.choice([rng.randint(-r + 1, r - 1), rng.randint(-89, 89), r, -r, rng.randint(-400, 400)])
        img[base + 4 + 2 * k : base + 6 + 2 * k] = (v & 0xFFFF).to_bytes(2, "little")
        img[base + 1 + k] = 0xFF if v < 0 else 0
    img[base + 0x0C] = rng.getrandbits(8)
    roll = rng.randrange(2048)
    img[base + 0x0E : base + 0x10] = roll.to_bytes(2, "little")
    img[base + 0x1E] = rng.choice([0, 0, 1, rng.getrandbits(8)])
    img[base + 0x31] = rng.choice([0, 0xFF, rng.getrandbits(8)])

    def near(c: int) -> int:
        return (c + rng.randint(-260, 260)) & 0x7FF

    a0 = near(rng.choice([0, 0x400]))
    angles = [a0, near(rng.choice([0, 0x400])), near(rng.choice([roll, roll + 0x400]))]
    for k, a in enumerate(angles):
        img[DS * 16 + 0x76D8 + 2 * k : DS * 16 + 0x76DA + 2 * k] = a.to_bytes(2, "little")


def scoop_world(img: bytearray, rng: random.Random) -> None:
    """Fuel scoops fitted (mostly) and something under the ship, in or near the scoop box."""
    w = ds_poke(img)
    if rng.random() < 0.2:
        return
    w(0x835C, rng.choice([1, 1, 1, 0]))
    w(0xB126, rng.choice([0, 0, 0, 3]))
    w(0x8358, rng.choice([0, 1]))
    w(0x839C, rng.choice([0, 5, 19, 20, 34, 35, 40]))
    for k in (9, 12, 13, 14, 15, 16):
        w(0x8379 + 2 * k, rng.choice([0, 3, 0xF9, 0xFA, 0xFB, 0xFE]))
    if img[DS * 16 + 0xAF14]:  # not under the docking computer (its divide-by-zero is approximated)
        return
    slot = rng.randint(2, 19)
    base = DS * 16 + 0x76DE + 0x40 * slot
    t = rng.choice([0x11, 0x11, 0x0B, 0x0B, 0x15, 0x07, 0x14, rng.randrange(30)])
    img[base] = (t << 1) | 1
    for k, v in enumerate(
        (rng.randint(-160, 160), rng.choice([rng.randint(20, 240), 30, 29, 229, 230]), rng.randint(-160, 160))
    ):
        img[base + 4 + 2 * k : base + 6 + 2 * k] = (v & 0xFFFF).to_bytes(2, "little")
        img[base + 1 + k] = 0xFF if v < 0 else 0
    img[base + 0x1E] = rng.choice([0, 0x10, 0x40, 0x50])
    if rng.random() < 0.7 and NAME == "frame":  # level, looking ahead (not in the loop, where a key may
        # turn the docking computer on)
        for k in range(3):
            w(0x76D8 + 2 * k, 0, 2)
        w(0xB0DE, 0, 2)


def dashboard_world(img: bytearray, rng: random.Random) -> None:
    """Gauges around the condition thresholds and the station around the safe-zone radius."""
    if rng.random() < 0.25:
        return

    def w8(a: int, v: int) -> None:
        img[DS * 16 + a] = v & 0xFF

    def near(c: int, d: int) -> int:
        return rng.randint(c - d, c + d)

    level = rng.randrange(3)
    energy = near((0x100, 0x200, 0x300)[level], 2)
    img[DS * 16 + 0x54C8 : DS * 16 + 0x54CA] = max(0, min(energy, 0x3FF)).to_bytes(2, "little")
    good = [rng.choice([0, 1, rng.randrange(0x7E)]) for _ in range(5)]
    w8(0x54C1, rng.choice([near((0xE0, 0xC0, 0x80)[level], 1), rng.randrange(0x7F)]))
    w8(0x54C3, rng.choice([near((0x20, 0x28, 0x80)[level], 1), 0xFE]))
    w8(0x54C4, rng.choice([near(0x80, 1), 0, 0xFF, 0x80 + good[0]]))
    w8(0x54C5, rng.choice([near(0x80, 1), 0, 0xFF, 0x80 + good[1]]))
    base = DS * 16 + 0x76DE + 0x80
    img[base] = (rng.choice([0, 1, 2]) << 1) | rng.choice([1, 1, 0x81, 0])
    big = rng.random() < 0.2
    for k in range(3):
        v = (
            rng.choice(
                [
                    near(0x32C8, 40),
                    -near(0x32C8, 40),
                    rng.randint(-0x3000, 0x3000),
                    rng.randint(-200, 200),
                    rng.randint(-0x7FFF, 0x7FFF),
                ]
            )
            if k == rng.randrange(3)
            else rng.randint(-0x1000, 0x1000)
        )
        img[base + 4 + 2 * k : base + 6 + 2 * k] = (v & 0xFFFF).to_bytes(2, "little")
        img[base + 1 + k] = rng.getrandbits(8) if big else (0xFF if v < 0 else 0)


def dust_world(img: bytearray, rng: random.Random) -> None:
    """Any view, speed, steering, invert options and jump mode, particles near the edges."""
    w = ds_poke(img)
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
            w(
                p,
                rng.choice([rng.randint(-0x2400, 0x23FF), rng.choice([0x1F80, -0x2000, 0x0500, -0x0600])]),
                2,
            )
            w(
                p + 2,
                rng.choice([rng.randint(-0x1200, 0x11FF), rng.choice([0x0F80, -0x1000, 0x0300, -0x0300])]),
                2,
            )
            w(p + 4, rng.choice([1, 2, rng.getrandbits(8)]))
        if rng.random() < 0.2:
            w(p + 5, rng.choice([0, 1]))


def lock_roles(img: bytearray, rng: random.Random) -> None:
    """Every type and class in the slots, some of them mission ships."""
    for i in range(2, 20):
        b = DS * 16 + 0x76DE + 0x40 * i
        if img[b] & 1 and rng.random() < 0.7:
            img[b] = (img[b] & 0xC1) | rng.randrange(32) << 1
            img[b + 0x33] = rng.choice([0, 1, 2, 3, 3, 4, 4, 5, 6, 7])
            img[b + 0x1E] = rng.choice([0, 2, 0x20, 0x60, 0x40])


def far_masses(img: bytearray, rng: random.Random) -> None:
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


def near_centre(img: bytearray, rng: random.Random) -> None:
    """A zoomed chart with the cursor near its middle and the centre on a busy area."""
    if rng.random() < 0.5:
        d = DS * 16
        img[d + 0x831E] = 1
        img[d + 0x8318] = 0x50 + rng.randint(-60, 60)
        img[d + 0x8319] = 0x40 + rng.randint(-40, 40)


def jump_world(img: bytearray, rng: random.Random) -> None:
    """A jump under way: galactic or not, its target, missions near their start, rings."""
    w = ds_poke(img)
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
        # fmt: off
        for k, v in enumerate([1, 0x14, 0x0C, 5, 0x0F, 0x0C, 8, 0x0B, 0x0C, 0x0A, 8, 0x0A, 0x0B, 6, 0x0A,
                               0x0C, 6, 0x0F, 0x0D, 6, 0x0F, 0x0E, 6, 0x0F, 0x0F, 6, 0x0F, 0x14, 6, 0x0C]):
            w(0x85DC + k, v)
        # fmt: on
    else:
        for k in range(10):
            w(0x85DC + 3 * k, rng.choice([0, 0, 1, 5]))
            w(0x85DD + 3 * k, rng.choice([0, 6, 0x13, 0x14, 0x80, 0x95, 0x96]))
    if rng.random() < 0.3:  # the flight generator about to misjump
        for j in range(3):
            w(0x830F + 2 * j, rng.randrange(1, 4), 2)  # never all zero (it would stay 0)


def bar_world(img: bytearray, rng: random.Random) -> None:
    """Any screen, the bar as drawn, the equipment the flight bar looks at."""
    w = ds_poke(img)
    w(0x02F9, rng.choice([0, 0, 0, 1, 2, 3, 4]))
    w(0x02FA, rng.choice([0, 0, 1, 2, 0xFF]))
    w(0x8711, rng.choice([0, 0, 1, 2]))
    ids = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xA, 0xB, 0xE, 0xF, 0x23, 0x24, 0x0C, 0x10, 0x20]
    for k in range(12):
        w(0x0301 + k, rng.choice(ids + [0xFF]))
    for a, vals in (
        (0x8357, [0, 1, 4]),
        (0x8359, [0, 1]),
        (0x835D, [0, 1]),
        (0x835E, [0, 1]),
        (0x8364, [0, 1]),
        (0x83AC, [0, 1]),
        (0x8361, [0, 1]),
        (0x54CA, [0, 1, 2]),
        (0xAE60, [0, 0, 0, 3]),
        (0xAE23, [0, 0, 5]),
        (0x031D, [0, 1]),
        (0x031E, [0, 1, 2, 4, 8, 0xF]),
    ):
        w(a, rng.choice(vals))


def command_world(img: bytearray, rng: random.Random) -> None:
    """A key pressed, and the state the flight commands test."""
    w = ds_poke(img)
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
    for a, vals in (
        (0xAF14, [0, 0, 1]),
        (0xB126, [0, 0, 1]),
        (0xB0E0, [0, 0, 1]),
        (0x83AA, [0, 0, 1]),
        (0x83AB, [0, 1]),
        (0x83B1, [0, 1]),
        (0xAE25, [0, 0, 3]),
        (0xAE60, [0, 0, 0, 2]),
        (0xAE23, [0, 0, 3]),
        (0xB1F8, [0, 0, 1]),
        (0xB3D5, [0, 0, 5]),
        (0x7680, [0, 1, 1]),
        (0x8360, [0, 1]),
        (0xB139, [0, 1]),
        (0x8711, [0, 0, 0, 1]),
        (0xAF56, [0, 4, 0x14]),
        (0x8356, [0, 0x23, 0x46, 0xFF]),
    ):
        w(a, rng.choice(vals))
    w(0x54C8, rng.choice([0x10, 0x77, 0x78, 0x2FF, 0x300, 0x3FF]), 2)
    w(0x8343, rng.choice([0, 1, 0x10, 0x46, 0x47, 0x80]), 2)
    w(0x8367, rng.choice([0, 0x1F3, 0x1F4, 100000]), 4)
    w(0x775E + 0x1E, rng.choice([0, 1, 4]))
    w(0xB0E1, 0x76DE + 0x40 * rng.randint(2, 19), 2)  # a locked missile has a target
    if rng.random() < 0.3:
        for j in range(3):
            w(0x830F + 2 * j, rng.randrange(1, 4), 2)  # never all zero (it would stay 0)


def autopilot_world(img: bytearray, rng: random.Random) -> None:
    """The docking computer in any of its states, the station around."""
    if rng.random() < 0.3:
        return
    w = ds_poke(img)
    w(0xAF14, 1)
    w(0xAF17, rng.randrange(13))
    w(0xAF5B, rng.choice([0, 1]))
    w(0xAF59, rng.randrange(0x800), 2)
    w(0xAF56, rng.choice([0, 4, 8, 0x14, 0x30, 2]), 2)
    b = 0x76DE + 0x80
    w(b, (rng.choice([0, 1]) << 1) | 1)
    r = rng.choice([300, 700, 2000, 5000, 9000])
    for k in range(3):
        v = (
            rng.randint(-r, r)
            if k < 2
            else rng.choice([rng.randint(-r, r), -rng.randint(0x200, 0x400), -0x28A, -0x289, -0x3E8])
        )
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


def status_world(img: bytearray, rng: random.Random) -> None:
    """Docking with any equipment, cash, legal status, kills, Tribbles; no dialogs (yet)."""
    w = ds_poke(img)
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


def market_world(img: bytearray, rng: random.Random) -> None:
    """Docked or in flight, the market drawn already or not, any cargo, any economy."""
    w = ds_poke(img)
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


def start_world(img: bytearray, rng: random.Random) -> None:
    """Any time of day; a saved commander a little different from the current one."""
    w = ds_poke(img)
    for j, top in enumerate((24, 60, 60, 100)):
        w(0xFF30 + j, rng.randrange(top))
    b = 0x83BE  # fields of the saved commander (its texts stay valid)
    w(b + 0x7B, rng.choice([0, 0x46, 0xFF]))  # fuel
    for k in range(14):
        w(b + 0x7C + k, rng.choice([0, 0, 1]))  # equipment
    w(b + 0x8C, rng.choice([0, 1000, 99999, 0x10000]), 4)  # cash
    w(b + 0x90, rng.choice([0, 5, 0x28, 0xFF]))  # legal status
    w(b + 0xC5, rng.choice([0, 0, 1, 3, 4]))  # mission
    w(b + 0xD5, rng.choice([0, 0, 1]))  # mission phase
    w(b + 0x3A, rng.randrange(8))  # galaxy
    w(0x83BE + 0x93, rng.choice([0, 1, 9, 0x14]), 2)  # its kills (836e) and the current ones
    w(0x83BE + 0x91, rng.choice([0, 1, 9, 0x14, 0x15]), 2)


FILES = 0xFC00  # the fake disk: a count, then 16 bytes a file (name, NUL, kind, a byte to vary it)
FILE_GOOD, FILE_BAD_SUM, FILE_SHORT, FILE_NO_OPEN, FILE_READ_ONLY = range(5)


def disk(image: Memory) -> DosFiles:
    """The files ds:fc00 describes: {name: (kind, contents)}, in directory order. A commander file
    is the commander in the state with its cash and fuel varied (tests/subsys.c makes the same)."""
    files: DosFiles = {}
    base = bytearray(image[DS * 16 + 0x82DB : DS * 16 + 0x82DB + 0xE2])
    for j in range(image[DS * 16 + FILES]):
        at = DS * 16 + FILES + 0x10 + 0x10 * j
        name = bytes(image[at : at + 13]).split(b"\0")[0].decode()
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


def fake_dos(files: DosFiles, written: list[str]) -> IntrFn:  # noqa: C901 - a branch per DOS function
    """int 21h for the commander files: {name: (kind, contents)}; what is written is noted."""
    found: list[str] = []
    handles: dict[int, str] = {}

    def string(e: Elite, at: int) -> str:
        out = b""
        while e.r8(at + len(out)):
            out += bytes([e.r8(at + len(out))])
        return out.decode("latin-1")

    def dos(e: Elite, intno: int) -> bool:  # noqa: C901 - a branch per DOS function
        if intno != 0x21:
            return False
        mu = e.mu
        ax, bx, cx, dx = (int(mu.reg_read(REGS[k])) for k in ("ax", "bx", "cx", "dx"))
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
        fl = int(mu.reg_read(UC_X86_REG_EFLAGS))
        mu.reg_write(UC_X86_REG_EFLAGS, fl | 1 if fail else fl & ~1)
        return True

    return dos


def files_world(img: bytearray, rng: random.Random) -> None:
    """Commander files on the disk (none to a screenful and more), good and bad; a name to save."""
    w = ds_poke(img)
    name = rng.choice(["JAMESON", "A", "ELITE-8", "Z9", "NEWCMDR", "LAVE", "ABCDEFGH"])
    w(0x8370, 0, 9)
    img[DS * 16 + 0x8370 : DS * 16 + 0x8370 + len(name)] = name.encode()
    n = rng.choice([0, 1, 2, 3, 5, 12, 13, 14, 20, 40])
    pool: set[str] = set()
    while len(pool) < n:
        pool.add(
            "".join(rng.choice("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-") for _ in range(rng.randint(1, 8)))
        )
    names = sorted(pool)
    if names and rng.random() < 0.5:
        names[rng.randrange(len(names))] = name  # saving would overwrite it
    names = list(dict.fromkeys(names))
    w(FILES, len(names))
    for j, f in enumerate(names):
        at = FILES + 0x10 + 0x10 * j
        w(at, 0, 16)
        img[DS * 16 + at : DS * 16 + at + len(f) + 4] = (f + ".CDR").encode()
        w(
            at + 13,
            rng.choice(
                [FILE_GOOD, FILE_GOOD, FILE_GOOD, FILE_BAD_SUM, FILE_SHORT, FILE_NO_OPEN, FILE_READ_ONLY]
            ),
        )
        w(at + 14, rng.randrange(256))
    w(0x8365, rng.choice([0, 1, 1, 3]))  # lasers, a military one in front
    w(0x8366, rng.choice([0, 3, 3, 0x0F]))
    w(0x8363, rng.choice([0, 0, 1]))
    keys: list[int] = []
    if NAME == "save_session":
        if rng.random() < 0.5:
            keys += [8] * rng.randrange(4) + [ord(c) for c in rng.choice(["", "X", "LAVE", "AB-1", "x"])]
        keys += [rng.choice([0x0D, 0x0D, 0x0D, 0x1B, 0xFF])]
    if rng.random() < 0.5:  # down past the end of a long list and back up past its top
        keys += [0x50] * rng.randint(11, 16) + [0x48] * rng.randint(11, 15)
    while len(keys) < 32:
        keys.append(
            rng.choice(
                [0x48, 0x50, 0x50, 0x50, 0x0D, 0x1B, 0xFF, ord("Y"), ord("n"), ord("N"), ord("y"), 0x20, 0x20]
            )
        )
    for j in range(32):
        w(0xFF10 + j, keys[j])


def title_world(img: bytearray, rng: random.Random) -> None:
    """Any sound; keys for the waits or the passes; a title ship part way. (Not the video mode:
    its routines are chosen once, at the start.)"""
    w = ds_poke(img)
    w(0x4801, rng.choice([0, 1, 2]))
    w(0x45EA, rng.randrange(4))
    w(0x76B5, rng.choice([3, 3, 12, 36]))
    keys: list[int] = []
    # fmt: off
    while len(keys) < 12:
        if NAME == "title_session":  # mostly passes with no key; F-keys, Esc; space late if at all
            keys.append(rng.choice([0xFF] * 12 + [0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F, 0xA0,
                                                  0xA1, 0xA2, 0x1B, ord("x"),
                                                  0x20 if len(keys) > 8 else 0xFF]))
        else:
            keys += [0xFF] * rng.choice([0, 0, 1, 3]) + [rng.choice([0x20, 0x20, 0x9A, 0x9B, 0x9D, 0x9E, 0x1B,
                                                                     ord("x"), 0xFF])]
    # fmt: on
    for j in range(12):
        w(0xFF10 + j, keys[j])
    if NAME == "title_session":  # where the ship is
        w(0xB25F, rng.choice([0, 0, 1, 0x76, 0x77, 0x78]), 2)
        w(0x775E + 8, rng.choice([5000, 4900, 1000, 200]), 2)
        at = rng.choice([0, 5, 13, 22, 23, 23])  # 23: the last before the end mark
        w(0xB261, 0xB263 + at, 2)
        w(0xB1BB, img[DS * 16 + 0xB263 + at])
        w(0x775E, img[DS * 16 + 0xB263 + at] << 1 | 1)


def key_bytes(img: bytearray, rng: random.Random) -> None:
    """Presses and releases, E0h prefixes, NumLock, the shift."""
    for k in range(8):
        sc = rng.choice([rng.randrange(0x59), 0x1C, 0x39, 0x3B, 0x48, 0x2A, 0x45, 0xE0])
        img[DS * 16 + 0xFF10 + k] = sc | (0x80 if sc != 0xE0 and rng.random() < 0.4 else 0)


def controls_world(img: bytearray, rng: random.Random) -> None:
    """Keys up; the bindings set or not; a joystick or mouse there or not; keys to answer and
    scancodes to bind (some twice: taken only once)."""
    w = ds_poke(img)
    for k in range(0x80):
        w(0x020D + k, 0x80)
    if rng.random() < 0.4:
        for a in range(0xB251, 0xB25F, 2):
            w(a, 0xFFFF, 2)
    w(0xFF40, rng.choice([0, 1, 1]))  # a joystick, a mouse
    w(0xFF41, rng.randint(200, 1800), 2)
    w(0xFF43, rng.randint(200, 1800), 2)
    keys: list[int] = []
    while len(keys) < 16:
        keys.append(rng.choice([ord("Y"), ord("y"), ord("N"), ord("x"), 0x20, 0x20, 0xFF,
                                rng.randrange(1, 0x59), rng.randrange(1, 0x59), 0x48, 0x50]))  # fmt: skip
    for j in range(16):
        w(0xFF10 + j, keys[j])


def device_world(img: bytearray, rng: random.Random) -> None:
    """Keyboard, joystick or mouse control; the devices there or not, where they are."""
    w = ds_poke(img)
    w(0x8F2C, rng.choice([0, 1, 1, 2, 2]))
    w(0xFF40, rng.choice([0, 1, 1, 1]))
    for a in (0xFF41, 0xFF43):
        w(a, rng.choice([0, 1000, 1010, 1200, 1900, rng.randint(0, 2200)]), 2)
    for a in (0x09CD, 0x09CF):
        w(a, rng.choice([1000, 1000, 1005, 0, rng.randint(100, 1900)]), 2)
    w(0xFF45, rng.choice([0xFF, 0xEF, 0xDF, 0xCF, 0x00]))
    for a in (0xFF46, 0xFF48):
        w(a, rng.choice([0, 3, -3, 9, -9, 40, -40, 600, -600, rng.randint(-2000, 2000)]), 2)
    w(0xFF4A, rng.choice([0, 0, 1, 2, 3]))
    w(0x0CA8, rng.choice([0, 0, 1, 9]))
    w(0x0CAB, rng.choice([0, 0, 0x18, -0x18, 0x30]), 2)
    for k in (0x7D, 0x7E):
        w(0x020D + k, rng.choice([0x80, 0x80, 0x80, 0]))


SONG = Path(HERE, "..", "..", "original", "ADBLUE.MID").read_bytes()  # the title music (003b loads it)


def adlib(img: bytearray, rng: random.Random | None = None) -> None:
    """An AdLib (4ecf: A), the title music not on yet."""
    img[DS * 16 + 0x4801] = 1
    img[DS * 16 + 0xB5B7] = 1
    img[DS * 16 + 0x45E7] = 0


def adlib_fx(img: bytearray, rng: random.Random | None = None) -> None:
    """An AdLib; at ds:ff10, 32 times the ticks to wait and the effect to queue (of the bank's 28;
    not 10h, which only the speaker is sent: it starts effects 80h..82h, past the bank, and runs
    what their pointers land on)."""
    adlib(img)
    rng = random.Random(
        bytes(img[DS * 16 : DS * 16 + 0x100]) + bytes(img[DS * 16 + 0x45DC : DS * 16 + 0x4600])
    )
    ids = [n for n in range(28) if n != 0x10]
    alone = rng.random() < 0.35  # each effect on its own, long enough to play out
    order = rng.sample(ids, len(ids)) + rng.sample(ids, 5)
    for k in range(32):
        img[DS * 16 + 0xFF10 + 2 * k] = (
            255 if alone else rng.choice([0, 1, 3, 12, 24, 40, 100, 255, rng.randrange(256)])
        )
        img[DS * 16 + 0xFF11 + 2 * k] = order[k] if alone else rng.choice(ids)


FX_CS = 0x1390  # the effects' state in the driver's segment, 300h bytes


def fx_lines(mem: bytes | bytearray) -> list[str]:
    """The effects' state as lines to compare (cs:13c1..13c8, the vectors kept, left out)."""
    mem = bytearray(mem)
    mem[0x13C1 - FX_CS : 0x13C9 - FX_CS] = bytes(8)
    return [f"cs {FX_CS + k:04x}: {mem[k : k + 16].hex()}" for k in range(0, len(mem), 16)]


def speaker_world(img: bytearray, rng: random.Random) -> None:
    """The speaker part way through a sequence, a note, a pattern, a rest or a loop."""
    w = ds_poke(img)
    seqs = [
        int.from_bytes(img[DS * 16 + 0x4F7F + 2 * k : DS * 16 + 0x4F81 + 2 * k], "little") for k in range(12)
    ]
    pats = [
        int.from_bytes(img[DS * 16 + 0x4FCE + 2 * k : DS * 16 + 0x4FD0 + 2 * k], "little") for k in range(9)
    ]
    w(0x4801, rng.choice([2, 2, 2, 1, 0]))
    w(0x45E7, rng.choice([0, 0, 0, 1]))
    w(0x45E6, rng.choice([0, 0, 0, 1]))
    w(0x45E4, rng.choice([0, 1, 2, 0x2EE]), 2)
    w(0x45EA, rng.choice([0, 1, 2, 4, 5, 6, 8, 0x10, 0x12, 0x18, 0x0A, rng.randrange(0x20)]))
    w(0x45EB, rng.choice(seqs) + rng.choice([0, 0, 0, 3, 6]), 2)
    w(0x45EF, rng.randrange(0x60))
    w(0x45F0, rng.choice([0, 1, 2, 5, 30]))
    w(0x45F1, rng.choice([0, 0, 0, 1, 3]))
    sp = rng.choice([0x45F4, 0x45F4, 0x45F7, 0x45FA])
    w(0x45F2, sp, 2)  # mid-pattern only inside a loop (the patterns' loops balance)
    w(0x45ED, rng.choice(pats) + (rng.choice([0, 1, 2, 3]) if sp > 0x45F4 else 0), 2)
    for k in range((sp - 0x45F4) // 3):
        w(0x45F4 + 3 * k, rng.choice(pats) + rng.randrange(4), 2)
        w(0x45F6 + 3 * k, rng.randint(1, 4))
    w(0x4600, rng.choice([0, 0, 1, 4]))
    w(0x45DC, rng.getrandbits(16), 2)
    w(0x4F74, rng.choice([0xA0, 0xC0, 0xFA]))


def pause_world(img: bytearray, rng: random.Random) -> None:
    """From any screen; options toggled, sound, abandon or exit asked, space."""
    w = ds_poke(img)
    w(0x02F9, rng.choice([0, 0, 1, 2, 3]))
    w(0x02FA, rng.choice([0, 0xFF]))
    for j in range(12):
        w(
            0xFF10 + j,
            rng.choice(
                [0x9A, 0x9B, 0x9C, 0x9D, 0x9F, 0xA1, 0xA2, ord("Y"), ord("n"), ord("x"), 0xFF, 0x20, 0x20]
            ),
        )


def chart_world(img: bytearray, rng: random.Random) -> None:
    """Docked or in flight, a chart already up or not, cursors, fuel; keys and arrows per pass."""
    w = ds_poke(img)
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
    keys: list[int] = []  # single keys, or a find: F9, some letters, Enter or Esc (Esc elsewhere is the menu)
    while len(keys) < 12:
        if rng.random() < 0.3:
            keys += (
                [0x9F]
                + [
                    rng.choice([ord("L"), ord("A"), ord("V"), ord("E"), ord("Z"), 8, ord("l"), 0xFF])
                    for _ in range(rng.randrange(5))
                ]
                + [rng.choice([0x0D, 0x0D, 0x1B])]
            )
        else:
            keys.append(rng.choice([0xFF, 0xFF, 0x9E, 0xA0, 0x9A, ord("L"), 0x0D]))
    for j in range(12):
        w(0xFF10 + j, keys[j])
        w(0xFF20 + j, rng.choice([0, 0, 1, 2, 4, 8, 5, 10]))


def equip_world(img: bytearray, rng: random.Random) -> None:
    """Docked, any tech level, equipment, lasers fitted, cash, fuel, cargo."""
    w = ds_poke(img)
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


def equip_keys(img: bytearray, rng: random.Random) -> None:
    """12 keys: arrows, buy (F9/9), sell (F10/0), Enter, nothing."""
    for j in range(12):
        img[DS * 16 + 0xFF10 + j] = rng.choice(
            [0xFF, 0x48, 0x50, 0x50, 0x50, 0x9F, 0x9F, 0xA0, ord("9"), ord("0"), 0x0D, 0x0D]
        )


def market_keys(img: bytearray, rng: random.Random) -> None:
    """Docked, and 12 keys: arrows, buy (F9 or 9), sell (F10 or 0), nothing."""
    img[DS * 16 + 0x02F9] = 1
    for j in range(12):
        img[DS * 16 + 0xFF10 + j] = rng.choice([0xFF, 0x48, 0x50, 0x50, 0x9F, 0x9F, 0xA0, ord("9"), ord("0")])


def arrival_dialogs(img: bytearray, rng: random.Random) -> None:
    """Promotions, the Tribble offer, briefings and debriefings, and the keys answering them."""
    if rng.random() < 0.3:
        return
    w = ds_poke(img)
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


def arrival_world(img: bytearray, rng: random.Random) -> None:
    """Any galaxy (the hidden 8 too), system, tech, government; witchspace; docked here before."""
    w = ds_poke(img)
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


def tribble_world(img: bytearray, rng: random.Random) -> None:
    """Any number of Tribbles, cargo to eat, sprites walking to the edges."""
    w = ds_poke(img)
    w(
        0x83B5,
        rng.choice(
            [0, 1, 1, 2, 14, 15, 29, 30, 79, 80, 124, 125, 0x5E, 0x5F, 0x2AB, 0x2AC, 0x1000, 0x98C9, 0x98CA]
        ),
        2,
    )
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


def docking_approach(img: bytearray, rng: random.Random) -> None:
    """The station just ahead, nearly lined up: docking, bouncing off or crashing."""
    slot = rng.choice([1, 2, 3])
    base = DS * 16 + 0x76DE + 0x40 * slot
    img[base] = (rng.choice([0, 1]) << 1) | 0x81
    for k, v in enumerate((rng.randint(-130, 130), rng.randint(-130, 130), rng.randint(-274, 274))):
        img[base + 4 + 2 * k : base + 6 + 2 * k] = (v & 0xFFFF).to_bytes(2, "little")
        img[base + 1 + k] = 0xFF if v < 0 else 0
    img[base + 0x0C] = rng.choice([0, 0, 1])
    img[base + 0x1E] = rng.choice([0, 0, 0, 1])
    roll = rng.randrange(2048)
    img[base + 0x0E : base + 0x10] = roll.to_bytes(2, "little")

    def near(c: int) -> int:
        return (c + rng.randint(-120, 120)) & 0x7FF

    first = rng.choice([0, 0x400])
    angles = [near(first), near(0x400 - first), near(rng.choice([roll, roll + 0x400]))]
    for k, a in enumerate(angles):
        img[DS * 16 + 0x76D8 + 2 * k : DS * 16 + 0x76DA + 2 * k] = a.to_bytes(2, "little")


def trading(img: bytearray, rng: random.Random) -> None:
    """A random commander and market: cargo, cash, equipment, the docked system, a row."""
    d = DS * 16
    for k in range(17):
        img[d + 0x8379 + 2 * k] = rng.choice([0, 0, 1, 5, 0xF9, 0xFA, 0xFF, rng.getrandbits(8)])
        img[d + 0x837A + 2 * k] = rng.choice([0, 1, 7, 0xF9, 0xFA, 0xFF, rng.getrandbits(8)])
    img[d + 0x839C] = rng.choice([0, 5, 0x13, 0x14, 0x22, 0x23, 0x30])
    cash = rng.choice([0, 1, 100, 1000, 30000, 0x10000, 0x7FFFF, rng.getrandbits(20)])
    img[d + 0x8367 : d + 0x836B] = cash.to_bytes(4, "little")
    for k in range(14):
        img[d + 0x8356 + k] = rng.choice([0, 0, 0, 1, rng.getrandbits(8)])
    img[d + 0x8356] = rng.choice([0, 0x10, 0xFA, 0xFB, 0xFF, rng.getrandbits(8)])
    img[d + 0x8357] = rng.choice([0, 3, 4, 5])
    img[d + 0x8358] = rng.choice([0, 1])
    img[d + 0x835C] = rng.choice([0, 1])
    img[d + 0x8365] = rng.choice([0, 1, 0x0F, 0x0E, 0x07, rng.getrandbits(4)])
    img[d + 0x8366] = rng.getrandbits(8)
    img[d + 0x836B] = rng.choice([0, 0x20, 0xFF])
    img[d + 0x832C : d + 0x832F] = bytes([rng.randrange(8), rng.randrange(8), rng.randrange(13)])
    img[d + 0x83A0] = rng.choice([0, 0, 1, 4])
    img[d + 0xAD2B] = rng.choice([rng.randrange(17), rng.randrange(14), 0, 0, 1, 4, 5, 12, 13])
    img[d + 0x839D] = 1  # on the market screen the table is already drawn
    if NAME == "equip":  # only rows the station lists (min tech <= tech + 1)
        row = img[d + 0xAD2B] % 14
        img[d + 0xAD2B] = row
        min_tech = [1, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 10][row]
        img[d + 0x832E] = rng.randint(min_tech - 1, 12)


def exploding(img: bytearray, rng: random.Random) -> None:
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


def ai_world(img: bytearray, rng: random.Random) -> None:
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
    img[d + 0x888F : d + 0x8891] = (4 * rng.randrange(8)).to_bytes(2, "little")
    img[d + 0x8362] = rng.choice([0, 1])


def ai_handlers(img: bytearray, rng: random.Random) -> None:  # noqa: C901 - one run of random draws, in order
    """Ships of every class in every state, near the boxes their handlers test, with targets."""
    d = DS * 16
    w = ds_poke(img)

    def slot(i: int) -> int:
        return 0x76DE + 0x40 * i

    used: list[int] = []
    for i in range(2, 20):
        b = slot(i)
        if rng.random() < 0.35:
            continue
        cls = rng.choice([1, 2, 2, 3, 4, 4, 5, 5, 6, 6]) if i > 2 else 1
        types = {
            1: [0, 1],
            2: [20],
            3: [3, 1, 7, 12, 5],
            4: [9, 10, 28, 28, 5, 12, 13],
            5: [22, 7, 22, 15, 25],
            6: [15, 17, 18],
        }[cls]
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
            v = rng.choice(
                [
                    rng.randint(-r, r),
                    rng.randint(-r - 20, -r + 20),
                    rng.randint(r - 20, r + 20),
                    rng.randint(-0x7FFF, 0x7FFF),
                ]
            )
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
                    w(
                        b + 4 + 2 * k,
                        int.from_bytes(img[d + slot(j) + 4 + 2 * k : d + slot(j) + 6 + 2 * k], "little")
                        + rng.randint(-220, 220),
                        2,
                    )
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
                v = (
                    (hi << 8 | rng.choice([0xC2, 0xC3, 0xC1, rng.randrange(256)]))
                    if j == 0
                    else rng.randint(-100, 100)
                )
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


def attacker(img: bytearray, rng: random.Random) -> None:
    slot = rng.randint(2, 19)
    base = DS * 16 + 0x76DE + 0x40 * slot
    img[base] = (rng.randrange(30) << 1) | 1 | rng.choice([0, 0x80])
    for k, v in enumerate(
        (rng.randint(-3000, 3000), rng.randint(-3000, 3000), rng.choice([0, 100, 500, 4000]))
    ):
        img[base + 0x10 + 2 * k : base + 0x12 + 2 * k] = (v & 0xFFFF).to_bytes(2, "little")
    img[DS * 16 + 0x7610 : DS * 16 + 0x7612] = (0x76DE + 0x40 * slot).to_bytes(2, "little")


# name -> data-segment fields (address, size) given random values when fuzzing; a value list
# picks from interesting values instead of all
# fmt: off
FUZZ: dict[str, list[tuple[int, FuzzSpec]]] = {
    "message": [(0x8058, [0x81FE, 0x8212, 0x802C]), (0x805A, 1), (0x805B, [0, 0xFF]),
                (0x8056, [0x81FE, 0x802C]), (0xB0DE, [0, 0x200, 0x400, 0x600, 0x100]), (0xB126, [0, 0, 1]),
                (0x81F4, [0, 0, 1, 5]), (0x81F5, 1), (0x81F2, [0x81FE, 0x8212]), (0x8892, [0, 1, 2]),
                (0x54C3, [0x10, 0x31, 0x32, 0xFE]), (0x54C1, [0x10, 0xE0, 0xE1]),
                (0x54C8, [0xFF, 0x100, 0x3FF])],
    "fuel_leak": [(0x83A5, [0, 0, 1, 2, 9]), (0x83A6, [0, 1, 2, 0x33]), (0x8356, [0, 3, 5, 6, 0x46, 0xFF])],
    "energy_drain": [(0xB139, [0, 1, 2]), (0x54C8, [0, 1, 2, 3, 0x3FF])],
    "laser": [(0xB126, [0, 0, 1]), (0xAE23, [0, 0, 1]), (0x020D + 0x39, [0, 0x80]), (0x8365, 1), (0x8366, 1),
              (0xB0DE, [0, 0x200, 0x400, 0x600]), (0x54C2, [0, 0xEB, 0xEF, 0xF0, 0xFC]), (0xB3D3, [0, 0, 1]),
              (0xB125, [0, 1]), (0, device_world)],
    "tunnel": [(0xAE23, [0, 1, 2, 30]), (0x83B5, [0, 0, 5])],
    "laser_hits": [(0, ship_in_sights), (0, ship_in_sights), (0xB0E4, [1, 1, 1, 0]), (0xB0E3, [0, 1, 2, 3]),
                   (0x83AA, [0, 0, 1]), (0x83A0, [0, 4, 6]), (0x83A2, [0, 1, 2, 3]),
                   (0x836B, [0, 0xD7, 0xD8, 0xFF]), (0x7680, [0, 1]),
                   (0x83A4, [0, 0, 1, 5, 6, 0x23, 0x24, 0x40]), (0x805A, [0, 0, 3]), (0x54CA, [0, 2]),
                   (0xAF14, [0, 1]), (0x54B9, [0, 1]), (0x54BA, [0, 2]), (0x54BB, [0, 1, 2, 3])],
    "explode": [(0, exploding), (0xAE22, [0, 0, 1]), (0x83A9, [0, 0, 1, 2]), (0x7FDF, [16])],
    "dust": [(0, dust_world)],
    "tribbles": [(0, tribble_world)],
    "flight_start": [(0, arrival_world)],
    "arrive": [(0, arrival_world), (0, jump_world)],
    "key_bar": [(0, bar_world)],
    "commands": [(0, bar_world), (0, command_world), (0, ship_in_sights)],
    "countdowns": [(0, jump_world), (0xAE60, [0, 0, 1, 2, 10, 11]), (0xAE61, [1, 1, 2, 10]),
                   (0xAE25, [0, 1, 5]), (0xB3D5, [0, 0, 1, 2, 7, 15, 0x28]),
                   (0x54C8, [0x100, 0x2FE, 0x2FF, 0x3FF]), (0, ai_world)],
    "status": [(0, status_world), (0, arrival_dialogs)],
    "market": [(0, market_world)],
    "market_session": [(0, trading), (0, market_world), (0, market_keys), (0, device_world)],
    "launch": [(0, arrival_world), (0x8711, [0, 1]), (0x4801, [0, 1, 2]), (0x45E7, [0, 1]), (0, dust_world)],
    "dock": [(0x7613, [0, 1, 1]), (0x4801, [0, 1, 2]), (0x45E7, [0, 1]), (0, dust_world), (0, ai_world)],
    "loop": [(0, ai_handlers), (0, autopilot_world), (0, dust_world), (0, ship_in_sights), (0, bar_world),
             (0, command_world), (0x76BD, [0, 0, 0, 1]), (0xB126, [0, 0, 1, 2, 0x3C]), (0x839C, [0, 3]),
             (0x7613, [0, 0, 0, 1]), (0xAE23, [0, 0, 1, 2]), (0, scoop_world)],
    "frame": [(0, ai_handlers), (0, dust_world), (0, ship_in_sights), (0, tribble_world),
              (0, dashboard_world), (0x54CA, [0, 0, 1, 2]), (0x020D + 0x39, [0, 0, 0x80]),
              (0x8365, [0, 1, 0xF]), (0x020D + 0x48, [0, 0x80]), (0x020D + 0x50, [0, 0x80]),
              (0, scoop_world)],
    "jump_missions": [(0, jump_world)],
    "witchspace": [(0, jump_world), (0x8316, 1), (0x8317, 1)],
    "rings": [(0, jump_world)],
    "select_system": [(0x8315, [0, 1, 2, 3, 4, 5, 6, 7, 8]), (0x8318, 1), (0x8319, 1), (0x831E, [0, 0, 1, 2]),
                      (0x8316, 1), (0x8317, 1), (0, near_centre), (0x834A, 1), (0x834B, 1), (0x834C, 1)],
    "new_system": [(0, arrival_world)],
    "jump_drive": [(0xB0DD, [0, 1, 1, 1]), (0xAF14, [0, 0, 1]), (0xAF56, [0x30, 0x30, 0x2F, 4]),
                   (0xAE20, [0, 1]), (0x7680, [0, 0, 1]), (0, far_masses), (0x76B5, [36, 36, 36, 2, 3, 4])],
    "missile_lock": [(0, ship_in_sights), (0, ship_in_sights), (0x54CA, [1, 1, 1, 0, 2]), (0x4801, [0, 2]),
                     (0, lock_roles)],
    "dust_reset": [(0x805A, [0, 0x100, 0x128, 0x28])],
    "dashboard": [(0x54C8, [0, 0xFF, 0x100, 0x1FF, 0x200, 0x2FF, 0x300, 0x3FE, 0x3FF]),
                  (0x54C1, [0, 0x7F, 0x80, 0xBF, 0xC0, 0xDF, 0xE0]),
                  (0x54C3, [0x1F, 0x20, 0x27, 0x28, 0x7F, 0x80, 0xFF]), (0x54C4, [0, 1, 0x7F, 0x80, 0xFF]),
                  (0x54C5, [0, 1, 0x7F, 0x80, 0xFF]), (0x54C2, [0, 1, 2, 0x80]), (0x835F, [0, 1]),
                  (0xB126, [0, 0, 1]), (0xAE23, [0, 0, 1]), (0x54C0, [0, 1]), (0, something_close),
                  (0, dashboard_world)],
    "ai": [(0, ai_handlers), (0, ai_world), (0xB0DD, [0, 0, 1]), (0x83A4, [0, 0, 0, 5]),
           (0x83A9, [0, 0, 0, 3]), (0x83B3, [0, 1]), (0x83A7, [0, 0, 1]), (0x83AA, [0, 0, 1]),
           (0x83B1, [0, 1]), (0x83AB, [0, 0, 1]), (0x7680, [0, 1]), (0x83A0, [0, 4, 5, 6]),
           (0x83A2, [0, 2, 3]), (0x83B0, [0, 1]), (0x839E, [0, 5, 0x0D]), (0x839F, [0, 1]), (0x83A3, [0, 7]),
           (0x8329, [7, 7, 3])],
    "equip_screen": [(0, trading), (0, equip_world)],
    "chart_session": [(0, chart_world), (0, device_world)],
    "pause_session": [(0, pause_world)],
    "timer": [(0, speaker_world)],
    "define_keys": [(0, controls_world)],
    "joystick": [(0, controls_world)],
    "mouse": [(0, controls_world)],
    "key_event": [(0, key_bytes)],
    "adlib_music": [(0, adlib)],
    "title_open": [(0, title_world)],
    "protection_pick": [(0x0205, 8)],  # any generator state
    "title_session": [(0, title_world)],
    "save_session": [(0, files_world)],
    "load_session": [(0, files_world)],
    "start_game": [(0, start_world), (0, arrival_dialogs)],
    "data_screen": [(0, chart_world), (0x031D, [0, 1]), (0xAE60, [0, 0, 3]), (0x831E, [0, 1])],
    "equip_session": [(0, trading), (0, equip_world), (0, equip_keys), (0, device_world)],
    "collisions": [(0, something_close), (0, docking_approach), (0x83AA, [0, 0, 1]), (0xAE23, [0, 0, 0, 1]),
                   (0x54C4, [0, 10, 0x80, 0xFF]), (0x54C8, [0, 0x10, 0x200, 0x3FF])],
    "enemy_fire": [(0, attacker), (0x7612, [1, 1, 1, 0]), (0x7681, [0, 0x80]),
                   (0x54C4, [0, 5, 14, 15, 16, 0xFF]), (0x54C5, [0, 5, 14, 15, 16, 0xFF]),
                   (0x54C8, [0, 1, 0x10, 0x3FF])],
    "controls": [(0, autopilot_world), (0x020D + 0x48, [0, 0x80, 0x80]), (0x020D + 0x50, [0, 0x80, 0x80]),
                 (0x020D + 0x4B, [0, 0x80, 0x80]), (0x020D + 0x4D, [0, 0x80, 0x80]),
                 (0x020D + 0x34, [0, 0x80, 0x80]), (0x020D + 0x33, [0, 0x80, 0x80]),
                 (0x09D1, [0, 1, 2, 0x16, 0x17, 0xE9, 0xEA, 0xFF, 0x0B, 0xF5]),
                 (0x09D2, [0, 1, 2, 0x16, 0x17, 0xE9, 0xEA, 0xFF, 0x0B, 0xF5]), (0x09D3, [0, 1, 0xFF]),
                 (0x09D4, [0, 1, 0xFF]), (0x09D5, [0, 1, 0x16, 0x17, 0xE9, 0xEA, 0xFF, 0x0C]),
                 (0x09D6, [0, 1, 0x16, 0x17, 0xE9, 0xEA, 0xFF, 0x0C]), (0xB134, [0, 1]), (0xB135, [0, 1]),
                 (0xB136, [0, 0, 1]), (0xB137, [0, 0, 1]), (0xAF56, [4, 8, 0x2C, 0x30]), (0xAF58, [0, 1]),
                 (0xB0DD, [0, 0, 1]), (0x76D8, 2), (0x76DA, 2), (0x76DC, 2), (0xAE23, [0, 0, 0, 3]),
                 (0xB126, [0, 0, 0, 0x3C, 5]), (0, device_world)],
}
# fmt: on


PREPARE: dict[str, Prepare] = {"adlib_music": adlib, "adlib_fx": adlib_fx}  # every state, fuzzed or not


def fuzz(image: Memory, rng: random.Random) -> Memory:
    """The image with FUZZ's fields for NAME given random values (worlds called in turn)."""
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


def s16(v: int) -> int:
    """A word as signed."""
    return v - 65536 if v >= 32768 else v


def s8(v: int) -> int:
    """A byte as signed."""
    return v - 256 if v >= 128 else v


def text_bytes(e: Elite, si: int) -> str:
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


DRIVER = LOAD + 0x2270  # the music driver's segment

# Where a session leaves for another screen (its routine is up): 'commands' and 'loop' stop there.
SCREENS_UP = (0x8DAC, 0x9124, 0x90B7, 0x92D3, 0x5C80, 0x595A, 0x8AFA, 0x0DF6, 0x0AAC, 0x0AEF, 0x08E4,
              0x0694, 0x05E5, 0x0ED5)  # fmt: skip

# Routines whose sprites (3411) are recorded (the bar's icons through it as events).
SPRITES = ("tribbles", "status", "market", "market_session", "equip_screen", "equip_session", "chart_session",
           "data_screen", "pause_session", "start_game", "save_session", "load_session", "title_open",
           "title_session", "frame", "message", "dashboard", "update_objects", "launch", "dock", "arrive",
           "countdowns", "loop")  # fmt: skip

# Routines whose filled rectangles (2fd4) are recorded.
RECTS = ("status", "market", "market_session", "equip_screen", "equip_session", "chart_session",
         "data_screen", "pause_session", "start_game", "save_session", "load_session", "title_open",
         "title_session", "frame", "dashboard", "launch", "dock", "define_keys", "joystick", "mouse",
         "arrive", "countdowns", "loop")  # fmt: skip

# The exit line when the routine returned (or its scripted keys ran out) without taking an exit.
DEFAULT_EXIT = {
    "pause_session": "end",
    **dict.fromkeys(("chart_session", "save_session", "load_session", "title_open", "title_session",
                     "define_keys", "joystick", "mouse"), "end"),
    "commands": "cmd 0",
    "tunnel": "end 0",
}  # fmt: skip


def reg(mu: Uc, name: str) -> int:
    """A register by its eliteemu.REGS name."""
    return int(mu.reg_read(REGS[name]))


def skip(e: Elite, r: Regs) -> None:
    """A routine left out: returns at once."""


def jump(to: int) -> CodeHook:
    """A code hook going on at to (in the current code segment): the code up to there skipped."""

    def go(mu: Uc, address: int, size: int, user_data: object) -> None:
        mu.reg_write(UC_X86_REG_IP, to)

    return go


def icon_event(r: Regs) -> str:
    """A bar icon drawn (0312, sprite at cx, bx): the core's event 4, its slot and its sprite."""
    return f"event 4:{((r['cx'] - 0x10) // 0x18) << 8 | (r['bx'] & 0xFF)}"


def opl_lines(ports: list[tuple[int, int]]) -> list[str]:
    """The AdLib's register writes (388h: the register, 389h: its value), in order."""
    lines = []
    reg_no = None
    for port, v in ports:
        if port == 0x388:
            reg_no = v
        elif port == 0x389:
            lines.append(f"opl {reg_no},{v}")
    return lines


class OriginalRun:
    """The original's routine in the emulator on one image, with the hooks that note what it does
    the way the core reports it: the primitives drawn (prims, spans), the events and the sound
    hardware written (sounds), the exits taken (left) and the files written (written). Other hooks
    stand in for the hardware, DOS and the player (scripted keys from ds:ff10..), or leave out
    what the core does not do (waits, the second display page)."""

    def __init__(self, image: Memory, exits: dict[int, str]) -> None:
        self.image = image
        self.e = Elite()
        self.mu: Uc = self.e.mu
        self.left: list[str] = []
        for a, line in exits.items():
            self.stop_at(a, line)
        self.mu.mem_write(0, image)
        self.prims: list[str] = []
        self.spans: list[str] = []
        self.sounds: list[str] = []
        self.written: list[str] = []
        self.clipped: list[int] = []  # 2618: a clipped line (2576) goes on into 261b: recorded once
        self.intr_handlers: list[IntrFn] = []
        # the joystick and the mouse as ds:ff40.. say (tests/subsys.c reads the same): there, the
        # joystick's counts, its buttons (port 201h), the mouse's mickeys and buttons (int 33h)
        self.dev = image[DS * 16 + 0xFF40 : DS * 16 + 0xFF4B]
        self.there = self.dev[0]
        self.mickeys = [self.dev_word(6), self.dev_word(8)]

    # ---- hooking ----

    def at(self, addr: int, cb: CodeHook) -> None:
        """cb on reaching cs:addr; the code there then runs."""
        self.mu.hook_add(UC_HOOK_CODE, cb, begin=CS * 16 + addr, end=CS * 16 + addr)

    def stop(self, line: str) -> None:
        """Leave the routine here, noting line."""
        self.left.append(line)
        self.mu.emu_stop()

    def stop_at(self, addr: int, line: str) -> None:
        """An exit: leave the routine on reaching cs:addr, noting line."""

        def leave(mu: Uc, address: int, size: int, user_data: object) -> None:
            self.stop(line)

        self.at(addr, leave)

    def watch(self, at: int, fn: WatchFn) -> None:
        """fn(e, regs) on entry to cs:at; the routine runs."""
        e = self.e

        def entry(mu: Uc, address: int, size: int, user_data: object) -> None:
            fn(e, {k: mu.reg_read(v) for k, v in REGS.items()})

        self.at(at, entry)

    def observe(self, at: int, line: Callable[[Elite], str]) -> None:
        """line(e) noted (as an event) on entry to cs:at; the routine runs."""
        e = self.e

        def note(mu: Uc, address: int, size: int, user_data: object) -> None:
            self.sounds.append(line(e))

        self.at(at, note)

    def out_of(self, keys: list[int]) -> bool:
        """True, the routine left ("end"), when the scripted keys are used up."""
        if keys:
            return False
        self.stop("end")
        return True

    def key_at_pass(self, keys: list[int]) -> CodeHook:
        """A code hook putting the next scripted key in ds:0d2f (the key read); none left: the end."""

        def key(mu: Uc, address: int, size: int, user_data: object) -> None:
            if self.out_of(keys):
                return
            mu.mem_write(DS * 16 + 0x0D2F, bytes([keys.pop(0)]))

        return key

    def key_answers(self, keys: list[int], ff_none: bool) -> HookFn:
        """0276 (a key waiting?) answered from keys: carry set and the key in AL; with ff_none, ffh
        is no key (carry clear); none left: the end."""

        def answer(e: Elite, r: Regs) -> Regs | None:
            if self.out_of(keys):
                return None
            k = keys.pop(0)
            if ff_none and k == 0xFF:
                return {"flags": r["flags"] & ~1}
            e.mu.mem_write(DS * 16 + 0x0D2F, b"\xff")
            return {"ax": (r["ax"] & 0xFF00) | k, "flags": r["flags"] | 1}

        return answer

    def scripted_keys(self, n: int) -> list[int]:
        """The first n scripted keys, from ds:ff10."""
        return list(self.image[DS * 16 + 0xFF10 : DS * 16 + 0xFF10 + n])

    # ---- what the hooks note ----

    def prim(self, kind: int, pts: list[int]) -> None:
        """A primitive in the core's words: its kind, the colour (ds:10a2), its points."""
        self.prims.append(f"{kind}:{self.e.r8(0x10A2)}" + "".join(f",{s16(v)}" for v in pts))

    def span(self, e: Elite, r: Regs) -> None:
        """16da, 1514: a row of a filled circle."""
        ret = int.from_bytes(e.mu.mem_read(SS * 16 + e.mu.reg_read(UC_X86_REG_SP), 2), "little")
        if not 0x2A00 <= ret < 0x2E00:  # a circle's (2ab9, 2d16); not a rect's or a polygon's rows
            return
        if s16(r["cx"]) >= 0:
            self.spans.append(f"span {s16(r['bx'])},{s16(r['cx']) or 1},{(s16(r['di']) - 0x168) // 0x28}")

    def pixel(self, e: Elite, r: Regs) -> None:
        """2973: a pixel (a dust particle) inside the view."""
        x = (r["ax"] + (r["ax"] >> 2) - 8) & 0xFFFF
        if x < 0x130 and r["bx"] < 0x7C:
            self.prims.append(f"8:{e.r8(r['si'] + 6) & 0xF},{x},{r['bx']}")

    def text(self, shadow: int) -> CodeHook:
        """2e6d, 2ec0 (shadowed): a text at bx, cx, as its bytes."""
        e = self.e

        def note(mu: Uc, address: int, size: int, user_data: object) -> None:
            self.prims.append(
                f"text {s16(reg(mu, 'bx'))},{s16(reg(mu, 'cx'))},{e.r8(0x10A2)},{shadow}:"
                + text_bytes(e, reg(mu, "si"))
            )

        return note

    def sprite_or_icon(self, e: Elite, r: Regs) -> None:
        """3411: a sprite, or the bar's icon (from 0312, or through 37bd on EGA/VGA) as an event."""
        sp = SS * 16 + e.mu.reg_read(UC_X86_REG_SP)
        if e.mu.mem_read(sp, 2) == b"\x15\x03" or (
            e.mu.mem_read(sp, 2) == b"\xce\x37" and e.mu.mem_read(sp + 8, 2) == b"\x15\x03"
        ):  # the bar's (0312, through 37bd on EGA/VGA)
            self.sounds.append(icon_event(r))
        else:
            self.prims.append(f"10:{r['bx'] & 0xFF},{s16(r['cx'])},{s16(r['dx'])}")

    def bar_icon(self, e: Elite, r: Regs) -> None:
        """37bd: only the bar's own icons (from 0312); the Esc menu's marks are the frontend's."""
        if e.mu.mem_read(SS * 16 + e.mu.reg_read(UC_X86_REG_SP), 2) == b"\x15\x03":
            self.sounds.append(icon_event(r))

    def view_clear(self, e: Elite, r: Regs) -> None:
        """3130 as the core emits it."""
        self.prims.extend(["rect 0:8,9,304,124", "10:0,96,160", "10:51,296,157"])

    def flip(self, e: Elite, r: Regs) -> None:
        """301a runs (its registers matter), all but its wait for the clock (3027)."""
        self.sounds.append(f"event {EV_FLIP}:0")

    def palette(self, e: Elite, r: Regs) -> None:
        """3821: a palette loaded."""
        self.sounds.append(f"event 10:{r['si']}")

    def unported(self, stub: int) -> HookFn:
        """A routine the core does not reconstruct yet: stubbed, logged."""

        def note(e: Elite, r: Regs) -> None:
            self.sounds.append(f"event {EV_UNPORTED}:{stub}")

        return note

    def screen_kept(self, kind: int) -> HookFn:
        """397c, 3981: the screen under a box or the top line kept (7), put back (8)."""

        def note(e: Elite, r: Regs) -> None:
            self.sounds.append(f"event {kind}:{1 if r['ax'] == 0x18 else 2}")

        return note

    # ---- devices ----

    def dev_word(self, k: int) -> int:
        """The word at ds:ff40 + k."""
        return int.from_bytes(self.dev[k : k + 2], "little")

    def joystick_counts(self, e: Elite, r: Regs) -> Regs:
        """0ffb: the joystick's counts (carry: none)."""
        there = self.there
        return {
            "bx": self.dev_word(1) if there else 0,
            "cx": self.dev_word(3) if there else 0,
            "flags": (r["flags"] & ~1) if there else (r["flags"] | 1),
        }

    def mouse(self, e: Elite, intno: int) -> bool:
        """int 33h: the mouse's mickeys (once), its buttons, there or not."""
        if intno != 0x33:
            return False
        fn = e.mu.reg_read(REGS["ax"])
        if fn == 0x0B:
            e.mu.reg_write(REGS["cx"], self.mickeys[0])
            e.mu.reg_write(REGS["dx"], self.mickeys[1])
            self.mickeys[:] = [0, 0]
        elif fn == 5:
            e.mu.reg_write(REGS["ax"], self.dev[10])
        elif fn == 0:
            e.mu.reg_write(REGS["ax"], 0xFFFF if self.there else 0)
        return True

    def interrupt(self, e: Elite, intno: int) -> bool:
        """Elite.on_intr: the first handler that takes it."""
        return any(h(e, intno) for h in self.intr_handlers)

    # ---- the hooks, by concern (hook_all applies them in order) ----

    def hook_primitives(self) -> None:
        """Polygons, lines (a clipped line once, as clipped), circles' rows."""
        self.watch(0x172C, lambda e, r: self.prim(0, [r["cx"], r["dx"], r["ax"], r["bx"], r["si"], r["bp"]]))
        self.watch(
            0x1A7A,
            lambda e, r: self.prim(
                2, [r["ax"], r["bx"], r["cx"], r["dx"], e.r16(0x10B0), e.r16(0x10B4), r["si"], r["bp"]]
            ),
        )

        def clipping(mu: Uc, address: int, size: int, user_data: object) -> None:
            self.clipped.append(1)

        self.at(0x2618, clipping)
        self.watch(
            0x261B,
            lambda e, r: (
                self.clipped.pop() if self.clipped else self.prim(4, [r["cx"], r["dx"], r["ax"], r["bx"]])
            ),
        )
        self.watch(0x16DA, self.span)
        self.watch(0x1514, self.span)

    def hook_devices(self) -> None:
        """The drivers; the joystick (port 201h, 0ffb) and the mouse (int 33h) as ds:ff40.. say."""
        e = self.e
        e.devices(drivers=True)
        e.port_in[0x201] = self.dev[5] if self.there else 0xFF
        e.hook(0x0FFB, self.joystick_counts)
        self.intr_handlers.append(self.mouse)
        e.on_intr = self.interrupt

    def hook_events(self) -> None:
        """Sounds, music, the launch sound's wait, unported routines, screens kept: as events."""
        self.observe(0x4E1A, lambda e: f"event {EV_SURFACE}:{e.mu.reg_read(REGS['ax'])}")
        self.observe(0x4C98, lambda e: f"event {EV_SOUND}:{e.mu.reg_read(REGS['ax']) & 0xFF}")
        self.observe(0x4D21, lambda e: "event 6:2")  # the title music on
        self.observe(0x4D55, lambda e: "event 6:1")  # off
        self.observe(
            0x4D6C, lambda e: f"event 6:{e.mu.reg_read(REGS['ax']) & 0xFF}"
        )  # sound off/on from the options

        def sound_wait(mu: Uc, address: int, size: int, user_data: object) -> None:
            # 4e88: until the launch sound has played (the clock stands still here)
            until = reg(mu, "dx") << 16 | reg(mu, "ax")
            self.sounds.append(f"event 5:{until - (self.e.r16(0x45E2) << 16 | self.e.r16(0x45E0))}")
            mu.reg_write(UC_X86_REG_IP, 0x4E96)

        self.at(0x4E88, sound_wait)
        for stub in UNPORTED_FOR.get(NAME, UNPORTED):
            self.e.hook(stub, self.unported(stub))
        for at, kind in ((0x397C, 7), (0x3981, 8)):  # the screen under a box or the top line kept, put back
            self.e.hook(at, self.screen_kept(kind))

    def hook_screen_flip(self) -> None:
        """3130 as the core emits it; 301a noted, its wait for the clock (3027) skipped."""
        self.e.hook(0x3130, self.view_clear)
        self.watch(0x301A, self.flip)
        self.at(0x3027, jump(0x3035))

    def hook_bar_icons(self) -> None:
        """Icon redraws."""
        self.e.hook(0x37BD, self.bar_icon)

    def hook_loop_top(self) -> None:
        """Back at the top of the loop (a040): the frame is over."""
        visits: list[int] = []

        def top(mu: Uc, address: int, size: int, user_data: object) -> None:
            visits.append(1)
            if len(visits) > 1:
                self.stop("frame 0")

        self.at(0xA040, top)

    def hook_command_exits(self) -> None:
        """'commands', 'loop': a screen up, the pause menu up: exits; no commander files."""
        if NAME == "commands":  # the bar's icons as events; the cockpit (763e) is drawing
            self.e.hook(0x37BD, self.bar_icon)
        done = "cmd 2" if NAME == "commands" else "frame 2"
        for at in SCREENS_UP:  # screens up
            self.stop_at(at, done)
        paused = "cmd 3" if NAME == "commands" else "frame 4"
        self.stop_at(0x0480, paused)  # the pause menu is up
        self.intr_handlers.append(fake_dos({}, []))  # no commander files

    def hook_mouse_driver(self) -> None:
        """028d: the mouse driver, left out."""
        self.e.hook(0x028D, skip)

    def hook_pixels_texts(self) -> None:
        """Pixels (2973) and texts (observed, not replaced)."""
        self.watch(0x2973, self.pixel)
        for at, shadow in ((0x2E6D, 0), (0x2EC0, 1)):  # texts (observed, not replaced)
            self.at(at, self.text(shadow))

    def hook_sprites(self) -> None:
        """Sprites (3411), the bar's icons among them."""
        self.watch(0x3411, self.sprite_or_icon)

    def hook_clock(self) -> None:
        """'start_game': the music stops; the time of day from ds:ff30."""
        self.e.hook(0x4AC0, skip)
        image = self.image

        def clock(mu: Uc, address: int, size: int, user_data: object) -> None:
            t = image[DS * 16 + 0xFF30 : DS * 16 + 0xFF34]
            mu.reg_write(REGS["cx"], t[0] << 8 | t[1])
            mu.reg_write(REGS["dx"], t[2] << 8 | t[3])
            mu.reg_write(UC_X86_REG_IP, 0x7264)

        self.at(0x7260, clock)

    def hook_wait_keys(self) -> None:
        """'status', 'start_game': scripted keys for the waits (ds:ff10, then Y); no palette cycling."""
        keys = self.scripted_keys(8)

        def key(e: Elite, r: Regs) -> Regs:
            k = keys.pop(0) if keys else ord("Y")
            e.mu.mem_write(DS * 16 + 0x0D2F, b"\xff")
            return {"ax": (r["ax"] & 0xFF00) | k, "flags": r["flags"] | 1}

        self.e.hook(0x0276, key)
        self.e.hook(0x3BB1, skip)
        self.e.hook(0x3821, self.palette)  # a palette loaded

    def hook_pause_keys(self) -> None:
        """'pause_session': a key at each pass (0480) or question (0aac, 0aef), 12 keys."""
        pass_key = self.key_at_pass(self.scripted_keys(12))
        for at in (0x0480, 0x0AAC, 0x0AEF):
            self.at(at, pass_key)

    def hook_files(self) -> None:
        """'save_session', 'load_session': the keys (0276, 32 of them) and a fake DOS."""
        self.e.hook(0x0276, self.key_answers(self.scripted_keys(32), ff_none=False))
        self.e.hook(0x4AC0, skip)
        self.intr_handlers.append(fake_dos(disk(self.image), self.written))

        def leave(mu: Uc, address: int, size: int, user_data: object) -> None:
            # 9e80: the station (af18 = 1) or the title
            self.left.extend(["end", f"leave {3 if self.e.r8(0xAF18) == 1 else 1}"])
            mu.emu_stop()

        self.at(0x9E80, leave)

    def hook_no_files(self) -> None:
        """'protection_pick': it closes the graphics file (handle ds:2128)."""
        self.intr_handlers.append(fake_dos({}, []))

    def hook_one_page(self) -> None:
        """Drawn on both pages (EGA, VGA): the second is skipped."""

        def second_page(mu: Uc, address: int, size: int, user_data: object) -> None:
            ret = int.from_bytes(mu.mem_read(SS * 16 + mu.reg_read(UC_X86_REG_SP), 2), "little")
            mu.reg_write(UC_X86_REG_SP, mu.reg_read(UC_X86_REG_SP) + 2)
            mu.reg_write(UC_X86_REG_IP, ret)

        for at in (0x2F2D, 0x2F64, 0x37D1):  # 2f12, 2f4d, 37bd
            self.at(at, second_page)

    def hook_controls_keys(self) -> None:
        """'define_keys', 'joystick', 'mouse': answers at 0276, presses for 05f9, no hardware."""
        keys = self.scripted_keys(16)
        e = self.e
        e.hook(0x0276, self.key_answers(keys, ff_none=True))

        def press(mu: Uc, address: int, size: int, user_data: object) -> None:
            # 05f9: armed, or the key taken already: the next press
            p = e.r16(0x0D2D)
            if p != 0xFFFF and p not in [e.r16(a) for a in range(0xB251, 0xB25F, 2)]:
                return
            if self.out_of(keys):
                return
            mu.mem_write(DS * 16 + 0x0D2D, (0x20D + (keys.pop(0) & 0x7F)).to_bytes(2, "little"))

        self.at(0x05F9, press)
        there = self.there
        e.hook(
            0x0BCA,
            lambda e, r: {
                "ax": 0xFFFF if there else 0,
                "flags": (r["flags"] & ~0x40) if there else (r["flags"] | 0x40),
            },
        )

    def hook_title_open(self) -> None:
        """'title_open': keys at 0276 (ffh: none); one page drawn; no hardware."""
        self.e.hook(0x0276, self.key_answers(self.scripted_keys(12), ff_none=True))
        for stub in (0x30DC, 0x3941, 0x3956, 0x3B3E):
            self.e.hook(stub, skip)
        self.e.hook(0x3821, self.palette)  # a palette loaded
        self.hook_screen_flip()

    def hook_title_session(self) -> None:
        """'title_session': a key at each pass (9f21), 12 passes."""
        self.at(0x9F21, self.key_at_pass(self.scripted_keys(12)))
        self.e.hook(0x028D, skip)
        self.hook_screen_flip()

    def hook_chart_session(self) -> None:
        """'chart_session': a key and the arrows held at each pass (5c80, 595a), 12 passes."""
        e = self.e
        keys = self.scripted_keys(12)
        arrows = list(self.image[DS * 16 + 0xFF20 : DS * 16 + 0xFF2C])

        def chart_pass(mu: Uc, address: int, size: int, user_data: object) -> None:
            if self.out_of(keys):
                return
            a = arrows.pop(0)
            for j, b in enumerate((0xB255, 0xB257, 0xB259, 0xB25B)):
                ptr = e.r8(b) | e.r8(b + 1) << 8
                mu.mem_write(DS * 16 + ptr, bytes([0 if a >> j & 1 else 0x80]))
            mu.mem_write(DS * 16 + 0x0D2F, bytes([keys.pop(0)]))

        for at in (0x5C80, 0x595A):
            self.at(at, chart_pass)

        def typing(mu: Uc, address: int, size: int, user_data: object) -> None:
            # each turn of the text's loop takes the next key
            if self.out_of(keys):
                return
            arrows.pop(0)
            mu.mem_write(DS * 16 + 0x0D2F, bytes([keys.pop(0)]))

        self.at(0x0DF6, typing)

        def pixel(mu: Uc, address: int, size: int, user_data: object) -> None:  # 291b
            ax, bx = reg(mu, "ax"), reg(mu, "bx")
            if ax < 0x130 and bx < 0x7C:
                self.prims.append(f"8:{reg(mu, 'cx') & 0xFF},{ax},{bx}")

        self.at(0x291B, pixel)  # pixels
        e.hook(0x3956, skip)  # the display pages
        e.hook(0x3941, skip)
        e.hook(0x396E, skip)

    def hook_equip_session(self) -> None:
        """'equip_session': a scripted key at each pass (92d3) or dialog loop (9502, 968f)."""
        key = self.key_at_pass(self.scripted_keys(12))
        for at in (0x92D3, 0x9502, 0x968F):
            self.at(at, key)

    def hook_market_session(self) -> None:
        """'market_session': a scripted key at the start of each pass (9124), 12 passes."""
        key = self.key_at_pass(self.scripted_keys(12))
        for at in (0x9124, 0x90B7):  # docked, in flight
            self.at(at, key)

    def hook_rects(self) -> None:
        """Filled rectangles (2fd4)."""
        self.watch(
            0x2FD4,
            lambda e, r: self.prims.append(
                f"rect {e.r8(0x10A2)}:{s16(r['ax'])},{s16(r['bx'])},{s16(r['cx'])},{s16(r['dx'])}"
            ),
        )

    def hook_clipped_lines_blips(self) -> None:
        """Clipped lines (2576) and scanner blips (29ed)."""
        self.watch(0x2576, lambda e, r: self.prim(6, [r["cx"], r["ax"], r["dx"], r["bx"]]))  # clipped line
        e = self.e

        def blip(
            mu: Uc, address: int, size: int, user_data: object
        ) -> None:  # 29ed: a scanner blip (MCGA) drawn
            self.prims.append(
                f"blip {(e.r8(reg(mu, 'di')) >> 1) & 0x1F},{reg(mu, 'ax') >> 8},"
                f"{reg(mu, 'bx') >> 8},{s8(reg(mu, 'cx') >> 8)}"
            )

        self.at(0x29ED, blip)

    def hook_all(self) -> None:
        """Every hook NAME takes, in the order they are added (hooks at one address run in it)."""
        every = None
        setups: list[tuple[tuple[str, ...] | None, Callable[[], None]]] = [
            (every, self.hook_primitives),
            (every, self.hook_devices),
            (every, self.hook_events),
            (("arrive", "countdowns", "commands"), self.hook_screen_flip),
            (("key_bar",), self.hook_bar_icons),
            (("loop",), self.hook_loop_top),
            (("commands", "loop"), self.hook_command_exits),
            (("frame", "loop", "launch", "dock"), self.hook_screen_flip),
            (("launch", "dock", "loop", "commands"), self.hook_mouse_driver),
            (every, self.hook_pixels_texts),
            (SPRITES, self.hook_sprites),
            (("start_game",), self.hook_clock),
            (("status", "start_game"), self.hook_wait_keys),
            (("pause_session",), self.hook_pause_keys),
            (("save_session", "load_session"), self.hook_files),
            (("protection_pick",), self.hook_no_files),
            (("title_open", "title_session"), self.hook_one_page),
            (("define_keys", "joystick", "mouse"), self.hook_controls_keys),
            (("title_open",), self.hook_title_open),
            (("title_session",), self.hook_title_session),
            (("chart_session",), self.hook_chart_session),
            (("equip_session",), self.hook_equip_session),
            (("market_session",), self.hook_market_session),
            (RECTS, self.hook_rects),
            (every, self.hook_clipped_lines_blips),
        ]
        for names, setup in setups:
            if names is None or NAME in names:
                setup()

    # ---- running ----

    def run(self, addr: int, regs: Regs) -> None:
        """The routine (or the routine's driver, for the sound ones); then the exit taken."""
        runners: dict[str, Callable[[int, Regs], None]] = {
            "adlib_music": self.run_adlib_music,
            "adlib_fx": self.run_adlib_fx,
            "key_event": self.run_key_event,
            "timer": self.run_timer,
        }
        try:
            if NAME == "explode":
                regs = dict(regs, di=0x76DE + 0x40 * self.image[DS * 16 + 0xFF00] % (0x40 * 36))
            runners.get(NAME, self.run_call)(addr, regs)
        except RuntimeError:
            if not self.left:
                raise
        if NAME in DEFAULT_EXIT and not self.left:
            self.left.append(DEFAULT_EXIT[NAME])

    def run_call(self, addr: int, regs: Regs) -> None:
        """A near call of the routine."""
        max_insns = 100_000_000 if NAME == "title_session" else 10_000_000  # 12 title passes
        self.e.call(addr, max_insns=max_insns, **regs)

    def run_adlib_music(self, addr: int, regs: Regs) -> None:
        """The driver started (far: through 14a8), then 80000 of its ticks."""
        e, mu = self.e, self.mu
        drv = DRIVER * 16
        seg = DRIVER.to_bytes(2, "little")
        mu.mem_write((LOAD + 0x16E4) * 16, SONG)  # 003b's load
        mu.mem_write(CS * 16 + 0x14A8, b"\x9a\x00\x00" + seg + b"\xc3")
        e.call(0x14A8)
        mu.hook_add(UC_HOOK_CODE, jump(0x0E8A), begin=drv + 0x0E8C, end=drv + 0x0E8C)  # no BIOS
        # the tick's own trampoline (rewriting 14a8 would leave its old translation running)
        mu.mem_write(CS * 16 + 0x14B0, b"\x9c\x9a\xf8\x0d" + seg + b"\xc3")
        for _ in range(80000):
            e.call(0x14B0)
        self.sounds.extend(opl_lines(e.ports))
        pit = [v for p, v in e.ports if p == 0x40]
        if len(pit) >= 2:
            self.sounds.append(f"pit {pit[-2] | pit[-1] << 8}")

        def word(o: int, n: int = 2) -> int:  # in the driver's segment
            return int.from_bytes(mu.mem_read(drv + o, n), "little")

        self.sounds.append(
            f"drv {word(0xDA7)},{word(0xDA9, 1)},{word(0xDAA)},{word(0xDAC)},{word(0xDAE)},{word(0xDB0)}"
        )

    def run_adlib_fx(self, addr: int, regs: Regs) -> None:
        """The effects installed (17c6), the schedule at ds:ff10, 2000 ticks more."""
        e, mu, image = self.e, self.mu, self.image
        drv = DRIVER * 16
        seg = DRIVER.to_bytes(2, "little")
        mu.mem_write(CS * 16 + 0x14A8, b"\x9a\xc6\x17" + seg + b"\xc3")  # lcall 17c6
        mu.mem_write(CS * 16 + 0x14B0, b"\x9c\x9a\xc1\x16" + seg + b"\xc3")  # pushf; lcall 16c1
        mu.mem_write(CS * 16 + 0x14B8, b"\x9a\x5a\x18" + seg + b"\xc3")  # lcall 185a (AL)
        self.at(0x4AB4, jump(0x4ABF))  # no BIOS
        e.call(0x14A8)
        for k in range(33):
            for _ in range(image[DS * 16 + 0xFF10 + 2 * k] if k < 32 else 2000):
                e.call(0x14B0)
            if k < 32:
                e.call(0x14B8, ax=image[DS * 16 + 0xFF11 + 2 * k])
        self.sounds.extend(opl_lines(e.ports))
        pit = [v for p, v in e.ports if p == 0x40]
        self.sounds.append(f"pit {pit[-2] | pit[-1] << 8} int8 2")
        self.sounds.extend(fx_lines(mu.mem_read(drv + FX_CS, 0x300)))

    def run_key_event(self, addr: int, regs: Regs) -> None:
        """The interrupt for each of 8 bytes from port 60h (ds:ff10); its iret a ret."""
        e = self.e
        e.hook(0x0274, skip)
        for k in range(8):
            e.port_in[0x60] = self.image[DS * 16 + 0xFF10 + k]
            e.call(addr, **regs)

    def run_timer(self, addr: int, regs: Regs) -> None:
        """24 ticks; 4a50 is far: its retf (4a98) taken as a near ret. Then the speaker's divisor
        (PIT channel 2, after the mode byte b6h) and its gate (port 61h), as last written."""
        e = self.e
        e.hook(0x4A98, skip)
        for _ in range(24):
            e.call(addr, **regs)
        div: int | None = None
        gate: int | None = None
        out = e.ports
        for k, (port, v) in enumerate(out):
            if (
                port == 0x42
                and k + 1 < len(out)
                and out[k + 1][0] == 0x42
                and k
                and out[k - 1] == (0x43, 0xB6)
            ):
                div = v | out[k + 1][1] << 8
            if port == 0x61:
                gate = v & 1
        if div is not None:
            self.sounds.append(f"speaker {div}")
        if gate is not None:
            self.sounds.append(f"gate {gate}")

    def result(self) -> tuple[bytes, list[str]]:
        """The data segment after the run, and the lines to compare with the core's."""
        return bytes(
            self.mu.mem_read(DS * 16, 0x10000)
        ), self.prims + self.spans + self.left + self.sounds + self.written


def run_original(
    image: Memory, addr: int, regs: Regs, exits: dict[int, str] | None = None
) -> tuple[bytes, list[str]]:
    """The original's routine at cs:addr run on image: its data segment after, and its lines."""
    run = OriginalRun(image, exits or {})
    run.hook_all()
    run.run(addr, regs)
    return run.result()


def state(path: str, variant: int | None, rng: random.Random) -> Memory:
    """A corpus state, PREPAREd, fuzzed when variant is not None."""
    image = load(path)
    if NAME in PREPARE:
        img = bytearray(image)
        PREPARE[NAME](img)
        image = bytes(img)
    if variant is not None:
        image = fuzz(image, rng)
    return image


def run_core(tmp: str, image: Memory, before: bytes) -> tuple[bytes, list[str]]:
    """ep_subsys on the data segment: the data segment after, and its lines."""
    inf, outf = os.path.join(tmp, "in"), os.path.join(tmp, "out")
    Path(inf).write_bytes(before)
    if NAME == "adlib_fx":  # the driver's segment's part the core keeps outside the data segment
        Path(inf + ".cs").write_bytes(image[DRIVER * 16 + FX_CS : DRIVER * 16 + FX_CS + 0x300])
    out = subprocess.run([TOOL, NAME, inf, outf], capture_output=True, text=True, check=True).stdout
    return Path(outf).read_bytes(), out.splitlines()


def report(
    label: str, diff: list[int], want: bytes, got: bytes, want_prims: list[str], got_prims: list[str]
) -> None:
    """A differing state: the modelled bytes that differ, the lines' counts, the first difference."""
    shown = [f"{i:x}:{want[i]:02x}/{got[i]:02x}" for i in diff[:12]]
    print(
        f"{label}: {len(diff)} modelled bytes differ {shown}, "
        f"primitives {len(want_prims)} vs {len(got_prims)}" + ("" if want_prims == got_prims else " (differ)")
    )
    if want_prims != got_prims and SHOW > 3:
        # padded: a line one side lacks shows as "-" (99 past the shorter side at most)
        for k, (a, b) in enumerate(zip(want_prims + ["-"] * 99, got_prims + ["-"] * 99, strict=False)):
            if a != b:
                print(f"    first difference at {k}: want {a} got {b}")
                break


def runs_of(addrs: Iterable[int]) -> list[tuple[int, int]]:
    """Sorted addresses as runs (first, last)."""
    runs: list[tuple[int, int]] = []
    for i in sorted(addrs):
        if runs and i == runs[-1][1] + 1:
            runs[-1] = (runs[-1][0], i)
        else:
            runs.append((i, i))
    return runs


def main() -> None:
    addr, regs, exits = ROUTINES[NAME]
    files = sorted(glob.glob(PATTERN))
    if not files:  # nothing compared proves nothing: a failure, not "0 differ"
        sys.exit(f"{NAME}: no states match {PATTERN} (make the corpus with corpus.py)")
    tmp = tempfile.mkdtemp()
    maskf = os.path.join(tmp, "mask")
    subprocess.run([TOOL, "mask", maskf], check=True)
    mask = Path(maskf).read_bytes()  # the data-segment bytes the core models
    bad = 0
    unmodelled: collections.Counter[int] = collections.Counter()
    rng = random.Random(1)
    cases: list[tuple[str, int | None]] = [(p, None) for p in files] + [
        (p, k) for p in files for k in range(FUZZ_N)
    ]
    for path, variant in cases:
        image = state(path, variant, rng)
        before = image[DS * 16 : DS * 16 + 0x10000]
        want, want_prims = run_original(image, addr, regs, exits)
        if NAME in ("frame", "loop"):
            want_prims = [line for line in want_prims if line != "end"]
        got, got_prims = run_core(tmp, image, before)
        if NAME in ("commands",):  # drawing is checked per screen; here state and events
            want_prims = [line for line in want_prims if line.startswith(("event", "frame", "cmd"))]
            got_prims = [line for line in got_prims if line.startswith(("event", "frame", "cmd"))]
        diff = [i for i in range(0x10000) if mask[i] and want[i] != got[i]]
        for i in range(0x10000):
            if not mask[i] and want[i] != before[i] and not any(a <= i <= b for a, b in SCRATCH):
                unmodelled[i] += 1
        if os.environ.get("EP_DUMP"):  # both sides of the last state, for a closer look
            Path(os.environ["EP_DUMP"] + ".want").write_text("\n".join(want_prims) + "\n")
            Path(os.environ["EP_DUMP"] + ".got").write_text("\n".join(got_prims) + "\n")
        if diff or want_prims != got_prims:
            bad += 1
            if bad <= SHOW:
                label = os.path.basename(path) + ("" if variant is None else " fuzz " + str(variant))
                report(label, diff, want, got, want_prims, got_prims)
    print(f"{NAME}: {len(cases)} states ({FUZZ_N} fuzzed per state), {bad} differ")
    if unmodelled:
        print(
            "not modelled, changed by the original: "
            + " ".join(
                f"ds:{a:04x}" + (f"-{b:04x}" if b != a else "") + f"({unmodelled[a]})"
                for a, b in runs_of(unmodelled)[:40]
            )
        )
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    if "--list" in sys.argv:
        print("\n".join(ROUTINES))
    elif not NAME:
        sys.exit(__doc__)
    else:
        main()
