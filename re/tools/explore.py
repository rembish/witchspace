#!/usr/bin/env python3
"""Recursive-descent disassembler for the unpacked ELITE.EXE.

Follows near/far calls and jumps from the entry point and from extra roots, and reports
call targets (functions), indirect jumps/calls that still need resolving, and code coverage
per segment.

usage: explore.py [--roots seg:off,...] [--funcs OUT] [--indirect] [--gaps]

  --roots     further entry points (hex), walked as functions
  --funcs     write every call target with its number of callers to OUT
  --indirect  list unresolved indirect jumps/calls, computed targets, call vectors and
              push/ret trampolines
  --gaps      list undecoded stretches of 16 bytes or more in each code segment

Reads original/elite_unpacked.exe (made by unexepack.py) and prints the coverage per code
segment first. eliteemu.py uses Explorer directly to find every div/idiv of segment 0000.
"""

import argparse
import operator
import struct
import sys
from collections.abc import Callable
from pathlib import Path
from typing import Final, Protocol

from capstone import CS_ARCH_X86, CS_MODE_16, Cs
from capstone.x86 import X86_OP_IMM, X86_OP_REG


# capstone ships no type information: the parts of its instruction objects used here and by
# eliteemu.py, as protocols.
class CsMem(Protocol):
    base: int  # register id, 0 = none
    index: int
    disp: int
    segment: int


class CsOperand(Protocol):
    type: int  # X86_OP_REG / X86_OP_IMM / X86_OP_MEM
    reg: int
    imm: int
    mem: CsMem
    size: int


class CsInsn(Protocol):
    address: int
    size: int
    bytes: bytearray
    mnemonic: str
    op_str: str
    operands: list[CsOperand]

    def reg_name(self, reg_id: int) -> str: ...

    def regs_access(self) -> tuple[list[int], list[int]]: ...  # (read, written) register ids


Addr = tuple[int, int]  # (segment, offset)
Regs = dict[str, int]  # known 16-bit register values
Stack = list[int | None]  # pushed words, None = unknown
Pending = tuple[int, int, Regs, Stack]  # a control-flow path still to follow

ROOT: Final = Path(__file__).resolve().parents[2]
WORD_REGS: Final = ("ax", "bx", "cx", "dx", "si", "di", "bp")
# Writing any of these registers forgets the value of the word register(s) it is part of.
ALIASES: Final[dict[str, tuple[str, ...]]] = {
    "ax": ("ax",), "al": ("ax",), "ah": ("ax",), "bx": ("bx",), "bl": ("bx",), "bh": ("bx",),
    "cx": ("cx",), "cl": ("cx",), "ch": ("cx",), "dx": ("dx",), "dl": ("dx",), "dh": ("dx",),
    "si": ("si",), "di": ("di",), "bp": ("bp",),
}  # fmt: skip
# Two-operand instructions whose result the tracker computes when both operands are known.
BINOPS: Final[dict[str, Callable[[int, int], int]]] = {
    "add": operator.add,
    "sub": operator.sub,
    "xor": operator.xor,
    "or": operator.or_,
    "and": operator.and_,
}
DATA_SEG: Final = 0x0B00
# Indirect calls/jumps through word tables in the data segment: site -> (table, entries).
# Bounds come from the code that builds the index (see NOTES.md).
JUMP_TABLES: Final[dict[Addr, tuple[int, int]]] = {
    (0, 0x02F0): (0x0368, 6),
    (0, 0x041F): (0x0399, 32),  # command index from the per-screen key map ds:030d
    (0, 0x3DFB): (0x2B6C, 3),
    (0, 0x7155): (0x81EA, 4),
    (0, 0x7813): (0x8720, 8),
    (0, 0x63B0): (0x5B3E, 6),  # text control codes 1..6 (5b4a on is the token table); handlers return to 63d8
}
# Code reached only through values the walker cannot see (pushed return addresses etc.).
EXTRA_ROOTS: list[Addr] = [
    (0, 0x63D8),
    (0, 0x0215),
    (0, 0x4A99),
    (0, 0x00D6),
    (0, 0x00E4),
    (0, 0x0144),
    (0, 0x014C),
]
EXTRA_ROOTS += [(0, a) for a in (0x16C2, 0x16DA, 0x16CD, 0x16A6, 0x1675)]
# (63d8: text code return; 0215..014c interrupt handlers the game installs; 16xx: the MCGA
# span routines patched in from ds:1b1a)
CODE_SEGS: Final = {0x0000: 0xB000, 0x2270: 0x3010}  # segment -> size in bytes


