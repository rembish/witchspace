"""Differential test: update_objects for ships, original (emulated) vs core.

usage: objtest.py [n] [seed] [path/to/ep_objdump]

Random object tables (1-12 slots, ship types 0-29, active or not, positions from close by to
out of range including 24-bit ones, random angles and flags) and player angles through
update_objects (4154) outside flight (ds:af18 = 0: no scanner or compass) without fuel scoops
(ds:835c = 0). Compares the slots drawn (in order), every primitive and all slot bytes after.
Explosion timers are left at 0: their end (7e82) is not reconstructed yet.
"""

import os
import random
import subprocess
import sys
from typing import Any, Final

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_SI

from eliteemu import CS, Elite, Regs, Uc

HERE: Final = os.path.dirname(os.path.abspath(__file__))
N: Final = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
SEED: Final = int(sys.argv[2]) if len(sys.argv) > 2 else 1
DUMP: Final = sys.argv[3] if len(sys.argv) > 3 else os.path.join(HERE, "..", "..", "build", "ep_objdump")
TABLE: Final = 0x76DE  # the object table, 64 bytes a slot

# A case: slot count, the player's three angles, the extra angle ds:b0de and the slots.
type Table = tuple[int, list[int], int, list[bytes]]


def s16(v: int) -> int:
    """A word as signed."""
    return v - 65536 if v >= 32768 else v


def tables(rng: random.Random) -> list[Table]:
    """N random object tables with player angles."""
    out: list[Table] = []
    for _ in range(N):
        count = rng.randint(1, 12)
        slots = []
        for _ in range(count):
            b = bytearray(rng.getrandbits(8) for _ in range(64))
            # draws in this order: type, active, flags
            kind = rng.randrange(30)
            active = 1 if rng.random() < 0.85 else 0
            b[0] = (kind << 1) | active | (rng.getrandbits(2) << 6)
            scale = rng.choice([300, 2000, 8000, 12500, 40000])
            edge = rng.random() < 0.3  # boundary values: range limit, near plane, view edges
            z = rng.choice([99, 100, 101, rng.randint(100, 12000)])
            for k in range(3):
                v = rng.randint(-scale, scale)
                if k == 2 and rng.random() < 0.7:
                    v = abs(v)
                if edge:
                    v = rng.choice(
                        [
                            0x2EDF,
                            0x2EE0,
                            0x2EE1,
                            -0x2EDF,
                            -0x2EE0,
                            z // 2,
                            -(z // 2),
                            z // 2 + 1,
                            -(z // 2) - 1,
                            z,
                            v,
                        ]
                    )
                v &= 0xFFFFFF
                if rng.random() < 0.03:
                    v = rng.getrandbits(24)
                b[1 + k] = v >> 16
                b[4 + 2 * k : 6 + 2 * k] = (v & 0xFFFF).to_bytes(2, "little")
            b[0x34] = 0
            b[0x1E] = rng.choice([0, 0, 2, 0x20, 0x40, 0x60, rng.getrandbits(8)])
            slots.append(bytes(b))
        pl = [rng.randrange(2048) if rng.random() < 0.6 else 0 for _ in range(3)]
        extra = rng.randrange(2048) if rng.random() < 0.2 else 0
        out.append((count, pl, extra, slots))
    return out


def original(ts: list[Table]) -> list[str]:
    """Slots drawn, primitives and slots after, from the emulated original."""
    e = Elite()
    log: list[str] = []
    drawn: list[int] = []  # slot numbers, in drawing order

    def prim(kind: int, pts: list[int]) -> None:
        log.append(f" {kind}:{e.r8(0x10A2)}" + "".join(f",{s16(v)}" for v in pts))

    def triangle(e: Elite, r: Regs) -> None:
        prim(0, [r["cx"], r["dx"], r["ax"], r["bx"], r["si"], r["bp"]])

    def quad(e: Elite, r: Regs) -> None:
        prim(2, [r["ax"], r["bx"], r["cx"], r["dx"], e.r16(0x10B0), e.r16(0x10B4), r["si"], r["bp"]])

    def line(e: Elite, r: Regs) -> None:
        prim(4, [r["cx"], r["dx"], r["ax"], r["bx"]])

    e.hook(0x172C, triangle)
    e.hook(0x1A7A, quad)
    e.hook(0x261B, line)

    def draw_ship(mu: Uc, a: int, s: int, u: Any) -> None:
        drawn.append((mu.reg_read(UC_X86_REG_SI) - TABLE) // 64)

    e.mu.hook_add(UC_HOOK_CODE, draw_ship, begin=CS * 16 + 0x43CE, end=CS * 16 + 0x43CE)
    e.w8(0xAF18, 0)
    e.w8(0x835C, 0)
    lines = []
    for count, pl, extra, slots in ts:
        e.w8(0x76B5, count)
        for k in range(3):
            e.w16(0x76D8 + 2 * k, pl[k])
        e.w16(0xB0DE, extra)
        for i, b in enumerate(slots):
            e.wb(TABLE + 64 * i, b)
        log.clear()
        drawn.clear()
        e.call(0x4154)
        after = " ".join(e.rb(TABLE + 64 * i, 64).hex() for i in range(count))
        lines.append("drawn" + "".join(f" {d}" for d in drawn) + " |" + "".join(log) + " | " + after)
    return lines


def main() -> None:
    ts = tables(random.Random(SEED))
    want = original(ts)
    inp = "".join(
        f"{c} {pl[0]} {pl[1]} {pl[2]} {x} " + "".join(b.hex() for b in sl) + "\n" for c, pl, x, sl in ts
    )
    run = subprocess.run([DUMP], input=inp, capture_output=True, text=True, check=True)
    got = run.stdout.split("\n")[:-1]
    bad = [(t, a, b) for t, a, b in zip(ts, want, got, strict=False) if a != b]
    for _t, a, b in bad[:3]:
        print(f"original: {a[:400]}\ncore:     {b[:400]}")
        wa, wb = a.split(" | ")[-1].split(), b.split(" | ")[-1].split()
        for i, (x, y) in enumerate(zip(wa, wb, strict=False)):
            if x != y:
                diff = [k for k in range(64) if x[2 * k : 2 * k + 2] != y[2 * k : 2 * k + 2]]
                print(f"  slot {i} differs at bytes {[hex(k) for k in diff]}")
        print()
    drawn = sum(len(a.split(" |")[0].split()) - 1 for a in want)
    print(
        f"{len(want)} tables, {drawn} ships drawn, {len(bad)} differ"
        + ("" if len(got) == len(want) else f" (core printed {len(got)})")
    )
    sys.exit(1 if bad or len(got) != len(want) else 0)


if __name__ == "__main__":
    main()
