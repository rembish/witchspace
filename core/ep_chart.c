/* Elite Plus galaxy charts, reconstructed from ELITE.EXE (see ep_chart.h). */
#include "ep_chart.h"

#include "ep_galaxy.h"

#define SEL(off) (g->cmdr.b[EP_CMDR_SELECTED + (off)])

static uint8_t seed_x(const ep_seed *s) { return (uint8_t)(s->w[1] >> 8); }
static uint8_t seed_y(const ep_seed *s) { return (uint8_t)(s->w[0] >> 9); }

static int zoomed(const ep_game *g) { return g->cmdr.b[EP_CMDR_ZOOM] >= 1; }

static uint8_t centre(const ep_game *g, int k) { return g->cmdr.b[EP_CMDR_CHART_CENTRE + k]; }

/* (cursor - origin) * 2 / 7 (idiv) + centre, at least 0 */
static uint8_t unzoom(uint8_t c, uint8_t origin, uint8_t mid)
{
    int16_t a = (int16_t)((c - origin) * 2);
    int q = (int8_t)(a / 7) + mid;
    return (uint8_t)(q < 0 ? 0 : q);
}

uint16_t ep_cursor_position(const ep_game *g)
{
    uint8_t x = g->cmdr.b[EP_CMDR_CURSOR], y = g->cmdr.b[EP_CMDR_CURSOR + 1];
    if (g->cmdr.b[EP_CMDR_ZOOM] == 1) {
        x = unzoom(x, 0x50, centre(g, 0));
        y = unzoom(y, 0x40, centre(g, 1));
    }
    return (uint16_t)(y << 8 | x);
}

static unsigned absdiff(unsigned a, unsigned b) { return a > b ? a - b : b - a; }

/* 5dfe/5dc5: on the zoomed chart, only systems inside its window */
static int in_window(const ep_game *g, const ep_seed *s)
{
    if (!zoomed(g)) return 1;
    return absdiff(seed_x(s), centre(g, 0)) < 0x14 && absdiff(seed_y(s), centre(g, 1)) < 0x11;
}

/* 5e95: the cursor onto g->seed's system */
static void snap_cursor(ep_game *g)
{
    uint8_t *c = &g->cmdr.b[EP_CMDR_CURSOR];
    if (g->cmdr.b[EP_CMDR_ZOOM] != 1) {
        c[0] = seed_x(&g->seed);
        c[1] = seed_y(&g->seed);
        return;
    }
    for (int k = 0; k < 2; k++) {
        int16_t d = (int16_t)((k ? seed_y(&g->seed) : seed_x(&g->seed)) - centre(g, k));
        /* x 7/2 (as ep_cursor_position undoes it) around the window's middle, 50h, 40h */
        c[k] = (uint8_t)(3 * d + (d >> 1) + (k ? 0x40 : 0x50));
    }
}

void ep_find_nearest(ep_game *g)
{
    uint16_t at = ep_cursor_position(g);
    uint8_t x = (uint8_t)at, y = (uint8_t)(at >> 8);
    ep_seed s = ep_galaxy_seed(g->cmdr.b[EP_CMDR_GALAXY]);
    uint16_t best = 0xffff;
    uint8_t pick = 0; /* bp: stale if nothing qualified, which play never allows (re/FLIGHT.md) */
    for (unsigned n = 0; n < 256; n++) {
        unsigned dx = absdiff(seed_x(&s), x) & 0xff, dy = absdiff(seed_y(&s), y) & 0xff;
        uint32_t d = dx * dx + dy * dy;
        if (d <= 0xffff && d < best && in_window(g, &s)) {
            best = (uint16_t)d;
            pick = (uint8_t)n;
        }
        for (int k = 0; k < 4; k++) ep_twist(&s);
    }
    SEL(EP_SYSREC_INDEX) = pick;
    g->seed = ep_system_seed(g->cmdr.b[EP_CMDR_GALAXY], pick); /* 610e */
    snap_cursor(g);
}

void ep_system_distance(ep_game *g)
{
    unsigned dx = absdiff(seed_x(&g->seed), centre(g, 0)) & 0xff;
    unsigned dy = absdiff(seed_y(&g->seed), centre(g, 1)) & 0xff;
    /* the integer square root by subtracting odd numbers, 1 + 3 + 5 ... = n^2 */
    uint32_t sum = dx * dx + dy * dy, odd = 1;
    uint16_t root = 0;
    while (sum >= odd) {
        sum -= odd;
        odd += 2;
        root++;
    }
    uint16_t d = (uint16_t)(root << 2);
    SEL(EP_SYSREC_DIST) = (uint8_t)d;
    SEL(EP_SYSREC_DIST + 1) = (uint8_t)(d >> 8);
}

void ep_select_system(ep_game *g)
{
    ep_find_nearest(g);
    ep_system_distance(g);
    /* 6f91, 6fbc: five digits, up to three leading zeros blanked */
    uint16_t v = (uint16_t)(SEL(EP_SYSREC_DIST) | SEL(EP_SYSREC_DIST + 1) << 8);
    static const uint16_t pow10[4] = { 10000, 1000, 100, 10 };
    for (int k = 0; k < 4; k++) {
        g->dist_text[k] = (uint8_t)('0' + v / pow10[k]);
        v %= pow10[k];
    }
    g->dist_text[4] = (uint8_t)('0' + v);
    for (int k = 0; k < 3 && g->dist_text[k] < '1'; k++) g->dist_text[k] = ' ';
    /* 5f00: the system's data */
    ep_system sys;
    ep_system_data(&g->seed, &sys);
    SEL(EP_SYSREC_GOVERNMENT) = sys.government;
    SEL(EP_SYSREC_ECONOMY) = sys.economy;
    SEL(EP_SYSREC_TECH) = sys.tech;
    SEL(0x10) = sys.population;
    SEL(0x11) = sys.species[0];
    if (g->seed.w[2] & 0x80) /* aliens: all four (humans leave the rest as it was) */
        for (int k = 1; k < 4; k++) SEL(0x11 + k) = sys.species[k];
    const uint16_t w[4] = { sys.productivity, sys.radius, sys.desc_seed[0], sys.desc_seed[1] };
    for (int k = 0; k < 4; k++) {
        SEL(0x15 + 2 * k) = (uint8_t)w[k];
        SEL(0x16 + 2 * k) = (uint8_t)(w[k] >> 8);
    }
    ep_planet_name(&g->seed, &SEL(EP_SYSREC_NAME)); /* 6130 */
}
