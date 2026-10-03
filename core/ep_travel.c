/* Elite Plus between systems, reconstructed from ELITE.EXE (see ep_travel.h). */
#include "ep_travel.h"

#include <string.h>

#include "ep_combat.h"
#include "ep_dust.h"
#include "ep_galaxy.h"
#include "ep_objects.h"

static uint16_t rng(ep_game *g) { return ep_flight_random(g); }

static uint8_t current(const ep_game *g, int field) { return g->cmdr.b[EP_CMDR_CURRENT + field]; }

static void set16(ep_object *o, int off, uint16_t v)
{
    o->b[off] = (uint8_t)v;
    o->b[off + 1] = (uint8_t)(v >> 8);
}

static uint16_t get16(const ep_object *o, int off) { return (uint16_t)(o->b[off] | o->b[off + 1] << 8); }

/* the middle and high bytes (+5/+1, +7/+2, +9/+3) of 24-bit coordinate k */
static void set_upper(ep_object *o, int k, uint16_t v)
{
    o->b[EP_OBJ_POS + 2 * k + 1] = (uint8_t)v;
    o->b[EP_OBJ_POS_HI + k] = (uint8_t)(v >> 8);
}

static uint16_t get_upper(const ep_object *o, int k)
{
    return (uint16_t)(o->b[EP_OBJ_POS + 2 * k + 1] | o->b[EP_OBJ_POS_HI + k] << 8);
}

void ep_flight_start(ep_game *g)
{
    ep_flight *f = &g->f;
    ep_space *s = &g->space;
    g->seed = ep_system_seed(g->cmdr.b[EP_CMDR_GALAXY], current(g, EP_SYSREC_INDEX)); /* 610e */
    s->ship_slots = 0x14;
    s->debris_slots = 0x10;
    s->count = 0x24;
    memset(s->obj, 0, sizeof s->obj); /* 816b */
    ep_dust_reset(g);
    f->hyper_countdown = 0;
    f->autopilot = 0;
    s->extra_angle = 0;
    f->ap_flag = 0;
    f->no_crash = 0;
    f->docked = 0;
    f->velocity[0] = 0;
    f->velocity[1] = 0;
    f->velocity[2] = 0x14;
    f->speed = 0x14;
    f->altitude = 0xff;
    f->missile_block = 0;
    for (int k = 0; k < 3; k++) s->player_angle[k] = 0;
    if (f->hyperspace) {
        f->danger_gov = 0;
        return;
    }
    if (f->approach || g->cmdr.b[0xde] != g->cmdr.b[EP_CMDR_GALAXY] ||
        g->cmdr.b[0xdf] != current(g, EP_SYSREC_INDEX)) {
        ep_object *o = &s->obj[0]; /* the sun: 24-bit position from three steps */
        o->b[EP_OBJ_FLAGS] = 0x3d;
        set_upper(o, 0, (uint16_t)((rng(g) & 0x3ff) - 0x200));
        set_upper(o, 1, (uint16_t)((rng(g) & 0x3ff) - 0x200));
        set_upper(o, 2, (uint16_t)((rng(g) & 0x3ff) - 0xc00));
        set16(o, 0x0b, (uint16_t)((g->seed.w[0] >> 8 & 3) + 0xb6)); /* colour */
        o->b[0x33] = 0;
        o->b[EP_OBJ_FLAGS1E] = 6;
        o->b[0x3f] = 0xff;
        o = &s->obj[1]; /* the planet, straight ahead */
        o->b[EP_OBJ_FLAGS] = 0x3f;
        set_upper(o, 2, 0x6e);
        set16(o, 0x0b, (uint16_t)((g->seed.w[0] & 7) + 0xae));
        o->b[0x33] = 0;
        o->b[EP_OBJ_FLAGS1E] = 6;
        o->b[0x3f] = 0xff;
        o = &s->obj[2]; /* the station: a Coriolis from tech level 9 */
        o->b[EP_OBJ_FLAGS] = (uint8_t)((current(g, EP_SYSREC_TECH) < 9) << 1 | 1);
        set16(o, 0x0c, 0x400);
        set16(o, EP_OBJ_POS + 4, 0xfed4);
        o->b[EP_OBJ_POS_HI + 2] = 0xff;
        o->b[EP_OBJ_FLAGS1E] = 4;
        o->b[0x33] = 1;
        o->b[0x1f] = (uint8_t)((rng(g) >> 8 & 7) + 10);
        o->b[0x3f] = 0xff;
        o->b[0x2b] = 0x96;
        o->b[0x31] = 0;
        if (f->station_angry == 1) {
            o->b[0x32] = 0;
            o->b[0x30] = 0;
            o->b[0x2b] = 0x96;
            o->b[0x1f] = 0;
            o->b[0x2d] = 10;
            o->b[EP_OBJ_FLAGS1E] &= 0xfe;
        }
    }
    f->danger_gov = current(g, EP_SYSREC_GOVERNMENT);
}

/* +-(200h..3ffh) from a step: the sign from bit 15 */
static uint16_t offset(ep_game *g)
{
    uint16_t r = rng(g);
    uint16_t v = (uint16_t)((r & 0x1ff) + 0x200);
    return (r & 0x8000) ? (uint16_t)(0u - v) : v;
}

/* 7db9: the angles that point at a 24-bit position (scaled like the planet) */
static void face(ep_game *g, const ep_object *o, uint16_t *a, uint16_t *b)
{
    uint8_t cl = ep_planet_scale(o);
    int16_t p[3];
    for (int k = 0; k < 3; k++) {
        int32_t v = (int32_t)((uint32_t)o->b[EP_OBJ_POS_HI + k] << 24 | (uint32_t)get16(o, EP_OBJ_POS + 2 * k)
                                                                            << 8) >>
                    8;
        p[k] = (int16_t)(uint16_t)(v >> cl);
    }
    uint16_t t = ep_atan2(p[1], p[2]);
    g->space.rot[4] = ep_rot_from_angle(t);
    ep_rotate_pair(&g->space.rot[4], &p[1], &p[2]);
    *a = t;
    *b = ep_atan2(p[0], p[2]);
}

void ep_new_system(ep_game *g)
{
    ep_flight_start(g);
    if (g->f.hyperspace) return;
    uint16_t z = rng(g);
    uint16_t y = offset(g), x = offset(g);
    for (int i = 0; i < 3; i++) {
        ep_object *o = &g->space.obj[i];
        set_upper(o, 0, (uint16_t)(get_upper(o, 0) + x));
        set_upper(o, 1, (uint16_t)(get_upper(o, 1) + y));
        uint32_t w = (uint32_t)get16(o, EP_OBJ_POS + 4) + z;
        set16(o, EP_OBJ_POS + 4, (uint16_t)w);
        unsigned hi = o->b[EP_OBJ_POS_HI + 2] + ((z & 0x8000) ? 0xffu : 0u) + (w >> 16);
        o->b[EP_OBJ_POS_HI + 2] = (uint8_t)hi;
    }
    uint16_t a, b;
    face(g, &g->space.obj[2], &a, &b);
    g->space.player_angle[0] = a;
    g->space.player_angle[1] = b;
    g->space.player_angle[2] = rng(g) & 0x7ff;
}
