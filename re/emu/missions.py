"""Collect game states from the missions: real v3.1 commander saves flown in the original.

usage: corpus.py --saves DIR [frames] [every] [outdir]

For each DIR/*.CDR (George Hooper's mission saves, say): boot the original in machine.py with
the saves as its files, load the commander through the game's own Load screen (title, F8, the
cursor down to the name, Enter, space) and check the commander block (ds:82db) is the file.
Then, as a player would:

1. answer the station's offer if there is one (Y: JAMESON's Tribble), launch (F1) and fly;
2. hyperspace to the save's selected system (chart F4, hyperspace F6): the jump counters start
   the mission on arrival (ds:83a0), and fly there;
3. unless in witchspace, head for the station (roll and pitch taps toward it, the jump drive
   F11 while far ahead) until the safe zone (ds:7680 bit 0), dock with the docking
   computer (F1), read the briefing (any key), launch again and fly into the mission.

Each flight window holds random keys as corpus.py does (fire only after the briefing, so the
station stays friendly for the docking) and saves the whole memory every `every`-th flight
frame of `frames` as OUTDIR/mission-<save>-<frame>.bin (frame: counted from the load). A step
that cannot be done (no target, a misjump: no station, a station under siege) ends the run there;
what was collected stays. Deterministic: the random keys are seeded with the save's name.

The images and the saves are your own copies of the game's data: keep them out of the
repository (the default OUTDIR, re/emu/corpus/, is git-ignored).
"""

import math
import os
import random
import struct
from typing import Any, Final

from unicorn import UC_HOOK_CODE

from corpus import FLIGHT_TOP, KEYS, TITLE_TOP, save
from eliteemu import Uc
from machine import CS, DS, Machine

# scancodes (set 1)
F1: Final = 0x3B
F4: Final = 0x3E
F6: Final = 0x40
F8: Final = 0x42
F11: Final = 0x57
SPACE: Final = 0x39
ENTER: Final = 0x1C
DOWN: Final = 0x50
UP: Final = 0x48
LEFT: Final = 0x4B
RIGHT: Final = 0x4D
Y: Final = 0x15
FASTER: Final = 0x34
CALM_KEYS: Final = [k for k in KEYS if k != SPACE]  # no fire: the station is not provoked

# data segment
COMMANDER: Final = 0x82DB
COMMANDER_SIZE: Final = 226
CURRENT_SYSTEM: Final = 0x831F  # its name first
SELECTED_DISTANCE: Final = 0x8338 + 0x0B  # the selected system's record: its distance word
SLOTS: Final = 0x76DE  # 64-byte object slots: 0 sun, 1 planet, 2 station
SCREEN: Final = 0x02F9  # 0 flight, 1/2 docked screens, 5 title
SAFE_ZONE: Final = 0x7680  # bit 0: near the station
JUMP: Final = 0xB0DD  # jump drive engaged
WITCHSPACE: Final = 0x83A4
MISSION: Final = 0x83A0
DEAD: Final = 0x76BD

# Roll or pitch held this many frames turns the view by about 1.4, 4.5, 11 and 23 degrees once
# the rate has died down (measured in the original): (least error in degrees, frames held).
TAPS: Final = ((20.0, 6), (9.0, 5), (3.5, 4), (1.2, 3))
SETTLE: Final = 25  # frames for the turn rate to die down after a tap
JUMP_FAR: Final = 100_000  # the jump drive only with the station this far ahead (else it passes it)

type Vec = tuple[float, float, float]


def _norm(v: Vec) -> Vec:
    n = math.sqrt(sum(a * a for a in v))
    return (v[0] / n, v[1] / n, v[2] / n)


def _dot(a: Vec, b: Vec) -> float:
    return sum(p * q for p, q in zip(a, b, strict=True))


def _cross(a: Vec, b: Vec) -> Vec:
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _triad(u: Vec, v: Vec) -> tuple[Vec, Vec, Vec]:
    """An orthonormal basis from two directions: u, v made orthogonal to it, their cross."""
    e1 = _norm(u)
    d = _dot(v, e1)
    e2 = _norm((v[0] - d * e1[0], v[1] - d * e1[1], v[2] - d * e1[2]))
    return e1, e2, _cross(e1, e2)


