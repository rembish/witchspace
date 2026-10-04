"""Witchspace's icon, in every form the program and the page use, from one drawing.

The drawing is ``assets/icon.png``: a green wireframe ship on black, 1024 x 1024, made for
this project (it is not from the game). Small icons cannot show its thin lines, so there are
two renderings:

- 48 px and under: the ship's silhouette, dark green with a bright outline;
- 64 px and over: the wireframe, its lines thickened to the size, over the dark hull, so it
  reads on light backgrounds as well as dark.

Outputs (committed, so building needs no Python):

- ``src/witchspace.ico``: 16 to 256 px, built into the Windows program (src/witchspace.rc.in);
- ``src/icon.h``: 64 x 64 RGBA, the window's icon elsewhere (SDL_SetWindowIcon in src/main.c);
- ``web/favicon.png``: 32 x 32, the browser tab's.

    uv run tools/make_icon.py          (make icon)
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "assets" / "icon.png"

HULL = (6, 46, 20, 255)  # the wireframe's background, inside the ship
FILL = (10, 110, 45, 255)  # the silhouette's body
EDGE = (90, 255, 130, 255)  # the silhouette's outline
SMALL = (16, 24, 32, 48)
LARGE = (64, 128, 256)


def square(src: Image.Image) -> Image.Image:
    """The ship, centred in a square with a small margin."""
    lit = src.getchannel("G").point(lambda v: 255 if v > 40 else 0)
    box = lit.getbbox()
    if box is None:
        raise SystemExit(f"{SOURCE}: no ship in it")
    x0, y0, x1, y1 = box
    half = max(x1 - x0, y1 - y0) / 2 * 1.04
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    return src.crop((int(cx - half), int(cy - half), int(cx + half), int(cy + half)))


def hull(ship: Image.Image) -> Image.Image:
    """The ship's outline filled: its lines closed up, all not reached from outside."""
    lines = ship.getchannel("G").point(lambda v: 255 if v > 40 else 0)
    closed = lines.filter(ImageFilter.MaxFilter(15)).filter(ImageFilter.MinFilter(15))
    ImageDraw.floodfill(closed, (0, 0), 128)  # the corner is outside the ship
    return closed.point(lambda v: 0 if v == 128 else 255)


def silhouette(shape: Image.Image, size: int) -> Image.Image:
    """A small icon: the hull filled, with a bright outline (drawn at 4x, then reduced)."""
    big = shape.resize((size * 4, size * 4), Image.Resampling.LANCZOS).point(lambda v: 255 if v > 127 else 0)
    edge = ImageChops.subtract(big, big.filter(ImageFilter.MinFilter(5)))
    out = Image.new("RGBA", big.size, (0, 0, 0, 0))
    out.paste(FILL, mask=big)
    out.paste(EDGE, mask=edge)
    return out.resize((size, size), Image.Resampling.LANCZOS)


def wireframe(ship: Image.Image, shape: Image.Image, size: int) -> Image.Image:
    """A large icon: the lines, thickened to stay about a pixel wide, over the dark hull."""
    k = max(1, round(ship.width / size * 0.6)) | 1  # odd, as the filter needs
    lines = ship.filter(ImageFilter.MaxFilter(k)) if k > 1 else ship
    lines = lines.resize((size, size), Image.Resampling.LANCZOS)
    glow = lines.getchannel("G").point(lambda v: 0 if v < 20 else min(255, int((v - 20) * 2.2)))
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.paste(HULL, mask=shape.resize((size, size), Image.Resampling.LANCZOS))
    out.paste(lines.point(lambda v: min(255, int(v * 1.3))).convert("RGBA"), mask=glow)
    return out


def icon(ship: Image.Image, shape: Image.Image, size: int) -> Image.Image:
    return silhouette(shape, size) if size in SMALL else wireframe(ship, shape, size)


def c_header(im: Image.Image) -> str:
    """The pixels as a C array, RGBA bytes row by row."""
    data = im.tobytes()
    rows = [", ".join(f"0x{b:02x}" for b in data[i : i + 16]) for i in range(0, len(data), 16)]
    body = ",\n    ".join(rows)
    return (
        "/* Witchspace's window icon, made by tools/make_icon.py from assets/icon.png: do not edit. */\n"
        "// clang-format off\n"
        f"#define ICON_SIZE {im.width}\n"
        f"static const unsigned char icon_rgba[{len(data)}] = {{\n    {body}\n}};\n"
        "// clang-format on\n"
    )


def main() -> None:
    ship = square(Image.open(SOURCE).convert("RGB"))
    shape = hull(ship)
    sizes = SMALL + LARGE
    icons = [icon(ship, shape, s) for s in sizes]
    icons[-1].save(ROOT / "src" / "witchspace.ico", sizes=[(s, s) for s in sizes], append_images=icons[:-1])
    (ROOT / "src" / "icon.h").write_text(c_header(icons[sizes.index(64)]))
    icons[sizes.index(32)].save(ROOT / "web" / "favicon.png", optimize=True)
    print(f"icon: {', '.join(str(s) for s in sizes)} px from {SOURCE.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
