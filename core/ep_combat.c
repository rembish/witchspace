/* Elite Plus laser hits, reconstructed from ELITE.EXE (see ep_combat.h). */
#include "ep_combat.h"

#include "ep_ships.h"
#include "ep_tables.h"

#include <string.h>

static uint16_t get16(const ep_object *o, int off) { return (uint16_t)(o->b[off] | o->b[off + 1] << 8); }

static int type_of(const ep_object *o) { return (o->b[EP_OBJ_FLAGS] >> 1) & 0x1f; }

/* 43b2: a space station (types 0 and 1) */
static int is_station(const ep_object *o) { return type_of(o) <= 1; }

uint16_t ep_flight_random(ep_game *g)
{
    uint8_t *p = g->cmdr.b + EP_CMDR_SEED;
    uint16_t w[3];
    for (int k = 0; k < 3; k++) w[k] = (uint16_t)(p[2 * k] | p[2 * k + 1] << 8);
    uint16_t ret = (uint16_t)(w[0] + w[1]);
    uint16_t n[3] = { w[1], w[2], (uint16_t)(w[2] + ret) };
    for (int k = 0; k < 3; k++) {
        p[2 * k] = (uint8_t)n[k];
        p[2 * k + 1] = (uint8_t)(n[k] >> 8);
    }
    return ret;
}

/* (|v| << 8 | stray high byte) / z as abd1 divides: the dividend is built with cwd from
 * |v|, so v = 8000h gives a negative high word; 0 on a divide error */
static int scaled(int16_t v, uint16_t z, uint16_t *q)
{
    uint16_t a = (uint16_t)v;
    if (a & 0x8000) a = (uint16_t)(0u - a);
    uint16_t dx = (a & 0x8000) ? 0xffff : 0;
    dx = (uint16_t)((dx & 0xff00) | (a >> 8));
    uint32_t num = (uint32_t)dx << 16 | (uint32_t)(a & 0xff) << 8;
    if (!z || num / z > 0xffff) return 0;
    *q = (uint16_t)(num / z);
    return 1;
}

/* abd1: the ship in the crosshair: in view, not hidden, its hit box around the centre of
 * the view, the nearest of them; -1 if none */
static int laser_target(ep_game *g)
{
    int best = -1;
    uint16_t near = 0xffff;
    int n = (uint8_t)(g->space.ship_slots - 2);
    for (int i = 2; i < 2 + n && i < EP_OBJECTS; i++) {
        const ep_object *o = &g->space.obj[i];
        if ((o->b[EP_OBJ_FLAGS] & 0x81) != 0x81) continue;
        if ((o->b[EP_OBJ_FLAGS1E] & 0x60) == 0x60) continue;
        uint16_t z = get16(o, EP_OBJ_CAM + 4);
        if (!z) continue; /* divide error: next slot */
        uint32_t box = ep_hit_size[(o->b[EP_OBJ_FLAGS] & 0x3e) >> 1] / z;
        uint16_t lim = (uint16_t)(box + 2), q;
        if (!scaled((int16_t)get16(o, EP_OBJ_CAM), z, &q) || q >= lim) continue;
        if (!scaled((int16_t)get16(o, EP_OBJ_CAM + 2), z, &q) || q >= lim) continue;
        if (z >= near) continue;
        near = z;
        best = i;
    }
    return best;
}

/* 53e2: the beam: two lines from the bottom of the view to the centre, jittered by the
 * flight generator, in colours that cycle per laser type */
static void beam(ep_game *g)
{
    ep_flight *f = &g->f;
    uint16_t r = ep_flight_random(g);
    int16_t ex = (int16_t)(0x96 + (r & 3)), ey = (int16_t)(0x3c + ((r >> 8) & 3));
    uint8_t type = f->laser_fired;
    int16_t from[2];
    uint8_t colour;
    if (type & 1) {
        if (type & 2) { /* military laser: two pairs */
            colour = (uint8_t)(f->beam_pair + 0xaa);
            f->beam_pair ^= 2;
            ep_render_line(&g->render, colour, 0x46, 0x7b, ex, ey);
            ep_render_line(&g->render, colour, 0xe9, 0x7b, ex, ey);
            colour = (uint8_t)((((uint8_t)(colour - 0xaa)) ^ 2) + 0xaa);
        } else {
            uint8_t c = f->beam_colour;
            do c = (uint8_t)((c + 1) & 3);
            while (!c);
            f->beam_colour = c;
            colour = (uint8_t)(c + 0xaa);
        }
        ep_render_line(&g->render, colour, 0x32, 0x7b, ex, ey);
        ep_render_line(&g->render, colour, 0xfd, 0x7b, ex, ey);
        return;
    }
    uint8_t c = f->beam_colour;
    if (type & 2) {
        do c = (uint8_t)((c + 1) & 3);
        while (!c);
    } else {
        do c = (uint8_t)((c - 1) & 3);
        while (!c);
    }
    f->beam_colour = c;
    colour = (uint8_t)(c + 0xaa);
    if ((f->beam_flip ^= 1) == 0) {
        from[0] = 0xfd;
        from[1] = 0xe9;
    } else {
        from[0] = 0x32;
        from[1] = 0x46;
    }
    ep_render_line(&g->render, colour, from[0], 0x7b, ex, ey);
    ep_render_line(&g->render, colour, from[1], 0x7b, ex, ey);
}

