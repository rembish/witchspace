"""Play the original ELITE.EXE in the test harness: a Tk window on machine.py.

usage: play.py [--scale N] [--keys PMA]

Runs at the original's timer rate (54.6 ticks a second). Keys go to the game's own keyboard
handler as PC scancodes; --keys types the start-up answers (sound P, graphics M, any word for
the protection) so the game starts at the title. MCGA only, no sound. This is a viewer for
checking the reverse engineering, not the port.
"""
import argparse
import os
import sys
import time
import tkinter as tk

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from machine import Machine  # noqa: E402

TICK_HZ = 1193182 / 0x5555

# Tk keysym -> PC scancode (set 1); extended keys are sent with an E0 prefix
SCAN = {"Escape": 0x01, "minus": 0x0C, "equal": 0x0D, "BackSpace": 0x0E, "Tab": 0x0F,
        "Return": 0x1C, "Control_L": 0x1D, "Shift_L": 0x2A, "Shift_R": 0x36, "Alt_L": 0x38,
        "space": 0x39, "Caps_Lock": 0x3A, "comma": 0x33, "period": 0x34, "slash": 0x35,
        "semicolon": 0x27, "apostrophe": 0x28, "bracketleft": 0x1A, "bracketright": 0x1B,
        "backslash": 0x2B, "grave": 0x29, "KP_Add": 0x4E, "KP_Subtract": 0x4A,
        "KP_Multiply": 0x37}
for i, ch in enumerate("1234567890"):
    SCAN[ch] = 0x02 + i
for row, start in (("qwertyuiop", 0x10), ("asdfghjkl", 0x1E), ("zxcvbnm", 0x2C)):
    for i, ch in enumerate(row):
        SCAN[ch] = start + i
for i in range(10):
    SCAN[f"F{i + 1}"] = 0x3B + i
SCAN["F11"], SCAN["F12"] = 0x57, 0x58
EXT = {"Up": 0x48, "Down": 0x50, "Left": 0x4B, "Right": 0x4D, "Insert": 0x52, "Delete": 0x53,
       "Home": 0x47, "End": 0x4F, "Prior": 0x49, "Next": 0x51, "Control_R": 0x1D,
       "KP_Enter": 0x1C}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scale", type=int, default=3)
    ap.add_argument("--keys", default="PMA", help="typed at start: sound, graphics, protection word")
    a = ap.parse_args()

    m = Machine()
    m.boot()
    first = [SCAN[c.lower()] for c in a.keys] + [0x1C]
    m.press(*first)
    events = []      # scancodes from the window, delivered one per pause
    down = set()

    root = tk.Tk()
    root.title("Elite Plus (original, in the test harness)")
    label = tk.Label(root, bg="black")
    label.pack()
    status = tk.StringVar()
    tk.Label(root, textvariable=status, anchor="w").pack(fill="x")

    def key(ev, release):
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
        elif sym not in down:   # ignore the window system's auto-repeat
            down.add(sym)
            events.extend(codes)

    root.bind("<KeyPress>", lambda ev: key(ev, False))
    root.bind("<KeyRelease>", lambda ev: key(ev, True))

    t0 = time.time()
    base_ticks = [None]

    def deliver(mm):
        if events and not mm.pending and not mm.keys and mm.down is None:
            mm.scancode_event(events.pop(0))
        return mm.ticks >= target[0]

    target = [0]

    def frame():
        if m.exit_code is not None:
            root.destroy()
            return
        if base_ticks[0] is None and m.ticks > 0:
            base_ticks[0] = m.ticks
        now = time.time() - t0
        target[0] = max(m.ticks + 1, int(now * TICK_HZ) + (base_ticks[0] or 0))
        target[0] = min(target[0], m.ticks + 30)
        try:
            m.run(stop=deliver, idle_ticks=True)
        except Exception as ex:  # keep the window to show where it stopped
            status.set(f"stopped: {ex}")
            return
        im = Image.frombytes("P", (320, 200), bytes(m.mu.mem_read(0xA0000, 320 * 200)))
        im.putpalette([v * 255 // 63 for v in m.dac])
        im = im.convert("RGB").resize((320 * a.scale, 200 * a.scale), Image.NEAREST)
        ppm = b"P6 %d %d 255\n" % im.size + im.tobytes()
        photo = tk.PhotoImage(data=ppm, format="PPM")
        label.configure(image=photo)
        label.image = photo
        status.set(f"ticks {m.ticks}")
        root.after(10, frame)

    root.after(10, frame)
    root.mainloop()


if __name__ == "__main__":
    main()
