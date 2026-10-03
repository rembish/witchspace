"""Differential test: filled circles (planets, sun), original (emulated) vs core.

usage: circletest.py [n] [seed] [path/to/ep_circledump]

Random circles (centre and radius on and off the 3D view, detail mask ds:108f 0/1/3/7,
outline mode ds:1091) and RNG states through draw_circle (2ab9) in the EGA code path. The
final span routine (1514) is replaced by a log of (x, width, row); the RNG state after each
circle is compared too, since masked spans step it.
"""
import os
import random
import subprocess
import sys

from eliteemu import Elite

HERE = os.path.dirname(os.path.abspath(__file__))
N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
SEED = int(sys.argv[2]) if len(sys.argv) > 2 else 1
DUMP = sys.argv[3] if len(sys.argv) > 3 else os.path.join(HERE, "..", "..", "build", "ep_circledump")


def s16(v):
    return v - 65536 if v >= 32768 else v


def cases(rng):
    out = []
    for _ in range(N):
        r = rng.choice([rng.randint(1, 8), rng.randint(1, 60), rng.randint(1, 300), rng.randint(-3, 0x1F5)])
        x = rng.choice([rng.randint(0, 303), rng.randint(-400, 700), rng.choice([-r, 0x130 + r - 1, -r + 1, 0x130 + r])])
        y = rng.choice([rng.randint(0, 123), rng.randint(-300, 500), rng.choice([-r, 0x7C + r - 1, -r + 1])])
        mask = rng.choice([0, 1, 3, 7])
        outline = 1 if rng.random() < 0.3 else 0
        w = [rng.getrandbits(16) for _ in range(4)]
        out.append([x, y, r, mask, outline] + w)
    return out


def original(cs):
    e = Elite()
    log = []
    e.hook(0x1514, lambda e, r: log.append(f"{s16(r['bx'])},{s16(r['cx']) or 1 if s16(r['cx']) >= 0 else None},"
                                           f"{(s16(r['di']) - 0x168) // 0x28} ") if s16(r["cx"]) >= 0 else None)
    lines = []
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


def main():
    cs = cases(random.Random(SEED))
    want = original(cs)
    inp = "".join(" ".join(str(v) for v in c) + "\n" for c in cs)
    got = subprocess.run([DUMP], input=inp, capture_output=True, text=True, check=True).stdout.split("\n")[:-1]
    bad = [(c, a, b) for c, a, b in zip(cs, want, got) if a != b]
    for c, a, b in bad[:4]:
        print(f"case {c}\noriginal: {a[:300]}\ncore:     {b[:300]}\n")
    spans = sum(a.count(",") // 2 for a in want)
    print(f"{len(want)} circles, {spans} spans, {len(bad)} differ" + (
        "" if len(got) == len(want) else f" (core printed {len(got)})"))
    sys.exit(1 if bad or len(got) != len(want) else 0)


if __name__ == "__main__":
    main()
