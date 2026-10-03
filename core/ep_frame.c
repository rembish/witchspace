/* Elite Plus flight frame, reconstructed from ELITE.EXE (see ep_frame.h). */
#include "ep_frame.h"

#include <string.h>

#include "ep_combat.h"
#include "ep_commands.h"
#include "ep_travel.h"
#include "ep_dust.h"
#include "ep_flight.h"
#include "ep_ships.h"
#include "ep_world.h"

void ep_frame_before_ai(ep_game *g)
{
    if (++g->f.flash == 6) g->f.flash = 0; /* 3921 */
    ep_missile_lock(g);
    ep_view_clear(g); /* 3130 */
    ep_dashboard_tick(g);
    ep_dust_frame(g);
    int drawn[EP_OBJECTS];
    ep_world_update(g, drawn);
    ep_enemy_fire(g);
    ep_laser_hits(g);
    ep_fuel_leak(g);
    ep_message_tick(g);
    int laser = ep_view_laser(g);
    if (laser >= 0) { /* 4f34: the crosshair, by laser (3411 leaves DL 10h) */
        ep_render_sprite(&g->render, (uint8_t)(laser + 1), 0x98, 0x3f);
        g->f.reg_dl = 0x10;
    }
    if (g->test_dl_force) g->f.reg_dl = g->test_dl;
}

void ep_frame_from_ai(ep_game *g)
{
    ep_ai_frame(g);
    ep_controls(g);
    ep_collisions(g);
    ep_tribbles_tick(g);
    ep_view_flip(g); /* 301a */
}

/* 6b71: (0, 0, 40) turned the way the ship flies (the view, then the attitude) */
static void flight_direction(ep_game *g, int16_t v[3])
{
    ep_space *s = &g->space;
    s->rot[5] = ep_rot_from_angle((uint16_t)(0u - s->extra_angle));
    s->rot[4] = ep_rot_from_angle((uint16_t)(0u - (uint16_t)(s->player_angle[0] + 0x400)));
    s->rot[3] = ep_rot_from_angle((uint16_t)(s->player_angle[1] + 0x400));
    int16_t ax = 0, bx = 0x28, cx = 0, t;
    if (s->extra_angle) {
        ep_rotate_pair(&s->rot[5], &ax, &bx);
        s->rot[5] = ep_rot_from_angle((uint16_t)(0u - s->player_angle[2]));
        cx = bx;
        bx = 0;
        ep_rotate_pair(&s->rot[5], &ax, &bx);
        t = cx, cx = bx, bx = t;
    }
    ep_rotate_pair(&s->rot[3], &ax, &bx);
    t = cx, cx = ax, ax = t;
    ep_rotate_pair(&s->rot[4], &ax, &bx);
    t = cx, cx = ax, ax = t;
    t = cx, cx = bx, bx = t;
    v[0] = ax;
    v[1] = bx;
    v[2] = cx;
}

static void put24(ep_object *o, int k, int16_t v)
{
    o->b[EP_OBJ_POS + 2 * k] = (uint8_t)v;
    o->b[EP_OBJ_POS + 2 * k + 1] = (uint8_t)((uint16_t)v >> 8);
    o->b[EP_OBJ_POS_HI + k] = v < 0 ? 0xff : 0;
}

static void add24(ep_object *o, int k, int16_t v)
{
    uint32_t p = (uint32_t)o->b[EP_OBJ_POS_HI + k] << 16;
    p |= (uint32_t)o->b[EP_OBJ_POS + 2 * k + 1] << 8;
    p |= o->b[EP_OBJ_POS + 2 * k];
    uint32_t d = v < 0 ? 0x1000000u - (uint32_t)(-(int32_t)v) : (uint32_t)v;
    p = (p + d) & 0xffffffu;
    o->b[EP_OBJ_POS + 2 * k] = (uint8_t)p;
    o->b[EP_OBJ_POS + 2 * k + 1] = (uint8_t)(p >> 8);
    o->b[EP_OBJ_POS_HI + k] = (uint8_t)(p >> 16);
}

/* a random -15..16 spread (5 bits) plus a component of the flight */
static uint8_t spread(uint8_t bits, int16_t v)
{
    return (uint8_t)((int16_t)(int8_t)((bits & 0x1f) - 0xf) + v);
}

