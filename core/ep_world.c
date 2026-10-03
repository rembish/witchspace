/* Elite Plus objects in flight, reconstructed from ELITE.EXE (see ep_world.h). */
#include "ep_world.h"

#include "ep_circle.h"
#include "ep_combat.h"
#include "ep_dsmap.h"
#include "ep_render.h"

static uint16_t get16(const ep_object *o, int off) { return (uint16_t)(o->b[off] | o->b[off + 1] << 8); }

static void set16(ep_object *o, int off, uint16_t v)
{
    o->b[off] = (uint8_t)v;
    o->b[off + 1] = (uint8_t)(v >> 8);
}

void ep_event_add(ep_game *g, uint8_t kind, uint16_t arg)
{
    if (g->nevents < EP_MAX_EVENTS) {
        g->event[g->nevents].kind = kind;
        g->event[g->nevents].arg = arg;
        g->event[g->nevents].at = (uint16_t)g->render.nprim;
        g->nevents++;
    }
}

void ep_view_clear(ep_game *g)
{
    ep_render_rect(&g->render, 0, 8, 9, 0x130, 0x7c);
    ep_render_sprite(&g->render, 0x00, 0x60, 0xa0);
    ep_render_sprite(&g->render, 0x33, 0x128, 0x9d);
}

void ep_view_flip(ep_game *g)
{
    g->flip = g->clock;
    ep_event_add(g, EP_EV_FLIP, 0);
}

void ep_output_begin(ep_game *g)
{
    g->render.nprim = 0;
    g->render.ntext = 0;
    g->circles.n = 0;
    g->nevents = 0;
}

/* 6cfa: flying into the sun or the planet */
static void crash(ep_game *g)
{
    if (g->f.no_crash) return;
    g->f.dead = 1;
    ep_event_add(g, EP_EV_SOUND, 0x12); /* 4df5 */
}

/* 46e2: what the fuel scoops pick up: anything in the box under the ship (|x|, |z| < 150,
 * 30 <= y < 230), by type: a canister's cargo (at random, or the masking device), an escape
 * pod's slaves, a Thargon's alien items, a boulder's alloys or an asteroid's gems and metals.
 * Larger things only get a message. The hold's room is a byte: over-full counts as room. */
static void scoop(ep_game *g, ep_object *o, const int16_t p[3])
{
    ep_flight *f = &g->f;
    uint8_t *c = g->cmdr.b, *cargo = &c[EP_CMDR_CARGO];
    uint8_t room = (uint8_t)((c[EP_CMDR_EQUIPMENT + 1] == 1 ? 0x23 : 0x14) - c[EP_CMDR_CARGO_USED]);
    if (p[1] < 0x1e || p[1] >= 0x1e + 0xc8) return;
    if (ep_abs16((uint16_t)p[0]) >= 0x96 || ep_abs16((uint16_t)p[2]) >= 0x96) return;
    uint16_t msg;
    switch (o->b[EP_OBJ_FLAGS] & 0x3e) {
    case 0x28: return;
    case 0x22: /* 4754: a canister */
        if (o->b[EP_OBJ_FLAGS1E] & 0x40) {
            o->b[EP_OBJ_FLAGS] &= 0xfe; /* 7e82 */
            c[0xcd] = 1;                /* ds:83a8: the masking device */
            msg = 0x2c30;
            break;
        }
        if (!room) {
            msg = 0x2c42;
            break;
        }
        o->b[EP_OBJ_FLAGS] &= 0xfe;
        {
            uint8_t k = (uint8_t)((uint8_t)ep_flight_random(g) / 0x18);
            if (k == 3) k = 0x0b; /* no slaves: furs */
            cargo[2 * k]++;
            c[EP_CMDR_CARGO_USED]++;
            for (int j = 0; j < 13; j++) f->scoop_text[j] = ep_ds_byte(g, (uint16_t)(0x8f2d + 17 * k + j));
        }
        msg = 0x2c51;
        break;
    case 0x2a: /* 47b2: an escape pod */
        if (!room) {
            msg = 0x2c42;
            break;
        }
        o->b[EP_OBJ_FLAGS] &= 0xfe;
        cargo[2 * 3]++;
        c[EP_CMDR_CARGO_USED]++;
        msg = 0x2c5f;
        break;
    case 0x16: /* 47cd */
        if (o->b[EP_OBJ_FLAGS1E] & 0x10) {
            o->b[EP_OBJ_FLAGS] &= 0xfe;
            uint16_t r = ep_flight_random(g) & 0x0307;
            unsigned gems = cargo[2 * 15] + (r & 0xff);
            cargo[2 * 15] = gems > 0xff || (uint8_t)gems >= 0xfb ? 0xfa : (uint8_t)gems;
            cargo[2 * 13] = (uint8_t)(cargo[2 * 13] + (r >> 8)); /* no carry looked at */
            if (cargo[2 * 13] >= 0xfb) cargo[2 * 13] = 0xfa;
            r = ep_flight_random(g);
            cargo[2 * 14] = (uint8_t)(cargo[2 * 14] + ((r >> 8) & 3) + 1);
            if (cargo[2 * 14] >= 0xfb) cargo[2 * 14] = 0xfa;
            if (room) {
                cargo[(uint8_t)ep_flight_random(g) >= 0x28 ? 2 * 12 : 2 * 9]++;
                c[EP_CMDR_CARGO_USED]++;
            }
            msg = 0x2c7f;
            break;
        }
        if (!room) {
            msg = 0x2c42;
            break;
        }
        o->b[EP_OBJ_FLAGS] &= 0xfe;
        if (cargo[2 * 9] < 0xfa) {
            cargo[2 * 9]++;
            c[EP_CMDR_CARGO_USED]++;
        }
        msg = 0x2c98;
        break;
    case 0x0e: /* 485f: a Thargon */
        if (!room) {
            msg = 0x2c42;
            break;
        }
        o->b[EP_OBJ_FLAGS] &= 0xfe;
        if (cargo[2 * 16] < 0xfa) {
            cargo[2 * 16]++;
            c[EP_CMDR_CARGO_USED]++;
        }
        msg = 0x2ca5;
        break;
    default: msg = 0x2c0b; /* too large */
    }
    f->message = msg;
    f->message_time = 0x14;
}

