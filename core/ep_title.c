/* Elite Plus title screen, reconstructed from ELITE.EXE (see ep_title.h). */
#include "ep_title.h"

#include "ep_tables.h"
#include "ep_world.h"

#include <string.h>

static void set16(ep_object *o, int off, uint16_t v)
{
    o->b[off] = (uint8_t)v;
    o->b[off + 1] = (uint8_t)(v >> 8);
}

static uint16_t get16(const ep_object *o, int off) { return (uint16_t)(o->b[off] | o->b[off + 1] << 8); }

void ep_title_init(ep_title *t)
{
    ep_space *s = &t->g.space;
    memset(s->player_angle, 0, sizeof s->player_angle);
    s->extra_angle = 0;
    s->in_flight = 0;
    s->count = 3;
    ep_object *o = &s->obj[EP_TITLE_SLOT];
    set16(o, 0x0c, 0);
    set16(o, 0x0e, 0);
    set16(o, EP_OBJ_POS, 0);
    set16(o, EP_OBJ_POS + 2, 0);
    set16(o, EP_OBJ_POS + 4, 5000);
    set16(o, EP_OBJ_POS_HI, 0);
    o->b[EP_OBJ_POS_HI + 2] = 0;
    t->ship_type = ep_title_ships[0];
    t->list_pos = 0;
    t->hold = 0;
}

int ep_title_frame(ep_title *t, int space)
{
    /* 9f2a: the red disc, jittered (ds:108f = 1) */
    t->disc.n = 0;
    ep_draw_circle(&t->g.rng, 0xc8, 0x3c, 0x19, 1, 0, t->g.f.video == 2, &t->disc);

    /* 9f47: the ship's distance */
    ep_object *o = &t->g.space.obj[EP_TITLE_SLOT];
    uint16_t z = get16(o, EP_OBJ_POS + 4);
    if (t->hold == 0 && (uint16_t)(z - 0x50) >= ep_title_min_dist[t->ship_type & 31]) {
        set16(o, EP_OBJ_POS + 4, (uint16_t)(z - 0x50));
    } else if (++t->hold >= 0x78) {
        t->hold--;
        z = (uint16_t)(z + 100);
        set16(o, EP_OBJ_POS + 4, z);
        if (z >= 5000) {
            set16(o, EP_OBJ_POS + 4, 5000);
            if (++t->list_pos >= EP_TITLE_SHIPS) t->list_pos = 0;
            t->ship_type = ep_title_ships[t->list_pos];
            t->hold = 0;
        }
    }
    /* 9fab: type, spin */
    o->b[EP_OBJ_FLAGS1E] = 2;
    o->b[EP_OBJ_FLAGS] = (uint8_t)(t->ship_type << 1 | 1);
    set16(o, 0x0e, (uint16_t)(get16(o, 0x0e) + 0x1e));
    set16(o, 0x0c, (uint16_t)(get16(o, 0x0c) + 0x14));
    set16(o, 0x0a, (uint16_t)(get16(o, 0x0a) + 0x19));
    /* 9fc3: name and "Press spacebar to start game" are text; 3921: flashing colour */
    if (++t->flash == 6) t->flash = 0;
    /* 4154 */
    int drawn[EP_OBJECTS];
    t->g.render.nprim = 0;
    t->g.circles.n = 0;
    ep_world_update(&t->g, drawn);
    /* 301a: wait until two ticks after the last flip */
    if (t->g.clock < t->g.flip + 2) t->g.clock = t->g.flip + 2;
    t->g.flip = t->g.clock;
    /* 03c0: space starts the game */
    return space;
}
