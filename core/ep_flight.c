/* Elite Plus flight loop subsystems, reconstructed from ELITE.EXE (see ep_flight.h). */
#include "ep_flight.h"

#include "ep_combat.h"

/* 7129: the warnings, checked in turn from the one after the last shown */
static void warnings(ep_game *g)
{
    static const uint16_t text[4] = { 0x81fe, 0x8212, 0x8222, 0x8236 }; /* ds:81f6 */
    ep_flight *f = &g->f;
    if (f->scoop_lock) return;
    if (f->warn_time) {
        f->message_time = f->warn_time; /* high byte 0: show it */
        f->message = f->warn_message;
        f->warn_time--;
        return;
    }
    for (int n = 0, k = (f->warn_index + 1) & 3; n < 4; n++, k = (k + 1) & 3) {
        int hit = 0;
        switch (k) {
        case 0:
            hit = f->missile_alert == 1;
            f->missile_alert = 0;
            break;
        case 1: hit = f->altitude < 0x32; break;
        case 2: hit = f->sun_size >= 0xe1; break;
        case 3: hit = f->energy < 0x100; break;
        }
        if (!hit) continue;
        f->warn_time = 0x14;
        f->warn_index = (uint8_t)k;
        if (k) ep_event_add(g, EP_EV_SOUND, 0x0b); /* 4d9f */
        f->warn_message = text[k];
        return;
    }
}

void ep_message_tick(ep_game *g)
{
    ep_flight *f = &g->f;
    warnings(g);
    if (f->message_time >> 8) { /* ds:805b: shown */
        if ((f->message_time & 0xff) == 0) {
            /* 7069: back to the view's name */
            uint16_t v = g->space.extra_angle;
            f->message = v == 0 ? 0x802c : v == 0x400 ? 0x8037 : v == 0x200 ? 0x8041 : 0x804b;
        } else {
            f->message_time = (uint16_t)(f->message_time - 1);
            return;
        }
    }
    f->message_time |= 0xff00; /* 7040 */
    f->message_shown = f->message;
}

void ep_fuel_leak(ep_game *g)
{
    ep_flight *f = &g->f;
    if (f->leak_countdown) {
        if (!--f->leak_countdown) f->leak = 0x33;
        return;
    }
    if (!f->leak) return;
    f->leak--;
    uint8_t fuel = ep_commander_b(&g->cmdr, EP_CMDR_FUEL);
    ep_commander_set_b(&g->cmdr, EP_CMDR_FUEL, fuel >= 5 ? (uint8_t)(fuel - 5) : 0);
    f->message_time = (uint16_t)(int16_t)(int8_t)f->leak;
    f->message = 0x85fa; /* FUEL LEAK! */
}

void ep_energy_drain(ep_game *g)
{
    if (g->f.energy_drain != 1) return;
    g->f.energy = g->f.energy >= 2 ? (uint16_t)(g->f.energy - 2) : 0;
}

int ep_view_laser(const ep_game *g)
{
    static const uint8_t shift[4] = { 1, 4, 2, 3 }; /* ds:5310: front, left?, rear, right? */
    uint8_t cl = shift[(g->space.extra_angle >> 9) & 3];
    if (!((ep_commander_b(&g->cmdr, EP_CMDR_LASERS) >> (cl - 1)) & 1)) return -1;
    return (ep_commander_b(&g->cmdr, EP_CMDR_LASER_TYPES) >> ((cl - 1) * 2)) & 3;
}

/* 0edf: the fire control (keyboard only so far) */
static int fire_pressed(ep_game *g)
{
    if (g->in.control != 0) {
        ep_event_add(g, EP_EV_UNPORTED, 0x0ef0); /* joystick, mouse */
        return 0;
    }
    return !(g->in.key[g->in.fire & 0x7f] & 0x80);
}

void ep_laser_fire(ep_game *g)
{
    ep_flight *f = &g->f;
    if (f->scoop_lock || f->no_crash) return;
    int type;
    if (!fire_pressed(g) || (type = ep_view_laser(g)) < 0) {
        f->laser_hold = 0;
        return;
    }
    if (f->laser_temp >= 0xf0 || f->laser_hold) return;
    f->laser_temp = f->laser_temp > 0xff - 5 ? 0xff : (uint8_t)(f->laser_temp + 5);
    if (type == 0 && (f->pulse_phase ^= 1)) return; /* pulse laser: every other frame */
    f->laser_fired = (uint8_t)type;
    f->firing = 1;
}

