"""Run the whole original ELITE.EXE headless: a minimal DOS and PC around Unicorn.

The game is started from its entry point like DOS would (PSP below the image, DS = ES = PSP)
and talks to Python stand-ins for DOS (INT 21h: files from original/ and an in-memory
directory for saves, memory blocks, vectors, time), the video BIOS (INT 10h, only the mode
calls) and the mouse driver (INT 33h: none present). Ports: 3dah toggles the retrace bits on
every read, 60h returns the injected scancode, everything else reads 0.

Time is deterministic: timer interrupts (INT 8, the game's handler at 4a99) are only injected
where the game waits for them (the frame wait at 3027), and keys only when the game polls for
one (0276) or between frames, from a script.
"""
import datetime
import os
import struct

from unicorn import UC_HOOK_CODE, UC_HOOK_INSN, UC_HOOK_INTR
from unicorn.x86_const import (UC_X86_INS_IN, UC_X86_INS_OUT, UC_X86_REG_AX, UC_X86_REG_BX,
                               UC_X86_REG_CS, UC_X86_REG_CX, UC_X86_REG_DI, UC_X86_REG_DS,
                               UC_X86_REG_DX, UC_X86_REG_ES, UC_X86_REG_FLAGS, UC_X86_REG_IP,
                               UC_X86_REG_SI, UC_X86_REG_SP, UC_X86_REG_SS)

import eliteemu
from eliteemu import CS, DS, LOAD, Elite

HERE = os.path.dirname(os.path.abspath(__file__))
ORIGINAL = os.path.join(HERE, "..", "..", "original")

PSP = LOAD - 0x10
BIOS = 0xF000           # segment of the default interrupt handlers (an IRET each)
MEM_TOP = 0xA000
CF, IF = 0x0001, 0x0200

KEY_POLL = 0x0276


def _deadline(hi, lo, equal):
    def waiting(m):
        target = m.reg(hi) << 16 | m.reg(lo)
        now = m.r16(0x45E2) << 16 | m.r16(0x45E0)
        return target > now or (equal and target == now)
    return waiting


# Loops that wait for timer ticks: address -> condition that still holds while waiting. The
# deadline loops compare the tick count ds:45e0 with a 32-bit deadline in registers.
WAITS = {
    0x3027: _deadline(UC_X86_REG_DX, UC_X86_REG_AX, False),  # frame wait (301a)
    0x3B18: _deadline(UC_X86_REG_DX, UC_X86_REG_CX, True),   # title picture: 1000 ticks or a key
    0x4E88: _deadline(UC_X86_REG_DX, UC_X86_REG_AX, False),  # delay: 570 ticks (120 with a card)
    0xAF9A: lambda m: m.r16(0x45E4) != 0,                    # countdown ds:45e4 or a key
}


class Exit(Exception):
    pass