def load() -> bytes:
    """The load image of original/elite_unpacked.exe (the file without its MZ header)."""
    d = (ROOT / "original/elite_unpacked.exe").read_bytes()
    hdr = struct.unpack_from("<H", d, 8)[0] * 16
    return d[hdr:]


def reg_of(i: CsInsn, op: CsOperand) -> str | None:
    """The register name of a register operand, else None."""
    return i.reg_name(op.reg) if op.type == X86_OP_REG else None


def value_of(i: CsInsn, op: CsOperand, regs: Regs) -> int | None:
    """The 16-bit value of an immediate or known register operand, else None."""
    if op.type == X86_OP_IMM:
        return op.imm & 0xFFFF
    r = reg_of(i, op)
    return regs.get(r) if r is not None else None


class Explorer:
    """Disassembly state of one image, filled by walk() and resolve()."""

    def __init__(self, img: bytes) -> None:
        self.img = img
        self.md = Cs(CS_ARCH_X86, CS_MODE_16)
        self.md.detail = True
        self.insns: dict[Addr, CsInsn] = {}  # (seg, off) -> insn
        self.funcs: dict[Addr, set[Addr]] = {}  # (seg, off) -> set of callers
        self.indirect: list[tuple[int, int, str]] = []  # (seg, off, text)
        self.labels: set[Addr] = set()
        self.trampolines: list[tuple[int, int, int]] = []  # (seg, off of ret, target)
        self.vectors: dict[int, set[int]] = {}  # data cell -> constants stored into it
        self.computed: list[tuple[int, int, str, int]] = []  # (seg, off, text, resolved target)

    def insn_at(self, seg: int, off: int) -> CsInsn | None:
        """Decode the one instruction at seg:off, None if it is not valid."""
        base = seg * 16 + off
        insn: CsInsn | None = next(iter(self.md.disasm(self.img[base : base + 16], off, 1)), None)
        return insn

    def walk(self, seg: int, off: int, regs: Regs | None = None, stack: Stack | None = None) -> None:
        """Follow control flow, carrying known 16-bit register values and pushed constants
        along each path (enough for `push imm / ret` trampolines and `mov r,imm / add r,imm /
        call r` computed calls). Calls are assumed to preserve registers and the stack."""
        todo: list[Pending] = [(seg, off, dict(regs or {}), list(stack or []))]
        while todo:
            self._follow(*todo.pop(), todo)

    def _follow(self, seg: int, off: int, regs: Regs, stack: Stack, todo: list[Pending]) -> None:
        """Decode one path until it ends or runs into code already decoded; branches it
        meets go on todo."""
        while (seg, off) not in self.insns:
            if seg not in CODE_SEGS or off >= CODE_SEGS[seg]:
                print(f"warning: flow leaves code at {seg:04x}:{off:04x}", file=sys.stderr)
                return
            i = self.insn_at(seg, off)
            if i is None:
                print(f"warning: bad opcode at {seg:04x}:{off:04x}", file=sys.stderr)
                return
            self.insns[(seg, off)] = i
            if not self._step(seg, off, i, regs, stack, todo):
                return
            off += i.size

    def _step(self, seg: int, off: int, i: CsInsn, regs: Regs, stack: Stack, todo: list[Pending]) -> bool:
        """Account for one instruction; True if flow falls through to the next one."""
        m, ops = i.mnemonic, i.operands
        if m == "ret" and not ops and stack and (ret_to := stack[-1]) is not None:
            # push imm / ret: a jump to the pushed address
            self.labels.add((seg, ret_to))
            self.trampolines.append((seg, off, ret_to))
            todo.append((seg, ret_to, dict(regs), stack[:-1]))
            return False
        if m in ("ret", "retf", "iret", "hlt"):
            return False
        if m in ("lcall", "ljmp"):
            self._far(seg, off, i, todo)
            return m != "ljmp"
        if m in ("call", "jmp") or m.startswith("j") or m.startswith("loop"):
            self._near(seg, off, i, regs, stack, todo)
            return m != "jmp"
        self.track(i, regs, stack)
        return True

    def _far(self, seg: int, off: int, i: CsInsn, todo: list[Pending]) -> None:
        """lcall/ljmp: a direct target starts a new path with nothing known."""
        m, ops = i.mnemonic, i.operands
        if len(ops) == 2 and ops[0].type == X86_OP_IMM:
            t = (ops[0].imm & 0xFFFF, ops[1].imm & 0xFFFF)
            if m == "lcall":
                self.funcs.setdefault(t, set()).add((seg, off))
            else:
                self.labels.add(t)
            todo.append((t[0], t[1], {}, []))
        else:
            self.indirect.append((seg, off, f"{m} {i.op_str}"))

    def _near(self, seg: int, off: int, i: CsInsn, regs: Regs, stack: Stack, todo: list[Pending]) -> None:
        """call/jmp/jcc/loop: the target is an immediate or a register of known value. A call
        starts with an empty stack; a jump keeps the path's registers and stack."""
        m, ops = i.mnemonic, i.operands
        to = value_of(i, ops[0], regs) if ops else None
        if to is None:
            self.indirect.append((seg, off, f"{m} {i.op_str}"))
            return
        if m == "call":
            self.funcs.setdefault((seg, to), set()).add((seg, off))
            todo.append((seg, to, dict(regs), []))
        else:
            self.labels.add((seg, to))
            todo.append((seg, to, dict(regs), list(stack)))
        if ops[0].type != X86_OP_IMM:
            self.computed.append((seg, off, f"{m} {i.op_str}", to))

    def track(self, i: CsInsn, regs: Regs, stack: Stack) -> None:
        """Update the known registers and stack for an instruction that does not transfer
        control."""
        if self._track_stack(i, stack, regs):
            return
        _, written = i.regs_access()
        ops = i.operands
        dst = reg_of(i, ops[0]) if ops else None
        known = self._result(i, regs, dst) if dst is not None and dst in WORD_REGS else None
        for r in written:
            for w in ALIASES.get(i.reg_name(r), ()):
                regs.pop(w, None)
        if dst is not None and known is not None:
            regs[dst] = known & 0xFFFF

    @staticmethod
    def _track_stack(i: CsInsn, stack: Stack, regs: Regs) -> bool:
        """push/pop/pushf/popf and changes to sp; True if i was one of them."""
        m, ops = i.mnemonic, i.operands
        if m == "push":
            stack.append(value_of(i, ops[0], regs))
        elif m == "pop":
            v = stack.pop() if stack else None
            if r := reg_of(i, ops[0]):
                if v is None:
                    regs.pop(r, None)  # popped an unknown word
                else:
                    regs[r] = v
        elif m in ("pushf", "pushaw", "pusha"):
            stack.append(None)
        elif m == "popf":
            if stack:
                stack.pop()
        elif ops and ops[0].type == X86_OP_REG and reg_of(i, ops[0]) == "sp" and m != "cmp":
            # add/sub sp,imm drops or reserves words; anything else loses track entirely.
            if m in ("add", "sub") and ops[1].type == X86_OP_IMM and ops[1].imm % 2 == 0:
                n = ops[1].imm // 2
                if m == "add":
                    del stack[max(0, len(stack) - n) :]
                else:
                    stack.extend([None] * n)
            else:
                stack.clear()
        else:
            return False
        return True

    @staticmethod
    def _result(i: CsInsn, regs: Regs, dst: str) -> int | None:
        """The value i leaves in the word register dst, if it can be known (unmasked)."""
        m, ops = i.mnemonic, i.operands
        if len(ops) == 2:
            a, b = regs.get(dst), value_of(i, ops[1], regs)
            if m == "mov":
                return b
            if m == "lea" and ops[1].mem.base and not ops[1].mem.index:  # lea r, [base + disp]
                base = regs.get(i.reg_name(ops[1].mem.base))
                return None if base is None else (base + ops[1].mem.disp) & 0xFFFF
            if m == "lea" and not ops[1].mem.base and not ops[1].mem.index:  # lea r, [disp]
                return ops[1].mem.disp & 0xFFFF
            if a is not None and b is not None:
                op = BINOPS.get(m)
                return op(a, b) if op else None
            if m == "xor" and reg_of(i, ops[1]) == dst:
                return 0
            return None
        if len(ops) == 1 and (v := regs.get(dst)) is not None:
            return {"inc": v + 1, "dec": v - 1}.get(m)
        return None

    def word(self, off: int) -> int:
        """The word at ds:off."""
        w: int = struct.unpack_from("<H", self.img, DATA_SEG * 16 + off)[0]
        return w

    def resolve(self) -> None:
        """Feed jump-table entries and constants stored into call-vector cells
        (`mov word ptr [cell], imm` where `call/jmp word ptr [cell]` exists) back in as roots,
        until nothing new is found."""
        done: set[Addr] = set()
        while True:
            new, cells = self._table_targets()
            new += self._vector_targets(cells)
            new = [x for x in new if x[:2] not in done]
            if not new:
                return
            for seg, t, is_call, site in new:
                done.add((seg, t))
                if is_call:
                    self.funcs.setdefault((seg, t), set()).add(site)
                else:
                    self.labels.add((seg, t))
                self.walk(seg, t)

    def _table_targets(self) -> tuple[list[tuple[int, int, bool, Addr]], dict[int, tuple[int, int, bool]]]:
        """Targets from JUMP_TABLES for the indirect sites found so far, and the cells of the
        `call/jmp word ptr [cell]` sites: cell -> (seg, off, is call)."""
        new: list[tuple[int, int, bool, Addr]] = []
        cells: dict[int, tuple[int, int, bool]] = {}
        for seg, off, text in self.indirect:
            if (seg, off) in JUMP_TABLES:
                base, n = JUMP_TABLES[(seg, off)]
                for k in range(n):
                    t = self.word(base + 2 * k)
                    if t:
                        new.append((seg, t, "call" in text, (seg, off)))
            elif text.split()[-1].startswith("[0x") and "ptr" in text:
                cells[int(text.split("[")[1].rstrip("]"), 16)] = (seg, off, "call" in text)
        return new, cells

    def _vector_targets(self, cells: dict[int, tuple[int, int, bool]]) -> list[tuple[int, int, bool, Addr]]:
        """Constants stored by `mov word ptr [cell], imm` into the given cells; recorded in
        self.vectors too."""
        new: list[tuple[int, int, bool, Addr]] = []
        for i in self.insns.values():
            if (
                i.mnemonic == "mov"
                and i.op_str.startswith("word ptr [0x")
                and len(i.operands) == 2
                and i.operands[1].type == X86_OP_IMM
            ):
                cell = i.operands[0].mem.disp & 0xFFFF
                if cell in cells and i.operands[0].mem.base == 0:
                    cs, co, is_call = cells[cell]
                    new.append((cs, i.operands[1].imm & 0xFFFF, is_call, (cs, co)))
                    self.vectors.setdefault(cell, set()).add(i.operands[1].imm & 0xFFFF)
        return new

    def coverage(self) -> None:
        """Print the decoded bytes and call targets per code segment."""
        for seg, size in CODE_SEGS.items():
            n = sum(i.size for (s, _), i in self.insns.items() if s == seg)
            print(
                f"segment {seg:04x}: {n}/{size} bytes decoded ({100 * n / size:.1f}%), "
                f"{sum(1 for s, _ in self.funcs if s == seg)} call targets"
            )

    def gaps(self, seg: int, minlen: int = 16) -> list[tuple[int, int]]:
        """Undecoded [start, end) ranges of at least minlen bytes in code segment seg."""
        size = CODE_SEGS[seg]
        cov = bytearray(size)
        for (s, o), i in self.insns.items():
            if s == seg:
                cov[o : o + i.size] = b"\1" * i.size
        out: list[tuple[int, int]] = []
        start: int | None = None
        for o in range(size + 1):
            c = cov[o] if o < size else 1
            if not c and start is None:
                start = o
            elif c and start is not None:
                if o - start >= minlen:
                    out.append((start, o))
                start = None
        return out