int ep_tunnel_tick(ep_game *g)
{
    if (!g->f.no_crash) return 0;
    if (--g->f.no_crash) return 0;
    if (g->f.tribbles) g->f.tribbles = 1;
    return 1;
}

static int key_down(const ep_game *g, uint8_t scancode) { return g->in.key[scancode & 0x7f] == 0; }

/* 10ea: arrow keys; a key held from the last frame builds up to +-23 */
static void arrows(ep_game *g, int8_t *x, int8_t *y)
{
    ep_flight *f = &g->f;
    int8_t ax = 0, ay = 0;
    if (key_down(g, g->in.up)) ay--;
    if (key_down(g, g->in.down)) ay++;
    if (key_down(g, g->in.left)) ax--;
    if (key_down(g, g->in.right)) ax++;
    int same = ax == f->last_x;
    f->last_x = ax;
    if (same) {
        int8_t v = (int8_t)(ax + f->accel_x);
        if (v > -24 && v < 24) f->accel_x = v;
    } else {
        f->accel_x = 0;
    }
    same = ay == f->last_y;
    f->last_y = ay;
    if (same) {
        int8_t v = (int8_t)(ay + f->accel_y);
        if (v > -24 && v < 24) f->accel_y = v;
    } else {
        f->accel_y = 0;
    }
    *x = f->accel_x;
    *y = (int8_t)-f->accel_y;
}

/* 0f27 for one axis: steering builds up from the input, or returns to centre by 3 a frame */
static int8_t steer_axis(const ep_flight *f, int8_t in, int8_t *acc)
{
    int8_t v;
    if (in == 0) {
        v = *acc;
        if (f->opt_self_centre == 1 && v) {
            for (int k = 0; k < 3 && v; k++) v = (int8_t)(v > 0 ? v - 1 : v + 1);
        }
        return v;
    }
    if (f->opt_reverse_stop == 1 && *acc && ((in ^ *acc) & 0x80)) {
        *acc = 0;
        return 0;
    }
    v = (int8_t)(in + *acc);
    if (v >= 24) v = 23;
    if (v <= -24) v = -23;
    return v;
}

/* 0f27: steering from the keyboard (joystick and mouse are not reconstructed yet) */
static uint16_t steering(ep_game *g)
{
    ep_flight *f = &g->f;
    if (g->in.control != 0) {
        ep_event_add(g, EP_EV_UNPORTED, 0x0fee);
        return (uint16_t)((uint8_t)f->pitch << 8 | (uint8_t)f->roll);
    }
    int8_t x, y;
    arrows(g, &x, &y);
    int8_t r = steer_axis(f, x, &f->roll);
    int8_t p = steer_axis(f, y, &f->pitch);
    f->roll = r;
    f->pitch = p;
    return (uint16_t)((uint8_t)p << 8 | (uint8_t)r);
}

/* 6d49.. : rotate a pair by rotation slot n */
static void rot(ep_game *g, int n, int16_t *a, int16_t *b) { ep_rotate_pair(&g->space.rot[n], a, b); }

/* 6dd9: (x, y, z) by slots 3, 2, 1, 0 */
static void rotate_back(ep_game *g, int16_t *x, int16_t *y, int16_t *z)
{
    rot(g, 3, y, z);
    rot(g, 2, x, y);
    rot(g, 1, x, z);
    rot(g, 0, y, z);
}

static void set_slot(ep_game *g, int n, uint16_t angle) { g->space.rot[n] = ep_rot_from_angle(angle); }

/* a768: velocity from the attitude and speed, when something changed */
static void velocity(ep_game *g)
{
    ep_flight *f = &g->f;
    if (f->moved != 1) return;
    f->moved = 0;
    set_slot(g, 4, (uint16_t)(0u - (uint16_t)(g->space.player_angle[0] + 0x400)));
    set_slot(g, 3, (uint16_t)(g->space.player_angle[1] + 0x400));
    int16_t a = 0, b = (int16_t)f->speed;
    if (f->jump_speed) b = (int16_t)(uint16_t)((uint16_t)b << 5);
    rot(g, 3, &a, &b);
    f->velocity[0] = a;
    a = 0;
    rot(g, 4, &a, &b);
    f->velocity[1] = a;
    f->velocity[2] = b;
}

