/* Elite Plus commands, reconstructed from ELITE.EXE (see ep_commands.h). */
#include "ep_commands.h"

#include <string.h>

#include "ep_sound.h"
#include "ep_dsmap.h"

#include "ep_chart.h"
#include "ep_combat.h"
#include "ep_dust.h"
#include "ep_flight.h"
#include "ep_ships.h"
#include "ep_station.h"
#include "ep_tables.h"
#include "ep_trade.h"
#include "ep_travel.h"

static uint8_t *equip(ep_game *g, int k) { return &g->cmdr.b[EP_CMDR_EQUIPMENT + k]; }

static void message(ep_game *g, uint16_t text, uint16_t time)
{
    g->f.message = text;
    g->f.message_time = time;
}

static void sound(ep_game *g, uint8_t n) { ep_sound(g, n); }

/* 4e09: the beep (a different sound on device 2) */
static void beep(ep_game *g) { sound(g, g->f.sound_device == 2 ? 0x88 : 4); }

static int safe_zone(const ep_game *g) { return g->f.safe_zone & 1; }

/* ---- the bar ---- */

/* 04b2: in flight, by the equipment fitted */
static void flight_bar(ep_game *g)
{
    ep_flight *f = &g->f;
    uint8_t *w = f->bar_wanted;
    if (f->hyper_countdown || f->no_crash) {
        w[0] = w[1] = w[2] = w[3] = w[4] = w[10] = 0;
        return;
    }
    if (*equip(g, 0x0d) == 1) w[7] = 0x23;  /* masking device */
    if (g->cmdr.b[0xd1] == 1) w[11] = 0x24; /* ds:83ac: anti-ECM */
    if (*equip(g, 0)) {                     /* missiles */
        w[6] = 7;
        if (f->target_note == 2) w[7] = 8;
    }
    if (*equip(g, 6)) w[9] = 0x0a;   /* escape capsule */
    if (*equip(g, 2) == 1) w[5] = 6; /* ECM */
    if (*equip(g, 7) == 1) w[8] = 9; /* energy bomb */
}

/* 0534: docked screen 1 */
static void screen1_bar(ep_game *g)
{
    ep_flight *f = &g->f;
    uint8_t *w = f->bar_wanted;
    if (f->screen_flag) {
        w[6] = 0;
        w[7] = 0x10;
        w[8] = 0x11;
        w[9] = 0x12;
    }
    if (f->screen_bits & 1) w[8] = 0x0c;
    if (f->screen_bits & 2) w[9] = 0x20;
    if (f->screen_bits & 4) w[8] = 0x21;
    if (f->screen_bits & 8) w[9] = 0x22;
}

/* 0580: docked screen 2 */
static void screen2_bar(ep_game *g)
{
    ep_flight *f = &g->f;
    uint8_t *w = f->bar_wanted;
    if (f->screen_flag) {
        w[7] = 0x10;
        w[8] = 0x11;
        w[9] = 0x12;
    }
    if (*equip(g, 10) == 1 && !f->hyper_countdown) w[6] = 0x0f; /* galactic hyperdrive */
}

