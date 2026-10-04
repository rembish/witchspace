/* The original's tables, read from ELITE.EXE and ELITE.GRF (see ep_tables.h). The places
 * are those re/tools/gen_tables.py reads (and checks this against); see re/NOTES.md. */
#include "ep_tables.h"

#include <stdlib.h>
#include <string.h>

uint16_t ep_galaxy_seeds[9][3];
char ep_digrams[65];
const char *ep_desc_template;
const char *ep_desc_ian;
const char *ep_desc_tokens[EP_DESC_TOKENS][5];
const char *ep_goods_names[EP_GOODS];
uint16_t ep_goods_eco_factor[EP_GOODS][8];
uint16_t ep_goods_gov_factor[EP_GOODS][8];
uint16_t ep_goods_base_price[EP_GOODS];
int8_t ep_goods_tech_adj[EP_GOODS][3];
uint16_t ep_market_rng0[3];
ep_equipment_record ep_equipment[EP_EQUIPMENT];
uint16_t ep_model_offset[32];
uint8_t ep_models[EP_MODELS_SIZE];
int16_t ep_sin1024[1024];
int16_t ep_sin2048[2048];
uint8_t ep_title_ships[EP_TITLE_SHIPS];
uint16_t ep_title_min_dist[32];
const char *ep_ship_names[30];
uint8_t ep_ring_start[30];
uint8_t ep_glyph_width[0x5b];
uint8_t ep_sprite_width[EP_SPRITES];
uint8_t ep_ds_initial[0x10000];
uint8_t ep_drv_initial[EP_DRV_SIZE];
uint8_t ep_key_rows[6][12];
uint8_t ep_icon_sprite[37];
uint8_t ep_bar_colour[3];
uint8_t ep_ship_text[EP_SHIP_TEXT_SIZE];
uint8_t ep_mcga_colour[256];
uint8_t ep_dac[768];
uint16_t ep_tan256[256];
uint16_t ep_hit_size[32];
uint16_t ep_crash_radius[32];
uint8_t ep_spawn[31][10];
uint8_t ep_spawn_limit[8][4];
uint16_t ep_spawn_chance[8][4];
uint8_t ep_commander0[EP_COMMANDER_SIZE];

/* the release this was reconstructed from (V3.1): its load image (unpacked), FNV-1a. Copies
 * differ only in four bytes of code at 0000:149a, the copy protection's comparison, patched
 * out with nops (3013df64) or with xchg bp,bp; cmc; cmc (6ae033fe, the Internet Archive's);
 * no table is there. */
#define IMAGE_SIZE 153360
static const uint32_t known[] = { 0x3013df64u, 0x6ae033feu };
#define DS  0xb000  /* segment 0b00 */
#define SS  0x1c0c0 /* segment 1c0c: the ships' models */
#define DRV 0x22700 /* segment 2270: the music driver */

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* EXEPACK (as re/tools/unexepack.py): the image decompressed backwards from its end, by fill
 * (b0) and copy (b2) commands, bit 0 the last; the stub at CS:0 gives the unpacked size */
static int unexepack(const uint8_t *img, size_t len, uint16_t cs, uint8_t *out, size_t out_len)
{
    if ((size_t)cs * 16 + 16 > len) return 0;
    const uint8_t *stub = img + (size_t)cs * 16;
    if (stub[14] != 'R' || stub[15] != 'B') return 0;
    size_t dest = (size_t)le16(stub + 12) * 16, src_len = (size_t)cs * 16;
    if (dest != out_len || src_len > out_len) return 0;
    memset(out, 0, out_len);
    memcpy(out, img, src_len);
    size_t si = src_len - 1, di = dest - 1;
    while (si > 0 && img[si] == 0xff) si--; /* padding to a paragraph */
    for (;;) {
        if (si < 3) return 0;
        uint8_t cmd = img[si];
        size_t n = (size_t)(img[si - 2] | img[si - 1] << 8);
        si -= 3;
        if ((cmd & 0xfe) == 0xb0) {
            uint8_t v = img[si--];
            if (n > di + 1) return 0;
            while (n--) out[di--] = v;
        } else if ((cmd & 0xfe) == 0xb2) {
            if (n > di + 1 || n > si + 1) return 0;
            while (n--) out[di--] = img[si--];
        } else {
            return 0;
        }
        if (cmd & 1) return 1;
    }
}

static void words(uint16_t *to, const uint8_t *from, size_t n)
{
    for (size_t k = 0; k < n; k++) to[k] = le16(from + 2 * k);
}