/* 42c8: camera-space position and the in-view test */
static void to_camera(ep_game *g, ep_object *o)
{
    int16_t p[3];
    for (int k = 0; k < 3; k++) p[k] = (int16_t)get16(o, EP_OBJ_POS + 2 * k);
    ep_object_rotate(&g->space, o, p);
    if (ep_commander_b(&g->cmdr, EP_CMDR_EQUIPMENT + 5) == 1 && g->f.scoop_lock == 0) scoop(g, o, p);
    if (p[2] < 100) return;
    for (int k = 0; k < 3; k++) set16(o, EP_OBJ_CAM + 2 * k, (uint16_t)p[k]);
    uint16_t z = (uint16_t)p[2];
    if (z < (uint16_t)(ep_abs16((uint16_t)p[0]) << 1)) return;
    if (z < (uint16_t)(ep_abs16((uint16_t)p[1]) << 1)) return;
    o->b[EP_OBJ_FLAGS] |= 0x80;
}

/* (v << 8) / z with the sign handled separately, as 4626: 0 on a divide error */
static int project(int16_t v, uint16_t z, int16_t *out)
{
    int neg = v < 0;
    uint32_t a = ep_abs16((uint16_t)v);
    if (!z) return 0;
    uint32_t q = (a << 8) / z;
    if (q > 0xffff) return 0;
    *out = (int16_t)(uint16_t)(neg ? 0u - q : q);
    return 1;
}

/* 4612: the disc of the sun or a planet, radius `size`, colour from the slot (+0b) */
static void disc(ep_game *g, const ep_object *o, uint16_t size, uint16_t mask)
{
    int16_t y, x;
    if (!project((int16_t)get16(o, EP_OBJ_CAM + 2), get16(o, EP_OBJ_CAM + 4), &y)) return;
    y = (int16_t)(y + 0x3e);
    if (!project((int16_t)get16(o, EP_OBJ_CAM), get16(o, EP_OBJ_CAM + 4), &x)) return;
    x = (int16_t)(x + 0x98);
    int32_t r = (int16_t)size;
    if (x + r > 32767 || x + r < 0) return;
    if (x - r < -32768 || x - r > 0x12f) return;
    if (y + r > 32767 || y + r < 0) return;
    if (y - r < -32768 || y - r > 0x7b) return;
    int n0 = g->circles.n;
    ep_draw_circle(&g->rng, x, y, (int16_t)size, mask, 0, g->f.video == 2, &g->circles);
    ep_render_spans(&g->render, o->b[0x0b], n0, g->circles.n - n0); /* colour from the slot */
}

static void draw_disc(ep_game *g, const ep_object *o, uint16_t size, uint16_t mask)
{
    disc(g, o, size, mask);
    g->f.circle_mask = 0; /* 468d, also where a divide error resumes */
}

