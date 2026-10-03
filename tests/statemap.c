/* Where each part of ep_game lives in the original's data segment (see statemap.h). */
#include "statemap.h"

#include <stddef.h>
#include <string.h>

typedef struct {
    uint16_t ds;   /* offset in the data segment */
    uint16_t size; /* bytes, little-endian scalars or raw blocks */
    size_t off;    /* offsetof(ep_game, ...) */
    int raw;       /* 1: copy bytes as they are; 0: a little-endian integer of `size` bytes */
} field;

#define F(ds, member, raw) { ds, (uint16_t)sizeof(((ep_game *)0)->member), offsetof(ep_game, member), raw }

static const field fields[] = {
    F(0x82db, cmdr.b, 1),
    F(0x76de, space.obj, 1),
    F(0x76b5, space.count, 0),
    F(0x76d8, space.player_angle[0], 0),
    F(0x76da, space.player_angle[1], 0),
    F(0x76dc, space.player_angle[2], 0),
    F(0xb0de, space.extra_angle, 0),
    F(0xaf18, space.in_flight, 0),
    F(0x76be, space.rot[0].sin, 0),
    F(0x76c0, space.rot[0].cos, 0),
    F(0x76c2, space.rot[1].sin, 0),
    F(0x76c4, space.rot[1].cos, 0),
    F(0x76c6, space.rot[2].sin, 0),
    F(0x76c8, space.rot[2].cos, 0),
    F(0x76ca, space.rot[3].sin, 0),
    F(0x76cc, space.rot[3].cos, 0),
    F(0x76ce, space.rot[4].sin, 0),
    F(0x76d0, space.rot[4].cos, 0),
    F(0x76d2, space.rot[5].sin, 0),
    F(0x76d4, space.rot[5].cos, 0),
    F(0x0205, rng.w[0], 0),
    F(0x0207, rng.w[1], 0),
    F(0x0209, rng.w[2], 0),
    F(0x020b, rng.w[3], 0),
    F(0x45e0, clock, 0),
    F(0x267c, flip, 0),
    F(0x28e6, render.vtx, 1),
    F(0x83a4, f.hyperspace, 0),
    F(0x83ae, f.approach, 0),
    F(0x83ad, f.approach_size, 0),
    F(0x54c1, f.sun_size, 0),
    F(0x54c3, f.altitude, 0),
    F(0x0aa4, f.tribbles_shown, 0),
    F(0x0aa6, f.tribble_sprites, 0),
    F(0x83b5, f.tribbles, 0),
    F(0x0aa8, f.tribble, 1),
    F(0x8058, f.message, 0),
    F(0x805a, f.message_time, 0),
    F(0x76bd, f.dead, 0),
    F(0xae23, f.no_crash, 0),
    F(0xb126, f.scoop_lock, 0),
    F(0x10bc, f.video, 0),
    F(0x8056, f.message_shown, 0),
    F(0x83a5, f.leak_countdown, 0),
    F(0x83a6, f.leak, 0),
    F(0x54c8, f.energy, 0),
    F(0xb139, f.energy_drain, 0),
    F(0x54c2, f.laser_temp, 0),
    F(0xb3d3, f.laser_hold, 0),
    F(0xb125, f.pulse_phase, 0),
    F(0xb0e3, f.laser_fired, 0),
    F(0xb0e4, f.firing, 0),
    F(0x81f4, f.warn_time, 0),
    F(0x81f5, f.warn_index, 0),
    F(0x81f2, f.warn_message, 0),
    F(0x8892, f.missile_alert, 0),
    F(0xaf56, f.speed, 0),
    F(0xaf58, f.moved, 0),
    F(0xaf14, f.autopilot, 0),
    F(0xae20, f.jump_new, 0),
    F(0xaf15, f.autopilot_in, 0),
    F(0x09d1, f.roll, 0),
    F(0x09d2, f.pitch, 0),
    F(0x09d3, f.last_x, 0),
    F(0x09d4, f.last_y, 0),
    F(0x09d5, f.accel_x, 0),
    F(0x09d6, f.accel_y, 0),
    F(0x09d7, f.steer, 0),
    F(0xb134, f.opt_reverse_stop, 0),
    F(0xb135, f.opt_self_centre, 0),
    F(0xb136, f.opt_invert_pitch, 0),
    F(0xb137, f.opt_invert_both, 0),
    F(0xaf4c, f.pitch_angle[0], 0),
    F(0xaf4e, f.pitch_angle[1], 0),
    F(0xaf50, f.velocity[0], 0),
    F(0xaf52, f.velocity[1], 0),
    F(0xaf54, f.velocity[2], 0),
    F(0xb0dd, f.jump_speed, 0),
    F(0x83a0, f.mission, 0),
    F(0x83a2, f.mission_state, 0),
    F(0x83aa, f.station_angry, 0),
    F(0x83ab, f.station_hit, 0),
    F(0xae22, f.mining, 0),
    F(0x54ca, f.target_note, 0),
    F(0xb0e1, f.target_slot, 0),
    F(0x54b9, f.beam_flip, 0),
    F(0x54ba, f.beam_pair, 0),
    F(0x54bb, f.beam_colour, 0),
    F(0x7680, f.safe_zone, 0),
    F(0xb0e0, f.ap_flag, 0),
    F(0x805c, f.bounty_text, 1),
    F(0x7613, f.docked, 0),
    F(0x7612, f.under_fire, 0),
    F(0x7610, f.attacker, 0),
    F(0x7681, f.hit_from_behind, 0),
    F(0x54c4, f.fore_shield, 0),
    F(0x54c5, f.aft_shield, 0),
    F(0x54cb, f.status, 0),
    F(0x5314, f.dust, 1),
    F(0x53e6, f.dust_old, 1),
    F(0x54b8, f.dust_shift, 0),
    F(0x54c0, f.ecm_shown, 0),
    F(0x8730, f.class_count, 1),
    F(0xb138, f.ai_hold, 0),
    F(0x8891, f.station_ecm, 0),
    F(0x45e7, f.sound_off, 0),
    F(0xae21, f.launching, 0),
    F(0xaf59, f.ap_roll, 0),
    F(0xaf5b, f.ap_passes, 0),
    F(0x76b7, f.death_vel, 1),
    F(0x8711, f.other_screen, 0),
    F(0x02f9, f.screen, 0),
    F(0x02fa, f.screen_shown, 0),
    F(0x02fe, f.bar_colour, 0),
    F(0x0300, f.bar_redraw, 0),
    F(0x0301, f.bar_active, 1),
    F(0x030d, f.bar_wanted, 1),
    F(0x0319, f.space_pressed, 0),
    F(0x031d, f.screen_flag, 0),
    F(0x031e, f.screen_bits, 0),
    F(0xae25, f.hyper_text_time, 0),
    F(0xae5d, f.hyper_digits, 1),
    F(0xb3d5, f.escape_countdown, 0),
    F(0xb0c1, f.escape_digit, 0),
    F(0xaf17, f.autopilot_step, 0),
    F(0xae60, f.hyper_countdown, 0),
    F(0xae61, f.hyper_tick, 0),
    F(0xb1f8, f.missile_block, 0),
    F(0x5503, seed.w, 1),
    F(0x5562, dist_text, 1),
    F(0x1b3e, f.flash, 0),
    F(0x108f, f.circle_mask, 0),
    F(0x85dc, f.rings, 1),
    F(0xae24, f.galactic_jump, 0),
    F(0x8610, f.force_misjump, 0),
    F(0x8611, f.hyper_target, 1),
    F(0x82d6, f.jump_fuel, 0),
    F(0x829b, f.galaxy_digit, 0),
    F(0x0d2f, in.last_key, 0),
    F(0x8081, f.lock_text, 1),
    F(0x4801, f.sound_device, 0),
    F(0x76b6, f.danger_gov, 0),
    F(0x8897, f.spawn_gov8, 0),
    F(0x888f, f.spawn_row, 0),
    F(0x8893, f.convoy_leader, 0),
    F(0x8896, f.exploding_station, 0),
    F(0x83a9, f.convoy_left, 0),
    F(0x83a7, f.convoy_leader_dead, 0),
    F(0x83b3, f.convoy_countdown, 0),
    F(0x83b1, f.siege, 0),
    F(0x83a3, f.mission_system, 0),
    F(0x83b0, f.mission5_phase, 0),
    F(0x839e, f.mission5_count, 0),
    F(0x839f, f.mission5_flag, 0),
    F(0x7fde, space.ship_slots, 0),
    F(0x7fdf, space.debris_slots, 0),
    F(0x020d, in.key, 1),
    F(0x8f2c, in.control, 0),
};