void ep_key_bar(ep_game *g)
{
    ep_flight *f = &g->f;
    f->bar_redraw = 0;
    if (f->screen != f->screen_shown) {
        f->screen_shown = f->screen;
        f->bar_redraw++;
        f->bar_colour = ep_bar_colour[f->other_screen < 3 ? f->other_screen : 0];
        memset(f->bar_active, 0xff, sizeof f->bar_active);
    }
    int row = (int8_t)f->screen_shown;
    if (row < 0 || row > 5) {
        ep_event_add(g, EP_EV_UNPORTED, 0x02e8); /* a row outside the table */
        return;
    }
    memcpy(f->bar_wanted, ep_key_rows[row], sizeof f->bar_wanted);
    switch (row) {
    case 0: flight_bar(g); break;
    case 1: screen1_bar(g); break;
    case 2: screen2_bar(g); break;
    case 5: /* 05ac: the title's, where the protection's ret is looked for (else 00ba: to DOS) */
        if (f->protection_failed) {
            f->leave = 2;
            return;
        }
        if (f->bar_quiet) { /* the credits: only some keys */
            static const uint8_t quiet[6] = { 0, 1, 2, 7, 8, 11 };
            for (int k = 0; k < 6; k++) f->bar_wanted[quiet[k]] = 0;
        }
        break;
    default: break; /* 05aa, 05ab: nothing */
    }
    for (int k = 0; k < 12; k++) {
        if (f->bar_wanted[k] == f->bar_active[k]) continue;
        f->bar_active[k] = f->bar_wanted[k];
        uint8_t id = f->bar_wanted[k];
        ep_event_add(g, EP_EV_ICON,
                     (uint16_t)(k << 8 | (id < sizeof ep_icon_sprite ? ep_icon_sprite[id] : 0)));
    }
    /* 0323: on the options' screens, marks by what is chosen */
    if (!f->bar_redraw || f->bar_quiet) return;
    uint8_t s = f->screen_shown;
    if (s < 3 || s > 5) return;
    uint8_t c = g->in.control < 3 ? g->in.control : 0;
    ep_render_sprite(&g->render, 0x32, ep_ds_byte(g, (uint16_t)(0x03e3 + c)),
                     (int16_t)(uint8_t)(ep_ds_byte(g, (uint16_t)(0x03e6 + c)) + f->bar_colour));
    if (s != 5) {
        const uint8_t opt[4] = { f->opt_reverse_stop, f->opt_self_centre, f->opt_invert_pitch,
                                 f->opt_invert_both };
        for (int k = 0; k < 4; k++)
            if (opt[k] & 1)
                ep_render_sprite(
                    &g->render, 0x32, ep_ds_byte(g, (uint16_t)(0x03e9 + 2 * k)),
                    (int16_t)(uint8_t)(ep_ds_byte(g, (uint16_t)(0x03ea + 2 * k)) + f->bar_colour));
    }
    if (f->sound_off) ep_render_sprite(&g->render, 0x36, 0xda, (int16_t)(uint8_t)(3 + f->bar_colour));
    /* 03ad: the protection has put a ret here */
}

/* ---- slots and launches ---- */

static void set16(ep_object *o, int off, uint16_t v)
{
    o->b[off] = (uint8_t)v;
    o->b[off + 1] = (uint8_t)(v >> 8);
}

static uint16_t get16(const ep_object *o, int off) { return (uint16_t)(o->b[off] | o->b[off + 1] << 8); }

void ep_launch_missile(ep_game *g)
{
    ep_space *s = &g->space;
    int taken;
    ep_object *m = ep_claim_slot_how(g, &taken);
    /* 8103, 81b7: the new slot gets the 64 bytes DI points at, as the code before the commands
     * left it. MCGA: the frame's flip (3081..30c2) leaves ds:d828, memory nothing writes (0).
     * EGA, VGA: the flip leaves DI alone: slot 20 as the collision loop stops (or, with
     * Tribble sprites on screen, their table: not reproduced). A slot taken at random is
     * copied onto itself. */
    if (!taken) {
        if (g->f.video == 2)
            memset(m->b, 0, sizeof m->b);
        else if (m != &s->obj[20])
            memcpy(m->b, s->obj[20].b, sizeof m->b);
    }
    ep_ship_init(m, 0);
    m->b[0x33] = 2;
    /* the player's rotation undone (angles negated, slots applied in reverse) to bring the
     * point from the ship's frame into space */
    for (int k = 0; k < 3; k++) s->rot[k] = ep_rot_from_angle((uint16_t)(0u - s->player_angle[k]));
    int16_t x = 0, y = 100, z = 0; /* 6dc9: below the ship, into space */
    ep_rotate_pair(&s->rot[2], &x, &y);
    ep_rotate_pair(&s->rot[1], &x, &z);
    ep_rotate_pair(&s->rot[0], &y, &z);
    const int16_t p[3] = { x, y, z };
    for (int k = 0; k < 3; k++) {
        set16(m, EP_OBJ_POS + 2 * k, (uint16_t)p[k]);
        m->b[EP_OBJ_POS_HI + k] = p[k] < 0 ? 0xff : 0;
    }
    uint16_t t = g->f.target_slot;
    set16(m, 0x29, t);
    const ep_object *to = &s->obj[(uint16_t)(t - 0x76de) / 0x40 % EP_OBJECTS];
    uint16_t a, c;
    ep_aim(g, (int16_t)get16(to, EP_OBJ_POS), (int16_t)get16(to, EP_OBJ_POS + 2),
           (int16_t)get16(to, EP_OBJ_POS + 4), &a, &c);
    set16(m, 0x0a, a);
    set16(m, 0x0c, c);
    ep_ship_velocity(g, m);
    ep_ship_move(m);
    ep_ship_move(m);
}

