/* Elite Plus between systems, reconstructed from ELITE.EXE (see ep_travel.h). */
#include "ep_travel.h"

#include <string.h>

#include "ep_chart.h"
#include "ep_combat.h"
#include "ep_commands.h"
#include "ep_world.h"
#include "ep_flight.h"
#include "ep_tables.h"
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
    if (f->approach || g->cmdr.b[EP_CMDR_DOCKED_AT] != g->cmdr.b[EP_CMDR_GALAXY] ||
        g->cmdr.b[EP_CMDR_DOCKED_AT + 1] != current(g, EP_SYSREC_INDEX)) {
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

void ep_rings_start(ep_game *g) { memcpy(g->f.rings, ep_ring_start, sizeof g->f.rings); }

void ep_rings_frame(ep_game *g)
{
    for (int k = 0; k < 10; k++) {
        uint8_t *r = &g->f.rings[3 * k];
        if (r[0]) {
            r[0]--;
            continue;
        }
        if (r[1] >= 0x96) continue;
        uint8_t step = (uint8_t)(r[1] >> 3);
        r[1] = (uint8_t)(r[1] + (step ? step : 1));
        if (r[1] < 0x14) continue;
        /* 2d16, colour r[2]: an outline at the centre of the view */
        ep_draw_circle(&g->rng, 0x98, 0x3e, r[1], g->f.circle_mask, 1, g->f.video == 2, &g->circles);
    }
}

void ep_witchspace(ep_game *g)
{
    uint8_t *c = g->cmdr.b;
    g->f.hyperspace = 0x64;
    uint8_t x = (uint8_t)(((unsigned)(g->seed.w[1] >> 8) + c[EP_CMDR_CHART_CENTRE]) >> 1); /* 16-bit add */
    c[EP_CMDR_CHART_CENTRE] = c[EP_CMDR_CURSOR] = c[EP_CMDR_CURSOR + 4] = x;
    c[EP_CMDR_CURSOR + 2] = 0x50;
    uint8_t y = (uint8_t)((uint8_t)((uint8_t)(g->seed.w[0] >> 9) + c[EP_CMDR_CHART_CENTRE + 1]) >> 1);
    c[EP_CMDR_CHART_CENTRE + 1] = c[EP_CMDR_CURSOR + 1] = c[EP_CMDR_CURSOR + 5] = y;
    c[EP_CMDR_CURSOR + 3] = 0x40;
}

void ep_jump_missions(ep_game *g)
{
    ep_flight *f = &g->f;
    uint8_t *c = g->cmdr.b;
    if (f->hyperspace) return;
    if (f->mission == 4 && (f->mission_state == 2 || f->mission_state == 1))
        f->mission_state = f->mission_system == c[EP_CMDR_TARGET] ? 2 : 1;
    if (f->convoy_countdown && --f->convoy_countdown == 0) c[EP_CMDR_CONVOY_DUE] = 1;
    if (c[EP_CMDR_GALAXY] == 0 && c[EP_CMDR_JUMPS_COUNTED] != 1) return;
    if (f->mission) return;
    if (++f->mission5_count == 0) f->mission5_count = 0xff; /* the jump count */
    f->mission5_flag = 1;
    static const uint8_t start[6] = { 0x20, 0x38, 0x50, 0x6e, 0x8c, 0xa0 };
    for (int k = 0; k < 6; k++)
        if (f->mission5_count == start[k]) {
            f->mission = (uint8_t)(k + 1);
            f->mission_state = 0;
        }
}

/* 74e3: 50 frames of rings (the view cleared, the message, the crosshair and the frame wait
 * are the frontend's) */
static void ring_frames(ep_game *g)
{
    for (int n = 0; n < 50; n++) {
        if (++g->f.flash == 6) g->f.flash = 0; /* 3921 */
        ep_message_tick(g);
        ep_rings_frame(g);
        g->in.last_key = 0xff; /* 0287 */
    }
}

void ep_arrive(ep_game *g)
{
    ep_flight *f = &g->f;
    uint8_t *c = g->cmdr.b;
    if (f->mission == 3 && f->mission_state == 1) {
        f->mission_state = 0;
        f->station_angry = 1;
    }
    f->message = 0x8269;
    f->message_time = 0x32;
    int galactic = f->galactic_jump == 1;
    if (galactic) { /* 7439 */
        if (c[EP_CMDR_GALAXY] == 8)
            c[EP_CMDR_GALAXY] = 0;
        else if (++c[EP_CMDR_GALAXY] == 8 && rng(g) >= 0x12c)
            c[EP_CMDR_GALAXY] = 0; /* the ninth galaxy, rarely */
        f->galaxy_digit = (uint8_t)('1' + c[EP_CMDR_GALAXY]);
        uint16_t r = rng(g);
        c[EP_CMDR_CURSOR] = (uint8_t)((r & 0x3f) + 0x60);
        c[EP_CMDR_CURSOR + 1] = (uint8_t)((r >> 8 & 0x1f) + 0x30);
        c[EP_CMDR_ZOOM] = 0;
        ep_find_nearest(g);
        ep_select_system(g);
    } else {
        c[EP_CMDR_FUEL] = (uint8_t)(c[EP_CMDR_FUEL] - f->jump_fuel);
        c[EP_CMDR_LEGAL] = c[EP_CMDR_LEGAL] >= 5 ? (uint8_t)(c[EP_CMDR_LEGAL] - 5) : 0;
    }
    c[EP_CMDR_SELECTED + EP_SYSREC_DIST] = 0;
    c[EP_CMDR_SELECTED + EP_SYSREC_DIST + 1] = 0;
    memmove(&c[EP_CMDR_CURRENT], galactic ? &c[EP_CMDR_SELECTED] : f->hyper_target, 0x19); /* ds:82d9 */
    c[EP_CMDR_MARKET_DRAWN] = 0;
    uint8_t index = galactic ? c[EP_CMDR_SELECTED + EP_SYSREC_INDEX] : c[EP_CMDR_TARGET];
    g->seed = ep_system_seed(c[EP_CMDR_GALAXY], index);
    if ((!galactic && rng(g) < 0x366 && f->mission == 0) || f->force_misjump == 1) {
        f->force_misjump = 0;
        ep_witchspace(g);
    } else {
        f->hyperspace = 0;
        uint8_t x = (uint8_t)(g->seed.w[1] >> 8), y = (uint8_t)(g->seed.w[0] >> 9);
        c[EP_CMDR_CHART_CENTRE] = c[EP_CMDR_CURSOR] = c[EP_CMDR_CURSOR + 4] = x;
        c[EP_CMDR_CURSOR + 2] = 0x50;
        c[EP_CMDR_CHART_CENTRE + 1] = c[EP_CMDR_CURSOR + 1] = c[EP_CMDR_CURSOR + 5] = y;
        c[EP_CMDR_CURSOR + 3] = 0x40;
        if (c[EP_CMDR_ZOOM] == 1) {
            c[EP_CMDR_CURSOR] = 0x50;
            c[EP_CMDR_CURSOR + 1] = 0x40;
        }
    }
    ring_frames(g);
    ep_new_system(g);
    ep_jump_missions(g);
    f->approach_size = 0;
    f->approach = 0;
    f->siege = 1;
    uint16_t text = 0x8250;
    if (f->galactic_jump) {
        text = c[EP_CMDR_GALAXY] == 8 ? 0x829d : 0x8283;
        c[EP_CMDR_LEGAL] = 0;
        c[EP_CMDR_EQUIPMENT + 10] = 0; /* the galactic hyperdrive is used up */
    }
    if (f->hyperspace) text = 0x82b8;
    f->message = text;
    f->message_time = 0x28;
    f->galactic_jump = 0;
    if (f->hyperspace == 1) return;
    if (f->mission == 1) {
        if (f->mission5_phase == 0) f->leak_countdown = 0x32;
    } else if (f->mission == 3 && f->mission5_phase == 1 && f->station_hit != 1) {
        f->leak_countdown = 0x32;
    }
}

void ep_tunnel_start(ep_game *g)
{
    g->f.message = g->f.docked ? 0x7698 : 0x7682;
    g->f.message_time = 1;
    ep_message_tick(g);
    ep_dashboard_tick(g);
}

void ep_tunnel_frame(ep_game *g, int k)
{
    ep_flight *f = &g->f;
    if (++f->flash == 6) f->flash = 0; /* 3921 */
    if (!f->docked) {
        int drawn[EP_OBJECTS];
        ep_dust_frame(g);
        ep_world_update(g, drawn);
        ep_player_move(g);
    }
    /* 6988, 6941: the walls (the frontend's), 301a: the frame wait */
    g->in.last_key = 0xff;                                               /* 0287 */
    if (k == 0 && !f->docked && f->sound_device != 2 && !f->sound_off) { /* 4e5a: the launch */
        ep_event_add(g, EP_EV_SOUND, 0x11);
        ep_event_add(g, EP_EV_WAIT, f->sound_device ? 0x78 : 0x23a);
    }
}

void ep_launch(ep_game *g)
{
    ep_flight *f = &g->f;
    if (f->other_screen) { /* 763e */
        f->other_screen = 0;
        f->message_time = (uint16_t)(f->message_time & 0xff);
        f->message_shown = 0;
    }
    f->screen = 0;
    ep_key_bar(g);
    /* 028d: the mouse driver is reset */
    ep_flight_start(g);
    f->launching = 1;
    ep_tunnel_start(g);
    for (int k = 0; k < 20; k++) ep_tunnel_frame(g, k);
}

void ep_enter_station(ep_game *g)
{
    g->f.screen_shown = 0xff;
    g->f.screen = 1;
    g->f.launching = 0;
}

void ep_dock(ep_game *g)
{
    ep_tunnel_start(g);
    for (int k = 0; k < 20; k++) ep_tunnel_frame(g, k);
    ep_enter_station(g);
}
