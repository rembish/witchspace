/* Elite Plus ships: spawning, the AI frame (77e0), explosions, reconstructed from ELITE.EXE.
 * Notes: re/SHIPS.md. */
#ifndef EP_SHIPS_H
#define EP_SHIPS_H

#include "ep_game.h"

/* 7d14: a slot from spawn table entry n (ep_spawn) */
void ep_ship_init(ep_object *o, int entry);

/* 7df2: the two angles that point along (x, y, z) (uses rotation slot 5) */
void ep_aim(ep_game *g, int16_t x, int16_t y, int16_t z, uint16_t *a, uint16_t *c);

/* 7e32: per-frame velocity (+19..+1b) from the angles and speed (uses rotation slots 3, 4);
 * returns DX as it leaves it (the last rounded product) */
uint16_t ep_ship_velocity(ep_game *g, ep_object *o);

/* 7e58: move by the velocity; leaving the 16-bit range removes it */
void ep_ship_move(ep_object *o);

/* 80ae: the first free ship slot (3 .. ds:7fde - 1), or NULL */
ep_object *ep_free_ship_slot(ep_game *g);

/* 81c7: a debris particle's frame; 0 when it is gone */
int ep_particle_update(ep_object *o);

/* 7ea8: a ship explodes: debris and canisters */
void ep_explode(ep_game *g, ep_object *o);

/* 77e0: every object's AI (the class handlers, re/AI.md), then new ships by the system's
 * government, missions and witchspace. DL at entry is g->f.reg_dl. */
void ep_ai_frame(ep_game *g);

#endif