void ep_escape_capsule(ep_game *g)
{
    ep_flight *f = &g->f;
    f->hyper_countdown = 0;
    f->no_crash = 0x64;
    ep_object *hulk = ep_claim_slot(g);
    memset(hulk->b, 0, sizeof hulk->b); /* 6bbe */
    ep_ship_init(hulk, 9);              /* 7b9f: the Cobra left behind */
    hulk->b[0x33] = 0;
    /* half a turn (2048 a turn), and the rear view on */
    g->space.player_angle[0] = (uint16_t)(g->space.player_angle[0] + 0x400);
    g->space.extra_angle = 0x400;
    f->ap_flag = 1;
    f->speed = 0x14;
    f->moved = 1;
    *equip(g, 6) = 0;
    ep_player_velocity(g);
    for (int k = 0; k < 12; k++) ep_player_move(g);
    for (int k = 0; k < 17; k++) g->cmdr.b[EP_CMDR_CARGO + 2 * k] = 0;
    g->cmdr.b[EP_CMDR_CARGO_USED] = 0;
    g->cmdr.b[EP_CMDR_LEGAL] = 0;
}

void ep_energy_bomb(ep_game *g)
{
    if (safe_zone(g)) {
        unsigned l = g->cmdr.b[EP_CMDR_LEGAL] + 40u;
        g->cmdr.b[EP_CMDR_LEGAL] = (uint8_t)(l > 0xff ? 0xff : l);
    }
    int n = (uint8_t)(g->space.ship_slots - 3);
    for (int i = 3; i < 3 + n && i < EP_OBJECTS; i++) {
        ep_object *o = &g->space.obj[i];
        if (!(o->b[EP_OBJ_FLAGS] & 1) || !(o->b[EP_OBJ_FLAGS1E] & 2)) continue;
        o->b[0x2c] = 0; /* no canisters */
        ep_explode(g, o);
    }
}

/* ---- countdowns ---- */

/* ae25: the countdown message */
static void countdown_text(ep_game *g)
{
    ep_flight *f = &g->f;
    beep(g);
    if (f->hyper_countdown == 10) {
        f->hyper_digits[0] = '1';
        f->hyper_digits[1] = '0';
    } else {
        f->hyper_digits[0] = ' ';
        f->hyper_digits[1] = (uint8_t)(f->hyper_countdown + '0');
    }
    message(g, 0xae43, 0x0a);
    f->message_shown = 0;
}

/* a16b */
static void escape_now(ep_game *g)
{
    g->f.escape_countdown = 0;
    ep_escape_capsule(g);
    message(g, 0xb0c3, 0x2d);
    sound(g, 0x18);
}

