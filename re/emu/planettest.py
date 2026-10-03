"""Differential test: planet and sun positioning, original (emulated) vs core.

usage: planettest.py [n] [seed] [path/to/ep_planetdump]

Random planet/sun slots (24-bit positions from near to the far edge, random flags) and player
angles through planet_to_camera (433c, outside flight) and apparent_size (4694) with sizes 100
(planet) and 50 (sun). Compares the sizes and all slot bytes.
"""
import os
import random
import subprocess
import sys

from eliteemu import Elite

HERE = os.path.dirname(os.path.abspath(__file__))
N = int(sys.argv[1]) if len(sys.argv) > 1 else 5000
SEED = int(sys.argv[2]) if len(sys.argv) > 2 else 1
DUMP = sys.argv[3] if len(sys.argv) > 3 else os.path.join(HERE, "..", "..", "build", "ep_planetdump")
SLOT = 0x76DE


def cases(rng):
    out = []
    for _ in range(N):
        b = bytearray(rng.getrandbits(8) for _ in range(64))
        b[0] = rng.choice([0x3D, 0x3F]) | (rng.getrandbits(2) << 6)
        for k in range(3):
            bits = rng.choice([8, 12, 15, 18, 22, 24])
            v = rng.randint(-(1 << (bits - 1)), (1 << (bits - 1)) - 1) & 0xFFFFFF
            b[1 + k] = v >> 16
            b[4 + 2 * k:6 + 2 * k] = (v & 0xFFFF).to_bytes(2, "little")
        pl = [rng.randrange(2048) if rng.random() < 0.6 else 0 for _ in range(3)]
        x = rng.randrange(2048) if rng.random() < 0.2 else 0
        out.append((pl, x, bytes(b)))
    return out


def original(cs):
    e = Elite()
    e.w8(0xAF18, 0)
    lines = []
    for pl, x, b in cs:
        for k in range(3):
            e.w16(0x76D8 + 2 * k, pl[k])
        e.w16(0xB0DE, x)
        e.call(0x6D0A, ax=pl[0])  # rotation slots, as update_objects sets them
        e.call(0x6D0F, ax=pl[1])
        e.call(0x6D14, ax=pl[2])
        e.wb(SLOT, b)
        e.call(0x433C, di=SLOT)
        s1 = e.call(0x4694, di=SLOT, dx=100, ax=0)["ax"]
        s2 = e.call(0x4694, di=SLOT, dx=50, ax=0)["ax"]
        lines.append(f"{s1} {s2} " + e.rb(SLOT, 64).hex())
    return lines


def main():
    cs = cases(random.Random(SEED))
    want = original(cs)
    inp = "".join(f"{pl[0]} {pl[1]} {pl[2]} {x} {b.hex()}\n" for pl, x, b in cs)
    got = subprocess.run([DUMP], input=inp, capture_output=True, text=True, check=True).stdout.split("\n")[:-1]
    bad = [(c, a, b) for c, a, b in zip(cs, want, got) if a != b]
    for c, a, b in bad[:4]:
        print(f"original: {a}\ncore:     {b}\n")
    print(f"{len(want)} planets, {len(bad)} differ" + ("" if len(got) == len(want) else f" (core printed {len(got)})"))
    sys.exit(1 if bad or len(got) != len(want) else 0)


if __name__ == "__main__":
    main()
