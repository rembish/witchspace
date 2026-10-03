"""Subsystem test on game states from the running original (corpus.py).

usage: subtest.py NAME [corpus glob] [path/to/ep_subsys] [--fuzz N]

For every state: the original's routine runs in the emulator on the whole memory image, the
core's on the data segment loaded through tests/statemap.c. Compares the data-segment bytes
the core models and the primitives drawn; lists the bytes the original changed that the core
does not model yet (what to reconstruct next). With --fuzz N, each state is also tried N
times with random values in the fields the routine reads (FUZZ), to reach every branch.
"""
import collections
import random
import glob
import os
import subprocess
import sys
import tempfile

from corpus import load
from eliteemu import CS, DS, Elite
from unicorn import UC_HOOK_CODE

HERE = os.path.dirname(os.path.abspath(__file__))
ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
FUZZ_N = int(sys.argv[sys.argv.index("--fuzz") + 1]) if "--fuzz" in sys.argv else 0
if FUZZ_N:
    ARGS.remove(str(FUZZ_N))
NAME = ARGS[0]
PATTERN = ARGS[1] if len(ARGS) > 1 else os.path.join(HERE, "corpus", "*.bin")
TOOL = ARGS[2] if len(ARGS) > 2 else os.path.join(HERE, "..", "..", "build", "ep_subsys")

# Scratch space the original reuses within a routine (not state): INT 0 resume address, draw
# parameters, matrices and model temporaries, rotation temporary; and sound state (the core
# reports sounds as events).
SCRATCH = [(0x01F8, 0x01F9), (0x1074, 0x10BB), (0x10BD, 0x10C9), (0x28D0, 0x28E5), (0x2B66, 0x2BF5), (0x2CB1, 0x2CB2),
           (0x76D6, 0x76D7), (0x45E6, 0x45FF), (0x4FE0, 0x4FE0)]

EV_SOUND, EV_SURFACE = 1, 2

# name -> (address, registers, {exit address: line printed}) - exits are where a routine
# leaves without returning (the core reports them as a result line instead)
ROUTINES = {
    "update_objects": (0x4154, {}, {}),
    "message": (0x702A, {}, {}),
    "fuel_leak": (0x75D5, {}, {}),
    "energy_drain": (0xA52F, {}, {}),
    "laser": (0xA183, {}, {}),
    "tunnel": (0xA0CC, {}, {0xA0E9: "end 1"}),
    "controls": (0xA63D, {}, {}),
}


# name -> data-segment fields (address, size) given random values when fuzzing; a value list
# picks from interesting values instead of all
FUZZ = {
    "message": [(0x8058, [0x81FE, 0x8212, 0x802C]), (0x805A, 1), (0x805B, [0, 0xFF]), (0x8056, [0x81FE, 0x802C]),
                (0xB0DE, [0, 0x200, 0x400, 0x600, 0x100]), (0xB126, [0, 0, 1]), (0x81F4, [0, 0, 1, 5]),
                (0x81F5, 1), (0x81F2, 2), (0x8892, [0, 1, 2]), (0x54C3, [0x10, 0x31, 0x32, 0xFE]),
                (0x54C1, [0x10, 0xE0, 0xE1]), (0x54C8, [0xFF, 0x100, 0x3FF])],
    "fuel_leak": [(0x83A5, [0, 0, 1, 2, 9]), (0x83A6, [0, 1, 2, 0x33]), (0x8356, [0, 3, 5, 6, 0x46, 0xFF])],
    "energy_drain": [(0xB139, [0, 1, 2]), (0x54C8, [0, 1, 2, 3, 0x3FF])],
    "laser": [(0xB126, [0, 0, 1]), (0xAE23, [0, 0, 1]), (0x020D + 0x39, [0, 0x80]), (0x8365, 1), (0x8366, 1),
              (0xB0DE, [0, 0x200, 0x400, 0x600]), (0x54C2, [0, 0xEB, 0xEF, 0xF0, 0xFC]), (0xB3D3, [0, 0, 1]),
              (0xB125, [0, 1])],
    "tunnel": [(0xAE23, [0, 1, 2, 30]), (0x83B5, [0, 0, 5])],
    "controls": [(0x020D + 0x48, [0, 0x80, 0x80]), (0x020D + 0x50, [0, 0x80, 0x80]),
                 (0x020D + 0x4B, [0, 0x80, 0x80]), (0x020D + 0x4D, [0, 0x80, 0x80]),
                 (0x020D + 0x34, [0, 0x80, 0x80]), (0x020D + 0x33, [0, 0x80, 0x80]),
                 (0x09D1, [0, 1, 2, 0x16, 0x17, 0xE9, 0xEA, 0xFF, 0x0B, 0xF5]),
                 (0x09D2, [0, 1, 2, 0x16, 0x17, 0xE9, 0xEA, 0xFF, 0x0B, 0xF5]),
                 (0x09D3, [0, 1, 0xFF]), (0x09D4, [0, 1, 0xFF]),
                 (0x09D5, [0, 1, 0x16, 0x17, 0xE9, 0xEA, 0xFF, 0x0C]), (0x09D6, [0, 1, 0x16, 0x17, 0xE9, 0xEA, 0xFF, 0x0C]), (0xB134, [0, 1]), (0xB135, [0, 1]), (0xB136, [0, 0, 1]),
                 (0xB137, [0, 0, 1]), (0xAF56, [4, 8, 0x2C, 0x30]), (0xAF58, [0, 1]), (0xB0DD, [0, 0, 1]),
                 (0x76D8, 2), (0x76DA, 2), (0x76DC, 2), (0xAE23, [0, 0, 0, 3]), (0xB126, [0, 0, 0, 0x3C, 5])],
}


