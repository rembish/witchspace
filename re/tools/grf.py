#!/usr/bin/env python3
"""Decode ELITE.GRF, the bitmap file of Elite Plus.

usage: grf.py dump [OUTDIR]     write every image as PNG (default assets-local/grf): the 16-colour
                                set with the EGA and the VGA palette, the 256-colour set
       grf.py sheet [OUTDIR]    overview sheets of both sets with image numbers, and the MCGA
                                title picture with its own palette (needs Pillow)
       grf.py list              print the image sizes

Reads original/ELITE.GRF, and original/ELITE.EXE for the palettes. gen_tables.py uses images()
and GRF for the sprite widths.

Header: 4 entries of 8 bytes, indexed by video class (ds:10bc >> 1: 0 EGA/VGA 16 colours,
1 MCGA 256 colours): paragraphs to allocate, 32-bit file offset, image count. Each image
(load_grf 31f6 / 3362 / 33a6) is a 3-byte header and a PackBits-style body (n >= 0: n + 1
literal bytes, n < 0: the next byte 1 - n times; the stream runs on across images):
- 16 colours: width in bytes, plane mask, height; planes (bit 0 first) of width x height
  bytes, 8 pixels per byte, MSB first.
- 256 colours: width in pixels (15 bits), height; one byte per pixel.
"""

import os
import struct
import sys
import zlib
from collections.abc import Sequence
from typing import Final, NamedTuple

HERE: Final = os.path.dirname(os.path.abspath(__file__))
ROOT: Final = os.path.join(HERE, "..", "..")
GRF: Final = os.path.join(ROOT, "original", "ELITE.GRF")

sys.path.insert(0, HERE)
import unexepack  # noqa: E402

RGB = tuple[int, ...]  # (r, g, b), 0..255 each
Palette = Sequence[RGB]

TITLE_IMAGE: Final = 138  # the MCGA title picture, drawn with its own palette


class Palettes(NamedTuple):
    """The game's palettes, as 8-bit RGB."""

    ega: list[RGB]  # 16 colours through the EGA attribute registers
    vga16: list[RGB]  # 16 colours through the VGA attribute registers and the DAC
    vga: list[RGB]  # the DAC table, 256 colours (MCGA)
    title: list[RGB]  # MCGA palette of the title picture


class GrfImage(NamedTuple):
    """One decoded image. Indexable as the plain tuple it used to be: (kind, w, h, extra, raw)."""

    kind: str  # "planar" (16 colours) or "chunky" (256 colours)
    width: int  # in pixels
    height: int
    extra: int  # planar: plane mask; chunky: bit 15 of the width word
    raw: bytearray  # unpacked body: planes one after another, or one byte a pixel


def _dac_rgb(dac: bytes) -> list[RGB]:
    """6-bit DAC triples -> 8-bit RGB."""
    return [tuple(c * 255 // 63 for c in dac[3 * i : 3 * i + 3]) for i in range(len(dac) // 3)]


def _ega6(v: int) -> RGB:
    """EGA attribute value rgbRGB -> RGB."""
    return tuple(85 * ((v >> hi & 1) + 2 * (v >> lo & 1)) for lo, hi in ((2, 5), (1, 4), (0, 3)))


def palettes() -> Palettes:
    """The game's palettes from ELITE.EXE (set_video_mode 384f): EGA attribute registers
    ds:1122; VGA attribute registers ds:1133 into the DAC table ds:1144, which MCGA loads
    whole (256 entries, 6 bits per component)."""
    with open(os.path.join(ROOT, "original", "ELITE.EXE"), "rb") as f:
        raw = unexepack.unpack(f.read(), quiet=True)
    img = raw[struct.unpack_from("<H", raw, 8)[0] * 16 :]

    def ds(off: int, n: int) -> bytes:
        return img[0xB000 + off : 0xB000 + off + n]

    vga = _dac_rgb(ds(0x1144, 768))
    ega = [_ega6(v) for v in ds(0x1122, 16)]
    vga16 = [vga[v] for v in ds(0x1133, 16)]
    title = _dac_rgb(ds(0x1744, 768))  # MCGA palette of the title picture (loaded at 3b7f)
    return Palettes(ega, vga16, vga, title)


class Stream:
    """A read position in the file; the PackBits stream runs on across images."""

    def __init__(self, data: bytes, pos: int) -> None:
        self.d, self.p = data, pos

    def byte(self) -> int:
        b = self.d[self.p]
        self.p += 1
        return b


def unpack(st: Stream, n: int) -> bytearray:
    """Unpack at least n bytes of PackBits from st."""
    out = bytearray()
    while len(out) < n:
        c = st.byte()
        if c < 0x80:
            for _ in range(c + 1):
                out.append(st.byte())
        else:
            out += bytes([st.byte()]) * (1 - (c - 256))
    return out  # the original also overshoots if a run crosses n


def images(data: bytes, cls: int) -> list[GrfImage]:
    """All images of video class cls (0: 16 colours, 1: 256 colours) in the GRF data."""
    _alloc, off, count = struct.unpack_from("<HIH", data, 8 * cls)
    st = Stream(data, off)
    out: list[GrfImage] = []
    for _ in range(count):
        h = bytes([st.byte(), st.byte(), st.byte()])
        if cls == 0:
            w, mask, hgt = h
            planes = [b for b in range(8) if mask >> b & 1]
            raw = unpack(st, w * hgt * len(planes))
            out.append(GrfImage("planar", w * 8, hgt, mask, raw))
        else:
            w = (h[0] | h[1] << 8) & 0x7FFF
            hgt = h[2]
            raw = unpack(st, w * hgt)
            out.append(GrfImage("chunky", w, hgt, h[1] >> 7, raw))
    return out


def to_rgb(img: GrfImage, pal: Palette) -> tuple[int, int, bytes]:
    """(width, height, RGB bytes) of img in palette pal; 256-colour pixels beyond the palette
    come out grey."""
    kind, w, h, extra, raw = img
    px = bytearray(w * h * 3)
    if kind == "planar":
        planes = [b for b in range(8) if extra >> b & 1]
        bw = w // 8
        for y in range(h):
            for x in range(w):
                c = 0
                for k, b in enumerate(planes):
                    byte = raw[k * bw * h + y * bw + x // 8]
                    if byte >> (7 - x % 8) & 1:
                        c |= 1 << b
                px[3 * (y * w + x) : 3 * (y * w + x) + 3] = bytes(pal[c & 15])
    else:
        for i in range(w * h):
            c = raw[i]
            px[3 * i : 3 * i + 3] = bytes(pal[c]) if c < len(pal) else bytes((c, c, c))
    return w, h, bytes(px)


def png(path: str, w: int, h: int, rgb: bytes) -> None:
    """Write an 8-bit RGB PNG without needing Pillow."""

    def chunk(t: bytes, d: bytes) -> bytes:
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))

    rows = b"".join(b"\0" + rgb[y * w * 3 : (y + 1) * w * 3] for y in range(h))
    with open(path, "wb") as f:
        f.write(
            b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows, 9))
            + chunk(b"IEND", b"")
        )


