"""Subsystem test on game states from the running original (corpus.py).

usage: subtest.py NAME [corpus glob] [path/to/ep_subsys]

For every state: the original's routine runs in the emulator on the whole memory image, the
core's on the data segment loaded through tests/statemap.c. Compares the data-segment bytes
the core models and the primitives drawn; lists the bytes the original changed that the core
does not model yet (what to reconstruct next).
"""
import collections
import glob
import os
import subprocess
import sys
import tempfile

from corpus import load
from eliteemu import DS, Elite

HERE = os.path.dirname(os.path.abspath(__file__))
NAME = sys.argv[1]
PATTERN = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "corpus", "*.bin")
TOOL = sys.argv[3] if len(sys.argv) > 3 else os.path.join(HERE, "..", "..", "build", "ep_subsys")

# Scratch space the original reuses within a routine (not state): INT 0 resume address, draw
# parameters, matrices and model temporaries, rotation temporary.
SCRATCH = [(0x01F8, 0x01F9), (0x1074, 0x10B5), (0x28D0, 0x28E5), (0x2B66, 0x2BF5), (0x76D6, 0x76D7)]

# name -> (address, registers)
ROUTINES = {
    "update_objects": (0x4154, {}),
}


def s16(v):
    return v - 65536 if v >= 32768 else v


def run_original(image, addr, regs):
    e = Elite()
    e.mu.mem_write(0, image)
    prims = []

    def prim(kind, pts):
        prims.append(f"{kind}:{e.r8(0x10A2)}" + "".join(f",{s16(v)}" for v in pts))

    e.hook(0x172C, lambda e, r: prim(0, [r["cx"], r["dx"], r["ax"], r["bx"], r["si"], r["bp"]]))
    e.hook(0x1A7A, lambda e, r: prim(2, [r["ax"], r["bx"], r["cx"], r["dx"], e.r16(0x10B0),
                                         e.r16(0x10B4), r["si"], r["bp"]]))
    e.hook(0x261B, lambda e, r: prim(4, [r["cx"], r["dx"], r["ax"], r["bx"]]))
    e.call(addr, **regs)
    return bytes(e.mu.mem_read(DS * 16, 0x10000)), prims


def main():
    addr, regs = ROUTINES[NAME]
    files = sorted(glob.glob(PATTERN))
    tmp = tempfile.mkdtemp()
    maskf = os.path.join(tmp, "mask")
    subprocess.run([TOOL, "mask", maskf], check=True)
    mask = open(maskf, "rb").read()
    bad, unmodelled = 0, collections.Counter()
    for path in files:
        image = load(path)
        before = image[DS * 16:DS * 16 + 0x10000]
        want, want_prims = run_original(image, addr, regs)
        inf, outf = os.path.join(tmp, "in"), os.path.join(tmp, "out")
        open(inf, "wb").write(before)
        out = subprocess.run([TOOL, NAME, inf, outf], capture_output=True, text=True, check=True).stdout
        got, got_prims = open(outf, "rb").read(), out.split()
        diff = [i for i in range(0x10000) if mask[i] and want[i] != got[i]]
        for i in range(0x10000):
            if not mask[i] and want[i] != before[i] and not any(a <= i <= b for a, b in SCRATCH):
                unmodelled[i] += 1
        if diff or want_prims != got_prims:
            bad += 1
            if bad <= 3:
                print(f"{os.path.basename(path)}: {len(diff)} modelled bytes differ "
                      f"{[hex(i) for i in diff[:12]]}, primitives {len(want_prims)} vs {len(got_prims)}"
                      + ("" if want_prims == got_prims else " (differ)"))
    print(f"{NAME}: {len(files)} states, {bad} differ")
    if unmodelled:
        runs, start, prev = [], None, None
        for i in sorted(unmodelled):
            if start is None:
                start = prev = i
            elif i == prev + 1:
                prev = i
            else:
                runs.append((start, prev))
                start = prev = i
        runs.append((start, prev))
        print("not modelled, changed by the original: " + " ".join(
            f"ds:{a:04x}" + (f"-{b:04x}" if b != a else "") + f"({unmodelled[a]})" for a, b in runs[:40]))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