void ep_death(ep_game *g)
{
    ep_flight *f = &g->f;
    flight_direction(g, f->death_vel);
    const int16_t *v = f->death_vel;
    f->velocity[0] = f->velocity[1] = f->velocity[2] = 0;
    f->speed = 8;
    f->moved = 1;
    f->ap_flag = 1;
    for (int n = 0; n < 6; n++) {
        ep_object *p = ep_debris_slot(g);
        if (!p) break;
        memset(p->b, 0, sizeof p->b);
        uint16_t r = ep_flight_random(g);
        p->b[0x26] = (uint8_t)r;
        p->b[0x27] = (uint8_t)(r >> 8);
        p->b[0x19] = spread((uint8_t)r, v[0]);
        p->b[0x1a] = spread((uint8_t)(r >> 8), v[1]);
        p->b[0x1b] = spread((uint8_t)((uint8_t)r >> 2), v[2]);
        p->b[0x33] = 7;
        p->b[0x2e] = 0x32;
        p->b[EP_OBJ_FLAGS] = 0x17;
        ep_particle_update(p);
    }
    if (!g->cmdr.b[EP_CMDR_CARGO_USED]) return;
    ep_object *c = ep_claim_slot(g); /* a canister of the cargo */
    memset(c->b, 0, sizeof c->b);
    for (int k = 0; k < 3; k++) {
        int16_t d = (int16_t)(int8_t)((ep_flight_random(g) & 0xf) - 7);
        c->b[0x19 + k] = (uint8_t)((int16_t)(uint16_t)(d + v[k]) >> 2);
        put24(c, k, (int16_t)(uint16_t)((uint16_t)v[k] << 2));
    }
    ep_ship_init(c, 3);
    c->b[0x33] = 3;
    uint16_t a, b;
    ep_aim(g, (int16_t)(c->b[4] | c->b[5] << 8), (int16_t)(c->b[6] | c->b[7] << 8),
           (int16_t)(c->b[8] | c->b[9] << 8), &a, &b);
    c->b[0x0a] = (uint8_t)a;
    c->b[0x0b] = (uint8_t)(a >> 8);
    c->b[0x0c] = (uint8_t)b;
    c->b[0x0d] = (uint8_t)(b >> 8);
    int16_t j = (int16_t)((ep_flight_random(g) & 0x3f) - 0x1f);
    add24(c, 0, j);
    add24(c, 1, j); /* the same jitter both ways */
}

/* a07f onwards, after the commands */
static int frame_after(ep_game *g, int r)
{
    ep_flight *f = &g->f;
    switch (r) {
    case EP_CMD_RESTART: return EP_FRAME_NEXT;
    case EP_CMD_SCREEN: return EP_FRAME_SCREEN;
    case EP_CMD_PAUSE: f->resume = EP_RESUME_FLIGHT; return EP_FRAME_PAUSED;
    default: break;
    }
    ep_jump_drive(g);
    ep_countdowns(g);
    if (ep_tunnel_tick(g)) { /* the escape capsule reached the station */
        ep_enter_station(g);
        return EP_FRAME_DOCKED;
    }
    ep_energy_drain(g);
    if (f->dead != 1) return EP_FRAME_NEXT;
    f->hyper_countdown = 0;
    if (f->scoop_lock) {
        if (--f->scoop_lock) return EP_FRAME_NEXT;
        g->space.in_flight = 0;
        return EP_FRAME_OVER;
    }
    ep_death(g);
    f->scoop_lock = 0x3c;
    f->message = 0xb127;
    f->message_time = 0x3c;
    return EP_FRAME_NEXT;
}

int ep_flight_frame(ep_game *g)
{
    ep_flight *f = &g->f;
    ep_key_bar(g);
    ep_frame_before_ai(g);
    ep_frame_from_ai(g);
    if (f->docked == 1) {
        ep_dock(g);
        return EP_FRAME_DOCKED;
    }
    ep_laser_fire(g);
    return frame_after(g, ep_commands(g));
}

int ep_flight_resume(ep_game *g)
{
    g->f.resume = EP_RESUME_NONE;
    return frame_after(g, ep_commands(g)); /* 03c0 goes on reading keys */
}