def cmd_list(data: bytes) -> None:
    """grf.py list: class, number, kind, size and extra of every image."""
    for cls in (0, 1):
        for i, img in enumerate(images(data, cls)):
            print(f"{cls} {i:3d} {img[0]} {img[1]}x{img[2]} extra={img[3]:#x}")


def _outdir() -> str:
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "assets-local", "grf")
    os.makedirs(out, exist_ok=True)
    return out


def cmd_sheet(data: bytes) -> None:
    """grf.py sheet: one overview sheet per set (images at 2x with their numbers) and the
    title picture at 3x."""
    # Pillow is optional (only this command needs it) and not installed in the dev venv.
    from PIL import Image, ImageDraw  # type: ignore[import-not-found, unused-ignore]

    out = _outdir()
    pals = palettes()
    for cls, name, pal in ((0, "vga", pals.vga16), (1, "mcga", pals.vga)):
        imgs = images(data, cls)
        cells = []
        for i, img in enumerate(imgs):
            if not (img[1] and img[2]) or img[1] * img[2] > 64000 - 1:
                continue
            w, h, rgb = to_rgb(img, pal)
            cells.append((i, Image.frombytes("RGB", (w, h), rgb).resize((w * 2, h * 2), Image.NEAREST)))
        sheet_w, x, y, row_h, placed = 1400, 0, 0, 0, []
        for i, im in cells:
            if x + im.width + 8 > sheet_w:
                x, y, row_h = 0, y + row_h + 22, 0
            placed.append((i, im, x, y))
            x += im.width + 12
            row_h = max(row_h, im.height)
        sheet = Image.new("RGB", (sheet_w, y + row_h + 24), (40, 40, 48))
        d = ImageDraw.Draw(sheet)
        for i, im, px, py in placed:
            sheet.paste(im, (px, py + 14))
            d.text((px, py), str(i), fill=(255, 255, 120))
        sheet.save(os.path.join(out, f"sheet-{name}.png"))
    title = images(data, 1)[TITLE_IMAGE]
    w, h, rgb = to_rgb(title, pals.title)
    Image.frombytes("RGB", (w, h), rgb).resize((w * 3, h * 3), Image.NEAREST).save(
        os.path.join(out, "title-mcga.png")
    )
    print(f"wrote {out}/sheet-vga.png, sheet-mcga.png, title-mcga.png")


def cmd_dump(data: bytes) -> None:
    """grf.py dump: every non-empty image as {ega,vga,mcga}-NNN.png."""
    out = _outdir()
    pals = palettes()
    sets: tuple[tuple[int, str, Palette], ...] = (
        (0, "ega", pals.ega),
        (0, "vga", pals.vga16),
        (1, "mcga", pals.vga),
    )
    for cls, name, set_pal in sets:
        pal = set_pal
        for i, img in enumerate(images(data, cls)):
            if i == TITLE_IMAGE and cls == 1:
                pal = pals.title  # and stays so for the rest of the set, as before
            if img[1] and img[2]:
                png(os.path.join(out, f"{name}-{i:03d}.png"), *to_rgb(img, pal))
    print(f"wrote {out}")


def main() -> None:
    with open(GRF, "rb") as f:
        data = f.read()
    cmd = sys.argv[1] if len(sys.argv) > 1 else "list"
    if cmd == "list":
        cmd_list(data)
    elif cmd == "sheet":
        cmd_sheet(data)
    elif cmd == "dump":
        cmd_dump(data)


if __name__ == "__main__":
    main()
