"""Collect game states from the missions: real v3.1 commander saves flown in the original.

usage: corpus.py --saves DIR [frames] [every] [outdir]

For each DIR/*.CDR (George Hooper's mission saves, say): boot the original in machine.py with
the saves as its files, load the commander through the game's own Load screen (title, F8, the
cursor down to the name, Enter, space) and check the commander block (ds:82db) is the file.
Then, as a player would:

1. answer the station's offer if there is one (Y: JAMESON's Tribble), launch (F1) and fly;
2. hyperspace to the save's selected system (chart F4, hyperspace F6): the jump counters start
   the mission on arrival (ds:83a0), and fly there;
3. unless in witchspace, head for the station (roll and pitch taps toward it, or beside the
   planet while it is in the way, the jump drive F11 while far ahead) until the safe zone
   (ds:7680 bit 0), dock with the docking computer (F1), read the briefing (any key, Y for
   the refugees), buy fuel (F6, F9), launch again and fly;
4. into the mission, where its ships come (core/ep_ships.c, ep_station.c):
   - 1, refugees: the arrival's leak took all the fuel and none is sold to them: out by the
     galactic hyperdrive (chart F4, F7), and docked there (the debriefing);
   - 2, the convoy: one jump (the next would lose it), the leader (type 24, +1e bit 5) and
     its escorts appear;
   - 3, the siege: one jump; that station is hostile and Thargoids come in its safe zone;
   - 4, the stolen Viper (type 28, +25 = 1): only in the briefing's system (ds:83a3);
   - 5, the plans: Thargoids (22) anywhere out of the safe zone, none while other hostiles
     are about (the energy bomb, F9, clears them once); then delivered to ds:83a3;
   - 6, the asteroids (type 5, +25 = 2): only in the briefing's system. ELITE is under way
     already (no briefing): it goes there from the start.
   A system away is reached hop by hop (at most 7.0 light years, the fewest hops, the
   safer governments first): each picked on the galactic chart (F4 twice) by name (F9,
   typed, Enter), and at each stop the station docked and the tank filled. The systems'
   names and places come from the game's memory (the galaxy seeds ds:5509, the digrams
   ds:5585), as the chart makes them.

Each flight window holds random keys as corpus.py does (fire only in the mission, so the
station stays friendly for the docking) and saves the whole memory every `every`-th flight
frame as OUTDIR/mission-<save>-<frame>.bin (frame: counted from the load). In the mission's
own window a state is saved only while its ships are there (at most MISSION_STATES). A step
that cannot be done (no target, a misjump: no station, a station under siege, death) ends the
run there; what was collected stays. Deterministic: the random keys are seeded with the
save's name.

The images and the saves are your own copies of the game's data: keep them out of the
repository (the default OUTDIR, re/emu/corpus/, is git-ignored).
"""

import heapq
import math
import os
import random
import struct
from collections import Counter
from collections.abc import Callable
from typing import Any, Final, NamedTuple

from unicorn import UC_HOOK_CODE

from corpus import FLIGHT_TOP, KEYS, TITLE_TOP, save
from eliteemu import Uc
from machine import CS, DS, Machine

# scancodes (set 1)
F1: Final = 0x3B
F4: Final = 0x3E
F5: Final = 0x3F
F6: Final = 0x40
F7: Final = 0x41
F8: Final = 0x42
F9: Final = 0x43
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
LETTERS: Final = {
    c: code
    for row, first in (("QWERTYUIOP", 0x10), ("ASDFGHJKL", 0x1E), ("ZXCVBNM", 0x2C))
    for code, c in enumerate(row, first)
}

