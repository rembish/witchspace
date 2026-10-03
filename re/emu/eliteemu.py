"""Run routines of the original ELITE.EXE headless under Unicorn.

The unpacked image (re/tools/unexepack.py) is loaded at segment 0x1000, the same addresses as
the Ghidra project, and relocated. Routines are called directly with register arguments; the
game keeps all its state in the data segment (0b00 -> 1b00 here), which tests read and write
through the helpers below.
"""
import os
import struct
import sys

from unicorn import UC_ARCH_X86, UC_HOOK_CODE, UC_HOOK_INSN, UC_HOOK_INTR, UC_MODE_16, Uc
from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BP, UC_X86_REG_BX, UC_X86_REG_CS,
                               UC_X86_REG_CX, UC_X86_REG_DI, UC_X86_REG_DS, UC_X86_REG_DX,
                               UC_X86_REG_ES, UC_X86_REG_FLAGS, UC_X86_REG_IP, UC_X86_REG_SI,
                               UC_X86_REG_SP, UC_X86_REG_SS)

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import unexepack  # noqa: E402

EXE = os.path.join(HERE, "..", "..", "original", "ELITE.EXE")

LOAD = 0x1000
CS = LOAD
DS = LOAD + 0x0B00
SS = LOAD + 0x1C0C
STACK_TOP = 0x4000
# Return address pushed for every call; CS:fff0 is data (ds:4ff0), never executed.
SENTINEL = 0xFFF0

_DIVS = None  # div/idiv sites of segment 0000, found once per process

REGS = {"ax": UC_X86_REG_AX, "bx": UC_X86_REG_BX, "cx": UC_X86_REG_CX, "dx": UC_X86_REG_DX,
        "si": UC_X86_REG_SI, "di": UC_X86_REG_DI, "bp": UC_X86_REG_BP, "flags": UC_X86_REG_FLAGS}


