#!/usr/bin/env python3
"""The original's tables as read from original/ELITE.EXE, to check the core's loader.

usage: gen_tables.py [--check path/to/ep_datadump]

The core reads these tables from your copy of ELITE.EXE at start-up (core/ep_data.c); this
extracts them independently, each place confirmed by the code that uses it, and prints them
in ep_datadump's format. With --check it runs ep_datadump and compares.

Reads original/ELITE.EXE (unpacked in memory by unexepack.py) and original/ELITE.GRF (via
grf.py). Without --check it prints one line per table, "name: hex bytes" or "name: hex
words"; with --check it prints the first differing lines and "N tables, M differ", and exits
1 if any differ. Every table address is checked against the instruction that uses it
(code()) before it is trusted, so a different EXE fails an assertion instead of producing
wrong tables.
"""

import os
import struct
import subprocess
import sys
from collections.abc import Iterable
from typing import Final

HERE: Final = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import grf  # noqa: E402
import unexepack  # noqa: E402

ROOT: Final = os.path.join(HERE, "..", "..")
with open(os.path.join(ROOT, "original", "ELITE.EXE"), "rb") as _f:
    raw = unexepack.unpack(_f.read(), quiet=True)
img = raw[struct.unpack_from("<H", raw, 8)[0] * 16 :]  # the load image, segment 0000 at 0
DS: Final = 0x0B00 * 16  # the data segment


def ds(off: int, n: int) -> bytes:
    """n bytes at ds:off."""
    return img[DS + off : DS + off + n]


def words(off: int, n: int) -> list[int]:
    """n words at ds:off."""
    return list(struct.unpack_from(f"<{n}H", img, DS + off))


def code(addr: int, expect: bytes) -> None:
    """Check the instruction bytes at addr (segment 0000) before trusting a table address."""
    assert img[addr : addr + len(expect)] == expect, f"unexpected code at {addr:04x}"


def arr(vals: list[int], per: int = 12, fmt: str = "{:#06x}") -> str:
    """vals as the body of a C array initialiser, per values a line."""
    return ",\n    ".join(
        ", ".join(fmt.format(v) for v in vals[i : i + per]) for i in range(0, len(vals), per)
    )


# load_galaxy_seed (5e25): bx = 0x5509 + 6 * galaxy
code(0x5E34, bytes([0x81, 0xC3, 0x09, 0x55]))
galaxy_seeds = words(0x5509, 9 * 3)  # 8: the hidden ninth galaxy
# planet_name (6130): digram pairs at ds:5585, index = seed byte & 0x1f
code(0x6153, bytes([0xBB, 0x85, 0x55]))
digrams = ds(0x5585, 64)
assert b"LAVE" in digrams


def cstr(off: int) -> bytes:
    """The NUL-terminated string at ds:off (at most 255 characters), without the NUL."""
    b = img[DS + off : DS + off + 256]
    return b[: b.index(0)]


def clit(b: bytes) -> str:
    """C string literal; escapes split off so a following hex digit is not swallowed."""
    out, prev_hex = '"', False
    for ch in b:
        if 0x20 <= ch < 0x7F and ch not in (0x22, 0x5C):
            if prev_hex and chr(ch) in "0123456789abcdefABCDEF":
                out += '" "'
            out += chr(ch)
            prev_hex = False
        else:
            out += f"\\x{ch:02x}"
            prev_hex = True
    return out + '"'


# describe_system (632b) expands the template ds:5a35 with the printer 6396: bytes 1..6 are
# control codes (handlers ds:5b3e), bytes >= 0x80 tokens (ds:5b4a: five alternatives each)
code(0x633C, bytes([0xBE, 0x35, 0x5A]))
code(0x63BC, bytes([0xBB, 0x4A, 0x5B]))
code(0x6431, bytes([0xBE, 0xFF, 0x63]))
template = cstr(0x5A35)
N_TOKENS: Final = 0xA7 - 0x80
tokens: list[list[bytes]] = []
for t in range(N_TOKENS):
    p = words(0x5B4A + 2 * t, 1)[0]
    tokens.append([cstr(q) for q in words(p, 5)])