void ep_countdowns(ep_game *g)
{
    ep_flight *f = &g->f;
    if (f->hyper_text_time) f->hyper_text_time--;
    if (f->hyper_countdown && --f->hyper_tick == 0) {
        f->hyper_tick = 10;
        f->hyper_countdown--;
        countdown_text(g);
        if (!f->hyper_countdown) {
            sound(g, f->galactic_jump == 1 ? 0x1b : 0x1a);
            ep_arrive(g);
        }
    }
    if (!f->escape_countdown) return;
    if (f->energy < 0x2ff || --f->escape_countdown == 0) {
        escape_now(g);
        return;
    }
    uint8_t c = f->escape_countdown;
    if ((c & 7) != 7) return;
    f->escape_digit = (uint8_t)((c >> 3) + '1');
    message(g, 0xb098, 8);
    f->message_shown = 0;
}

/* ---- the commands ---- */

void ep_cockpit(ep_game *g)
{
    ep_flight *f = &g->f;
    if (f->other_screen) {
        static const struct {
            uint8_t id;
            int16_t x, y;
        } cockpit[] = { { 0x0c, 0, 0x95 },    { 0x0d, 0, 0xa0 }, { 0x0e, 0xe0, 0xa0 },  { 0x0f, 0, 0xc1 },
                        { 0x00, 0x60, 0xa0 }, { 0x30, 0, 0x85 }, { 0x31, 0x130, 0x85 }, { 0x6c, 0, 0 },
                        { 0x82, 0, 9 },       { 0x83, 0x138, 9 } };
        for (size_t k = 0; k < sizeof cockpit / sizeof cockpit[0]; k++)
            ep_render_sprite(&g->render, cockpit[k].id, cockpit[k].x, cockpit[k].y);
        f->other_screen = 0;
        memset(f->dash, 0x80, sizeof f->dash); /* 5490: the dashboard to be drawn again */
        f->message_time = (uint16_t)(f->message_time & 0xff);
        f->message_shown = 0;
    }
}

/* the way back to the space view, front, from a command (a1db, a2f8, a398) */
static int back_to_flight(ep_game *g)
{
    ep_flight *f = &g->f;
    ep_cockpit(g);
    g->space.extra_angle = 0;
    ep_dust_reset(g);
    f->screen = 0;
    f->screen_shown = 0xff;
    return EP_CMD_RESTART;
}

/* a1cf: the next view: front, rear, left, right */
int ep_view_command(ep_game *g)
{
    ep_flight *f = &g->f;
    f->screen_flag = 0;
    if (f->other_screen) return back_to_flight(g);
    if (f->scoop_lock || f->ap_flag == 1) return EP_CMD_STAY;
    /* the view is the extra rotation's high byte: 0 front, 4 rear, 2 left, 6 right; anything
     * else goes to the front with the low byte sign-extended (cbw) */
    uint16_t v = g->space.extra_angle;
    uint8_t hi = (uint8_t)(v >> 8);
    if (hi == 0)
        v = (uint16_t)(0x400 | (v & 0xff));
    else if (hi == 4)
        v = (uint16_t)(0x200 | (v & 0xff));
    else if (hi == 2)
        v = (uint16_t)(0x600 | (v & 0xff));
    else
        v = (uint16_t)(int8_t)v;
    g->space.extra_angle = v;
    ep_dust_reset(g);
    return EP_CMD_STAY;
}

/* a557: the docking computer on or off (50.0 Cr a time without one fitted) */
static void docking_computer(ep_game *g)
{
    ep_flight *f = &g->f;
    if (f->autopilot == 1) {
        f->autopilot = 0;
        f->ap_flag = 0;
        if (!f->speed) {
            f->speed = 4;
            f->moved = 1;
        }
        beep(g);
        message(g, 0xafa1, 0x19);
        return;
    }
    if (!safe_zone(g)) {
        beep(g);
        message(g, 0xafd8, 0x19);
        return;
    }
    if (g->space.obj[2].b[EP_OBJ_FLAGS1E] & 1) { /* the station is hostile */
        beep(g);
        message(g, 0xb234, 0x19);
        return;
    }
    if (*equip(g, 9) != 1 && !ep_pay(g, 0x1f4)) {
        message(g, 0xaff7, 0x28);
        return;
    }
    f->autopilot_in = 0;
    f->autopilot_step = 0;
    f->autopilot = 1;
    sound(g, 4); /* 4e15 */
    message(g, 0xafbd, 0x19);
}