/* key bindings: pointers into the key table in the original, scancodes in the core */
static const uint16_t bindings[7] = { 0xb251, 0xb253, 0xb255, 0xb257, 0xb259, 0xb25b, 0xb25d };

static uint8_t *binding(ep_game *g, int k)
{
    uint8_t *b[7] = { &g->in.faster, &g->in.slower, &g->in.up,  &g->in.down,
                      &g->in.left,   &g->in.right,  &g->in.fire };
    return b[k];
}

void state_load(ep_game *g, const uint8_t ds[DS_SIZE])
{
    memset(g, 0, sizeof *g);
    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++) {
        const field *f = &fields[i];
        uint8_t *p = (uint8_t *)g + f->off;
        if (f->raw) {
            memcpy(p, ds + f->ds, f->size);
        } else {
            uint32_t v = 0;
            for (int k = f->size - 1; k >= 0; k--) v = v << 8 | ds[(uint16_t)(f->ds + k)];
            memcpy(p, &v, f->size); /* little-endian hosts only, as the tests are */
        }
    }
    for (int k = 0; k < 7; k++)
        *binding(g, k) = (uint8_t)((ds[bindings[k]] | ds[bindings[k] + 1] << 8) - 0x20d);
}

void state_store(const ep_game *g, uint8_t ds[DS_SIZE])
{
    for (int k = 0; k < 7; k++) {
        uint16_t p = (uint16_t)(0x20d + *binding((ep_game *)g, k));
        ds[bindings[k]] = (uint8_t)p;
        ds[bindings[k] + 1] = (uint8_t)(p >> 8);
    }
    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++) {
        const field *f = &fields[i];
        const uint8_t *p = (const uint8_t *)g + f->off;
        if (f->raw) {
            memcpy(ds + f->ds, p, f->size);
        } else {
            uint32_t v = 0;
            memcpy(&v, p, f->size);
            for (int k = 0; k < f->size; k++) ds[(uint16_t)(f->ds + k)] = (uint8_t)(v >> (8 * k));
        }
    }
}

void state_mask(uint8_t mask[DS_SIZE])
{
    memset(mask, 0, DS_SIZE);
    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++)
        memset(mask + fields[i].ds, 1, fields[i].size);
    for (int k = 0; k < 7; k++) memset(mask + bindings[k], 1, 2);
}
