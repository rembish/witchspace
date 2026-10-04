/* The original's screen, drawn from the core's output (see screen.h): the witchspace frontend's
 * software renderer. Pixels go through the game's colour tables, so this draws only the MCGA
 * mode. */
#include "screen.h"

#include "ep_circle.h"
#include "ep_tables.h"
#include "grf.h"

#include <string.h>

uint8_t screen_px[SCREEN_H][SCREEN_W];

#define VIEW_X 8 /* the 3D view: 304 x 124 at 8, 9 */
#define VIEW_Y 9
#define VIEW_W 0x130
#define VIEW_H 0x7c

static uint8_t text_colour[256 + 0x15]; /* ds:20e9 (shadows from ds:20fe, 15h on) */
static uint16_t palette = 0x1144;       /* the DAC table loaded: its ds address (EP_EV_PALETTE) */
static uint8_t kept_box[0x75][0x112], kept_line[9][0x130]; /* EP_EV_KEEP's arg 1, arg 2 */

void screen_init(void)
{
    memcpy(text_colour, &ep_ds_initial[0x20e9], sizeof text_colour);
    memcpy(&text_colour[0x20f9 - 0x20e9], &ep_ds_initial[0x2113], 5); /* 38a6: MCGA's own */
    memcpy(&text_colour[0x210e - 0x20e9], &ep_ds_initial[0x2118], 5);
    memset(screen_px, 0, sizeof screen_px);
}

/* ds:1ee9 for MCGA (from ds:1cf3); 3921 keeps changing colour 16h's (flashing) */
static uint8_t pixel_of(const ep_game *g, uint8_t colour)
{
    if (colour == 0x16) colour = (uint8_t)(0x86 + g->f.flash);
    return ep_mcga_colour[colour];
}

static void put(int x, int y, uint8_t c)
{
    if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H) screen_px[y][x] = c;
}

static void view_put(int x, int y, uint8_t c)
{
    if ((unsigned)x < VIEW_W && (unsigned)y < VIEW_H) screen_px[VIEW_Y + y][VIEW_X + x] = c;
}