/* a ship model's length: vertices, then face groups and primitives up to the end mark */
static size_t model_len(const uint8_t *m)
{
    size_t q = 1 + 6 * (size_t)m[0];
    for (;;) {
        uint8_t b = m[q];
        if (b == 1)
            q += 9;
        else if (b & 1)
            return q + 1;
        else
            q += b == 0 ? 8 : b == 2 ? 10 : 6;
    }
}

static int fill(const uint8_t *img)
{
    const uint8_t *ds = img + DS;
    memcpy(ep_ds_initial, ds, 0x10000);
    memset(ep_ds_initial + EP_DS_INITIAL, 0, 0x10000 - EP_DS_INITIAL);
    memcpy(ep_drv_initial, img + DRV, EP_DRV_SIZE);
    const char *text = (const char *)ep_ds_initial; /* strings point into the data segment */

    words(&ep_galaxy_seeds[0][0], ds + 0x5509, 27);
    memcpy(ep_digrams, ds + 0x5585, 64);
    ep_digrams[64] = 0;
    ep_desc_template = text + 0x5a35;
    ep_desc_ian = text + 0x63ff;
    for (int t = 0; t < EP_DESC_TOKENS; t++) {
        uint16_t p = le16(ds + 0x5b4a + 2 * t);
        for (int k = 0; k < 5; k++) ep_desc_tokens[t][k] = text + le16(ds + p + 2 * k);
    }
    for (int k = 0; k < EP_GOODS; k++) {
        ep_goods_names[k] = text + 0x8f2d + 17 * k;
        words(ep_goods_eco_factor[k], ds + 0x905f + 16 * k, 8);
        words(ep_goods_gov_factor[k], ds + 0x916f + 16 * k, 8);
        for (int j = 0; j < 3; j++) ep_goods_tech_adj[k][j] = (int8_t)ds[0x92a1 + 3 * k + j];
    }
    words(ep_goods_base_price, ds + 0x927f, EP_GOODS);
    words(ep_market_rng0, ds + 0x92e0, 3);
    uint16_t p = 0x8bef; /* {min tech, name, i8 government factor, i8 economy factor, base} */
    for (int k = 0; k < EP_EQUIPMENT; k++) {
        ep_equipment_record *e = &ep_equipment[k];
        e->min_tech = ds[p];
        e->name = text + p + 1;
        uint16_t q = (uint16_t)(p + 2 + strlen(e->name));
        e->gov_factor = (int8_t)ds[q];
        e->eco_factor = (int8_t)ds[q + 1];
        e->base_price = le16(ds + q + 2);
        p = (uint16_t)(q + 4);
    }
    size_t at = 0;
    for (int k = 0; k < 32; k++) {
        uint16_t m = le16(img + SS + 0x65bc + 2 * k);
        if (!m) {
            ep_model_offset[k] = 0xffff;
            continue;
        }
        size_t n = model_len(img + SS + m);
        if (at + n > EP_MODELS_SIZE) return 0;
        ep_model_offset[k] = (uint16_t)at;
        memcpy(ep_models + at, img + SS + m, n);
        at += n;
    }
    if (at != EP_MODELS_SIZE) return 0;
    words((uint16_t *)ep_sin1024, ds + 0x2cc0, 1024);
    words((uint16_t *)ep_sin2048, ds + 0x6410, 2048);
    for (int k = 0; k < EP_TITLE_SHIPS; k++) ep_title_ships[k] = ds[0xb263 + k];
    if (ds[0xb263 + EP_TITLE_SHIPS] != 0xff) return 0;
    words(ep_title_min_dist, ds + 0xb1bc, 32);
    for (int k = 0; k < 30; k++) ep_ship_names[k] = text + le16(ds + 0xb27c + 2 * k);
    memcpy(ep_key_rows, ds + 0x31f, sizeof ep_key_rows);
    memcpy(ep_icon_sprite, ds + 0x374, sizeof ep_icon_sprite);
    memcpy(ep_bar_colour, ds + 0x2fb, sizeof ep_bar_colour);
    for (int k = 0; k < 0x5b; k++) ep_glyph_width[k] = ds[0xd48 + 9 * k];
    memcpy(ep_ring_start, ds + 0x85be, sizeof ep_ring_start);
    memcpy(ep_ship_text, ds + 0x80a9, sizeof ep_ship_text);
    for (int k = 0; k < 256; k++) ep_mcga_colour[k] = ds[0x1cf3 + 2 * k];
    memcpy(ep_dac, ds + 0x1144, sizeof ep_dac);
    if (le16(ds + 0x82d7) != EP_COMMANDER_SIZE) return 0;
    memcpy(ep_commander0, ds + 0x82db, EP_COMMANDER_SIZE);
    words(ep_tan256, ds + 0x7410, 256);
    for (int k = 0; k < 32; k++) ep_hit_size[k] = (uint16_t)(ds[0xb0e5 + 2 * k] << 8 | ds[0xb0e6 + 2 * k]);
    words(ep_crash_radius, ds + 0x7614, 32);
    memcpy(ep_spawn, ds + 0x8739, sizeof ep_spawn);
    memcpy(ep_spawn_limit, ds + 0x886f, sizeof ep_spawn_limit);
    words(&ep_spawn_chance[0][0], ds + 0x8899, 32);
    return 1;
}

