/* Elite Plus star dust, reconstructed from ELITE.EXE (see ep_dust.h). */
#include "ep_dust.h"

#include <string.h>

#include "ep_combat.h"
#include "ep_render.h"

enum { DUST = 30, X = 0, Y = 2, LIFE = 4, SPARE = 5, COLOUR = 6 };

static int16_t get(const uint8_t *p, int off) { return (int16_t)(p[off] | p[off + 1] << 8); }

static void put(uint8_t *p, int off, int16_t v)
{
    p[off] = (uint8_t)v;
    p[off + 1] = (uint8_t)((uint16_t)v >> 8);
}

/* 5307: integer parts within -20h..1fh (x), -10h..0fh (y) */
static int in_range(int16_t x, int16_t y)
{
    int8_t hx = (int8_t)((uint16_t)x >> 8), hy = (int8_t)((uint16_t)y >> 8);
    return hx >= -0x20 && hx <= 0x1f && hy >= -0x10 && hy <= 0x0f;
}

/* 531f: the inner box, where the rear view respawns */
static int in_core(int16_t x, int16_t y)
{
    int8_t hx = (int8_t)((uint16_t)x >> 8), hy = (int8_t)((uint16_t)y >> 8);
    return hx >= -6 && hx <= 6 && hy >= -3 && hy <= 3;
}

/* 5359: screen byte of a coordinate (only the low byte is used): bits 6..13 of v, by two
 * shifts that keep the carry */
static uint8_t project(int16_t v, uint8_t centre)
{
    uint16_t t = (uint16_t)((uint16_t)v << 1);
    uint8_t hi = (uint8_t)(t >> 8);
    uint8_t carry = (uint8_t)(t >> 7 & 1);
    return (uint8_t)((uint8_t)(hi << 1 | carry) + centre);
}

static int16_t sar(int16_t v, int n) { return (int16_t)(v >> n); }

/* 5350: the shadow position */
static void set_shadow(ep_game *g, int i, int16_t x, int16_t y)
{
    put(g->f.dust_old + 7 * i, X, x);
    put(g->f.dust_old + 7 * i, Y, y);
}

static void new_life(ep_game *g, int i, uint8_t r)
{
    uint8_t life = (uint8_t)((r & 0x1f) + 0x28);
    g->f.dust[7 * i + LIFE] = life;
    g->f.dust_old[7 * i + LIFE] = life;
    g->f.dust_old[7 * i + SPARE] = 1;
}

/* 52ce: a fresh particle anywhere */
static void respawn(ep_game *g, int i, int16_t *x, int16_t *y)
{
    uint16_t r = ep_flight_random(g);
    new_life(g, i, (uint8_t)r);
    g->f.dust[7 * i + COLOUR] = (uint8_t)(r >> 8 & 0x0f);
    r = ep_flight_random(g);
    uint16_t a = (uint16_t)sar((int16_t)r, 2);
    if ((a >> 8) == 0x20) a = (uint16_t)(a - 0x100);
    uint16_t b = (uint16_t)sar((int16_t)(uint16_t)(r << 8 | r >> 8), 3);
    if ((b >> 8) == 0x10) b = (uint16_t)(b - 0x100);
    *x = (int16_t)a;
    *y = (int16_t)b;
}

/* 51de: mask of the bits below the shift's magnitude */
static uint16_t spread(int16_t d)
{
    uint16_t m = 0;
    for (int16_t a = sar(d, 1); a != 0 && a != -1; a = sar(a, 1)) m = (uint16_t)(m << 1 | 1);
    return m;
}

/* 528e: re-enter at the top or bottom edge (the colour comes from the old y) */
static void respawn_y(ep_game *g, int i, int16_t d, uint16_t mask, int16_t *x, int16_t *y)
{
    uint16_t r = ep_flight_random(g);
    new_life(g, i, (uint8_t)r);
    g->f.dust[7 * i + COLOUR] = (uint8_t)((uint16_t)*y >> 8 & 0x0f);
    uint16_t a = (uint16_t)sar((int16_t)r, 2);
    if ((a >> 8) == 0x20) a = (uint16_t)(a - 0x100);
    uint16_t b = (uint16_t)(ep_flight_random(g) & mask);
    *y = (int16_t)(d >= 0 ? (uint16_t)(0xf000 + b) : (uint16_t)(0x0f00 - b));
    *x = (int16_t)a;
}