def fuzz(image, rng):
    img = bytearray(image)
    for addr, spec in FUZZ.get(NAME, []):
        if isinstance(spec, list):
            v, n = rng.choice(spec), 2 if max(spec) > 0xFF else 1
        else:
            n = spec
            v = rng.getrandbits(8 * n)
        for k in range(n):
            img[DS * 16 + addr + k] = (v >> (8 * k)) & 0xFF
    return bytes(img)


def s16(v):
    return v - 65536 if v >= 32768 else v


def run_original(image, addr, regs, exits=None):
    e = Elite()
    exits = exits or {}
    left = []
    for a, line in exits.items():
        e.mu.hook_add(UC_HOOK_CODE, lambda mu, ad, sz, u, line=line: (left.append(line), mu.emu_stop()),
                      begin=CS * 16 + a, end=CS * 16 + a)
    e.mu.mem_write(0, image)
    prims = []

    def prim(kind, pts):
        prims.append(f"{kind}:{e.r8(0x10A2)}" + "".join(f",{s16(v)}" for v in pts))

    e.hook(0x172C, lambda e, r: prim(0, [r["cx"], r["dx"], r["ax"], r["bx"], r["si"], r["bp"]]))
    e.hook(0x1A7A, lambda e, r: prim(2, [r["ax"], r["bx"], r["cx"], r["dx"], e.r16(0x10B0),
                                         e.r16(0x10B4), r["si"], r["bp"]]))
    e.hook(0x261B, lambda e, r: prim(4, [r["cx"], r["dx"], r["ax"], r["bx"]]))

    spans = []

    def span(e, r):
        if s16(r["cx"]) >= 0:
            spans.append(f"span {s16(r['bx'])},{s16(r['cx']) or 1},{(s16(r['di']) - 0x168) // 0x28}")
    e.hook(0x16DA, span)
    e.hook(0x1514, span)
    sounds = []
    e.hook(0x4E1A, lambda e, r: sounds.append(f"event {EV_SURFACE}:{r['ax']}"))
    e.hook(0x4C98, lambda e, r: sounds.append(f"event {EV_SOUND}:{r['ax'] & 0xFF}"))
    e.hook(0x487E, lambda e, r: None)  # compass: drawing only
    try:
        e.call(addr, **regs)
    except RuntimeError:
        if not left:
            raise
    if NAME == "tunnel" and not left:
        left.append("end 0")
    return bytes(e.mu.mem_read(DS * 16, 0x10000)), prims + spans + left + sounds
    return bytes(e.mu.mem_read(DS * 16, 0x10000)), prims


def main():
    addr, regs, exits = ROUTINES[NAME]
    files = sorted(glob.glob(PATTERN))
    tmp = tempfile.mkdtemp()
    maskf = os.path.join(tmp, "mask")
    subprocess.run([TOOL, "mask", maskf], check=True)
    mask = open(maskf, "rb").read()
    bad, unmodelled = 0, collections.Counter()
    rng = random.Random(1)
    cases = [(p, None) for p in files] + [(p, k) for p in files for k in range(FUZZ_N)]
    for path, variant in cases:
        image = load(path)
        if variant is not None:
            image = fuzz(image, rng)
        before = image[DS * 16:DS * 16 + 0x10000]
        want, want_prims = run_original(image, addr, regs, exits)
        inf, outf = os.path.join(tmp, "in"), os.path.join(tmp, "out")
        open(inf, "wb").write(before)
        out = subprocess.run([TOOL, NAME, inf, outf], capture_output=True, text=True, check=True).stdout
        got, got_prims = open(outf, "rb").read(), out.splitlines()
        diff = [i for i in range(0x10000) if mask[i] and want[i] != got[i]]
        for i in range(0x10000):
            if not mask[i] and want[i] != before[i] and not any(a <= i <= b for a, b in SCRATCH):
                unmodelled[i] += 1
        if diff or want_prims != got_prims:
            bad += 1
            if bad <= 3:
                print(f"{os.path.basename(path)}{'' if variant is None else ' fuzz ' + str(variant)}: {len(diff)} modelled bytes differ "
                      f"{[hex(i) for i in diff[:12]]}, primitives {len(want_prims)} vs {len(got_prims)}"
                      + ("" if want_prims == got_prims else " (differ)"))
    print(f"{NAME}: {len(cases)} states ({FUZZ_N} fuzzed per state), {bad} differ")
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