/* 6f91 + 6fbc: five digits of v at p, leading zeros (up to `blank`) as spaces */
static void digits5(char *p, uint16_t v, int blank)
{
    static const uint16_t div[4] = { 10000, 1000, 100, 10 }; /* ds:8016 */
    for (int k = 0; k < 4; k++) {
        p[k] = (char)('0' + v / div[k]);
        v = (uint16_t)(v % div[k]);
    }
    p[4] = (char)('0' + v);
    for (int k = 0; k < blank && p[k] < '1'; k++) p[k] = ' ';
}

void ep_cash_text(ep_commander *c)
{
    static const uint32_t div[9] = { 1000000000u, 100000000u, 10000000u, 1000000u, 100000u,
                                     10000u,      1000u,      100u,      10u }; /* ds:7fe2 */
    char *p = (char *)c->b + EP_CMDR_CASH_TEXT;
    uint32_t v = ep_commander_cash(c);
    for (int k = 0; k < 9; k++) {
        p[k] = (char)('0' + v / div[k]); /* the original counts subtractions: same digit */
        v %= div[k];
    }
    p[9] = (char)('0' + v);
    for (int k = 0; k < 8 && p[k] == '0'; k++) p[k] = ' ';
    p[10] = p[9];
    p[9] = '.';
}

static void add_cash(ep_game *g, uint32_t tenths)
{
    ep_commander_set_cash(&g->cmdr, ep_commander_cash(&g->cmdr) + tenths);
    ep_cash_text(&g->cmdr);
}

static void add_legal(ep_game *g, unsigned v)
{
    unsigned l = ep_commander_b(&g->cmdr, EP_CMDR_LEGAL) + v;
    ep_commander_set_b(&g->cmdr, EP_CMDR_LEGAL, (uint8_t)(l > 0xff ? 0xff : l));
}

static void message(ep_game *g, uint16_t text, uint16_t time)
{
    g->f.message = text;
    g->f.message_time = time;
}

static void add_kill(ep_game *g)
{
    uint8_t *k = g->cmdr.b + EP_CMDR_KILLS;
    uint16_t v = (uint16_t)(k[0] | k[1] << 8);
    v++;
    k[0] = (uint8_t)v;
    k[1] = (uint8_t)(v >> 8);
}

/* ad3d: shooting the station switches the docking computer off */
static void autopilot_off(ep_game *g)
{
    if (g->f.autopilot == 1) {
        g->f.autopilot = 0;
        g->f.ap_flag = 0;
    }
}

/* 7092: "BOUNTY: nnn.n Cr" as the message */
static void bounty_message(ep_game *g, uint16_t v)
{
    char *t = g->f.bounty_text;
    digits5(t + 7, v, 3);
    t[12] = t[11];
    t[11] = '.';
    message(g, 0x805c, 0x23);
}

/* ad4f: what killing this ship earns (or costs) */
void ep_kill_reward(ep_game *g, ep_object *o)
{
    ep_flight *f = &g->f;
    if (f->mission == 4 && f->mission_state == 2 && o->b[0x25] == 1) {
        f->mission_state = 3;
        add_kill(g);
        message(g, 0xaed8, 0x23);
        return;
    }
    if (f->mission == 6 && f->mission_state > 1 && o->b[0x25] == 2) f->mission_state--;
    uint8_t bounty = o->b[0x31];
    if (!bounty) {
        if ((uint8_t)f->message_time == 0) message(g, 0xaf97, 0x23);
        return;
    }
    add_kill(g);
    uint16_t pay;
    if (bounty == 0xff) { /* no bounty: Thargoids pay 50 Cr, police and the safe zone cost */
        if (type_of(o) == 22) {
            pay = 0x1f4;
        } else {
            int police = type_of(o) == 28 && get16(o, 0x3a) == 1;
            if (g->f.safe_zone & 1)
                add_legal(g, police ? 4 : 2);
            else if (police)
                add_legal(g, 4);
            return;
        }
    } else {
        pay = bounty;
    }
    bounty_message(g, pay);
    add_cash(g, pay);
    if (f->hyperspace) { /* witchspace: count the Thargoids down */
        int t = type_of(o);
        if (t != 7 && t != 22) return;
        unsigned sub = t == 22 ? 0x23 : 5;
        if (f->hyperspace < sub || f->hyperspace == sub)
            f->hyperspace = 1;
        else
            f->hyperspace = (uint8_t)(f->hyperspace - sub);
        if (f->hyperspace == 1) message(g, 0xaec1, 0x23);
    }
}