/* 5217: re-enter at the left or right edge */
static void respawn_x(ep_game *g, int i, int16_t d, uint16_t mask, int16_t *x, int16_t *y)
{
    uint16_t r = ep_flight_random(g);
    new_life(g, i, (uint8_t)r);
    uint16_t b = (uint16_t)sar((int16_t)r, 3);
    if ((b >> 8) == 0x10) b = (uint16_t)(b - 0x100);
    uint16_t a = (uint16_t)(ep_flight_random(g) & mask);
    *x = (int16_t)(d >= 0 ? (uint16_t)(a + 0xe000) : (uint16_t)(0x1f00 - a));
    *y = (int16_t)b;
}

/* 5265 (vertical) and 51f0 (horizontal): shift every particle in range */
static void shift(ep_game *g, int16_t d, int horizontal)
{
    uint16_t mask = spread(d);
    for (int i = 0; i < DUST; i++) {
        uint8_t *p = g->f.dust + 7 * i;
        int16_t x = get(p, X), y = get(p, Y);
        if (!in_range(x, y)) continue;
        if (horizontal)
            x = (int16_t)(uint16_t)((uint16_t)x + (uint16_t)d);
        else
            y = (int16_t)(uint16_t)((uint16_t)y + (uint16_t)d);
        if (!in_range(x, y)) {
            if (horizontal)
                respawn_x(g, i, d, mask, &x, &y);
            else
                respawn_y(g, i, d, mask, &x, &y);
            set_shadow(g, i, x, y);
        }
        put(p, X, x);
        put(p, Y, y);
    }
}

/* 6d19 + 524c: roll every particle (no range test) */
static void roll(ep_game *g, uint16_t angle)
{
    g->space.rot[3] = ep_rot_from_angle(angle);
    for (int i = 0; i < DUST; i++) {
        uint8_t *p = g->f.dust + 7 * i;
        int16_t x = get(p, X), y = get(p, Y);
        ep_rotate_pair(&g->space.rot[3], &x, &y);
        put(p, X, x);
        put(p, Y, y);
    }
}

/* af5e/af49: the invert options on the steering word */
static uint16_t invert(const ep_flight *f, uint16_t s)
{
    uint8_t lo = (uint8_t)s, hi = (uint8_t)(s >> 8);
    if (f->opt_invert_pitch == 1) hi = (uint8_t)-hi;
    if (f->opt_invert_both == 1) {
        lo = (uint8_t)-lo;
        hi = (uint8_t)-hi;
    }
    return (uint16_t)(hi << 8 | lo);
}

/* the byte stretched by 5/4 over the 304-pixel view */
static int16_t screen_x(uint8_t b) { return (int16_t)(b + (b >> 2) - 8); }

/* 2973 (a pixel) or 4f6d (a streak from the shadow position in jump mode) */
static void draw(ep_game *g, int i, int16_t x, int16_t y)
{
    uint8_t px = project(x, 0x80), py = project(y, 0x3e);
    if (g->f.jump_speed) {
        const uint8_t *s = g->f.dust_old + 7 * i;
        if (s[SPARE]) return;
        int16_t sx = get(s, X), sy = get(s, Y);
        if (!in_range(sx, sy)) return;
        uint8_t qx = project(sx, 0x80), qy = project(sy, 0x3e);
        if (qy < 0x7c && py < 0x7c) ep_render_line(&g->render, 7, screen_x(qx), qy, screen_x(px), py);
        return;
    }
    int16_t sx = screen_x(px);
    if ((uint16_t)sx >= 0x130 || py >= 0x7c) return;
    ep_render_pixel(&g->render, (uint8_t)(g->f.dust[7 * i + COLOUR] & 0x0f), sx, py);
}

