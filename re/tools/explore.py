#!/usr/bin/env python3
"""Recursive-descent disassembler for the unpacked ELITE.EXE.

Follows near/far calls and jumps from the entry point and from extra roots, and reports
call targets (functions), indirect jumps/calls that still need resolving, and code coverage
per segment.

usage: explore.py [--roots seg:off,...] [--funcs OUT] [--indirect]
"""
import argparse
import struct
import sys
from pathlib import Path

from capstone import CS_ARCH_X86, CS_MODE_16, Cs
from capstone.x86 import X86_OP_IMM, X86_OP_REG

ROOT = Path(__file__).resolve().parents[2]
WORD_REGS = ("ax", "bx", "cx", "dx", "si", "di", "bp")
ALIASES = {"ax": ("ax",), "al": ("ax",), "ah": ("ax",), "bx": ("bx",), "bl": ("bx",), "bh": ("bx",),
           "cx": ("cx",), "cl": ("cx",), "ch": ("cx",), "dx": ("dx",), "dl": ("dx",), "dh": ("dx",),
           "si": ("si",), "di": ("di",), "bp": ("bp",)}
CODE_SEGS = {0x0000: 0xB000, 0x2270: 0x3010}  # segment -> size in bytes


def load():
    d = (ROOT / "original/elite_unpacked.exe").read_bytes()
    hdr = struct.unpack_from("<H", d, 8)[0] * 16
    return d[hdr:]


