#!/usr/bin/env python3
"""Unpack a Microsoft EXEPACK-compressed DOS executable into a plain MZ file.

usage: unexepack.py IN.EXE OUT.EXE

EXEPACK stores the program image RLE-compressed (commands read backwards from the end:
0xB0 = fill, 0xB2 = copy, bit 0 = last), followed by a stub at CS:0 with a 16-byte header
(real IP, CS, SP, SS, unpacked size in paragraphs, "RB") and a packed relocation table
(for each of 16 64 KB frames: count, then that many offsets).
"""
import struct
import sys


def unpack(data: bytes) -> bytes:
    (_, last, pages, nrel, hdr_par, minalloc, maxalloc, ss, sp, _, ip, cs, _, _) = \
        struct.unpack_from("<2sHHHHHHHHHHHHH", data)
    size = (pages - 1) * 512 + last if last else pages * 512
    img = data[hdr_par * 16:size]
    stub = img[cs * 16:]
    real_ip, real_cs, _, stub_size, real_sp, real_ss, dest_par, sig = struct.unpack_from("<7H2s", stub)
    if sig != b"RB":
        raise SystemExit("not EXEPACK (no RB signature)")
    if ip != 0x10:
        raise SystemExit(f"unexpected stub entry {ip:#x}")

    # Decompress backwards, in place, as the stub does.
    src = img[:cs * 16]
    buf = bytearray(src) + bytes(max(0, dest_par * 16 - len(src)))
    si = len(src) - 1
    while src[si] == 0xFF:  # padding to a paragraph
        si -= 1
    di = dest_par * 16 - 1
    while True:
        cmd = src[si]
        length = src[si - 2] | src[si - 1] << 8
        si -= 3
        if cmd & 0xFE == 0xB0:
            val = src[si]
            si -= 1
            for _ in range(length):
                buf[di] = val
                di -= 1
        elif cmd & 0xFE == 0xB2:
            for _ in range(length):
                buf[di] = src[si]
                di -= 1
                si -= 1
        else:
            raise SystemExit(f"bad command {cmd:#x} at {si + 3:#x}")
        if cmd & 1:
            break
    body = bytes(buf[:dest_par * 16])

    # Relocation table follows the "Packed file is corrupt" message.
    msg = stub.index(b"Packed file is corrupt") + len(b"Packed file is corrupt")
    relocs = []
    p = msg
    for frame in range(16):
        (count,) = struct.unpack_from("<H", stub, p)
        p += 2
        for _ in range(count):
            (off,) = struct.unpack_from("<H", stub, p)
            p += 2
            relocs.append((off, frame * 0x1000))

    # Build a normal MZ: 28-byte header + relocs, padded to a paragraph.
    hdr_len = 28 + 4 * len(relocs)
    hdr_len = (hdr_len + 15) // 16 * 16
    total = hdr_len + len(body)
    # Keep the memory the packed image asked for, measured from the unpacked end.
    extra = max(0, (len(img) // 16 + minalloc) - dest_par)
    out = bytearray(struct.pack("<2sHHHHHHHHHHHHH", b"MZ", total % 512, (total + 511) // 512,
                                len(relocs), hdr_len // 16, extra, maxalloc,
                                real_ss, real_sp, 0, real_ip, real_cs, 28, 0))
    for off, seg in relocs:
        out += struct.pack("<HH", off, seg)
    out += bytes(hdr_len - len(out))
    out += body
    print(f"unpacked {len(img)} -> {len(body)} bytes, {len(relocs)} relocations, "
          f"entry {real_cs:04x}:{real_ip:04x}, stack {real_ss:04x}:{real_sp:04x}, "
          f"stub {stub_size} bytes", file=sys.stderr)
    return bytes(out)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    with open(sys.argv[1], "rb") as f:
        packed = f.read()
    with open(sys.argv[2], "wb") as f:
        f.write(unpack(packed))
