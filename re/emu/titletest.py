"""Frame test: the title screen, original (whole game in machine.py, MCGA) vs core.

usage: titletest.py [frames] [path/to/ep_titledump]

Boots the original (P, M, a word for the protection), snapshots the state at the start of
the first title frame (9f21) and records it at the start of every following frame: main RNG,
object slot 2, ship type ds:b1bb, hold timer ds:b25f, list position ds:b261, flash step
ds:1b3e, tick count ds:45e0 and last flip ds:267c, with the disc spans (MCGA span routine 16da) and the ship
primitives (172c, 1a7a, 261b) drawn during the frame. The core runs the same frames from the
snapshot. The machine's idle-tick fallback is off, so timing is the original's frame waits.
"""

import os
import subprocess
import sys
from typing import Any, Final

from unicorn import UC_HOOK_CODE

from eliteemu import Elite, Regs, Uc
from machine import CS, Machine

HERE: Final = os.path.dirname(os.path.abspath(__file__))
FRAMES: Final = int(sys.argv[1]) if len(sys.argv) > 1 else 600
DUMP: Final = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "..", "..", "build", "ep_titledump")
SLOT2: Final = 0x775E


def s16(v: int) -> int:
    """A word as signed."""
    return v - 65536 if v >= 32768 else v


def main() -> None:
    m = Machine()
    m.boot()
    m.press(0x19, 0x32, 0x1E, 0x1C)
    spans: list[str] = []
    prims: list[str] = []
    rows: list[tuple[str, str]] = []  # (state at the frame's start, drawing of the frame before)

    def state() -> str:
        return (
            " ".join(str(m.r16(0x205 + 2 * k)) for k in range(4))
            + f" {m.r8(0xB1BB)} {m.r16(0xB25F)} {m.r16(0xB261) - 0xB263} {m.r8(0x1B3E)}"
            + f" {m.r16(0x45E2) << 16 | m.r16(0x45E0)} {m.r16(0x267E) << 16 | m.r16(0x267C)} "
        )

    def frame(mu: Uc, a: int, s: int, u: Any) -> None:
        snap = state() + m.rb(SLOT2, 64).hex()
        rows.append((snap, " |" + "".join(spans) + " |" + "".join(prims)))
        spans.clear()
        prims.clear()

    def prim(kind: int, pts: list[int]) -> None:
        prims.append(f" {kind}:{m.r8(0x10A2)}" + "".join(f",{s16(v)}" for v in pts))

    def span(e: Elite, r: Regs) -> None:
        if s16(r["cx"]) >= 0:
            spans.append(f" {s16(r['bx'])},{s16(r['cx']) or 1},{(s16(r['di']) - 0x168) // 0x28}")

    def triangle(e: Elite, r: Regs) -> None:
        prim(0, [r["cx"], r["dx"], r["ax"], r["bx"], r["si"], r["bp"]])

    def quad(e: Elite, r: Regs) -> None:
        prim(2, [r["ax"], r["bx"], r["cx"], r["dx"], e.r16(0x10B0), e.r16(0x10B4), r["si"], r["bp"]])

    def line(e: Elite, r: Regs) -> None:
        prim(4, [r["cx"], r["dx"], r["ax"], r["bx"]])

    m.mu.hook_add(UC_HOOK_CODE, frame, begin=CS * 16 + 0x9F21, end=CS * 16 + 0x9F21)
    m.hook(0x16DA, span)
    m.hook(0x172C, triangle)
    m.hook(0x1A7A, quad)
    m.hook(0x261B, line)
    m.run(stop=lambda m: len(rows) > FRAMES)
    first = rows[0][0]
    want = [rows[i + 1][0] + rows[i + 1][1] for i in range(FRAMES)]
    # rows[i+1] holds the state after frame i and the drawing done during frame i
    words = first.split()
    inp = " ".join(words[:10]) + f" {FRAMES}\n" + words[10] + "\n"
    run = subprocess.run([DUMP], input=inp, capture_output=True, text=True, check=True)
    got = run.stdout.split("\n")[:-1]
    bad = [i for i, (a, b) in enumerate(zip(want, got, strict=False)) if a != b]
    for i in bad[:3]:
        print(f"frame {i}\noriginal: {want[i][:400]}\ncore:     {got[i][:400]}\n")
    print(
        f"{len(want)} title frames, {len(bad)} differ"
        + ("" if len(got) == len(want) else f" (core printed {len(got)})")
        + (f", first at frame {bad[0]}" if bad else "")
    )
    sys.exit(1 if bad or len(got) != len(want) else 0)


if __name__ == "__main__":
    main()
