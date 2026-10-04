"""Run routines of the original ELITE.EXE headless under Unicorn.

Not run on its own: the differential tests (galaxytest.py, rendertest.py, subtest.py, ...)
import Elite, call single routines of the original with it and compare what they leave in
the data segment with what the C core computes. machine.py builds the whole-game harness on
top of it.

The unpacked image (re/tools/unexepack.py) is loaded at segment 0x1000, the same addresses as
the Ghidra project, and relocated. Routines are called directly with register arguments; the
game keeps all its state in the data segment (0b00 -> 1b00 here), which tests read and write
through the helpers below.

Unicorn ships its own type information; its reg_read() is unannotated (Any), so values read
from it are stored in annotated locals rather than cast.
"""

import os
import struct
import sys
from collections.abc import Buffer, Callable, Iterable, Mapping
from typing import TYPE_CHECKING, Any, ClassVar, Final, NamedTuple

from unicorn import UC_ARCH_X86, UC_HOOK_CODE, UC_HOOK_INSN, UC_HOOK_INTR, UC_MODE_16
from unicorn.x86_const import (
    UC_X86_REG_AX,
    UC_X86_REG_BP,
    UC_X86_REG_BX,
    UC_X86_REG_CS,
    UC_X86_REG_CX,
    UC_X86_REG_DI,
    UC_X86_REG_DS,
    UC_X86_REG_DX,
    UC_X86_REG_ES,
    UC_X86_REG_FLAGS,
    UC_X86_REG_IP,
    UC_X86_REG_SI,
    UC_X86_REG_SP,
    UC_X86_REG_SS,
)

if TYPE_CHECKING:
    # The same class as unicorn.Uc, which mypy sees untyped: the package root picks its Python 2
    # or 3 module on sys.version_info.major, a test mypy does not evaluate. The tests and
    # subtest.py take Uc from here.
    from unicorn.unicorn_py3.unicorn import Uc as Uc
else:
    from unicorn import Uc as Uc

HERE: Final = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import unexepack  # noqa: E402

EXE: Final = os.path.join(HERE, "..", "..", "original", "ELITE.EXE")

LOAD: Final = 0x1000
CS: Final = LOAD
DS: Final = LOAD + 0x0B00
SS: Final = LOAD + 0x1C0C
STACK_TOP: Final = 0x4000
# Return address pushed for every call; CS:fff0 is data (ds:4ff0), never executed.
SENTINEL: Final = 0xFFF0

REGS: Final = {
    "ax": UC_X86_REG_AX,
    "bx": UC_X86_REG_BX,
    "cx": UC_X86_REG_CX,
    "dx": UC_X86_REG_DX,
    "si": UC_X86_REG_SI,
    "di": UC_X86_REG_DI,
    "bp": UC_X86_REG_BP,
    "flags": UC_X86_REG_FLAGS,
}

# Registers by name (the keys of REGS), as call() returns them and hooked routines get them.
type Regs = dict[str, int]
# A Python stand-in for a near routine (hook): fn(emu, regs), optionally returning registers to set.
type HookFn = Callable[[Elite, Regs], Mapping[str, int] | None]
# An interrupt handler (Elite.on_intr): fn(emu, intno) -> True when it handled the interrupt.
type IntrFn = Callable[[Elite, int], bool]
# Unicorn's code hook: fn(uc, address, size, user_data).
type CodeHook = Callable[[Uc, int, int, Any], None]


class DivReg(NamedTuple):
    """A register divisor (div bl, idiv cx, ...)."""

    name: str
    width: int  # bytes


class DivMem(NamedTuple):
    """A memory divisor, [seg:base+index+disp]; missing parts are None."""

    base_reg: str | None
    index_reg: str | None
    disp: int
    seg: str | None
    width: int  # bytes


class Division(NamedTuple):
    """A div/idiv site of segment 0000."""

    signed: bool  # idiv
    src: DivReg | DivMem


_DIVS: dict[int, Division] | None = None  # div/idiv sites of segment 0000, found once per process