/* a7b1: everything moves by minus the velocity (24-bit positions) */
static void move_objects(ep_game *g)
{
    int n = g->space.count < EP_OBJECTS ? g->space.count : EP_OBJECTS;
    for (int i = 0; i < n; i++) {
        ep_object *o = &g->space.obj[i];
        for (int k = 0; k < 3; k++) {
            uint32_t p = (uint32_t)o->b[EP_OBJ_POS_HI + k] << 16 | (uint32_t)o->b[EP_OBJ_POS + 2 * k] |
                         (uint32_t)o->b[EP_OBJ_POS + 2 * k + 1] << 8;
            int32_t v = g->f.velocity[k];
            p = (p + (uint32_t)(0 - v)) & 0xffffffu;
            o->b[EP_OBJ_POS + 2 * k] = (uint8_t)p;
            o->b[EP_OBJ_POS + 2 * k + 1] = (uint8_t)(p >> 8);
            o->b[EP_OBJ_POS_HI + k] = (uint8_t)(p >> 16);
        }
    }
}

void ep_controls(ep_game *g)
{
    ep_flight *f = &g->f;
    ep_space *s = &g->space;
    if (f->scoop_lock && f->scoop_lock != 0x3c) return;
    if (f->no_crash) {
        move_objects(g);
        return;
    }
    uint16_t ax;
    if (f->autopilot) {
        ep_event_add(g, EP_EV_UNPORTED, 0xa7de); /* docking computer */
        ax = f->autopilot_in;
    } else {
        uint8_t sp = (uint8_t)f->speed;
        if (key_down(g, g->in.faster)) {
            sp = (uint8_t)(sp + 4);
            f->moved = 1;
            if (sp >= 0x31) sp = 0x30;
        }
        if (key_down(g, g->in.slower)) {
            sp = (uint8_t)(sp - 4);
            f->moved = 1;
            if (sp < 4) sp = 4;
        }
        f->speed = (uint16_t)((f->speed & 0xff00) | sp);
        ax = steering(g);
        f->steer = ax;
        int8_t al = (int8_t)ax, ah = (int8_t)(ax >> 8); /* af49: inverted controls */
        if (f->opt_invert_pitch == 1) ah = (int8_t)-ah;
        if (f->opt_invert_both == 1) {
            al = (int8_t)-al;
            ah = (int8_t)-ah;
        }
        ax = (uint16_t)((uint8_t)ah << 8 | (uint8_t)al);
    }
    /* a6a1: roll straight onto the third angle */
    int8_t pitch = (int8_t)(ax >> 8), roll = (int8_t)-(int8_t)ax;
    if (roll) {
        if (roll <= -24) roll = -23;
        if (roll >= 24) roll = 23;
        s->player_angle[2] = (uint16_t)(s->player_angle[2] + roll * 2);
    }
    if (pitch) {
        /* pitch: turn a point ahead and one above by the pitch and the current attitude,
         * and read the three angles back */
        set_slot(g, 3, (uint16_t)(-(int)pitch * 2));
        set_slot(g, 2, (uint16_t)(0u - s->player_angle[2]));
        set_slot(g, 1, (uint16_t)(0u - s->player_angle[1]));
        set_slot(g, 0, (uint16_t)(0u - s->player_angle[0]));
        int16_t x = 0, y = 0, z = 10000;
        rotate_back(g, &x, &y, &z);
        uint16_t a = ep_atan2(y, z);
        f->pitch_angle[0] = (uint16_t)(0u - a);
        set_slot(g, 4, a);
        int16_t y2 = y, z2 = z;
        rot(g, 4, &y2, &z2);
        f->pitch_angle[1] = (uint16_t)(0u - ep_atan2(x, z2));
        set_slot(g, 4, (uint16_t)(0u - (uint16_t)(f->pitch_angle[0] + 0x400)));
        x = 0;
        y = (int16_t)0xd8f0;
        z = 0;
        rotate_back(g, &x, &y, &z);
        int16_t y3 = y, z3 = z;
        rot(g, 4, &y3, &z3);
        set_slot(g, 3, (uint16_t)(f->pitch_angle[1] + 0x400));
        int16_t x3 = x;
        rot(g, 3, &x3, &z3);
        s->player_angle[2] = ep_atan2(x3, y3);
        s->player_angle[1] = (uint16_t)(0u - f->pitch_angle[1]);
        s->player_angle[0] = (uint16_t)(0u - f->pitch_angle[0]);
        f->moved = 1;
    }
    if (f->autopilot == 1) return;
    velocity(g);
    move_objects(g);
}

/* 6a72: an approximate |(x, y, z)| (integer square root of the high bits of the sum of
 * squares, scaled back) */