static void view_line(int x0, int y0, int x1, int y1, uint8_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (int n = 0; n < 4096; n++) {
        view_put(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* a convex polygon in the view, filled row by row */
static void view_polygon(const int16_t *pt, int n, uint8_t c)
{
    int ymin = pt[1], ymax = pt[1];
    for (int k = 1; k < n; k++) {
        if (pt[2 * k + 1] < ymin) ymin = pt[2 * k + 1];
        if (pt[2 * k + 1] > ymax) ymax = pt[2 * k + 1];
    }
    if (ymin < 0) ymin = 0;
    if (ymax > VIEW_H - 1) ymax = VIEW_H - 1;
    for (int y = ymin; y <= ymax; y++) {
        int xl = 0x7fff, xr = -0x7fff;
        for (int k = 0; k < n; k++) {
            int x0 = pt[2 * k], y0 = pt[2 * k + 1], x1 = pt[2 * ((k + 1) % n)],
                y1 = pt[2 * ((k + 1) % n) + 1];
            if ((y < y0 && y < y1) || (y > y0 && y > y1)) continue;
            int x;
            if (y0 == y1) {
                if (x0 < xl) xl = x0;
                if (x1 < xl) xl = x1;
                if (x0 > xr) xr = x0;
                if (x1 > xr) xr = x1;
                continue;
            }
            x = x0 + (int)((long)(x1 - x0) * (y - y0) / (y1 - y0));
            if (x < xl) xl = x;
            if (x > xr) xr = x;
        }
        if (xl < 0) xl = 0;
        if (xr > VIEW_W - 1) xr = VIEW_W - 1;
        for (int x = xl; x <= xr; x++) screen_px[VIEW_Y + y][VIEW_X + x] = c;
    }
}

/* 3768: the MCGA blit */
static void sprite(int id, int x, int y)
{
    const grf_image *im = grf_get(id);
    if (!im) { /* no ELITE.GRF: a marker */
        for (int k = 0; k < 6; k++) {
            put(x + k, y, 0x0d);
            put(x, y + k, 0x0d);
        }
        return;
    }
    for (int r = 0; r < im->h; r++)
        for (int c = 0; c < im->w; c++) {
            uint8_t p = im->px[r * im->w + c];
            if (p || !im->transparent) put(x + c, y + r, p);
        }
}

/* 2e12: the glyphs at ds:0d40, 8 rows of bits and a width each */
static int glyph(int x, int y, uint8_t ch, uint8_t c)
{
    const uint8_t *gl = &ep_ds_initial[0x0d40 + (ch - 0x20) * 9];
    for (int r = 0; r < 8; r++) {
        uint8_t bits = gl[r];
        for (int k = 0; bits; k++, bits = (uint8_t)(bits << 1))
            if (bits & 0x80) put(x + k, y + r, c);
    }
    return (int8_t)gl[8];
}

/* 2e6d (and 2ec0 first, one down and right in the shadows' colours) */
static void text(const uint8_t *s, int len, int x, int y, uint8_t colour, int shadow)
{
    for (int pass = shadow ? 0 : 1; pass < 2; pass++) { /* pass 0: the shadow, colours from +15h */
        int off = pass ? 0 : 0x15, d = pass ? 0 : 1, px = x + d, py = y + d;
        uint8_t c = text_colour[colour + off];
        for (int i = 0; i < len && s[i];) {
            uint8_t ch = s[i];
            if (ch == 1 && i + 1 < len) {
                c = text_colour[s[i + 1] + off];
                i += 2;
            } else if (ch == 2 && i + 4 < len) {
                px = (int16_t)(s[i + 1] | s[i + 2] << 8) + d;
                py = (int16_t)(s[i + 3] | s[i + 4] << 8) + d;
                i += 5;
            } else {
                if (ch >= 0x20 && ch <= 0x7a) px += glyph(px, py, ch, c);
                i++;
            }
        }
    }
}

static void keep(int which, int back)
{
    int x = which == 1 ? 0x18 : 8, y = which == 1 ? 0x0c : 0, w = which == 1 ? 0x112 : 0x130,
        h = which == 1 ? 0x75 : 9;
    for (int r = 0; r < h; r++) {
        uint8_t *kept = which == 1 ? kept_box[r] : kept_line[r];
        if (back)
            memcpy(&screen_px[y + r][x], kept, (size_t)w);
        else
            memcpy(kept, &screen_px[y + r][x], (size_t)w);
    }
}

static void event(const ep_game *g, const ep_event *e)
{
    switch (e->kind) {
    case EP_EV_ICON: sprite(e->arg & 0xff, 0x10 + 0x18 * (e->arg >> 8), g->f.bar_colour); break;
    case EP_EV_KEEP: keep(e->arg, 0); break;
    case EP_EV_PUT_BACK: keep(e->arg, 1); break;
    case EP_EV_PALETTE: palette = e->arg; break;
    default: break;
    }
}

static void prim(const ep_game *g, const ep_prim *p)
{
    uint8_t c = pixel_of(g, p->colour);
    switch (p->kind) {
    case EP_PRIM_TRI: view_polygon(p->pt, 3, c); break;
    case EP_PRIM_QUAD: view_polygon(p->pt, 4, c); break;
    case EP_PRIM_LINE:
    case EP_PRIM_CLIPPED_LINE: view_line(p->pt[0], p->pt[1], p->pt[2], p->pt[3], c); break;
    case EP_PRIM_PIXEL: view_put(p->pt[0], p->pt[1], c); break;
    case EP_PRIM_SPANS:
        for (int k = (uint16_t)p->pt[0]; k < (uint16_t)p->pt[0] + p->pt[1] && k < g->circles.n; k++) {
            const ep_span *s = &g->circles.span[k];
            for (int x = s->x; x < s->x + s->w; x++) view_put(x, s->row, c);
        }
        break;
    case EP_PRIM_SPRITE: sprite(p->colour, p->pt[0], p->pt[1]); break;
    case EP_PRIM_BLIP: { /* 29f4: the stick from the scanner's plane, then a head two wide */
        uint8_t px = ep_ds_initial[0x265e + (p->colour & 0x1f)];
        int y = p->pt[1], dir = p->pt[2] < 0 ? -1 : 1;
        for (int k = 0; k < (p->pt[2] < 0 ? -p->pt[2] : p->pt[2]); k++, y += dir) put(p->pt[0], y, px);
        put(p->pt[0], y, px);
        put(p->pt[0] + 1, y, px);
        break;
    }
    case EP_PRIM_TEXT:
        text(&g->render.text[p->pt[2]], p->pt[3], p->pt[0], p->pt[1], p->colour, p->pt[4]);
        break;
    case EP_PRIM_RECT:
        for (int y = p->pt[1]; y < p->pt[1] + p->pt[3]; y++)
            for (int x = p->pt[0]; x < p->pt[0] + p->pt[2]; x++) put(x, y, c);
        break;
    default: break;
    }
}

/* the events in their places among the primitives (ep_event.at): a palette, an icon or the
 * screen kept or put back takes effect between the primitives it came between */
void screen_draw(const ep_game *g)
{
    int e = 0;
    for (int k = 0; k <= g->render.nprim; k++) {
        while (e < g->nevents && g->event[e].at <= k) event(g, &g->event[e++]);
        if (k < g->render.nprim) prim(g, &g->render.prim[k]);
    }
}

void screen_rgba(uint32_t *out)
{
    const uint8_t *dac = palette == 0x1144 ? ep_dac : &ep_ds_initial[palette];
    uint32_t rgb[256];
    for (int i = 0; i < 256; i++) {
        uint32_t r = dac[3 * i] * 255u / 63, gr = dac[3 * i + 1] * 255u / 63, b = dac[3 * i + 2] * 255u / 63;
        rgb[i] = 0xff000000u | r << 16 | gr << 8 | b;
    }
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++) out[y * SCREEN_W + x] = rgb[screen_px[y][x]];
}