int ep_data_load(const uint8_t *exe, size_t len)
{
    if (len < 28 || exe[0] != 'M' || exe[1] != 'Z') return EP_DATA_NOT_EXE;
    size_t last = le16(exe + 2), pages = le16(exe + 4), header = (size_t)le16(exe + 8) * 16;
    size_t size = last ? (pages - 1) * 512 + last : pages * 512;
    if (!pages || size > len || header > size) return EP_DATA_NOT_EXE;
    const uint8_t *img = exe + header;
    size_t img_len = size - header;
    uint8_t *buf = NULL;
    if (img_len != IMAGE_SIZE) { /* EXEPACK: its stub at CS:0, entered at 10h */
        if (le16(exe + 0x14) != 0x10) return EP_DATA_VERSION;
        buf = malloc(IMAGE_SIZE);
        if (!buf) return EP_DATA_BAD_PACKING;
        if (!unexepack(img, img_len, le16(exe + 0x16), buf, IMAGE_SIZE)) {
            free(buf);
            size_t sig = (size_t)le16(exe + 0x16) * 16 + 14; /* EXEPACK's, but not this release's */
            return sig + 1 < img_len && img[sig] == 'R' && img[sig + 1] == 'B' ? EP_DATA_BAD_PACKING
                                                                               : EP_DATA_VERSION;
        }
        img = buf;
    }
    uint32_t h = 0x811c9dc5u;
    for (size_t k = 0; k < IMAGE_SIZE; k++) h = (h ^ img[k]) * 0x01000193u;
    int ok = 0;
    for (size_t k = 0; k < sizeof known / sizeof known[0]; k++)
        if (h == known[k]) ok = fill(img);
    free(buf);
    return ok ? EP_DATA_OK : EP_DATA_VERSION;
}

const char *ep_data_error(int code)
{
    switch (code) {
    case EP_DATA_OK: return "loaded";
    case EP_DATA_NOT_EXE: return "not a DOS executable (or cut short)";
    case EP_DATA_BAD_PACKING: return "its EXEPACK data does not unpack";
    default: return "not the Elite Plus release this was reconstructed from";
    }
}

/* ELITE.GRF: the MCGA set (the header's second entry: paragraphs, offset, count); each
 * picture a width word (bit 15: transparent), a height, then its pixels run-length packed */
int ep_data_grf(const uint8_t *grf, size_t len)
{
    if (len < 16) return 1;
    size_t p = (size_t)(grf[10] | grf[11] << 8 | grf[12] << 16 | (uint32_t)grf[13] << 24);
    int count = grf[14] | grf[15] << 8;
    if (count > EP_SPRITES) count = EP_SPRITES;
    for (int i = 0; i < count; i++) {
        if (p + 3 > len) return 1;
        size_t w = (size_t)(grf[p] | grf[p + 1] << 8) & 0x7fff, h = grf[p + 2];
        ep_sprite_width[i] = (uint8_t)w;
        p += 3;
        for (size_t k = 0; k < w * h;) { /* 33a6: n + 1 bytes as they are, or a byte 1 - n times */
            if (p >= len) return 1;
            int c = grf[p++];
            if (c < 0x80) {
                k += (size_t)c + 1;
                p += (size_t)c + 1;
            } else {
                k += (size_t)(1 - (c - 256));
                p++;
            }
        }
    }
    return 0;
}

uint8_t ep_ds_static(uint16_t addr)
{
    if ((addr >= 0x0300 && addr < 0x0b00) || (addr >= 0x2600 && addr < 0x2d00) ||
        (addr >= 0x54e2 && addr < 0xb400))
        return ep_ds_initial[addr];
    return 0;
}