class Elite:
    """The original's image in a Unicorn 8086, with helpers to call its routines and to read and
    write its data segment."""

    # Only after devices():
    ports: list[tuple[int, int]]  # (port, byte) written to the sound ports, in order
    port_in: dict[int, int]  # what a port reads, if not 0

    _REG_PAIRS: ClassVar[dict[str, str]] = {
        "al": "ax", "ah": "ax", "bl": "bx", "bh": "bx", "cl": "cx", "ch": "cx", "dl": "dx", "dh": "dx",
    }  # fmt: skip
    _SEGS: ClassVar[dict[str, int]] = {
        "ds": UC_X86_REG_DS, "es": UC_X86_REG_ES, "ss": UC_X86_REG_SS, "cs": UC_X86_REG_CS,
    }  # fmt: skip

    def __init__(self) -> None:
        with open(EXE, "rb") as f:
            raw = unexepack.unpack(f.read(), quiet=True)
        hdr = struct.unpack_from("<H", raw, 8)[0] * 16
        img = bytearray(raw[hdr:])
        nrel, rtab = struct.unpack_from("<H", raw, 6)[0], struct.unpack_from("<H", raw, 0x18)[0]
        for k in range(nrel):
            off, seg = struct.unpack_from("<HH", raw, rtab + 4 * k)
            p = seg * 16 + off
            v = struct.unpack_from("<H", img, p)[0]
            img[p : p + 2] = struct.pack("<H", (v + LOAD) & 0xFFFF)
        self.mu = mu = Uc(UC_ARCH_X86, UC_MODE_16)
        mu.mem_map(0, 0x100000)
        mu.mem_write(LOAD * 16, bytes(img))
        mu.hook_add(UC_HOOK_INTR, self._intr)
        self.on_intr: IntrFn | None = None  # fn(emu, intno) -> True when it handled the interrupt
        self.pyfuncs: dict[int, HookFn] = {}
        self._resume: int | None = None
        self._hook_divisions(img)

    def devices(self, drivers: bool = False) -> None:
        """No hardware: port reads give 0, writes to the speaker's ports (PIT 42h/43h, 61h) are
        noted in self.ports. The music driver's far entries (segment 2270) return at once; with
        drivers, only its Roland ones do (008f, 0116, and 0000 and 0045 on a Roland: ds:b5b7 0),
        the AdLib's run."""
        from unicorn.x86_const import UC_X86_INS_IN, UC_X86_INS_OUT

        self.ports = []
        self.port_in = {}  # what a port reads, if not 0

        def port_read(mu: Uc, port: int, size: int, _: Any) -> int:
            return self.port_in.get(port, 0)

        def port_write(mu: Uc, port: int, size: int, value: int, _: Any) -> None:
            if port in (0x40, 0x42, 0x43, 0x61, 0x388, 0x389):  # PIT, speaker gate, AdLib
                self.ports.append((port, value & 0xFF))

        def roland(mu: Uc, addr: int, size: int, u: Any) -> None:
            # 0000 and 0045 return at once on a Roland (ds:b5b7 0), run on an AdLib
            if not self.r8(0xB5B7):
                self._retf(mu, addr, size, u)

        self.mu.hook_add(UC_HOOK_INSN, port_read, None, 1, 0, UC_X86_INS_IN)
        self.mu.hook_add(UC_HOOK_INSN, port_write, None, 1, 0, UC_X86_INS_OUT)
        stubs: dict[int, CodeHook]
        if drivers:
            stubs = {0x008F: self._retf, 0x0116: self._retf, 0x0000: roland, 0x0045: roland}
        else:
            stubs = dict.fromkeys((0, 59, 69, 143, 278, 6086, 6169, 6234), self._retf)
        for off, stub in stubs.items():
            at = (LOAD + 0x2270) * 16 + off
            self.mu.hook_add(UC_HOOK_CODE, stub, begin=at, end=at)

    def _retf(self, mu: Uc, addr: int, size: int, _: Any) -> None:
        """Code hook: return far at once (a stubbed driver entry)."""
        sp: int = mu.reg_read(UC_X86_REG_SP)
        ip, cs = struct.unpack("<HH", mu.mem_read(SS * 16 + sp, 4))
        mu.reg_write(UC_X86_REG_SP, sp + 4)
        mu.reg_write(UC_X86_REG_CS, cs)
        mu.reg_write(UC_X86_REG_IP, ip)

    def _intr(self, mu: Uc, intno: int, _: Any) -> None:
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

    def _hook_divisions(self, img: bytes | bytearray) -> None:
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
    def _find_divisions(img: bytes | bytearray) -> dict[int, Division]:
        """The div/idiv instructions of segment 0000 by offset, from the control-flow recovery
        (re/tools/explore.py) walked from the entry and its extra roots."""
        from capstone.x86 import X86_OP_MEM, X86_OP_REG

        divs: dict[int, Division] = {}
        sys.path.insert(0, os.path.join(HERE, "..", "tools"))
        import explore

        ex = explore.Explorer(bytes(img))  # it only reads the image
        ex.funcs[(0, 0)] = set()
        stderr, sys.stderr = sys.stderr, open(os.devnull, "w")  # explorer warnings
        try:
            for r in [(0, 0), *explore.EXTRA_ROOTS]:
                ex.walk(*r)
            ex.resolve()
        finally:
            sys.stderr.close()
            sys.stderr = stderr
        for (seg, off), i in ex.insns.items():
            if seg == 0 and i.mnemonic in ("div", "idiv"):
                op = i.operands[0]
                src: DivReg | DivMem
                if op.type == X86_OP_REG:
                    src = DivReg(i.reg_name(op.reg), op.size)
                elif op.type == X86_OP_MEM:
                    m = op.mem
                    src = DivMem(
                        i.reg_name(m.base) if m.base else None,
                        i.reg_name(m.index) if m.index else None,
                        m.disp,
                        i.reg_name(m.segment) if m.segment else None,
                        op.size,
                    )
                divs[off] = Division(i.mnemonic == "idiv", src)
        return divs

    def _reg(self, name: str) -> int:
        """A register by name: the word registers of REGS, the segment registers and the byte
        halves (al, ah, ...)."""
        mu = self.mu
        full = self._REG_PAIRS
        v: int = (
            mu.reg_read(REGS[full.get(name, name)])
            if name not in self._SEGS
            else mu.reg_read(self._SEGS[name])
        )
        if name.endswith("h") and name in full:
            return v >> 8
        if name.endswith("l") and name in full:
            return v & 0xFF
        return v

    def _divisor(self, src: DivReg | DivMem) -> int:
        """The divisor's raw (unsigned) value."""
        if isinstance(src, DivReg):
            return self._reg(src.name)
        ea = (
            src.disp
            + (self._reg(src.base_reg) if src.base_reg else 0)
            + (self._reg(src.index_reg) if src.index_reg else 0)
        ) & 0xFFFF
        seg = src.seg or ("ss" if src.base_reg in ("bp",) else "ds")
        raw = bytes(self.mu.mem_read(self._reg(seg) * 16 + ea, src.width))
        return int.from_bytes(raw, "little")

    def _div(self, mu: Uc, addr: int, size: int, _: Any) -> None:
        """Code hook at a div/idiv: skip to ds:01f8 when it would fault (#DE)."""
        signed, src = self._divs[addr - CS * 16]
        d, width = self._divisor(src), src.width
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

    def hook(self, func: int, fn: HookFn) -> None:
        """Replace the near routine at CS:func by fn(emu, regs) followed by a near `ret`.
        fn may return a dict of registers to set."""
        self.pyfuncs[func] = fn
        self.mu.hook_add(UC_HOOK_CODE, self._py, begin=CS * 16 + func, end=CS * 16 + func)

    def _py(self, mu: Uc, addr: int, size: int, _: Any) -> None:
        fn = self.pyfuncs[addr - CS * 16]
        regs: Regs = {k: mu.reg_read(r) for k, r in REGS.items()}
        out = fn(self, regs) or {}
        for k, v in out.items():
            mu.reg_write(REGS[k], v & 0xFFFF)
        sp: int = mu.reg_read(UC_X86_REG_SP)
        ret = struct.unpack("<H", mu.mem_read(SS * 16 + sp, 2))[0]
        mu.reg_write(UC_X86_REG_SP, sp + 2)
        mu.reg_write(UC_X86_REG_IP, ret)

    # ---- stack segment (ship models and their table live there) ----
    def ss_rb(self, off: int, n: int = 1) -> bytes:
        """n bytes at ss:off."""
        return bytes(self.mu.mem_read(SS * 16 + off, n))

    def ss_r16(self, off: int) -> int:
        """The word at ss:off."""
        v: int = struct.unpack("<H", self.ss_rb(off, 2))[0]
        return v

    # ---- data segment helpers ----
    def rb(self, off: int, n: int = 1) -> bytes:
        """n bytes at ds:off."""
        return bytes(self.mu.mem_read(DS * 16 + off, n))

    def wb(self, off: int, data: Buffer | Iterable[int]) -> None:
        """Write bytes at ds:off."""
        self.mu.mem_write(DS * 16 + off, bytes(data))

    def r8(self, off: int) -> int:
        """The byte at ds:off."""
        return self.rb(off)[0]

    def r16(self, off: int) -> int:
        """The word at ds:off."""
        v: int = struct.unpack("<H", self.rb(off, 2))[0]
        return v

    def w8(self, off: int, v: int) -> None:
        """Store the low byte of v at ds:off."""
        self.wb(off, [v & 0xFF])

    def w16(self, off: int, v: int) -> None:
        """Store the low word of v at ds:off."""
        self.wb(off, struct.pack("<H", v & 0xFFFF))

    def cstr(self, off: int, n: int = 64) -> bytes:
        """The NUL-terminated string at ds:off (at most n bytes, without the NUL)."""
        s = self.rb(off, n)
        return s[: s.index(0)] if 0 in s else s

    # ---- calls ----
    def call(self, func: int, max_insns: int = 10_000_000, until: int | None = None, **regs: int) -> Regs:
        """Near-call CS:func with the given registers; return the registers on return, or when
        execution reaches CS:until (for routines that go on to draw)."""
        mu = self.mu
        for seg, v in ((UC_X86_REG_DS, DS), (UC_X86_REG_ES, DS), (UC_X86_REG_SS, SS), (UC_X86_REG_CS, CS)):
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
