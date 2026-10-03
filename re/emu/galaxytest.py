"""Differential test: every system of every galaxy, original (emulated) vs core.

usage: galaxytest.py [path/to/ep_galdump]

The original side loads each galaxy seed with 5e25 and calls the system-data routine from
5f00 (the part of 5ee8 after the chart cursor search), which ends with the name generator
6130; that twists the seed four times, so consecutive calls walk the galaxy.
"""
import os
import subprocess
import sys

from eliteemu import Elite

HERE = os.path.dirname(os.path.abspath(__file__))
DUMP = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "build", "ep_galdump")


def original():
    e = Elite()
    lines = []
    for g in range(8):
        e.w8(0x8315, g)
        e.call(0x5E25)
        for n in range(256):
            w = [e.r16(0x5503 + 2 * k) for k in range(3)]
            e.w8(0x831E, 0)  # chart not zoomed: seed_to_chart stores the raw position
            e.call(0x5E95)
            x, y = e.r8(0x8318), e.r8(0x8319)
            e.wb(0x8349, b"\xee" * 4)
            e.call(0x5F00)
            sp = e.rb(0x8349, 4)
            species = " human" if sp[0] == 0xFF else "".join(f" {b}" for b in sp)
            if sp[0] == 0xFF and sp[1:] != b"\xee" * 3:
                species += " (species bytes written)"
            name = e.cstr(0x8338).decode("ascii")
            lines.append(
                f"{g} {n:3d} {w[0]:04x}{w[1]:04x}{w[2]:04x} {name:<8s} x{x:3d} y{y:3d} gov{e.r8(0x8345)} eco{e.r8(0x8346)} tech{e.r8(0x8347):2d} "
                f"pop{e.r8(0x8348):3d} prod{e.r16(0x834D):5d} rad{e.r16(0x834F):5d} sp{species} "
                f"desc {e.r16(0x8351):04x} {e.r16(0x8353):04x}")
    return lines


def main():
    want = original()
    got = subprocess.run([DUMP], capture_output=True, text=True, check=True).stdout.splitlines()
    bad = [(a, b) for a, b in zip(want, got) if a != b]
    if len(want) != len(got):
        print(f"line count differs: original {len(want)}, core {len(got)}")
    for a, b in bad[:10]:
        print(f"original: {a}\ncore:     {b}\n")
    print(f"{len(want)} systems, {len(bad)} differ")
    sys.exit(1 if bad or len(want) != len(got) else 0)


if __name__ == "__main__":
    main()
