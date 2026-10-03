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
code(0x6D2F, bytes([0x8B, 0x87, 0x10, 0x64]))
sin2048 = [v - 65536 if v > 32767 else v for v in words(0x6410, 2048)]
sin1024 = [v - 65536 if v > 32767 else v for v in words(0x2CC0, 1024)]
# title_loop (9e80): ships shown in turn (ds:b263, ff-terminated), closest distance by type
# (ds:b1bc), names by type (ds:b27c)
code(0x9F59, bytes([0x8B, 0x9F, 0xBC, 0xB1]))
code(0x9F99, bytes([0xBE, 0x63, 0xB2]))
code(0x9FCB, bytes([0x8B, 0xB7, 0x7C, 0xB2]))
title_ships = []
p = 0xB263
while ds(p, 1)[0] != 0xFF:
    title_ships.append(ds(p, 1)[0])
    p += 1
title_min_dist = words(0xB1BC, 32)
ship_names = [cstr(w) for w in words(0xB27C, 30)]
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
     "/* ds:6410: sine for the player's rotation slots, 2048 steps per turn, Q15 */",
     "const int16_t ep_sin2048[2048] = {",
     ",\n".join("    " + ", ".join(str(v) for v in sin2048[i:i + 12]) for i in range(0, 2048, 12)),
     "};",
     "/* ds:b263: ship types the title shows in turn */",
     f"const uint8_t ep_title_ships[{len(title_ships)}] = {{ " + ", ".join(map(str, title_ships)) + " };",
     "/* ds:b1bc: closest distance of each type on the title */",
     "const uint16_t ep_title_min_dist[32] = {",
     ",\n".join("    " + ", ".join(map(str, title_min_dist[i:i + 8])) for i in range(0, 32, 8)), "};",
     "/* ds:b27c: ship names by type */",
     "const char *const ep_ship_names[30] = {", ",\n".join("    " + clit(n) for n in ship_names), "};",
     "/* ds:1cf3: MCGA pixel value of each game colour */",
     "const uint8_t ep_mcga_colour[256] = {",
     ",\n".join("    " + ", ".join(map(str, mcga_map[i:i + 16])) for i in range(0, 256, 16)), "};",
     "/* ds:1144: VGA/MCGA DAC, 6 bits per component */",
     "const uint8_t ep_dac[768] = {",
     ",\n".join("    " + ", ".join(map(str, dac[i:i + 24])) for i in range(0, 768, 24)), "};",
     "/* ds:82db: the commander block (save file) at start-up: Jameson at Lave */",
     f"const uint8_t ep_commander0[{commander_len}] = {{",
     ",\n".join("    " + ", ".join(f"{b:#04x}" for b in commander0[i:i + 12]) for i in range(0, commander_len, 12)),
     "};",
     "/* ds:7410: tangents searched by the arctangent (6e8a), Q15 */",
     "const uint16_t ep_tan256[256] = {",
     ",\n".join("    " + ", ".join(map(str, tan256[i:i + 12])) for i in range(0, 256, 12)), "};",
     "/* ds:b0e5: laser hit box by type (the word with its bytes swapped, as abd1 uses it) */",
     "const uint16_t ep_hit_size[32] = {",
     ",\n".join("    " + ", ".join(f"{v:#06x}" for v in hit_size[i:i + 8]) for i in range(0, 32, 8)), "};",
     "/* ds:7614: collision radius by type */",
     "const uint16_t ep_crash_radius[32] = {",
     ",\n".join("    " + ", ".join(map(str, crash_radius[i:i + 8])) for i in range(0, 32, 8)), "};",
     "/* ds:8739: ship spawn table {type, speed, turn, bounty, missiles, canisters, debris, energy, +3f, +1c} */",
     "const uint8_t ep_spawn[31][10] = {", rows(spawn), "};",
     "/* ds:886f: most ships of a kind by government {junk, traders, loners, pirates} */",
     "const uint8_t ep_spawn_limit[8][4] = {", rows(spawn_limit), "};",
     "/* ds:8899: chance (out of 65536) of a new ship of a kind each frame, by government */",
     "const uint16_t ep_spawn_chance[8][4] = {", rows(spawn_chance), "};",
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
     "extern const int16_t ep_sin1024[1024];", "extern const int16_t ep_sin2048[2048];", "",
     f"#define EP_TITLE_SHIPS {len(title_ships)}", "",
     "extern const uint8_t ep_title_ships[EP_TITLE_SHIPS];", "extern const uint16_t ep_title_min_dist[32];",
     "extern const char *const ep_ship_names[30];", "",
     "extern const uint8_t ep_mcga_colour[256];", "extern const uint8_t ep_dac[768];", "",
     "extern const uint16_t ep_tan256[256];", "extern const uint16_t ep_hit_size[32];",
     "extern const uint16_t ep_crash_radius[32];",
     "extern const uint8_t ep_spawn[31][10];", "extern const uint8_t ep_spawn_limit[8][4];",
     "extern const uint16_t ep_spawn_chance[8][4];", "",
     f"#define EP_COMMANDER_SIZE {commander_len}", "",
     "extern const uint8_t ep_commander0[EP_COMMANDER_SIZE];", "",
     "#endif", ""]
open(os.path.join(ROOT, "core", "ep_tables.c"), "w").write("\n".join(c))
open(os.path.join(ROOT, "core", "ep_tables.h"), "w").write("\n".join(h))
print("wrote core/ep_tables.c, core/ep_tables.h")