/* 51ac: how far a particle moves per frame (the larger, the slower) */
static uint8_t step_shift(const ep_flight *f)
{
    uint8_t s = (uint8_t)((uint16_t)(0x34 - f->speed) / 12 + 4);
    if (f->jump_speed) s--;
    return s;
}

static void fly(ep_game *g, int rear)
{
    ep_flight *f = &g->f;
    f->dust_shift = step_shift(f);
    int n = f->dust_shift;
    for (int i = 0; i < DUST; i++) {
        uint8_t *p = f->dust + 7 * i;
        int16_t x = get(p, X), y = get(p, Y);
        if (!in_range(x, y)) continue;
        if (f->speed) {
            int16_t dx = sar(x, n), dy = sar(y, n);
            x = (int16_t)(uint16_t)(rear ? (uint16_t)x - (uint16_t)dx : (uint16_t)x + (uint16_t)dx);
            y = (int16_t)(uint16_t)(rear ? (uint16_t)y - (uint16_t)dy : (uint16_t)y + (uint16_t)dy);
        }
        for (;;) {
            put(p, X, x);
            put(p, Y, y);
            if (rear ? !in_core(x, y) && --p[LIFE] != 0 : in_range(x, y)) break;
            respawn(g, i, &x, &y);
            set_shadow(g, i, x, y);
        }
        draw(g, i, x, y);
    }
}

void ep_dust_frame(ep_game *g)
{
    ep_flight *f = &g->f;
    if (f->jump_speed) memcpy(f->dust_old, f->dust, sizeof f->dust); /* 53bf */
    uint16_t view = g->space.extra_angle;
    uint16_t pitch = invert(f, f->steer), turn = invert(f, f->steer);
    /* side views: the dust drifts sideways; seen from the side the ship's roll moves it up or
     * down, and its pitch turns it */
    if (view == 0x600 || view == 0x200) {
        int right = view == 0x600;
        uint8_t v = (uint8_t)pitch;
        if (v) shift(g, (int16_t)((int16_t)(right ? -(int16_t)(v << 8) : (int16_t)(v << 8)) >> 1), 0);
        uint16_t sp = f->speed;
        if (sp) shift(g, (int16_t)(uint16_t)((uint8_t)(right ? sp : -sp) << 8) >> 3, 1);
        uint16_t a = (uint16_t)((int16_t)(int8_t)(turn >> 8) *
                                2); /* times two: a shift of a negative is undefined in C */
        if (a) roll(g, right ? a : (uint16_t)-a);
        for (int i = 0; i < DUST; i++) {
            const uint8_t *p = f->dust + 7 * i;
            int16_t x = get(p, X), y = get(p, Y);
            if (in_range(x, y)) draw(g, i, x, y);
        }
        return;
    }
    int rear = view == 0x400;
    if (pitch >> 8) {
        uint16_t d = (uint16_t)(pitch & 0xff00);
        if (!rear) d = (uint16_t)-d;
        shift(g, sar((int16_t)d, 1), 0);
    }
    uint16_t a = (uint16_t)((int16_t)(int8_t)turn * 2);
    if (!rear) a = (uint16_t)-a;
    if (a) roll(g, a);
    fly(g, rear);
}

void ep_dust_reset(ep_game *g)
{
    ep_flight *f = &g->f;
    if (!(f->message_time & 0xff)) f->message_time = 0;
    for (int i = 0; i < DUST; i++) {
        uint8_t *p = f->dust + 7 * i;
        for (int k = X; k <= Y; k += 2) {
            int16_t v;
            do v = sar((int16_t)ep_flight_random(g), 1);
            while ((int8_t)((uint16_t)v >> 8) < -0x23 || (int8_t)((uint16_t)v >> 8) > 0x23);
            put(p, k, v);
        }
        uint16_t r = ep_flight_random(g);
        p[LIFE] = (uint8_t)((r & 0x1f) + 0x28);
        p[COLOUR] = (uint8_t)(r >> 8 & 0x0f);
    }
}
