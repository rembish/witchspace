"""Differential test: filled circles (planets, sun), original (emulated) vs core.

usage: circletest.py [n] [seed] [mcga] [path/to/ep_circledump]

Random circles (centre and radius on and off the 3D view, detail mask ds:108f 0/1/3/7,
outline mode ds:1091) and RNG states through draw_circle (2ab9) in the EGA code path. The
final span routine (1514) is replaced by a log of (x, width, row); the RNG state after each
circle is compared too, since masked spans step it. With "mcga", the emulator applies the
MCGA code patches (ds:1b1a, as set_video_mode does) and logs the MCGA span routine (16da).
"""

import os
import random
import subprocess
import sys
from typing import Final

from eliteemu import Elite, Regs

HERE: Final = os.path.dirname(os.path.abspath(__file__))
N: Final = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
SEED: Final = int(sys.argv[2]) if len(sys.argv) > 2 else 1
MCGA: Final = len(sys.argv) > 3 and sys.argv[3] == "mcga"
DUMP: Final = sys.argv[4] if len(sys.argv) > 4 else os.path.join(HERE, "..", "..", "build", "ep_circledump")


def s16(v: int) -> int:
    """A word as signed."""
    return v - 65536 if v >= 32768 else v


def cases(rng: random.Random) -> list[list[int]]:
    """N random circles: x, y, radius, detail mask, outline, then the four RNG words."""
    out: list[list[int]] = []
    for _ in range(N):
        r = rng.choice([rng.randint(1, 8), rng.randint(1, 60), rng.randint(1, 300), rng.randint(-3, 0x1F5)])
        # every candidate is drawn, in this order, before one is chosen (the RNG sequence)
        x_in, x_any = rng.randint(0, 303), rng.randint(-400, 700)
        x_edge = rng.choice([-r, 0x130 + r - 1, -r + 1, 0x130 + r])  # just on and off the view's sides
        x = rng.choice([x_in, x_any, x_edge])
        y_in, y_any = rng.randint(0, 123), rng.randint(-300, 500)
        y_edge = rng.choice([-r, 0x7C + r - 1, -r + 1])
        y = rng.choice([y_in, y_any, y_edge])
        mask = rng.choice([0, 1, 3, 7])
        outline = 1 if rng.random() < 0.3 else 0
        w = [rng.getrandbits(16) for _ in range(4)]
        out.append([x, y, r, mask, outline] + w)
    return out


def original(cs: list[list[int]]) -> list[str]:
    """The spans and the RNG after each circle, from the emulated original."""
    e = Elite()
    log: list[str] = []
    if MCGA:
        p = 0x1B1A
        while e.r16(p):
            e.mu.mem_write(0x10000 + e.r16(p), e.rb(p + 2, 2))
            p += 4

    def span(e: Elite, r: Regs) -> None:
        if s16(r["cx"]) >= 0:
            log.append(f"{s16(r['bx'])},{s16(r['cx']) or 1},{(s16(r['di']) - 0x168) // 0x28} ")

    e.hook(0x16DA if MCGA else 0x1514, span)
    lines: list[str] = []
    for x, y, r, mask, outline, *w in cs:
        for k in range(4):
            e.w16(0x205 + 2 * k, w[k])
        e.w16(0x108F, mask)
        e.w8(0x1091, outline)
        e.w8(0x10A2, 0)
        log.clear()
        e.call(0x2AB9, bx=x, cx=y, dx=r)
        st = " ".join(str(e.r16(0x205 + 2 * k)) for k in range(4))
        lines.append("".join(log) + "| " + st)
    return lines


def main() -> None:
    cs = cases(random.Random(SEED))
    want = original(cs)
    inp = "".join(" ".join(str(v) for v in c) + "\n" for c in cs)
    args = ["mcga"] if MCGA else []
    run = subprocess.run([DUMP, *args], input=inp, capture_output=True, text=True, check=True)
    got = run.stdout.split("\n")[:-1]
    bad = [(c, a, b) for c, a, b in zip(cs, want, got, strict=False) if a != b]
    for c, a, b in bad[:4]:
        print(f"case {c}\noriginal: {a[:300]}\ncore:     {b[:300]}\n")
    spans = sum(a.count(",") // 2 for a in want)
    print(
        f"{len(want)} circles, {spans} spans, {len(bad)} differ"
        + ("" if len(got) == len(want) else f" (core printed {len(got)})")
    )
    sys.exit(1 if bad or len(got) != len(want) else 0)


if __name__ == "__main__":
    main()
