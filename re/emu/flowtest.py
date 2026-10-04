"""Flow test: the original booted headless (machine.py, its timer running) from the first
title frame through starting a game and launching, vs the core driven the same way.

usage: flowtest.py [title frames before space] [path/to/ep_flowdump]

The original runs from its entry point with the start-up answers typed (P, M, a word) up to
the title's first frame (9f21), where its data segment is taken. Space is then put in its key
byte at the start of title frame N, F1 at the status screen's first pass (8dac), and it runs
on through the launch tunnel to the first flight frame (a040). The core starts from the
same snapshot and gets the same keys at the same passes; the timer runs where the original
waits for it (machine.py ticks only there), through the core's wait callback. The data
segments are compared where the core models them (not the keys held).
"""

import datetime
import os
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from typing import Any, Final

from unicorn import UC_HOOK_CODE

from eliteemu import Uc
from machine import CS, DS, Machine

HERE: Final = os.path.dirname(os.path.abspath(__file__))
FRAMES: Final = int(sys.argv[1]) if len(sys.argv) > 1 else 5
DUMP: Final = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "..", "..", "build", "ep_flowdump")
SUBSYS: Final = os.path.join(os.path.dirname(DUMP), "ep_subsys")
WHEN: Final = datetime.datetime(1991, 1, 1, 12, 0, 0)


@dataclass
class Flow:
    """Where the original is: passes counted at the hooks, and whether to stop."""

    title: int = 0  # title frames (9f21)
    status: int = 0  # status screen passes (8dac)
    stop: bool = False
    launched: bool = False


def main() -> None:
    tmp = tempfile.mkdtemp()
    subprocess.run([SUBSYS, "mask", os.path.join(tmp, "mask")], check=True)
    with open(os.path.join(tmp, "mask"), "rb") as f:
        mask = f.read()
    m = Machine(clock=WHEN)
    m.boot()
    m.press(0x19, 0x32, 0x1E, 0x1C)  # P, M, A, Enter
    state = Flow()
    snap: dict[str, bytes] = {}

    def title(mu: Uc, a: int, s: int, u: Any) -> None:
        state.title += 1
        if state.title == 1:
            snap["start"] = bytes(mu.mem_read(DS * 16, 0x10000))
        if state.title == FRAMES:
            m.w8(0x0D2F, 0x20)

    def status(mu: Uc, a: int, s: int, u: Any) -> None:
        state.status += 1
        if state.status == 1:
            m.w8(0x0D2F, 0x97)  # F1
            state.launched = True

    def flight(mu: Uc, a: int, s: int, u: Any) -> None:
        if state.launched:
            state.stop = True
            mu.emu_stop()

    m.mu.hook_add(UC_HOOK_CODE, title, begin=CS * 16 + 0x9F21, end=CS * 16 + 0x9F21)
    m.mu.hook_add(UC_HOOK_CODE, status, begin=CS * 16 + 0x8DAC, end=CS * 16 + 0x8DAC)
    m.mu.hook_add(UC_HOOK_CODE, flight, begin=CS * 16 + 0xA040, end=CS * 16 + 0xA040)
    m.run(stop=lambda m: state.stop, max_insns=2_000_000_000)
    want = bytes(m.mu.mem_read(DS * 16, 0x10000))
    inp, out = os.path.join(tmp, "in"), os.path.join(tmp, "out")
    with open(inp, "wb") as fw:
        fw.write(snap["start"])
    clock = [str(WHEN.hour), str(WHEN.minute), str(WHEN.second), "0"]
    r = subprocess.run([DUMP, str(FRAMES), *clock, inp, out], capture_output=True, text=True)
    print(r.stdout, end="")
    if r.returncode:
        print(r.stderr)
        sys.exit(1)
    with open(out, "rb") as f:
        got = f.read()
    skip = [(0x020D, 0x028C)]  # keys held
    diff = [
        i for i in range(0x10000) if mask[i] and want[i] != got[i] and not any(a <= i <= b for a, b in skip)
    ]
    print(
        f"title frames {state.title}, status passes {state.status}; ticks: original "
        f"{m.r16(0x45E0)}, core {got[0x45E0] | got[0x45E1] << 8}"
    )
    print(
        f"{len(diff)} modelled bytes differ "
        + " ".join(f"{i:x}:{want[i]:02x}/{got[i]:02x}" for i in diff[:40])
    )
    sys.exit(1 if diff else 0)


if __name__ == "__main__":
    main()