# every token the template can reach must be in the table
reach: set[int] = set()
todo = [c for c in template if c >= 0x80]
while todo:
    t = todo.pop()
    if t not in reach:
        reach.add(t)
        todo += [c for o in tokens[t - 0x80] for c in o if c >= 0x80]
assert max(reach) < 0x80 + N_TOKENS
ian = cstr(0x63FF)

# market_prices (97d8): per commodity, factors by economy (ds:905f) and government (ds:916f),
# rows of 8 words; base prices (ds:927f); tech adjustment {a, b, flag} (ds:92a1)
code(0x97E0, bytes([0x81, 0xC3, 0x5F, 0x90]))
code(0x97F0, bytes([0x81, 0xC3, 0x6F, 0x91]))
code(0x980D, bytes([0xBE, 0x7F, 0x92]))
code(0x9804, bytes([0xC7, 0x06, 0xD8, 0x92, 0xA1, 0x92]))
code(0x9810, bytes([0xB9, 0x11, 0x00]))
N_GOODS: Final = 17
eco_factor = [words(0x905F + 16 * k, 8) for k in range(N_GOODS)]
gov_factor = [words(0x916F + 16 * k, 8) for k in range(N_GOODS)]
base_price = words(0x927F, N_GOODS)
tech_adj = [list(ds(0x92A1 + 3 * k, 3)) for k in range(N_GOODS)]
goods = [cstr(0x8F2D + 17 * k) for k in range(N_GOODS)]
code(0x9880, bytes([0xA1, 0xE4, 0x92]))
market_rng = words(0x92E0, 3)
# equipment_list (9161): records {min tech, name, i8 gov factor, i8 eco factor, word base}
code(0x916D, bytes([0xC7, 0x06, 0xA8, 0xAC, 0xEF, 0x8B]))
code(0x923A, bytes([0x80, 0x3E, 0xB0, 0xAC, 0x0E]))
N_EQUIP: Final = 14
equipment: list[tuple[int, bytes, int, int, int]] = []  # (min tech, name, gov, eco, base)
p = 0x8BEF
for _ in range(N_EQUIP):
    name = cstr(p + 1)
    q = p + 2 + len(name)
    gf, ef, base = struct.unpack_from("<bbH", img, DS + q)
    equipment.append((ds(p, 1)[0], name, gf, ef, base))
    p = q + 4
# draw_ship (43ce) / draw_model (3c90): models live in the stack segment 1c0c (bp-relative):
# table of 32 words at ss:65bc (0 = no model), sine table for the matrices at ds:2cc0
SS: Final = 0x1C0C * 16
code(0x3C92, bytes([0x8B, 0xAE, 0xBC, 0x65]))
code(0x3F60, bytes([0x8B, 0x8F, 0xC0, 0x2C]))
model_ptr: list[int] = list(struct.unpack_from("<32H", img, SS + 0x65BC))
# model face records by type byte: their length, and how many vertex references follow it
SKIP: Final = {0: 8, 2: 10, 4: 6}
REFS: Final = {0: 3, 2: 4, 4: 2}


def model_len(p: int) -> int:
    """Length of the model at ss:p: vertex count, 6 bytes a vertex, then records up to one
    with an odd type byte (other than 1); checks every vertex reference on the way."""
    nv = img[SS + p]
    q = p + 1 + 6 * nv

    def refs(at: int, n: int) -> None:  # vertex references are byte offsets of 10-byte buffer records
        for k in range(n):
            r = struct.unpack_from("<H", img, SS + at + 2 * k)[0]
            assert r % 10 == 0 and r // 10 < nv, f"model {p:04x}: bad vertex ref {r}"

    while True:
        b = img[SS + q]
        if b in SKIP:
            refs(q + 1, REFS[b])
        if b == 1:
            refs(q + 1, 1)
            q += 9
        elif b & 1:
            return q + 1 - p
        else:
            q += SKIP[b]


models = bytearray()
model_off: list[int] = []  # offset of each type's model in models, ffff = none
for p in model_ptr:
    if not p:
        model_off.append(0xFFFF)
        continue
    model_off.append(len(models))
    models += img[SS + p : SS + p + model_len(p)]