class Explorer:
    def __init__(self, img):
        self.img = img
        self.md = Cs(CS_ARCH_X86, CS_MODE_16)
        self.md.detail = True
        self.insns = {}  # (seg, off) -> insn
        self.funcs = {}  # (seg, off) -> set of callers
        self.indirect = []  # (seg, off, text)
        self.labels = set()
        self.trampolines = []  # (seg, off of ret, target)
        self.computed = []  # (seg, off, text, resolved target)

    def insn_at(self, seg, off):
        base = seg * 16 + off
        for i in self.md.disasm(self.img[base:base + 16], off, 1):
            return i
        return None

    def walk(self, seg, off, regs=None, stack=None):
        """Follow control flow, carrying known 16-bit register values and pushed constants
        along each path (enough for `push imm / ret` trampolines and `mov r,imm / add r,imm /
        call r` computed calls). Calls are assumed to preserve registers and the stack."""
        todo = [(seg, off, dict(regs or {}), list(stack or []))]
        while todo:
            seg, off, regs, stack = todo.pop()
            while (seg, off) not in self.insns:
                if seg not in CODE_SEGS or off >= CODE_SEGS[seg]:
                    print(f"warning: flow leaves code at {seg:04x}:{off:04x}", file=sys.stderr)
                    break
                i = self.insn_at(seg, off)
                if i is None:
                    print(f"warning: bad opcode at {seg:04x}:{off:04x}", file=sys.stderr)
                    break
                self.insns[(seg, off)] = i
                m = i.mnemonic
                nxt = off + i.size
                ops = i.operands

                def target(op):
                    if op.type == X86_OP_IMM:
                        return op.imm & 0xFFFF
                    if op.type == X86_OP_REG:
                        return regs.get(i.reg_name(op.reg))
                    return None

                if m == "ret" and not ops and stack and stack[-1] is not None:
                    t = (seg, stack[-1])
                    self.labels.add(t)
                    self.trampolines.append((seg, off, t[1]))
                    todo.append((seg, t[1], dict(regs), stack[:-1]))
                    break
                if m in ("ret", "retf", "iret", "hlt"):
                    break
                if m in ("lcall", "ljmp"):
                    if len(ops) == 2 and ops[0].type == X86_OP_IMM:
                        t = (ops[0].imm & 0xFFFF, ops[1].imm & 0xFFFF)
                        (self.funcs.setdefault(t, set()).add((seg, off)) if m == "lcall"
                         else self.labels.add(t))
                        todo.append((t[0], t[1], {}, []))
                    else:
                        self.indirect.append((seg, off, f"{m} {i.op_str}"))
                    if m == "ljmp":
                        break
                    off = nxt
                    continue
                if m in ("call", "jmp") or m.startswith("j") or m.startswith("loop"):
                    t = target(ops[0]) if ops else None
                    if t is not None:
                        t = (seg, t)
                        if m == "call":
                            self.funcs.setdefault(t, set()).add((seg, off))
                            todo.append((seg, t[1], dict(regs), []))
                        else:
                            self.labels.add(t)
                            todo.append((seg, t[1], dict(regs), list(stack)))
                        if ops[0].type != X86_OP_IMM:
                            self.computed.append((seg, off, f"{m} {i.op_str}", t[1]))
                    else:
                        self.indirect.append((seg, off, f"{m} {i.op_str}"))
                    if m == "jmp":
                        break
                    off = nxt
                    continue
                self.track(i, regs, stack)
                off = nxt

    def track(self, i, regs, stack):
        m, ops = i.mnemonic, i.operands
        name = lambda op: i.reg_name(op.reg) if op.type == X86_OP_REG else None
        val = lambda op: (op.imm & 0xFFFF) if op.type == X86_OP_IMM else regs.get(name(op))
        if m == "push":
            stack.append(val(ops[0]))
            return
        if m == "pop":
            v = stack.pop() if stack else None
            if name(ops[0]):
                regs[name(ops[0])] = v
            return
        if m in ("pushf", "pushaw", "pusha"):
            stack.append(None)
            return
        _, written = i.regs_access()
        dst = name(ops[0]) if ops else None
        known = None
        if dst in WORD_REGS and len(ops) == 2:
            a, b = regs.get(dst), val(ops[1])
            if m == "mov":
                known = b
            elif m == "lea" and ops[1].mem.base and not ops[1].mem.index:
                base = regs.get(i.reg_name(ops[1].mem.base))
                known = None if base is None else (base + ops[1].mem.disp) & 0xFFFF
            elif m == "lea" and not ops[1].mem.base and not ops[1].mem.index:
                known = ops[1].mem.disp & 0xFFFF
            elif a is not None and b is not None:
                known = {"add": lambda: a + b, "sub": lambda: a - b, "xor": lambda: a ^ b,
                         "or": lambda: a | b, "and": lambda: a & b}.get(m, lambda: None)()
            elif m == "xor" and name(ops[1]) == dst:
                known = 0
        elif dst in WORD_REGS and len(ops) == 1 and regs.get(dst) is not None:
            known = {"inc": regs[dst] + 1, "dec": regs[dst] - 1}.get(m)
        for r in written:
            for w in ALIASES.get(i.reg_name(r), ()):
                regs.pop(w, None)
        if dst in WORD_REGS and known is not None:
            regs[dst] = known & 0xFFFF

    def coverage(self):
        for seg, size in CODE_SEGS.items():
            n = sum(i.size for (s, _), i in self.insns.items() if s == seg)
            print(f"segment {seg:04x}: {n}/{size} bytes decoded ({100 * n / size:.1f}%), "
                  f"{sum(1 for s, _ in self.funcs if s == seg)} call targets")

    def gaps(self, seg, minlen=16):
        size = CODE_SEGS[seg]
        cov = bytearray(size)
        for (s, o), i in self.insns.items():
            if s == seg:
                cov[o:o + i.size] = b"\1" * i.size
        out, start = [], None
        for o in range(size + 1):
            c = cov[o] if o < size else 1
            if not c and start is None:
                start = o
            elif c and start is not None:
                if o - start >= minlen:
                    out.append((start, o))
                start = None
        return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--roots", default="")
    ap.add_argument("--funcs")
    ap.add_argument("--indirect", action="store_true")
    ap.add_argument("--gaps", action="store_true")
    a = ap.parse_args()
    ex = Explorer(load())
    ex.funcs[(0, 0)] = set()
    roots = [(0, 0)]
    for r in filter(None, a.roots.split(",")):
        s, o = r.split(":")
        roots.append((int(s, 16), int(o, 16)))
        ex.funcs.setdefault(roots[-1], set())
    for s, o in roots:
        ex.walk(s, o)
    ex.coverage()
    if a.indirect:
        for s, o, t in sorted(ex.indirect):
            print(f"indirect {s:04x}:{o:04x}  {t}")
    if a.indirect:
        for s, o, t, v in sorted(ex.computed):
            print(f"computed {s:04x}:{o:04x}  {t} -> {v:04x}")
        for s, o, v in sorted(ex.trampolines):
            print(f"trampoline {s:04x}:{o:04x}  ret -> {v:04x}")
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
