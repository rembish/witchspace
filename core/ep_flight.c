/* Elite Plus flight loop subsystems, reconstructed from ELITE.EXE (see ep_flight.h). */
#include "ep_flight.h"

#include "ep_sound.h"
#include "ep_dsmap.h"

#include "ep_combat.h"
#include "ep_dust.h"

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
        if (k) ep_sound(g, 0x0b); /* 4d9f */
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
    if (f->message == f->message_shown) return;
    f->message_shown = f->message;
    /* 7053: the message line: the bar under it (30d2), the text centred and shadowed (2fca) */
    ep_render_sprite(&g->render, 0x6c, 0, 0);
    uint8_t t[128];
    int n = ep_ds_text(g, f->message, t, sizeof t);
    ep_render_text(&g->render, 0x0f, (int16_t)(0xa0 - (ep_text_width(t) >> 1)), 0, t, n, 1);
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

/* the byte a binding points at (ds:020d + binding: the key table; none points at ds:ffff) */
static uint8_t bound_key(const ep_game *g, uint16_t binding)
{
    uint16_t a = (uint16_t)(0x20d + binding);
    return a >= 0x20d && a < 0x28d ? g->in.key[a - 0x20d] : ep_ds_byte(g, a);
}

/* 0edf: the fire control: the key; a joystick's buttons (port 201h, 0 when pressed); a mouse's
 * buttons, or keys 7dh, 7eh */