code(0x6D2F, bytes([0x8B, 0x87, 0x10, 0x64]))
sin2048 = [v - 65536 if v > 32767 else v for v in words(0x6410, 2048)]
sin1024 = [v - 65536 if v > 32767 else v for v in words(0x2CC0, 1024)]
# title_loop (9e80): ships shown in turn (ds:b263, ff-terminated), closest distance by type
# (ds:b1bc), names by type (ds:b27c)
code(0x9F59, bytes([0x8B, 0x9F, 0xBC, 0xB1]))
code(0x9F99, bytes([0xBE, 0x63, 0xB2]))
code(0x9FCB, bytes([0x8B, 0xB7, 0x7C, 0xB2]))
title_ships: list[int] = []
p = 0xB263
while ds(p, 1)[0] != 0xFF:
    title_ships.append(ds(p, 1)[0])
    p += 1
title_min_dist = words(0xB1BC, 32)
ship_names = [cstr(w) for w in words(0xB27C, 30)]
# ds:031f: function-key rows per screen (12 command ids); ds:0374: icon sprite per command id;
# ds:02fb: bar colour by ds:8711
key_rows = ds(0x31F, 6 * 12)
icon_sprite = ds(0x374, 37)
bar_colour = ds(0x2FB, 3)
# static data the texts are read from, by address (the live buffers inside are game state)
DS_TEXT: Final = [(0x0300, 0x0B00), (0x2600, 0x2D00), (0x54E2, 0xB400)]
ds_text = [(a, ds(a, b - a)) for a, b in DS_TEXT]
ds_initial = ds(0, 0x10000).rstrip(b"\0")  # the data segment as loaded
drv_initial = img[0x2270 * 16 : 0x2270 * 16 + 0x3010]  # the music driver's segment as loaded

# ELITE.GRF, the 256-colour pictures' widths (3768 leaves DL the low byte of the width)
with open(grf.GRF, "rb") as _f:
    sprite_width = [im[1] & 0xFF for im in grf.images(_f.read(), 1)]
# ds:0d40: the font, 9 bytes a glyph from 20h: 8 rows, then the width
glyph_width = [ds(0xD48 + 9 * i, 1)[0] for i in range(0x5B)]
# ds:85be: the hyperspace rings at the start of a jump (7489): delay, radius, colour each
ring_start = ds(0x85BE, 30)
# ds:80a9: role names (11) then, at 80e9, short type names (32), walked by 7110
ship_text = ds(0x80A9, 0x200)
ship_text = ship_text[: ship_text.index(b"Planet\0") + 7]
# set_video_mode (384f), MCGA: game colour -> pixel value (low byte of the words copied from
# ds:1cf3 into ds:1ee9, all undithered; checked by drawing every colour), and the DAC table
code(0x38B6, bytes([0xBE, 0xF3, 0x1C]))
code(0x38C1, bytes([0xBE, 0x44, 0x11]))
mcga_map = [w & 0xFF for w in words(0x1CF3, 256)]
assert all((w & 0xFF) == (w >> 8) for w in words(0x1CF3, 256))
dac = list(ds(0x1144, 768))
# the commander block (save file): ds:82db, length ds:82d7, as initialised in the EXE
code(0x0888, bytes([0xBA, 0xDB, 0x82]))
commander_len = words(0x82D7, 1)[0]
commander0 = ds(0x82DB, commander_len)
# atan (6e8a): binary search over 256 tangents at ds:7410 (Q15, one per 1/2048 turn)
code(0x6E96, bytes([0xBE, 0x10, 0x74]))
tan256 = words(0x7410, 256)
# laser target (abd1): hit box size by type at ds:b0e5, used byte-swapped
code(0xABFC, bytes([0x8B, 0x87, 0xE5, 0xB0]))
hit_size = [((w & 0xFF) << 8) | (w >> 8) for w in words(0xB0E5, 32)]
# collisions (66d6): radius by type at ds:7614
code(0x66ED, bytes([0x8B, 0x97, 0x14, 0x76]))
crash_radius = words(0x7614, 32)
# ships (7d14): spawn table ds:8739, 31 entries of 10 bytes {type, speed, turn, bounty,
# missiles, canisters, debris, energy, +3f, +1c}; AI frame (77e0): limits per government
# ds:886f (4 bytes: junk, trader, loner, pirate), chances ds:8899 (4 words)
code(0x7B85, bytes([0xBB, 0x39, 0x87]))
code(0x7889, bytes([0x8A, 0x87, 0x6F, 0x88]))
code(0x78A3, bytes([0x8B, 0x9F, 0x99, 0x88]))
spawn = [list(ds(0x8739 + 10 * k, 10)) for k in range(31)]
spawn_limit = [list(ds(0x886F + 4 * g, 4)) for g in range(8)]
spawn_chance = [words(0x8899 + 8 * g, 4) for g in range(8)]
# divisors of the number formatters: ds:7fe2 (32-bit, cash) and ds:8016 (16-bit)
code(0x6FDA, bytes([0xBE, 0xE2, 0x7F]))