/* a22a: launch, or (in flight) the docking computer */
static int dock_command(ep_game *g)
{
    ep_flight *f = &g->f;
    f->screen_bits = 0;
    f->screen_flag = 0;
    if (f->other_screen) {
        if (f->screen == 1) { /* at the station: 7294, then the launch (a027) */
            f->energy = 0x3ff;
            f->fore_shield = 0xff;
            f->aft_shield = 0xff;
            f->sun_size = 0x0c;
            f->altitude = 0xff;
            f->dead = 0;
            f->missile_alert = 0;
            f->warn_time = 0;
            f->laser_temp = 0;
            f->target_note = 0;
            f->message_time = 0;
            f->force_misjump = 0;
            f->tribbles_shown = 0;
            ep_launch(g);
            return EP_CMD_RESTART;
        }
        if (f->station_angry) return EP_CMD_STAY;
        back_to_flight(g);
        if (!f->scoop_lock && !f->no_crash && !f->station_angry) docking_computer(g); /* a277 */
        return EP_CMD_RESTART;
    }
    if (f->scoop_lock || f->no_crash || f->station_angry) return EP_CMD_STAY;
    docking_computer(g);
    return EP_CMD_RESTART;
}

/* the siege: the station is angry and the Thargoids are around (a2b0, a323) */
static int under_siege(const ep_game *g)
{
    return ((g->f.station_hit ^ 1) & g->f.station_angry & g->f.siege) != 0;
}

/* 7624: the system under the chart cursor becomes the target */
static void target_selected(ep_game *g)
{
    ep_select_system(g);
    g->cmdr.b[EP_CMDR_TARGET] = g->cmdr.b[EP_CMDR_SELECTED + EP_SYSREC_INDEX];
    memcpy(g->f.hyper_target, &g->cmdr.b[EP_CMDR_SELECTED], sizeof g->f.hyper_target);
}

/* a293: the galactic hyperdrive */
static int galactic_jump(ep_game *g)
{
    ep_flight *f = &g->f;
    if (f->autopilot == 1 || *equip(g, 10) != 1 || f->hyper_text_time || f->hyper_countdown)
        return EP_CMD_STAY;
    if (under_siege(g)) {
        message(g, 0xaef2, 0x23);
    } else {
        f->galactic_jump = 1;
        message(g, 0xae26, 0x28);
        f->hyper_text_time = 0x28;
        f->hyper_countdown = 0x0b;
        f->hyper_tick = 0x0a;
        ep_rings_start(g);
        f->target_note = 0;
        target_selected(g);
    }
    return back_to_flight(g);
}

/* a314: hyperspace to the selected system */
static int hyperspace(ep_game *g)
{
    ep_flight *f = &g->f;
    if (f->autopilot == 1 || f->hyper_countdown) return EP_CMD_STAY;
    uint16_t dist = (uint16_t)(g->cmdr.b[EP_CMDR_SELECTED + EP_SYSREC_DIST] |
                               g->cmdr.b[EP_CMDR_SELECTED + EP_SYSREC_DIST + 1] << 8);
    uint8_t fuel = g->cmdr.b[EP_CMDR_FUEL];
    if (under_siege(g))
        message(g, 0xaef2, 0x23);
    else if (!dist)
        message(g, 0xae83, 0x23); /* no target */
    else if (dist >= 0x47)
        message(g, 0xae62, 0x23); /* out of range */
    /* the fuel's range is fuel * 10 / 36 (24h), a jump costs dist * 36 / 10 (at least 1) */
    else if ((uint8_t)(fuel * 10u / 0x24) < (uint8_t)dist)
        message(g, 0xaea3, 0x23); /* not enough fuel */
    else {
        uint8_t cost = (uint8_t)((uint16_t)((uint8_t)dist * 0x24u) / 10);
        f->jump_fuel = cost ? cost : 1;
        f->hyper_countdown = 0x0a;
        f->hyper_tick = 0x0a;
        countdown_text(g);
        ep_rings_start(g);
        f->target_note = 0;
        target_selected(g);
    }
    return back_to_flight(g);
}

