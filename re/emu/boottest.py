"""Start-up test: the original booted headless (machine.py) to the title (9e80) vs ep_boot.

usage: boottest.py [path/to/ep_bootdump]

For a few times of day, sound devices and video modes, the original runs from its entry
point with the answers typed (sound, graphics, the protection's word) up to 9e80; its data
segment is compared, byte by byte where the core models it, with what ep_boot (and the
protection's question, asked with the same word) gives.
"""
import datetime
import os
import subprocess
import sys
import tempfile

from machine import CS, DS, Machine
from unicorn import UC_HOOK_CODE

HERE = os.path.dirname(os.path.abspath(__file__))
DUMP = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "build", "ep_bootdump")
SUBSYS = os.path.join(os.path.dirname(DUMP), "ep_subsys")
os.environ["EP_ORIGINAL"] = os.path.join(HERE, "..", "..", "original")  # where ep_bootdump reads the music
SCAN = {c: s for c, s in zip("QWERTYUIOP", range(0x10, 0x1A))}
SCAN.update({c: s for c, s in zip("ASDFGHJKL", range(0x1E, 0x27))})
SCAN.update({c: s for c, s in zip("ZXCVBNM", range(0x2C, 0x33))})
CASES = [  # time, sound key, graphics key, word
    (datetime.datetime(1991, 1, 1, 12, 0, 0), "P", "M", "A"),
    (datetime.datetime(1991, 3, 7, 9, 41, 17, 230000), "P", "E", "ELITE"),
    (datetime.datetime(1991, 5, 2, 23, 59, 59, 990000), "P", "V", "WORD"),
    (datetime.datetime(1990, 8, 1, 6, 6, 6, 60000), "P", "M", "IMPRINT"),
    (datetime.datetime(1991, 2, 2, 2, 2, 2, 20000), "A", "M", "LAVE"),
    (datetime.datetime(1991, 4, 4, 4, 4, 4, 40000), "R", "V", "ZAONCE"),
]


def main():
    tmp = tempfile.mkdtemp()
    subprocess.run([SUBSYS, "mask", os.path.join(tmp, "mask")], check=True)
    mask = open(os.path.join(tmp, "mask"), "rb").read()
    bad = 0
    for when, sound, video, word in CASES:
        m = Machine(clock=when)
        m.boot()
        m.press(SCAN[sound], SCAN[video], *[SCAN[c] for c in word], 0x1C)
        at = []
        m.mu.hook_add(UC_HOOK_CODE, lambda mu, a, s, u: (at.append(1), mu.emu_stop()),
                      begin=CS * 16 + 0x9E80, end=CS * 16 + 0x9E80)
        m.run(stop=lambda m: bool(at))
        want = bytes(m.mu.mem_read(DS * 16, 0x10000))
        out = os.path.join(tmp, "out")
        subprocess.run([DUMP, str(m.r8(0x10BC)), str(m.r8(0x4801)), str(when.minute), str(when.second),
                        str(when.microsecond // 10000), word, out], check=True)
        got = open(out, "rb").read()
        diff = [i for i in range(0x10000) if mask[i] and want[i] != got[i]
                and not 0x020D <= i < 0x028D and i not in (0x0D2D, 0x0D2E)]  # the keys held (Enter, still down at 9e80)
        print(f"{when} {sound}{video} {word}: {len(diff)} modelled bytes differ "
              + " ".join(f"{i:x}:{want[i]:02x}/{got[i]:02x}" for i in diff[:16]))
        bad += bool(diff)
    print(f"{len(CASES)} start-ups, {bad} differ")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