def dump() -> list[str]:
    """The tables as ep_datadump prints them, one line each, in its order."""
    out: list[str] = []

    def b(name: str, data: Iterable[int]) -> None:  # bytes
        out.append(name + ":" + "".join(f" {x:02x}" for x in data))

    def w(name: str, data: list[int]) -> None:  # words (negative values as 16-bit)
        out.append(name + ":" + "".join(f" {x & 0xFFFF:04x}" for x in data))

    w("galaxy_seeds", galaxy_seeds)
    b("digrams", digrams)
    b("desc_template", template)
    b("desc_ian", ian)
    for opts in tokens:
        for o in opts:
            b("desc_token", o)
    for k in range(N_GOODS):
        b("goods_name", goods[k])
        w("goods_eco", eco_factor[k])
        w("goods_gov", gov_factor[k])
        b("goods_tech", tech_adj[k])
    w("goods_base", base_price)
    w("market_rng0", market_rng)
    for t, n, g, e, base in equipment:
        out.append(f"equipment: {t} {g} {e} {base} name:" + "".join(f" {x:02x}" for x in n))
    w("model_offset", model_off)
    b("models", models)
    w("sin1024", sin1024)
    w("sin2048", sin2048)
    b("title_ships", title_ships)
    w("title_min_dist", title_min_dist)
    for n in ship_names:
        b("ship_name", n)
    b("ring_start", ring_start)
    b("glyph_width", glyph_width)
    b("sprite_width", sprite_width)
    b("ds_initial", ds_initial)
    b("drv_initial", drv_initial)
    static = bytearray(0x10000)  # ds_static: the DS_TEXT ranges, zero elsewhere
    for a, data in ds_text:
        static[a : a + len(data)] = data
    b("ds_static", static)
    b("key_rows", key_rows)
    b("icon_sprite", icon_sprite)
    b("bar_colour", bar_colour)
    b("ship_text", ship_text)
    b("mcga_colour", mcga_map)
    b("dac", dac)
    w("tan256", tan256)
    w("hit_size", hit_size)
    w("crash_radius", crash_radius)
    b("spawn", [v for r in spawn for v in r])
    b("spawn_limit", [v for r in spawn_limit for v in r])
    w("spawn_chance", [v for r in spawn_chance for v in r])
    b("commander0", commander0)
    return out


def check(want: list[str], tool: str) -> int:
    """Run ep_datadump (tool) on original/ and compare its lines with want; print the first
    five differences and the count; return the exit status."""
    got = subprocess.run(
        [tool],
        capture_output=True,
        text=True,
        check=True,
        env=dict(os.environ, EP_ORIGINAL=os.path.join(ROOT, "original")),
    ).stdout.splitlines()
    bad = [(k, a, g) for k, (a, g) in enumerate(zip(want, got, strict=False)) if a != g]
    if len(want) != len(got):
        bad.append((min(len(want), len(got)), f"{len(want)} lines", f"{len(got)} lines"))
    for k, a, g in bad[:5]:
        print(f"line {k}: want {a[:100]}\n         got  {g[:100]}")
    print(f"{len(want)} tables, {len(bad)} differ")
    return 1 if bad else 0


if __name__ == "__main__":
    want = dump()
    if "--check" not in sys.argv:
        print("\n".join(want))
        sys.exit(0)
    sys.exit(check(want, sys.argv[sys.argv.index("--check") + 1]))