/* a3b4: arm or disarm a missile */
static void arm_missile(ep_game *g)
{
    ep_flight *f = &g->f;
    if (f->target_note == 2) {
        f->target_note = 0;
        message(g, 0xb088, 0x23);
    } else if (f->target_note == 0 && *equip(g, 0) && !f->hyper_countdown) {
        f->target_note = 1;
        message(g, 0xb079, 0x23);
    }
}

/* a41f: fire the locked missile (it may jam) */
static void fire_missile(ep_game *g)
{
    ep_flight *f = &g->f;
    if (f->target_note != 2 || f->missile_block == 1) return;
    if (ep_flight_random(g) < 0x1f4) {
        f->missile_block = 1;
        message(g, 0xb1f9, 0x23);
        return;
    }
    f->target_note = 0;
    (*equip(g, 0))--;
    message(g, 0xb068, 0x23);
    ep_launch_missile(g);
    sound(g, 0x0e);
}

/* a464: the escape capsule: now when energy is low, else a countdown (again: cancel) */
static void escape_command(ep_game *g)
{
    ep_flight *f = &g->f;
    if (*equip(g, 6) != 1 || f->no_crash) return;
    if (f->escape_countdown)
        f->escape_countdown = 0;
    else if (f->energy > 0x2ff)
        f->escape_countdown = 0x28;
    else {
        ep_escape_capsule(g);
        message(g, 0xb0c3, 0x2d);
        sound(g, 0x18);
    }
}

/* a544 */
static void use_energy(ep_game *g, uint16_t v)
{
    if (g->f.energy < v) {
        g->f.energy = 0;
        g->f.dead = 1;
    } else {
        g->f.energy = (uint16_t)(g->f.energy - v);
    }
}

/* a4a0: the ECM */
static void ecm(ep_game *g)
{
    if (*equip(g, 2) != 1) return;
    g->f.ecm_shown = 1;
    message(g, 0xaf85, 0x19);
    sound(g, 3);
    for (int i = 0; i < g->space.ship_slots && i < EP_OBJECTS; i++) { /* 7e86 */
        ep_object *m = &g->space.obj[i];
        if ((m->b[EP_OBJ_FLAGS] & 1) && ((m->b[EP_OBJ_FLAGS] >> 1) & 0x1f) == 20) m->b[EP_OBJ_FLAGS] &= 0xfe;
    }
    use_energy(g, 20);
}

/* a4c4 */
static void bomb(ep_game *g)
{
    if (*equip(g, 7) != 1) return;
    *equip(g, 7) = 0;
    sound(g, 0x0d);
    ep_sound_marked(g, 0x12); /* 4df5 */
    ep_energy_bomb(g);
}

/* a4da: the masking device: everyone forgets the player */
static void masking(ep_game *g)
{
    ep_flight *f = &g->f;
    f->energy = f->energy >= 0x78 ? (uint16_t)(f->energy - 0x78) : 0;
    f->ai_hold = 9;
    sound(g, 5);
    for (int i = 0; i < g->space.ship_slots && i < EP_OBJECTS; i++) {
        ep_object *o = &g->space.obj[i];
        o->b[0x17] = 0;
        o->b[EP_OBJ_FLAGS1E] &= 0xfe;
        o->b[0x30] = o->b[0x30] >= 8 ? (uint8_t)(o->b[0x30] - 8) : 0;
    }
}