class Elite:
    def __init__(self):
        raw = unexepack.unpack(open(EXE, "rb").read(), quiet=True)
        hdr = struct.unpack_from("<H", raw, 8)[0] * 16
        img = bytearray(raw[hdr:])
        nrel, rtab = struct.unpack_from("<H", raw, 6)[0], struct.unpack_from("<H", raw, 0x18)[0]
        for k in range(nrel):
            off, seg = struct.unpack_from("<HH", raw, rtab + 4 * k)
            p = seg * 16 + off
            v = struct.unpack_from("<H", img, p)[0]
            img[p:p + 2] = struct.pack("<H", (v + LOAD) & 0xFFFF)
        self.mu = mu = Uc(UC_ARCH_X86, UC_MODE_16)
        mu.mem_map(0, 0x100000)
        mu.mem_write(LOAD * 16, bytes(img))
        mu.hook_add(UC_HOOK_INTR, self._intr)
        self.on_intr = None  # fn(emu, intno) -> True when it handled the interrupt
        self.pyfuncs = {}
        self._resume = None
        self._hook_divisions(img)

    def devices(self, drivers=False):
        """No hardware: port reads give 0, writes to the speaker's ports (PIT 42h/43h, 61h) are
        noted in self.ports. The music driver's far entries (segment 2270) return at once; with
        drivers, only its Roland ones do (008f, 0116, and 0000 and 0045 on a Roland: ds:b5b7 0),
        the AdLib's run."""
        from unicorn.x86_const import UC_X86_INS_IN, UC_X86_INS_OUT
        self.ports = []
        self.port_in = {}  # what a port reads, if not 0
        self.mu.hook_add(UC_HOOK_INSN, lambda mu, port, size, u: self.port_in.get(port, 0), None, 1, 0,
                         UC_X86_INS_IN)
        self.mu.hook_add(UC_HOOK_INSN, lambda mu, port, size, value, u: port in (0x40, 0x42, 0x43, 0x61, 0x388, 0x389)
                         and self.ports.append((port, value & 0xFF)), None, 1, 0, UC_X86_INS_OUT)
        roland = lambda mu, addr, size, u: self.r8(0xB5B7) or self._retf(mu, addr, size, u)
        stubs = {0x008F: self._retf, 0x0116: self._retf, 0x0000: roland, 0x0045: roland} if drivers else \
            {off: self._retf for off in (0x0000, 0x003B, 0x0045, 0x008F, 0x0116, 0x17C6, 0x1819, 0x185A)}
        for off, stub in stubs.items():
            self.mu.hook_add(UC_HOOK_CODE, stub, begin=(LOAD + 0x2270) * 16 + off, end=(LOAD + 0x2270) * 16 + off)

    def _retf(self, mu, addr, size, _):
        sp = mu.reg_read(UC_X86_REG_SP)
        ip, cs = struct.unpack("<HH", mu.mem_read(SS * 16 + sp, 4))
        mu.reg_write(UC_X86_REG_SP, sp + 4)
        mu.reg_write(UC_X86_REG_CS, cs)
        mu.reg_write(UC_X86_REG_IP, ip)

    def _intr(self, mu, intno, _):
        if self.on_intr and self.on_intr(self, intno):
            return
        ah = mu.reg_read(UC_X86_REG_AX) >> 8
        if intno == 0x21 and ah in (0x25, 0x35):  # the vectors (the music driver's timer interrupts)
            at = (mu.reg_read(UC_X86_REG_AX) & 0xFF) * 4
            if ah == 0x25:
                mu.mem_write(at, struct.pack("<HH", mu.reg_read(UC_X86_REG_DX), mu.reg_read(UC_X86_REG_DS)))
            else:
                off, seg = struct.unpack("<HH", mu.mem_read(at, 4))
                mu.reg_write(UC_X86_REG_BX, off)
                mu.reg_write(UC_X86_REG_ES, seg)
            return
        ip = mu.reg_read(UC_X86_REG_IP)
        raise RuntimeError(f"unhandled int {intno:#x} near {ip:04x}")

    def _hook_divisions(self, img):
        """Divide errors: the game's INT 0 handler (00d6) resumes at the address stored in
        ds:01f8 before the division, registers and stack unchanged. Unicorn's exception
        delivery does not cope with that, so every div/idiv of segment 0000 is checked before
        it runs and skipped to that address when it would fault."""
        global _DIVS
        if _DIVS is None:
            _DIVS = self._find_divisions(img)
        self._divs = _DIVS
        for off in _DIVS:
            self.mu.hook_add(UC_HOOK_CODE, self._div, begin=CS * 16 + off, end=CS * 16 + off)

    @staticmethod
    def _find_divisions(img):
        from capstone.x86 import X86_OP_MEM, X86_OP_REG
        divs = {}
        sys.path.insert(0, os.path.join(HERE, "..", "tools"))
        import explore
        ex = explore.Explorer(img)
        ex.funcs[(0, 0)] = set()
        stderr, sys.stderr = sys.stderr, open(os.devnull, "w")  # explorer warnings
        try:
            for r in [(0, 0)] + explore.EXTRA_ROOTS:
                ex.walk(*r)
            ex.resolve()
        finally:
            sys.stderr.close()
            sys.stderr = stderr
        for (seg, off), i in ex.insns.items():
            if seg == 0 and i.mnemonic in ("div", "idiv"):
                op = i.operands[0]
                if op.type == X86_OP_REG:
                    src = ("reg", i.reg_name(op.reg), op.size)
                elif op.type == X86_OP_MEM:
                    m = op.mem
                    src = ("mem", i.reg_name(m.base) if m.base else None,
                           i.reg_name(m.index) if m.index else None, m.disp,
                           i.reg_name(m.segment) if m.segment else None, op.size)
                divs[off] = (i.mnemonic == "idiv", src)
        return divs

    def _reg(self, name):
        mu = self.mu
        full = {"al": "ax", "ah": "ax", "bl": "bx", "bh": "bx", "cl": "cx", "ch": "cx",
                "dl": "dx", "dh": "dx"}
        v = mu.reg_read(REGS[full.get(name, name)]) if name not in ("ds", "es", "ss", "cs") else \
            mu.reg_read({"ds": UC_X86_REG_DS, "es": UC_X86_REG_ES, "ss": UC_X86_REG_SS,
                         "cs": UC_X86_REG_CS}[name])
        if name.endswith("h") and name in full:
            return v >> 8
        if name.endswith("l") and name in full:
            return v & 0xFF
        return v

    def _div(self, mu, addr, size, _):
        signed, src = self._divs[addr - CS * 16]
        if src[0] == "reg":
            d, width = self._reg(src[1]), src[2]
        else:
            _, base, index, disp, seg, width = src
            ea = (disp + (self._reg(base) if base else 0) + (self._reg(index) if index else 0)) & 0xFFFF
            seg = seg or ("ss" if base in ("bp",) else "ds")
            raw = bytes(mu.mem_read(self._reg(seg) * 16 + ea, width))
            d = int.from_bytes(raw, "little")
        ax, dx = self._reg("ax"), self._reg("dx")
        if width == 1:
            num, bits = ax, 16
        else:
            num, bits = dx << 16 | ax, 32
        if signed:
            num -= (1 << bits) if num >> (bits - 1) else 0
            d -= (1 << (8 * width)) if d >> (8 * width - 1) else 0
        lim = 8 * width
        fault = d == 0
        if not fault:
            q = abs(num) // abs(d) * (1 if (num < 0) == (d < 0) else -1) if signed else num // d
            fault = not (-(1 << (lim - 1)) <= q < (1 << (lim - 1))) if signed else q >= (1 << lim)
        if fault:
            mu.reg_write(UC_X86_REG_IP, self.r16(0x01F8))

    def hook(self, func, fn):
        """Replace the near routine at CS:func by fn(emu, regs) followed by a near `ret`.
        fn may return a dict of registers to set."""
        self.pyfuncs[func] = fn
        self.mu.hook_add(UC_HOOK_CODE, self._py, begin=CS * 16 + func, end=CS * 16 + func)

    def _py(self, mu, addr, size, _):
        fn = self.pyfuncs[addr - CS * 16]
        regs = {k: mu.reg_read(r) for k, r in REGS.items()}
        out = fn(self, regs) or {}
        for k, v in out.items():
            mu.reg_write(REGS[k], v & 0xFFFF)
        sp = mu.reg_read(UC_X86_REG_SP)
        ret = struct.unpack("<H", mu.mem_read(SS * 16 + sp, 2))[0]
        mu.reg_write(UC_X86_REG_SP, sp + 2)
        mu.reg_write(UC_X86_REG_IP, ret)

    # ---- stack segment (ship models and their table live there) ----
    def ss_rb(self, off, n=1):
        return bytes(self.mu.mem_read(SS * 16 + off, n))

    def ss_r16(self, off):
        return struct.unpack("<H", self.ss_rb(off, 2))[0]

    # ---- data segment helpers ----
    def rb(self, off, n=1):
        return bytes(self.mu.mem_read(DS * 16 + off, n))

    def wb(self, off, data):
        self.mu.mem_write(DS * 16 + off, bytes(data))

    def r8(self, off):
        return self.rb(off)[0]

    def r16(self, off):
        return struct.unpack("<H", self.rb(off, 2))[0]

    def w8(self, off, v):
        self.wb(off, [v & 0xFF])

    def w16(self, off, v):
        self.wb(off, struct.pack("<H", v & 0xFFFF))

    def cstr(self, off, n=64):
        s = self.rb(off, n)
        return s[:s.index(0)] if 0 in s else s

    # ---- calls ----
    def call(self, func, max_insns=10_000_000, until=None, **regs):
        """Near-call CS:func with the given registers; return the registers on return, or when
        execution reaches CS:until (for routines that go on to draw)."""
        mu = self.mu
        for seg, v in ((UC_X86_REG_DS, DS), (UC_X86_REG_ES, DS), (UC_X86_REG_SS, SS),
                       (UC_X86_REG_CS, CS)):
            mu.reg_write(seg, v)
        for k, v in regs.items():
            mu.reg_write(REGS[k], v & 0xFFFF)
        sp = STACK_TOP - 2
        mu.mem_write(SS * 16 + sp, struct.pack("<H", SENTINEL))
        mu.reg_write(UC_X86_REG_SP, sp)
        stop = SENTINEL if until is None else until
        start = func
        while True:
            self._resume = None
            mu.emu_start(CS * 16 + start, CS * 16 + stop, count=max_insns)
            if self._resume is None:
                break
            start = self._resume
        if until is not None:
            if mu.reg_read(UC_X86_REG_IP) != until:
                raise RuntimeError(f"call {func:04x} did not reach {until:04x}")
            return {k: mu.reg_read(r) for k, r in REGS.items()}
        if mu.reg_read(UC_X86_REG_IP) != SENTINEL:
            raise RuntimeError(f"call {func:04x} did not return (ip {mu.reg_read(UC_X86_REG_IP):04x})")
        if mu.reg_read(UC_X86_REG_SP) != STACK_TOP:
            raise RuntimeError(f"call {func:04x} left the stack unbalanced")
        return {k: mu.reg_read(r) for k, r in REGS.items()}