static uint16_t magnitude(int16_t x, int16_t y, int16_t z)
{
    uint32_t s = (uint32_t)((int32_t)x * x) + (uint32_t)((int32_t)y * y) + (uint32_t)((int32_t)z * z);
    uint16_t d;
    int shift;
    if (s >> 24) {
        d = (uint16_t)(s >> 16);
        shift = 8;
    } else if ((s >> 16) & 0xff) {
        d = (uint16_t)(s >> 8);
        shift = 4;
    } else {
        d = (uint16_t)s;
        shift = 0;
    }
    uint16_t odd = 0xffff;
    uint8_t n = 0xff;
    for (;;) {
        odd = (uint16_t)(odd + 2);
        n++;
        if (d < odd) break;
        d = (uint16_t)(d - odd);
    }
    return (uint16_t)(n << shift);
}

/* 6a45: near the station (slot 2): bit 0 of ds:7680; the other bits keep what the original
 * computed on the way */
static void station_zone(ep_game *g)
{
    const ep_object *o = &g->space.obj[2];
    uint8_t t = (o->b[EP_OBJ_FLAGS] >> 1) & 0x1f;
    if (!(o->b[EP_OBJ_FLAGS] & 1) || t > 1) {
        g->f.safe_zone = 0;
        return;
    }
    for (int k = 0; k < 3; k++) { /* 4217: the 24-bit position fits 16 bits */
        uint8_t hi = o->b[EP_OBJ_POS_HI + k];
        int neg = o->b[EP_OBJ_POS + 2 * k + 1] & 0x80;
        if ((hi == 0 && !neg) || (hi == 0xff && neg)) continue;
        uint8_t al = (hi == 0 || hi == 0xff) ? 0 : hi;
        g->f.safe_zone = (uint8_t)(al << 1);
        return;
    }
    uint16_t m = magnitude((int16_t)(o->b[4] | o->b[5] << 8), (int16_t)(o->b[6] | o->b[7] << 8),
                           (int16_t)(o->b[8] | o->b[9] << 8));
    g->f.safe_zone = (uint8_t)((uint8_t)(m << 1) | (m < 0x32c8));
}

void ep_dashboard_tick(ep_game *g)
{
    ep_flight *f = &g->f;
    /* 585c: condition */
    uint8_t st = 0;
    if (f->energy >= 0x100 && f->sun_size < 0xe0 && f->altitude >= 0x20) {
        st = 2;
        if (f->fore_shield && f->aft_shield && f->energy >= 0x200 && f->sun_size < 0xc0 &&
            f->altitude >= 0x28) {
            st = 3;
            if (f->sun_size < 0x80 && f->altitude >= 0x80 && f->aft_shield >= 0x80 &&
                f->fore_shield >= 0x80 && f->energy >= 0x300)
                st = 1;
        }
    }
    f->status = st;
    /* 579d: cooling, recharging, losing equipment on low energy */
    if (!f->scoop_lock && !f->no_crash) {
        f->laser_temp = f->laser_temp >= 2 ? (uint8_t)(f->laser_temp - 2) : 0;
        if (f->energy == 0x3ff) {
            if (f->aft_shield != 0xff) f->aft_shield++;
            if (f->fore_shield != 0xff) f->fore_shield++;
        } else {
            uint8_t unit = (uint8_t)((uint8_t)(g->cmdr.b[EP_CMDR_EQUIPMENT + 8] << 1) + 1);
            f->energy = (uint16_t)(f->energy + (int8_t)unit);
            if (f->energy >= 0x400) f->energy = 0x3ff;
        }
        if (f->energy < 0x100 && ep_flight_random(g) < 0x32) {
            uint8_t k = (uint8_t)((ep_flight_random(g) & 0xff) / 20);
            uint8_t *item = &g->cmdr.b[EP_CMDR_EQUIPMENT + k];
            if (*item) {
                (*item)--;
                f->message = 0x54e2;
                f->message_time = 0x28;
            }
        }
    }
    station_zone(g);
    if (f->ecm_shown == 1) f->ecm_shown = 0;
}

static uint16_t main_hi(ep_game *g) { return (uint16_t)(ep_rng_step(&g->rng) >> 16); }

static uint16_t w16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

