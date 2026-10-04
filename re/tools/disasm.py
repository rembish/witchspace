#!/usr/bin/env python3
"""Disassemble a range of the unpacked ELITE.EXE as 16-bit x86.

usage: disasm.py SEG:OFF [LEN]

SEG:OFF is a hex address in the load image (segment 0000 is the main code, 0b00 the data
segment, 2270 the music driver); LEN is a hex byte count (default 40). Reads
original/elite_unpacked.exe (made by unexepack.py) and prints one line per instruction:
address, raw bytes, mnemonic and operands.
"""

import struct
import sys
from pathlib import Path
from typing import Final

from capstone import CS_ARCH_X86, CS_MODE_16, Cs

EXE: Final = Path(__file__).resolve().parents[2] / "original/elite_unpacked.exe"

d = EXE.read_bytes()
img = d[struct.unpack_from("<H", d, 8)[0] * 16 :]  # skip the MZ header (paragraphs at +8)
seg, off = (int(x, 16) for x in sys.argv[1].split(":"))
n = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x40
md = Cs(CS_ARCH_X86, CS_MODE_16)
for i in md.disasm(img[seg * 16 + off : seg * 16 + off + n], off):
    print(f"{seg:04x}:{i.address:04x}  {i.bytes.hex():14} {i.mnemonic} {i.op_str}")