/* a510 */
static void anti_ecm(ep_game *g)
{
    ep_flight *f = &g->f;
    f->energy_drain ^= 1;
    uint16_t text = 0xaf6f;
    if (f->energy_drain == 1) {
        sound(g, 6);
        text = 0xaf5c;
    }
    message(g, text, 0x19);
}

/* ---- the pause menu ---- */

int ep_pause_open(ep_game *g)
{
    ep_flight *f = &g->f;
    uint8_t s = f->screen;
    if (s == 0 || s == 2)
        f->screen = 3;
    else if (s == 1)
        f->screen = 4;
    else
        return EP_CMD_STAY; /* no pause within the pause */
    f->pause_screen = s;
    sound(g, 4);                    /* 4e15 */
    ep_event_add(g, EP_EV_KEEP, 2); /* 397c: the top line kept */
    ep_render_sprite(&g->render, 0x6c, 0, 0);
    uint8_t t[96];
    int n = ep_ds_text(g, 0x095c, t, sizeof t);
    ep_pen(&g->render, (int16_t)(0xa0 - (ep_text_width(t) >> 1)), 0, 0x0f);
    ep_text(&g->render, t, n, 1);
    g->in.last_key = 0xff;
    return EP_CMD_PAUSE;
}

int ep_pause_idle(ep_game *g)
{
    ep_flight *f = &g->f;
    ep_key_bar(g);
    f->space_pressed = 0;
    int r = ep_commands(g);
    if (r != EP_CMD_STAY) return r;
    if (!f->space_pressed) return EP_CMD_STAY;
    ep_event_add(g, EP_EV_PUT_BACK, 2); /* 0492: the top line put back */
    f->screen = f->pause_screen;
    f->laser_hold = 1;
    ep_key_bar(g);
    return EP_CMD_RESUME;
}