class Machine(Elite):
    def __init__(self, clock=datetime.datetime(1991, 1, 1, 12, 0, 0), files=None):
        super().__init__()
        mu = self.mu
        self.clock = clock
        self.stdout = ""
        self.files = dict(files or {})   # extra/writable files by upper-case name
        self.handles = {}
        self.next_seg = LOAD + 0x2571     # first paragraph after the image
        self.video_mode = 3
        self.scancode = 0
        self.retrace = 0
        self.pending = []                 # interrupts to inject before resuming
        self.ticks = 0
        self.keys = []                    # scancodes, delivered at polls
        self.down = None                  # pressed key whose release is still to come
        self.exit_code = None
        self.dta = (0, 0)
        self.dac = bytearray(768)
        self.dac_index = 0
        # IVT: every vector at an IRET in the BIOS segment
        mu.mem_write(BIOS * 16, b"\xcf" * 256)
        for n in range(256):
            mu.mem_write(4 * n, struct.pack("<HH", n, BIOS))
        # PSP
        psp = bytearray(256)
        psp[0:2] = b"\xcd\x20"
        psp[2:4] = struct.pack("<H", MEM_TOP)
        psp[0x80] = 0
        psp[0x81] = 0x0D
        mu.mem_write(PSP * 16, bytes(psp))
        mu.hook_add(UC_HOOK_INSN, self._in, None, 1, 0, UC_X86_INS_IN)
        mu.hook_add(UC_HOOK_INSN, self._out, None, 1, 0, UC_X86_INS_OUT)
        for a in WAITS:
            mu.hook_add(UC_HOOK_CODE, self._wait, begin=CS * 16 + a, end=CS * 16 + a)
        mu.hook_add(UC_HOOK_CODE, self._key_poll, begin=CS * 16 + KEY_POLL, end=CS * 16 + KEY_POLL)
        self.stop_at = {}                 # address -> callback(machine) returning True to stop

    # ---- registers ----
    def reg(self, r):
        return self.mu.reg_read(r)

    def setreg(self, r, v):
        self.mu.reg_write(r, v & 0xFFFF)

    def carry(self, on):
        f = self.reg(UC_X86_REG_FLAGS)
        self.setreg(UC_X86_REG_FLAGS, (f | CF) if on else (f & ~CF))

    def lin(self, seg, off):
        return (seg << 4) + off

    def read_str(self, seg, off, end=0):
        out = bytearray()
        while True:
            b = self.mu.mem_read(self.lin(seg, off + len(out)), 1)[0]
            if b == end:
                return bytes(out)
            out.append(b)

    # ---- ports ----
    def _in(self, mu, port, size, _):
        if port == 0x3DA:
            self.retrace ^= 0x09
            return self.retrace
        if port == 0x60:
            return self.scancode
        return 0

    def _out(self, mu, port, size, value, _):
        if port == 0x3C8:
            self.dac_index = (value & 0xFF) * 3
        elif port == 0x3C9:
            self.dac[self.dac_index % 768] = value & 0x3F
            self.dac_index += 1

    def screenshot(self, path):
        """Save the mode 13h screen (a000:0000) with the current DAC as a PNG."""
        from PIL import Image
        pix = bytes(self.mu.mem_read(0xA0000, 320 * 200))
        im = Image.frombytes("P", (320, 200), pix)
        im.putpalette([v * 255 // 63 for v in self.dac])
        im.convert("RGB").save(path)

    # ---- interrupts ----
    def _intr(self, mu, intno, _):
        ip = self.reg(UC_X86_REG_IP)
        if intno == 0x21:
            return self._dos()
        if intno == 0x10:
            ah = self.reg(UC_X86_REG_AX) >> 8
            if ah == 0x00:
                self.video_mode = self.reg(UC_X86_REG_AX) & 0x7F
            elif ah == 0x0F:
                self.setreg(UC_X86_REG_AX, 0x5000 | self.video_mode)
                self.setreg(UC_X86_REG_BX, self.reg(UC_X86_REG_BX) & 0xFF)
            return
        if intno == 0x33:
            if self.reg(UC_X86_REG_AX) == 0:
                self.setreg(UC_X86_REG_AX, 0)
            return
        raise RuntimeError(f"unhandled int {intno:#x} near {self.reg(UC_X86_REG_CS):04x}:{ip:04x}")

    def _dos(self):
        ax = self.reg(UC_X86_REG_AX)
        ah, al = ax >> 8, ax & 0xFF
        ds, dx = self.reg(UC_X86_REG_DS), self.reg(UC_X86_REG_DX)
        if ah == 0x09:
            self.stdout += self.read_str(ds, dx, ord("$")).decode("latin1")
        elif ah == 0x1A:
            self.dta = (ds, dx)
        elif ah == 0x25:
            self.mu.mem_write(4 * al, struct.pack("<HH", dx, ds))
        elif ah == 0x35:
            off, seg = struct.unpack("<HH", self.mu.mem_read(4 * al, 4))
            self.setreg(UC_X86_REG_BX, off)
            self.setreg(UC_X86_REG_ES, seg)
        elif ah == 0x2C:
            c = self.clock
            self.setreg(UC_X86_REG_CX, c.hour << 8 | c.minute)
            self.setreg(UC_X86_REG_DX, c.second << 8 | c.microsecond // 10000)
        elif ah == 0x30:
            self.setreg(UC_X86_REG_AX, 0x0005)
        elif ah in (0x3C, 0x3D):
            name = self.read_str(ds, dx).decode("latin1").upper()
            if ah == 0x3C:
                self.files[name] = bytearray()
            data = self.files.get(name)
            if data is None:
                path = os.path.join(ORIGINAL, name)
                if not os.path.exists(path):
                    self.setreg(UC_X86_REG_AX, 2)
                    return self.carry(True)
                data = bytearray(open(path, "rb").read())
                self.files[name] = data
            h = 5 + len(self.handles)
            while h in self.handles:
                h += 1
            self.handles[h] = [name, 0]
            self.setreg(UC_X86_REG_AX, h)
            return self.carry(False)
        elif ah == 0x3E:
            self.handles.pop(self.reg(UC_X86_REG_BX), None)
            return self.carry(False)
        elif ah in (0x3F, 0x40):
            h = self.handles.get(self.reg(UC_X86_REG_BX))
            if h is None:
                self.setreg(UC_X86_REG_AX, 6)
                return self.carry(True)
            data, n = self.files[h[0]], self.reg(UC_X86_REG_CX)
            if ah == 0x3F:
                chunk = bytes(data[h[1]:h[1] + n])
                self.mu.mem_write(self.lin(ds, dx), chunk)
            else:
                chunk = bytes(self.mu.mem_read(self.lin(ds, dx), n))
                data[h[1]:h[1] + n] = chunk
            h[1] += len(chunk)
            self.setreg(UC_X86_REG_AX, len(chunk))
            return self.carry(False)
        elif ah == 0x42:
            h = self.handles[self.reg(UC_X86_REG_BX)]
            off = self.reg(UC_X86_REG_CX) << 16 | dx
            base = (0, h[1], len(self.files[h[0]]))[al]
            h[1] = (base + (off - (1 << 32) if off >> 31 else off))
            self.setreg(UC_X86_REG_AX, h[1] & 0xFFFF)
            self.setreg(UC_X86_REG_DX, h[1] >> 16)
            return self.carry(False)
        elif ah == 0x48:
            n = self.reg(UC_X86_REG_BX)
            if self.next_seg + n > MEM_TOP:
                self.setreg(UC_X86_REG_AX, 8)
                self.setreg(UC_X86_REG_BX, max(0, MEM_TOP - self.next_seg))
                return self.carry(True)
            self.setreg(UC_X86_REG_AX, self.next_seg)
            self.next_seg += n
            return self.carry(False)
        elif ah == 0x4A:
            return self.carry(False)
        elif ah == 0x4C:
            self.exit_code = al
            self.mu.emu_stop()
        elif ah in (0x4E, 0x4F):
            self.setreg(UC_X86_REG_AX, 0x12)
            return self.carry(True)
        else:
            raise RuntimeError(f"unhandled DOS call {ax:#06x}")

    def inject(self, n):
        """Push FLAGS, CS, IP and enter the handler of INT n (emulation must be stopped)."""
        mu = self.mu
        off, seg = struct.unpack("<HH", mu.mem_read(4 * n, 4))
        ss, sp = self.reg(UC_X86_REG_SS), self.reg(UC_X86_REG_SP)
        for v in (self.reg(UC_X86_REG_FLAGS), self.reg(UC_X86_REG_CS), self.reg(UC_X86_REG_IP)):
            sp = (sp - 2) & 0xFFFF
            mu.mem_write(self.lin(ss, sp), struct.pack("<H", v))
        self.setreg(UC_X86_REG_SP, sp)
        self.setreg(UC_X86_REG_FLAGS, self.reg(UC_X86_REG_FLAGS) & ~IF)
        self.setreg(UC_X86_REG_CS, seg)
        self.setreg(UC_X86_REG_IP, off)

    # ---- waits ----
    def _wait(self, mu, addr, size, _):
        if WAITS[addr - CS * 16](self):
            self.pending.append(8)
            self.ticks += 1
            mu.emu_stop()

    def _key_poll(self, mu, addr, size, _):
        # One keyboard interrupt per poll with no key waiting: the release of the last key,
        # else the next press. Delivering them only here keeps keys from arriving while the
        # game is busy (it flushes the key before each prompt).
        if self.r8(0x0D2F) != 0xFF:
            return
        if self.down is not None:
            self.pending.append(("kbd", self.down | 0x80))
            self.down = None
            mu.emu_stop()
        elif self.keys:
            self.down = self.keys.pop(0)
            self.pending.append(("kbd", self.down))
            mu.emu_stop()

    def press(self, *scancodes):
        """Queue key presses (each a press and a release through the game's keyboard
        handler, INT 9), delivered when the game next polls with no key waiting."""
        self.keys += scancodes

    # ---- running ----
    def boot(self):
        hdr_ss, hdr_sp = 0x2571, 0x0100
        self.setreg(UC_X86_REG_CS, CS)
        self.setreg(UC_X86_REG_IP, 0)
        self.setreg(UC_X86_REG_DS, PSP)
        self.setreg(UC_X86_REG_ES, PSP)
        self.setreg(UC_X86_REG_SS, LOAD + hdr_ss)
        self.setreg(UC_X86_REG_SP, hdr_sp)
        self.setreg(UC_X86_REG_FLAGS, IF | 0x0002)

    def run(self, max_insns=50_000_000, stop=None):
        """Run until the program exits or stop(machine) returns True. stop is checked whenever
        emulation pauses: at frame waits that need a tick, at key polls with keys queued and
        every million instructions."""
        mu = self.mu
        done = 0
        while self.exit_code is None:
            if stop and stop(self):
                return
            if self.pending:
                ev = self.pending.pop(0)
                if ev == 8:
                    self.inject(8)
                else:
                    self.scancode = ev[1]
                    self.inject(9)
            start = self.lin(self.reg(UC_X86_REG_CS), self.reg(UC_X86_REG_IP))
            mu.emu_start(start, 0xFFFFF, count=1_000_000)
            done += 1
            if done > max_insns // 1_000_000 + 100_000:
                raise RuntimeError("no end")
