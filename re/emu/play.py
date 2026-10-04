"""Play the original ELITE.EXE in the test harness: a Tk window on machine.py.

usage: play.py [--scale N] [--keys PMA] [--record DIR] [--debug-keys]

Runs at the original's timer rate (54.6 ticks a second). Keys go to the game's own keyboard
handler as PC scancodes; --keys types the start-up answers (sound P, graphics M, any word for
the protection) so the game starts at the title. MCGA only, no sound. This is a viewer for
checking the reverse engineering, not the port: nothing is compared, it shows what the
original does where a differential test disagrees. Needs Pillow (not a dependency of the
tools).
"""

import argparse
import os
import sys
import time
import tkinter as tk
from typing import Final

from PIL import Image  # type: ignore[import-not-found]  # Pillow: a system package, not a dependency

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from machine import Machine  # noqa: E402

TICK_HZ: Final = 1193182 / 0x5555

# Tk keysym -> PC scancode (set 1); extended keys are sent with an E0 prefix
SCAN: Final = {
    "Escape": 0x01, "minus": 0x0C, "equal": 0x0D, "BackSpace": 0x0E, "Tab": 0x0F,
    "Return": 0x1C, "Control_L": 0x1D, "Shift_L": 0x2A, "Shift_R": 0x36, "Alt_L": 0x38,
    "space": 0x39, "Caps_Lock": 0x3A, "comma": 0x33, "period": 0x34, "slash": 0x35,
    "semicolon": 0x27, "apostrophe": 0x28, "bracketleft": 0x1A, "bracketright": 0x1B,
    "backslash": 0x2B, "grave": 0x29, "KP_Add": 0x4E, "KP_Subtract": 0x4A,
    "KP_Multiply": 0x37,
}  # fmt: skip
for i, ch in enumerate("1234567890"):
    SCAN[ch] = 0x02 + i
for row, start in (("qwertyuiop", 0x10), ("asdfghjkl", 0x1E), ("zxcvbnm", 0x2C)):
    for i, ch in enumerate(row):
        SCAN[ch] = start + i
for i in range(10):
    SCAN[f"F{i + 1}"] = 0x3B + i
SCAN["F11"], SCAN["F12"] = 0x57, 0x58
EXT: Final = {
    "Up": 0x48, "Down": 0x50, "Left": 0x4B, "Right": 0x4D, "Insert": 0x52, "Delete": 0x53,
    "Home": 0x47, "End": 0x4F, "Prior": 0x49, "Next": 0x51, "Control_R": 0x1D,
    "KP_Enter": 0x1C,
}  # fmt: skip


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--scale", type=int, default=3)
    ap.add_argument("--keys", default="PMA", help="typed at start: sound, graphics, protection word")
    ap.add_argument("--record", help="save the screen to DIR/frame-NNN.png once a second")
    ap.add_argument("--debug-keys", action="store_true", help="print key events and deliveries")
    a = ap.parse_args()

    m = Machine()
    m.boot()
    first = [SCAN[c.lower()] for c in a.keys] + [0x1C]
    m.press(*first)
    events: list[int] = []  # scancodes from the window, delivered one per pause
    down: set[str] = set()
    last = {"key": "-", "sent": "-"}

    root = tk.Tk()
    root.title("Elite Plus (original, in the test harness)")
    label = tk.Label(root, bg="black")
    label.pack()
    status = tk.StringVar()
    tk.Label(root, textvariable=status, anchor="w").pack(fill="x")

    def key(ev: "tk.Event[tk.Misc]", release: bool) -> None:
        last["key"] = ev.keysym + (" up" if release else "")
        if a.debug_keys:
            print("key", ev.keysym, "release" if release else "press", flush=True)
        sym = ev.keysym if ev.keysym in SCAN or ev.keysym in EXT else ev.keysym.lower()
        if sym in EXT:
            codes = [0xE0, EXT[sym]]
        elif sym in SCAN:
            codes = [SCAN[sym]]
        else:
            return
        if release:
            down.discard(sym)
            events.extend(c | 0x80 if c != 0xE0 else c for c in codes)
        elif sym not in down:  # ignore the window system's auto-repeat
            down.add(sym)
            events.extend(codes)

    def key_press(ev: "tk.Event[tk.Misc]") -> None:
        key(ev, False)

    def key_release(ev: "tk.Event[tk.Misc]") -> None:
        key(ev, True)

    def take_focus(ev: "tk.Event[tk.Label]") -> None:
        root.focus_force()

    root.bind_all("<KeyPress>", key_press)
    root.bind_all("<KeyRelease>", key_release)
    # WSLg and some window managers do not give a new window keyboard focus: take it, and
    # again on any click.
    root.after(300, root.focus_force)
    label.bind("<Button-1>", take_focus)

    t0 = time.time()
    base_ticks: int | None = None  # the machine's ticks when the clock started
    target = 0  # run up to this tick count
    saved = 0  # frames recorded
    photo: tk.PhotoImage | None = None  # the picture shown (Tk drops it unless referenced)

    def deliver(mm: Machine) -> bool:
        # one keyboard interrupt per pause (a timer tick is usually pending as well)
        if events and not mm.keys and mm.down is None and not any(e != 8 for e in mm.pending):
            code = events.pop(0)
            if a.debug_keys:
                print(f"deliver {code:#04x} at tick {mm.ticks}", flush=True)
            mm.scancode_event(code)
            last["sent"] = f"{code:02x}"
        return mm.ticks >= target

    def frame() -> None:
        nonlocal base_ticks, target, saved, photo
        if m.exit_code is not None:
            root.destroy()
            return
        if base_ticks is None and m.ticks > 0:
            base_ticks = m.ticks
        now = time.time() - t0
        target = max(m.ticks + 1, int(now * TICK_HZ) + (base_ticks or 0))
        target = min(target, m.ticks + 30)
        try:
            m.run(stop=deliver, idle_ticks=True)
        except Exception as ex:  # noqa: BLE001 - keep the window to show where it stopped
            status.set(f"stopped: {ex}")
            return
        im = Image.frombytes("P", (320, 200), bytes(m.mu.mem_read(0xA0000, 320 * 200)))
        im.putpalette([v * 255 // 63 for v in m.dac])
        im = im.convert("RGB").resize((320 * a.scale, 200 * a.scale), Image.NEAREST)
        ppm = b"P6 %d %d 255\n" % im.size + im.tobytes()
        photo = tk.PhotoImage(data=ppm, format="PPM")
        label.configure(image=photo)
        status.set(
            f"ticks {m.ticks}   last key {last['key']}   sent scancode {last['sent']}"
            "   (click the picture if keys do not arrive)"
        )
        if a.record and int(time.time() - t0) >= saved:
            os.makedirs(a.record, exist_ok=True)
            im.save(os.path.join(a.record, f"frame-{saved:03d}.png"))
            saved += 1
        root.after(10, frame)

    root.after(10, frame)
    root.mainloop()


if __name__ == "__main__":
    main()
