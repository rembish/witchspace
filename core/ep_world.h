/* Elite Plus objects in flight: update_objects (4154) over the whole game state, with the
 * planet, the sun and the scanner, reconstructed from ELITE.EXE. */
#ifndef EP_WORLD_H
#define EP_WORLD_H

#include "ep_game.h"

/* update_objects (4154). Ships drawn go to g->render, planet and sun spans to g->circles,
 * slot numbers drawn (planets/sun first, then ships) to drawn[]; returns their number. */
int ep_world_update(ep_game *g, int drawn[EP_OBJECTS]);

#endif
