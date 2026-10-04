#!/usr/bin/env python3
"""Unpack a Microsoft EXEPACK-compressed DOS executable into a plain MZ file.

usage: unexepack.py IN.EXE OUT.EXE

Reads the packed IN.EXE (original/ELITE.EXE), writes the unpacked OUT.EXE
(original/elite_unpacked.exe, which disasm.py and explore.py read) and prints a one-line
summary to stderr. eliteemu.py, grf.py and gen_tables.py call unpack() directly on the bytes.

EXEPACK stores the program image RLE-compressed (commands read backwards from the end:
0xB0 = fill, 0xB2 = copy, bit 0 = last), followed by a stub at CS:0 with a 16-byte header
(real IP, CS, SP, SS, unpacked size in paragraphs, "RB") and a packed relocation table
(for each of 16 64 KB frames: count, then that many offsets).
"""

import struct
import sys
from typing import Final, NamedTuple

MZ_HEADER: Final = "<2sHHHHHHHHHHHHH"  # signature .. overlay number, 28 bytes
CORRUPT_MSG: Final = b"Packed file is corrupt"


class Stub(NamedTuple):
    """The 16-byte header at the start of the EXEPACK stub (CS:0)."""

    real_ip: int
    real_cs: int
    mem_start: int  # unused here
    stub_size: int
    real_sp: int
    real_ss: int
    dest_par: int  # unpacked size in paragraphs
    sig: bytes


def decompress(src: bytes, dest_par: int) -> bytes:
    """Run the RLE commands backwards from the end of src, in place, as the stub does."""
    buf = bytearray(src) + bytes(max(0, dest_par * 16 - len(src)))
    si = len(src) - 1
    while src[si] == 0xFF:  # padding to a paragraph
        si -= 1
    di = dest_par * 16 - 1
    while True:
        cmd = src[si]
        length = src[si - 2] | src[si - 1] << 8
        si -= 3
        if cmd & 0xFE == 0xB0:  # fill: one byte, length times
            val = src[si]
            si -= 1
            for _ in range(length):
                buf[di] = val
                di -= 1
        elif cmd & 0xFE == 0xB2:  # copy: length literal bytes
            for _ in range(length):
                buf[di] = src[si]
                di -= 1
                si -= 1
        else:
            raise SystemExit(f"bad command {cmd:#x} at {si + 3:#x}")
        if cmd & 1:  # last command
            break
    return bytes(buf[: dest_par * 16])


def relocations(stub: bytes) -> list[tuple[int, int]]:
    """The packed relocation table, which follows the "Packed file is corrupt" message:
    (offset, segment) pairs, segment being the 64 KB frame number * 0x1000."""
    p = stub.index(CORRUPT_MSG) + len(CORRUPT_MSG)
    relocs: list[tuple[int, int]] = []
    for frame in range(16):
        (count,) = struct.unpack_from("<H", stub, p)
        p += 2
        for _ in range(count):
            (off,) = struct.unpack_from("<H", stub, p)
            p += 2
            relocs.append((off, frame * 0x1000))
    return relocs


def unpack(data: bytes, quiet: bool = False) -> bytes:
    """Return the unpacked MZ executable for the EXEPACK-packed data; unless quiet, print a
    summary to stderr."""
    (_, last, pages, _nrel, hdr_par, minalloc, maxalloc, _ss, _sp, _, ip, cs, _, _) = struct.unpack_from(
        MZ_HEADER, data
    )
    size = (pages - 1) * 512 + last if last else pages * 512
    img = data[hdr_par * 16 : size]
    stub = img[cs * 16 :]
    hdr = Stub._make(struct.unpack_from("<7H2s", stub))
    if hdr.sig != b"RB":
        raise SystemExit("not EXEPACK (no RB signature)")
    if ip != 0x10:
        raise SystemExit(f"unexpected stub entry {ip:#x}")

    body = decompress(img[: cs * 16], hdr.dest_par)
    relocs = relocations(stub)

    # Build a normal MZ: 28-byte header + relocs, padded to a paragraph.
    hdr_len = 28 + 4 * len(relocs)
    hdr_len = (hdr_len + 15) // 16 * 16
    total = hdr_len + len(body)
    # Keep the memory the packed image asked for, measured from the unpacked end.
    extra = max(0, (len(img) // 16 + minalloc) - hdr.dest_par)
    out = bytearray(
        struct.pack(
            MZ_HEADER,
            b"MZ",
            total % 512,
            (total + 511) // 512,
            len(relocs),
            hdr_len // 16,
            extra,
            maxalloc,
            hdr.real_ss,
            hdr.real_sp,
            0,
            hdr.real_ip,
            hdr.real_cs,
            28,
            0,
        )
    )
    for off, seg in relocs:
        out += struct.pack("<HH", off, seg)
    out += bytes(hdr_len - len(out))
    out += body
    if not quiet:
        print(
            f"unpacked {len(img)} -> {len(body)} bytes, {len(relocs)} relocations, "
            f"entry {hdr.real_cs:04x}:{hdr.real_ip:04x}, stack {hdr.real_ss:04x}:{hdr.real_sp:04x}, "
            f"stub {hdr.stub_size} bytes",
            file=sys.stderr,
        )
    return bytes(out)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    with open(sys.argv[1], "rb") as fin:
        packed = fin.read()
    with open(sys.argv[2], "wb") as fout:
        fout.write(unpack(packed))
