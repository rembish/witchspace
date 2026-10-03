#!/usr/bin/env python3
"""Extract Elite Plus game tables from original/ELITE.EXE into core/ep_tables.{c,h}."""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import unexepack  # noqa: E402

ROOT = os.path.join(HERE, "..", "..")
raw = unexepack.unpack(open(os.path.join(ROOT, "original", "ELITE.EXE"), "rb").read(), quiet=True)
img = raw[struct.unpack_from("<H", raw, 8)[0] * 16:]
DS = 0x0B00 * 16


def ds(off, n):
    return img[DS + off:DS + off + n]


def words(off, n):
    return list(struct.unpack_from(f"<{n}H", img, DS + off))


def code(addr, expect):
    """Check the instruction bytes at addr (segment 0000) before trusting a table address."""
    assert img[addr:addr + len(expect)] == expect, f"unexpected code at {addr:04x}"


def arr(vals, per=12, fmt="{:#06x}"):
    return ",\n    ".join(", ".join(fmt.format(v) for v in vals[i:i + per])
                          for i in range(0, len(vals), per))


# load_galaxy_seed (5e25): bx = 0x5509 + 6 * galaxy
code(0x5E34, bytes([0x81, 0xC3, 0x09, 0x55]))
galaxy_seeds = words(0x5509, 8 * 3)
# planet_name (6130): digram pairs at ds:5585, index = seed byte & 0x1f
code(0x6153, bytes([0xBB, 0x85, 0x55]))
digrams = ds(0x5585, 64)
assert b"LAVE" in digrams



def cstr(off):
    b = img[DS + off:DS + off + 256]
    return b[:b.index(0)]


def clit(b):
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
N_TOKENS = 0xA7 - 0x80
tokens = []
for t in range(N_TOKENS):
    p = words(0x5B4A + 2 * t, 1)[0]
    tokens.append([cstr(q) for q in words(p, 5)])
reach, todo = set(), [c for c in template if c >= 0x80]
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
N_GOODS = 17
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
N_EQUIP = 14
equipment, p = [], 0x8BEF
for _ in range(N_EQUIP):
    name = cstr(p + 1)
    q = p + 2 + len(name)
    gf, ef, base = struct.unpack_from("<bbH", img, DS + q)
    equipment.append((ds(p, 1)[0], name, gf, ef, base))
    p = q + 4
# draw_ship (43ce) / draw_model (3c90): models live in the stack segment 1c0c (bp-relative):
# table of 32 words at ss:65bc (0 = no model), sine table for the matrices at ds:2cc0
SS = 0x1C0C * 16
code(0x3C92, bytes([0x8B, 0xAE, 0xBC, 0x65]))
code(0x3F60, bytes([0x8B, 0x8F, 0xC0, 0x2C]))
model_ptr = list(struct.unpack_from("<32H", img, SS + 0x65BC))
SKIP = {0: 8, 2: 10, 4: 6}


def model_len(p):
    nv = img[SS + p]
    q = p + 1 + 6 * nv

    def refs(at, n):  # vertex references are byte offsets of 10-byte buffer records
        for k in range(n):
            r = struct.unpack_from("<H", img, SS + at + 2 * k)[0]
            assert r % 10 == 0 and r // 10 < nv, f"model {p:04x}: bad vertex ref {r}"

    while True:
        b = img[SS + q]
        if b in SKIP:
            refs(q + 1, {0: 3, 2: 4, 4: 2}[b])
        if b == 1:
            refs(q + 1, 1)
            q += 9
        elif b & 1:
            return q + 1 - p
        else:
            q += SKIP[b]


models, model_off = bytearray(), []
for p in model_ptr:
    if not p:
        model_off.append(0xFFFF)
        continue
    model_off.append(len(models))
    models += img[SS + p:SS + p + model_len(p)]
sin1024 = [v - 65536 if v > 32767 else v for v in words(0x2CC0, 1024)]


def rows(table, fmt="{}"):
    return ",\n".join("    { " + ", ".join(fmt.format(v) for v in r) + " }" for r in table)