/* ad1e */
static void target_note(ep_game *g, int slot)
{
    if (g->f.target_note == 2 && g->f.target_slot == (uint16_t)(0x76de + 0x40 * slot)) {
        message(g, 0xb21b, 0x23);
        g->f.target_note = 0;
    }
}

void ep_laser_hits(ep_game *g)
{
    ep_flight *f = &g->f;
    if (f->firing != 1) return;
    int slot = laser_target(g);
    if (slot >= 0) {
        ep_object *o = &g->space.obj[slot];
        o->b[EP_OBJ_FLAGS1E] |= 1;
        ep_event_add(g, EP_EV_SOUND, 0x0f); /* 4deb */
        uint8_t dmg = f->laser_fired;
        if (dmg == 2 && type_of(o) == 5) f->mining = 1;
        dmg++;
        if (f->station_angry == 1) {
            if (is_station(o)) {
                autopilot_off(g);
                dmg = (uint8_t)-(int8_t)((int8_t)-(int8_t)dmg >> 1);
            }
        } else if (is_station(o)) {
            autopilot_off(g);
            add_legal(g, 0x28);
        }
        unsigned heat = o->b[0x30] + (unsigned)dmg;
        o->b[0x30] = (uint8_t)(heat > 0xff ? 0xff : heat);
        int alive = o->b[0x2b] >= dmg;
        o->b[0x2b] = (uint8_t)(o->b[0x2b] - dmg);
        if (!alive) {
            int destroy = 1;
            if (o->b[EP_OBJ_FLAGS1E] & 4) {
                o->b[0x2b] = 0;
                destroy = 0;
                if (is_station(o)) {
                    if (f->station_angry == 1) {
                        f->station_hit = 1;
                        f->station_angry = 0;
                        destroy = 1;
                    } else {
                        unsigned l = ep_commander_b(&g->cmdr, EP_CMDR_LEGAL) + 0x28u;
                        ep_commander_set_b(&g->cmdr, EP_CMDR_LEGAL, (uint8_t)l);
                        if (l > 0xff) {
                            ep_commander_set_b(&g->cmdr, EP_CMDR_LEGAL, 0xff);
                            autopilot_off(g);
                        }
                    }
                }
            }
            if (destroy) {
                ep_kill_reward(g, o);
                target_note(g, slot);
                ep_explode(g, o);
                beam(g);
                f->mining = 0;
                f->firing = 0;
                return;
            }
        }
    }
    beam(g);
    ep_event_add(g, EP_EV_SOUND, (uint16_t)(0x14 + (f->laser_fired & 3))); /* 4dc9 */
    f->mining = 0;
    f->firing = 0;
}

/* 67ab: damage to the player: the fore shield takes it first, then energy */
void ep_damage(ep_game *g, uint16_t amount)
{
    ep_flight *f = &g->f;
    if (f->no_crash) return;
    uint16_t rest;
    if (amount >= 0x100) {
        rest = (uint16_t)(amount - f->fore_shield);
        f->fore_shield = 0;
    } else {
        uint8_t s = (uint8_t)(f->fore_shield - (uint8_t)amount);
        int borrow = f->fore_shield < (uint8_t)amount;
        f->fore_shield = s;
        if (!borrow) return;
        f->fore_shield = 0;
        rest = (uint16_t)(int16_t)(int8_t)(uint8_t)(0u - s);
    }
    if (f->energy < rest) {
        if (!f->no_crash) {
            f->dead = 1;
            ep_event_add(g, EP_EV_SOUND, 0x12); /* 6cfa: 4df5 */
        }
        f->energy = 0;
    } else {
        f->energy = (uint16_t)(f->energy - rest);
    }
}

