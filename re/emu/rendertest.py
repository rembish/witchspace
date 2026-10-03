"""Differential test: ship rendering, original (emulated) vs core.

usage: rendertest.py [n] [seed] [path/to/ep_renderdump]

Random ship views (type, the object's three angles, its camera-space position, flags and the
player's angles) through draw_ship (43ce) in object slot 0, with the triangle (172c), quad
(1a7a) and line (261b) routines replaced by a log of their arguments and colour (ds:10a2).
Positions range from the far distance down to inside the ship, so projection overflows (the
divide-error path) are exercised too. One emulator runs all views, so the vertex buffer
carries over as in the game.
"""
import os
import random
import subprocess
import sys

from eliteemu import Elite

HERE = os.path.dirname(os.path.abspath(__file__))
N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
SEED = int(sys.argv[2]) if len(sys.argv) > 2 else 1
DUMP = sys.argv[3] if len(sys.argv) > 3 else os.path.join(HERE, "..", "..", "build", "ep_renderdump")
SLOT = 0x76DE


def s16(v):
    return v - 65536 if v >= 32768 else v


def views(rng):
    out = []
    for _ in range(N):
        t = rng.randrange(32)
        flags0 = (t << 1) | 1 | rng.choice([0xC0, 0x80, 0x40])
        ang = [rng.getrandbits(16) if rng.random() < 0.3 else rng.randrange(2048) for _ in range(3)]
        scale = rng.choice([30, 200, 1000, 5000, 16000])
        z = rng.randint(1, scale)
        cam = [rng.randint(-z, z), rng.randint(-z, z), z]
        if rng.random() < 0.05:
            cam = [rng.randint(-32768, 32767) for _ in range(3)]
        f1e = rng.choice([0, 0, 0, 0x20, 0x40, 0x60])
        pl = [rng.randrange(2048) if rng.random() < 0.5 else 0 for _ in range(3)]
        extra = rng.randrange(2048) if rng.random() < 0.2 else 0
        out.append([flags0] + ang + cam + [f1e] + pl + [extra])
    return out


def original(vs):
    e = Elite()
    log = []

    def prim(kind, pts):
        log.append(f"{kind}:{e.r8(0x10A2)} " + " ".join(str(s16(v)) for v in pts) + ";")

    e.hook(0x172C, lambda e, r: prim(0, [r["cx"], r["dx"], r["ax"], r["bx"], r["si"], r["bp"]]))
    e.hook(0x1A7A, lambda e, r: prim(2, [r["ax"], r["bx"], r["cx"], r["dx"], e.r16(0x10B0),
                                         e.r16(0x10B4), r["si"], r["bp"]]))
    e.hook(0x261B, lambda e, r: prim(4, [r["cx"], r["dx"], r["ax"], r["bx"]]))
    lines = []
    for v in vs:
        f0, a0, a1, a2, c0, c1, c2, f1e, p0, p1, p2, x = v
        e.wb(SLOT, bytes(0x40))
        e.w8(SLOT, f0)
        for k, a in enumerate((a0, a1, a2)):
            e.w16(SLOT + 0x0A + 2 * k, a)
        for k, c in enumerate((c0, c1, c2)):
            e.w16(SLOT + 0x10 + 2 * k, c)
        e.w8(SLOT + 0x1E, f1e)
        for k, p in enumerate((p0, p1, p2)):
            e.w16(0x76D8 + 2 * k, p)
        e.w16(0xB0DE, x)
        log.clear()
        e.call(0x43CE, si=SLOT)
        lines.append("".join(log))
    return lines


def main():
    vs = views(random.Random(SEED))
    want = original(vs)
    inp = "".join(" ".join(str(x) for x in v) + "\n" for v in vs)
    got = subprocess.run([DUMP], input=inp, capture_output=True, text=True, check=True).stdout.split("\n")[:-1]
    bad = [(v, a, b) for v, a, b in zip(vs, want, got) if a != b]
    for v, a, b in bad[:4]:
        print(f"view {v}\noriginal: {a[:300]}\ncore:     {b[:300]}\n")
    prims = sum(a.count(";") for a in want)
    print(f"{len(want)} views, {prims} primitives, {len(bad)} differ" + (
        "" if len(got) == len(want) else f" (core printed {len(got)})"))
    sys.exit(1 if bad or len(got) != len(want) else 0)


if __name__ == "__main__":
    main()
