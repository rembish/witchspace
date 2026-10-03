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
    F(0x0aa4, f.surface, 0),
    F(0x0aa6, f.surface_count, 0),
    F(0x83b5, f.atmosphere, 0),
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