# data segment
COMMANDER: Final = 0x82DB
COMMANDER_SIZE: Final = 226
GALAXY_SEEDS: Final = 0x5509  # three words a galaxy: its first system
GALAXY: Final = 0x8315
ZOOM: Final = 0x831E  # 1: the short-range chart
CURRENT_SYSTEM: Final = 0x831F  # its record: the name first, +0a its index
SELECTED: Final = 0x8338  # the selected system's record: +0a its index, +0b distance word
FUEL: Final = 0x8356
BOMB: Final = 0x835E  # the energy bomb fitted (equipment 7)
GALACTIC_DRIVE: Final = 0x8361  # the galactic hyperdrive fitted (equipment 10)
DIGRAMS: Final = 0x5585  # the names' letter pairs, 32
SLOTS: Final = 0x76DE  # 64-byte object slots: 0 sun, 1 planet, 2 station, 3..19 ships
SHIP_SLOTS: Final = range(3, 20)
SCREEN: Final = 0x02F9  # 0 flight, 1/2 docked screens, 5 title
SAFE_ZONE: Final = 0x7680  # bit 0: near the station
JUMP: Final = 0xB0DD  # jump drive engaged
WITCHSPACE: Final = 0x83A4
MISSION: Final = 0x83A0
MISSION_SYSTEM: Final = 0x83A3  # missions 4..6: the briefing's system
BRIEFED: Final = 0x83B0  # 1 once the briefing was shown
STATION_ANGRY: Final = 0x83AA
DEAD: Final = 0x76BD

MISSION_STATES: Final = 15  # states saved at most in a mission's own window
NAMES: Final = {
    5: "asteroid",
    7: "Thargon",
    18: "escort",
    19: "escort",
    22: "Thargoid",
    24: "Asp",
    28: "Viper",
}

# Roll or pitch held this many frames turns the view by about 1.4, 4.5, 11 and 23 degrees once
# the rate has died down (measured in the original): (least error in degrees, frames held).
TAPS: Final = ((20.0, 6), (9.0, 5), (3.5, 4), (1.2, 3))
SETTLE: Final = 25  # frames for the turn rate to die down after a tap
JUMP_FAR: Final = 100_000  # the jump drive only with the station this far ahead (else it passes it)
PLANET_CRASH: Final = 12_700  # the planet's centre this near: a crash (measured)
HOP: Final = 0x47  # hyperspace range: distance words below this (tenths of a light year)

type Vec = tuple[float, float, float]


class System(NamedTuple):
    name: str
    x: int
    y: int
    government: int


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


def _twist(s: list[int]) -> None:
    s[0], s[1], s[2] = s[1], s[2], (s[0] + s[1] + s[2]) & 0xFFFF


def galaxy(seed: tuple[int, int, int], digrams: bytes) -> list[System]:
    """The 256 systems from a galaxy's seed, as 5f00 and the name generator 6130 make them."""
    s = list(seed)
    out = []
    for _ in range(256):
        x, y, government = s[1] >> 8, s[0] >> 9, s[1] >> 3 & 7
        name = bytearray(b"    " * 3)
        n, long = 0, s[0] & 0x40
        for k in range(4, 0, -1):  # the pairs written in place, advancing past letters only
            i = s[2] >> 8 & 0x1F
            _twist(s)
            if k == 1 and not long:
                continue
            a, b = digrams[2 * i], digrams[2 * i + 1]
            name[n : n + 2] = bytes((a, b))
            n += (b != 0x20) + (a != 0x20)
        out.append(System(name[:n].decode("ascii"), x, y, government))
    return out


def distance(a: System, b: System) -> int:
    """The chart's distance in tenths of a light year (an integer square root, times 4)."""
    return math.isqrt((a.x - b.x) ** 2 + (a.y - b.y) ** 2) * 4