static int run(ep_game *g, uint8_t id)
{
    /* the original's handler for each id: what EP_EV_UNPORTED reports for one not ported */
    static const uint16_t handler[37] = {
        0x04b1, 0xa22a, 0x9048, 0x8bea, 0x5ac0, 0xa1cf, 0xa4a0, 0xa3b4, 0xa41f, 0xa4c4,
        0xa464, 0xa5de, 0x96de, 0x8880, 0xa314, 0xa293, 0x5dc2, 0x6189, 0x5d9f, 0x924a,
        0x07aa, 0x08ab, 0x0674, 0x0736, 0x0779, 0x062c, 0x0637, 0x0642, 0x064d, 0x0658,
        0x0a92, 0x0ad5, 0x9781, 0x932f, 0x9563, 0xa4da, 0xa510,
    };
    switch (id) {
    case 0x01: return dock_command(g);
    case 0x05: return ep_view_command(g);
    case 0x06: ecm(g); return EP_CMD_STAY;
    case 0x07: arm_missile(g); return EP_CMD_STAY;
    case 0x08: fire_missile(g); return EP_CMD_STAY;
    case 0x09: bomb(g); return EP_CMD_STAY;
    case 0x0a: escape_command(g); return EP_CMD_STAY;
    case 0x0b: /* a5de: the jump drive */
        g->f.jump_speed ^= 1;
        g->f.moved = 1;
        g->f.jump_new = 1;
        return EP_CMD_STAY;
    case 0x0e: return hyperspace(g);
    case 0x0f: return galactic_jump(g);
    case 0x02: ep_market_screen(g); return EP_CMD_SCREEN;
    case 0x03: ep_status_screen(g); return EP_CMD_SCREEN;
    case 0x04: return ep_chart_screen(g);
    case 0x0d: ep_data_screen(g); return EP_CMD_SCREEN;
    case 0x10: ep_chart_find(g); return EP_CMD_STAY;
    case 0x11: ep_chart_find_name(g); return EP_CMD_SCREEN; /* a name to type: the caller routes keys */
    case 0x12: ep_chart_home(g); return EP_CMD_STAY;
    case 0x0c: ep_market_buy(g); return EP_CMD_STAY;
    case 0x13: ep_equipment_screen(g); return EP_CMD_SCREEN;
    case 0x21: return ep_equipment_buy(g) ? EP_CMD_SCREEN : EP_CMD_STAY; /* a mount to pick: a dialogue */
    case 0x22: return ep_equipment_sell(g) ? EP_CMD_SCREEN : EP_CMD_STAY;
    case 0x20: ep_market_sell(g); return EP_CMD_STAY;
    case 0x19:
    case 0x1a:
    case 0x1b:
    case 0x1c: /* 062c..064d: the steering and invert options */
        if (id == 0x19)
            g->f.opt_reverse_stop ^= 1;
        else if (id == 0x1a)
            g->f.opt_self_centre ^= 1;
        else if (id == 0x1b)
            g->f.opt_invert_pitch ^= 1;
        else
            g->f.opt_invert_both ^= 1;
        g->f.screen_shown = 0xff;
        return EP_CMD_STAY;
    case 0x1d: /* 0658: sound on or off */
        g->f.sound_off ^= 1;
        g->f.sound_mode = 5; /* the sequencer stopped, the speaker on with the next note */
        g->f.screen_shown = 0xff;
        ep_music_switch(g, g->f.sound_off); /* 4d6c */
        return EP_CMD_STAY;
    case 0x14: ep_save_screen(g); return EP_CMD_SCREEN;                /* 07aa */
    case 0x16: return ep_define_keys(g) ? EP_CMD_SCREEN : EP_CMD_STAY; /* 0674 */
    case 0x17: return ep_joystick(g) ? EP_CMD_SCREEN : EP_CMD_STAY;    /* 0736 */
    case 0x18: return ep_mouse(g) ? EP_CMD_SCREEN : EP_CMD_STAY;       /* 0779: none: no dialogue */
    case 0x15: ep_load_screen(g); return EP_CMD_SCREEN;                /* 08ab */
    case 0x1e: return ep_station_ask(g, 0x0451, EP_ASK_ABANDON);       /* 0a92 */
    case 0x1f: return ep_station_ask(g, 0x0445, EP_ASK_EXIT);          /* 0ad5 */
    case 0x23: masking(g); return EP_CMD_STAY;
    case 0x24: anti_ecm(g); return EP_CMD_STAY;
    default: ep_event_add(g, EP_EV_UNPORTED, id < 37 ? handler[id] : id); return EP_CMD_SCREEN;
    }
}

int ep_commands(ep_game *g)
{
    for (;;) {
        uint8_t key = g->in.last_key; /* 0276 */
        if (key == 0xff) return EP_CMD_STAY;
        g->in.last_key = 0xff;
        g->f.last_cmd_key = key; /* 03c6 */
        int slot;
        if (key == 0x20) {
            g->f.space_pressed = 1;
            continue;
        } else if (key == 0x1b) {
            int r = ep_pause_open(g);
            if (r != EP_CMD_STAY) return r;
            continue;
        } else if (key >= 0x97 && key <= 0xa2) { /* F1..F12, as the key table (ds:0cad) codes them */
            slot = key - 0x97;
        } else if (key >= '1' && key <= '9') {
            slot = key - '1';
        } else if (key == '0') {
            slot = 9;
        } else if (key == '-') {
            slot = 10;
        } else if (key == '=') {
            slot = 11;
        } else {
            continue;
        }
        uint8_t id = g->f.bar_active[slot];
        if (!id) continue;
        sound(g, 4); /* 4e15 */
        int r = run(g, id);
        if (r != EP_CMD_STAY) return r;
    }
}
