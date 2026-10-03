#!/usr/bin/env python3
"""Disassemble a range of the unpacked ELITE.EXE: disasm.py SEG:OFF [LEN]."""
import struct
import sys
from pathlib import Path

from capstone import CS_ARCH_X86, CS_MODE_16, Cs

d = (Path(__file__).resolve().parents[2] / "original/elite_unpacked.exe").read_bytes()
img = d[struct.unpack_from("<H", d, 8)[0] * 16:]
seg, off = (int(x, 16) for x in sys.argv[1].split(":"))
n = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x40
md = Cs(CS_ARCH_X86, CS_MODE_16)
for i in md.disasm(img[seg * 16 + off:seg * 16 + off + n], off):
    print(f"{seg:04x}:{i.address:04x}  {i.bytes.hex():14} {i.mnemonic} {i.op_str}")
