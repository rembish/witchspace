#!/usr/bin/env python3
"""Decode ELITE.GRF, the bitmap file of Elite Plus.

usage: grf.py dump [OUTDIR]     write every image as PNG (default assets-local/grf): the 16-colour
                                set with the EGA and the VGA palette, the 256-colour set
       grf.py sheet [OUTDIR]    overview sheets of both sets with image numbers, and the MCGA
                                title picture with its own palette
       grf.py list              print the image sizes

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

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
GRF = os.path.join(ROOT, "original", "ELITE.GRF")

sys.path.insert(0, HERE)
import unexepack  # noqa: E402


def palettes():
    """The game's palettes from ELITE.EXE (set_video_mode 384f): EGA attribute registers
    ds:1122; VGA attribute registers ds:1133 into the DAC table ds:1144, which MCGA loads
    whole (256 entries, 6 bits per component)."""
    raw = unexepack.unpack(open(os.path.join(ROOT, "original", "ELITE.EXE"), "rb").read(), quiet=True)
    img = raw[struct.unpack_from("<H", raw, 8)[0] * 16:]
    ds = lambda off, n: img[0xB000 + off:0xB000 + off + n]  # noqa: E731
    dac = ds(0x1144, 768)
    vga = [tuple(c * 255 // 63 for c in dac[3 * i:3 * i + 3]) for i in range(256)]

    def ega6(v):  # attribute value rgbRGB -> RGB
        return tuple(85 * ((v >> hi & 1) + 2 * (v >> lo & 1)) for lo, hi in ((2, 5), (1, 4), (0, 3)))

    ega = [ega6(v) for v in ds(0x1122, 16)]
    vga16 = [vga[v] for v in ds(0x1133, 16)]
    title = ds(0x1744, 768)  # MCGA palette of the title picture (loaded at 3b7f)
    palettes.title = [tuple(c * 255 // 63 for c in title[3 * i:3 * i + 3]) for i in range(256)]
    return ega, vga16, vga


class Stream:
    def __init__(self, data, pos):
        self.d, self.p = data, pos

    def byte(self):
        b = self.d[self.p]
        self.p += 1
        return b


def unpack(st, n):
    out = bytearray()
    while len(out) < n:
        c = st.byte()
        if c < 0x80:
            for _ in range(c + 1):
                out.append(st.byte())
        else:
            out += bytes([st.byte()]) * (1 - (c - 256))
    return out  # the original also overshoots if a run crosses n


def images(data, cls):
    _alloc, off, count = struct.unpack_from("<HIH", data, 8 * cls)
    st = Stream(data, off)
    out = []
    for _ in range(count):
        h = bytes([st.byte(), st.byte(), st.byte()])
        if cls == 0:
            w, mask, hgt = h
            planes = [b for b in range(8) if mask >> b & 1]
            raw = unpack(st, w * hgt * len(planes))
            out.append(("planar", w * 8, hgt, mask, raw))
        else:
            w = (h[0] | h[1] << 8) & 0x7FFF
            hgt = h[2]
            raw = unpack(st, w * hgt)
            out.append(("chunky", w, hgt, h[1] >> 7, raw))
    return out


def to_rgb(img, pal):
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
                px[3 * (y * w + x):3 * (y * w + x) + 3] = bytes(pal[c & 15])
    else:
        for i in range(w * h):
            c = raw[i]
            px[3 * i:3 * i + 3] = bytes(pal[c]) if c < len(pal) else bytes((c, c, c))
    return w, h, bytes(px)


def png(path, w, h, rgb):
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    rows = b"".join(b"\0" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def main():
    data = open(GRF, "rb").read()
    cmd = sys.argv[1] if len(sys.argv) > 1 else "list"
    if cmd == "list":
        for cls in (0, 1):
            for i, img in enumerate(images(data, cls)):
                print(f"{cls} {i:3d} {img[0]} {img[1]}x{img[2]} extra={img[3]:#x}")
    elif cmd == "sheet":
        from PIL import Image, ImageDraw
        out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "assets-local", "grf")
        os.makedirs(out, exist_ok=True)
        ega, vga16, vga = palettes()
        for cls, name, pal in ((0, "vga", vga16), (1, "mcga", vga)):
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
        title = images(data, 1)[138]
        w, h, rgb = to_rgb(title, palettes.title)
        Image.frombytes("RGB", (w, h), rgb).resize((w * 3, h * 3), Image.NEAREST).save(
            os.path.join(out, "title-mcga.png"))
        print(f"wrote {out}/sheet-vga.png, sheet-mcga.png, title-mcga.png")
    elif cmd == "dump":
        out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "assets-local", "grf")
        os.makedirs(out, exist_ok=True)
        ega, vga16, vga = palettes()
        for cls, name, pal in ((0, "ega", ega), (0, "vga", vga16), (1, "mcga", vga)):
            for i, img in enumerate(images(data, cls)):
                if i == 138 and cls == 1:
                    pal = palettes.title
                if img[1] and img[2]:
                    png(os.path.join(out, f"{name}-{i:03d}.png"), *to_rgb(img, pal))
        print(f"wrote {out}")


if __name__ == "__main__":
    main()