/* 67eb: |a - b| as 11-bit signed angles, compared with a tolerance: 1 when within */
static int angle_near(uint16_t a, uint16_t b, uint16_t tol)
{
    int16_t d = (int16_t)(a & 0x7ff), c = (int16_t)(b & 0x7ff);
    if (d & 0x400) d = (int16_t)(d | (int16_t)0xf800);
    if (c & 0x400) c = (int16_t)(c | (int16_t)0xf800);
    int16_t x = (int16_t)((d - c) & 0x7ff);
    if (x & 0x400) x = (int16_t)(x | (int16_t)0xf800);
    uint16_t m = (uint16_t)(x < 0 ? -x : x);
    return m < tol;
}

/* 681f: lined up with the station within tol */
static int lined_up(const ep_game *g, const ep_object *o, uint16_t tol)
{
    const uint16_t *a = g->space.player_angle;
    uint16_t want;
    if (angle_near(a[0], 0, tol))
        want = 0x400;
    else if (angle_near(a[0], 0x400, tol))
        want = 0;
    else
        return 0;
    if (!angle_near(a[1], want, tol)) return 0;
    uint16_t roll = (uint16_t)(get16(o, 0x0e) & 0x7ff);
    if (angle_near(a[2], roll, tol)) return 1;
    return angle_near(a[2], (uint16_t)((roll + 0x400) & 0x7ff), tol);
}

/* 6b54: |x|, |y|, |z| all below r (16-bit, unsigned after neg) */
static int within(int16_t x, int16_t y, int16_t z, uint16_t r)
{
    return ep_abs16((uint16_t)x) < r && ep_abs16((uint16_t)y) < r && ep_abs16((uint16_t)z) < r;
}

void ep_collisions(ep_game *g)
{
    ep_flight *f = &g->f;
    int n = g->space.ship_slots;
    for (int i = 0; i < n && i < EP_OBJECTS; i++) {
        ep_object *o = &g->space.obj[i];
        if (!(o->b[EP_OBJ_FLAGS] & 1)) continue;
        uint16_t r = ep_crash_radius[(o->b[EP_OBJ_FLAGS] & 0x3e) >> 1];
        int16_t x = (int16_t)get16(o, EP_OBJ_POS), y = (int16_t)get16(o, EP_OBJ_POS + 2);
        int16_t z = (int16_t)get16(o, EP_OBJ_POS + 4);
        if (!within(x, y, z, r)) {
            if (is_station(o)) o->b[0x0c] &= 0xfe;
            continue;
        }
        uint16_t dmg;
        if (!is_station(o)) {
            dmg = 0x1c2;
        } else if (o->b[0x0c] & 1) {
            dmg = 0x5dc;
        } else {
            o->b[0x0c] |= 1;
            if (!(o->b[EP_OBJ_FLAGS] & 0x80)) {
                dmg = 0x5dc;
            } else if (lined_up(g, o, 0x64)) {
                if (f->station_angry == 1 || !within(x, y, 0, 0x5a) || (o->b[EP_OBJ_FLAGS1E] & 1)) {
                    dmg = 0x5dc;
                } else { /* docked */
                    f->docked = 1;
                    f->autopilot = 0;
                    f->steer = 0;
                    f->ap_flag = 0;
                    continue;
                }
            } else if (!lined_up(g, o, 0xfa)) {
                dmg = 0x5dc;
            } else if (!within(x, y, 0, 0x6e)) {
                dmg = 0x190;
            } else {
                o->b[0x0c] &= 0xfe;
                dmg = 0x1e;
            }
        }
        ep_kill_reward(g, o);
        target_note(g, i);
        if (dmg == 0x5dc || !is_station(o)) o->b[EP_OBJ_FLAGS] &= 0xfe; /* 7e82 */
        ep_damage(g, dmg);
        ep_event_add(g, EP_EV_SOUND, 0x12); /* 4df5 */
    }
}

/* aef7: screen point of a camera position, (x·256/z + 98h, (y − y/8)·256/z + 3eh); the view
 * centre on a divide error */
static void screen_point(int16_t x, int16_t y, uint16_t z, int16_t *sx, int16_t *sy)
{
    uint16_t ax = ep_abs16((uint16_t)x), ay = ep_abs16((uint16_t)y);
    uint32_t nx = (uint32_t)(ax >> 8) << 16 | (uint32_t)(ax & 0xff) << 8;
    if (!z || nx / z > 0xffff) {
        *sx = 0x98;
        *sy = 0x3e;
        return;
    }
    uint16_t qx = (uint16_t)(nx / z);
    uint16_t yy = (uint16_t)(ay - (ay >> 3));
    uint32_t ny = (uint32_t)(yy >> 8) << 16 | (uint32_t)(yy & 0xff) << 8;
    if (ny / z > 0xffff) {
        *sx = 0x98;
        *sy = 0x3e;
        return;
    }
    uint16_t qy = (uint16_t)(ny / z);
    if (y < 0) qy = (uint16_t)(0u - qy);
    if (x < 0) qx = (uint16_t)(0u - qx);
    *sx = (int16_t)(qx + 0x98);
    *sy = (int16_t)(qy + 0x3e);
}

