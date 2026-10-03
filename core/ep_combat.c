/* Elite Plus laser hits, reconstructed from ELITE.EXE (see ep_combat.h). */
#include "ep_combat.h"

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
static void kill_reward(ep_game *g, ep_object *o)
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
                kill_reward(g, o);
                target_note(g, slot);
                ep_event_add(g, EP_EV_UNPORTED, 0x7ea8); /* the explosion */
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
