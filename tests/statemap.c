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
    F(0x54c1, f.planet_size, 0),
    F(0x54c3, f.sun_heat, 0),
    F(0x0aa4, f.surface, 0),
    F(0x0aa6, f.surface_count, 0),
    F(0x83b5, f.atmosphere, 0),
    F(0x8058, f.message, 0),
    F(0x805a, f.message_time, 0),
    F(0x76bd, f.dead, 0),
    F(0xae23, f.no_crash, 0),
    F(0xb126, f.scoop_lock, 0),
    F(0x10bc, f.video, 0),
};

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
}

void state_store(const ep_game *g, uint8_t ds[DS_SIZE])
{
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
}