void ep_enemy_fire(ep_game *g)
{
    ep_flight *f = &g->f;
    if (!f->under_fire) return;
    ep_event_add(g, EP_EV_SOUND, 0x17); /* 4da4 */
    const ep_object *o = &g->space.obj[(uint16_t)(f->attacker - 0x76de) / 0x40 % EP_OBJECTS];
    if (o->b[EP_OBJ_FLAGS] & 0x80) { /* the beam, from an edge of the view to the attacker */
        int16_t sx, sy;
        screen_point((int16_t)get16(o, EP_OBJ_CAM), (int16_t)get16(o, EP_OBJ_CAM + 2),
                     get16(o, EP_OBJ_CAM + 4), &sx, &sy);
        uint16_t r = ep_flight_random(g);
        int16_t ex, ey;
        if (r < 0x53fc) {
            ex = (int16_t)(r & 0xff);
            ey = 0;
        } else if (r < 0xa7f8) {
            ex = (int16_t)(r & 0xff);
            ey = 0x7f;
        } else if (r < 0xd2f0) {
            ex = 0;
            ey = (int16_t)(r & 0x7f);
        } else {
            ex = 0xff;
            ey = (int16_t)(r & 0x7f);
        }
        ep_render_clipped_line(&g->render, 0x0e, ex, ey, sx, sy);
    }
    f->under_fire = 0;
    uint8_t *shield = (f->hit_from_behind & 0x80) ? &f->aft_shield : &f->fore_shield;
    if (*shield >= 0x0f) {
        *shield = (uint8_t)(*shield - 0x0f);
        ep_event_add(g, EP_EV_SOUND, 0x19); /* 4e9d */
        return;
    }
    uint8_t over = (uint8_t)(*shield - 0x0f);
    *shield = 0;
    uint16_t rest = (uint16_t)(int16_t)(int8_t)(uint8_t)(0u - over);
    if (f->energy < rest) {
        f->energy = 0;
        f->dead = 1;
        return;
    }
    f->energy = (uint16_t)(f->energy - rest);
    ep_event_add(g, EP_EV_SOUND, 1); /* 4ea2 */
}

/* 7110: copy the n-th NUL-terminated name from the text at off; returns the end (the NUL) */
static int copy_name(uint8_t *dst, int at, int max, unsigned off, uint8_t n)
{
    while (n && off < sizeof ep_ship_text) {
        if (!ep_ship_text[off++]) n--;
    }
    for (;;) {
        uint8_t c = off < sizeof ep_ship_text ? ep_ship_text[off++] : 0;
        if (at < max) dst[at] = c;
        at++;
        if (!c) return at - 1;
    }
}

static int is_rock(const ep_object *o)
{
    int t = type_of(o);
    return t == 12 || t == 6 || t == 5 || t == 11;
}

/* 70b9: "Missile locked onto <type> (<role>)" */
static void lock_message(ep_game *g, const ep_object *o)
{
    ep_flight *f = &g->f;
    uint8_t type = (uint8_t)type_of(o), role = o->b[0x33];
    if (role == 3 && !is_rock(o)) role = 8;
    if (role == 4 && type == 5) role = 9;
    if (type == 0x1c && role != 5) role = 10;
    int max = (int)sizeof f->lock_text;
    int at = copy_name(f->lock_text, 0, max, EP_SHIP_TEXT_TYPES, type);
    if (role && role != 8) {
        if (at < max) f->lock_text[at] = ' ';
        if (at + 1 < max) f->lock_text[at + 1] = '(';
        at = copy_name(f->lock_text, at + 2, max, 0, role);
        if (at < max) f->lock_text[at] = ')';
        if (at + 1 < max) f->lock_text[at + 1] = 0;
    }
    f->message = 0x806d;
    f->message_time = 0x28;
}

void ep_missile_lock(ep_game *g)
{
    ep_flight *f = &g->f;
    if (f->target_note != 1) return;
    int i = laser_target(g);
    if (i < 0) return;
    const ep_object *o = &g->space.obj[i];
    if (o->b[EP_OBJ_FLAGS1E] & 0x20) return; /* not the mission ships */
    f->target_slot = (uint16_t)(0x76de + 0x40 * i);
    f->target_note = 2;
    lock_message(g, o);
    ep_event_add(g, EP_EV_SOUND, f->sound_device == 2 ? 0x88 : 4); /* 4e09 */
}