static int fire_pressed(ep_game *g)
{
    const ep_input *in = &g->in;
    if (in->control == 0) return !(bound_key(g, in->fire) & 0x80);
    if (in->control == 1) {
        uint8_t b = in->joy_present ? in->joy_buttons : 0xff;
        return !(b & 0x20) || !(b & 0x10);
    }
    if (!in->key[0x7e] || !in->key[0x7d]) return 1;
    return (in->mouse_buttons & 3) != 0;
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

static int key_down(const ep_game *g, uint16_t binding) { return bound_key(g, binding) == 0; }

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

/* 1038: one axis of the joystick: its way from the centre times 256 over the centre, halved,
 * at most 127, an eighth of that less a dead zone of 4; -1 if the division faults (the
 * original then returns with the dividend's low word, *fault) */
static int joy_axis(uint16_t v, uint16_t centre, uint8_t *out, uint16_t *fault)
{
    uint16_t d = (uint16_t)(v - centre);
    int neg = (int16_t)d < 0;
    if (neg) d = (uint16_t)(0u - d);
    uint32_t n = (uint32_t)(d >> 8 & 0xff) << 16 | (uint16_t)(d << 8);
    if (!centre || n / centre > 0xffff) {
        *fault = (uint16_t)(d << 8);
        return -1;
    }
    uint16_t q = (uint16_t)(n / centre) >> 1;
    uint8_t a = q >= 0x80 ? 0x7f : (uint8_t)q;
    a = (uint8_t)(a >> 3);
    a = a >= 4 ? (uint8_t)(a - 4) : 0;
    *out = neg ? (uint8_t)(0u - a) : a;
    return 0;
}

static int8_t clamp23(int8_t v) { return v < -0x17 ? -0x17 : v > 0x17 ? 0x17 : v; }

/* 1038: the joystick (0ffb counts it; no joystick counts 0, 0): AL roll, AH pitch, +-23 */
uint16_t ep_joystick_steering(ep_game *g)
{
    const ep_input *in = &g->in;
    uint16_t x = in->joy_present ? in->joy_x : 0, y = in->joy_present ? in->joy_y : 0, fault;
    uint8_t ax, ay;
    if (joy_axis(x, in->joy_centre_x, &ax, &fault) < 0) return fault; /* 10e9 */
    if (joy_axis(y, in->joy_centre_y, &ay, &fault) < 0) return fault;
    int8_t r = clamp23((int8_t)ax), p = clamp23((int8_t)(0u - ay));
    return (uint16_t)((uint8_t)p << 8 | (uint8_t)r);
}

/* int 33h, 0bh: the mickeys since the last time */
static void mickeys(ep_game *g, int16_t *x, int16_t *y)
{
    *x = g->in.mouse_dx;
    *y = g->in.mouse_dy;
    g->in.mouse_dx = g->in.mouse_dy = 0;
}

/* 1164: an eighth of the mickeys, at most 63 */
static int8_t mouse_axis(int16_t v)
{
    uint16_t a = (uint16_t)(v < 0 ? 0u - (uint16_t)v : (uint16_t)v) >> 3;
    uint8_t b = a >= 0x40 ? 0x3f : (uint8_t)a;
    return (int8_t)(v < 0 ? (uint8_t)(0u - b) : b);
}

/* 11f0: one from nothing is nothing */
static int8_t mouse_snap(int8_t v) { return v == 1 || v == -1 ? 0 : v; }

static uint16_t mouse_steering(ep_game *g)
{
    int16_t mx, my;
    mickeys(g, &mx, &my);
    int8_t r = mouse_axis(mx), p = mouse_axis(my);
    p = (int8_t)(0u - (uint8_t)p);
    r = (int8_t)(r >> 1);
    p = (int8_t)(p >> 1);
    r = clamp23((int8_t)(r + (int8_t)g->f.steer));
    p = clamp23((int8_t)(p + (int8_t)(g->f.steer >> 8)));
    return (uint16_t)((uint8_t)mouse_snap(p) << 8 | (uint8_t)mouse_snap(r));
}

uint16_t ep_steering(ep_game *g)
{
    ep_flight *f = &g->f;
    if (g->in.control != 0) /* 0fee */
        return g->in.control == 1 ? ep_joystick_steering(g) : mouse_steering(g);
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
void ep_player_velocity(ep_game *g)
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
void ep_player_move(ep_game *g)
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

static void autopilot(ep_game *g);

void ep_controls(ep_game *g)
{
    ep_flight *f = &g->f;
    ep_space *s = &g->space;
    if (f->scoop_lock && f->scoop_lock != 0x3c) return;
    if (f->no_crash) {
        ep_player_move(g);
        return;
    }
    uint16_t ax;
    if (f->autopilot) {
        autopilot(g);
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
        ax = ep_steering(g);
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
        y = (int16_t)0xd8f0; /* -10000: the point above */
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
    ep_player_velocity(g);
    ep_player_move(g);
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
    /* the square root by counting the odd numbers 1, 3, 5.. that can be taken away (their
     * sums are the squares); dropping 16 bits of the square drops 8 of the root */
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

/* 67eb: two angles (2048 a turn) within tol of each other */
static int angle_close(uint16_t a, uint16_t c, uint16_t tol)
{
    int16_t d = (int16_t)(((a & 0x7ff) - (c & 0x7ff)) & 0x7ff);
    if (d & 0x400) d = (int16_t)(d | (int16_t)0xf800);
    return ep_abs16((uint16_t)d) < tol;
}

/* |angle| of an 11-bit signed angle */
static uint16_t angle_abs(uint16_t a)
{
    if (a & 0x400) a = (uint16_t)(0u - (uint16_t)(a | 0xf800));
    return a;
}

static void ap_steer(ep_game *g, uint16_t v)
{
    g->f.autopilot_in = v;
    g->f.steer = v;
}

/* the station (slot 2), the docking point 2000 in front of its slot when `ahead`, as the
 * player sees it (6e01 with the rotation slots as they are) */
static void station_seen(ep_game *g, int ahead, int16_t p[3])
{
    const ep_object *o = &g->space.obj[2];
    for (int k = 0; k < 3; k++)
        p[k] = (int16_t)(o->b[EP_OBJ_POS + 2 * k] | o->b[EP_OBJ_POS + 2 * k + 1] << 8);
    if (ahead) p[2] = (int16_t)(uint16_t)((uint16_t)p[2] + 0x7d0);
    ep_rotate_by_player(&g->space, p);
}

/* abbe, then the roll that puts the station straight above or below (a815, a9c6) */
static void ap_choose_roll(ep_game *g, int ahead)
{
    ep_space *s = &g->space;
    for (int k = 0; k < 3; k++) s->rot[k] = ep_rot_from_angle(s->player_angle[k]);
    int16_t p[3];
    station_seen(g, ahead, p);
    uint16_t t = ep_atan2((int16_t)(p[0] >> 2), (int16_t)(p[1] >> 2));
    if (angle_abs(t) >= 0x200) t = (uint16_t)(t + 0x400);
    g->f.ap_roll = (uint16_t)((t + s->player_angle[2]) & 0x7ff);
}

/* a861, aa0e: roll onto ap_roll */
static int ap_roll_to(ep_game *g)
{
    ep_flight *f = &g->f;
    uint16_t *roll = &g->space.player_angle[2];
    if (angle_close(*roll, f->ap_roll, 0x13)) {
        uint16_t c = f->ap_roll & 0x7ff; /* as 67eb leaves cx: sign-extended from 11 bits */
        *roll = (c & 0x400) ? (uint16_t)(c | 0xf800) : c;
        ap_steer(g, 0);
        return 1;
    }
    uint8_t al = 0xf7;
    if ((uint16_t)(f->ap_roll - *roll) & 0x400) al = 9;
    ap_steer(g, al);
    return 0;
}

/* a8a6, aa53: pitch until the point is straight ahead; 1 once it is */
static int ap_pitch_to(ep_game *g, int ahead, uint16_t tol, uint8_t rate)
{
    if (!ahead) /* abbe */
        for (int k = 0; k < 3; k++) g->space.rot[k] = ep_rot_from_angle(g->space.player_angle[k]);
    int16_t p[3];
    station_seen(g, ahead, p);
    uint16_t t = ep_atan2((int16_t)(p[1] >> 2), (int16_t)(p[2] >> 2));
    if (!angle_close(t, 0, tol)) {
        uint8_t al = (t & 0x400) ? (uint8_t)-rate : rate;
        ap_steer(g, (uint16_t)(al << 8));
        return 0;
    }
    ap_steer(g, (uint16_t)((uint8_t)((int16_t)t >> 1) << 8));
    return 1;
}

static void ap_speed_up_or_down(ep_flight *f, int up)
{
    if (up) {
        f->speed = (uint16_t)(f->speed + 4);
        if (f->speed >= 0x31) f->speed = 0x30;
    } else {
        f->speed = (uint16_t)(f->speed - 4);
        if (!f->speed) f->speed = 4;
    }
}

/* a7de: the docking computer: stop, roll and pitch to face the point in front of the slot
 * (twice), fly there, line up on the slot (twice), fly in matching the station's roll */
static void autopilot(ep_game *g)
{
    ep_flight *f = &g->f;
    ep_object *st = &g->space.obj[2];
    ap_steer(g, 0);
    switch (f->autopilot_step) {
    case 0:
        if (f->speed) {
            f->speed = (uint16_t)(f->speed - 4);
            f->moved = 1;
            return;
        }
        f->autopilot_step = 1;
        f->ap_passes = 0;
        return;
    case 1:
        ap_choose_roll(g, 1);
        f->autopilot_step = 2;
        return;
    case 2:
        if (ap_roll_to(g)) f->autopilot_step = 3;
        return;
    case 3:
        if (!ap_pitch_to(g, 1, 0x0f, 6)) return;
        if (f->ap_passes != 1) {
            f->ap_passes++;
            f->autopilot_step = 1;
            return;
        }
        f->autopilot_step = 4;
        f->speed = 4;
        f->moved = 1;
        return;
    case 4: {
        int16_t p[3];
        for (int k = 0; k < 3; k++)
            p[k] = (int16_t)(st->b[EP_OBJ_POS + 2 * k] | st->b[EP_OBJ_POS + 2 * k + 1] << 8);
        p[2] = (int16_t)(uint16_t)((uint16_t)p[2] + 0x7d0);
        uint16_t m = magnitude(p[0], p[1], p[2]);
        ap_speed_up_or_down(f, m >= 0x15e);
        uint16_t q = f->speed ? (uint16_t)(m / f->speed) : 0;
        if (q == 1) { /* there: the last step exactly onto the point */
            f->speed = 0;
            f->moved = 1;
            for (int k = 0; k < 3; k++) f->velocity[k] = p[k];
            ep_player_move(g);
            f->autopilot_step = 5;
            f->ap_passes = 0;
            return;
        }
        if (!q) q = 1; /* the original's divide error (closer than one step): re/FLIGHT.md */
        for (int k = 0; k < 3; k++) f->velocity[k] = (int16_t)(p[k] / (int16_t)q);
        ep_player_move(g);
        return;
    }
    case 5:
        ap_choose_roll(g, 0);
        f->autopilot_step = 6;
        return;
    case 6:
        if (ap_roll_to(g)) f->autopilot_step = 7;
        return;
    case 7:
        if (!ap_pitch_to(g, 0, 9, 3)) return;
        if (f->ap_passes != 1) {
            f->ap_passes++;
            f->autopilot_step = 5;
            return;
        }
        f->autopilot_step = 8;
        f->speed = 4;
        f->moved = 1;
        return;
    case 8: {
        uint16_t z = (uint16_t)(0u - (uint16_t)(st->b[EP_OBJ_POS + 4] | st->b[EP_OBJ_POS + 5] << 8));
        if (z < 0x28a) { /* at the slot: look ahead, match the roll */
            f->autopilot_step = 9;
            if (g->space.extra_angle) {
                g->space.extra_angle = 0;
                ep_sound(g, 4); /* 4e15 */
            }
            ep_dust_reset(g);
            f->ap_flag = 1;
            return;
        }
        ap_speed_up_or_down(f, z >= 0x3e8);
        f->velocity[0] = 0;
        f->velocity[1] = 0;
        f->velocity[2] = (int16_t)(uint16_t)(0u - f->speed);
        ep_player_move(g);
        f->moved = 1;
        return;
    }
    case 9: { /* the slot's roll matched either way up: 0ah as it is, 0bh half a turn round */
        ep_player_move(g);
        uint16_t sr = (uint16_t)(st->b[0x0e] | st->b[0x0f] << 8);
        if (angle_close(sr, g->space.player_angle[2], 0x0b))
            f->autopilot_step = 0x0a;
        else if (angle_close(sr, (uint16_t)(g->space.player_angle[2] + 0x400), 0x0b))
            f->autopilot_step = 0x0b;
        return;
    }
    case 0x0a:
    case 0x0b: {
        ep_player_move(g);
        uint16_t sr = (uint16_t)(st->b[0x0e] | st->b[0x0f] << 8);
        if (f->autopilot_step == 0x0b) sr = (uint16_t)(sr + 0x400);
        int16_t d = (int16_t)((sr & 0x7ff) - (g->space.player_angle[2] & 0x7ff));
        ap_steer(g, (uint8_t)-(uint8_t)(d >> 1));
        return;
    }
    default: return;
    }
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

/* the dashboard's gauges, redrawn when their value is not the one drawn (ds:54cc..54e1) */
static uint8_t *drawn(ep_game *g, uint16_t addr) { return &g->f.dash[addr - 0x54cc]; }

/* 567d (and 5685 with the value as it is, 55e7 two high): a bar of value x 12 / 63 in a field of
 * 48, the rest black */
static void gauge(ep_game *g, int16_t x, int16_t y, uint8_t colour, uint8_t value, int scale, int16_t h)
{
    int16_t w = scale ? (int16_t)(value * 12 / 0x3f) : value;
    if (w) ep_render_rect(&g->render, colour, x, y, w, h);
    if (w != 0x30) ep_render_rect(&g->render, 0, (int16_t)(x + w), y, (int16_t)(0x30 - w), h);
}

/* 561d: the roll or pitch marker, two wide, in a field of 48 */
static void marker(ep_game *g, int16_t x, int16_t y, uint8_t v)
{
    int8_t a = (int8_t)v;
    if (a < -0x17) a = -0x17;
    if (a > 0x17) a = 0x17;
    int16_t pos = (int16_t)(a + 0x17);
    if (pos) ep_render_rect(&g->render, 0, x, y, pos, 4);
    ep_render_rect(&g->render, 0x0f, (int16_t)(x + pos), y, 2, 4);
    if (pos != 0x2e) ep_render_rect(&g->render, 0, (int16_t)(x + pos + 2), y, (int16_t)(0x2e - pos), 4);
}

/* 56b3: the missiles (54h none, 55h one, 56h armed), the armed one blinking */
static void missiles(ep_game *g)
{
    ep_flight *f = &g->f;
    uint8_t n = g->cmdr.b[EP_CMDR_EQUIPMENT];
    if (*drawn(g, 0x54cc) != n) {
        *drawn(g, 0x54cc) = n;
        uint8_t left = n;
        for (int16_t x = 0x11b; x <= 0x133; x += 8, left--)
            ep_render_sprite(&g->render, (int8_t)left > 0 ? 0x55 : 0x54, x, 0xb3);
    }
    int16_t x = (int16_t)(((uint8_t)(n - 1)) * 8 + 0x11b); /* no missile left: far off to the right */
    if (*drawn(g, 0x54cd) != f->target_note) {
        *drawn(g, 0x54cd) = f->target_note;
        f->missile_blink = 0;
        if ((int8_t)(n - 1) >= 0) {
            ep_render_sprite(&g->render, f->target_note ? 0x56 : 0x55, x, 0xb3);
            return;
        }
    }
    if (f->target_note != 1) return; /* 5716 */
    uint8_t b = ++f->missile_blink;
    if (b == 4) {
        ep_render_sprite(&g->render, 0x54, x, 0xb3);
    } else if (b == 8) {
        f->missile_blink = 0;
        ep_render_sprite(&g->render, 0x56, x, 0xb3);
    }
}

/* 5751: the four energy banks, full ones first */
static void energy_banks(ep_game *g)
{
    uint16_t e = g->f.energy;
    uint8_t hi = (uint8_t)(e >> 8), lo = (uint8_t)e;
    uint16_t at = 0x54d5;
    int left = 4;
    while (left && hi) {
        *drawn(g, at--) = 0xff;
        hi--;
        left--;
    }
    if (left && !hi) {
        *drawn(g, at--) = lo;
        while (--left) *drawn(g, at--) = 0;
    }
    for (int k = 0; k < 4; k++) {
        uint8_t *was = drawn(g, (uint16_t)(0x54d1 - k)), want = *drawn(g, (uint16_t)(0x54d5 - k));
        if (want == *was) continue;
        *was = want;
        gauge(g, 0xe4, (int16_t)(0xc4 - 5 * k), 0x0a, want, 1, 2);
    }
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
    /* 582a: its light (none: blinking with the clock) */
    uint8_t light = st ? st : (uint8_t)(((uint8_t)g->clock >> 3) & 2);
    if (*drawn(g, 0x54e0) != light) {
        *drawn(g, 0x54e0) = light;
        ep_render_sprite(&g->render, (uint8_t)(5 + light), 0x9c, 0x96);
    }
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
    uint8_t s = (f->safe_zone & 1) ? 9 : 0x0b; /* 6a3f: the safe zone's light */
    if (*drawn(g, 0x54dc) != s) {
        *drawn(g, 0x54dc) = s;
        ep_render_sprite(&g->render, s, 0x12d, 0xbf);
    }
    s = 0x0b; /* the ECM's, once */
    if (f->ecm_shown == 1) {
        f->ecm_shown = 0;
        s = 0x0a;
    }
    if (*drawn(g, 0x54db) != s) {
        *drawn(g, 0x54db) = s;
        ep_render_sprite(&g->render, s, 0x125, 0xbf);
    }
    energy_banks(g);
    missiles(g);
    uint8_t pitch = (uint8_t)(f->steer >> 8), roll = (uint8_t)f->steer;
    if (*drawn(g, 0x54d7) != pitch) {
        *drawn(g, 0x54d7) = pitch;
        marker(g, 0xe4, 0xad, pitch);
    }
    if (*drawn(g, 0x54d6) != roll) {
        *drawn(g, 0x54d6) = roll;
        marker(g, 0xe4, 0xa6, roll);
    }
    static const struct {
        uint16_t was;
        int16_t y;
        uint8_t colour;
    } bars[] = { { 0x54dd, 0xbb, 0x0c }, { 0x54d8, 0xc2, 0x0f }, { 0x54d9, 0xb4, 0x0e },
                 { 0x54da, 0xad, 0x0a }, { 0x54de, 0x9f, 0x0d }, { 0x54df, 0xa6, 0x0d } };
    const uint8_t value[6] = { f->laser_temp,           f->altitude,    f->sun_size,
                               g->cmdr.b[EP_CMDR_FUEL], f->fore_shield, f->aft_shield };
    for (int k = 0; k < 6; k++) {
        if (*drawn(g, bars[k].was) == value[k]) continue;
        *drawn(g, bars[k].was) = value[k];
        gauge(g, 0x2c, bars[k].y, bars[k].colour, value[k], 1, 3);
    }
    uint8_t speed = (uint8_t)f->speed; /* 5685 */
    if (*drawn(g, 0x54e1) == speed) return;
    *drawn(g, 0x54e1) = speed;
    gauge(g, 0xe4, 0x9f, 0x0c, speed, 0, 3);
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
                if (b < 0x1a) g->cmdr.b[EP_CMDR_CARGO_USED]--; /* rows 0..12 are in tonnes */
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
            ep_sound(g, 0x0c); /* 4e98 */
            f->jump_new = 0;
        }
        text = 0xb01d; /* Engaged */
    }
    f->message = text;
    f->message_time = 0x19;
    f->moved = 1;
}