def report_indirect(ex: Explorer) -> None:
    """--indirect: what is left unresolved, and what was resolved by value tracking."""
    for s, o, t in sorted(ex.indirect):
        print(f"indirect {s:04x}:{o:04x}  {t}")
    for s, o, t, v in sorted(ex.computed):
        print(f"computed {s:04x}:{o:04x}  {t} -> {v:04x}")
    for cell, vals in sorted(ex.vectors.items()):
        print(f"vector ds:{cell:04x} = " + " ".join(f"{v:04x}" for v in sorted(vals)))
    for s, o, v in sorted(ex.trampolines):
        print(f"trampoline {s:04x}:{o:04x}  ret -> {v:04x}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--roots", default="")
    ap.add_argument("--funcs")
    ap.add_argument("--indirect", action="store_true")
    ap.add_argument("--gaps", action="store_true")
    a = ap.parse_args()
    ex = Explorer(load())
    ex.funcs[(0, 0)] = set()
    roots = [(0, 0), *EXTRA_ROOTS]
    for r in filter(None, a.roots.split(",")):
        s, o = r.split(":")
        roots.append((int(s, 16), int(o, 16)))
        ex.funcs.setdefault(roots[-1], set())
    for s, o in roots:
        ex.walk(s, o)
    ex.resolve()
    ex.coverage()
    if a.indirect:
        report_indirect(ex)
    if a.gaps:
        for seg in CODE_SEGS:
            for g0, g1 in ex.gaps(seg):
                print(f"gap {seg:04x}:{g0:04x}-{g1:04x} ({g1 - g0} bytes)")
    if a.funcs:
        with open(a.funcs, "w") as f:
            for (s, o), callers in sorted(ex.funcs.items()):
                f.write(f"{s:04x}:{o:04x} {len(callers)}\n")


if __name__ == "__main__":
    main()