c = ["/* Generated by re/tools/gen_tables.py from original/ELITE.EXE. Do not edit. */",
     "/* clang-format off */", '#include "ep_tables.h"', "",
     "/* ds:5509: seed words of the eight galaxies */",
     "const uint16_t ep_galaxy_seeds[8][3] = {\n    "
     + ",\n    ".join("{ " + arr(galaxy_seeds[k:k + 3]) + " }" for k in range(0, 24, 3)) + "\n};",
     "/* ds:5585: name digrams; index 0 is two spaces (no letters) */",
     'const char ep_digrams[65] = "' + digrams.decode("ascii") + '";',
     "/* ds:5a35: system description template */",
     f"const char ep_desc_template[] = {clit(template)};",
     "/* ds:63ff: appended by text code 2 */",
     f"const char ep_desc_ian[] = {clit(ian)};",
     "/* ds:5b4a: description tokens 0x80.., five alternatives each */",
     f"const char *const ep_desc_tokens[{N_TOKENS}][5] = {{",
     ",\n".join("    { " + ", ".join(clit(o) for o in opts) + " }" for opts in tokens),
     "};",
     "/* ds:8f2d: commodity names padded to 13, then the unit */",
     f"const char *const ep_goods_names[{N_GOODS}] = {{",
     ",\n".join("    " + clit(g) for g in goods), "};",
     "/* ds:905f: price factor by economy (8.8 fixed point) */",
     f"const uint16_t ep_goods_eco_factor[{N_GOODS}][8] = {{", rows(eco_factor), "};",
     "/* ds:916f: price factor by government (8.8 fixed point) */",
     f"const uint16_t ep_goods_gov_factor[{N_GOODS}][8] = {{", rows(gov_factor), "};",
     "/* ds:927f: base prices (tenths of a credit) */",
     f"const uint16_t ep_goods_base_price[{N_GOODS}] = {{ " + ", ".join(map(str, base_price)) + " };",
     "/* ds:92a1: tech level adjustment: price factor 256 + a + b * tech; third byte: illegal */",
     f"const int8_t ep_goods_tech_adj[{N_GOODS}][3] = {{", rows([[v - 256 if v > 127 else v for v in r] for r in tech_adj]), "};",
     "/* ds:8bef: equipment {min tech, name, government factor, economy factor, base price} */",
     f"const ep_equipment_record ep_equipment[{N_EQUIP}] = {{",
     ",\n".join(f"    {{ {t}, {clit(n)}, {g}, {e}, {b} }}" for t, n, g, e, b in equipment), "};",
     "/* ss:65bc: ship models by type, offsets into ep_models (ffff: none). Model: vertex count,",
     " * vertices (3 x i16), then face groups {01, vertex offset (x10), normal (3 x i16)} each followed",
     " * by primitives {00 triangle, 02 quad, 04 line: vertex offsets, colour} and ending in 03 */",
     "const uint16_t ep_model_offset[32] = { " + ", ".join(f"{v:#06x}" for v in model_off) + " };",
     f"const uint8_t ep_models[{len(models)}] = {{",
     ",\n".join("    " + ", ".join(f"{b:#04x}" for b in models[i:i + 12]) for i in range(0, len(models), 12)),
     "};",
     "/* ds:2cc0: sine for rotation matrices, 1024 steps per turn, Q15 */",
     "const int16_t ep_sin1024[1024] = {",
     ",\n".join("    " + ", ".join(str(v) for v in sin1024[i:i + 12]) for i in range(0, 1024, 12)),
     "};",
     "/* ds:92e0: market generator state at start-up */",
     "const uint16_t ep_market_rng0[3] = { " + ", ".join(f"{v:#06x}" for v in market_rng) + " };", ""]
h = ["/* Generated by re/tools/gen_tables.py from original/ELITE.EXE. Do not edit. */",
     "#ifndef EP_TABLES_H", "#define EP_TABLES_H", "", "#include <stdint.h>", "",
     "extern const uint16_t ep_galaxy_seeds[8][3];", "extern const char ep_digrams[65];", "",
     f"#define EP_DESC_TOKENS {N_TOKENS}", "",
     "extern const char ep_desc_template[];", "extern const char ep_desc_ian[];",
     "extern const char *const ep_desc_tokens[EP_DESC_TOKENS][5];", "",
     f"#define EP_GOODS {N_GOODS}", "",
     "extern const char *const ep_goods_names[EP_GOODS];",
     "extern const uint16_t ep_goods_eco_factor[EP_GOODS][8];",
     "extern const uint16_t ep_goods_gov_factor[EP_GOODS][8];",
     "extern const uint16_t ep_goods_base_price[EP_GOODS];",
     "extern const int8_t ep_goods_tech_adj[EP_GOODS][3];",
     "extern const uint16_t ep_market_rng0[3];", "",
     f"#define EP_EQUIPMENT {N_EQUIP}", "",
     "typedef struct {", "    uint8_t min_tech;", "    const char *name;",
     "    int8_t gov_factor, eco_factor;", "    uint16_t base_price;", "} ep_equipment_record;", "",
     "extern const ep_equipment_record ep_equipment[EP_EQUIPMENT];", "",
     "extern const uint16_t ep_model_offset[32];", f"extern const uint8_t ep_models[{len(models)}];",
     "extern const int16_t ep_sin1024[1024];", "",
     "#endif", ""]
open(os.path.join(ROOT, "core", "ep_tables.c"), "w").write("\n".join(c))
open(os.path.join(ROOT, "core", "ep_tables.h"), "w").write("\n".join(h))
print("wrote core/ep_tables.c, core/ep_tables.h")