/* 44c7: sun (type 30) or planet (type 31) */
static void draw_planet_or_sun(ep_game *g, ep_object *o)
{
    ep_flight *f = &g->f;
    if (f->hyperspace) return;
    uint16_t size;
    if ((o->b[EP_OBJ_FLAGS] & 0x3e) == 0x3e) { /* 45e1: the planet */
        size = ep_apparent_size(o, 50);
        uint8_t h = (uint8_t)~size;
        if (h >= 0x80) h = 0x7f;
        f->altitude = (uint8_t)(h << 1);
        if (o->b[EP_OBJ_CAM + 5] & 0x80) return;
        if (size >= 0xfd) crash(g);
        draw_disc(g, o, size, 0);
        return;
    }
    /* the sun */
    if (f->approach && --f->approach == 0) { /* falling into it */
        f->approach = 1;
        if (!f->approach_size) f->approach_size = f->sun_size;
        unsigned grow = (unsigned)(f->approach_size >> 2);
        if (!grow) grow = 1;
        unsigned next = f->approach_size + grow;
        if (next > 0xff) {
            crash(g);
            next = 0xff;
        }
        f->approach_size = (uint8_t)next;
        size = (uint16_t)next;
    } else {
        size = ep_apparent_size(o, 100);
    }
    f->sun_size = (uint8_t)size;
    if (f->tribbles_shown) { /* 4527: Tribbles squeak now and then */
        uint32_t old = ep_rng_step(&g->rng);
        if ((old >> 16) <= 0x1388) ep_event_add(g, EP_EV_SURFACE_SOUND, size);
    }
    if (size >= 0xd2 && f->tribbles) {
        if (f->tribbles <= 0x10) {
            f->tribbles = 0;
            f->tribbles_shown = 0;
        } else {
            f->tribbles = (uint16_t)(f->tribbles - 0x10);
        }
        if (f->tribble_sprites) f->tribble_sprites--;
    }
    if (o->b[EP_OBJ_CAM + 5] & 0x80) return; /* behind */
    uint16_t mask = 1;
    if (size >= 0x28) {
        mask = 3;
        if (size >= 0xb4) mask = 7;
    }
    if (size >= 0xc3) {
        if (ep_commander_b(&g->cmdr, EP_CMDR_EQUIPMENT + 5) == 1) { /* fuel scoops: 45a2 */
            unsigned fuel = ep_commander_b(&g->cmdr, EP_CMDR_FUEL) + 6u;
            ep_commander_set_b(&g->cmdr, EP_CMDR_FUEL, (uint8_t)fuel);
            if (fuel > 0xff) {
                ep_commander_set_b(&g->cmdr, EP_CMDR_FUEL, 0xff);
                if (f->message != 0x2bf6) ep_event_add(g, EP_EV_SOUND, 2); /* 4ea7 */
                f->message = 0x2bf6;
                f->message_time = 5;
            }
        }
        if (size >= 0xfd) crash(g);
    }
    draw_disc(g, o, size, mask);
}

int ep_world_update(ep_game *g, int drawn[EP_OBJECTS])
{
    ep_space *s = &g->space;
    for (int k = 0; k < 3; k++) s->rot[k] = ep_rot_from_angle(s->player_angle[k]);
    int n = s->count < EP_OBJECTS ? s->count : EP_OBJECTS;
    for (int i = 0; i < n; i++) {
        ep_object *o = &s->obj[i];
        if (!(o->b[EP_OBJ_FLAGS] & 1)) continue;
        o->b[EP_OBJ_FLAGS] &= 0x3f;
        if (o->b[EP_OBJ_FLAGS] >= 0x3c) { /* 433c */
            ep_planet_to_camera(s, o);
            continue;
        }
        if (o->b[EP_OBJ_TIMER] && !(o->b[EP_OBJ_FLAGS1E] & 2)) {
            if (++o->b[EP_OBJ_TIMER] == 0) { /* 7e82: the explosion is over */
                o->b[EP_OBJ_FLAGS] &= 0xfe;
                continue;
            }
        }
        o->b[EP_OBJ_FLAGS1E] &= 0xfd;
        if (ep_object_in_range(o)) to_camera(g, o);
    }
    int nd = 0;
    for (;;) { /* 41aa: planet and sun, farthest (+3c) first */
        uint8_t far = 0;
        int pick = -1;
        for (int i = 0; i < n; i++) {
            const ep_object *o = &s->obj[i];
            uint8_t fl = o->b[EP_OBJ_FLAGS];
            if (!(fl & 1) || ((fl >> 1) & 0x1f) < 30 || !(fl & 0x40)) continue;
            if (far <= o->b[EP_OBJ_ZHI]) {
                far = o->b[EP_OBJ_ZHI];
                pick = i;
            }
        }
        if (!far) break;
        s->obj[pick].b[EP_OBJ_FLAGS] &= 0xbf;
        draw_planet_or_sun(g, &s->obj[pick]);
        drawn[nd++] = pick;
    }
    for (;;) { /* 41e3: ships, farthest camera z first */
        uint16_t far = 0;
        int pick = -1;
        for (int i = 0; i < n; i++) {
            const ep_object *o = &s->obj[i];
            if ((o->b[EP_OBJ_FLAGS] & 0xc1) != 0xc1) continue;
            uint16_t z = get16(o, EP_OBJ_CAM + 4);
            if (far < z) {
                far = z;
                pick = i;
            }
        }
        if (!far) break;
        s->obj[pick].b[EP_OBJ_FLAGS] &= 0xbf;
        ep_ship_view v = ep_ship_view_of(s, &s->obj[pick]);
        ep_draw_ship(&g->render, &v);
        drawn[nd++] = pick;
    }
    /* 487e: the compass (in flight) only draws */
    return nd;
}
