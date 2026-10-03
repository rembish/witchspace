"""Run routines of the original ELITE.EXE headless under Unicorn.

The unpacked image (re/tools/unexepack.py) is loaded at segment 0x1000, the same addresses as
the Ghidra project, and relocated. Routines are called directly with register arguments; the
game keeps all its state in the data segment (0b00 -> 1b00 here), which tests read and write
through the helpers below.
"""
import os
import struct
import sys

from unicorn import UC_ARCH_X86, UC_HOOK_INTR, UC_MODE_16, Uc
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

    def _intr(self, mu, intno, _):
        ip = mu.reg_read(UC_X86_REG_IP)
        raise RuntimeError(f"unhandled int {intno:#x} near {ip:04x}")

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
    def call(self, func, max_insns=10_000_000, **regs):
        """Near-call CS:func with the given registers; return the registers on return."""
        mu = self.mu
        for seg, v in ((UC_X86_REG_DS, DS), (UC_X86_REG_ES, DS), (UC_X86_REG_SS, SS),
                       (UC_X86_REG_CS, CS)):
            mu.reg_write(seg, v)
        for k, v in regs.items():
            mu.reg_write(REGS[k], v & 0xFFFF)
        sp = STACK_TOP - 2
        mu.mem_write(SS * 16 + sp, struct.pack("<H", SENTINEL))
        mu.reg_write(UC_X86_REG_SP, sp)
        mu.emu_start(CS * 16 + func, CS * 16 + SENTINEL, count=max_insns)
        if mu.reg_read(UC_X86_REG_IP) != SENTINEL:
            raise RuntimeError(f"call {func:04x} did not return (ip {mu.reg_read(UC_X86_REG_IP):04x})")
        if mu.reg_read(UC_X86_REG_SP) != STACK_TOP:
            raise RuntimeError(f"call {func:04x} left the stack unbalanced")
        return {k: mu.reg_read(r) for k, r in REGS.items()}