void ep_tribbles_tick(ep_game *g)
{
    ep_flight *f = &g->f;
    if (!f->tribbles) return;
    if (f->tribbles == 1) {
        if (main_hi(g) > 0xf) return;
        f->tribbles++;
        f->tribbles_shown = 0;
    }
    uint16_t h = main_hi(g);
    uint16_t lim = f->tribbles < 15    ? 250
                   : f->tribbles < 30  ? 500
                   : f->tribbles < 80  ? 750
                   : f->tribbles < 125 ? 2000
                                       : 10000;
    if (h <= lim && f->tribbles <= 0x98c9) f->tribbles++;
    if (f->tribbles >= 0x5f) { /* they eat the cargo */
        uint32_t r = ep_rng_step(&g->rng);
        if ((r >> 16) <= 0xfa0) {
            unsigned b = r & 0x1e;
            uint8_t *c = &g->cmdr.b[EP_CMDR_CARGO + b];
            if (*c) {
                (*c)--;
                f->tribbles++;
                if (b < 0x1a) g->cmdr.b[EP_CMDR_CARGO_USED]--;
            }
        }
    }
    if (f->tribbles <= 0x2ab) return;
    if (!f->tribbles_shown) {
        f->tribbles_shown = 1;
        f->tribble_sprites = 0;
    }
    uint16_t h1 = main_hi(g), h2 = main_hi(g);
    if ((h1 >> 8) < 0xa) {
        if (f->tribble_sprites == 0x40) f->tribble_sprites--;
        uint16_t x = (uint16_t)((h1 & 0xff) + ((h2 >> 8) & 0x3f)), y = h2 & 0xff;
        if (x >= 8 && x < 0x128 && y < 0xbd) {
            uint16_t v = 0;
            if (y >= 9 && y < 0x7a) {
                v = main_hi(g) & 7;
                if (v >= 3) v = (uint16_t)(v - 5);
            }
            uint8_t *t = f->tribble[f->tribble_sprites & 63];
            put16(t, x);
            put16(t + 2, y);
            put16(t + 4, v);
            f->tribble_sprites++;
        }
    }
    for (unsigned k = 0; k < f->tribble_sprites && k < 64; k++) {
        uint8_t *t = f->tribble[k];
        uint16_t x = w16(t), dx = w16(t + 4);
        uint8_t sprite = 0x5d;
        if (dx) {
            if (!(dx & 0x8000)) sprite++;
            uint16_t nx = (uint16_t)(x + dx);
            if (nx < 8 || nx >= 0x128) { /* bounce */
                put16(t + 4, (uint16_t)(0u - dx));
                nx = x;
            }
            x = nx;
            put16(t, x);
        }
        ep_render_sprite(&g->render, sprite, (int16_t)x, (int16_t)w16(t + 2));
    }
}

static int fits16(const ep_object *o)
{
    for (int k = 0; k < 3; k++) {
        uint8_t hi = o->b[EP_OBJ_POS_HI + k];
        int neg = o->b[EP_OBJ_POS + 2 * k + 1] & 0x80;
        if (!((hi == 0 && !neg) || (hi == 0xff && neg))) return 0;
    }
    return 1;
}

int ep_mass_locked(const ep_game *g)
{
    if (g->f.safe_zone & 1) return 1;
    if (fits16(&g->space.obj[0]) || fits16(&g->space.obj[1])) return 1;
    uint8_t n = g->space.count;
    if (n < 3) return 1; /* sub cl,3 borrows: CF */
    if ((int8_t)(n - 3) <= 0) return 0;
    for (int i = 3; i < 3 + (uint8_t)(n - 3) && i < EP_OBJECTS; i++) {
        const ep_object *o = &g->space.obj[i];
        if (!(o->b[EP_OBJ_FLAGS] & 1)) continue;
        int t = (o->b[EP_OBJ_FLAGS] >> 1) & 0x1f;
        if (t == 5 || t == 17 || t == 6 || t == 11) continue;
        if (o->b[EP_OBJ_FLAGS1E] & 2) return 1;
    }
    return 0;
}

void ep_jump_drive(ep_game *g)
{
    ep_flight *f = &g->f;
    if (!f->jump_speed) return;
    uint16_t text;
    if (f->autopilot == 1 || (f->speed == 0x30 && ep_mass_locked(g))) {
        f->jump_speed = 0;
        text = 0xb030; /* Mass-Locked */
    } else if (f->speed != 0x30) {
        f->jump_speed = 0;
        text = 0xb04a; /* Velocity-Locked */
    } else {
        if (f->jump_new) {
            ep_event_add(g, EP_EV_SOUND, 0x0c); /* 4e98 */
            f->jump_new = 0;
        }
        text = 0xb01d; /* Engaged */
    }
    f->message = text;
    f->message_time = 0x19;
    f->moved = 1;
}