class Pilot:
    """One save flown in the original: the machine, the flight frame count and the states."""

    def __init__(self, name: str, files: dict[str, bytearray], outdir: str) -> None:
        self.name = name.removesuffix(".CDR")
        self.file = bytes(files[name])
        self.index = sorted(files).index(name)  # its row on the Load screen (DOS order)
        self.outdir = outdir
        self.rng = random.Random(self.name)
        self.m = Machine(files=files)
        self.title = 0  # title frames (9f21)
        self.flight = 0  # flight frames (a040)
        self.saved: list[str] = []
        self.log: list[str] = []

        def on_title(mu: Uc, a: int, s: int, u: Any) -> None:
            self.title += 1

        def on_flight(mu: Uc, a: int, s: int, u: Any) -> None:
            self.flight += 1

        mu = self.m.mu
        mu.hook_add(UC_HOOK_CODE, on_title, begin=CS * 16 + TITLE_TOP, end=CS * 16 + TITLE_TOP)
        mu.hook_add(UC_HOOK_CODE, on_flight, begin=CS * 16 + FLIGHT_TOP, end=CS * 16 + FLIGHT_TOP)

    # ---- time ----
    def ticks(self, n: int) -> None:
        """Run n timer ticks and until the queued keys are delivered."""
        m = self.m
        end = m.ticks + n
        m.run(stop=lambda m: m.ticks > end and not m.keys and m.down is None, idle_ticks=True)

    def frames(self, n: int) -> None:
        """Run n flight frames (or 4n ticks, should flight end)."""
        end, limit = self.flight + n, self.m.ticks + 4 * n + 8
        self.m.run(stop=lambda m: self.flight >= end or m.ticks > limit, idle_ticks=True)

    def keys(self, *scancodes: int, ticks: int = 50) -> None:
        """Press keys (at the game's polls) and let ticks pass."""
        self.m.press(*scancodes)
        self.ticks(ticks)

    def tap(self, key: int, frames: int) -> None:
        """Hold a key for some flight frames."""
        self.m.scancode_event(key)
        self.frames(frames)
        self.m.scancode_event(key | 0x80)

    # ---- state ----
    def r8(self, a: int) -> int:
        return self.m.r8(a)

    def system(self) -> bytes:
        """The current system's name."""
        return bytes(self.m.mu.mem_read(DS * 16 + CURRENT_SYSTEM, 10)).split(b"\0")[0]

    def slot(self, n: int) -> bytes:
        return bytes(self.m.mu.mem_read(DS * 16 + SLOTS + 64 * n, 64))

    def in_flight(self) -> bool:
        return self.r8(SCREEN) == 0 and not self.r8(DEAD)

    def note(self, text: str) -> None:
        self.log.append(f"{self.flight:5d} {text}")

    # ---- the steps ----
    def load(self) -> None:
        """Start the game and load the commander through the Load screen (F8 on the title)."""
        m = self.m
        m.boot()
        m.press(0x19, 0x32, 0x1E, ENTER)  # sound P, graphics M, a word for the protection
        m.run(stop=lambda m: self.title >= 5)
        self.keys(F8)
        self.keys(*[DOWN] * self.index, ENTER, ticks=100)
        block = bytes(m.mu.mem_read(DS * 16 + COMMANDER, COMMANDER_SIZE))
        if block != self.file:
            raise RuntimeError(f"{self.name}: the commander block is not the file")
        self.keys(SPACE, ticks=200)  # the station; an offer waits for Y/N
        self.keys(Y, ticks=100)
        self.keys(SPACE, ticks=100)
        self.note(f"loaded at {self.system().decode()}")

    def launch(self) -> bool:
        """F1 from the station, through the tunnel."""
        self.keys(F1, ticks=200)
        self.frames(20)
        return self.in_flight()

    def collect(self, frames: int, every: int, keys: list[int]) -> None:
        """Fly with random held keys, saving every `every`-th frame; release them at the end."""
        held: set[int] = set()
        end, last = self.flight + frames, self.flight
        limit = self.m.ticks + 4 * frames + 8

        def stop(m: Machine) -> bool:
            nonlocal last
            n = self.flight
            if n == last:
                return m.ticks > limit
            last = n
            if n % every == 0:
                path = os.path.join(self.outdir, f"mission-{self.name.lower()}-{n:05d}.bin")
                save(m, path)
                self.saved.append(path)
            if not any(e != 8 for e in m.pending) and self.rng.random() < 0.3:
                k = self.rng.choice(keys)
                if k in held:
                    held.discard(k)
                    m.scancode_event(k | 0x80)
                else:
                    held.add(k)
                    m.scancode_event(k)
            return n >= end or not self.in_flight()

        self.m.run(stop=stop, idle_ticks=True)
        for k in held:
            self.m.scancode_event(k | 0x80)
        self.frames(SETTLE)

    def hyperspace(self) -> bool:
        """Chart (F4), hyperspace (F6) to the selected system; True once out of the old one
        (at the target, or in witchspace after a misjump)."""
        if not self.m.r16(SELECTED_DISTANCE):
            self.note("no hyperspace: no system selected")
            return False
        before = self.system()
        self.keys(F4)
        self.keys(F6)
        limit = self.m.ticks + 3000
        self.m.run(stop=lambda m: self.system() != before or m.ticks > limit, idle_ticks=True)
        if self.system() == before:
            self.note("no hyperspace (out of range, or no fuel)")
            return False
        self.frames(60)
        where = "witchspace (a misjump)" if self.r8(WITCHSPACE) else self.system().decode()
        self.note(f"arrived in {where}, mission {self.r8(MISSION)}")
        return self.in_flight()

    def station_view(self) -> Vec:
        """Where the station is in the view (x right, y down, z ahead).

        Its slot keeps the position relative to the player in space's axes; the view's are
        only computed in range. The rotation between them follows from the sun and the planet,
        which are in both (any distance): the same two directions in either basis."""
        pos = [self._position(self.slot(n)) for n in (1, 0, 2)]
        cam = [struct.unpack("<3h", self.slot(n)[0x10:0x16]) for n in (1, 0)]
        world = _triad(pos[0], pos[1])
        view = _triad((cam[0][0], cam[0][1], cam[0][2]), (cam[1][0], cam[1][1], cam[1][2]))
        k = [_dot(pos[2], e) for e in world]
        x, y, z = (sum(k[j] * view[j][i] for j in range(3)) for i in range(3))
        return x, y, z

    @staticmethod
    def _position(slot: bytes) -> Vec:
        """A slot's position: three 24-bit signed values (high byte +01.., low word +04..)."""
        out = []
        for k in range(3):
            v = slot[1 + k] << 16 | slot[4 + 2 * k] | slot[5 + 2 * k] << 8
            out.append(float(v - (1 << 24) if v >> 23 else v))
        return out[0], out[1], out[2]

    def steer(self) -> None:
        """One step toward the station: a roll tap to bring it onto the vertical axis, else a
        pitch tap toward it, then the turn settles. Far off, the axis is always the upper half
        (the nearer half flips as the station crosses the horizontal, and the rolls with it)."""
        x, y, z = self.station_view()
        off = math.degrees(math.atan2(math.hypot(x, y), z))
        if off > 1.5:
            # angle of (x, y) from the vertical half-axis, clockwise; RIGHT rolls it back
            up = y < 0 or off > 10
            roll = math.degrees(math.atan2(x, -y)) if up else -math.degrees(math.atan2(x, y))
            if abs(roll) > 8 and off > 3:
                key, error = (RIGHT if roll > 0 else LEFT), min(abs(roll), 3 * off)
            else:
                key, error = (DOWN if up else UP), off
            hold = next((h for least, h in TAPS if error >= least), 0)
            if hold:
                self.tap(key, hold)
        self.frames(SETTLE)

    def approach(self, frames: int = 20000) -> bool:
        """Head for the station at full speed until the safe zone; False if it does not come."""
        flags = self.slot(2)[0]
        if not flags & 1 or (flags >> 1) & 0x1F > 1:  # type 0 Coriolis, 1 Dodecahedron
            self.note("no station")
            return False
        self.tap(FASTER, 60)
        end = self.flight + frames
        while not self.r8(SAFE_ZONE) & 1:
            if self.flight > end or not self.in_flight():
                why = "killed" if self.r8(DEAD) else "time out" if self.in_flight() else "flight left"
                self.note(f"the safe zone not reached ({why})")
                return False
            self.steer()
            x, y, z = self.station_view()
            far = z > JUMP_FAR and math.hypot(x, y) < z / 10  # far, and about ahead
            if far != bool(self.r8(JUMP)):  # the jump drive would overshoot the station
                self.m.press(F11)
        self.note("safe zone")
        return True

    def dock(self) -> bool:
        """The docking computer (F1) to the station, then past the briefing (any key)."""
        self.keys(F1, ticks=10)
        limit = self.m.ticks + 30000
        self.m.run(stop=lambda m: not self.in_flight() or m.ticks > limit, idle_ticks=True)
        if self.r8(SCREEN) != 1 or self.r8(DEAD):
            self.note("not docked (refused, or the station hostile)")
            return False
        self.note(f"docked, mission {self.r8(MISSION)}")
        self.keys(SPACE, ticks=200)
        self.keys(Y, ticks=100)
        self.keys(SPACE, ticks=100)
        return True


def fly(name: str, files: dict[str, bytearray], frames: int, every: int, outdir: str) -> Pilot:
    """Run one save through the steps of the module docstring as far as they go."""
    p = Pilot(name, files, outdir)
    p.load()
    if not p.launch():
        return p
    p.collect(frames, every, CALM_KEYS)
    if not p.in_flight() or not p.hyperspace():
        return p
    p.collect(frames, every, CALM_KEYS)
    if p.in_flight() and p.approach() and p.dock() and p.launch():
        p.collect(2 * frames, every, KEYS)
    return p


def collect_saves(savedir: str, frames: int, every: int, outdir: str) -> int:
    """Every DIR/*.CDR flown; returns how many states were saved."""
    files: dict[str, bytearray] = {}
    for entry in sorted(os.listdir(savedir)):
        if entry.upper().endswith(".CDR"):
            with open(os.path.join(savedir, entry), "rb") as f:
                files[entry.upper()] = bytearray(f.read())
    total = 0
    for name in files:
        p = fly(name, {k: bytearray(v) for k, v in files.items()}, frames, every, outdir)
        print(f"{p.name}: {len(p.saved)} states")
        for line in p.log:
            print("   ", line)
        total += len(p.saved)
    return total