def route(systems: list[System], start: int, goal: int) -> list[int]:
    """The fewest hops within range, the safer governments (7 corporate .. 0 anarchy) first;
    the systems after the start ([] if none)."""
    best = {start: (0, 0)}
    back: dict[int, int] = {}
    todo = [(0, 0, start)]
    while todo:
        hops, danger, at = heapq.heappop(todo)
        if at == goal:
            break
        if best[at] < (hops, danger):
            continue
        for n, s in enumerate(systems):
            if 0 < distance(systems[at], s) < HOP:
                cost = (hops + 1, danger + 7 - s.government)
                if cost < best.get(n, (1 << 30, 0)):
                    best[n], back[n] = cost, at
                    heapq.heappush(todo, (*cost, n))
    if goal not in back:
        return []
    path = [goal]
    while path[-1] != start:
        path.append(back[path[-1]])
    return path[-2::-1]


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

    def scancode(self, code: int) -> None:
        """A scancode through INT 9 at the next resume, ahead of the timer ticks queued (behind
        them it comes a varying number of frames late, and taps go wrong)."""
        self.m.pending.insert(0, ("kbd", code))

    def tap(self, key: int, frames: int) -> None:
        """Hold a key for some flight frames (counted from when it is down)."""
        self.scancode(key)
        self.frames(frames)
        self.scancode(key | 0x80)

    # ---- state ----
    def r8(self, a: int) -> int:
        return self.m.r8(a)

    def system(self) -> bytes:
        """The current system's name."""
        return bytes(self.m.mu.mem_read(DS * 16 + CURRENT_SYSTEM, 10)).split(b"\0")[0]

    def slot(self, n: int) -> bytes:
        return bytes(self.m.mu.mem_read(DS * 16 + SLOTS + 64 * n, 64))

    def ships(self) -> list[bytes]:
        """The ship slots in use."""
        return [s for s in map(self.slot, SHIP_SLOTS) if s[0] & 1]

    def in_flight(self) -> bool:
        return self.r8(SCREEN) == 0 and not self.r8(DEAD)

    def note(self, text: str) -> None:
        self.log.append(f"{self.flight:5d} {text}")

    def systems(self) -> list[System]:
        """This galaxy's systems, from its seed and the digrams in the game's memory."""
        at = DS * 16 + GALAXY_SEEDS + 6 * self.r8(GALAXY)
        seed = struct.unpack("<3H", bytes(self.m.mu.mem_read(at, 6)))
        return galaxy((seed[0], seed[1], seed[2]), bytes(self.m.mu.mem_read(DS * 16 + DIGRAMS, 64)))

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

    def collect(
        self,
        frames: int,
        every: int,
        keys: list[int],
        wanted: Callable[[list[bytes]], bool] | None = None,
        bomb: bool = False,
    ) -> None:
        """Fly with random held keys, saving every `every`-th frame; release them at the end.

        With `wanted`, only the frames whose ships it accepts are saved, MISSION_STATES at
        most, and the ship types of those states are noted (in how many states each). With
        `bomb`, the energy bomb (F9) goes off at the first hostile (AI class 5) while nothing
        wanted is there: Thargoids do not come while one is about."""
        held: set[int] = set()
        end, last, count = self.flight + frames, self.flight, 0
        limit = self.m.ticks + 4 * frames + 8
        seen: Counter[str] = Counter()
        armed = [True] if bomb and self.r8(BOMB) == 1 else []

        def stop(m: Machine) -> bool:
            nonlocal last, count
            n = self.flight
            if n == last:
                return m.ticks > limit
            last = n
            ships = self.ships()
            if armed and wanted and not wanted(ships) and any(s[0x33] == 5 for s in ships):
                m.press(F9)
                armed.clear()
                self.note("energy bomb")
            if n % every == 0:
                if wanted is None or wanted(ships):
                    path = os.path.join(self.outdir, f"mission-{self.name.lower()}-{n:05d}.bin")
                    save(m, path)
                    self.saved.append(path)
                    count += 1
                    if wanted:
                        seen.update({NAMES.get(s[0] >> 1 & 0x1F, "other") for s in ships})
            if not any(e != 8 for e in m.pending) and self.rng.random() < 0.3:
                k = self.rng.choice(keys)
                if k in held:
                    held.discard(k)
                    m.scancode_event(k | 0x80)
                else:
                    held.add(k)
                    m.scancode_event(k)
            return n >= end or not self.in_flight() or (wanted is not None and count >= MISSION_STATES)

        self.m.run(stop=stop, idle_ticks=True)
        for k in held:
            self.m.scancode_event(k | 0x80)
        self.frames(SETTLE)
        if self.r8(DEAD):
            self.note("killed")
        if wanted:
            types = ", ".join(f"{t} in {c}" for t, c in sorted(seen.items()))
            self.note(f"{count} mission states ({types or 'no ships'})")

    def select(self, target: int) -> bool:
        """A system picked on the galactic chart by name (F4 twice, F9, the name, Enter)."""
        name = self.systems()[target].name
        if not all(c in LETTERS for c in name):
            self.note(f"{name!r} cannot be typed")
            return False
        self.keys(F4)
        for _ in range(2):
            if self.r8(ZOOM):
                self.keys(F4)
        self.keys(F9)
        self.keys(*[LETTERS[c] for c in name], ENTER)
        if self.r8(SELECTED + 0x0A) != target:
            self.note(f"{name} not found on the chart")
            return False
        return True

    def hyperspace(self, target: int | None = None) -> bool:
        """Hyperspace (F6) from the chart (F4: the short-range one with the save's selection,
        or `target` picked by name); True once in flight at the target (False in witchspace
        after a misjump: no station there)."""
        if target is None:
            self.keys(F4)
        elif not self.select(target):
            return False
        dist = self.m.r16(SELECTED + 0x0B)
        if not dist or dist >= HOP or self.r8(FUEL) * 10 // 36 < dist:
            self.note(f"no hyperspace: distance {dist}, fuel {self.r8(FUEL)}")
            self.keys(F5)  # back to the view
            return False
        before = self.system()
        self.keys(F6)
        limit = self.m.ticks + 3000
        self.m.run(stop=lambda m: self.system() != before or m.ticks > limit, idle_ticks=True)
        if self.system() == before:
            self.note("no hyperspace (under siege?)")
            return False
        self.frames(60)
        where = "witchspace (a misjump)" if self.r8(WITCHSPACE) else self.system().decode()
        self.note(f"arrived in {where}, mission {self.r8(MISSION)}")
        return self.in_flight() and not self.r8(WITCHSPACE)

    def view(self, at: Vec) -> Vec:
        """A point in space's axes (relative to the player, as the slots keep positions) in
        the view's (x right, y down, z ahead).

        The view's axes are only computed for objects in range. The rotation between them
        follows from the sun and the planet, which are in both (any distance): the same two
        directions in either basis."""
        pos = [self.position(self.slot(n)) for n in (1, 0)]
        cam = [struct.unpack("<3h", self.slot(n)[0x10:0x16]) for n in (1, 0)]
        world = _triad(pos[0], pos[1])
        view = _triad((cam[0][0], cam[0][1], cam[0][2]), (cam[1][0], cam[1][1], cam[1][2]))
        k = [_dot(at, e) for e in world]
        x, y, z = (sum(k[j] * view[j][i] for j in range(3)) for i in range(3))
        return x, y, z

    def aim(self) -> Vec:
        """Where to head for the station: straight at it, or while the planet is in the way
        (the line passing within 1.5 crash distances of its centre), beside the planet at
        twice the crash distance, on the station's side."""
        planet, station = (self.position(self.slot(n)) for n in (1, 2))
        t = max(0.0, min(_dot(planet, station) / _dot(station, station), 1.0))
        miss = math.dist(planet, tuple(t * a for a in station))
        if t > 0.9 or miss > 1.5 * PLANET_CRASH:
            return station
        to = [a - b for a, b in zip(station, planet, strict=True)]
        u = _norm(planet)
        d = _dot((to[0], to[1], to[2]), u)
        side = [a - d * b for a, b in zip(to, u, strict=True)]
        if math.hypot(*side) < 1:  # right behind it: any way round
            side = [u[1], -u[0], 0.0] if abs(u[2]) < 0.9 else [0.0, u[2], -u[1]]
        e = _norm((side[0], side[1], side[2]))
        return (
            planet[0] + 2 * PLANET_CRASH * e[0],
            planet[1] + 2 * PLANET_CRASH * e[1],
            planet[2] + 2 * PLANET_CRASH * e[2],
        )

    @staticmethod
    def position(slot: bytes) -> Vec:
        """A slot's position: three 24-bit signed values (high byte +01.., low word +04..)."""
        out = []
        for k in range(3):
            v = slot[1 + k] << 16 | slot[4 + 2 * k] | slot[5 + 2 * k] << 8
            out.append(float(v - (1 << 24) if v >> 23 else v))
        return out[0], out[1], out[2]

    def steer(self, target: Vec) -> None:
        """One step toward a point (space's axes): a roll tap to bring it onto the vertical
        axis, else a pitch tap toward it, then the turn settles. Far off, the axis is always
        the upper half (the nearer half flips as the point crosses the horizontal, and the
        rolls with it)."""
        x, y, z = self.view(target)
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
            target = self.aim()
            self.steer(target)
            x, y, z = self.view(target)
            far = z > JUMP_FAR and math.hypot(x, y) < z / 10  # far, and about ahead
            if far != bool(self.r8(JUMP)):  # the jump drive would overshoot the station
                self.tap(F11, 1)
        self.note("safe zone")
        return True

    def dock(self) -> bool:
        """The docking computer (F1) to the station, then past the briefing or the debriefing
        (any key; Y takes mission 1's refugees)."""
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

    def galactic_jump(self) -> bool:
        """The galactic hyperdrive (chart F4, F7): no fuel needed; True once in the next
        galaxy."""
        if self.r8(GALACTIC_DRIVE) != 1:
            return False
        galaxy = self.r8(GALAXY)
        self.keys(F4)
        self.keys(F7)
        limit = self.m.ticks + 3000
        self.m.run(stop=lambda m: self.r8(GALAXY) != galaxy or m.ticks > limit, idle_ticks=True)
        if self.r8(GALAXY) == galaxy:
            self.note("no galactic jump")
            return False
        self.frames(60)
        self.note(f"galactic jump to {self.system().decode()} in galaxy {self.r8(GALAXY) + 1}")
        return self.in_flight()

    def refuel(self) -> None:
        """A full tank from the equipment screen (F6; fuel is its first row, F9 buys)."""
        if self.r8(FUEL) < 0xFB:
            self.keys(F6, ticks=100)
            self.keys(F9, ticks=100)
            self.note(f"fuel {self.r8(FUEL)}")

    def stop_over(self) -> bool:
        """At a system on the way: the station docked, the tank filled, launched again."""
        if not (self.approach() and self.dock()):
            return False
        self.refuel()
        return self.launch()

    def travel(self, goal: int) -> bool:
        """Hop by hop to a system, docking for fuel on the way; True when in flight there."""
        systems = self.systems()
        here = self.r8(CURRENT_SYSTEM + 0x0A)
        hops = route(systems, here, goal)
        self.note(f"to {systems[goal].name}: {' '.join(systems[n].name for n in hops) or 'no route'}")
        for n, hop in enumerate(hops):
            if n and not self.stop_over():
                return False
            if not self.hyperspace(hop):
                return False
        return bool(hops)

    def nearest(self) -> int | None:
        """The nearest other system in range of the fuel left."""
        systems = self.systems()
        here = systems[self.r8(CURRENT_SYSTEM + 0x0A)]
        reach = min(HOP - 1, self.r8(FUEL) * 10 // 36)
        near = [(distance(here, s), n) for n, s in enumerate(systems) if 0 < distance(here, s) <= reach]
        return min(near)[1] if near else None


def _of_type(*types: int) -> Callable[[list[bytes]], bool]:
    return lambda ships: any(s[0] >> 1 & 0x1F in types for s in ships)


def _tagged(tag: int) -> Callable[[list[bytes]], bool]:
    return lambda ships: any(s[0x25] == tag for s in ships)


def _convoy(ships: list[bytes]) -> bool:
    return any(s[0x1E] & 0x20 or s[0] >> 1 & 0x1F == 24 for s in ships)


def _leave_station(p: Pilot) -> None:
    """Out of the safe zone (Thargoids come only out of it): turned square to both the planet
    (straight on from the launch it would be hit) and the station, full speed straight on."""
    for _ in range(12):
        planet, station = (p.position(p.slot(n)) for n in (1, 2))
        p.steer(_cross(planet, station))
    p.tap(FASTER, 60)
    end = p.flight + 3000
    while p.r8(SAFE_ZONE) & 1 and p.in_flight() and p.flight < end:
        p.frames(50)


def mission(p: Pilot, frames: int, every: int) -> None:
    """After the briefing, docked: into the mission as far as it goes (module docstring)."""
    m = p.r8(MISSION)
    p.refuel()
    if not p.launch():
        return
    p.collect(2 * frames, every, CALM_KEYS)  # no fire yet: the station would turn on us
    many = 40 * frames
    match m:
        case 1:
            # The leak took all the fuel (none is sold with refugees aboard): out by the
            # galactic hyperdrive, else a jump within what is left. Docking anywhere else ends it.
            near = p.nearest()
            if p.in_flight() and (p.galactic_jump() or near is not None and p.hyperspace(near)):
                p.collect(frames, every, CALM_KEYS)
                if p.in_flight() and p.approach() and p.dock() and p.launch():
                    p.collect(frames, every, KEYS)
        case 2:
            near = p.nearest()
            if p.in_flight() and near is not None and p.hyperspace(near):
                p.collect(many, every, KEYS, _convoy)
        case 3:
            near = p.nearest()
            if p.in_flight() and near is not None and p.hyperspace(near) and p.approach():
                p.collect(many, every, CALM_KEYS, _of_type(22, 7))
        case 4 | 6:
            if p.in_flight() and p.travel(p.r8(MISSION_SYSTEM)):
                p.collect(many, every, KEYS, _tagged(1 if m == 4 else 2))
        case 5:
            if p.in_flight():
                _leave_station(p)  # a Thargoid comes about once in 1100 frames with no hostiles
                p.collect(4 * many, every, [FASTER], _of_type(22, 7), bomb=True)
            if p.in_flight() and p.travel(p.r8(MISSION_SYSTEM)) and p.approach() and p.dock():
                if p.launch():  # the plans delivered
                    p.collect(frames, every, KEYS)


def fly(name: str, files: dict[str, bytearray], frames: int, every: int, outdir: str) -> Pilot:
    """Run one save through the steps of the module docstring as far as they go."""
    p = Pilot(name, files, outdir)
    try:
        p.load()
        if not p.launch():
            return p
        p.collect(frames, every, CALM_KEYS)
        if p.r8(MISSION) and p.r8(BRIEFED):  # under way (ELITE): straight to its system
            if p.in_flight() and p.travel(p.r8(MISSION_SYSTEM)):
                p.collect(40 * frames, every, KEYS, _tagged(2 if p.r8(MISSION) == 6 else 1))
            return p
        if not p.in_flight() or not p.hyperspace():
            return p
        p.collect(frames, every, CALM_KEYS)
        if p.r8(MISSION) and p.in_flight() and p.approach() and p.dock():
            mission(p, frames, every)
    except RuntimeError as e:  # the machine stopped (an interrupt it does not handle)
        p.note(f"stopped: {e}")
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
