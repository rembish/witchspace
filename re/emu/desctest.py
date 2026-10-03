"""Differential test: system descriptions, original (emulated) vs core.

usage: desctest.py [n_random] [path/to/ep_descdump]

Every system of every galaxy, then n_random random seeds (default 20000). The original side
zeroes the name buffer, runs system_data from 5f00, then describe_system (632b) up to the
point where the text is complete (6345), before it is drawn.
"""
import os
import random
import subprocess
import sys

from eliteemu import Elite

HERE = os.path.dirname(os.path.abspath(__file__))
N = int(sys.argv[1]) if len(sys.argv) > 1 else 20000
DUMP = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "..", "..", "build", "ep_descdump")


def seeds():
    e = Elite()
    out = []
    for g in range(8):
        e.w8(0x8315, g)
        e.call(0x5E25)
        for _ in range(256):
            out.append(tuple(e.r16(0x5503 + 2 * k) for k in range(3)))
            e.call(0x6124)
    rng = random.Random(1)
    out += [tuple(rng.getrandbits(16) for _ in range(3)) for _ in range(N)]
    return out


def original(seed_list):
    e = Elite()
    lines = []
    for w in seed_list:
        e.wb(0x8338, bytes(10))
        for k in range(3):
            e.w16(0x5503 + 2 * k, w[k])
        e.call(0x5F00)
        e.call(0x632B, until=0x6345)
        lines.append(e.cstr(0x5A3E, 257).decode("latin1"))
    return lines


def main():
    s = seeds()
    want = original(s)
    inp = "".join(f"{a:04x} {b:04x} {c:04x}\n" for a, b, c in s)
    got = subprocess.run([DUMP], input=inp, capture_output=True, text=True, encoding="latin1",
                         check=True).stdout.split("\n")[:-1]
    bad = [(w, a, b) for w, a, b in zip(s, want, got) if a != b]
    for w, a, b in bad[:8]:
        print(f"seed {w[0]:04x} {w[1]:04x} {w[2]:04x}\noriginal: {a!r}\ncore:     {b!r}\n")
    print(f"{len(want)} descriptions, {len(bad)} differ" + ("" if len(got) == len(want) else
          f" (core printed {len(got)})"))
    sys.exit(1 if bad or len(got) != len(want) else 0)


if __name__ == "__main__":
    main()
